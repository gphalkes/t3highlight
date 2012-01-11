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
	const t3_highlight_t *highlight;
	VECTOR(state_mapping_t, mapping);
	size_t start,
		match_start,
		end;
	int state,
		begin_attribute,
		match_attribute;
};

typedef struct {
	const char *name;
	int state;
} use_mapping_t;

typedef struct {
	int (*map_style)(void *, const char *);
	void *map_style_data;
	t3_highlight_t *highlight;
	t3_config_t *syntax;
	int flags;
	VECTOR(use_mapping_t, use_map);
} highlight_context_t;

typedef struct {
	t3_highlight_match_t *match;
	const char *line;
	size_t size;
	state_t *state;
	int ovector[30],
		best_start,
		best_end,
		extract_start,
		extract_end;
	highlight_t *best;
	int options;
	int recursion_depth;
} match_context_t;

static const state_t null_state = { { NULL, 0, 0 }, 0 };

static const char syntax_schema[] = {
#include "syntax.bytes"
};

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, int *error);
static void free_state(state_t *state);

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, int *error)
{
	t3_highlight_t *result = NULL;
	t3_config_schema_t *schema = NULL;
	t3_config_t *highlights;
	t3_config_error_t local_error;
	highlight_context_t context;

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

	highlights = t3_config_get(syntax, "highlight");

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
	VECTOR_INIT(context.use_map);
	if (!init_state(&context, highlights, 0, error)) {
		free(context.use_map.data);
		goto return_error;
	}
	free(context.use_map.data);

	result->flags = flags;
	result->lang_file = NULL;
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

static t3_bool compile_highlight(const char *highlight, full_pcre_t *action, int flags, int *error) {
	const char *error_message;
	int error_offset, local_error;
	const char *study_error;

	if ((action->regex = pcre_compile2(highlight, flags & T3_HIGHLIGHT_UTF8 ? PCRE_UTF8 : 0,
			&local_error, &error_message, &error_offset, NULL)) == NULL)
	{
		if (error != NULL)
			*error = local_error == 21 ? T3_ERR_OUT_OF_MEMORY : T3_ERR_INVALID_REGEX;
		return t3_false;
	}
	action->extra = pcre_study(action->regex, 0, &study_error);
	return t3_true;
}

static t3_bool match_name(const t3_config_t *config, const void *data) {
	return t3_config_get(config, (const char *) data) != NULL;
}

static t3_bool add_delim_highlight(highlight_context_t *context, t3_config_t *regex, int next_state,
		const highlight_t *action, int *error)
{
	highlight_t nest_action;
	nest_action.next_state = next_state;

	nest_action.dynamic = NULL;
	if (action->dynamic != NULL && action->dynamic->name != NULL && next_state <= EXIT_STATE) {
		char *regex_with_define;
		t3_bool result;

		/* Create the full regex pattern, including a fake define for the named
		   back reference, and try to compile the pattern. */
		if ((regex_with_define = malloc(strlen(t3_config_get_string(regex)) + strlen(action->dynamic->name) + 18)) == NULL)
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
		sprintf(regex_with_define, "(?(DEFINE)(?<%s>))%s", action->dynamic->name, t3_config_get_string(regex));
		nest_action.regex.extra = NULL;
		result = compile_highlight(regex_with_define, &nest_action.regex, context->flags, error);
		free(regex_with_define);
		pcre_free(nest_action.regex.regex);
		pcre_free(nest_action.regex.extra);
		if (!result)
			return t3_false;
		nest_action.regex.regex = NULL;
		nest_action.regex.extra = NULL;
		/* Save the regular expression, because we need it to build the actual regex once the
		   start pattern is matched. */
		action->dynamic->pattern = t3_config_take_string(regex);
	} else {
		if (!compile_highlight(t3_config_get_string(regex), &nest_action.regex, context->flags, error))
			return t3_false;
	}

	nest_action.attribute_idx = action->attribute_idx;
	if (!VECTOR_RESERVE(context->highlight->states.data[action->next_state].highlights))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);

	/* Find the highlight entry, starting after the end entry. If it does not exist,
	   the list of highlights was specified first. */
	for ( ; regex != NULL && strcmp(t3_config_get_name(regex), "highlight") != 0; regex = t3_config_get_next(regex)) {}

	if (regex == NULL && context->highlight->states.data[action->next_state].highlights.used > 0) {
		VECTOR_LAST(context->highlight->states.data[action->next_state].highlights) = nest_action;
	} else {
		memmove(context->highlight->states.data[action->next_state].highlights.data + 1,
			context->highlight->states.data[action->next_state].highlights.data,
			(context->highlight->states.data[action->next_state].highlights.used - 1) * sizeof(highlight_t));
		context->highlight->states.data[action->next_state].highlights.data[0] = nest_action;
	}
	return t3_true;
return_error:
	return t3_false;
}

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, int *error) {
	t3_config_t *regex, *style, *use;
	highlight_t action;
	int style_attr_idx;

	for (highlights = t3_config_get(highlights, NULL); highlights != NULL; highlights = t3_config_get_next(highlights)) {
		style_attr_idx = (style = t3_config_get(highlights, "style")) == NULL ?
			context->highlight->states.data[idx].attribute_idx :
			context->map_style(context->map_style_data, t3_config_get_string(style));

		action.regex.regex = NULL;
		action.regex.extra = NULL;
		action.dynamic = NULL;
		if ((regex = t3_config_get(highlights, "regex")) != NULL) {
			if (!compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error))
				return t3_false;

			action.attribute_idx = style_attr_idx;
			action.next_state = NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
		} else if ((regex = t3_config_get(highlights, "start")) != NULL) {
			t3_config_t *sub_highlights;
			t3_config_t *on_entry;
			const char *dynamic;

			action.attribute_idx = (style = t3_config_get(highlights, "delim-style")) == NULL ?
				style_attr_idx : context->map_style(context->map_style_data, t3_config_get_string(style));

			if (!compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error))
				return t3_false;

			dynamic = t3_config_get_string(t3_config_get(highlights, "extract"));
			on_entry = t3_config_get(highlights, "on-entry");
			if (dynamic != NULL || on_entry != NULL) {
				if ((action.dynamic = malloc(sizeof(dynamic_highlight_t))) == NULL)
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
				action.dynamic->name = NULL;
				action.dynamic->pattern = NULL;
				action.dynamic->on_entry = NULL;

				if (on_entry != NULL) {
					int i;
					action.dynamic->on_entry_cnt = t3_config_get_length(on_entry);
					if ((action.dynamic->on_entry = malloc(sizeof(on_entry_info_t) * action.dynamic->on_entry_cnt)) == NULL)
						RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
					for (i = 0; i < action.dynamic->on_entry_cnt; i++)
						action.dynamic->on_entry[i].end_pattern = NULL;
				}

				if (dynamic != NULL) {
					if (dynamic[strspn(dynamic,
							"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")] != 0)
						//FIXME: this should be more precise
						RETURN_ERROR(T3_ERR_INVALID_REGEX);
					action.dynamic->name = t3_config_take_string(t3_config_get(highlights, "extract"));
				}
			}

			/* Create new state to which start will switch. */
			action.next_state = context->highlight->states.used;
			if (!VECTOR_RESERVE(context->highlight->states))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
			VECTOR_LAST(context->highlight->states) = null_state;
			VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;

			/* Add sub-highlights to the new state, if they are specified. */
			if ((sub_highlights = t3_config_get(highlights, "highlight")) != NULL) {
				if (!init_state(context, sub_highlights, action.next_state, error))
					goto return_error;
			}

			if (on_entry != NULL) {
				highlight_t parent_action = action;
				dynamic_highlight_t parent_dynamic = *action.dynamic;
				int idx;

				parent_action.dynamic = &parent_dynamic;

				for (on_entry = t3_config_get(on_entry, NULL), idx = 0; on_entry != NULL;
						on_entry = t3_config_get_next(on_entry), idx++)
				{
					action.dynamic->on_entry[idx].state = context->highlight->states.used;
					parent_action.next_state = action.dynamic->on_entry[idx].state;

					if ((style = t3_config_get(on_entry, "style")) != NULL)
						parent_action.attribute_idx = style_attr_idx =
							context->map_style(context->map_style_data, t3_config_get_string(style));

					if ((style = t3_config_get(on_entry, "delim-style")) != NULL)
						parent_action.attribute_idx = context->map_style(context->map_style_data, t3_config_get_string(style));

					if (!VECTOR_RESERVE(context->highlight->states))
						RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
					VECTOR_LAST(context->highlight->states) = null_state;
					VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;
					if ((sub_highlights = t3_config_get(on_entry, "highlight")) != NULL) {
						if (!init_state(context, sub_highlights, action.dynamic->on_entry[idx].state, error))
							goto return_error;
					}
					/* If the highlight specifies an end regex, create an extra action for that and paste that
					   to in the list of sub-highlights. Depending on whether end is specified before or after
					   the highlight list, it will be pre- or appended. */
					if ((regex = t3_config_get(on_entry, "end")) != NULL) {
						int return_state = NO_CHANGE - t3_config_get_int(t3_config_get(on_entry, "exit"));
						if (return_state == NO_CHANGE)
							return_state = EXIT_STATE;
						if (!add_delim_highlight(context, regex, return_state, &parent_action, error))
							goto return_error;
						else
							action.dynamic->on_entry[idx].end_pattern = parent_action.dynamic->pattern;
						parent_action.dynamic->pattern = NULL;
					}
				}
			}

			/* If the highlight specifies an end regex, create an extra action for that and paste that
			   to in the list of sub-highlights. Depending on whether end is specified before or after
			   the highlight list, it will be pre- or appended. */
			if ((regex = t3_config_get(highlights, "end")) != NULL) {
				int return_state = NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
				if (return_state == NO_CHANGE)
					return_state = EXIT_STATE;
				if (!add_delim_highlight(context, regex, return_state, &action, error))
					goto return_error;
			}

			if (t3_config_get_bool(t3_config_get(highlights, "nested")) &&
					!add_delim_highlight(context, t3_config_get(highlights, "start"), action.next_state, &action, error))
				goto return_error;
		} else if ((use = t3_config_get(highlights, "use")) != NULL) {
			size_t i;

			t3_config_t *definition = t3_config_get(t3_config_find(t3_config_get(context->syntax, "define"),
				match_name, t3_config_get_string(use), NULL), t3_config_get_string(use));

			if (definition == NULL)
				RETURN_ERROR(T3_ERR_UNDEFINED_USE);

			/* regex = NULL signifies that this is a link to another state. We
			   also have to set extra to NULL, to prevent segfaults on free. */
			action.regex.regex = NULL;
			action.regex.extra = NULL;

			/* Lookup the name in the use_map. If the definition was already
			   compiled before, we don't have to do it again, but we can simply
			   refer to the previous definition. */
			for (i = 0; i < context->use_map.used; i++) {
				if (strcmp(t3_config_get_string(use), context->use_map.data[i].name) == 0)
					break;
			}

			if (i == context->use_map.used) {
				/* If we didn't already compile the defintion, do it now. */
				action.next_state = context->highlight->states.used;

				if (!VECTOR_RESERVE(context->use_map))
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
				VECTOR_LAST(context->use_map).name = t3_config_get_string(use);
				VECTOR_LAST(context->use_map).state = action.next_state;

				if (!VECTOR_RESERVE(context->highlight->states))
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
				VECTOR_LAST(context->highlight->states) = null_state;

				if (!init_state(context, t3_config_get(definition, "highlight"), action.next_state, error))
					return t3_false;
			} else {
				action.next_state = context->use_map.data[i].state;
			}
		} else {
			RETURN_ERROR(T3_ERR_INTERNAL);
		}
		if (!VECTOR_RESERVE(context->highlight->states.data[idx].highlights))
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);
		VECTOR_LAST(context->highlight->states.data[idx].highlights) = action;
	}
	return t3_true;

