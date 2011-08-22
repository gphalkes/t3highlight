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
#include <unistd.h>
#include <t3config/config.h>
#include <t3highlight/highlight.h>

struct style_def_t {
	const char *tag;
	const char *code;
} styles[] = {
	{ "normal", "" },
	{ "keyword", "\033[34;1m" },
	{ "string", "\033[35m" },
	{ "string-escape", "\033[35;1m" },
	{ "comment", "\033[32m" },
	{ "number", "\033[36m" },
	{ "misc", "\033[33m" },
	{ "comment-keyword", "\033[32;1m" },
	{ NULL, NULL }
};

int map_style(struct style_def_t *styles, const char *name) {
	int i;
	for (i = 1; styles[i].tag != NULL; i++) {
		if (strcmp(styles[i].tag, name) == 0)
			return i;
	}
	return 0;
}

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

int main(int argc, char *argv[]) {
	FILE *pattern_file;
	t3_config_t *pattern_config;
	t3_config_error_t config_error;
	int error;
	t3_highlight_t *pattern;
	int c;

	//FIXME: make this proper
	while ((c = getopt(argc, argv, "h")) >= 0) {
		switch (c) {
			default:
				exit(EXIT_FAILURE);
			case 'h':
				printf("Usage: t3highlight <pattern file> <source file>\n");
				exit(EXIT_SUCCESS);
		}
	}

	if (argc - optind != 2)
		fatal("Need pattern and source file\n");


	if ((pattern_file = fopen(argv[optind], "rb")) == NULL)
		fatal("Can't open '%s': %m\n", argv[optind]);

	if ((pattern_config = t3_config_read_file(pattern_file, &config_error, NULL)) == NULL)
		fatal("Error reading patterns: %s @ %d\n", t3_config_strerror(config_error.error), config_error.line_number);

	pattern = t3_highlight_new(pattern_config, (int (*)(void *, const char *)) map_style, styles, &error);

	fclose(pattern_file);
	t3_config_delete(pattern_config);

 	FILE *input;
	char *line = NULL;
	size_t n;
	ssize_t chars_read;
	size_t begin;
	t3_highlight_match_t match_result = T3_HIGHLIGH_MATCH_INITIALIZER;

	if ((input = fopen(argv[optind + 1], "rb")) == NULL)
		fatal("Can't open '%s': %m\n", argv[optind + 1]);

	while ((chars_read = getline(&line, &n, input)) > 0) {
		if (line[chars_read - 1] == '\n')
			chars_read--;

		match_result.end = 0;
		begin = match_result.end;
		while (t3_highlight_match(pattern, line, chars_read, &match_result)) {
			if (begin != match_result.start) {
				fputs(styles[match_result.begin_attribute].code, stdout);
				printf("%.*s", (int) (match_result.start - begin), line + begin);
				fputs("\033[0m", stdout);
			}
			if (match_result.start != match_result.end) {
				fputs(styles[match_result.match_attribute].code, stdout);
				printf("%.*s", (int) (match_result.end - match_result.start), line + match_result.start);
				fputs("\033[0m", stdout);
			}
			begin = match_result.end;
		}
		if (begin != (size_t) chars_read) {
			fputs(styles[match_result.begin_attribute].code, stdout);
			printf("%.*s", (int) (chars_read - begin), line + begin);
		}
		putchar('\n');
	}

	fclose(input);

	t3_highlight_free(pattern);
	return EXIT_SUCCESS;

}
