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
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <t3config/config.h>
#include <t3highlight/highlight.h>

#include "optionMacros.h"
#include "t3highlight.h"

typedef struct {
	char *tag;
	char *start;
	char *end;
} style_def_t;

static style_def_t *styles;

static const char style_schema[] = {
#include "style.bytes"
};

static int option_verbose;
static const char *option_language;
static const char *option_style;
static const char *option_input;
#ifdef DEBUG
static const char *option_language_file;
#endif

/** Alert the user of a fatal error and quit.
    @param fmt The format string for the message. See fprintf(3) for details.
    @param ... The arguments for printing.
*/
void fatal(const char *fmt, ...) {
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(EXIT_FAILURE);
}

/** Duplicate a string but exit on allocation failure */
char *safe_strdup(const char *str) {
	char *result;
	size_t len = strlen(str) + 1;

	if ((result = malloc(len)) == NULL)
		fatal("Out of memory\n");
	memcpy(result, str, len);
	return result;
}

static PARSE_FUNCTION(parse_args)
	OPTIONS
		OPTION('v', "verbose", NO_ARG)
			option_verbose = 1;
		END_OPTION
		OPTION('l', "language", REQUIRED_ARG)
			if (option_language != NULL || option_language_file != NULL)
				fatal("Error: only one language option allowed\n");
			option_language = optArg;
		END_OPTION
		LONG_OPTION("language-file", REQUIRED_ARG)
			if (option_language != NULL || option_language_file != NULL)
				fatal("Error: only one language option allowed\n");
			option_language_file = optArg;
		END_OPTION
		OPTION('L', "list", NO_ARG)
			int i, error;
			t3_highlight_lang_t *list = t3_highlight_list(&error);

			if (list == NULL)
				fatal("Error retrieving listing: %s\n", t3_highlight_strerror(error));

			printf("Available languages:\n");
			for (i = 0; list[i].name != NULL; i++)
				printf("  %s\n", list[i].name);

			t3_highlight_free_list(list);

			printf("\nAvaliable styles:\n");
			printf("  FIXME: list styles\n");
			exit(EXIT_SUCCESS);
		END_OPTION
		OPTION('s', "style", REQUIRED_ARG)
			if (option_style != NULL)
				fatal("Error: only one style option allowed\n");
			option_style = optArg;
		END_OPTION
		OPTION('h', "help", NO_ARG)
			printf("Usage: t3highlight [<options>] [<file>]\n"
				"  -l<lang>,--language=<lang>      Highlight using language <lang>\n"
				"  --language-file=<file>          Load highlighting description file <file>\n"
				"  -L,--list                       List available languages and styles\n"
				"  -s<style>,--style=<style>       Output using style <style>\n"
				"  -v,--verbose                    Enable verbose output mode\n"
			);
		END_OPTION
		DOUBLE_DASH
			NO_MORE_OPTIONS;
		END_OPTION

		fatal("No such option " OPTFMT "\n", OPTPRARG);
	NO_OPTION
		//FIXME: handle more than one option
		if (option_input != NULL)
			fatal("Multiple inputs not implemented yet\n");
		option_input = optcurrent;
	END_OPTIONS
END_FUNCTION


static int map_style(void *_styles, const char *name) {
	style_def_t *styles = _styles;
	int i;

	for (i = 0; styles[i].tag != NULL; i++) {
		if (strcmp(styles[i].tag, name) == 0)
			return i;
	}
	return 0;
}

static char *expand_string(const char *str, t3_bool expand_escapes) {
	char *result;
	if (str == NULL)
		return safe_strdup("");

	result = safe_strdup(str);
	if (expand_escapes)
		parse_escapes(result);
	return result;
}

