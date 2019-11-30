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
#include <errno.h>
#ifdef PCRE_COMPAT
#include "pcre_compat.h"
#else
#include <pcre2.h>
#endif
#include <stdlib.h>
#include <string.h>

#include "highlight.h"
#include "highlight_errors.h"
#include "internal.h"

#ifdef USE_GETTEXT
#include <libintl.h>
#define _(x) dgettext("LIBT3", (x))
#else
#define _(x) (x)
#endif

static const state_t null_state = {{NULL, 0, 0}, 0};

static const char syntax_schema[] = {
#include "syntax.bytes"
};

static const char syntax_v3_schema[] = {
#include "syntax_v3.bytes"
};

static t3_bool init_state(highlight_context_t *context, const t3_config_t *highlights,
                          pattern_idx_t idx);
static void free_state(state_t *state);

static int do_map_style(highlight_context_t *context, const char *style) {
  size_t style_at_scope_len;
  char *style_at_scope;
  int style_at_scope_result;

  if ((context->flags & T3_HIGHLIGHT_USE_SCOPE) && context->scope != NULL) {
    size_t style_len = strlen(style);
    size_t scope_len = strlen(context->scope);
    style_at_scope_len = style_len + 1 + scope_len + 1;
    style_at_scope = malloc(style_at_scope_len);
    if (style_at_scope != NULL) {
      memcpy(style_at_scope, style, style_len);
      memcpy(style_at_scope + style_len, "@", 1);
      memcpy(style_at_scope + style_len + 1, context->scope, scope_len);
      style_at_scope[style_at_scope_len - 1] = 0;

      style_at_scope_result = context->map_style(context->map_style_data, style_at_scope);
      free(style_at_scope);
      if (style_at_scope_result != 0) {
        return style_at_scope_result;
      }
    }
  }
  return context->map_style(context->map_style_data, style);
}

