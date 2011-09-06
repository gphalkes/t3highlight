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
#include "vector.h"

t3_bool _t3_highlight_vector_reserve(vector_base_t *vector, size_t elsize) {
	if (vector->allocated <= vector->used) {
		size_t allocate = vector->allocated == 0 ? 8 : vector->allocated * 2;
		void *data = realloc(vector->data, allocate * elsize);
		if (data == NULL)
			return t3_false;
		vector->data = data;
		vector->allocated = allocate;
	}
	vector->used++;
	return t3_true;
}
