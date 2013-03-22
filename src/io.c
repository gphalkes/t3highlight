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
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pcre.h>

#include "highlight.h"
#include "internal.h"

#ifndef HAS_STRDUP
/** strdup implementation if none is provided by the environment. */
char *_t3_highlight_strdup(const char *str) {
	char *result;
	size_t len = strlen(str) + 1;

	if ((result = malloc(len)) == NULL)
		return NULL;
	memcpy(result, str, len);
	return result;
}
#endif

static const char map_schema[] = {
#include "map.bytes"
};

/** Load a single language map. */
static t3_config_t *load_single_map(const char *name, int flags, t3_highlight_error_t *error) {
	t3_config_schema_t *schema = NULL;
	t3_config_error_t local_error;
	t3_config_t *map;
	t3_config_opts_t opts;
	FILE *file;

	if ((file = fopen(name, "r")) == NULL) {
		_t3_highlight_set_error(error, T3_ERR_ERRNO, 0, name, NULL, flags);
		goto return_error;
	}

	opts.flags = (flags & T3_HIGHLIGHT_VERBOSE_ERROR) ? (T3_CONFIG_VERBOSE_ERROR | T3_CONFIG_ERROR_FILE_NAME) : 0;
	map = t3_config_read_file(file, &local_error, &opts);
	fclose(file);
	if (map == NULL) {
		_t3_highlight_set_error(error, local_error.error, local_error.line_number, local_error.file_name, local_error.extra, flags);
		free(local_error.file_name);
		free(local_error.extra);
		goto return_error;
	}

	if ((schema = t3_config_read_schema_buffer(map_schema, sizeof(map_schema), &local_error, NULL)) == NULL) {
		t3_config_delete(map);
		_t3_highlight_set_error_simple(error, local_error.error != T3_ERR_OUT_OF_MEMORY ? T3_ERR_INTERNAL : local_error.error, flags);
		goto return_error;
	}

	if (!t3_config_validate(map, schema, &local_error, T3_CONFIG_VERBOSE_ERROR | T3_CONFIG_ERROR_FILE_NAME)) {
		_t3_highlight_set_error(error, (flags & T3_HIGHLIGHT_VERBOSE_ERROR) ? local_error.error : T3_ERR_INVALID_FORMAT,
			local_error.line_number, local_error.file_name, local_error.extra, flags);
		free(local_error.file_name);
		free(local_error.extra);
		goto return_error;
	}
	t3_config_delete_schema(schema);

	return map;

return_error:
	t3_config_delete_schema(schema);
	return NULL;
}

/** Merge two maps, destroying the second one in the process. */
static void merge(t3_config_t *main, t3_config_t *map) {
	t3_config_t *main_lang = t3_config_get(main, "lang");
	t3_config_t *map_lang = t3_config_get(map, "lang");
	t3_config_t *ptr;

	while ((ptr = t3_config_get(map_lang, NULL)) != NULL) {
		t3_config_unlink_from_list(map_lang, ptr);
		t3_config_add_existing(main_lang, NULL, ptr);
	}
	t3_config_delete(map);
}

static t3_config_t *load_map(int flags, t3_highlight_error_t *error) {
	t3_config_t *full_map = NULL, *map;
	char *xdg_map;

	if ((full_map = t3_config_new()) == NULL) {
		_t3_highlight_set_error_simple(error, T3_ERR_OUT_OF_MEMORY, flags);
		goto return_error;
	}
	if (!t3_config_add_plist(full_map, "lang", error == NULL ? NULL : &error->error))
		goto return_error;

	xdg_map = t3_config_xdg_get_path(T3_CONFIG_XDG_DATA_HOME, "libt3highlight", strlen("lang.map"));
	if (xdg_map != NULL) {
		strcat(xdg_map, "/lang.map");
		map = load_single_map(xdg_map, 0, NULL);
		free(xdg_map);
		if (map != NULL)
			merge(full_map, map);
	}

	if ((map = load_single_map(DATADIR "/" "lang.map", 0, error)) == NULL)
		goto return_error;

	merge(full_map, map);
	return full_map;

return_error:
	t3_config_delete(full_map);
	return NULL;
}

