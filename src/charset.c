/*
	Copyright (c) 2026 KyroSpades contributors

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <string.h>
#include "charset.h"

/* Code Page 437 for bytes 0x80-0xFF, as Unicode. */
static const unsigned short cp437[128] = {
	0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, /* 80 */
	0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, /* 88 */
	0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, /* 90 */
	0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, /* 98 */
	0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, /* a0 */
	0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, /* a8 */
	0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, /* b0 */
	0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, /* b8 */
	0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, /* c0 */
	0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, /* c8 */
	0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, /* d0 */
	0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, /* d8 */
	0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, /* e0 */
	0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229, /* e8 */
	0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, /* f0 */
	0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0  /* f8 */
};

static size_t utf8_encode(unsigned codepoint, char out[4]) {
	if(codepoint < 0x80) {
		out[0] = (char)codepoint;
		return 1;
	}
	if(codepoint < 0x800) {
		out[0] = (char)(0xc0 | (codepoint >> 6));
		out[1] = (char)(0x80 | (codepoint & 0x3f));
		return 2;
	}
	if(codepoint < 0x10000) {
		out[0] = (char)(0xe0 | (codepoint >> 12));
		out[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
		out[2] = (char)(0x80 | (codepoint & 0x3f));
		return 3;
	}
	out[0] = (char)(0xf0 | (codepoint >> 18));
	out[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
	out[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
	out[3] = (char)(0x80 | (codepoint & 0x3f));
	return 4;
}

size_t charset_cp437_glyph(uint8_t byte, char out[4]) {
	return utf8_encode(byte < 0x80 ? byte : cp437[byte - 0x80], out);
}

size_t charset_utf8_length(uint8_t lead) {
	if(lead < 0x80) return 1;
	if((lead & 0xe0) == 0xc0) return 2;
	if((lead & 0xf0) == 0xe0) return 3;
	return 4;
}

bool charset_utf8_well_formed(const uint8_t* s, size_t length) {
	static const unsigned lowest[4] = {0, 0x80, 0x800, 0x10000};
	size_t i = 0;
	while(i < length) {
		uint8_t b = s[i];
		if(b < 0x80) {
			i++;
			continue;
		}
		unsigned codepoint;
		size_t extra;
		if(b >= 0xc2 && b <= 0xdf) {
			codepoint = b & 0x1f;
			extra = 1;
		} else if(b >= 0xe0 && b <= 0xef) {
			codepoint = b & 0x0f;
			extra = 2;
		} else if(b >= 0xf0 && b <= 0xf4) {
			codepoint = b & 0x07;
			extra = 3;
		} else {
			return false; /* 0x80-0xc1 and 0xf5-0xff never start a sequence */
		}
		if(i + extra >= length) return false;
		for(size_t k = 1; k <= extra; k++) {
			if((s[i + k] & 0xc0) != 0x80) return false;
			codepoint = (codepoint << 6) | (s[i + k] & 0x3f);
		}
		if(codepoint < lowest[extra] || codepoint > 0x10ffff) return false;
		if(codepoint >= 0xd800 && codepoint <= 0xdfff) return false;
		i += extra + 1;
	}
	return true;
}

bool charset_is_printable_ascii(const char* s, size_t length) {
	for(size_t i = 0; i < length; i++) {
		unsigned char c = (unsigned char)s[i];
		if(c < 0x20 || c >= 0x7f) return false;
	}
	return true;
}

size_t charset_cp437_to_utf8(char* out, size_t capacity, const uint8_t* in, size_t length) {
	if(!out || !capacity) return 0;
	size_t o = 0;
	for(size_t i = 0; i < length; i++) {
		char glyph[4];
		size_t n = charset_cp437_glyph(in[i], glyph);
		if(o + n > capacity - 1) break;
		memcpy(out + o, glyph, n);
		o += n;
	}
	out[o] = 0;
	return o;
}