t3_highlight_t *t3_highlight_new(t3_config_t *syntax, int (*map_style)(void *, const char *),
                                 void *map_style_data, int flags, t3_highlight_error_t *error) {
  t3_highlight_t *result = NULL;
  t3_config_schema_t *schema = NULL;
  t3_config_t *highlights;
  t3_config_error_t local_error;
  highlight_context_t context;

  int format;
  const char *schema_text;
  size_t schema_size;

  /* Sanatize flags */
  flags &= T3_HIGHLIGHT_UTF8_NOCHECK | T3_HIGHLIGHT_USE_PATH | T3_HIGHLIGHT_VERBOSE_ERROR |
           T3_HIGHLIGHT_USE_SCOPE;

  format = t3_config_get_int(t3_config_get(syntax, "format"));
  if (format < 3) {
    schema_text = syntax_schema;
    schema_size = sizeof(syntax_schema);
  } else {
    schema_text = syntax_v3_schema;
    schema_size = sizeof(syntax_v3_schema);
  }

  /* Validate the syntax using the schema. */
  if ((schema = t3_config_read_schema_buffer(schema_text, schema_size, &local_error, NULL)) ==
      NULL) {
    if (error != NULL) {
      error->error =
          local_error.error != T3_ERR_OUT_OF_MEMORY ? T3_ERR_INTERNAL : local_error.error;
      error->extra = NULL;
    }
    return NULL;
  }

  if (!t3_config_validate(syntax, schema, &local_error,
                          (flags & T3_HIGHLIGHT_VERBOSE_ERROR)
                              ? (T3_CONFIG_VERBOSE_ERROR | T3_CONFIG_ERROR_FILE_NAME)
                              : 0)) {
    _t3_highlight_set_error(
        error, (flags & T3_HIGHLIGHT_VERBOSE_ERROR) ? local_error.error : T3_ERR_INVALID_FORMAT,
        local_error.line_number, local_error.file_name, local_error.extra, flags);
    if (flags & T3_HIGHLIGHT_VERBOSE_ERROR) {
      free(local_error.file_name);
      free(local_error.extra);
    }
    goto return_error;
  }

  t3_config_delete_schema(schema);
  schema = NULL;

  /* Check whether to allow empty start patterns. */
  if (t3_config_get_int(t3_config_get(syntax, "format")) > 1 &&
      (t3_config_get(syntax, "allow-empty-start") == NULL ||
       t3_config_get_bool(t3_config_get(syntax, "allow-empty-start")))) {
    flags |= T3_HIGHLIGHT_ALLOW_EMPTY_START;
  }

  if (map_style == NULL) {
    _t3_highlight_set_error_simple(error, T3_ERR_BAD_ARG, flags);
    goto return_error;
  }

  if ((result = malloc(sizeof(t3_highlight_t))) == NULL) {
    _t3_highlight_set_error_simple(error, T3_ERR_OUT_OF_MEMORY, flags);
    goto return_error;
  }
  VECTOR_INIT(result->states);

  if (!VECTOR_RESERVE(result->states)) {
    _t3_highlight_set_error_simple(error, T3_ERR_OUT_OF_MEMORY, flags);
    goto return_error;
  }
  VECTOR_LAST(result->states) = null_state;

  /* Set up initialization. */
  highlights = t3_config_get(syntax, "highlight");
  context.map_style = map_style;
  context.map_style_data = map_style_data;
  context.highlight = result;
  context.syntax = syntax;
  context.flags = flags;
  context.error = error;
  context.scope = NULL;

  VECTOR_INIT(context.use_map);
  if (!init_state(&context, highlights, 0)) {
    free(context.use_map.data);
    goto return_error;
  }
  free(context.use_map.data);

  if (!_t3_check_use_cycle(&context)) {
    goto return_error;
  }

  /* If we allow empty start patterns, we need to analyze whether they don't result
     in infinite loops. */
  if ((flags & T3_HIGHLIGHT_ALLOW_EMPTY_START) && !_t3_check_empty_start_cycle(&context)) {
    goto return_error;
  }

  result->flags = flags;
  result->lang_file = NULL;
  return result;

return_error:
  t3_config_delete_schema(schema);
  if (result != NULL) {
    VECTOR_ITERATE(result->states, free_state);
    free(result->states.data);
    free(result);
  }
  return NULL;
}

t3_bool _t3_compile_highlight(const char *highlight, pcre2_code_8 **regex,
                              const t3_config_t *error_context, int flags,
                              t3_highlight_error_t *error) {
  int local_error;
  PCRE2_SIZE error_offset;

  if ((*regex = pcre2_compile_8((PCRE2_SPTR8)highlight, PCRE2_ZERO_TERMINATED,
                                (flags & T3_HIGHLIGHT_UTF8 ? PCRE2_UTF : 0) | PCRE2_ANCHORED,
                                &local_error, &error_offset, NULL)) == NULL) {
    if (local_error == PCRE2_ERROR_NOMEMORY) {
      _t3_highlight_set_error(error, T3_ERR_OUT_OF_MEMORY, 0, NULL, NULL, flags);
    } else {
      char error_message[256];
      pcre2_get_error_message_8(local_error, (PCRE2_UCHAR8 *)error_message, sizeof(error_message));
      _t3_highlight_set_error(error, T3_ERR_INVALID_REGEX, t3_config_get_line_number(error_context),
                              t3_config_get_file_name(error_context), error_message, flags);
    }
    return t3_false;
  }
  pcre2_jit_compile_8(*regex, PCRE2_JIT_COMPLETE);
  return t3_true;
}

