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
#include <errno.h>

#include "highlight.h"
#include "highlight_errors.h"
#include "internal.h"

#ifdef USE_GETTEXT
#include <libintl.h>
#define _(x) dgettext("LIBT3", (x))
#else
#define _(x) (x)
#endif

struct t3_highlight_match_t {
	size_t start,
		match_start,
		end;
	int state, forbidden_state,
		begin_attribute,
		match_attribute;
};

static const state_t null_state = { { NULL, 0, 0 }, 0 };

static const char syntax_schema[] = {
#include "syntax.bytes"
};

typedef struct {
	int (*map_style)(void *, const char *);
	void *map_style_data;
	t3_highlight_t *highlight;
	t3_config_t *syntax;
	int flags;
	VECTOR(const char *, use_stack);
} pattern_context_t;

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error);
static void free_state(state_t *state);

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, int *error)
{
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

	if (!t3_config_validate(syntax, schema, NULL, 0))
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);

	t3_config_delete_schema(schema);
	schema = NULL;


	if (map_style == NULL)
		RETURN_ERROR(T3_ERR_BAD_ARG);

	if ((result = malloc(sizeof(t3_highlight_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	VECTOR_INIT(result->states);
	VECTOR_INIT(result->mapping);
	if (!VECTOR_RESERVE(result->mapping))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	memset(&VECTOR_LAST(result->mapping), 0, sizeof(state_mapping_t));

	patterns = t3_config_get(syntax, "pattern");

	if (!VECTOR_RESERVE(result->states))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	VECTOR_LAST(result->states) = null_state;
	context.map_style = map_style;
	context.map_style_data = map_style_data;
	context.highlight = result;
	context.syntax = syntax;
	context.flags = flags;
	/* FIXME: we should pre-allocate the first 256 items, such that we are unlikely to
	   ever need to reallocate while matching. */
	VECTOR_INIT(context.use_stack);
	if (!init_state(&context, patterns, 0, error)) {
		free(context.use_stack.data);
		goto return_error;
	}
	free(context.use_stack.data);

	result->flags = flags;
	result->lang_file = NULL;
	return result;

return_error:
	t3_config_delete_schema(schema);
	if (result != NULL) {
		VECTOR_ITERATE(result->states, free_state);
		free(result->states.data);
		free(result->mapping.data);
		free(result);
	}
	return NULL;
}

static t3_bool compile_pattern(t3_config_t *pattern, pattern_t *action, int flags, int *error) {
	const char *error_message;
	int error_offset, local_error;
	const char *study_error;

	if ((action->regex = pcre_compile2(t3_config_get_string(pattern), flags & T3_HIGHLIGHT_UTF8 ? PCRE_UTF8 : 0,
			&local_error, &error_message, &error_offset, NULL)) == NULL)
	{
		if (error != NULL)
			*error = local_error == 21 ? T3_ERR_OUT_OF_MEMORY : T3_ERR_INVALID_REGEX;
		return t3_false;
	}
	action->extra = pcre_study(action->regex, 0, &study_error);
	return t3_true;
}

static t3_bool match_name(const t3_config_t *config, void *data) {
	return strcmp(t3_config_get_string(t3_config_get(config, "name")), data) == 0;
}

static t3_bool add_delim_pattern(pattern_context_t *context, t3_config_t *regex, int next_state, const pattern_t *action, int *error) {
	pattern_t nest_action;
	nest_action.next_state = next_state;

	if (!compile_pattern(regex, &nest_action, context->flags, error))
		return t3_false;

	nest_action.attribute_idx = action->attribute_idx;
	if (!VECTOR_RESERVE(context->highlight->states.data[action->next_state].patterns))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);

	/* Find the pattern entry, starting after the end entry. If it does not exist,
	   the list of patterns was specified first. */
	for ( ; regex != NULL && strcmp(t3_config_get_name(regex), "pattern") != 0; regex = t3_config_get_next(regex)) {}

	if (regex == NULL && context->highlight->states.data[action->next_state].patterns.used > 0) {
		VECTOR_LAST(context->highlight->states.data[action->next_state].patterns) = nest_action;
	} else {
		memmove(context->highlight->states.data[action->next_state].patterns.data + 1,
			context->highlight->states.data[action->next_state].patterns.data,
			(context->highlight->states.data[action->next_state].patterns.used - 1) * sizeof(pattern_t));
		context->highlight->states.data[action->next_state].patterns.data[0] = nest_action;
	}
	return t3_true;
return_error:
	return t3_false;
}

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error) {
	t3_config_t *regex, *style, *use;
	pattern_t action;
	int style_attr_idx;

	for (patterns = t3_config_get(patterns, NULL); patterns != NULL; patterns = t3_config_get_next(patterns)) {
		style_attr_idx = (style = t3_config_get(patterns, "style")) == NULL ?
			context->highlight->states.data[idx].attribute_idx :
			context->map_style(context->map_style_data, t3_config_get_string(style));

		if ((regex = t3_config_get(patterns, "regex")) != NULL) {
			if (!compile_pattern(regex, &action, context->flags, error))
				return t3_false;

			action.attribute_idx = style_attr_idx;
			action.next_state = NO_CHANGE;
		} else if ((regex = t3_config_get(patterns, "start")) != NULL) {
			t3_config_t *sub_patterns;

			action.attribute_idx = (style = t3_config_get(patterns, "delim-style")) == NULL ?
				style_attr_idx : context->map_style(context->map_style_data, t3_config_get_string(style));

			if (!compile_pattern(regex, &action, context->flags, error))
				return t3_false;

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
			if ((regex = t3_config_get(patterns, "end")) != NULL)
				add_delim_pattern(context, regex, EXIT_STATE, &action, error);

			if (t3_config_get_bool(t3_config_get(patterns, "nested")))
				add_delim_pattern(context, t3_config_get(patterns, "start"), action.next_state, &action, error);
		} else if ((use = t3_config_get(patterns, "use")) != NULL) {
			size_t i;

			t3_config_t *definition = t3_config_find(t3_config_get(context->syntax, "define"),
				match_name, (char *) t3_config_get_string(use), NULL);

			if (definition == NULL)
				RETURN_ERROR(T3_ERR_UNDEFINED_USE);

			for (i = 0; i < context->use_stack.used; i++) {
				if (strcmp(t3_config_get_string(use), context->use_stack.data[i]) == 0)
					RETURN_ERROR(T3_ERR_RECURSIVE_DEFINITION);
			}

			if (!VECTOR_RESERVE(context->use_stack))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
			VECTOR_LAST(context->use_stack) = t3_config_get_string(use);

			if (!init_state(context, t3_config_get(definition, "pattern"), idx, error))
				return t3_false;

			/* Pop name of latest use from stack. */
			context->use_stack.used--;

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
	if (highlight == NULL)
		return;
	VECTOR_ITERATE(highlight->states, free_state);
	free(highlight->states.data);
	free(highlight->mapping.data);
	free(highlight->lang_file);
	free(highlight);
}

static int find_state(const t3_highlight_t *highlight, int current, int pattern) {
	size_t i;

	if (pattern == EXIT_STATE)
		return highlight->mapping.data[current].parent;
	if (pattern == NO_CHANGE)
		return current;

	/* Check if the state is already mapped. */
	for (i = current + 1; i < highlight->mapping.used; i++) {
		if (highlight->mapping.data[i].parent == current && highlight->mapping.data[i].pattern == pattern)
			return i;
	}

	if (!VECTOR_RESERVE(highlight->mapping))
		return 0;
	VECTOR_LAST(highlight->mapping).parent = current;
	VECTOR_LAST(highlight->mapping).pattern = pattern;
	return highlight->mapping.used - 1;
}

t3_bool t3_highlight_match(const t3_highlight_t *highlight, const char *line, size_t size, t3_highlight_match_t *result) {
	int current_pattern_state = highlight->mapping.data[result->state].pattern;
	state_t *state = &highlight->states.data[current_pattern_state];
	int ovector[30], best = -1, best_pos[2];
	size_t j;

	best_pos[0] = INT_MAX;

	result->start = result->end;
	result->begin_attribute = state->attribute_idx;
	for (j = 0; j < state->patterns.used; j++) {
		int options = highlight->flags & T3_HIGHLIGHT_UTF8_NOCHECK ? PCRE_NO_UTF8_CHECK : 0;

		/* For items that do not change state, we do not want an empty match
		   ever (makes no progress). For state changing items, the rules are
		   more complex. Empty matches are allowed except when immediately
		   after the previous match and both of the following conditions are met:
		   - the item we match is a "start" item (next state >= current state)
		   - taking the state transistion enters a state smaller than or
			 equal to the forbidden state (set below when we encounter an
			 empty match for an end pattern)
		*/
		if (state->patterns.data[j].next_state == NO_CHANGE)
			options |= PCRE_NOTEMPTY;
		else if (state->patterns.data[j].next_state >= current_pattern_state &&
				state->patterns.data[j].next_state <= result->forbidden_state)
			options |= PCRE_NOTEMPTY_ATSTART;

		if (pcre_exec(state->patterns.data[j].regex, state->patterns.data[j].extra, line, size,
				result->end, options, ovector, sizeof(ovector) / sizeof(ovector[0])) >= 0 && ovector[0] < best_pos[0])
		{
			best = j;
			best_pos[0] = ovector[0];
			best_pos[1] = ovector[1];
		}
	}

	if (best >= 0) {
		result->match_start = best_pos[0];
		result->end = best_pos[1];
		/* Forbidden state is only set when we matched an empty end pattern. We recognize
		   those by checking the match start and end, and by the fact that the next
		   state is EXIT_STATE. The forbidden state then is the state
		   we are leaving, such that if we next match an empty start pattern it must
		   go into a higher numbered state. This ensures we will always make progress. */
		result->forbidden_state = result->match_start == result->end &&
			state->patterns.data[best].next_state == EXIT_STATE ? current_pattern_state : -1;
		result->state = find_state(highlight, result->state, state->patterns.data[best].next_state);
		result->match_attribute = state->patterns.data[best].attribute_idx;
		return t3_true;
	}

	result->match_start = size;
	result->end = size;
	return t3_false;
}


void t3_highlight_reset(t3_highlight_match_t *match, int state) {
	static const t3_highlight_match_t empty = { 0, 0, 0, 0, -1, 0, 0 };
	*match = empty;
	match->state = state;
}

t3_highlight_match_t *t3_highlight_new_match(void) {
	t3_highlight_match_t *result = malloc(sizeof(t3_highlight_match_t));
	if (result == NULL)
		return NULL;
	t3_highlight_reset(result, 0);
	return result;
}

void t3_highlight_free_match(t3_highlight_match_t *match) {
	free(match);
}

size_t t3_highlight_get_start(t3_highlight_match_t *match) {
	return match->start;
}

size_t t3_highlight_get_match_start(t3_highlight_match_t *match) {
	return match->match_start;
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

int t3_highlight_get_state(t3_highlight_match_t *match) {
	return match->state;
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
		case T3_ERR_NO_SYNTAX:
			return _("could not locate appropriate highlighting patterns");
		case T3_ERR_UNDEFINED_USE:
			return _("'use' specifies undefined pattern");
		case T3_ERR_RECURSIVE_DEFINITION:
			return _("recursive pattern definition");
	}
}

long t3_highlight_get_version(void) {
	return T3_HIGHLIGHT_VERSION;
}
