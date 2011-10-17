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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pcre.h>

#include "highlight.h"

#define RETURN_ERROR(x) do { if (error != NULL) *error = (x); goto return_error; } while (0)

static const char map_schema[] = {
#include "map.bytes"
};

static t3_config_t *load_map(int *error) {
	t3_config_schema_t *schema = NULL;
	t3_config_error_t local_error;
	t3_config_t *map;
	FILE *file;

	/* FIXME: should we retrieve a list from elsewhere as well? User's home dir? */
	if ((file = fopen(DATADIR "/" "lang.map", "r")) == NULL)
		RETURN_ERROR(T3_ERR_ERRNO);

	map = t3_config_read_file(file, &local_error, NULL);
	fclose(file);
	if (map == NULL)
		RETURN_ERROR(local_error.error);

	if ((schema = t3_config_read_schema_buffer(map_schema, sizeof(map_schema), &local_error, NULL)) == NULL) {
		t3_config_delete(map);
		RETURN_ERROR(local_error.error != T3_ERR_OUT_OF_MEMORY ? T3_ERR_INTERNAL : local_error.error);
	}

	if (!t3_config_validate(map, schema, NULL, 0))
		RETURN_ERROR(T3_ERR_INVALID_FORMAT);
	t3_config_delete_schema(schema);

	/* FIXME: check that the name-pattern (if it exists) matches the name. If not, there is a problem. */

	return map;

return_error:
	t3_config_delete_schema(schema);
	return NULL;
}

t3_highlight_lang_t *t3_highlight_list(int *error) {
	t3_config_t *map, *lang, *ptr;
	t3_highlight_lang_t *retval = NULL;
	int count;

	if ((map = load_map(error)) == NULL)
		return NULL;

	lang = t3_config_get(map, "lang");
	for (count = 0, ptr = t3_config_get(lang, NULL); ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {}

	if ((retval = malloc((count + 1) * sizeof(t3_highlight_lang_t))) == NULL)
		RETURN_ERROR(T3_ERR_OUT_OF_MEMORY);

	for (count = 0, ptr = t3_config_get(lang, NULL); ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {
		retval[count].name = t3_config_take_string(t3_config_get(ptr, "name"));
		retval[count].lang_file = t3_config_take_string(t3_config_get(ptr, "lang-file"));
	}

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

static t3_highlight_t *load_by_xname(const char *regex_name, const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int *error)
{
	t3_config_t *map, *ptr;
	pcre *pcre;
	int ovector[30];

	if ((map = load_map(error)) == NULL)
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
				map_style, map_style_data, error);
			t3_config_delete(map);
			return result;
		}

	}
	t3_config_delete(map);
	if (error != NULL)
		*error = T3_ERR_NO_SYNTAX;
	return NULL;
}

t3_highlight_t *t3_highlight_load_by_filename(const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int *error)
{
#if 0
	const char *file_name = strrchr(name, '/'); /* FIXME: use platform dependent dir separators */
	if (file_name == NULL)
		file_name = name;
#endif
	return load_by_xname("file-regex", name, map_style, map_style_data, error);
}

t3_highlight_t *t3_highlight_load_by_langname(const char *name, int (*map_style)(void *, const char *),
		void *map_style_data, int *error)
{
	return load_by_xname("name-regex", name, map_style, map_style_data, error);
}

/* FIXME: this now uses open_from_path, but do we always want that? Perhaps we should
   simply use open if the name contains a dir separator. */
t3_highlight_t *t3_highlight_load(const char *name, int (*map_style)(void *, const char *), void *map_style_data, int *error) {
	t3_config_opts_t opts;
	const char *path[] = { DATADIR, NULL };
	t3_config_t *config;
	t3_config_error_t config_error;
	t3_highlight_t *result;
	FILE *file;

	/* FIXME: do we want to add a path from the environment? User home directory? */

	if ((file = t3_config_open_from_path(path, name, 0)) == NULL) {
		if (error != NULL)
			*error = T3_ERR_ERRNO;
		return NULL;
	}

	opts.flags = T3_CONFIG_INCLUDE_DFLT;
	opts.include_callback.dflt.path = path;
	opts.include_callback.dflt.flags = 0;

	config = t3_config_read_file(file, &config_error, &opts);
	fclose(file);
	if (config == NULL) {
		if (error != NULL)
			*error = config_error.error;
		return NULL;
	}

	result = t3_highlight_new(config, map_style, map_style_data, error);
	t3_config_delete(config);

	return result;
}
