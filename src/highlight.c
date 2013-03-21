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

static const state_t null_state = { { NULL, 0, 0 }, 0 };

static const char syntax_schema[] = {
#include "syntax.bytes"
};

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, t3_highlight_error_t *error);
static void free_state(state_t *state);

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

	if (!_t3_check_use_cycle(result, error, flags))
		goto return_error;

	/* If we allow empty start patterns, we need to analyze whether they don't result
	   in infinite loops. */
	if ((flags & T3_HIGHLIGHT_ALLOW_EMPTY_START) && !_t3_check_empty_start_cycle(result, error, flags))
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

t3_bool _t3_compile_highlight(const char *highlight, full_pcre_t *action, int flags,
	t3_highlight_error_t *error, const t3_config_t *context)
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
		const pattern_t *action, t3_highlight_error_t *error)
{
	pattern_t new_highlight;
	new_highlight.next_state = next_state;

	new_highlight.extra = NULL;
	if (action->extra != NULL && action->extra->dynamic_name != NULL && next_state <= EXIT_STATE) {
		char *regex_with_define;
		t3_bool result;

		/* Create the full regex pattern, including a fake define for the named
		   back reference, and try to compile the pattern to check if it is valid. */
		if ((regex_with_define = malloc(strlen(t3_config_get_string(regex)) + strlen(action->extra->dynamic_name) + 18)) == NULL)
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
		sprintf(regex_with_define, "(?(DEFINE)(?<%s>))%s", action->extra->dynamic_name, t3_config_get_string(regex));
		new_highlight.regex.extra = NULL;
		result = _t3_compile_highlight(regex_with_define, &new_highlight.regex, context->flags, error, regex);

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
		action->extra->dynamic_pattern = t3_config_take_string(regex);
	} else {
		if (!_t3_compile_highlight(t3_config_get_string(regex), &new_highlight.regex, context->flags, error, regex))
			goto return_error;
	}

	new_highlight.attribute_idx = action->attribute_idx;
	if (!VECTOR_RESERVE(context->highlight->states.data[action->next_state].patterns))
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);

	/* Find the highlight entry, starting after the end entry. If it does not exist,
	   the list of highlights was specified first. */
	for ( ; regex != NULL && strcmp(t3_config_get_name(regex), "highlight") != 0; regex = t3_config_get_next(regex)) {}

	if (regex == NULL && context->highlight->states.data[action->next_state].patterns.used > 0) {
		VECTOR_LAST(context->highlight->states.data[action->next_state].patterns) = new_highlight;
	} else {
		memmove(context->highlight->states.data[action->next_state].patterns.data + 1,
			context->highlight->states.data[action->next_state].patterns.data,
			(context->highlight->states.data[action->next_state].patterns.used - 1) * sizeof(pattern_t));
		context->highlight->states.data[action->next_state].patterns.data[0] = new_highlight;
	}
	return t3_true;

return_error:
	return t3_false;
}

