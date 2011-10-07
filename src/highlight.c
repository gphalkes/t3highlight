/* Copyright (C) 2011 G.P. Halkes
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License version 3, as
   published by the Free Software Foundation.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <stdlib.h>
#include <string.h>
#include <pcre.h>

#include "highlight.h"
#include "highlight_errors.h"
#include "vector.h"

#ifdef USE_GETTEXT
#include <libintl.h>
#define _(x) dgettext("LIBT3", (x))
#else
#define _(x) (x)
#endif

typedef struct {
	pcre *regex;
	pcre_extra *extra;
	int next_state,
		attribute_idx;
} pattern_t;

typedef struct {
	VECTOR(pattern_t, patterns);
	int attribute_idx;
} state_t;

struct t3_highlight_t {
	VECTOR(state_t, states);
};

struct t3_highlight_match_t {
	size_t start,
		end;
	int state, forbidden_state,
		begin_attribute,
		match_attribute;
};

#define RETURN_ERROR(x) do { if (error != NULL) *error = (x); goto return_error; } while (0)

static const state_t null_state = { { NULL, 0, 0 }, 0 };

static const char syntax_schema[] = {
#include "syntax.bytes"
};

typedef struct {
	int (*map_style)(void *, const char *);
	void *map_style_data;
	t3_highlight_t *highlight;
	t3_config_t *syntax;
} pattern_context_t;

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error);
static void free_state(state_t *state);

t3_highlight_t *t3_highlight_load(const char *name, int (*map_style)(void *, const char *), void *map_style_data, int *error) {
	t3_config_opts_t opts;
	const char *path[] = { DATADIR, NULL };
	t3_config_t *config;
	t3_config_error_t config_error;
	t3_highlight_t *result;
	FILE *file;

	/* FIXME: do we want to add a path from the environment? */
	/* FIXME: allow use of name without extension to allow lookup from config file. */

	if ((file = t3_config_open_from_path(path, name, 0)) == NULL) {
		if (error != NULL)
			*error = T3_ERR_ERRNO;
		return NULL;
	}

	opts.flags = T3_CONFIG_INCLUDE_DFLT;
	opts.include_callback.dflt.path = path;
	opts.include_callback.dflt.flags = 0;

	config = t3_config_read_file(file, &config_error, &opts);
	fclose(file);
	if (config == NULL) {
		if (error != NULL)
			*error = config_error.error;
		return NULL;
	}

	result = t3_highlight_new(config, map_style, map_style_data, error);
	t3_config_delete(config);

	return result;
}

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *), void *map_style_data, int *error) {
	t3_highlight_t *result = NULL;
	t3_config_schema_t *schema = NULL;
	t3_config_t *patterns;
	t3_config_error_t local_error;
	pattern_context_t context;

	if ((schema = t3_config_read_schema_buffer(syntax_schema, sizeof(syntax_schema), &local_error, NULL)) == NULL) {
		if (error != NULL)
			*error = local_error.error != T3_ERR_OUT_OF_MEMORY ? T3_ERR_INTERNAL : local_error.error;
		return NULL;
	}

	if (!t3_config_validate(syntax, schema, NULL, NULL))
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);

	t3_config_delete_schema(schema);
	schema = NULL;


	if (map_style == NULL)
		RETURN_ERROR(T3_ERR_BAD_ARG);

	if ((result = malloc(sizeof(t3_highlight_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	VECTOR_INIT(result->states);

	patterns = t3_config_get(syntax, "pattern");

	if (!VECTOR_RESERVE(result->states))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	VECTOR_LAST(result->states) = null_state;
	context.map_style = map_style;
	context.map_style_data = map_style_data;
	context.highlight = result;
	context.syntax = syntax;
	if (!init_state(&context, patterns, 0, error))
		goto return_error;
	return result;

return_error:
	t3_config_delete_schema(schema);
	if (result != NULL) {
		VECTOR_ITERATE(result->states, free_state);
		free(result->states.data);
		free(result);
	}
	return NULL;
}

static pcre *compile_pattern(t3_config_t *pattern, int *error) {
	const char *regex_str;
	const char *error_message;
	int error_offset;

	pcre *regex;
	if ((regex_str = t3_config_get_string(pattern)) == NULL || *regex_str == 0)
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);
	if ((regex = pcre_compile(regex_str, 0, &error_message, &error_offset, NULL)) == NULL)
		RETURN_ERROR(T3_ERR_INVALID_REGEX);

	return regex;

return_error:
	return NULL;
}

static t3_bool match_name(const t3_config_t *config, void *data) {
	return strcmp(t3_config_get_string(t3_config_get(config, "name")), data) == 0;
}

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error) {
	t3_config_t *regex, *style, *use;
	pattern_t action;
	int style_attr_idx;
	const char *study_error;

	for (patterns = t3_config_get(patterns, NULL); patterns != NULL; patterns = t3_config_get_next(patterns)) {
		style_attr_idx = (style = t3_config_get(patterns, "style")) == NULL ?
			context->highlight->states.data[idx].attribute_idx :
			context->map_style(context->map_style_data, t3_config_get_string(style));

		if ((regex = t3_config_get(patterns, "regex")) != NULL) {
			if ((action.regex = compile_pattern(regex, error)) == NULL)
				return t3_false;
			action.extra = pcre_study(action.regex, 0, &study_error);

			action.attribute_idx = style_attr_idx;
			action.next_state = idx;
		} else if ((regex = t3_config_get(patterns, "start")) != NULL) {
			t3_config_t *sub_patterns;

			action.attribute_idx = (style = t3_config_get(patterns, "delim-style")) == NULL ?
				style_attr_idx : context->map_style(context->map_style_data, t3_config_get_string(style));

			if ((action.regex = compile_pattern(regex, NULL)) == NULL)
				return t3_false;
			action.extra = pcre_study(action.regex, 0, &study_error);

			/* Create new state to which start will switch. */
			action.next_state = context->highlight->states.used;
			if (!VECTOR_RESERVE(context->highlight->states))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
			VECTOR_LAST(context->highlight->states) = null_state;
			VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;

			/* Add sub-patterns to the new state, if they are specified. */
			if ((sub_patterns = t3_config_get(patterns, "pattern")) != NULL) {
				if (!init_state(context, sub_patterns, action.next_state, error))
					return t3_false;
			}

			/* If the pattern specifies an end regex, create an extra action for that and paste that
			   to in the list of sub-patterns. Depending on whether end is specified before or after
			   the pattern list, it will be pre- or appended. */
			if ((regex = t3_config_get(patterns, "end")) != NULL) {
				pattern_t end_action;
				end_action.next_state = idx;

				if ((end_action.regex = compile_pattern(regex, error)) == NULL)
					return t3_false;
				end_action.extra = pcre_study(action.regex, 0, &study_error);

				end_action.attribute_idx = action.attribute_idx;
				if (!VECTOR_RESERVE(VECTOR_LAST(context->highlight->states).patterns))
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);

				/* Find the pattern entry, starting after the end entry. If it does not exist,
				   the list of patterns was specified first. */
				for ( ; regex != NULL; regex = t3_config_get_next(regex))
					if (strcmp(t3_config_get_name(regex), "pattern") == 0)
						break;

				if (regex == NULL && VECTOR_LAST(context->highlight->states).patterns.used > 0) {
					VECTOR_LAST(VECTOR_LAST(context->highlight->states).patterns) = end_action;
				} else {
					memmove(VECTOR_LAST(context->highlight->states).patterns.data + 1,
						VECTOR_LAST(context->highlight->states).patterns.data,
						VECTOR_LAST(context->highlight->states).patterns.used * sizeof(pattern_t));
					VECTOR_LAST(context->highlight->states).patterns.data[0] = end_action;
				}
			}
		} else if ((use = t3_config_get(patterns, "use")) != NULL) {
			t3_config_t *definition = t3_config_find(t3_config_get(context->syntax, "define"),
				match_name, (char *) t3_config_get_string(use), NULL);

			if (definition == NULL)
				RETURN_ERROR(T3_ERR_INVALID_FORMAT);

			if (!init_state(context, t3_config_get(definition, "pattern"), idx, error))
				return t3_false;

			/* We do not fill in action, so we should just skip to the next entry in the list. */
			continue;
		} else {
			RETURN_ERROR(T3_ERR_INTERNAL);
		}
		if (!VECTOR_RESERVE(context->highlight->states.data[idx].patterns))
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
		VECTOR_LAST(context->highlight->states.data[idx].patterns) = action;
	}
	return t3_true;

return_error:
	return t3_false;
}

