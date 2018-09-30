/* Copyright (C) 2012 G.P. Halkes
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
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <t3config/config.h>
#include <t3highlight/highlight.h>
#include <unistd.h>

#include "t3highlight.h"

/* This header must be included after all the others to prevent issues with the
   definition of _. */
/* clang-format off */
#include "optionMacros.h"
/* clang-format on */

#define DEFAULT_STYLE "esc.style"

typedef struct {
  char *tag;
  char *start;
  char *end;
} style_def_t;

typedef struct {
  char *search;
  char *replace;
  size_t search_len;
  size_t replace_len;
} translation_t;

typedef struct tag_t {
  const char *name;
  const char *value;
  size_t name_len;
  struct tag_t *next;
} tag_t;

static style_def_t *styles;
static translation_t *translations;
static char *header;
static char *footer;
static tag_t *tags;

static const char style_schema[] = {
#include "style.bytes"
};

static int option_verbose;
static const char *option_language;
static char *option_style;
static const char *option_input;
static const char *option_language_file;
static const char *option_document_type;

static t3_bool set_tag(const char *name, const char *value);
static void write_data(const char *string, size_t size);
static void list_styles(void);
static void list_document_types(const char *name);

/** Alert the user of a fatal error and quit.
    @param fmt The format string for the message. See fprintf(3) for details.
    @param ... The arguments for printing.
*/
#ifdef __GNUC__
void fatal(const char *fmt, ...) __attribute__((noreturn));
#endif
void fatal(const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  exit(EXIT_FAILURE);
}

/** Duplicate a string but exit on allocation failure */
static char *safe_strdup(const char *str) {
  char *result;
  size_t len = strlen(str) + 1;

  if ((result = malloc(len)) == NULL) {
    fatal(_("Out of memory\n"));
  }
  memcpy(result, str, len);
  return result;
}

/* clang-format off */
static PARSE_FUNCTION(parse_args)
  t3_bool option_list_document_types = t3_false;
  OPTIONS
    OPTION('v', "verbose", NO_ARG)
      option_verbose = 1;
    END_OPTION
    OPTION('l', "language", REQUIRED_ARG)
      if (option_language != NULL || option_language_file != NULL) {
        fatal(_("Only one language option allowed\n"));
}
      option_language = optArg;
    END_OPTION
    LONG_OPTION("language-file", REQUIRED_ARG)
      if (option_language != NULL || option_language_file != NULL) {
        fatal(_("Only one language option allowed\n"));
}
      option_language_file = optArg;
    END_OPTION
    OPTION('L', "list", NO_ARG)
      t3_highlight_error_t error;
      int i;
      t3_highlight_lang_t *list = t3_highlight_list(T3_HIGHLIGHT_VERBOSE_ERROR, &error);

      if (list == NULL) {
        if (error.file_name != NULL) {
          fatal("%s:%d: %s\n", error.file_name, error.line_number,
                t3_highlight_strerror(error.error));
        } else {
          fatal(_("Error loading highlight listing: %s\n"), t3_highlight_strerror(error.error));
}
      }

      printf(_("Available languages:\n"));
      for (i = 0; list[i].name != NULL; i++) {
        printf("  %s\n", list[i].name);
}

      t3_highlight_free_list(list);

      putchar('\n');
      printf(_("Avaliable styles:\n"));
      list_styles();
      exit(EXIT_SUCCESS);
    END_OPTION
    OPTION('s', "style", REQUIRED_ARG)
      if (option_style != NULL) {
        fatal("Error: only one style option allowed\n");
}
      if ((option_style = malloc(strlen(optArg) + 7)) == NULL) {
        fatal("Out of memory");
}
      strcpy(option_style, optArg);
      if (strchr(option_style, '/') == NULL) {
        strcat(option_style, ".style");
}
    END_OPTION
    OPTION('h', "help", NO_ARG)
      printf("Usage: t3highlight [<options>] [<file>]\n"
        "  -d<type>,--document-type=<type> Output using document type <type>\n"
          "  -D,--list-document-types        List the document types for the current style\n"
        "  -l<lang>,--language=<lang>      Highlight using language <lang>\n"
        "  --language-file=<file>          Load highlighting description file <file>\n"
        "  -L,--list                       List available languages and styles\n"
        "  -s<style>,--style=<style>       Output using style <style>\n"
        "  -t<tag>,--tag=<tag>             Define tag <tag>, which must be <name>=<value>\n"
        "  -v,--verbose                    Enable verbose output mode\n"
      );
      exit(EXIT_SUCCESS);
    END_OPTION
    OPTION('d', "document-type", REQUIRED_ARG)
      option_document_type = optArg;
    END_OPTION
    OPTION('D', "--list-document-types", NO_ARG)
      option_list_document_types = t3_true;
    END_OPTION
    OPTION('t', "tag", REQUIRED_ARG)
      char *value;
      if ((value = strchr(optArg, '=')) == NULL) {
        fatal("-t/--tag argument must be <name>=<value>\n");
}
      *value = 0;
      value++;
      if (!set_tag(optArg, value)) {
        fatal(_("Duplicate tag specified\n"));
}
    END_OPTION
    DOUBLE_DASH
      NO_MORE_OPTIONS;
    END_OPTION

    fatal(_("No such option %.*s\n"), OPTPRARG);
  NO_OPTION
    if (option_input != NULL) {
      fatal(_("Error: only one input file allowed\n"));
}
    option_input = optcurrent;
  END_OPTIONS

  if (option_list_document_types) {
    list_document_types(option_style == NULL ? DEFAULT_STYLE : option_style);
    exit(EXIT_SUCCESS);
  }