return_error:
	if (action.dynamic != NULL) {
		free(action.dynamic->name);
		free(action.dynamic->pattern);
		if (action.dynamic->on_entry != NULL) {
			int i;
			for (i = 0; i < action.dynamic->on_entry_cnt; i++)
				free(action.dynamic->on_entry[i].end_pattern);
			free(action.dynamic->on_entry);
		}
		free(action.dynamic);
	}
	pcre_free(action.regex.regex);
	pcre_free(action.regex.extra);
	return t3_false;
}

static void free_highlight(highlight_t *highlight) {
	pcre_free(highlight->regex.regex);
	pcre_free(highlight->regex.extra);
	if (highlight->dynamic != NULL) {
		free(highlight->dynamic->name);
		free(highlight->dynamic->pattern);
		if (highlight->dynamic->on_entry != NULL) {
			int i;
			for (i = 0; i < highlight->dynamic->on_entry_cnt; i++)
				free(highlight->dynamic->on_entry[i].end_pattern);
			free(highlight->dynamic->on_entry);
		}
		free(highlight->dynamic);
	}
}

static void free_state(state_t *state) {
	VECTOR_ITERATE(state->highlights, free_highlight);
	VECTOR_FREE(state->highlights);
}

void t3_highlight_free(t3_highlight_t *highlight) {
	if (highlight == NULL)
		return;
	VECTOR_ITERATE(highlight->states, free_state);
	VECTOR_FREE(highlight->states);
	free(highlight->lang_file);
	free(highlight);
}

