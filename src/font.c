
/*
	Copyright (c) 2017-2020 ByteBit

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

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "glx.h"
#include "file.h"
#include "hashtable.h"
#include "font.h"
#include "stb_truetype.h"
#include "utils.h"

/* Codepoint ranges to bake. Latin-1 plus common symbol blocks so chat
   like "☆" (U+2606) and arrows render properly. */
static const struct { int first; int count; } font_ranges[] = {
	{ 0x0020, 0x00E0 }, /* Basic Latin + Latin-1 Supplement */
	{ 0x2000, 0x0070 }, /* General Punctuation */
	{ 0x2070, 0x0030 }, /* Super/Subscripts */
	{ 0x20A0, 0x0030 }, /* Currency */
	{ 0x2100, 0x0050 }, /* Letterlike */
	{ 0x2150, 0x0040 }, /* Number Forms */
	{ 0x2190, 0x0070 }, /* Arrows */
	{ 0x2200, 0x0100 }, /* Mathematical */
	{ 0x2300, 0x0100 }, /* Misc Technical */
	{ 0x2500, 0x0080 }, /* Box Drawing */
	{ 0x2580, 0x0020 }, /* Block Elements */
	{ 0x25A0, 0x0060 }, /* Geometric Shapes */
	{ 0x2600, 0x0100 }, /* Misc Symbols (incl. ☆ U+2606) */
	{ 0x2700, 0x00C0 }, /* Dingbats */
	{ 0x2B00, 0x0100 }, /* Misc Symbols and Arrows */
};
#define FONT_RANGE_COUNT ((int)(sizeof(font_ranges) / sizeof(font_ranges[0])))

static short* font_vertex_buffer;
static short* font_coords_buffer;
static enum font_type font_current_type = FONT_FIXEDSYS;

static void* font_data_fixedsys;
static void* font_data_smallfnt;
static void* font_data_fantasy;

static HashTable fonts_backed;

struct __attribute__((packed)) font_backed_id {
	enum font_type type;
	float size;
};

struct font_backed_data {
	stbtt_packedchar* cdata;
	int range_offset[FONT_RANGE_COUNT];
	int total_chars;
	GLuint texture_id;
	int w, h;
};

void font_init() {
	font_vertex_buffer = malloc(2048 * 8 * sizeof(short));
	CHECK_ALLOCATION_ERROR(font_vertex_buffer)
	font_coords_buffer = malloc(2048 * 8 * sizeof(short));
	CHECK_ALLOCATION_ERROR(font_coords_buffer)

	font_data_fixedsys = file_load("fonts/Fixedsys.ttf");
	CHECK_ALLOCATION_ERROR(font_data_fixedsys)
	font_data_smallfnt = file_load("fonts/Terminal.ttf");
	CHECK_ALLOCATION_ERROR(font_data_smallfnt)
	font_data_fantasy  = file_load("fonts/ft88.ttf");
	CHECK_ALLOCATION_ERROR(font_data_fantasy)

	ht_setup(&fonts_backed, sizeof(struct font_backed_id), sizeof(struct font_backed_data), 8);
}

void font_select(enum font_type type) {
	font_current_type = type;
}

/* Decode one codepoint, advance s. Returns 0 at terminator, 0xFFFD on
   malformed bytes (and advances 1 byte to make progress). */