END_FUNCTION
/* clang-format on */

static t3_bool set_tag(const char *name, const char *value) {
  tag_t *ptr;
  for (ptr = tags; ptr != NULL; ptr = ptr->next) {
    if (strcmp(ptr->name, name) == 0) {
      return t3_false;
    }
  }

  if ((ptr = malloc(sizeof(tag_t))) == NULL) {
    fatal(_("Out of memory\n"));
  }

  ptr->name = name;
  ptr->name_len = strlen(name);
  ptr->value = value;
  ptr->next = tags;
  tags = ptr;
  return t3_true;
}

static int style_filter(const struct dirent *entry) {
  /* FIXME: filter out directories and other non-file entries. */
  return fnmatch("*.style", entry->d_name, 0) == 0;
}

static void list_dir_styles(const char *dirname) {
  struct dirent **namelist;
  int namelist_len;
  int i;

  if ((namelist_len = scandir(dirname, &namelist, style_filter, alphasort)) <= 0) {
    return;
  }

  for (i = 0; i < namelist_len; i++) {
    printf("  %.*s\n", (int)(strrchr(namelist[i]->d_name, '.') - namelist[i]->d_name),
           namelist[i]->d_name);
    free(namelist[i]);
  }
  free(namelist);
}

static void list_styles(void) {
  char *home_env = getenv("HOME");
  if (home_env != NULL && home_env[0] != 0) {
    char *tmp;
    if ((tmp = malloc(strlen(home_env) + strlen("/.libt3highlight") + 1)) == NULL) {
      fatal(_("Out of memory\n"));
    }
    strcpy(tmp, home_env);
    strcat(tmp, "/.libt3highlight");
    list_dir_styles(tmp);
    free(tmp);
  }
  list_dir_styles(DATADIR);
}

