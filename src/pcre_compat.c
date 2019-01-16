/* Copyright (C) 2019 G.P. Halkes
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

#include "pcre_compat.h"

#ifdef PCRE_COMPAT
#include <string.h>

static const char *last_error_message = NULL;
static int last_error_code = 0;

pcre2_code_8 *pcre2_compile_8(PCRE2_SPTR8 pattern, PCRE2_SIZE pattern_size, uint32_t options,
                              int *errorcode, PCRE2_SIZE *erroroffset, void *ccontext) {
  const char *error_message;
  if (pattern_size != PCRE2_ZERO_TERMINATED) {
    abort();
  }
  if (ccontext != NULL) {
    abort();
  }
  pcre2_code_8 *result = malloc(sizeof(pcre2_code_8));
  result->regex = NULL;
  result->extra = NULL;

  result->regex =
      pcre_compile2((const char *)pattern, options, errorcode, &error_message, erroroffset, NULL);
  if (result->regex == NULL) {
    last_error_message = error_message;
    last_error_code = *errorcode;
    free(result);
    return NULL;
  }
  return result;
}

int pcre2_pattern_info_8(const pcre2_code_8 *code, uint32_t what, void *where) {
  uint32_t value;
  int result;
  if (what != PCRE2_INFO_MINLENGTH) {
    return PCRE2_ERROR_BADOPTION;
  }
  result = pcre_fullinfo(code->regex, code->extra, what, &value);
  if (result == 0) {
    *(int *)where = value;
  }
  return result;
}

int pcre2_jit_compile_8(pcre2_code_8 *code, uint32_t options) {
  const char *error_message;

  (void)options;

  code->extra = pcre_study(code->regex, 0, &error_message);
  return 0;
}

pcre2_match_data_8 *pcre2_match_data_create_8(uint32_t ovecsize, void *gcontext) {
  int *result;

  (void)gcontext;

  if (ovecsize == 0) {
    ++ovecsize;
  }
  result = malloc(sizeof(int) * (1 + 2 * ovecsize));
  *result = 2 * ovecsize;
  return result;
}

void pcre2_match_data_free_8(pcre2_match_data_8 *match_data) { free(match_data); }

PCRE2_SIZE *pcre2_get_ovector_pointer_8(pcre2_match_data_8 *match_data) { return match_data + 1; }
uint32_t pcre2_get_ovector_count_8(pcre2_match_data_8 *match_data) { return match_data[0] / 2; }

pcre2_match_data_8 *pcre2_match_data_create_from_pattern_8(const pcre2_code_8 *code,
                                                           void *gcontext) {
  (void)code;
  (void)gcontext;
  return pcre2_match_data_create_8(15, NULL);
}

void pcre2_code_free_8(pcre2_code_8 *code) {
  if (code == NULL) {
    return;
  }
  pcre_free(code->regex);
  pcre_free_study(code->extra);
  free(code);
}

int pcre2_match_8(const pcre2_code_8 *code, PCRE2_SPTR8 subject, PCRE2_SIZE length,
                  PCRE2_SIZE startoffset, uint32_t options, pcre2_match_data_8 *match_data,
                  void *mcontext) {
  (void)mcontext;
  return pcre_exec(code->regex, code->extra, (const char *)subject,
                   length == PCRE2_ZERO_TERMINATED ? strlen((const char *)subject) : length,
                   startoffset, options, match_data + 1, *match_data);
}

int pcre2_get_error_message_8(int errorcode, PCRE2_UCHAR8 *buffer, PCRE2_SIZE bufflen) {
  char *copy_end;
  if (errorcode == last_error_code) {
    copy_end = strncpy((char *)buffer, last_error_message, bufflen);
  } else {
    copy_end = strncpy((char *)buffer, "unknown error", bufflen);
  }
  buffer[bufflen - 1] = 0;
  if (copy_end < (char *)buffer + bufflen) {
    return copy_end - (char *)buffer;
  }
  return PCRE2_ERROR_NOMEMORY;
}

int pcre2_substring_number_from_name_8(const pcre2_code_8 *code, PCRE2_SPTR8 name) {
  return pcre_get_stringnumber(code->regex, (const char *)name);
}
#else
int _t3_highlight_no_empty_translation_unit;
#endif
