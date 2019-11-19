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

#ifdef PCRE_COMPAT
#include "pcre_compat.h"
#else
#include <pcre2.h>
#endif

#include "highlight_api.h"
#include "vector.h"

/* For debugging purposes, we define distinct enums to represent various
   index types. This makes it possible to distinguish from the type, which
   datastructure is supposed to be indexed by a variable. Furthermore, the
   compiler will emit a warning when trying to assign one to the other. However,
   as the integer type used is compiler dependent, we use a regular int for
   non-debug builds. */
#ifdef DEBUG
#define INDEX_TYPE(name) typedef enum { FAKE_CONST_##name = -1 } name
#else
#define INDEX_TYPE(name) typedef int name
#endif
INDEX_TYPE(pattern_idx_t);
INDEX_TYPE(dst_idx_t);

#define NO_CHANGE ((pattern_idx_t)-1)
/* EXIT_STATE is equal to exit = 1. For higher values of the exit attribute,
   subtract from EXIT_STATE. I.e. -3 equals exit = 2, -4 equals exit = 3, etc. */
#define EXIT_STATE ((pattern_idx_t)-2)

/* WARNING: make sure any flags defined here don't clash with the ones in
   highlight.h */
#define T3_HIGHLIGHT_ALLOW_EMPTY_START (1 << 15)

typedef struct {
  char *end_pattern;
  pattern_idx_t state;
} on_entry_info_t;

typedef struct {
  char *dynamic_name;
  char *dynamic_pattern;
  on_entry_info_t *on_entry;
  int on_entry_cnt;
} pattern_extra_t;

typedef struct {
  pcre2_code_8 *regex;
  pattern_extra_t *extra;   /* Only set for start patterns. */
  pattern_idx_t next_state; /* Values: NO_CHANGE, EXIT_STATE or smaller,  or a value >= 0. */
  int attribute_idx;
} pattern_t;

typedef VECTOR(pattern_t) patterns_t;

typedef struct {
  patterns_t patterns;
  int attribute_idx;
} state_t;

typedef VECTOR(state_t) states_t;

struct t3_highlight_t {
  states_t states;
  char *lang_file;
  int flags;
};

typedef struct {
  pcre2_code_8 *regex;
  char *extracted;
  int extracted_length;
} dynamic_state_t;

typedef struct {
  dst_idx_t parent;
  pattern_idx_t highlight_state;
  dynamic_state_t *dynamic;
} state_mapping_t;

struct t3_highlight_match_t {
  const t3_highlight_t *highlight;
  VECTOR(state_mapping_t) mapping;
  PCRE2_SIZE start, match_start, end, last_progress;
  dst_idx_t state;
  int begin_attribute, match_attribute, last_progress_state;
  t3_bool utf8_checked;
  pcre2_match_data_8 *match_data;
};

typedef struct {
  const char *name;
  pattern_idx_t state;
} use_mapping_t;

/* Structs to make passing a large number of arguments easier. */
typedef struct {
  int (*map_style)(void *, const char *);
  void *map_style_data;
  t3_highlight_t *highlight;
  t3_config_t *syntax;
  int flags;
  VECTOR(use_mapping_t) use_map;
  t3_highlight_error_t *error;
  const char *scope;
} highlight_context_t;

typedef struct {
  t3_highlight_match_t *match;
  const char *line;
  size_t size;
  state_t *state;
  pcre2_match_data_8 *match_data;
  PCRE2_SIZE best_end, extract_start, extract_end;
  pattern_t *best;
} match_context_t;

typedef struct {
  size_t i;
  pattern_idx_t state;
} state_stack_t;

T3_HIGHLIGHT_LOCAL char *_t3_highlight_strdup(const char *str);
T3_HIGHLIGHT_LOCAL t3_bool _t3_compile_highlight(const char *highlight, pcre2_code_8 **regex,
                                                 const t3_config_t *error_context, int flags,
                                                 t3_highlight_error_t *error);
T3_HIGHLIGHT_LOCAL t3_bool _t3_check_empty_start_cycle(highlight_context_t *context);
T3_HIGHLIGHT_LOCAL t3_bool _t3_check_use_cycle(highlight_context_t *context);
T3_HIGHLIGHT_LOCAL void _t3_highlight_set_error(t3_highlight_error_t *error, int code,
                                                int line_number, const char *file_name,
                                                const char *extra, int flags);
T3_HIGHLIGHT_LOCAL void _t3_highlight_set_error_simple(t3_highlight_error_t *error, int code,
                                                       int flags);
#endif