static t3_config_t *open_style(const char *name) {
  t3_config_t *style_config;
  t3_config_schema_t *schema;
  t3_config_error_t config_error;
  const char *path[] = {NULL, DATADIR, NULL};
  const char *home_env;
  char *tmp = NULL;
  FILE *style_file;

  home_env = getenv("HOME");
  if (home_env != NULL && home_env[0] != 0) {
    if ((tmp = malloc(strlen(home_env) + strlen("/.libt3highlight") + 1)) == NULL) {
      fatal(_("Out of memory\n"));
    }
    strcpy(tmp, home_env);
    strcat(tmp, "/.libt3highlight");
    path[0] = tmp;
  }

  if ((style_file = t3_config_open_from_path(path[0] == NULL ? path + 1 : path, name, 0)) == NULL) {
    fatal("Can't open '%s': %s\n", name, strerror(errno));
  }
  free(tmp);

  if ((style_config = t3_config_read_file(style_file, &config_error, NULL)) == NULL) {
    fprintf(stderr, _("Error reading style file: "));
    fatal("%s:%d: %s\n", name, config_error.line_number, t3_config_strerror(config_error.error));
  }
  fclose(style_file);

  if ((schema = t3_config_read_schema_buffer(style_schema, sizeof(style_schema), &config_error,
                                             NULL)) == NULL) {
    if (config_error.error != T3_ERR_OUT_OF_MEMORY) {
      config_error.error = T3_ERR_INTERNAL;
    }
    fprintf(stderr, _("Error reading style file: "));
    fatal("%s\n", t3_config_strerror(config_error.error));
  }

  if (!t3_config_validate(style_config, schema, &config_error, T3_CONFIG_VERBOSE_ERROR)) {
    fprintf(stderr, _("Error reading style file: "));
    fatal("%s:%d: %s%s%s\n", name, config_error.line_number, t3_config_strerror(config_error.error),
          config_error.extra == NULL ? "" : ": ",
          config_error.extra == NULL ? "" : config_error.extra);
  }
  t3_config_delete_schema(schema);
  return style_config;
}

static void list_document_types(const char *name) {
  t3_config_t *document;
  t3_config_t *style_config = open_style(name);
  printf(_("Available document types for style '%.*s':\n"), (int)(strrchr(name, '.') - name), name);
  for (document = t3_config_get(t3_config_get(style_config, "documents"), NULL); document != NULL;
       document = t3_config_get_next(document)) {
    const char *name, *description;
    name = t3_config_get_name(document);
    description = t3_config_get_string(t3_config_get(document, "description"));

    printf("  %s", name);
    if (description != NULL) {
      size_t name_len = strlen(name);
      size_t spaces = name_len < 15 ? 15 - name_len : 1;
      size_t i;

      for (i = 0; i < spaces; i++) {
        putchar(' ');
      }
      printf("%s", description);
    }
    putchar('\n');
  }
}

static int map_style(void *_styles, const char *name) {
  style_def_t *styles = _styles;
  int i;

  for (i = 0; styles[i].tag != NULL; i++) {
    if (strcmp(styles[i].tag, name) == 0) {
      return i;
    }
  }
  return 0;
}

static char *expand_string(const char *str, t3_bool expand_escapes) {
  char *result;
  if (str == NULL) {
    return safe_strdup("");
  }

  result = safe_strdup(str);
  if (expand_escapes) {
    parse_escapes(result);
  }
  return result;
}

static void init_translations(t3_config_t *translate, const char *name, t3_bool expand_escapes) {
  int count, i;
  if ((count = t3_config_get_length(translate)) == 0) {
    return;
  }

  if ((translations = malloc(sizeof(translation_t) * (count + 1))) == NULL) {
    fatal(_("Out of memory"));
  }
  for (i = 0, translate = t3_config_get(translate, NULL); translate != NULL;
       i++, translate = t3_config_get_next(translate)) {
    translations[i].search = t3_config_take_string(t3_config_get(translate, "search"));
    translations[i].replace = t3_config_take_string(t3_config_get(translate, "replace"));
    if (expand_escapes) {
      translations[i].search_len = parse_escapes(translations[i].search);
      translations[i].replace_len = parse_escapes(translations[i].replace);
    } else {
      translations[i].search_len = strlen(translations[i].search);
      translations[i].replace_len = strlen(translations[i].replace);
    }
    if (translations[i].search_len == 0) {
      fatal(_("Empty search string: %s:%d\n"), name,
            t3_config_get_line_number(t3_config_get(translate, "search")));
    }
  }
  translations[i].search = NULL;
  translations[i].replace = NULL;
}

