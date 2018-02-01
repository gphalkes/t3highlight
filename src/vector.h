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
#ifndef T3_HIGHLIGHT_VECTOR_H
#define T3_HIGHLIGHT_VECTOR_H

#include <stdlib.h>

#include "highlight_api.h"

typedef struct {
  void *data;
  size_t allocated, used;
} vector_base_t;

#define VECTOR(type)        \
  struct {                  \
    type *data;             \
    size_t allocated, used; \
  }
#define VECTOR_INIT(name) \
  do {                    \
    (name).data = NULL;   \
    (name).allocated = 0; \
    (name).used = 0;      \
  } while (0)
#define VECTOR_ITERATE(name, func)                               \
  do {                                                           \
    size_t _i;                                                   \
    for (_i = 0; _i < (name).used; _i++) func(&(name).data[_i]); \
  } while (0)
#define VECTOR_RESERVE(name) \
  _t3_highlight_vector_reserve((vector_base_t *)&name, sizeof((name).data[0]))
#define VECTOR_LAST(name) (name).data[(name).used - 1]
#define VECTOR_FREE(name) free((name).data)

T3_HIGHLIGHT_LOCAL t3_bool _t3_highlight_vector_reserve(vector_base_t *vector, size_t elsize);

#endif
