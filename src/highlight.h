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

/** The version of libt3highlight encoded as a single integer.

    The least significant 8 bits represent the patch level.
    The second 8 bits represent the minor version.
    The third 8 bits represent the major version.

	At runtime, the value of T3_HIGHLIGHT_VERSION can be retrieved by calling
	::t3_highlight_get_version.

    @internal
    The value 0 is an invalid value which should be replaced by the script
    that builds the release package.
*/
#define T3_HIGHLIGHT_VERSION 0

/** @name Error codes (libt3key specific) */
/*@{*/
/** Error code: invalid structure of the syntax highlighting file. */
#define T3_ERR_INVALID_FORMAT (-96)
/** Error code: invalid regular expression used in syntax highlighting file. */
#define T3_ERR_INVALID_REGEX (-95)
/** Error code: could not determine appropriate highlighting patterns. */
#define T3_ERR_NO_SYNTAX (-94)
/*@}*/


typedef struct t3_highlight_t t3_highlight_t;
typedef struct t3_highlight_match_t t3_highlight_match_t;

typedef struct {
	char *name;
	char *lang_file;
} t3_highlight_lang_t;

T3_HIGHLIGHT_API t3_highlight_lang_t *t3_highlight_list(int *error);
T3_HIGHLIGHT_API void t3_highlight_free_list(t3_highlight_lang_t *list);

T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load_by_filename(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load_by_langname(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_new(t3_config_t *syntax,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);

T3_HIGHLIGHT_API void t3_highlight_free(t3_highlight_t *highlight);

T3_HIGHLIGHT_API t3_bool t3_highlight_match(const t3_highlight_t *highlight, const char *str, size_t size, t3_highlight_match_t *match);

T3_HIGHLIGHT_API t3_highlight_match_t *t3_highlight_new_match(void);
T3_HIGHLIGHT_API void t3_highlight_free_match(t3_highlight_match_t *match);
T3_HIGHLIGHT_API void t3_highlight_reset(t3_highlight_match_t *match, int state);

T3_HIGHLIGHT_API size_t t3_highlight_get_start(t3_highlight_match_t *match);
T3_HIGHLIGHT_API size_t t3_highlight_get_end(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_get_begin_attr(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_get_match_attr(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_get_state(t3_highlight_match_t *match);
T3_HIGHLIGHT_API int t3_highlight_next_line(t3_highlight_match_t *match);

T3_HIGHLIGHT_API const char *t3_highlight_strerror(int error);

/** Get the value of ::T3_HIGHLIGHT_VERSION corresponding to the actual used library.
    @return The value of ::T3_HIGHLIGHT_VERSION.

    This function can be useful to determine at runtime what version of the library
    was linked to the program. Although currently there are no known uses for this
    information, future library additions may prompt library users to want to operate
    differently depending on the available features.
*/
T3_HIGHLIGHT_API long t3_highlight_get_version(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif
