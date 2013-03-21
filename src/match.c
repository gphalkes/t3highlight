/* Copyright (C) 2011-2013 G.P. Halkes
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

static int find_state(t3_highlight_match_t *match, int highlight_state, pattern_extra_t *extra,
		const char *dynamic_line, int dynamic_length, const char *dynamic_pattern)
{
	size_t i;

	if (highlight_state <= EXIT_STATE) {
		int return_state;
		for (return_state = match->state; highlight_state < EXIT_STATE && return_state > 0; highlight_state++)
			return_state = match->mapping.data[return_state].parent;
		return return_state > 0 ? match->mapping.data[return_state].parent : 0;
	}

	if (highlight_state == NO_CHANGE)
		return match->state;

	/* Check if the state is already mapped. */
	for (i = match->state + 1; i < match->mapping.used; i++) {
		if (match->mapping.data[i].parent == match->state && match->mapping.data[i].highlight_state == highlight_state &&
				/* Either neither is a match with dynamic back reference, or both are.
				   For safety we ensure that the found state actually has information
				   about a dynamic back reference. */
				(extra == NULL ||
				(extra != NULL && extra->dynamic_name != NULL && match->mapping.data[i].dynamic != NULL &&
				dynamic_length == match->mapping.data[i].dynamic->extracted_length &&
				memcmp(dynamic_line, match->mapping.data[i].dynamic->extracted, dynamic_length) == 0)))
			return i;
	}

	if (!VECTOR_RESERVE(match->mapping))
		return 0;
	VECTOR_LAST(match->mapping).parent = match->state;
	VECTOR_LAST(match->mapping).highlight_state = highlight_state;

	VECTOR_LAST(match->mapping).dynamic = NULL;
	if (extra != NULL && extra->dynamic_name != NULL) {
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
		if ((pattern = malloc(21 + dynamic_length + replace_count * 5 + strlen(extra->dynamic_name) + strlen(dynamic_pattern))) == NULL) {
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

		sprintf(pattern, "(?(DEFINE)(?<%s>\\Q", extra->dynamic_name);
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
		if (!_t3_compile_highlight(pattern, &new_dynamic->regex, NULL, match->highlight->flags & ~T3_HIGHLIGHT_VERBOSE_ERROR, NULL)) {
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

	for (j = 0; j < context->state->patterns.used; j++) {
		full_pcre_t *regex;
		int options = PCRE_NO_UTF8_CHECK;

		/* If the regex member == NULL, this highlight is either a pointer to
		   another state which we should search here ("use"), or it is an end
		   pattern with a dynamic back reference. */
		if (context->state->patterns.data[j].regex.regex == NULL) {
			if (context->state->patterns.data[j].next_state >= 0) {
				state_t *save_state;
				save_state = context->state;
				context->state = &context->match->highlight->states.data[context->state->patterns.data[j].next_state];
				match_internal(context);
				context->state = save_state;
				continue;
			}
			regex = &context->match->mapping.data[context->match->state].dynamic->regex;
		} else {
			regex = &context->state->patterns.data[j].regex;
			/* For items that do not change state, we do not want an empty match
			   ever (makes no progress). */
			if (context->state->patterns.data[j].next_state == NO_CHANGE)
				options |= PCRE_NOTEMPTY;
			/* The default behaviour is to not allow start patterns to be empty, such
			   that progress will be guaranteed. */
			else if (context->state->patterns.data[j].next_state > NO_CHANGE &&
					!(context->match->highlight->flags & T3_HIGHLIGHT_ALLOW_EMPTY_START))
				options |= PCRE_NOTEMPTY;
		}

		if (pcre_exec(regex->regex, regex->extra,
				context->line, context->size, context->match->match_start, options, context->ovector,
				sizeof(context->ovector) / sizeof(context->ovector[0])) >= 0 && context->ovector[1] > context->best_end)
		{
			context->best = &context->state->patterns.data[j];
			context->best_end = context->ovector[1];
			if (context->best->extra != NULL && context->best->extra->dynamic_name != NULL) {
				int string_number = pcre_get_stringnumber(context->best->regex.regex, context->best->extra->dynamic_name);
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
	context.state = &match->highlight->states.data[match->mapping.data[match->state].highlight_state];
	context.best = NULL;
	context.best_end = -1;

	match->start = match->end;
	match->begin_attribute = context.state->attribute_idx;

	if (match->last_progress != match->end) {
		match->last_progress = match->end;
		match->last_progress_state = match->state;
	} else if (match->last_progress_state < match->state) {
		match->last_progress_state = match->state;
	}

	for (match->match_start = match->end; match->match_start <= size; match->match_start +=
			(match->highlight->flags & T3_HIGHLIGHT_UTF8) ? step_utf8(line[match->match_start]) : 1)
	{
		match_internal(&context);

		if (context.best != NULL) {
			int next_state = find_state(match, context.best->next_state, context.best->extra,
				line + context.extract_start, context.extract_end - context.extract_start,
				context.best->extra != NULL ? context.best->extra->dynamic_pattern : NULL);

			/* Check if we have come full circle. If so, continue to the next byte and start over. */
			if (match->last_progress == match->end &&
					context.best->next_state > NO_CHANGE &&
					match->last_progress_state == next_state)
				continue;

			match->end = context.best_end;
			match->state = next_state;
			if (context.best->extra != NULL && context.best->extra->on_entry != NULL) {
				int i;
				for (i = 0; i < context.best->extra->on_entry_cnt; i++) {
					match->state = find_state(match, context.best->extra->on_entry[i].state, context.best->extra,
						line + context.extract_start, context.extract_end - context.extract_start,
						context.best->extra->on_entry[i].end_pattern);
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
	match->last_progress = 0;
	match->last_progress_state = -1;
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
	free_pcre_study(mapping->dynamic->regex.extra);
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
	match->last_progress = 0;
	match->last_progress_state = -1;
	return match->state;
}