static void free_pattern(pattern_t *pattern) {
	pcre_free(pattern->regex);
	pcre_free(pattern->extra);
}

static void free_state(state_t *state) {
	VECTOR_ITERATE(state->patterns, free_pattern);
	free(state->patterns.data);
}

void t3_highlight_free(t3_highlight_t *highlight) {
	VECTOR_ITERATE(highlight->states, free_state);
	free(highlight->states.data);
	free(highlight);
}

t3_bool t3_highlight_match(const t3_highlight_t *highlight, const char *line, size_t size, t3_highlight_match_t *result) {
	state_t *state = &highlight->states.data[result->state];
	int options = PCRE_ANCHORED;
	int ovector[30];
	size_t i, j;

	if (result->end != 0)
		options |= PCRE_NOTBOL;

	result->begin_attribute = highlight->states.data[result->state].attribute_idx;
	for (i = result->end; i <= size; i++) {
		for (j = 0; j < state->patterns.used; j++) {
			int local_options = options;

			if (state->patterns.data[j].next_state == result->state ||
					(state->patterns.data[j].next_state <= result->forbidden_state &&
					state->patterns.data[j].next_state > result->state))
				local_options |= PCRE_NOTEMPTY_ATSTART;

			if (pcre_exec(state->patterns.data[j].regex, state->patterns.data[j].extra, line + i, size - i,
					0, local_options, ovector, 30) >= 0)
			{
				result->start = i + ovector[0];
				result->end = i + ovector[1];
				/* Forbidden state is only set when we matched an empty end pattern. We recognize
				   those by checking the match start and end, and by the fact that the next
				   state is smaller than the current state (parent states are always before
				   child states in the state vector). */
				result->forbidden_state = result->start == result->end &&
					state->patterns.data[j].next_state < result->state ? result->state : -1;
				result->state = state->patterns.data[j].next_state;
				result->match_attribute = state->patterns.data[j].attribute_idx;
				return t3_true;
			}
		}
		options |= PCRE_NOTBOL;
	}

	result->start = size;
	result->end = size;
	return t3_false;
}


