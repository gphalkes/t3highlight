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
	const char *tag;
	const char *start;
	const char *end;
} style_def_t;

static style_def_t *styles;
static style_def_t default_styles[] = {
	{ "normal", "", "" },
	{ NULL, NULL, NULL }
};

static int option_verbose;
static const char *option_language;
static const char *option_style;
static const char *option_input;

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

static PARSE_FUNCTION(parse_args)
	OPTIONS
		OPTION('v', "verbose", NO_ARG)
			option_verbose = 1;
		END_OPTION
		OPTION('l', "language", REQUIRED_ARG)
			//FIXME: do proper search for language file
			if (option_language != NULL)
				fatal("Error: only one language option allowed\n");
			option_language = optArg;
		END_OPTION
		OPTION('s', "style", REQUIRED_ARG)
			//FIXME: do proper search for style file
			if (option_style != NULL)
				fatal("Error: only one style option allowed\n");
			option_style = optArg;
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


static int map_style(style_def_t *styles, const char *name) {
	int i;
	for (i = 0; styles[i].tag != NULL; i++) {
		if (strcmp(styles[i].tag, name) == 0)
			return i;
	}
	return 0;
}

static t3_highlight_t *load_highlight(const char *name) {
	FILE *highlight_file;
	t3_config_t *highlight_config;
	t3_config_error_t config_error;
	t3_highlight_t *highlight;
	int error;

	if ((highlight_file = fopen(name, "rb")) == NULL)
		fatal("Can't open '%s': %s\n", name, strerror(errno));

	if ((highlight_config = t3_config_read_file(highlight_file, &config_error, NULL)) == NULL)
		fatal("Error reading highlighting patterns: %s @ %d\n", t3_config_strerror(config_error.error), config_error.line_number);

	if ((highlight = t3_highlight_new(highlight_config, (int (*)(void *, const char *)) map_style, styles, &error)) == NULL)
		fatal("Error loading highlighting patterns: %s\n", t3_highlight_strerror(error));

	fclose(highlight_file);
	t3_config_delete(highlight_config);
	return highlight;
}

static const char *expand_string(const char *str, t3_bool expand_escapes) {
	char *result;
	if (str == NULL)
		return "";

	result = strdup(str);
	if (expand_escapes)
		parse_escapes(result);
	return result;
}

static style_def_t *load_style(const char *name) {
	FILE *style_file;
	t3_config_t *style_config, *styles, *expand_escapes_conf;
	t3_config_error_t config_error;
	style_def_t *result;
	int count;
	t3_bool expand_escapes = t3_false;

	if ((style_file = fopen(name, "rb")) == NULL)
		fatal("Can't open '%s': %s\n", name, strerror(errno));

	if ((style_config = t3_config_read_file(style_file, &config_error, NULL)) == NULL)
		fatal("Error reading style file: %s @ %d\n", t3_config_strerror(config_error.error), config_error.line_number);

	//FIXME: use libt3config validation when available
	if ((styles = t3_config_get(style_config, "styles")) == NULL || t3_config_get_type(styles) != T3_CONFIG_SECTION)
		fatal("Invalid style definition\n");
	styles = t3_config_get(styles, NULL);

	if ((expand_escapes_conf = t3_config_get(style_config, "expand-escapes")) != NULL) {
		if (t3_config_get_type(expand_escapes_conf) != T3_CONFIG_BOOL)
			fatal("Invalid style definition\n");
		expand_escapes = t3_config_get_bool(expand_escapes_conf);
	}

	//FIXME: implement special handling of "normal" style (must be style 0)
	for (count = 0; styles != NULL; count++, styles = t3_config_get_next(styles)) {}
	count++;
	if ((result = malloc(sizeof(style_def_t) * count)) == NULL)
		fatal("Out of memory\n");
	result[0].tag = "normal";
	result[0].start = "";
	result[0].end = "";
	styles = t3_config_get(t3_config_get(style_config, "styles"), NULL);
	for (count = 1; styles != NULL; count++, styles = t3_config_get_next(styles)) {
		result[count].tag = t3_config_get_name(styles);
		result[count].start = expand_string(t3_config_get_string(t3_config_get(styles, "start")), expand_escapes);
		result[count].end = expand_string(t3_config_get_string(t3_config_get(styles, "end")), expand_escapes);
	}
	return result;
}

static void highlight_file(const char *name, t3_highlight_t *highlight) {
 	FILE *input;
	char *line = NULL;
	size_t n;
	ssize_t chars_read;
	size_t begin;

	t3_highlight_match_t *match = t3_highlight_new_match();
	t3_bool match_result;

	//FIXME: use proper error message
	if (match == NULL)
		fatal("Out of memory\n");

	if ((input = fopen(name, "rb")) == NULL)
		fatal("Can't open '%s': %s\n", name, strerror(errno));

	while ((chars_read = getline(&line, &n, input)) > 0) {
		if (line[chars_read - 1] == '\n')
			chars_read--;

		t3_highlight_next_line(match);
		begin = 0;
		do {
			match_result = t3_highlight_match(highlight, line, chars_read, match);
			size_t start = t3_highlight_get_start(match), end = t3_highlight_get_end(match);
			if (begin != start) {
				fputs(styles[t3_highlight_get_begin_attr(match)].start, stdout);
				printf("%.*s", (int) (start - begin), line + begin);
				fputs(styles[t3_highlight_get_begin_attr(match)].end, stdout);
			}
			if (start != end) {
				fputs(styles[t3_highlight_get_match_attr(match)].start, stdout);
				printf("%.*s", (int) (end - start), line + start);
				fputs(styles[t3_highlight_get_match_attr(match)].end, stdout);
			}
			begin = end;
		} while (match_result);
		putchar('\n');
	}
	t3_highlight_free_match(match);
	fclose(input);
	free(line);
}

int main(int argc, char *argv[]) {
	t3_highlight_t *highlight;
	//FIXME: setlocale etc. for gettext

	parse_args(argc, argv);

	if (option_style == NULL)
		styles = default_styles;
	else
		styles = load_style(option_style);

	if (option_language == NULL)
		fatal("Language auto-detection not implemented yet\n");
	else
		highlight = load_highlight(option_language);

	highlight_file(option_input, highlight);

	t3_highlight_free(highlight);
	return EXIT_SUCCESS;

}
