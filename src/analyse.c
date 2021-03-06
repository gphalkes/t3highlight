/* Copyright (C) 2013 G.P. Halkes
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

static t3_bool check_empty_start_cycle_from_state(highlight_context_t *context,
                                                  pattern_idx_t state) {
  uint32_t min_length;
  size_t j;
  states_t *states = &context->highlight->states;

  VECTOR(state_stack_t) state_stack;
  VECTOR_INIT(state_stack);

  if (!VECTOR_RESERVE(state_stack)) {
    _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
    goto return_error;
  }

  VECTOR_LAST(state_stack).state = state;
  VECTOR_LAST(state_stack).i = 0;

  while (state_stack.used) {
    size_t current_idx = state_stack.used - 1;
#define CURRENT state_stack.data[current_idx]

    if (CURRENT.i == states->data[CURRENT.state].patterns.used) {
      state_stack.used--;
      continue;
    }

    for (; CURRENT.i < states->data[CURRENT.state].patterns.used; CURRENT.i++) {
      pattern_t *highlight = &states->data[CURRENT.state].patterns.data[CURRENT.i];

      if (highlight->next_state <= NO_CHANGE) {
        continue;
      }

      if (highlight->regex == NULL) {
        /* This is a use pattern. For those we can simply push them on the
           stack, and they will be handled correctly. */
        if (!VECTOR_RESERVE(state_stack)) {
          _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
          goto return_error;
        }
        VECTOR_LAST(state_stack).state = highlight->next_state;
        VECTOR_LAST(state_stack).i = 0;
        continue;
      }

      /* This should be pretty much impossible to happen, so we just continue
         as if this pattern matches at least one byte. */
      if (pcre2_pattern_info_8(highlight->regex, PCRE2_INFO_MINLENGTH, &min_length) != 0) {
        continue;
      }

      /* If this pattern can not match the empty string, we don't have to
         consider it any further. */
      if (min_length > 0) {
        continue;
      }

      /* If we push a state onto the stack that is already on it, we've
         found a cycle. */
      for (j = 0; j < state_stack.used; j++) {
        if (state_stack.data[j].state == highlight->next_state) {
          _t3_highlight_set_error_simple(context->error, T3_ERR_EMPTY_START_CYCLE, context->flags);
          goto return_error;
        }
      }
      /* Push the next state onto the stack, so we can go from there later on. */
      if (!VECTOR_RESERVE(state_stack)) {
        _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
        goto return_error;
      }
      VECTOR_LAST(state_stack).state = highlight->next_state;
      VECTOR_LAST(state_stack).i = 0;

      /* For on-entry states, we simply push all of them. */
      if (highlight->extra != NULL && highlight->extra->on_entry_cnt > 0) {
        for (j = 0; (int)j < highlight->extra->on_entry_cnt; j++) {
          if (!VECTOR_RESERVE(state_stack)) {
            _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
            goto return_error;
          }
          VECTOR_LAST(state_stack).state = highlight->extra->on_entry[j].state;
          VECTOR_LAST(state_stack).i = 0;
        }
      }
      CURRENT.i++;
      break;
    }
  }
  VECTOR_FREE(state_stack);
  return t3_true;

return_error:
  VECTOR_FREE(state_stack);
  return t3_false;
}

t3_bool _t3_check_empty_start_cycle(highlight_context_t *context) {
  size_t i;
  for (i = 0; i < context->highlight->states.used; i++) {
    if (!check_empty_start_cycle_from_state(context, i)) {
      return t3_false;
    }
  }
  return t3_true;
}

t3_bool _t3_check_use_cycle(highlight_context_t *context) {
  size_t i, j;

  VECTOR(state_stack_t) state_stack;
  VECTOR_INIT(state_stack);

  for (i = 0; i < context->highlight->states.used; i++) {
    if (!VECTOR_RESERVE(state_stack)) {
      _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
      goto return_error;
    }

    VECTOR_LAST(state_stack).state = i;
    VECTOR_LAST(state_stack).i = 0;

    while (state_stack.used) {
      size_t current_idx = state_stack.used - 1;
#define CURRENT state_stack.data[current_idx]

      if (CURRENT.i == context->highlight->states.data[CURRENT.state].patterns.used) {
        state_stack.used--;
        continue;
      }

      for (; CURRENT.i < context->highlight->states.data[CURRENT.state].patterns.used;
           CURRENT.i++) {
        pattern_t *highlight =
            &context->highlight->states.data[CURRENT.state].patterns.data[CURRENT.i];

        if (highlight->regex != NULL) {
          continue;
        }
        if (highlight->next_state <= NO_CHANGE) {
          continue;
        }

        /* If we push a state onto the stack that is already on it, we've
           found a cycle. */
        for (j = 0; j < state_stack.used; j++) {
          if (state_stack.data[j].state == highlight->next_state) {
            _t3_highlight_set_error_simple(context->error, T3_ERR_USE_CYCLE, context->flags);
            goto return_error;
          }
        }

        /* Push the next state onto the stack, so we can go from there later on. */
        if (!VECTOR_RESERVE(state_stack)) {
          _t3_highlight_set_error_simple(context->error, T3_ERR_OUT_OF_MEMORY, context->flags);
          goto return_error;
        }
        VECTOR_LAST(state_stack).state = highlight->next_state;
        VECTOR_LAST(state_stack).i = 0;

        CURRENT.i++;
        break;
      }
    }
  }
  VECTOR_FREE(state_stack);
  return t3_true;

return_error:
  VECTOR_FREE(state_stack);
  return t3_false;
}
