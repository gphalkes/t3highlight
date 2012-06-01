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

#include "highlight.h"

t3_bool t3_highlight_utf8check(const char *line, size_t size) {
	size_t i;
	for (i = 0; i < size; ) {
		int bytes;
		switch (line[i] & 0xf0) {
			case 0xf0:
				bytes = 3;
				break;
			case 0xe0:
				bytes = 2;
				break;
			case 0xc0:
			case 0xd0:
				bytes = 1;
				break;
			default:
				i++;
				continue;
		}
		/* Check that there is no partial codepoint at the end. */
		if (bytes + i > size)
			return t3_false;

		if (bytes == 3) {
			/* Check for out-of-range codepoints. */
			if ((unsigned char) line[i] > 0xf4 || ((unsigned char) line[i] == 0xf4 && (unsigned char) line[i + 1] >= 0x90))
				return t3_false;
		} else if (bytes == 2) {
			/* Check for surrogates. */
			if ((unsigned char) line[i] == 0xed && (unsigned char) line[i] >= 0xa0)
				return t3_false;
		}

		i++;
		while (bytes > 0) {
			/* Check that follow-up bytes start with 10 binary. */
			if ((line[i] & 0xc0) != 0x80)
				return t3_false;
			i++;
			bytes--;
		}
	}
	return t3_true;
}
/*
D7FF: ED 9F BF
D800: ED A0 80 <start of surrogate range>
DFFF: ED BF BF <end of surrogate range>
E000: EE 80 80

10FFFF: F4 8F BF BF <last Unicode codepoint>
110000: F4 90 80 80 <first non-codepoint>
*/