static t3_bool add_delim_highlight(highlight_context_t *context, t3_config_t *regex,
                                   pattern_idx_t next_state, pattern_t *pattern) {
  pattern_t new_pattern;
  patterns_t *patterns;

  patterns = &context->highlight->states.data[pattern->next_state].patterns;

  new_pattern.next_state = next_state;
  new_pattern.extra = NULL;
  new_pattern.regex = NULL;

  if (pattern->extra != NULL && pattern->extra->dynamic_name != NULL && next_state <= EXIT_STATE) {
    char *regex_with_define;
    t3_bool result;

    /* Create the full regex pattern, including a fake define for the named
       back reference, and try to compile the pattern to check if it is valid. */
    if ((regex_with_define = malloc(strlen(t3_config_get_string(regex)) +
                                    strlen(pattern->extra->dynamic_name) + 18)) == NULL) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    sprintf(regex_with_define, "(?(DEFINE)(?<%s>))%s", pattern->extra->dynamic_name,
            t3_config_get_string(regex));
    result = _t3_compile_highlight(regex_with_define, &new_pattern.regex, regex, context->flags,
                                   context->error);

    /* Throw away the results of the compilation, because we don't actually need it. */
    free(regex_with_define);
    pcre2_code_free_8(new_pattern.regex);

    /* If the compilation failed, abort the whole thing. */
    if (!result) {
      goto return_error;
    }

    new_pattern.regex = NULL;
    /* Save the regular expression, because we need it to build the actual regex once the
       start pattern is matched. */
    pattern->extra->dynamic_pattern = t3_config_take_string(regex);
  } else {
    if (!_t3_compile_highlight(t3_config_get_string(regex), &new_pattern.regex, regex,
                               context->flags, context->error)) {
      goto return_error;
    }
  }

  new_pattern.attribute_idx = pattern->attribute_idx;
  if (!VECTOR_RESERVE(*patterns)) {
    _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
    goto return_error;
  }

  /* Find the highlight entry, starting after the end entry. If it does not exist,
     the list of highlights was specified first. */
  for (; regex != NULL && strcmp(t3_config_get_name(regex), "highlight") != 0;
       regex = t3_config_get_next(regex)) {
  }

  if (regex == NULL && patterns->used > 0) {
    VECTOR_LAST(*patterns) = new_pattern;
  } else {
    memmove(patterns->data + 1, patterns->data, (patterns->used - 1) * sizeof(pattern_t));
    patterns->data[0] = new_pattern;
  }
  return t3_true;

return_error:
  return t3_false;
}

