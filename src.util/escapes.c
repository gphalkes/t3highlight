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
#include <string.h>
#include <limits.h>
#include "t3highlight.h"

#if INT_MAX < 2147483647L
typedef long UChar32;
#else
typedef int UChar32;
#endif


static int is_hex_digit(int c) {
	return strchr("abcdefABCDEF0123456789", c) != NULL;
}

static int is_digit(int c) {
	return c >= '0' && c <= '9';
}

static int to_lower(int c) {
	return 'a' + (c - 'A');
}

/** Convert a codepoint to a UTF-8 string.
    @param c The codepoint to convert.
    @param dst The location to store the result.
    @return The number of bytes stored in @p dst.

    If an invalid codepoint is passed in @p c, the replacement character
    (@c FFFD) is stored instead
*/
static size_t put_utf8(UChar32 c, char *dst) {
	if (c < 0x80) {
		dst[0] = c;
		return 1;
	} else if (c < 0x800) {
		dst[0] = 0xC0 | (c >> 6);
		dst[1] = 0x80 | (c & 0x3F);
		return 2;
	} else if (c < 0x10000l) {
		dst[0] = 0xe0 | (c >> 12);
		dst[1] = 0x80 | ((c >> 6) & 0x3f);
		dst[2] = 0x80 | (c & 0x3F);
		return 3;
	} else if (c < 0x110000l) {
		dst[0] = 0xf0 | (c >> 18);
		dst[1] = 0x80 | ((c >> 12) & 0x3f);
		dst[2] = 0x80 | ((c >> 6) & 0x3f);
		dst[3] = 0x80 | (c & 0x3F);
		return 4;
	} else {
		/* Store the replacement character. */
		dst[0] = 0xEF;
		dst[1] = 0xBF;
		dst[2] = 0xBD;
		return 3;
	}
}

/** Convert a string from the input format to an internally usable string.
	@param string A @a Token with the string to be converted.
	@param descr A description of the string to be included in error messages.
	@return The length of the resulting string.

	The use of this function processes escape characters. The converted
	characters are written in the original string.
*/
size_t parse_escapes(char *string) {
	size_t max_read_position = strlen(string);
	size_t read_position = 0, write_position = 0;
	size_t i;

	while(read_position < max_read_position) {
		if (string[read_position] == '\\') {
			read_position++;

			if (read_position == max_read_position)
				break;

			switch(string[read_position++]) {
				case 'n':
					string[write_position++] = '\n';
					break;
				case 'r':
					string[write_position++] = '\r';
					break;
				case '\'':
					string[write_position++] = '\'';
					break;
				case '\\':
					string[write_position++] = '\\';
					break;
				case 't':
					string[write_position++] = '\t';
					break;
				case 'b':
					string[write_position++] = '\b';
					break;
				case 'f':
					string[write_position++] = '\f';
					break;
				case 'a':
					string[write_position++] = '\a';
					break;
				case 'v':
					string[write_position++] = '\v';
					break;
				case '?':
					string[write_position++] = '\?';
					break;
				case '"':
					string[write_position++] = '"';
					break;
				case 'e':
					string[write_position++] = '\033';
					break;
				case 'x': {
					/* Hexadecimal escapes */
					unsigned int value = 0;
					/* Read at most two characters, or as many as are valid. */
					for (i = 0; i < 2 && (read_position + i) < max_read_position && is_hex_digit(string[read_position + i]); i++) {
						value <<= 4;
						if (is_digit(string[read_position + i]))
							value += (int) (string[read_position + i] - '0');
						else
							value += (int) (to_lower(string[read_position + i]) - 'a') + 10;
						if (value > UCHAR_MAX)
							value = UCHAR_MAX + 1;
					}
					read_position += i;

					if (i == 0)
						write_position += 2;
					else if (value <= UCHAR_MAX)
						string[write_position++] = (char) value;
					break;
				}
				case '0':
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7': {
					/* Octal escapes */
					int value = (int)(string[read_position - 1] - '0');
					size_t max_idx = string[read_position - 1] < '4' ? 2 : 1;
					for (i = 0; i < max_idx && read_position + i < max_read_position && string[read_position + i] >= '0' && string[read_position + i] <= '7'; i++)
						value = value * 8 + (int)(string[read_position + i] - '0');

					read_position += i;

					string[write_position++] = (char) value;
					break;
				}
				case 'u':
				case 'U': {
					UChar32 value = 0;
					size_t chars = string[read_position - 1] == 'U' ? 8 : 4;

					if (max_read_position < read_position + chars) {
						write_position += max_read_position - read_position;
						read_position = max_read_position;
						break;
					}
					for (i = 0; i < chars; i++) {
						if (!is_hex_digit(string[read_position + i])) {
							write_position += i + 2;
							read_position += i;
							goto next;
						}
						value <<= 4;
						if (is_digit(string[read_position + i]))
							value += (int) (string[read_position + i] - '0');
						else
							value += (int) (to_lower(string[read_position + i]) - 'a') + 10;
					}

					if (value > 0x10FFFFL || (value & 0xF800L) == 0xD800L)
						write_position += 2 + chars;
					else
						/* The conversion won't overwrite subsequent characters because
						   \uxxxx is already the as long as the max utf-8 length */
						write_position += put_utf8(value, string + write_position);

					read_position += chars;
					break;
				}
				default:
					string[write_position++] = string[read_position];
					break;
			}
		} else {
			string[write_position++] = string[read_position++];
		}
next:;
	}
	/* Terminate string. */
	string[write_position] = 0;
	return write_position;
}

