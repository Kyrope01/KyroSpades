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

#ifndef CHARSET_H
#define CHARSET_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Text on the wire is Code Page 437 unless it is UTF-8 behind the 0xff prefix
   of the UTF-8 Chat extension (aosprotocol extensions/utf-8-chat.md). The font
   takes UTF-8, so everything a server sends is converted on the way in, and
   everything this client sends is marked when it is not printable ASCII --
   the only range that means the same in both encodings. */
#define CHARSET_UTF8_PREFIX 0xffu

/* UTF-8 for one Code Page 437 byte. A byte below 0x80 encodes to itself.
   Writes at most 4 bytes and returns how many. */
size_t charset_cp437_glyph(uint8_t byte, char out[4]);

/* Length in bytes of the UTF-8 sequence a lead byte starts, 1 for ASCII. The
   caller must have checked that the sequence is well formed. */
size_t charset_utf8_length(uint8_t lead);

/* Rejects overlong forms, surrogates, anything above U+10FFFF and any
   truncated or bad continuation. */
bool charset_utf8_well_formed(const uint8_t* s, size_t length);

/* True when every byte is printable ASCII (0x20-0x7E), the only range that
   reads the same in Code Page 437 and UTF-8. */
bool charset_is_printable_ascii(const char* s, size_t length);

/* Decodes `length` Code Page 437 bytes to UTF-8 in `out`. Bytes below 0x80 are
   copied as they are, so the chat colour codes keep their meaning. Never
   splits a codepoint: stops at the last one that fits. Returns the bytes
   written, excluding the terminator. */
size_t charset_cp437_to_utf8(char* out, size_t capacity, const uint8_t* in, size_t length);

#endif