/** Set the @c extra member of the ::pattern_t.

    The @c extra member is used for storing the data for dynamic end patterns
    and for the on-entry list.
*/
static t3_bool set_extra(highlight_context_t *context, pattern_t *pattern,
                         const t3_config_t *highlights) {
  const char *dynamic = t3_config_get_string(t3_config_get(highlights, "extract"));
  t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

  if (dynamic == NULL && on_entry == NULL) {
    return t3_true;
  }

  if ((pattern->extra = malloc(sizeof(pattern_extra_t))) == NULL) {
    _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
    goto return_error;
  }
  pattern->extra->dynamic_name = NULL;
  pattern->extra->dynamic_pattern = NULL;
  pattern->extra->on_entry = NULL;
  pattern->extra->on_entry_cnt = 0;

  if (on_entry != NULL) {
    int i;
    pattern->extra->on_entry_cnt = t3_config_get_length(on_entry);
    if ((pattern->extra->on_entry =
             malloc(sizeof(on_entry_info_t) * pattern->extra->on_entry_cnt)) == NULL) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    for (i = 0; i < pattern->extra->on_entry_cnt; i++) {
      pattern->extra->on_entry[i].end_pattern = NULL;
    }
  }

  if (dynamic != NULL) {
    if (dynamic[strspn(dynamic, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")] != 0) {
      t3_config_t *extract = t3_config_get(highlights, "extract");

      _t3_highlight_set_error(context->error, T3_ERR_INVALID_NAME,
                              t3_config_get_line_number(extract), t3_config_get_file_name(extract),
                              dynamic, context->flags);
      goto return_error;
    }
    pattern->extra->dynamic_name = t3_config_take_string(t3_config_get(highlights, "extract"));
  }
  return t3_true;

return_error:
  return t3_false;
}

/** Fill the @c on_entry list. */
static t3_bool set_on_entry(highlight_context_t *context, pattern_t *pattern,
                            const t3_config_t *highlights) {
  t3_config_t *regex, *style;
  pattern_t parent_pattern;
  pattern_extra_t parent_extra;
  t3_config_t *sub_highlights;
  int style_attr_idx = 0;
  int idx;

  t3_config_t *on_entry = t3_config_get(highlights, "on-entry");

  if (on_entry == NULL) {
    return t3_true;
  }

  parent_pattern = *pattern;
  parent_extra = *pattern->extra;
  parent_pattern.extra = &parent_extra;

  for (on_entry = t3_config_get(on_entry, NULL), idx = 0; on_entry != NULL;
       on_entry = t3_config_get_next(on_entry), idx++) {
    pattern->extra->on_entry[idx].state = context->highlight->states.used;
    parent_pattern.next_state = pattern->extra->on_entry[idx].state;

    if ((style = t3_config_get(on_entry, "style")) != NULL) {
      parent_pattern.attribute_idx = style_attr_idx =
          do_map_style(context, t3_config_get_string(style));
    }

    if ((style = t3_config_get(on_entry, "delim-style")) != NULL) {
      parent_pattern.attribute_idx = do_map_style(context, t3_config_get_string(style));
    }

    if (!VECTOR_RESERVE(context->highlight->states)) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    VECTOR_LAST(context->highlight->states) = null_state;
    VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;
    if ((sub_highlights = t3_config_get(on_entry, "highlight")) != NULL) {
      if (!init_state(context, sub_highlights, pattern->extra->on_entry[idx].state)) {
        goto return_error;
      }
    }
    /* If the highlight specifies an end regex, create an extra pattern for that and paste that
       to in the list of sub-highlights. Depending on whether end is specified before or after
       the highlight list, it will be pre- or appended. */
    if ((regex = t3_config_get(on_entry, "end")) != NULL) {
      pattern_idx_t return_state = NO_CHANGE - t3_config_get_int(t3_config_get(on_entry, "exit"));
      if (return_state == NO_CHANGE) {
        return_state = EXIT_STATE;
      }
      if (!add_delim_highlight(context, regex, return_state, &parent_pattern)) {
        goto return_error;
      }
      pattern->extra->on_entry[idx].end_pattern = parent_pattern.extra->dynamic_pattern;
      parent_pattern.extra->dynamic_pattern = NULL;
    }
  }
  return t3_true;

return_error:
  return t3_false;
}

static t3_bool map_use(highlight_context_t *context, const t3_config_t *use,
                       pattern_idx_t *mapped_state) {
  size_t i;
  t3_config_t *define;
  t3_config_t *definition = NULL;
  const char *retrieved_scope;
  const char *saved_scope;
  const char *use_str = t3_config_get_string(use);

  for (define = t3_config_get(t3_config_get(context->syntax, "define"), NULL); define != NULL;
       define = t3_config_get_next(define)) {
    definition = t3_config_get(define, use_str);
    if (definition != NULL) {
      break;
    }
  }

  if (definition == NULL) {
    _t3_highlight_set_error(context->error, T3_ERR_UNDEFINED_USE, t3_config_get_line_number(use),
                            t3_config_get_file_name(use), use_str, context->flags);
    goto return_error;
  }

  /* Lookup the name in the use_map. If the definition was already
     compiled before, we don't have to do it again, but we can simply
     refer to the previous definition. */
  for (i = 0; i < context->use_map.used; i++) {
    if (strcmp(t3_config_get_string(use), context->use_map.data[i].name) == 0) {
      break;
    }
  }

  if (i == context->use_map.used) {
    /* If we didn't already compile the defintion, do it now. */
    *mapped_state = context->highlight->states.used;

    if (!VECTOR_RESERVE(context->use_map)) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    VECTOR_LAST(context->use_map).name = t3_config_get_string(use);
    VECTOR_LAST(context->use_map).state = *mapped_state;

    if (!VECTOR_RESERVE(context->highlight->states)) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    VECTOR_LAST(context->highlight->states) = null_state;

    saved_scope = context->scope;
    retrieved_scope = t3_config_get_string(t3_config_get(define, "style-scope"));
    if (retrieved_scope != NULL) {
      context->scope = retrieved_scope[0] == 0 ? NULL : retrieved_scope;
    }
    if (!init_state(context, t3_config_get(definition, "highlight"), *mapped_state)) {
      return t3_false;
    }
    context->scope = saved_scope;
  } else {
    *mapped_state = context->use_map.data[i].state;
  }

  return t3_true;

return_error:
  return t3_false;
}

static t3_bool init_state(highlight_context_t *context, const t3_config_t *highlights,
                          pattern_idx_t idx) {
  t3_config_t *regex, *style, *use;
  pattern_t pattern;
  int style_attr_idx;

  for (highlights = t3_config_get(highlights, NULL); highlights != NULL;
       highlights = t3_config_get_next(highlights)) {
    style_attr_idx = (style = t3_config_get(highlights, "style")) == NULL
                         ? context->highlight->states.data[idx].attribute_idx
                         : do_map_style(context, t3_config_get_string(style));

    pattern.regex = NULL;
    pattern.extra = NULL;
    if ((regex = t3_config_get(highlights, "regex")) != NULL) {
      if (!_t3_compile_highlight(t3_config_get_string(regex), &pattern.regex, regex, context->flags,
                                 context->error)) {
        goto return_error;
      }

      pattern.attribute_idx = style_attr_idx;
      pattern.next_state = NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
    } else if ((regex = t3_config_get(highlights, "start")) != NULL) {
      t3_config_t *sub_highlights;

      pattern.attribute_idx = (style = t3_config_get(highlights, "delim-style")) == NULL
                                  ? style_attr_idx
                                  : do_map_style(context, t3_config_get_string(style));

      if (!_t3_compile_highlight(t3_config_get_string(regex), &pattern.regex, regex, context->flags,
                                 context->error)) {
        goto return_error;
      }

      if (!set_extra(context, &pattern, highlights)) {
        goto return_error;
      }

      /* Create new state to which start will switch. */
      pattern.next_state = context->highlight->states.used;
      if (!VECTOR_RESERVE(context->highlight->states)) {
        _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
        goto return_error;
      }
      VECTOR_LAST(context->highlight->states) = null_state;
      VECTOR_LAST(context->highlight->states).attribute_idx = style_attr_idx;

      /* Add sub-highlights to the new state, if they are specified. */
      if ((sub_highlights = t3_config_get(highlights, "highlight")) != NULL) {
        if (!init_state(context, sub_highlights, pattern.next_state)) {
          goto return_error;
        }
      }

      /* Set the on_entry patterns, if any. Note that this calls add_delim_highlight for
         the end patterns, which frobs the extra->dynamic_pattern member. Thus this must be
         called before the add_delim_highlight call for this patterns own end pattern. */
      if (!set_on_entry(context, &pattern, highlights)) {
        goto return_error;
      }

      /* If the highlight specifies an end regex, create an extra pattern for that and paste that
         to in the list of sub-highlights. Depending on whether end is specified before or after
         the highlight list, it will be pre- or appended. */
      if ((regex = t3_config_get(highlights, "end")) != NULL) {
        pattern_idx_t return_state =
            NO_CHANGE - t3_config_get_int(t3_config_get(highlights, "exit"));
        if (return_state == NO_CHANGE) {
          return_state = EXIT_STATE;
        }
        if (!add_delim_highlight(context, regex, return_state, &pattern)) {
          goto return_error;
        }
      }

      if (t3_config_get_bool(t3_config_get(highlights, "nested")) &&
          !add_delim_highlight(context, t3_config_get(highlights, "start"), pattern.next_state,
                               &pattern)) {
        goto return_error;
      }
    } else if ((use = t3_config_get(highlights, "use")) != NULL) {
      /* regex = NULL (set above) signifies that this is a link to another state. */
      if (!map_use(context, use, &pattern.next_state)) {
        goto return_error;
      }
    } else {
      _t3_highlight_set_error_simple(context->error, T3_ERR_INTERNAL, context->flags);
      goto return_error;
    }
    if (!VECTOR_RESERVE(context->highlight->states.data[idx].patterns)) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }
    VECTOR_LAST(context->highlight->states.data[idx].patterns) = pattern;
  }
  return t3_true;

return_error:
  if (pattern.extra != NULL) {
    free(pattern.extra->dynamic_name);
    free(pattern.extra->dynamic_pattern);
    if (pattern.extra->on_entry != NULL) {
      int i;
      for (i = 0; i < pattern.extra->on_entry_cnt; i++) {
        free(pattern.extra->on_entry[i].end_pattern);
      }
      free(pattern.extra->on_entry);
    }
    free(pattern.extra);
  }
  pcre2_code_free_8(pattern.regex);
  return t3_false;
}

static void free_highlight(pattern_t *highlight) {
  pcre2_code_free_8(highlight->regex);
  if (highlight->extra != NULL) {
    free(highlight->extra->dynamic_name);
    free(highlight->extra->dynamic_pattern);
    if (highlight->extra->on_entry != NULL) {
      int i;
      for (i = 0; i < highlight->extra->on_entry_cnt; i++) {
        free(highlight->extra->on_entry[i].end_pattern);
      }
      free(highlight->extra->on_entry);
    }
    free(highlight->extra);
  }
}

static void free_state(state_t *state) {
  VECTOR_ITERATE(state->patterns, free_highlight);
  VECTOR_FREE(state->patterns);
}

void t3_highlight_free(t3_highlight_t *highlight) {
  if (highlight == NULL) {
    return;
  }
  VECTOR_ITERATE(highlight->states, free_state);
  VECTOR_FREE(highlight->states);
  free(highlight->lang_file);
  free(highlight);
}

void _t3_highlight_set_error(t3_highlight_error_t *error, int code, int line_number,
                             const char *file_name, const char *extra, int flags) {
  if (error != NULL) {
    error->error = code;
    if (flags & T3_HIGHLIGHT_VERBOSE_ERROR) {
      error->line_number = line_number;
      error->file_name = file_name == NULL ? NULL : _t3_highlight_strdup(file_name);
      error->extra = extra == NULL ? NULL : _t3_highlight_strdup(extra);
    }
  }
}

void _t3_highlight_set_error_simple(t3_highlight_error_t *error, int code, int flags) {
  _t3_highlight_set_error(error, code, 0, NULL, NULL, flags);
}

const char *t3_highlight_strerror(int error) {
  switch (error) {
    default:
      if (error >= -80) {
        return t3_config_strerror(error);
      }
      return t3_highlight_strerror_base(error);
    case T3_ERR_INVALID_FORMAT:
      return _("invalid file format");
    case T3_ERR_INVALID_REGEX:
      return _("invalid regular expression");
    case T3_ERR_NO_SYNTAX:
      return _("could not locate appropriate highlighting pattern");
    case T3_ERR_UNDEFINED_USE:
      return _("'use' specifies undefined highlight");
    case T3_ERR_INVALID_NAME:
      return _("invalid sub-pattern name");
    case T3_ERR_EMPTY_START_CYCLE:
      return _("empty start-pattern cycle");
    case T3_ERR_USE_CYCLE:
      return _("use-pattern cycle");
  }
}

long t3_highlight_get_version(void) { return T3_HIGHLIGHT_VERSION; }