void t3_highlight_reset(t3_highlight_match_t *match, int state) {
	static const t3_highlight_match_t empty = { 0, 0, 0, -1, 0, 0 };
	*match = empty;
	match->state = state;
}

t3_highlight_match_t *t3_highlight_new_match(void) {
	return calloc(1, sizeof(t3_highlight_match_t));
}

void t3_highlight_free_match(t3_highlight_match_t *match) {
	free(match);
}

size_t t3_highlight_get_start(t3_highlight_match_t *match) {
	return match->start;
}

size_t t3_highlight_get_end(t3_highlight_match_t *match) {
	return match->end;
}

int t3_highlight_get_begin_attr(t3_highlight_match_t *match) {
	return match->begin_attribute;
}

int t3_highlight_get_match_attr(t3_highlight_match_t *match) {
	return match->match_attribute;
}

int t3_highlight_next_line(t3_highlight_match_t *match) {
	match->end = 0;
	match->forbidden_state = -1;
	return match->state;
}

const char *t3_highlight_strerror(int error) {
	switch (error) {
		default:
			if (error >= -80)
				return t3_config_strerror(error);
			return t3_highlight_strerror_base(error);
		case T3_ERR_INVALID_FORMAT:
			return _("invalid file format");
		case T3_ERR_INVALID_REGEX:
			return _("invalid regular expression");
	}
}
