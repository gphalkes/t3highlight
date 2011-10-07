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
#ifndef T3_HIGHLIGHT_H
#define T3_HIGHLIGHT_H

#include <t3config/config.h>
#include <t3highlight/highlight_api.h>

#ifdef __cplusplus
extern "C" {
#endif


/** @name Error codes (libt3key specific) */
/*@{*/
/** Error code: invalid structure of the syntax highlighting file. */
#define T3_ERR_INVALID_FORMAT (-96)
/** Error code: invalid regular expression used in syntax highlighting file. */
#define T3_ERR_INVALID_REGEX (-95)
/*@}*/


typedef struct t3_highlight_t t3_highlight_t;
typedef struct t3_highlight_match_t t3_highlight_match_t;

T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_new(t3_config_t *syntax,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
T3_HIGHLIGHT_API t3_bool t3_highlight_match(const t3_highlight_t *highlight, const char *str, size_t size, t3_highlight_match_t *match);
T3_HIGHLIGHT_API void t3_highlight_free(t3_highlight_t *highlight);

T3_HIGHLIGHT_API void t3_highlight_reset(t3_highlight_match_t *match, int state);
T3_HIGHLIGHT_API t3_highlight_match_t *t3_highlight_new_match(void);
T3_HIGHLIGHT_API void t3_highlight_free_match(t3_highlight_match_t *match);
T3_HIGHLIGHT_API size_t t3_highlight_get_start(t3_highlight_match_t *match);
T3_HIGHLIGHT_API size_t t3_highlight_get_end(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_get_begin_attr(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_get_match_attr(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_next_line(t3_highlight_match_t *match);
T3_HIGHLIGHT_API const char *t3_highlight_strerror(int error);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