/** Set the @c extra member of the ::pattern_t.

    The @c extra member is used for storing the data for dynamic end patterns
    and for the on-entry list.
*/
static t3_bool set_extra(pattern_t *action, const t3_config_t *highlights, t3_highlight_error_t *error, int flags) {
	const char *dynamic = t3_config_get_string(t3_config_get(highlights, "extract"));
	t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

	if (dynamic == NULL && on_entry == NULL)
		return t3_true;

	if ((action->extra = malloc(sizeof(pattern_extra_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
	action->extra->dynamic_name = NULL;
	action->extra->dynamic_pattern = NULL;
	action->extra->on_entry = NULL;

	if (on_entry != NULL) {
		int i;
		action->extra->on_entry_cnt = t3_config_get_length(on_entry);
		if ((action->extra->on_entry = malloc(sizeof(on_entry_info_t) * action->extra->on_entry_cnt)) == NULL)
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, flags);
		for (i = 0; i < action->extra->on_entry_cnt; i++)
			action->extra->on_entry[i].end_pattern = NULL;
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
		action->extra->dynamic_name = t3_config_take_string(t3_config_get(highlights, "extract"));
	}
	return t3_true;

return_error:
	return t3_false;
}

/** Fill the @c on_entry list.

    FIXME: write full docs!
*/
static t3_bool set_on_entry(pattern_t *action, highlight_context_t *context, t3_config_t *highlights, t3_highlight_error_t *error) {
	t3_config_t *regex, *style;
	pattern_t parent_action;
	pattern_extra_t parent_extra;
	t3_config_t *sub_highlights;
	int style_attr_idx;
	int idx;

	t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

	if (on_entry == NULL)
		return t3_true;

	parent_action = *action;
	parent_extra = *action->extra;

	parent_action.extra = &parent_extra;

	for (on_entry = t3_config_get(on_entry, NULL), idx = 0; on_entry != NULL;
			on_entry = t3_config_get_next(on_entry), idx++)
	{
		action->extra->on_entry[idx].state = context->highlight->states.used;
		parent_action.next_state = action->extra->on_entry[idx].state;

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
			if (!init_state(context, sub_highlights, action->extra->on_entry[idx].state, error))
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
			action->extra->on_entry[idx].end_pattern = parent_action.extra->dynamic_pattern;
			parent_action.extra->dynamic_pattern = NULL;
		}
	}
	return t3_true;

return_error:
	return t3_false;
}

static t3_bool init_state(highlight_context_t *context, t3_config_t *highlights, int idx, t3_highlight_error_t *error) {
	t3_config_t *regex, *style, *use;
	pattern_t action;
	int style_attr_idx;

	for (highlights = t3_config_get(highlights, NULL); highlights != NULL; highlights = t3_config_get_next(highlights)) {
		style_attr_idx = (style = t3_config_get(highlights, "style")) == NULL ?
			context->highlight->states.data[idx].attribute_idx :
			context->map_style(context->map_style_data, t3_config_get_string(style));

		action.regex.regex = NULL;
		action.regex.extra = NULL;
		action.extra = NULL;
		if ((regex = t3_config_get(highlights, "regex")) != NULL) {
			if (!_t3_compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error, regex))
				goto return_error;

			action.attribute_idx = style_attr_idx;
			action.next_state = NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
		} else if ((regex = t3_config_get(highlights, "start")) != NULL) {
			t3_config_t *sub_highlights;

			action.attribute_idx = (style = t3_config_get(highlights, "delim-style")) == NULL ?
				style_attr_idx : context->map_style(context->map_style_data, t3_config_get_string(style));

			if (!_t3_compile_highlight(t3_config_get_string(regex), &action.regex, context->flags, error, regex))
				goto return_error;

			if (!set_extra(&action, highlights, error, context->flags))
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

			/* Set the on_entry patterns, if any. Note that this calls add_delim_highlight for
			   the end patterns, which frobs the extra->dynamic_pattern member. Thus this must be
			   called before the add_delim_highlight call for this patterns own end pattern. */
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
		if (!VECTOR_RESERVE(context->highlight->states.data[idx].patterns))
			RETURN_ERROR(T3_ERR_OUT_OF_MEMORY, context->flags);
		VECTOR_LAST(context->highlight->states.data[idx].patterns) = action;
	}
	return t3_true;

return_error:
	if (action.extra != NULL) {
		free(action.extra->dynamic_name);
		free(action.extra->dynamic_pattern);
		if (action.extra->on_entry != NULL) {
			int i;
			for (i = 0; i < action.extra->on_entry_cnt; i++)
				free(action.extra->on_entry[i].end_pattern);
			free(action.extra->on_entry);
		}
		free(action.extra);
	}
	pcre_free(action.regex.regex);
	pcre_free(action.regex.extra);
	return t3_false;
}

static void free_highlight(pattern_t *highlight) {
	pcre_free(highlight->regex.regex);
	pcre_free(highlight->regex.extra);
	if (highlight->extra != NULL) {
		free(highlight->extra->dynamic_name);
		free(highlight->extra->dynamic_pattern);
		if (highlight->extra->on_entry != NULL) {
			int i;
			for (i = 0; i < highlight->extra->on_entry_cnt; i++)
				free(highlight->extra->on_entry[i].end_pattern);
			free(highlight->extra->on_entry);
		}
		free(highlight->extra);
	}
}

static void free_state(state_t *state) {
	VECTOR_ITERATE(state->patterns, free_highlight);
	VECTOR_FREE(state->patterns);
}

void t3_highlight_free(t3_highlight_t *highlight) {
	if (highlight == NULL)
		return;
	VECTOR_ITERATE(highlight->states, free_state);
	VECTOR_FREE(highlight->states);
	free(highlight->lang_file);
	free(highlight);
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
		case T3_ERR_EMPTY_START_CYCLE:
			return _("empty start-pattern cycle");
		case T3_ERR_USE_CYCLE:
			return _("use-pattern cycle");
	}
}

long t3_highlight_get_version(void) {
	return T3_HIGHLIGHT_VERSION;
}
