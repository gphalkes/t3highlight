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

typedef struct {
	void *data;
	size_t allocated,
		used;
} vector_base_t;

#define VECTOR(type, name) struct { type *data; size_t allocated, used; } name
#define VECTOR_INIT(name) do { (name).data = NULL; (name).allocated = 0; (name).used = 0; } while (0)
#define VECTOR_ITERATE(name, func) do { size_t _i; for (_i = 0; _i < (name).used; _i++) func(&(name).data[_i]); } while (0)
#define VECTOR_RESERVE(name) vector_reserve((vector_base_t *) &name, sizeof((name).data[0]))
#define VECTOR_LAST(name) (name).data[(name).used - 1]

t3_bool vector_reserve(vector_base_t *vector, size_t elsize) {
	if (vector->allocated <= vector->used) {
		size_t allocate = vector->allocated == 0 ? 8 : vector->allocated * 2;
		void *data = realloc(vector->data, allocate * elsize);
		if (data == NULL)
			return t3_false;
		vector->data = data;
		vector->allocated = allocate;
	}
	vector->used++;
	return t3_true;
}

typedef struct {
	pcre *regex;
	int next_state,
		attribute_idx;
} pattern_t;

typedef struct {
	VECTOR(pattern_t, patterns);
	int attribute_idx;
} state_t;

struct t3_highlight_t {
	char *name;
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

typedef struct {
	int (*map_style)(void *, const char *);
	void *map_style_data;
	t3_highlight_t *highlight;
	t3_config_t *syntax;
} pattern_context_t;

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error);
static void free_state(state_t *state);

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *), void *map_style_data, int *error) {
	t3_highlight_t *result;
	t3_config_t *patterns;
	pattern_context_t context;
	const char *name;

	if (map_style == NULL)
		RETURN_ERROR(T3_ERR_BAD_ARG);

	if ((result = malloc(sizeof(t3_highlight_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
	result->name = NULL;
	VECTOR_INIT(result->states);

	//FIXME: when validation is implemented in libt3config, use that!

	name = t3_config_get_string(t3_config_get(syntax, "name"));
	if (name == NULL)
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);
	result->name = strdup(name);

	patterns = t3_config_get(syntax, "pattern");
	if (!t3_config_is_list(patterns))
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);

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
	VECTOR_ITERATE(result->states, free_state);
	free(result->states.data);
	free(result->name);
	free(result);
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

static t3_bool init_state(pattern_context_t *context, t3_config_t *patterns, int idx, int *error) {
	t3_config_t *regex, *style;
	pattern_t action;
	int style_attr_idx;

	for (patterns = t3_config_get(patterns, NULL); patterns != NULL; patterns = t3_config_get_next(patterns)) {
		if ((style = t3_config_get(patterns, "style")) == NULL) {
			style_attr_idx = action.attribute_idx = context->highlight->states.data[idx].attribute_idx;
		} else {
			if (t3_config_get_type(style) != T3_CONFIG_STRING)
				RETURN_ERROR(T3_ERR_INVALID_FORMAT);
			style_attr_idx = context->map_style(context->map_style_data, t3_config_get_string(style));
		}

		if ((regex = t3_config_get(patterns, "regex")) != NULL) {
			if (t3_config_get(patterns, "start") != NULL || t3_config_get(patterns, "end") != NULL)
				RETURN_ERROR(T3_ERR_INVALID_FORMAT);
			if ((action.regex = compile_pattern(regex, error)) == NULL)
				return t3_false;

			action.attribute_idx = style_attr_idx;
			action.next_state = idx;
		} else if ((regex = t3_config_get(patterns, "start")) != NULL) {
			t3_config_t *sub_patterns;
			if (t3_config_get(patterns, "regex") != NULL)
				RETURN_ERROR(T3_ERR_INVALID_FORMAT);

			if ((style = t3_config_get(patterns, "delim-style")) == NULL) {
				action.attribute_idx = style_attr_idx;
			} else {
				if (t3_config_get_type(style) != T3_CONFIG_STRING)
					RETURN_ERROR(T3_ERR_INVALID_FORMAT);
				action.attribute_idx = context->map_style(context->map_style_data, t3_config_get_string(style));
			}

			if ((action.regex = compile_pattern(regex, NULL)) == NULL)
				return t3_false;

			if ((sub_patterns = t3_config_get(patterns, "pattern")) != NULL)
				if (!t3_config_is_list(sub_patterns))
					RETURN_ERROR(T3_ERR_INVALID_FORMAT);

			action.next_state = context->highlight->states.used;
			if (!VECTOR_RESERVE(context->highlight->states))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
			VECTOR_LAST(context->highlight->states) = null_state;
			VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;
			if (!init_state(context, sub_patterns, action.next_state, error))
				return t3_false;

			if ((regex = t3_config_get(patterns, "end")) != NULL) {
				pattern_t end_action;
				end_action.next_state = idx;
				if ((end_action.regex = compile_pattern(regex, error)) == NULL)
					return t3_false;

				end_action.attribute_idx = action.attribute_idx;
				VECTOR_RESERVE(VECTOR_LAST(context->highlight->states).patterns);
				//FIXME use push front if end is defined before first %pattern
				VECTOR_LAST(VECTOR_LAST(context->highlight->states).patterns) = end_action;
			}
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
}

static void free_state(state_t *state) {
	VECTOR_ITERATE(state->patterns, free_pattern);
	free(state->patterns.data);
}

void t3_highlight_free(t3_highlight_t *highlight) {
	VECTOR_ITERATE(highlight->states, free_state);
	free(highlight->states.data);
	free(highlight->name);
	free(highlight);
}

t3_bool t3_highlight_match(const t3_highlight_t *highlight, const char *line, size_t size, t3_highlight_match_t *result) {
	state_t *state = &highlight->states.data[result->state];
	size_t best = (size_t) -1;
	int best_position = INT_MAX;
	int best_position_end;
	int options = result->end == 0 ? 0 : PCRE_NOTBOL;
	int ovector[30];
	size_t i;

	for (i = 0; i < state->patterns.used; i++) {
		if (pcre_exec(state->patterns.data[i].regex, NULL, line + result->end, size - result->end, 0, options, ovector, 30) >= 0) {
			/* Skip this match if it has zero size at position 0, and changes the state to
			   a state that we came from, or stays in the current state. */
			/*FIXME: if we use the PCRE_ANCHORED stuff we can avoid some of this check by
				adding PCRE_NOTEMPTY_ATSTART for non-delim patterns. */
			if (ovector[0] == 0 && ovector[1] == 0 && (state->patterns.data[i].next_state == result->forbidden_state ||
					state->patterns.data[i].next_state == result->state))
				continue;
			if (ovector[0] < best_position) {
				best_position = ovector[0];
				best_position_end = ovector[1];
				best = i;
			}
		}
	}

	result->begin_attribute = highlight->states.data[result->state].attribute_idx;
	if (best == (size_t) -1) {
		result->start = size;
		result->end = size;
		return t3_false;
	}

	result->start = result->end + best_position;
	result->end += best_position_end;
	result->state = state->patterns.data[best].next_state;
	result->match_attribute = state->patterns.data[best].attribute_idx;
	return t3_true;
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
