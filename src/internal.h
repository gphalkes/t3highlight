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
#ifndef INTERNAL_H
#define INTERNAL_H

#include <pcre.h>
#include "highlight_api.h"
#include "vector.h"

#define NO_CHANGE (-1)
#define EXIT_STATE (-2)

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
	int next_state, /* Values: NO_CHANGE, EXIT_STATE or a value >= 0. */
		attribute_idx;
} highlight_t;

typedef struct {
	VECTOR(highlight_t, highlights);
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

struct t3_highlight_t {
	VECTOR(state_t, states);
	char *lang_file;
	int flags;
};

#define RETURN_ERROR(x) do { if (error != NULL) *error = (x); goto return_error; } while (0)

T3_HIGHLIGHT_LOCAL char *_t3_highlight_strdup(const char *str);
#endif