static unsigned font_utf8_next(const char** s) {
	const unsigned char* p = (const unsigned char*)*s;
	unsigned c = *p;
	if(c == 0) return 0;
	if(c < 0x80) { *s = (const char*)(p + 1); return c; }
	int extra; unsigned cp;
	if((c & 0xE0) == 0xC0)      { cp = c & 0x1F; extra = 1; }
	else if((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
	else if((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
	else                        { *s = (const char*)(p + 1); return 0xFFFD; }
	p++;
	for(int i = 0; i < extra; i++) {
		if((p[i] & 0xC0) != 0x80) { *s = (const char*)p; return 0xFFFD; }
		cp = (cp << 6) | (p[i] & 0x3F);
	}
	*s = (const char*)(p + extra);
	return cp;
}

static int font_glyph_index(struct font_backed_data* f, unsigned cp) {
	for(int i = 0; i < FONT_RANGE_COUNT; i++) {
		int first = font_ranges[i].first;
		int cnt   = font_ranges[i].count;
		if((int)cp >= first && (int)cp < first + cnt)
			return f->range_offset[i] + ((int)cp - first);
	}
	return -1;
}

static struct font_backed_data* font_find(float h) {
	if(font_current_type == FONT_SMALLFNT)
		h *= 1.5F;

	struct font_backed_id id = (struct font_backed_id) { .type = font_current_type, .size = h };
	struct font_backed_data* f_cached = ht_lookup(&fonts_backed, &id);
	if(f_cached)
		return f_cached;

	void* file;
	switch(font_current_type) {
		case FONT_FIXEDSYS: file = font_data_fixedsys; break;
		case FONT_SMALLFNT: file = font_data_smallfnt; break;
		case FONT_FANTASY:  file = font_data_fantasy; break;
		default: return NULL;
	}

	int total = 0;
	for(int i = 0; i < FONT_RANGE_COUNT; i++) total += font_ranges[i].count;

	struct font_backed_data f;
	f.total_chars = total;
	f.cdata = malloc(total * sizeof(stbtt_packedchar));
	CHECK_ALLOCATION_ERROR(f.cdata)

	stbtt_pack_range ranges[FONT_RANGE_COUNT];
	int off = 0;
	for(int i = 0; i < FONT_RANGE_COUNT; i++) {
		ranges[i].font_size                        = h;
		ranges[i].first_unicode_codepoint_in_range = font_ranges[i].first;
		ranges[i].array_of_unicode_codepoints      = NULL;
		ranges[i].num_chars                        = font_ranges[i].count;
		ranges[i].chardata_for_range               = f.cdata + off;
		f.range_offset[i] = off;
		off += font_ranges[i].count;
	}

	int max_size = 0;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);

	f.w = 256;
	f.h = 256;
	void* temp_bitmap = NULL;
	while(1) {
		temp_bitmap = realloc(temp_bitmap, f.w * f.h);
		CHECK_ALLOCATION_ERROR(temp_bitmap)
		stbtt_pack_context spc;
		if(!stbtt_PackBegin(&spc, temp_bitmap, f.w, f.h, 0, 1, NULL)) {
			free(temp_bitmap); free(f.cdata); return NULL;
		}
		int ok = stbtt_PackFontRanges(&spc, file, 0, ranges, FONT_RANGE_COUNT);
		stbtt_PackEnd(&spc);
		if(ok || (f.w >= max_size && f.h >= max_size)) break;
		if(f.h > f.w) f.w *= 2; else f.h *= 2;
	}

	log_info("font texsize: %i:%ipx [size %f] type: %i", f.w, f.h, h, font_current_type);

	glGenTextures(1, &f.texture_id);
	glBindTexture(GL_TEXTURE_2D, f.texture_id);
#ifdef OPENGL_ES
	size_t texel_count = (size_t)f.w * (size_t)f.h;
	unsigned char* rgba = malloc(texel_count * 4);
	CHECK_ALLOCATION_ERROR(rgba)
	for(size_t i = 0; i < texel_count; i++) {
		rgba[i * 4 + 0] = 255;
		rgba[i * 4 + 1] = 255;
		rgba[i * 4 + 2] = 255;
		rgba[i * 4 + 3] = ((const unsigned char*)temp_bitmap)[i];
	}
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, f.w, f.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	free(rgba);
#else
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, f.w, f.h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, temp_bitmap);
#endif
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glBindTexture(GL_TEXTURE_2D, 0);

	free(temp_bitmap);

	ht_insert(&fonts_backed, &id, &f);
	return ht_lookup(&fonts_backed, &id);
}

float font_length(float h, char* text) {
	struct font_backed_data* font = font_find(h);
	if(!font) return 0.0F;

	stbtt_aligned_quad q;
	float y = h * 0.75F;
	float x = 0.0F;
	float length = 0.0F;
	const char* s = text;
	for(;;) {
		unsigned cp = font_utf8_next(&s);
		if(cp == 0) break;
		if(cp == '\n') { length = fmax(length, x); x = 0.0F; continue; }
		int idx = font_glyph_index(font, cp);
		if(idx < 0) idx = font_glyph_index(font, '?');
		if(idx >= 0)
			stbtt_GetPackedQuad(font->cdata, font->w, font->h, idx, &x, &y, &q, 0);
	}
	return fmax(length, x) + h * 0.125F;
}

bool font_remove_callback(void* key, void* value, void* user) {
	struct font_backed_data* f = (struct font_backed_data*)value;
	glDeleteTextures(1, &f->texture_id);
	free(f->cdata);
	return true;
}

void font_reset() {
	ht_iterate_remove(&fonts_backed, NULL, font_remove_callback);
}

void font_render(float x, float y, float h, char* text) {
	struct font_backed_data* font = font_find(h);
	if(!font) return;

	size_t k = 0;
	float x2 = x;
	float y2 = h * 0.75F;
	const char* s = text;
	for(;;) {
		unsigned cp = font_utf8_next(&s);
		if(cp == 0) break;
		if(cp == '\n') { x2 = x; y2 += h; continue; }

		/* The vertex/coord buffers hold 2048*8 shorts and we write 12 per glyph.
		   Without this guard, a long string (more likely on a wide landscape
		   surface where text wraps less) overruns the buffer and segfaults the
		   render thread (SEGV_ACCERR). Stop emitting glyphs before that happens. */
		if(k + 12 > 2048 * 8) break;

		int idx = font_glyph_index(font, cp);
		if(idx < 0) idx = font_glyph_index(font, '?');
		if(idx < 0) continue;

		stbtt_aligned_quad q;
		stbtt_GetPackedQuad(font->cdata, font->w, font->h, idx, &x2, &y2, &q, 0);

		font_coords_buffer[k + 0] = q.s0 * 8192.0F;
		font_coords_buffer[k + 1] = q.t1 * 8192.0F;
		font_coords_buffer[k + 2] = q.s1 * 8192.0F;
		font_coords_buffer[k + 3] = q.t1 * 8192.0F;
		font_coords_buffer[k + 4] = q.s1 * 8192.0F;
		font_coords_buffer[k + 5] = q.t0 * 8192.0F;

		font_coords_buffer[k + 6] = q.s0 * 8192.0F;
		font_coords_buffer[k + 7] = q.t1 * 8192.0F;
		font_coords_buffer[k + 8] = q.s1 * 8192.0F;
		font_coords_buffer[k + 9] = q.t0 * 8192.0F;
		font_coords_buffer[k + 10] = q.s0 * 8192.0F;
		font_coords_buffer[k + 11] = q.t0 * 8192.0F;

		font_vertex_buffer[k + 0] = q.x0;
		font_vertex_buffer[k + 1] = -q.y1 + y;
		font_vertex_buffer[k + 2] = q.x1;
		font_vertex_buffer[k + 3] = -q.y1 + y;
		font_vertex_buffer[k + 4] = q.x1;
		font_vertex_buffer[k + 5] = -q.y0 + y;

		font_vertex_buffer[k + 6] = q.x0;
		font_vertex_buffer[k + 7] = -q.y1 + y;
		font_vertex_buffer[k + 8] = q.x1;
		font_vertex_buffer[k + 9] = -q.y0 + y;
		font_vertex_buffer[k + 10] = q.x0;
		font_vertex_buffer[k + 11] = -q.y0 + y;
		k += 12;
	}

#if defined(OPENGL_ES)
	if(gles_version >= 2) {
		glx_use_default_shader();
		glUniform4fv(glGetUniformLocation(glx_default_shader_program(), "u_Color"), 1, gles_current_color);
		glUniform1f(glGetUniformLocation(glx_default_shader_program(), "u_HasVertexColor"), 0.0F);
		glUniform1f(glGetUniformLocation(glx_default_shader_program(), "u_TextureEnabled"), 1.0F);
		/* Font texcoords are baked as 8192x8192; scale down by 1/8192 */
		glUniform1f(glGetUniformLocation(glx_default_shader_program(), "u_TexCoordScale"), 1.0F / 8192.0F);

		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, font->texture_id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glUniform1i(glGetUniformLocation(glx_default_shader_program(), "u_Texture"), 0);
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glVertexAttribPointer(0, 2, GL_SHORT, GL_FALSE, 0, font_vertex_buffer);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(2, 2, GL_SHORT, GL_FALSE, 0, font_coords_buffer);
		glEnableVertexAttribArray(2);

		glDrawArrays(GL_TRIANGLES, 0, k / 2);

		glDisableVertexAttribArray(0);
		glDisableVertexAttribArray(2);
		glDisable(GL_BLEND);
		glBindTexture(GL_TEXTURE_2D, 0);
		return;
	}
#endif
	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	{
		GLfloat s = 1.0F / 8192.0F;
		GLfloat scale[16] = {
			s, 0, 0, 0,
			0, s, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		};
		glMultMatrixf(scale);
	}
	glMatrixMode(GL_MODELVIEW);

	glDisableClientState(GL_COLOR_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glActiveTexture(GL_TEXTURE0);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, font->texture_id);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_TEXTURE_COORD_ARRAY);
	glVertexPointer(2, GL_SHORT, 0, font_vertex_buffer);
	glTexCoordPointer(2, GL_SHORT, 0, font_coords_buffer);
	glDrawArrays(GL_TRIANGLES, 0, k / 2);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_VERTEX_ARRAY);

	glDisable(GL_BLEND);
	glBindTexture(GL_TEXTURE_2D, 0);
	glDisable(GL_TEXTURE_2D);

	glMatrixMode(GL_TEXTURE);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
}

void font_render_shadow(float x, float y, float h, char* text, float a) {
	float color[4];
	glx_get_current_color(color);

	glColor4f(0.F, 0.F, 0.F, a);
	font_render(x, y - 1.F, h, text);
	font_render(x, y - 2.F, h, text);

	glColor4f(color[0], color[1], color[2], color[3]);
	font_render(x, y, h, text);
}

void font_centered(float x, float y, float h, char* text) {
	font_render(x - font_length(h, text) / 2.0F, y, h, text);
}

void font_centered_shadow(float x, float y, float h, char* text, float a) {
	font_render_shadow(x - font_length(h, text) / 2.0F, y, h, text, a);
}