static style_def_t *load_style(const char *name) {
	FILE *style_file;
	t3_config_t *style_config, *styles, *ptr;
	t3_config_schema_t *schema;
	t3_config_error_t config_error;

	t3_bool expand_escapes = t3_false;
	style_def_t *result;
	const char *path[] = { DATADIR, NULL };
	int count;

	//FIXME: search in appropriate dirs (DATADIR and environment/user home)
	if ((style_file = t3_config_open_from_path(path, name, 0)) == NULL)
		fatal("Can't open '%s': %s\n", name, strerror(errno));

	if ((style_config = t3_config_read_file(style_file, &config_error, NULL)) == NULL)
		fatal("Error reading style file: %d: %s\n", config_error.line_number, t3_config_strerror(config_error.error));
	fclose(style_file);

	if ((schema = t3_config_read_schema_buffer(style_schema, sizeof(style_schema), &config_error, NULL)) == NULL) {
		if (config_error.error != T3_ERR_OUT_OF_MEMORY)
			config_error.error = T3_ERR_INTERNAL;
		fatal("Error reading style file: %s\n", t3_config_strerror(config_error.error));
	}

	if (!t3_config_validate(style_config, schema, &config_error, T3_CONFIG_VERBOSE_ERROR)) {
		//FIXME: print more information on what is wrong
		fatal("Error reading style file: %s\n", "invalid format");
	}
	t3_config_delete_schema(schema);

	expand_escapes = t3_config_get_bool(t3_config_get(style_config, "expand-escapes"));
	styles = t3_config_get(t3_config_get(style_config, "styles"), NULL);

	//FIXME: implement special handling of "normal" style (must be style 0)
	for (count = 0, ptr = styles; ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {}

	count += 2;

	if ((result = malloc(sizeof(style_def_t) * count)) == NULL)
		fatal("Out of memory\n");

	result[0].tag = safe_strdup("normal");
	result[0].start = safe_strdup("");
	result[0].end = safe_strdup("");

	for (count = 1, ptr = styles; ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {
		result[count].tag = safe_strdup(t3_config_get_name(ptr));
		result[count].start = expand_string(t3_config_get_string(t3_config_get(ptr, "start")), expand_escapes);
		result[count].end = expand_string(t3_config_get_string(t3_config_get(ptr, "end")), expand_escapes);
	}
	result[count].tag = NULL;

	t3_config_delete(style_config);

	return result;
}

static void highlight_file(const char *name, t3_highlight_t *highlight) {
 	FILE *input;
	char *line = NULL;
	size_t n;
	ssize_t chars_read;

	t3_highlight_match_t *match = t3_highlight_new_match(highlight);
	t3_bool match_result;

	//FIXME: use proper error message
	if (match == NULL)
		fatal("Out of memory\n");

	if (name == NULL)
		input = stdin;
	else if ((input = fopen(name, "rb")) == NULL)
		fatal("Can't open '%s': %s\n", name, strerror(errno));

	while ((chars_read = getline(&line, &n, input)) > 0) {
		if (line[chars_read - 1] == '\n')
			chars_read--;

		t3_highlight_next_line(match);
		do {
			match_result = t3_highlight_match(match, line, chars_read);
			size_t start = t3_highlight_get_start(match),
				match_start = t3_highlight_get_match_start(match),
				end = t3_highlight_get_end(match);
			if (start != match_start) {
				fputs(styles[t3_highlight_get_begin_attr(match)].start, stdout);
				printf("%.*s", (int) (match_start - start), line + start);
				fputs(styles[t3_highlight_get_begin_attr(match)].end, stdout);
			}
			if (match_start != end) {
				fputs(styles[t3_highlight_get_match_attr(match)].start, stdout);
				printf("%.*s", (int) (end - match_start), line + match_start);
				fputs(styles[t3_highlight_get_match_attr(match)].end, stdout);
			}
		} while (match_result);
		putchar('\n');
	}
	t3_highlight_free_match(match);
	fclose(input);
	free(line);
}

int main(int argc, char *argv[]) {
	t3_highlight_t *highlight;
	int error, i;
	//FIXME: setlocale etc. for gettext
	//FIXME: open style by file name only if so specified on the cli

	parse_args(argc, argv);

	if (option_style == NULL)
		styles = load_style("esc.style");
	else
		styles = load_style(option_style);

	if (strcmp(option_input, "-") == 0)
		option_input = NULL;

	if (option_language == NULL && option_input == NULL) {
		fatal("-l/--language required for reading from standard input\n");
	} else if (option_language_file != NULL) {
		if ((highlight = t3_highlight_load(option_language_file, map_style, styles, T3_HIGHLIGHT_UTF8, &error)) == NULL)
			fatal("Error loading highlighting patterns: %s\n", t3_highlight_strerror(error));
	} else if (option_language != NULL) {
		if ((highlight = t3_highlight_load_by_langname(option_language, map_style, styles, T3_HIGHLIGHT_UTF8, &error)) == NULL)
			fatal("Error loading highlighting patterns: %s\n", t3_highlight_strerror(error));
	} else {
		if ((highlight = t3_highlight_load_by_filename(option_input, map_style, styles, T3_HIGHLIGHT_UTF8, &error)) == NULL)
			fatal("Error loading highlighting patterns: %s\n", t3_highlight_strerror(error));
	}

	highlight_file(option_input, highlight);

	for (i = 0; styles[i].tag != NULL; i++) {
		free(styles[i].tag);
		free(styles[i].start);
		free(styles[i].end);
	}
	free(styles);

	t3_highlight_free(highlight);
	return EXIT_SUCCESS;
}
