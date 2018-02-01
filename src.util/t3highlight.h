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
#ifndef T3HIGHLIGHT_H
#define T3HIGHLIGHT_H
#include <stdlib.h>

#if defined _
#undef _
#endif

#ifdef USE_GETTEXT
#include <libintl.h>
#define _(x) gettext(x)
#else
#define _(x) (x)
#endif

void fatal(const char *fmt, ...);
size_t parse_escapes(char *string);

#endif