static int find_state(t3_highlight_match_t *match, int highlight, dynamic_highlight_t *dynamic,
		const char *dynamic_line, int dynamic_length, const char *dynamic_pattern)
{
	size_t i;

	if (highlight <= EXIT_STATE) {
		int return_state = match->state;
		while (highlight < EXIT_STATE && return_state > 0)
			return_state = match->mapping.data[return_state].parent;
		return return_state > 0 ? match->mapping.data[return_state].parent : 0;
	}

	if (highlight == NO_CHANGE)
		return match->state;

	/* Check if the state is already mapped. */
	for (i = match->state + 1; i < match->mapping.used; i++) {
		if (match->mapping.data[i].parent == match->state && match->mapping.data[i].highlight == highlight &&
				/* Either neither is a match with dynamic back reference, or both are.
				   For safety we ensure that the found state actually has information
				   about a dynamic back reference. */
				(dynamic == NULL ||
				(dynamic != NULL && match->mapping.data[i].dynamic != NULL &&
				dynamic_length == match->mapping.data[i].dynamic->extracted_length &&
				memcmp(dynamic_line, match->mapping.data[i].dynamic->extracted, dynamic_length) == 0)))
			return i;
	}

	if (!VECTOR_RESERVE(match->mapping))
		return 0;
	VECTOR_LAST(match->mapping).parent = match->state;
	VECTOR_LAST(match->mapping).highlight = highlight;

	VECTOR_LAST(match->mapping).dynamic = NULL;
	if (dynamic != NULL && dynamic->name != NULL) {
		int replace_count = 0, i;
		char *pattern, *patptr;
		dynamic_state_t *new_dynamic;

		for (i = 0; i < dynamic_length; i++) {
			if (dynamic_line[i] == 0 || (dynamic_line[i] == '\\' && i + 1 < dynamic_length && dynamic_line[i + 1] == 'E'))
				replace_count++;
		}
		/* Build the following pattern:
		   (?(DEFINE)(?<%s>\Q%s\E))%s
		   Note that the pattern between \Q and \E must be escaped for 0 bytes and \E.
		*/

		/* 22 bytes for fixed prefix and 0 byte, dynamic_length for the matched text,
		   5 * replace_count for replacing 0 bytes and the \ in any \E's in the matched text,
		   the length of the name of the pattern to insert and the length of the original
		   regular expression to be inserted. */
		if ((pattern = malloc(21 + dynamic_length + replace_count * 5 + strlen(dynamic->name) + strlen(dynamic_pattern))) == NULL) {
			/* Undo VECTOR_RESERVE performed above. */
			match->mapping.used--;
			return 0;
		}
		if ((new_dynamic = malloc(sizeof(dynamic_state_t))) == NULL) {
			/* Undo VECTOR_RESERVE performed above. */
			match->mapping.used--;
			free(pattern);
			return 0;
		}
		if ((new_dynamic->extracted = malloc(dynamic_length)) == NULL) {
			/* Undo VECTOR_RESERVE performed above. */
			match->mapping.used--;
			free(new_dynamic);
			free(pattern);
			return 0;
		}
		new_dynamic->extracted_length = dynamic_length;
		memcpy(new_dynamic->extracted, dynamic_line, dynamic_length);

		sprintf(pattern, "(?(DEFINE)(?<%s>\\Q", dynamic->name);
		patptr = pattern + strlen(pattern);
		for (i = 0; i < dynamic_length; i++) {
			if (dynamic_line[i] == 0 || (dynamic_line[i] == '\\' && i + 1 < dynamic_length && dynamic_line[i + 1] == 'E')) {
				*patptr++ = '\\';
				*patptr++ = 'E';
				*patptr++ = '\\';
				*patptr++ = dynamic_line[i] == 0 ? '0' : '\\';
				*patptr++ = '\\';
				*patptr++ = 'Q';
			} else {
				*patptr++ = dynamic_line[i];
			}
		}
		strcpy(patptr, "\\E))");
		strcat(patptr, dynamic_pattern);
		if (!compile_highlight(pattern, &new_dynamic->regex, match->highlight->flags, NULL)) {
			/* Undo VECTOR_RESERVE performed above. */
			match->mapping.used--;
			free(new_dynamic->extracted);
			free(new_dynamic);
			free(pattern);
			return 0;
		}
		VECTOR_LAST(match->mapping).dynamic = new_dynamic;
		free(pattern);
	}
	return match->mapping.used - 1;
}