static style_def_t *load_style(const char *name) {
  t3_config_t *style_config, *styles, *ptr, *normal, *document;
  t3_bool expand_escapes = t3_false;
  style_def_t *result;
  int count;

  style_config = open_style(name);

  expand_escapes = t3_config_get_bool(t3_config_get(style_config, "expand-escapes"));
  styles = t3_config_get(t3_config_get(style_config, "styles"), NULL);
  normal = t3_config_unlink(styles, "normal");

  for (count = 0, ptr = styles; ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {
  }

  count += 2; /* One for normal state, and one for terminator. */

  if ((result = malloc(sizeof(style_def_t) * count)) == NULL) {
    fatal(_("Out of memory\n"));
  }

  result[0].tag = safe_strdup("normal");
  if (normal == NULL) {
    result[0].start = safe_strdup("");
    result[0].end = safe_strdup("");
  } else {
    result[0].start =
        expand_string(t3_config_get_string(t3_config_get(normal, "start")), expand_escapes);
    result[0].end =
        expand_string(t3_config_get_string(t3_config_get(normal, "end")), expand_escapes);
  }

  for (count = 1, ptr = styles; ptr != NULL; count++, ptr = t3_config_get_next(ptr)) {
    result[count].tag = safe_strdup(t3_config_get_name(ptr));
    result[count].start =
        expand_string(t3_config_get_string(t3_config_get(ptr, "start")), expand_escapes);
    result[count].end =
        expand_string(t3_config_get_string(t3_config_get(ptr, "end")), expand_escapes);
  }
  result[count].tag = NULL;

  init_translations(t3_config_get(style_config, "translate"), name, expand_escapes);

  if (option_document_type != NULL) {
    document = t3_config_get(t3_config_get(style_config, "documents"), option_document_type);
    if (document == NULL) {
      fatal(_("Document type '%s' is not defined\n"), option_document_type);
    }
  } else {
    document = t3_config_get(t3_config_get(style_config, "documents"), NULL);
  }

  if (document != NULL) {
    header = t3_config_take_string(t3_config_get(document, "header"));
    footer = t3_config_take_string(t3_config_get(document, "footer"));
  }

  if (expand_escapes) {
    if (header != NULL) {
      parse_escapes(header);
    }
    if (footer != NULL) {
      parse_escapes(footer);
    }
  }

  t3_config_delete(normal);
  t3_config_delete(style_config);

  return result;
}

static void write_header(void) {
  char *ptr, *prev_ptr = header;
  tag_t *tag_ptr;
  if (header == NULL) {
    return;
  }

  for (ptr = strchr(header, '%'); ptr != NULL; ptr = strchr(prev_ptr, '%')) {
    if (ptr != prev_ptr) {
      fwrite(prev_ptr, 1, ptr - prev_ptr, stdout);
    }
    if (ptr[1] == '{') {
      for (tag_ptr = tags; tag_ptr != NULL; tag_ptr = tag_ptr->next) {
        if (strncmp(ptr + 2, tag_ptr->name, tag_ptr->name_len) == 0 &&
            ptr[tag_ptr->name_len + 2] == '}') {
          write_data(tag_ptr->value, strlen(tag_ptr->value));
          ptr += tag_ptr->name_len + 2;
          break;
        }
      }
      if (tag_ptr == NULL) {
        char *close_ptr = strchr(ptr, '}');
        if (close_ptr == NULL) {
          ptr++;
          putc(*ptr, stdout);
        } else {
          ptr = close_ptr;
        }
      }
    } else {
      ptr++;
      putc(*ptr, stdout);
    }
    prev_ptr = ptr + 1;
  }
  fwrite(prev_ptr, 1, header + strlen(header) - prev_ptr, stdout);
}

static void write_data(const char *string, size_t size) {
  translation_t *ptr;
  size_t i;

  if (translations == NULL) {
    fwrite(string, 1, size, stdout);
    return;
  }

  for (i = 0; i < size; i++) {
    for (ptr = translations; ptr->search != NULL; ptr++) {
      if (i + ptr->search_len <= size && strncmp(string + i, ptr->search, ptr->search_len) == 0) {
        fwrite(ptr->replace, 1, ptr->replace_len, stdout);
        i += ptr->search_len - 1;
        break;
      }
    }
    if (ptr->search == NULL) {
      putc(string[i], stdout);
    }
  }
}

static void highlight_file(t3_highlight_t *highlight) {
  FILE *input;
  char *line = NULL;
  size_t n;
  ssize_t chars_read;

  t3_highlight_match_t *match = t3_highlight_new_match(highlight);
  t3_bool match_result;

  if (match == NULL) {
    fatal(_("Out of memory\n"));
  }

  if (option_input == NULL) {
    input = stdin;
  } else if ((input = fopen(option_input, "rb")) == NULL) {
    fatal(_("Can't open '%s': %s\n"), option_input, strerror(errno));
  }

  write_header();

  while ((chars_read = getline(&line, &n, input)) > 0) {
    if (line[chars_read - 1] == '\n') {
      chars_read--;
    }

    t3_highlight_next_line(match);
    do {
      match_result = t3_highlight_match(match, line, chars_read);
      size_t start = t3_highlight_get_start(match),
             match_start = t3_highlight_get_match_start(match), end = t3_highlight_get_end(match);
      if (start != match_start) {
        fputs(styles[t3_highlight_get_begin_attr(match)].start, stdout);
        write_data(line + start, match_start - start);
        fputs(styles[t3_highlight_get_begin_attr(match)].end, stdout);
      }
      if (match_start != end) {
        fputs(styles[t3_highlight_get_match_attr(match)].start, stdout);
        write_data(line + match_start, end - match_start);
        fputs(styles[t3_highlight_get_match_attr(match)].end, stdout);
      }
    } while (match_result);
    write_data("\n", 1);
  }
  if (footer != NULL) {
    fwrite(footer, 1, strlen(footer), stdout);
  }
  fflush(stdout);
  t3_highlight_free_match(match);
  fclose(input);
  free(line);
}

int main(int argc, char *argv[]) {
  t3_highlight_t *highlight;
  t3_highlight_error_t error;
#ifdef DEBUG
  int i;
#endif

#ifdef USE_GETTEXT
  setlocale(LC_ALL, "");
  bindtextdomain("t3highlight", LOCALEDIR);
  textdomain("t3highlight");
#endif

  parse_args(argc, argv);

  styles = load_style(option_style == NULL ? DEFAULT_STYLE : option_style);

  if (option_input != NULL && strcmp(option_input, "-") == 0) {
    option_input = NULL;
  }

  if (option_language == NULL && option_language_file == NULL && option_input == NULL) {
    fatal(_("-l/--language or --language-file required for reading from standard input\n"));
  } else if (option_language_file != NULL) {
    highlight = t3_highlight_load(option_language_file, map_style, styles,
                                  T3_HIGHLIGHT_VERBOSE_ERROR | T3_HIGHLIGHT_UTF8, &error);
  } else if (option_language != NULL) {
    highlight = t3_highlight_load_by_langname(
        option_language, map_style, styles, T3_HIGHLIGHT_VERBOSE_ERROR | T3_HIGHLIGHT_UTF8, &error);
  } else {
    highlight = t3_highlight_load_by_filename(
        option_input, map_style, styles, T3_HIGHLIGHT_VERBOSE_ERROR | T3_HIGHLIGHT_UTF8, &error);
  }

  if (highlight == NULL) {
    if (error.file_name == NULL) {
      fprintf(stderr, _("Error loading highlighting patterns: "));
      fatal("%s\n", t3_highlight_strerror(error.error));
    }

    fprintf(stderr, _("Error loading highlighting patterns: "));
    fprintf(stderr, "%s:%d: %s", error.file_name, error.line_number,
            t3_highlight_strerror(error.error));
    if (error.extra != NULL) {
      fprintf(stderr, ": %s", error.extra);
    }
    fatal("\n");
  }

  set_tag("name", option_input);
  set_tag("charset", "UTF-8");

  highlight_file(highlight);
#ifdef DEBUG
  for (i = 0; styles[i].tag != NULL; i++) {
    free(styles[i].tag);
    free(styles[i].start);
    free(styles[i].end);
  }
  free(styles);

  tag_t *tag;
  for (tag = tags; tag != NULL;) {
    tag_t *ptr = tag;
    tag = tag->next;
    free(ptr);
  }
  t3_highlight_free(highlight);
#endif
  return EXIT_SUCCESS;
}
