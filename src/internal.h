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
#ifndef INTERNAL_H
#define INTERNAL_H

#include <pcre.h>
#include "highlight_api.h"
#include "vector.h"

#define NO_CHANGE (-1)
/* EXIT_STATE is equal to exit = 1. For higher values of the exit attribute,
   subtract from EXIT_STATE. I.e. -3 equals exit = 2, -4 equals exit = 3, etc. */
#define EXIT_STATE (-2)

/* WARNING: make sure any flags defined here don't clash with the ones in
   highlight.h */
#define T3_HIGHLIGHT_ALLOW_EMPTY_START (1<<15)

typedef struct {
	pcre *regex;
	pcre_extra *extra;
} full_pcre_t;

typedef struct {
	char *end_pattern;
	int state;
} on_entry_info_t;

typedef struct {
	char *name;
	char *pattern;
	on_entry_info_t *on_entry;
	int on_entry_cnt;
} dynamic_highlight_t;

typedef struct {
	full_pcre_t regex;
	dynamic_highlight_t *dynamic; /* Only set for start patterns. */
	int next_state, /* Values: NO_CHANGE, EXIT_STATE or smaller,  or a value >= 0. */
		attribute_idx;
} highlight_t;

typedef struct {
	VECTOR(highlight_t) highlights;
	int attribute_idx;
} state_t;

typedef struct {
	full_pcre_t regex;
	char *extracted;
	int extracted_length;
} dynamic_state_t;

typedef struct {
	int parent;
	int highlight;
	dynamic_state_t *dynamic;
} state_mapping_t;

typedef VECTOR(state_t) states_t;

struct t3_highlight_t {
	states_t states;
	char *lang_file;
	int flags;
};

struct t3_highlight_match_t {
	const t3_highlight_t *highlight;
	VECTOR(state_mapping_t) mapping;
	size_t start,
		match_start,
		end,
		last_progress;
	int state,
		begin_attribute,
		match_attribute,
		last_progress_state;
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
} match_context_t;

typedef struct {
	size_t i;
	int state;
} state_stack_t;



#define RETURN_ERROR_FULL(_error, _line_number, _file_name, _extra, _flags) do { \
	if (error != NULL) { \
		error->error = (_error); \
		if (_flags & T3_HIGHLIGHT_VERBOSE_ERROR) { \
			error->line_number = _line_number; \
			error->file_name = _file_name; \
			error->extra = _extra; \
		} \
	} \
	goto return_error; \
} while (0)
#define RETURN_ERROR(_error, _flags) RETURN_ERROR_FULL(_error, 0, NULL, NULL, _flags)


T3_HIGHLIGHT_LOCAL char *_t3_highlight_strdup(const char *str);
T3_HIGHLIGHT_LOCAL t3_bool _t3_compile_highlight(const char *highlight, full_pcre_t *action, int flags,
	t3_highlight_error_t *error, const t3_config_t *context);
T3_HIGHLIGHT_LOCAL t3_bool _t3_check_empty_start_cycle(t3_highlight_t *highlight, t3_highlight_error_t *error, int flags);
T3_HIGHLIGHT_LOCAL t3_bool _t3_check_use_cycle(t3_highlight_t *syntax, t3_highlight_error_t *error, int flags);
#endif