static void match_internal(match_context_t *context) {
	size_t j;

	context->recursion_depth++;

	for (j = 0; j < context->state->highlights.used; j++) {
		full_pcre_t *regex;
		int options = context->options;

		/* If the regex member == NULL, this highlight is either a pointer to
		   another state which we should search here ("use"), or it is an end
		   pattern with a dynamic back reference. */
		if (context->state->highlights.data[j].regex.regex == NULL) {
			if (context->state->highlights.data[j].next_state >= 0) {
				/* Don't keep on going into use definitions. At level 50, we probably
				   ended up in a cycle, so just stop it. */
				if (context->recursion_depth > 50)
					continue;
				state_t *save_state = context->state;
				context->state = &context->match->highlight->states.data[context->state->highlights.data[j].next_state];
				match_internal(context);
				context->state = save_state;
				continue;
			}
			regex = &context->match->mapping.data[context->match->state].dynamic->regex;
		} else {
			regex = &context->state->highlights.data[j].regex;
			/* For items that do not change state, we do not want an empty match
			   ever (makes no progress). Furthermore, start highlights have to make
			   progress, to ensure that we do not end up in an infinite loop of
			   state entry and exit, or nesting.
			*/
			if (context->state->highlights.data[j].next_state == NO_CHANGE || context->state->highlights.data[j].next_state >= 0)
				options |= PCRE_NOTEMPTY;
		}

		if (pcre_exec(regex->regex, regex->extra,
				context->line, context->size, context->match->end, options, context->ovector,
				sizeof(context->ovector) / sizeof(context->ovector[0])) >= 0 && (context->ovector[0] < context->best_start ||
				(context->ovector[0] == context->best_start && context->ovector[1] > context->best_end)))
		{
			context->best = &context->state->highlights.data[j];
			context->best_start = context->ovector[0];
			context->best_end = context->ovector[1];
			if (context->best->dynamic != NULL && context->best->dynamic->name != NULL) {
				int string_number = pcre_get_stringnumber(context->best->regex.regex, context->best->dynamic->name);
				if (string_number == PCRE_ERROR_NOSUBSTRING || string_number > 10) {
					context->extract_start = 0;
					context->extract_end = 0;
				} else {
					context->extract_start = context->ovector[string_number * 2];
					context->extract_end = context->ovector[string_number * 2 + 1];
				}
			}

		}
	}

	context->recursion_depth--;
}

