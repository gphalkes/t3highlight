/* Copyright (C) 2011-2012 G.P. Halkes
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
	VECTOR(state_mapping_t) mapping;
	size_t start,
		match_start,
		end;
	int state,
		begin_attribute,
		match_attribute;
	t3_bool utf8_checked;
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
	VECTOR(use_mapping_t) use_map;
} highlight_context_t;

typedef struct {
	t3_highlight_match_t *match;
	const char *line;
	size_t size;
	state_t *state;
	int ovector[30],
		best_end,
		extract_start,
		extract_end;
	highlight_t *best;
	int recursion_depth;
} match_context_t;

static const state_t null_state = { { NULL, 0, 0 }, 0 };

static const char syntax_schema[] = {
#include "syntax.bytes"
};

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, t3_highlight_error_t *error);
static void free_state(state_t *state);
static t3_bool check_empty_cycle(t3_highlight_t *highlight, t3_highlight_error_t *error, int flags);

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, t3_highlight_error_t *error)
{
	t3_highlight_t *result = NULL;
	t3_config_schema_t *schema = NULL;
	t3_config_t *highlights;
	t3_config_error_t local_error;
	highlight_context_t context;

	/* Sanatize flags */
	flags &= T3_HIGHLIGHT_UTF8_NOCHECK | T3_HIGHLIGHT_USE_PATH | T3_HIGHLIGHT_VERBOSE_ERROR;

	/* Validate the syntax using the schema. */
	if ((schema = t3_config_read_schema_buffer(syntax_schema, sizeof(syntax_schema), &local_error, NULL)) == NULL) {
		if (error != NULL)
			error->error = local_error.error != T3_ERR_OUT_OF_MEMORY ? T3_ERR_INTERNAL : local_error.error;
		return NULL;
	}

	if (!t3_config_validate(syntax, schema, &local_error, (flags & T3_HIGHLIGHT_VERBOSE_ERROR) ?
			(T3_CONFIG_VERBOSE_ERROR | T3_CONFIG_ERROR_FILE_NAME) : 0))
		RETURN_ERROR_FULL((flags & T3_HIGHLIGHT_VERBOSE_ERROR) ? local_error.error : T3_ERR_INVALID_FORMAT,
			local_error.line_number, local_error.file_name, local_error.extra, flags);

	t3_config_delete_schema(schema);
	schema = NULL;

	/* Check whether to allow empty start patterns. */
	if (t3_config_get_int(t3_config_get(syntax, "format")) > 1 &&
			(t3_config_get(syntax, "allow-empty-start") == NULL || t3_config_get_bool(t3_config_get(syntax, "allow-empty-start"))))
		flags |= T3_HIGHLIGHT_ALLOW_EMPTY_START;

	if (map_style == NULL)
		RETURN_ERROR(T3_ERR_BAD_ARG, flags);

	if ((result = malloc(sizeof(t3_highlight_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
	VECTOR_INIT(result->states);

	if (!VECTOR_RESERVE(result->states))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
	VECTOR_LAST(result->states) = null_state;

	/* Set up initialization. */
	highlights = t3_config_get(syntax, "highlight");
	context.map_style = map_style;
	context.map_style_data = map_style_data;
	context.highlight = result;
	context.syntax = syntax;
	context.flags = flags;
	VECTOR_INIT(context.use_map);
	if (!init_state(&context, highlights, 0, error)) {
		free(context.use_map.data);
		goto return_error;
	}
	free(context.use_map.data);

	/* If we allow empty start patterns, we need to analyze whether they don't result
	   in infinite loops. */
	if ((flags & T3_HIGHLIGHT_ALLOW_EMPTY_START) && !check_empty_cycle(result, error, flags))
		goto return_error;

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

static t3_bool compile_highlight(const char *highlight, full_pcre_t *action, int flags, t3_highlight_error_t *error,
		const t3_config_t *context)
{
	const char *error_message;
	int error_offset, local_error;
	const char *study_error;

	if ((action->regex = pcre_compile2(highlight, (flags & T3_HIGHLIGHT_UTF8 ? PCRE_UTF8: 0) | PCRE_ANCHORED,
			&local_error, &error_message, &error_offset, NULL)) == NULL)
	{
		if (error != NULL) {
			if (local_error == 21) {
				error->error = T3_ERR_OUT_OF_MEMORY;
			} else {
				error->error = T3_ERR_INVALID_REGEX;
				if (flags & T3_HIGHLIGHT_VERBOSE_ERROR) {
					const char *file_name;
					error->extra = error_message == NULL ? NULL : _t3_highlight_strdup(error_message);
					error->line_number = t3_config_get_line_number(context);
					if ((file_name = t3_config_get_file_name(context)) != NULL)
						error->file_name = _t3_highlight_strdup(file_name);
				}
			}
		}
		return t3_false;
	}
	action->extra = pcre_study(action->regex, 0, &study_error);
	return t3_true;
}

static t3_bool match_name(const t3_config_t *config, const void *data) {
	return t3_config_get(config, (const char *) data) != NULL;
}

static t3_bool add_delim_highlight(highlight_context_t *context, t3_config_t *regex, int next_state,
		const highlight_t *action, t3_highlight_error_t *error)
{
	highlight_t new_highlight;
	new_highlight.next_state = next_state;

	new_highlight.dynamic = NULL;
	if (action->dynamic != NULL && action->dynamic->name != NULL && next_state <= EXIT_STATE) {
		char *regex_with_define;
		t3_bool result;

		/* Create the full regex pattern, including a fake define for the named
		   back reference, and try to compile the pattern to check if it is valid. */
		if ((regex_with_define = malloc(strlen(t3_config_get_string(regex)) + strlen(action->dynamic->name) + 18)) == NULL)
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
		sprintf(regex_with_define, "(?(DEFINE)(?<%s>))%s", action->dynamic->name, t3_config_get_string(regex));
		new_highlight.regex.extra = NULL;
		result = compile_highlight(regex_with_define, &new_highlight.regex, context->flags, error, regex);

		/* Throw away the results of the compilation, because we don't actually need it. */
		free(regex_with_define);
		pcre_free(new_highlight.regex.regex);
		pcre_free(new_highlight.regex.extra);

		/* If the compilation failed, abort the whole thing. */
		if (!result)
			goto return_error;

		new_highlight.regex.regex = NULL;
		new_highlight.regex.extra = NULL;
		/* Save the regular expression, because we need it to build the actual regex once the
		   start pattern is matched. */
		action->dynamic->pattern = t3_config_take_string(regex);
	} else {
		if (!compile_highlight(t3_config_get_string(regex), &new_highlight.regex, context->flags, error, regex))
			goto return_error;
	}

	new_highlight.attribute_idx = action->attribute_idx;
	if (!VECTOR_RESERVE(context->highlight->states.data[action->next_state].highlights))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);

	/* Find the highlight entry, starting after the end entry. If it does not exist,
	   the list of highlights was specified first. */
	for ( ; regex != NULL && strcmp(t3_config_get_name(regex), "highlight") != 0; regex = t3_config_get_next(regex)) {}

	if (regex == NULL && context->highlight->states.data[action->next_state].highlights.used > 0) {
		VECTOR_LAST(context->highlight->states.data[action->next_state].highlights) = new_highlight;
	} else {
		memmove(context->highlight->states.data[action->next_state].highlights.data + 1,
			context->highlight->states.data[action->next_state].highlights.data,
			(context->highlight->states.data[action->next_state].highlights.used - 1) * sizeof(highlight_t));
		context->highlight->states.data[action->next_state].highlights.data[0] = new_highlight;
	}
	return t3_true;

return_error:
	return t3_false;
}

/** Set the @c dynamic member of the ::highlight_t.

    The @c dynamic member is used for storing the data for dynamic end patterns
    and for the on-entry list.
*/
static t3_bool set_dynamic(highlight_t *action, const t3_config_t *highlights, t3_highlight_error_t *error, int flags) {
	const char *dynamic = t3_config_get_string(t3_config_get(highlights, "extract"));
	t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

	if (dynamic == NULL && on_entry == NULL)
		return t3_true;

	if ((action->dynamic = malloc(sizeof(dynamic_highlight_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
	action->dynamic->name = NULL;
	action->dynamic->pattern = NULL;
	action->dynamic->on_entry = NULL;

	if (on_entry != NULL) {
		int i;
		action->dynamic->on_entry_cnt = t3_config_get_length(on_entry);
		if ((action->dynamic->on_entry = malloc(sizeof(on_entry_info_t) * action->dynamic->on_entry_cnt)) == NULL)
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
		for (i = 0; i < action->dynamic->on_entry_cnt; i++)
			action->dynamic->on_entry[i].end_pattern = NULL;
	}

	if (dynamic != NULL) {
		if (dynamic[strspn(dynamic,
				"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")] != 0)
		{
			t3_config_t *extract = t3_config_get(highlights, "extract");
			const char *file_name = t3_config_get_file_name(extract);
			RETURN_ERROR_FULL(T3_ERR_INVALID_NAME, t3_config_get_line_number(extract),
				file_name == NULL ? NULL : _t3_highlight_strdup(file_name),
				t3_config_take_string(t3_config_get(highlights, "extract")), flags);
		}
		action->dynamic->name = t3_config_take_string(t3_config_get(highlights, "extract"));
	}
	return t3_true;

return_error:
	return t3_false;
}

/** Fill the @c on_entry list.

    FIXME: write full docs!
*/
static t3_bool set_on_entry(highlight_t *action, highlight_context_t *context, t3_config_t *highlights, t3_highlight_error_t *error) {
	t3_config_t *regex, *style;
	highlight_t parent_action;
	dynamic_highlight_t parent_dynamic;
	t3_config_t *sub_highlights;
	int style_attr_idx;
	int idx;

	t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

	if (on_entry == NULL)
		return t3_true;

	parent_action = *action;
	parent_dynamic = *action->dynamic;

	parent_action.dynamic = &parent_dynamic;

	for (on_entry = t3_config_get(on_entry, NULL), idx = 0; on_entry != NULL;
			on_entry = t3_config_get_next(on_entry), idx++)
	{
		action->dynamic->on_entry[idx].state = context->highlight->states.used;
		parent_action.next_state = action->dynamic->on_entry[idx].state;

		if ((style = t3_config_get(on_entry, "style")) != NULL)
			parent_action.attribute_idx = style_attr_idx =
				context->map_style(context->map_style_data, t3_config_get_string(style));

		if ((style = t3_config_get(on_entry, "delim-style")) != NULL)
			parent_action.attribute_idx = context->map_style(context->map_style_data, t3_config_get_string(style));

		if (!VECTOR_RESERVE(context->highlight->states))
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
		VECTOR_LAST(context->highlight->states) = null_state;
		VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;
		if ((sub_highlights = t3_config_get(on_entry, "highlight")) != NULL) {
			if (!init_state(context, sub_highlights, action->dynamic->on_entry[idx].state, error))
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
			action->dynamic->on_entry[idx].end_pattern = parent_action.dynamic->pattern;
			parent_action.dynamic->pattern = NULL;
		}
	}
	return t3_true;

return_error:
	return t3_false;
}

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, t3_highlight_error_t *error) {
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
			if (!compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error, regex))
				goto return_error;

			action.attribute_idx = style_attr_idx;
			action.next_state = NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
		} else if ((regex = t3_config_get(highlights, "start")) != NULL) {
			t3_config_t *sub_highlights;

			action.attribute_idx = (style = t3_config_get(highlights, "delim-style")) == NULL ?
				style_attr_idx : context->map_style(context->map_style_data, t3_config_get_string(style));

			if (!compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error, regex))
				goto return_error;

			if (!set_dynamic(&action, highlights, error, context->flags))
				goto return_error;

			/* Create new state to which start will switch. */
			action.next_state = context->highlight->states.used;
			if (!VECTOR_RESERVE(context->highlight->states))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
			VECTOR_LAST(context->highlight->states) = null_state;
			VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;

			/* Add sub-highlights to the new state, if they are specified. */
			if ((sub_highlights = t3_config_get(highlights, "highlight")) != NULL) {
				if (!init_state(context, sub_highlights, action.next_state, error))
					goto return_error;
			}

			if (!set_on_entry(&action, context, highlights, error))
				goto return_error;

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

			if (definition == NULL) {
				const char *file_name = t3_config_get_file_name(use);

				RETURN_ERROR_FULL(T3_ERR_UNDEFINED_USE, t3_config_get_line_number(use),
					file_name == NULL ? NULL : _t3_highlight_strdup(file_name), t3_config_take_string(use),
					context->flags);
			}
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
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
				VECTOR_LAST(context->use_map).name = t3_config_get_string(use);
				VECTOR_LAST(context->use_map).state = action.next_state;

				if (!VECTOR_RESERVE(context->highlight->states))
					RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
				VECTOR_LAST(context->highlight->states) = null_state;

				if (!init_state(context, t3_config_get(definition, "highlight"), action.next_state, error))
					return t3_false;
			} else {
				action.next_state = context->use_map.data[i].state;
			}
		} else {
			RETURN_ERROR(T3_ERR_INTERNAL, context->flags);
		}
		if (!VECTOR_RESERVE(context->highlight->states.data[idx].highlights))
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
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

static t3_bool check_empty_cycle_from_state(states_t *states, size_t state, t3_highlight_error_t *error, int flags) {
	int min_length, depth;
	size_t i, j;

	typedef struct { int state, depth; } state_stack_t;
	VECTOR(state_stack_t) state_stack;
	VECTOR_INIT(state_stack);

	if (!VECTOR_RESERVE(state_stack))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);

	VECTOR_LAST(state_stack).state = state;
	VECTOR_LAST(state_stack).depth = 0;

	while (state_stack.used) {
		state = VECTOR_LAST(state_stack).state;
		depth = VECTOR_LAST(state_stack).depth;

		state_stack.used--;
		for (i = 0; i < states->data[state].highlights.used; i++) {
			highlight_t *highlight = &states->data[state].highlights.data[i];

			if (highlight->next_state == NO_CHANGE)
				continue;

			if (highlight->regex.regex == NULL) {
				if (highlight->next_state < NO_CHANGE) {
					/* FIXME: need to check the pattern with an empty replacement (we built that earlier).
					   For now just report a cycle. */
					if (-highlight->next_state - 1 <= depth)
						RETURN_ERROR(T3_ERR_EMPTY_CYCLE, flags);
					continue;
				} else {
					/* This is a use pattern. For those we can simply push them on the
					   stack at the same depth, and they will be handled correctly. */
					if (!VECTOR_RESERVE(state_stack))
						RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
					VECTOR_LAST(state_stack).state = highlight->next_state;
					VECTOR_LAST(state_stack).depth = depth;
					continue;
				}
			}

			/* This should be pretty much impossible to happen, so we just continue
			   as if this pattern matches at least one byte. */
			if (pcre_fullinfo(highlight->regex.regex, highlight->regex.extra, PCRE_INFO_MINLENGTH, &min_length) != 0)
				continue;

			/* If this pattern can not match the empty string, we don't have to
			   consider it any further. */
			if (min_length > 0)
				continue;

			/* For exit patterns, the only thing that counts is whether we jump
			   to a place inside the cycle, or out of it completely. */
			if (highlight->next_state < 0) {
				if (-highlight->next_state - 1 <= depth)
					RETURN_ERROR(T3_ERR_EMPTY_CYCLE, flags);
				continue;
			}

			/* If we push a state onto the stack that is already on it, we've
			   found a cycle. */
			for (j = 0; j < state_stack.used; j++) {
				if (state_stack.data[j].state == highlight->next_state)
					RETURN_ERROR(T3_ERR_EMPTY_CYCLE, flags);
			}
			/* Push the next state onto the stack, so we can go from there later on. */
			if (!VECTOR_RESERVE(state_stack))
				RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
			VECTOR_LAST(state_stack).state = highlight->next_state;
			VECTOR_LAST(state_stack).depth = depth + 1;

			/* For on-entry states, we simply push all of them. Note that they are
			   are at increasing depth. */
			if (highlight->dynamic != NULL && highlight->dynamic->on_entry_cnt > 0) {
				for (j = 0; (int) j < highlight->dynamic->on_entry_cnt; j++) {
					if (!VECTOR_RESERVE(state_stack))
						RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
					VECTOR_LAST(state_stack).state = highlight->dynamic->on_entry->state;
					VECTOR_LAST(state_stack).depth = depth + 2 + j;
				}
			}
		}
	}
	VECTOR_FREE(state_stack);
	return t3_true;

return_error:
	VECTOR_FREE(state_stack);
	return t3_false;
}

static t3_bool check_empty_cycle(t3_highlight_t *highlight, t3_highlight_error_t *error, int flags) {
	size_t i;
	for (i = 0; i < highlight->states.used; i++) {
		if (!check_empty_cycle_from_state(&highlight->states, i, error, flags))
			return t3_false;
	}
	return t3_true;
}

static int find_state(t3_highlight_match_t *match, int highlight, dynamic_highlight_t *dynamic,
		const char *dynamic_line, int dynamic_length, const char *dynamic_pattern)
{
	size_t i;

	if (highlight <= EXIT_STATE) {
		int return_state;
		for (return_state = match->state; highlight < EXIT_STATE && return_state > 0; highlight++)
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
		if (!compile_highlight(pattern, &new_dynamic->regex, match->highlight->flags & ~T3_HIGHLIGHT_VERBOSE_ERROR, NULL, NULL)) {
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
		int options = PCRE_NO_UTF8_CHECK;

		/* If the regex member == NULL, this highlight is either a pointer to
		   another state which we should search here ("use"), or it is an end
		   pattern with a dynamic back reference. */
		if (context->state->highlights.data[j].regex.regex == NULL) {
			if (context->state->highlights.data[j].next_state >= 0) {
				state_t *save_state;
				/* FIXME: this can be checked before hand. */
				/* Don't keep on going into use definitions. At level 50, we probably
				   ended up in a cycle, so just stop it. */
				if (context->recursion_depth > 50)
					continue;
				save_state = context->state;
				context->state = &context->match->highlight->states.data[context->state->highlights.data[j].next_state];
				match_internal(context);
				context->state = save_state;
				continue;
			}
			regex = &context->match->mapping.data[context->match->state].dynamic->regex;
		} else {
			regex = &context->state->highlights.data[j].regex;
			/* For items that do not change state, we do not want an empty match
			   ever (makes no progress). */
			if (context->state->highlights.data[j].next_state == NO_CHANGE)
				options |= PCRE_NOTEMPTY;
			/* The default behaviour is to not allow start patterns to be empty, such
			   that progress will be guaranteed. */
			else if (context->state->highlights.data[j].next_state > NO_CHANGE &&
					!(context->match->highlight->flags & T3_HIGHLIGHT_ALLOW_EMPTY_START))
				options |= PCRE_NOTEMPTY;
		}

		if (pcre_exec(regex->regex, regex->extra,
				context->line, context->size, context->match->match_start, options, context->ovector,
				sizeof(context->ovector) / sizeof(context->ovector[0])) >= 0 && context->ovector[1] > context->best_end)
		{
			context->best = &context->state->highlights.data[j];
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

static int step_utf8(char first) {
	switch (first & 0xf0) {
		case 0xf0:
			return 4;
		case 0xe0:
			return 3;
		case 0xc0:
		case 0xd0:
			return 2;
		default:
			return 1;
	}
}

t3_bool t3_highlight_match(t3_highlight_match_t *match, const char *line, size_t size) {
	match_context_t context;

	if ((match->highlight->flags & (T3_HIGHLIGHT_UTF8 | T3_HIGHLIGHT_UTF8_NOCHECK)) == T3_HIGHLIGHT_UTF8 && !match->utf8_checked) {
		if (!t3_highlight_utf8check(line, size)) {
			match->state = 0;
			match->begin_attribute = 0;
			match->match_attribute = 0;
			match->start = match->match_start = match->end = -1;
			return t3_false;
		}
		match->utf8_checked = t3_true;
	}

	context.match = match;
	context.line = line;
	context.size = size;
	context.state = &match->highlight->states.data[match->mapping.data[match->state].highlight];
	context.best = NULL;
	context.best_end = -1;
	context.recursion_depth = 0;

	match->start = match->end;
	match->begin_attribute = context.state->attribute_idx;

	for (match->match_start = match->end; match->match_start <= size; match->match_start +=
			(match->highlight->flags & T3_HIGHLIGHT_UTF8) ? step_utf8(line[match->match_start]) : 1)
	{
		match_internal(&context);

		if (context.best != NULL) {
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
	match->utf8_checked = t3_false;
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
	match->utf8_checked = t3_false;
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
			return _("could not locate appropriate highlighting pattern");
		case T3_ERR_UNDEFINED_USE:
			return _("'use' specifies undefined highlight");
		case T3_ERR_INVALID_NAME:
			return _("invalid sub-pattern name");
		case T3_ERR_EMPTY_CYCLE:
			return _("empty start pattern cycle");
	}
}

long t3_highlight_get_version(void) {
	return T3_HIGHLIGHT_VERSION;
}