t3_highlight_lang_t *t3_highlight_list(int flags, t3_highlight_error_t *error) {
	t3_config_t *map, *lang, *ptr;
	t3_highlight_lang_t *retval = NULL;
	int count;

	if ((map = load_map(flags, error)) == NULL)
		return NULL;

	lang = t3_config_get(map, "lang");
	for (count = 0, ptr = t3_config_get(lang, NULL); ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {}

	if ((retval = malloc((count + 1) * sizeof(t3_highlight_lang_t))) == NULL) {
		_t3_highlight_set_error_simple(error, T3_ERR_OUT_OF_MEMORY, flags);
		goto return_error;
	}

	for (count = 0, ptr = t3_config_get(lang, NULL); ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {
		retval[count].name = t3_config_take_string(t3_config_get(ptr, "name"));
		retval[count].lang_file = t3_config_take_string(t3_config_get(ptr, "lang-file"));
	}
	t3_config_delete(map);

	retval[count].name = NULL;
	retval[count].lang_file = NULL;
	return retval;

return_error:
	free(retval);
	t3_config_delete(map);
	return NULL;
}

void t3_highlight_free_list(t3_highlight_lang_t *list) {
	int i;

	if (list == NULL)
		return;

	for (i = 0; list[i].name != NULL; i++) {
		free((char *) list[i].name);
		free((char *) list[i].lang_file);
	}

	free(list);
}

/** Load a highlight file by file name or language name.
    @param regex_name The name of the configuration key containing the regular expression to match.
    @param name The name to match with the regex.
    @param map_style See ::t3_highlight_load.
    @param map_style_data See ::t3_highlight_load.
    @param map_style_flags See ::t3_highlight_load.
    @param map_style_error Location to store an error code.
*/
static t3_highlight_t *load_by_xname(const char *regex_name, const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, t3_highlight_error_t *error)
{
	t3_config_t *map, *ptr;
	pcre *pcre;
	int ovector[30];

	if ((map = load_map(flags, error)) == NULL)
		return NULL;

	for (ptr = t3_config_get(t3_config_get(map, "lang"), NULL); ptr != NULL; ptr = t3_config_get_next(ptr)) {
		const char *error_message;
		int error_offset;
		int pcre_result;
		t3_config_t *regex;

		if ((regex = t3_config_get(ptr, regex_name)) == NULL)
			continue;

		if ((pcre = pcre_compile(t3_config_get_string(regex), 0, &error_message, &error_offset, NULL)) == NULL)
			continue;

		pcre_result = pcre_exec(pcre, NULL, name, strlen(name), 0, 0, ovector, sizeof(ovector) / sizeof(ovector[0]));
		pcre_free(pcre);
		if (pcre_result >= 0) {
			t3_highlight_t *result = t3_highlight_load(t3_config_get_string(t3_config_get(ptr, "lang-file")),
				map_style, map_style_data, flags | T3_HIGHLIGHT_USE_PATH, error);
			t3_config_delete(map);
			return result;
		}

	}
	t3_config_delete(map);
	if (error != NULL) {
		error->error = T3_ERR_NO_SYNTAX;
		if (flags & T3_HIGHLIGHT_VERBOSE_ERROR) {
			error->line_number = 0;
			error->file_name = NULL;
			error->extra = NULL;
		}
	}
	return NULL;
}

t3_highlight_t *t3_highlight_load_by_filename(const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, t3_highlight_error_t *error)
{
	return load_by_xname("file-regex", name, map_style, map_style_data, flags, error);
}

t3_highlight_t *t3_highlight_load_by_langname(const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int flags, t3_highlight_error_t *error)
{
	return load_by_xname("name-regex", name, map_style, map_style_data, flags, error);
}

t3_highlight_t *t3_highlight_load(const char *name, int (*map_style)(void *, const char *), void *map_style_data,
		int flags, t3_highlight_error_t *error)
{
	t3_config_opts_t opts;
	const char *path[] = { NULL, NULL, NULL };
	char *xdg_path = NULL;
	t3_config_t *config = NULL;
	t3_config_error_t config_error;
	t3_highlight_t *result;
	FILE *file = NULL;

	/* Setup path. */
	path[0] = xdg_path = t3_config_xdg_get_path(T3_CONFIG_XDG_DATA_HOME, "libt3highlight", 0);
	path[path[0] == NULL ? 0 : 1] = DATADIR;

	if (flags & T3_HIGHLIGHT_USE_PATH) {
		if ((file = t3_config_open_from_path(path, name, 0)) == NULL) {
			_t3_highlight_set_error(error, T3_ERR_ERRNO, 0, name, NULL, flags);
			goto return_error;
		}
	} else {
		if ((file = fopen(name, "r")) == NULL) {
			_t3_highlight_set_error(error, T3_ERR_ERRNO, 0, name, NULL, flags);
			goto return_error;
		}
	}

	opts.flags = T3_CONFIG_INCLUDE_DFLT;
	if (flags & T3_HIGHLIGHT_VERBOSE_ERROR)
		opts.flags |= T3_CONFIG_VERBOSE_ERROR | T3_CONFIG_ERROR_FILE_NAME;
	opts.include_callback.dflt.path = path;
	opts.include_callback.dflt.flags = 0;

	if ((config = t3_config_read_file(file, &config_error, &opts)) == NULL) {
		_t3_highlight_set_error(error, config_error.error, config_error.line_number,
			config_error.file_name == NULL ? name : config_error.file_name, config_error.extra, flags);
		free(config_error.file_name);
		goto return_error;
	}

	free(xdg_path);
	xdg_path = NULL;
	fclose(file);
	file = NULL;


	if ((result = t3_highlight_new(config, map_style, map_style_data, flags, error)) == NULL) {
		if ((flags & T3_HIGHLIGHT_VERBOSE_ERROR) && error->file_name == NULL)
			error->file_name = _t3_highlight_strdup(name);
		goto return_error;
	}

	if ((result->lang_file = _t3_highlight_strdup(name)) == NULL) {
		_t3_highlight_set_error_simple(error, T3_ERR_OUT_OF_MEMORY, flags);
		goto return_error;
	}

	t3_config_delete(config);

	return result;

return_error:
	t3_config_delete(config);
	free(xdg_path);
	if (file != NULL) {
		int save_errno = errno;
		fclose(file);
		errno = save_errno;
	}
	return NULL;
}

const char *t3_highlight_get_langfile(const t3_highlight_t *highlight) {
	return highlight == NULL ? NULL : highlight->lang_file;
}

/* FIXME: do we want to return what type of result we are returning?
	i.e. whether it is from a modeline/emacs language identifier or from
	autodetection regexes?
*/
/* FIXME: do we really want to compile the patterns everytime we call this? Probably
   not, but doing it another way is difficult. */

char *t3_highlight_detect(const char *line, size_t line_length, t3_bool first, int flags, t3_highlight_error_t *error) {
	const char *error_message;
	int error_offset;
	int ovector[30];
	char *result = NULL;
	pcre *pcre;

	if (line == NULL)
		return NULL;

	if ((pcre = pcre_compile("-\\*-\\s*(?:mode:\\s*)([^\\s;]);?.*-\\*-", PCRE_CASELESS, &error_message, &error_offset, NULL)) == NULL) {
		_t3_highlight_set_error_simple(error, T3_ERR_INTERNAL, flags);
		goto return_error;
	}
	if (pcre_exec(pcre, NULL, line, line_length, 0, 0, ovector, sizeof(ovector) / sizeof(ovector[0])) > 0)
		goto pattern_succeeded;
	pcre_free(pcre);
	if ((pcre = pcre_compile("\\s(?:vim?|ex): .*[: ]syntax=([^\\s:]+)", 0, &error_message, &error_offset, NULL)) == NULL) {
		_t3_highlight_set_error_simple(error, T3_ERR_INTERNAL, flags);
		goto return_error;
	}
	if (pcre_exec(pcre, NULL, line, line_length, 0, 0, ovector, sizeof(ovector) / sizeof(ovector[0])) > 0)
		goto pattern_succeeded;
	pcre_free(pcre);

	if (first) {
		t3_config_t *map, *language;
		const char *regex;

		if ((map = load_map(flags, error)) == NULL)
			return NULL;

		for (language = t3_config_get(t3_config_get(map, "lang"), NULL); language != NULL; language = t3_config_get_next(language)) {
			if ((regex = t3_config_get_string(t3_config_get(language, "first-line-regex"))) == NULL)
				continue;

			if ((pcre = pcre_compile(regex, 0, &error_message, &error_offset, NULL)) == NULL)
				continue;

			if (pcre_exec(pcre, NULL, line, line_length, 0, 0, ovector, sizeof(ovector) / sizeof(ovector[0])) < 0) {
				pcre_free(pcre);
				continue;
			}
			pcre_free(pcre);
			result = t3_config_take_string(t3_config_get(language, "name"));
			t3_config_delete(map);
			return result;
		}
		t3_config_delete(map);
	}

	if (error != NULL)
		error->error = T3_ERR_SUCCESS;
	return NULL;

pattern_succeeded:
	pcre_free(pcre);
	if ((result = malloc(ovector[3] - ovector[2] + 1)) == NULL)
		return NULL;
	memcpy(result, line + ovector[2], ovector[3] - ovector[2]);
	result[ovector[3] - ovector[2]] = 0;
	return result;

return_error:
	return NULL;
}

t3_highlight_t *t3_highlight_load_by_detect(const char *line, size_t line_length, t3_bool first,
		int (*map_style)(void *, const char *), void *map_style_data, int flags, t3_highlight_error_t *error)
{
	char *language_name = t3_highlight_detect(line, line_length, first, flags, error);
	t3_highlight_t *result;

	if (language_name == NULL)
		return NULL;
	result = t3_highlight_load_by_langname(language_name, map_style, map_style_data, flags, error);
	free(language_name);
	return result;
}