t3_bool t3_highlight_match(t3_highlight_match_t *match, const char *line, size_t size) {
	match_context_t context;
	context.match = match;
	context.line = line;
	context.size = size;
	context.state = &match->highlight->states.data[match->mapping.data[match->state].highlight];
	context.best = NULL;
	context.best_start = INT_MAX;
	context.options = match->highlight->flags & T3_HIGHLIGHT_UTF8_NOCHECK ? PCRE_NO_UTF8_CHECK : 0;
	context.recursion_depth = 0;

	match->start = match->end;
	match->begin_attribute = context.state->attribute_idx;

	match_internal(&context);

	if (context.best != NULL) {
		match->match_start = context.best_start;
		match->end = context.best_end;
		match->state = find_state(match, context.best->next_state, context.best->dynamic,
			line + context.extract_start, context.extract_end - context.extract_start,
			context.best->dynamic != NULL ? context.best->dynamic->pattern : NULL);
		if (context.best->dynamic != NULL && context.best->dynamic->on_entry != NULL) {
			int i;
			for (i = 0; i < context.best->dynamic->on_entry_cnt; i++) {
				match->state = find_state(match, context.best->dynamic->on_entry[i].state, context.best->dynamic,
					line + context.extract_start, context.extract_end - context.extract_start,
					context.best->dynamic->on_entry[i].end_pattern);
			}
		}
		match->match_attribute = context.best->attribute_idx;
		return t3_true;
	}

	match->match_start = size;
	match->end = size;
	return t3_false;
}

