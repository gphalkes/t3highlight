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

/** @defgroup t3config_other Functions, constants and enums. */
/** @addtogroup t3config_other */
/** @{ */

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
/** Error code: a 'use' directive references a non-existing define. */
#define T3_ERR_UNDEFINED_USE (-93)
/** Error code: syntax highlighting file contains recursive defines. */
#define T3_ERR_RECURSIVE_DEFINITION (-92)
/*@}*/

/** @struct t3_highlight_t
    An opaque struct representing a highlighting pattern.
*/
typedef struct t3_highlight_t t3_highlight_t;
/** @struct t3_highlight_match_t
    An opaque struct representing a match and current state during highlighting.
*/
typedef struct t3_highlight_match_t t3_highlight_match_t;

/** @struct t3_highlight_lang_t
    A struct representing a display name/language file name tuple.
*/
typedef struct {
	char *name;
	char *lang_file;
} t3_highlight_lang_t;

/** List the known languages.
    @return A list of display name/language file name pairs.

    The returned list is terminated by an entry with two @c NULL pointers, and
    must be freed using ::t3_highlight_free_list.
*/
T3_HIGHLIGHT_API t3_highlight_lang_t *t3_highlight_list(int *error);
/** Free a list returned by ::t3_highlight_list.
    It is acceptable to pass a @c NULL pointer.
*/
T3_HIGHLIGHT_API void t3_highlight_free_list(t3_highlight_lang_t *list);

/** Load a highlighting pattern, using a language file name.
    @param name The file name (relative to the search path) to load.
    @param map_style Callback function to map symbolic style names to integers.
    @param map_style_data Data for the @p map_style callback.
    @param error Location to store an error code, or @c NULL.
    @return An opaque struct representing a highlighting pattern, or @c NULL on error.

    The @p map_style callback is passed @p map_style_data as its first argument,
    and a string describing a style as its second argument. The return value
    must be an integer, which will be used in the result from
    ::t3_highlight_match. Typically any unknown styles should be mapped to the
    same value as the 'normal' style.
*/
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
/** Load a highlighting pattern, using a source file name.
    @param name The source file name used to determine the appropriate highlighting pattern.

    Other parameters and return value are equal to ::t3_highlight_load. The
    file-regex member in the language definition in the lang.map file is used
    to determine which highlighting patterns should be loaded.
*/
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load_by_filename(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
/** Load a highlighting pattern, using a language name.
    @param name The source file name used to determine the appropriate highlighting pattern.

    Other parameters and return value are equal to ::t3_highlight_load. The
    name-regex member in the language definition in the lang.map file is used
    to determine which highlighting patterns should be loaded.
*/
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_load_by_langname(const char *name,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);
/** Create a highlighting pattern from a previously created configuration.
    @param syntax The @c t3_config_t to create the highlighting pattern from.

    Other parameters and return value are equal to ::t3_highlight_load. The
    highlighting pattern are stored in the format of @c libt3config. Any
    configuration which conforms to the schema of a syntax highlighting pattern
    can be used to create a highlighting pattern.
*/
T3_HIGHLIGHT_API t3_highlight_t *t3_highlight_new(t3_config_t *syntax,
	int (*map_style)(void *, const char *), void *map_style_data, int *error);

/** Free all memory associated with a highlighting pattern.
    It is acceptable to pass a @c NULL pointer.
*/
T3_HIGHLIGHT_API void t3_highlight_free(t3_highlight_t *highlight);

/** Find the next highlighting match in a subject string.
    @param highlight The highlighting pattern to use.
    @param str The string to search in.
    @param size The length of the string in bytes.
    @param match The ::t3_highlight_match_t structure to store the result.
    @return A boolean indicating whether the end of the line has been reached.

    This function should be called repeatedly to find all the highlighting
    matches in the line. The @p match parameter should be reset (using either
    ::t3_highlight_next_line or :: t3_highlight_reset) before the first call to
    this function, because it holds the intermediate state information.

    The following code demonstrates how to highlight a single line. It assumes
    that a ::t3_highlight_t struct named @c highlight has been previously
    created, as well as a ::t3_highlight_match_t named @c match. The line data
    is stored in @c line with @c line_length bytes.
    @code
        t3_bool match_result;
        size_t begin = 0;

        t3_highlight_next_line(match);
        do {
            match_result = t3_highlight_match(highlight, line, line_length, match);
            size_t begin = t3_highlight_get_start(match),
                start = t3_highlight_get_match_start(match),
                end = t3_highlight_get_end(match);
            // Output bytes from 'begin' up to 'start' using t3_highlight_get_begin_attr(match) as attribute
            // Output bytes from 'start' up to 'end' using t3_highlight_get_match_attr(match) as attribute
        } while (match_result);
    @endcode

    Note that both the pre-match section from 'begin' to 'start', and the match
    section from 'start' to 'end' may be empty.
*/
T3_HIGHLIGHT_API t3_bool t3_highlight_match(const t3_highlight_t *highlight,
	const char *str, size_t size, t3_highlight_match_t *match);

/** Allocate and initialize a new ::t3_highlight_match_t structure. */
T3_HIGHLIGHT_API t3_highlight_match_t *t3_highlight_new_match(void);
/** Free ::t3_highlight_match_t structure.
    It is acceptable to pass a @c NULL pointer.
*/
T3_HIGHLIGHT_API void t3_highlight_free_match(t3_highlight_match_t *match);
/** Reset a ::t3_highlight_match_t structure.
    @param match The ::t3_highlight_match_t structure to reset.
    @param state The state to which to initialize @p match.

    State must be a valid state index. Valid indices are retrieved as the
    return values of ::t3_highlight_get_state and ::t3_highlight_next_line.
    Furthermore, @c 0 is the initial state, and is therefore always valid.
*/
T3_HIGHLIGHT_API void t3_highlight_reset(t3_highlight_match_t *match, int state);
/** Get the start index of a result.
    This is equal to the end index of the previous result.
*/
T3_HIGHLIGHT_API size_t t3_highlight_get_start(t3_highlight_match_t *match);
/** Get the start of the match. */
T3_HIGHLIGHT_API size_t t3_highlight_get_match_start(t3_highlight_match_t *match);
/** Get the end of the match. */
T3_HIGHLIGHT_API size_t t3_highlight_get_end(t3_highlight_match_t *match);
/** Get the attribute for the pre-match section of the result. */
T3_HIGHLIGHT_API int t3_highlight_get_begin_attr(t3_highlight_match_t *match);
/** Get the attribute for the match section of the result. */
T3_HIGHLIGHT_API int t3_highlight_get_match_attr(t3_highlight_match_t *match);
/** Get the state represented by @p match. */
T3_HIGHLIGHT_API int t3_highlight_get_state(t3_highlight_match_t *match);
/** Set up @p match for highlighting the next line of input. */
T3_HIGHLIGHT_API int t3_highlight_next_line(t3_highlight_match_t *match);

/** Get a string description for an error code.
    @param error The error code returned by a function in libt3highlight.
    @return An internationalized string description for the error code.
*/
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