void t3_highlight_reset(t3_highlight_match_t *match, int state) {
	match->start = 0;
	match->match_start = 0;
	match->end = 0;
	match->begin_attribute = 0;
	match->match_attribute = 0;
	match->state = state;
}

t3_highlight_match_t *t3_highlight_new_match(const t3_highlight_t *highlight) {
	t3_highlight_match_t *result = malloc(sizeof(t3_highlight_match_t));
	if (result == NULL)
		return NULL;

	VECTOR_INIT(result->mapping);
	if (!VECTOR_RESERVE(result->mapping)) {
		free(result);
		return NULL;
	}

	result->highlight = highlight;
	memset(&VECTOR_LAST(result->mapping), 0, sizeof(state_mapping_t));
	t3_highlight_reset(result, 0);
	return result;
}

static void free_dynamic(state_mapping_t *mapping) {
	if (mapping->dynamic == NULL)
		return;
	free(mapping->dynamic->extracted);
	pcre_free(mapping->dynamic->regex.regex);
	pcre_free(mapping->dynamic->regex.extra);
	free(mapping->dynamic);
}

void t3_highlight_free_match(t3_highlight_match_t *match) {
	if (match == NULL)
		return;
	VECTOR_ITERATE(match->mapping, free_dynamic);
	VECTOR_FREE(match->mapping);
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
			return _("could not locate appropriate highlighting highlights");
		case T3_ERR_UNDEFINED_USE:
			return _("'use' specifies undefined highlight");
	}
}

long t3_highlight_get_version(void) {
	return T3_HIGHLIGHT_VERSION;
}
