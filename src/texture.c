
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

#include <math.h>
#include <dirent.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "common.h"
#include "config.h"
#include "glx.h"
#include "lighting.h"
#include "texture.h"
#include "map.h"
#include "log.h"
#include "file.h"

#include "lodepng/lodepng.c"

struct texture texture_splash;
struct texture texture_splash_icon;
struct texture texture_ui_exit;
struct texture texture_minimap;
struct texture texture_gradient;
struct texture texture_dummy;

struct texture texture_health;
struct texture texture_block;
struct texture texture_blocks;
struct texture texture_blocks_custom;
struct texture texture_blocks_normal;
struct texture texture_blocks_material;
int texture_blocks_custom_loaded = 0;
int texture_blocks_materials_ready = 0;

/* Colour lookup table granularity for custom block textures. Each axis of the
   8-bit RGB colour space is quantised into CUSTOM_LUT_DIM steps; the table maps
   every quantised colour to the nearest custom tile. 32 keeps the table small
   (32^3 = 32768 ints) while staying accurate enough for average-colour matching. */
#define CUSTOM_LUT_DIM 32
static int* g_custom_lut = NULL;       /* dim^3 tile indices */
static int g_custom_lut_dim = CUSTOM_LUT_DIM;
static int g_custom_tile_count = 0;    /* number of valid textures packed */
static int g_custom_grid = 0;          /* atlas is grid x grid tiles */
struct texture texture_grenade;
struct texture texture_ammo_semi;
struct texture texture_ammo_smg;
struct texture texture_ammo_shotgun;

struct texture texture_color_selection;

struct texture texture_zoom_semi;
struct texture texture_zoom_smg;
struct texture texture_zoom_shotgun;

struct texture texture_white;
struct texture texture_loader;
struct texture texture_target;
struct texture texture_indicator;

struct texture texture_player;
struct texture texture_medical;
struct texture texture_intel;
struct texture texture_command;
struct texture texture_tracer;

struct texture texture_ui_wait;
struct texture texture_ui_join;
struct texture texture_ui_reload;
struct texture texture_ui_bg;
struct texture texture_ui_input;
struct texture texture_ui_box_empty;
struct texture texture_ui_box_check;
struct texture texture_ui_expanded;
struct texture texture_ui_collapsed;
struct texture texture_ui_flags;
struct texture texture_ui_alert;
struct texture texture_ui_joystick;
struct texture texture_ui_knob;

struct texture texture_rain1;
struct texture texture_rain2;
struct texture texture_rain3;
struct texture texture_particle_anim;

static char* texture_flags[251]
        = {"AD",  "AE", "AF", "AG", "AI", "AL", "AM", "AN", "AO", "AQ", "AR", "AS", "AT", "AU", "AW", "AX", "AZ", "BA",
           "BB",  "BD", "BE", "BF", "BG", "BH", "BI", "BJ", "BL", "BM", "BN", "BO", "BR", "BS", "BT", "BV", "BW", "BY",
           "BZ",  "CA", "CC", "CD", "CF", "CG", "CH", "CI", "CK", "CL", "CM", "CN", "CO", "CR", "CS", "CU", "CV", "CX",
           "CY",  "CZ", "DE", "DJ", "DK", "DM", "DO", "DZ", "EC", "EE", "EG", "EH", "ER", "ES", "ET", "FI", "FJ", "FK",
           "FM",  "FO", "FR", "FX", "GA", "GB", "GD", "GE", "GF", "GG", "GH", "GI", "GL", "GM", "GN", "GP", "GQ", "GR",
           "GS",  "GT", "GU", "GW", "GY", "HK", "HM", "HN", "HR", "HT", "HU", "ID", "IE", "IL", "IN", "IO", "IQ", "IR",
           "IS",  "IT", "JE", "JM", "JO", "JP", "KE", "KG", "KH", "KI", "KM", "KN", "KP", "KR", "KW", "KY", "KZ", "LA",
           "LAN", "LB", "LC", "LI", "LK", "LR", "LS", "LT", "LU", "LV", "LY", "MA", "MC", "MD", "ME", "MF", "MG", "MH",
           "MK",  "ML", "MM", "MN", "MO", "MP", "MQ", "MR", "MS", "MT", "MU", "MV", "MW", "MX", "MY", "MZ", "NA", "NC",
           "NE",  "NF", "NG", "NI", "NL", "NO", "NP", "NR", "NU", "NZ", "OM", "PA", "PE", "PF", "PG", "PH", "PK", "PL",
           "PM",  "PN", "PR", "PS", "PT", "PW", "PY", "QA", "RE", "RO", "RS", "RU", "RW", "SA", "SB", "SC", "SD", "SE",
           "SG",  "SH", "SI", "SJ", "SK", "SL", "SM", "SN", "SO", "SR", "ST", "SV", "SY", "SZ", "TC", "TD", "TF", "TG",
           "TH",  "TJ", "TK", "TL", "TM", "TN", "TO", "TP", "TR", "TT", "TV", "TW", "TZ", "UA", "UG", "UM", "US", "UY",
           "UZ",  "VA", "VC", "VE", "VG", "VI", "VN", "VU", "WF", "WS", "XT", "YE", "YT", "YU", "ZA", "ZM", "ZW"};

static int texture_flag_cmp(const void* a, const void* b) {
        return strcmp(a, *(const void* const*)b);
}

int texture_flag_index(const char* country) {
        char** res = bsearch(country, texture_flags, sizeof(texture_flags) / sizeof(texture_flags[0]), sizeof(char*),
                                                 texture_flag_cmp);
        return res ? (res - texture_flags) : -1;
}

void texture_flag_offset(int index, float* u, float* v) {
        if(index >= 0) {
                *u = (index % 14) * (18.0F / 256.0F);
                *v = (index / 14) * (12.0F / 256.0F);
        } else {
                *u = 0.0F;
                *v = 0.9375F;
        }
}

void texture_filter(struct texture* t, int filter) {
        glBindTexture(GL_TEXTURE_2D, t->texture_id);
        switch(filter) {
                case TEXTURE_FILTER_NEAREST:
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                        break;
                case TEXTURE_FILTER_LINEAR:
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        break;
                case TEXTURE_WRAP_CLAMP:
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        break;
        }
        glBindTexture(GL_TEXTURE_2D, 0);
}

int texture_create(struct texture* t, char* filename) {
        int sz = file_size(filename);
        void* data = file_load(filename);
        int error = lodepng_decode32(&t->pixels, &t->width, &t->height, data, sz);
        free(data);

        if(error) {
                log_warn("Could not load texture (%u): %s", error, lodepng_error_text(error));
                return 0;
        }

        log_debug("Loaded texture: %s (%ix%i)", filename, t->width, t->height);

        texture_resize_pow2(t, 0);

        glGenTextures(1, &t->texture_id);
        glBindTexture(GL_TEXTURE_2D, t->texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        return 1;
}

int texture_create_buffer(struct texture* t, int width, int height, unsigned char* buff, int new) {
        if(new)
                glGenTextures(1, &t->texture_id);
        t->width = width;
        t->height = height;
        t->pixels = buff;
        texture_resize_pow2(t, max(width, height));

        glBindTexture(GL_TEXTURE_2D, t->texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glBindTexture(GL_TEXTURE_2D, 0);
        return 1;
}

void texture_delete(struct texture* t) {
        if(t->pixels)
                free(t->pixels);
        glDeleteTextures(1, &t->texture_id);
}

#define texture_emit_rotated(tx, ty, x, y, a) cos(a) * (x)-sin(a) * (y) + (tx), sin(a) * (x) + cos(a) * (y) + (ty)

#if defined(GLX_PROGRAMMABLE)
static bool texture_programmable_active(void) {
#ifdef OPENGL_ES
        return gles_version >= 2;
#else
        return true;
#endif
}

static void programmable_emit_quad(const float* vertices, const float* texcoords, float tex_enabled) {
        static GLuint quad_stream_vbo = 0;
        int prog = glx_default_shader_program();
        glx_use_default_shader();
        glUniform4fv(glx_uniform_location(prog, "u_Color"), 1, gles_current_color);
        glUniform1f(glx_uniform_location(prog, "u_HasVertexColor"), 0.0F);
        glUniform1f(glx_uniform_location(prog, "u_TextureEnabled"), tex_enabled);
        glUniform1f(glx_uniform_location(prog, "u_LightingEnabled"), 0.0F);
        glUniform1f(glx_uniform_location(prog, "u_TexCoordScale"), 1.0F);
        if(tex_enabled > 0.5F)
                glUniform1i(glx_uniform_location(prog, "u_Texture"), 0);

        if(!quad_stream_vbo)
                glGenBuffers(1, &quad_stream_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, quad_stream_vbo);
        glBufferData(GL_ARRAY_BUFFER, 24 * sizeof(float), NULL, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, 12 * sizeof(float), vertices);
        glBufferSubData(GL_ARRAY_BUFFER, 12 * sizeof(float), 12 * sizeof(float), texcoords);

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)(12 * sizeof(float)));
        glEnableVertexAttribArray(2);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
}
#endif

void texture_draw_sector(struct texture* t, float x, float y, float w, float h, float u, float v, float us, float vs) {
#if defined(GLX_PROGRAMMABLE)
        if(texture_programmable_active()) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, t->texture_id);
                float vertices[12] = {x, y, x, y - h, x + w, y - h, x, y, x + w, y - h, x + w, y};
                float texcoords[12] = {u, v, u, v + vs, u + us, v + vs, u, v, u + us, v + vs, u + us, v};
                programmable_emit_quad(vertices, texcoords, 1.0F);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glActiveTexture(GL_TEXTURE0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, t->texture_id);

        float vertices[12] = {x, y, x, y - h, x + w, y - h, x, y, x + w, y - h, x + w, y};
        float texcoords[12] = {u, v, u, v + vs, u + us, v + vs, u, v, u + us, v + vs, u + us, v};
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);

        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
#endif
}

void texture_draw(struct texture* t, float x, float y, float w, float h) {
#if defined(GLX_PROGRAMMABLE)
        if(texture_programmable_active()) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                texture_draw_sector(t, x, y, w, h, 0.0F, 0.0F, 1.0F, 1.0F);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_BLEND);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, t->texture_id);
        texture_draw_empty(x, y, w, h);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
#endif
}

void texture_draw_shadow(struct texture* t, float x, float y, float w, float h) {
        float color[4];
        glx_get_current_color(color);

        glColor4f(0.F, 0.F, 0.F, 1.F);
        texture_draw(t, x, y - 1.F, w, h);
        texture_draw(t, x, y - 2.F, w, h);

        glColor4f(color[0], color[1], color[2], color[3]);
        texture_draw(t, x, y, w, h);
}

void texture_draw_rotated(struct texture* t, float x, float y, float w, float h, float angle) {
#if defined(GLX_PROGRAMMABLE)
        if(texture_programmable_active()) {
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, t->texture_id);
                float vertices[12]
                        = {texture_emit_rotated(x, y, -w / 2, h / 2, angle), texture_emit_rotated(x, y, -w / 2, -h / 2, angle),
                           texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, -w / 2, h / 2, angle),
                           texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, w / 2, h / 2, angle)};
                float texcoords[12] = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
                programmable_emit_quad(vertices, texcoords, 1.0F);
                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_BLEND);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glEnable(GL_TEXTURE_2D);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, t->texture_id);
        texture_draw_empty_rotated(x, y, w, h, angle);
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_BLEND);
        glDisable(GL_TEXTURE_2D);
#endif
}

void texture_draw_empty(float x, float y, float w, float h) {
#if defined(GLX_PROGRAMMABLE)
        if(texture_programmable_active()) {
                float vertices[12] = {x, y, x, y - h, x + w, y - h, x, y, x + w, y - h, x + w, y};
                float texcoords[12] = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
                programmable_emit_quad(vertices, texcoords, 0.0F);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glActiveTexture(GL_TEXTURE0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        float vertices[12] = {x, y, x, y - h, x + w, y - h, x, y, x + w, y - h, x + w, y};
        float texcoords[12] = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
#endif
}

void texture_draw_empty_rotated(float x, float y, float w, float h, float angle) {
#if defined(GLX_PROGRAMMABLE)
        if(texture_programmable_active()) {
                float vertices[12]
                        = {texture_emit_rotated(x, y, -w / 2, h / 2, angle), texture_emit_rotated(x, y, -w / 2, -h / 2, angle),
                           texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, -w / 2, h / 2, angle),
                           texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, w / 2, h / 2, angle)};
                float texcoords[12] = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
                programmable_emit_quad(vertices, texcoords, 0.0F);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glActiveTexture(GL_TEXTURE0);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        float vertices[12]
                = {texture_emit_rotated(x, y, -w / 2, h / 2, angle), texture_emit_rotated(x, y, -w / 2, -h / 2, angle),
                   texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, -w / 2, h / 2, angle),
                   texture_emit_rotated(x, y, w / 2, -h / 2, angle), texture_emit_rotated(x, y, w / 2, h / 2, angle)};
        float texcoords[12] = {0.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F, 0.0F};
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
        glVertexPointer(2, GL_FLOAT, 0, vertices);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
#endif
}

void texture_resize_pow2(struct texture* t, int min_size) {
        if(!t->pixels)
                return;
        int max_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
        /* If the query failed (no current context / GL error), max_size stays 0
           and the texture would be "resized" to 0x0 below. Bail out instead. */
        if(max_size <= 0 && min_size <= 0)
                return;
        max_size = max(max_size, min_size);

        int w = 1, h = 1;
#ifdef OPENGL_CORE
        /* NPOT textures are core functionality; GL_EXTENSIONS is not a valid
           glGetString query in a 3.3 Core context. */
        const bool has_npot = true;
#else
        const char* exts = (const char*)glGetString(GL_EXTENSIONS);
        const bool has_npot = exts != NULL && strstr(exts, "ARB_texture_non_power_of_two") != NULL;
#endif
        if(has_npot) {
                if(t->width <= max_size && t->height <= max_size)
                        return;
                w = t->width;
                h = t->height;
        } else {
                while(w < t->width)
                        w += w;
                while(h < t->height)
                        h += h;
        }

        w = min(w, max_size);
        h = min(h, max_size);

        if(t->width == w && t->height == h)
                return;

        log_info("texture original: %i:%i now: %i:%i limit: %i", t->width, t->height, w, h, max_size);

        if(!t->pixels)
                return;

        unsigned int* pixels_new = malloc(w * h * sizeof(unsigned int));
        CHECK_ALLOCATION_ERROR(pixels_new)
        for(int y = 0; y < h; y++) {
                for(int x = 0; x < w; x++) {
                        float px = (float)x / (float)w * (float)t->width;
                        float py = (float)y / (float)h * (float)t->height;
                        float u = px - (int)px;
                        float v = py - (int)py;
                        /* Bilinear neighbor fetch. The old code clamped the flat index to
                           t->width * t->height, which is ONE PAST the end of the pixel
                           buffer (last valid element is w*h - 1). Every resample therefore
                           read 4 bytes past the malloc'd buffer on its last pixels. On
                           devices where the allocation ends flush against a page boundary
                           (Scudo guard pages on newer Android), that read lands on an
                           unmapped page -> SIGSEGV SEGV_ACCERR in texture_resize_pow2 at
                           startup. Clamp each axis to its own edge instead; this also
                           fixes the old behavior of x+1 wrapping into the next row. */
                        int px0 = (int)px, py0 = (int)py;
                        if(px0 > t->width - 1)  px0 = t->width - 1;
                        if(py0 > t->height - 1) py0 = t->height - 1;
                        int px1 = min(px0 + 1, t->width - 1);
                        int py1 = min(py0 + 1, t->height - 1);
                        unsigned int aa = ((unsigned int*)t->pixels)[px0 + py0 * t->width];
                        unsigned int ba = ((unsigned int*)t->pixels)[px1 + py0 * t->width];
                        unsigned int ab = ((unsigned int*)t->pixels)[px0 + py1 * t->width];
                        unsigned int bb = ((unsigned int*)t->pixels)[px1 + py1 * t->width];
                        pixels_new[x + y * w] = 0;
                        pixels_new[x + y * w] |= (int)((1.0F - v) * u * red(ba) + (1.0F - v) * (1.0F - u) * red(aa)
                                                                                   + v * u * red(bb) + v * (1.0F - u) * red(ab));
                        pixels_new[x + y * w] |= (int)((1.0F - v) * u * green(ba) + (1.0F - v) * (1.0F - u) * green(aa)
                                                                                   + v * u * green(bb) + v * (1.0F - u) * green(ab))
                                << 8;
                        pixels_new[x + y * w] |= (int)((1.0F - v) * u * blue(ba) + (1.0F - v) * (1.0F - u) * blue(aa)
                                                                                   + v * u * blue(bb) + v * (1.0F - u) * blue(ab))
                                << 16;
                        pixels_new[x + y * w] |= (int)((1.0F - v) * u * alpha(ba) + (1.0F - v) * (1.0F - u) * alpha(aa)
                                                                                   + v * u * alpha(bb) + v * (1.0F - u) * alpha(ab))
                                << 24;
                }
        }

        t->width = w;
        t->height = h;
        free(t->pixels);
        t->pixels = (unsigned char*)pixels_new;
}

unsigned int texture_block_color(int x, int y) {
        int base[3][8] = {{15, 31, 31, 31, 0, 0, 0, 31}, {15, 0, 15, 31, 31, 31, 0, 0}, {15, 0, 0, 0, 0, 31, 31, 31}};

        if(x < 4) {
                return rgb(base[0][y] + x * (base[0][y] * 2 + 2) * (base[0][y] > 0),
                                   base[1][y] + x * (base[1][y] * 2 + 2) * (base[1][y] > 0),
                                   base[2][y] + x * (base[2][y] * 2 + 2) * (base[2][y] > 0));
        } else {
                return rgb(
                        min(base[0][y] + x * (base[0][y] * 2 + 2) * (base[0][y] > 0) + ((x - 3) * 64 - 33) * (!base[0][y]), 255),
                        min(base[1][y] + x * (base[1][y] * 2 + 2) * (base[1][y] > 0) + ((x - 3) * 64 - 33) * (!base[1][y]), 255),
                        min(base[2][y] + x * (base[2][y] * 2 + 2) * (base[2][y] > 0) + ((x - 3) * 64 - 33) * (!base[2][y]), 255));
        }
}

void texture_gradient_fog(unsigned int* gradient) {
        int size = 512;
        for(int y = 0; y < size; y++) {
                for(int x = 0; x < size; x++) {
                        int d = min(sqrt(distance2D(size / 2, size / 2, x, y)) / (float)size * 2.0F * 255.0F, 255);
                        gradient[x + y * size] = rgba(d, d, d, 255);
                }
        }
}

// Helper function to get a random PNG file from the bg folder
/* Collect *.png names from a file_dir_list() walk. */
struct texture_bg_list {
        char** names;
        int count, cap;
};

static void texture_bg_collect(const char* name, void* user) {
        struct texture_bg_list* l = (struct texture_bg_list*)user;
        size_t len = strlen(name);
        if(len <= 4 || strcmp(name + len - 4, ".png") != 0)
                return;
        if(l->count == l->cap) {
                l->cap = l->cap ? l->cap * 2 : 16;
                l->names = realloc(l->names, l->cap * sizeof(char*));
                CHECK_ALLOCATION_ERROR(l->names)
        }
        l->names[l->count] = malloc(len + 1);
        CHECK_ALLOCATION_ERROR(l->names[l->count])
        memcpy(l->names[l->count++], name, len + 1);
}

static char* texture_get_random_bg() {
        static char bg_path[512];

        /* Previously this enumerated png/bg with opendir()/readdir() directly.
           That works on desktop, but on Android the backgrounds live inside the
           APK as assets and are invisible to the POSIX filesystem API -- the
           scan came up empty, fell back to png/ui/bg.png (which no longer
           ships), and the menu rendered on plain black. file_dir_list() now
           also knows how to enumerate APK assets, so both platforms share one
           path here. Picked once per app launch (texture_init runs once), so
           the background stays put across connects/disconnects and rotates on
           restart. Note: no srand() here anymore -- main() already seeds, and
           reseeding from time(NULL) mid-init just stomped the global RNG. */
        struct texture_bg_list list = {0};
        if(file_dir_list("png/bg", texture_bg_collect, &list) <= 0 || list.count == 0) {
                free(list.names);
                return "png/ui/bg.png";
        }

        int target = rand() % list.count;
        snprintf(bg_path, sizeof(bg_path), "png/bg/%s", list.names[target]);

        for(int k = 0; k < list.count; k++)
                free(list.names[k]);
        free(list.names);

        return bg_path;
}

/* ── Custom block textures (png/textures/) ────────────────────────────────
   Textured Blocks feature: when png/textures/ contains PNGs, every block is
   textured with the PNG whose average colour is closest to the block colour.
   All PNGs are packed into one square atlas (CUSTOM_BLOCK_TILE px tiles) and a
   lookup table maps a quantised block colour straight to the matching tile. */

struct custom_tex_name {
        char** names;
        int count, cap;
};

static void texture_custom_collect(const char* name, void* user) {
        struct custom_tex_name* l = (struct custom_tex_name*)user;
        size_t len = strlen(name);
        if(len <= 4)
                return;
        /* case-insensitive .png check */
        const char* ext = name + len - 4;
        if(strcmp(ext, ".png") != 0 && strcmp(ext, ".PNG") != 0)
                return;
        if(l->count == l->cap) {
                l->cap = l->cap ? l->cap * 2 : 16;
                l->names = realloc(l->names, l->cap * sizeof(char*));
                CHECK_ALLOCATION_ERROR(l->names)
        }
        l->names[l->count] = malloc(len + 1);
        CHECK_ALLOCATION_ERROR(l->names[l->count])
        memcpy(l->names[l->count++], name, len + 1);
}

struct custom_tex {
        unsigned char* pixels; /* decoded RGBA, w x h */
        int w, h;
        int ar, ag, ab;        /* average colour (0..255) */
};

static int texture_next_pow2(int v) {
        int p = 1;
        while(p < v)
                p <<= 1;
        return p;
}

/* Delete a rejected custom texture from png/textures/. Only attempted where the
   filesystem is actually writable (desktop builds); on Android the textures
   live inside the read-only APK assets, so deletion is a no-op there. */
#if !defined(USE_ANDROID_FILE)
static void texture_custom_remove(const char* path) {
        if(remove(path) != 0)
                log_debug("Custom block texture: could not delete %s (read-only / in use)", path);
}
#else
static void texture_custom_remove(const char* path) { (void)path; }
#endif

void texture_load_custom_blocks(void) {
        /* Make sure the folder exists so users have somewhere to drop files. */
        file_dir_create("png/textures");

        struct custom_tex_name names = {0};
        if(file_dir_list("png/textures", texture_custom_collect, &names) <= 0 || names.count == 0) {
                for(int k = 0; k < names.count; k++)
                        free(names.names[k]);
                free(names.names);
                log_info("Custom block textures: no PNGs in png/textures/, using default atlas");
                return;
        }

        /* Decode every PNG, keep only square ones, and compute average colour. */
        struct custom_tex* texs = calloc((size_t)names.count, sizeof(struct custom_tex));
        CHECK_ALLOCATION_ERROR(texs)
        int valid = 0;

        for(int k = 0; k < names.count; k++) {
                char path[1024];
                snprintf(path, sizeof(path), "png/textures/%s", names.names[k]);

                unsigned char* data = file_load(path);
                if(!data) {
                        log_warn("Custom block texture: could not read %s", path);
                        free(names.names[k]);
                        continue;
                }
                unsigned char* px;
                unsigned int w, h;
                unsigned int sz = file_size(path);
                unsigned int err = lodepng_decode32(&px, &w, &h, data, sz);
                free(data);
                if(err) {
                        log_warn("Custom block texture: failed to decode %s (%u: %s)", path, err, lodepng_error_text(err));
                        free(names.names[k]);
                        continue;
                }
                if(w != h) {
                        log_warn("Custom block texture: %s is %ux%u, not square -- removed (textures must be square)",
                                 path, w, h);
                        free(px);
                        texture_custom_remove(path);
                        free(names.names[k]);
                        continue;
                }

                /* Reject images that contain any fully-transparent pixel; blocks
                   are opaque, so a transparent texel would punch a hole in a
                   block face. Such files are deleted automatically. */
                int has_transparent = 0;
                unsigned int ncheck = w * h;
                for(unsigned int p = 0; p < ncheck; p++) {
                        if(px[p * 4 + 3] == 0) {
                                has_transparent = 1;
                                break;
                        }
                }
                if(has_transparent) {
                        log_warn("Custom block texture: %s has fully-transparent pixels -- removed", path);
                        free(px);
                        texture_custom_remove(path);
                        free(names.names[k]);
                        continue;
                }

                /* Average colour over the whole image (all pixels, RGB only). */
                unsigned long long sr = 0, sg = 0, sb = 0;
                unsigned int n = w * h;
                for(unsigned int p = 0; p < n; p++) {
                        sr += px[p * 4 + 0];
                        sg += px[p * 4 + 1];
                        sb += px[p * 4 + 2];
                }
                int idx = valid++;
                texs[idx].pixels = px;
                texs[idx].w = (int)w;
                texs[idx].h = (int)h;
                texs[idx].ar = (int)(sr / n);
                texs[idx].ag = (int)(sg / n);
                texs[idx].ab = (int)(sb / n);

                free(names.names[k]);
        }
        free(names.names);

        if(valid == 0) {
                free(texs);
                log_info("Custom block textures: no usable square PNGs in png/textures/, using default atlas");
                return;
        }

        /* Build a square atlas: grid x grid tiles, each CUSTOM_BLOCK_TILE px.
           grid is rounded up to a power of two so the whole atlas stays a
           power-of-two texture (safe on every GL backend, no resampling). */
        int grid = texture_next_pow2((int)ceil(sqrt((double)valid)));
        int atlas_w = grid * CUSTOM_BLOCK_TILE;
        int atlas_h = grid * CUSTOM_BLOCK_TILE;
        unsigned char* atlas = calloc((size_t)atlas_w * (size_t)atlas_h, 4);
        CHECK_ALLOCATION_ERROR(atlas)

        /* Keep the average colours around for the lookup table (texs' decoded
           pixel buffers are freed as we pack the atlas). */
        int* texs_avg = malloc((size_t)valid * 3 * sizeof(int));
        CHECK_ALLOCATION_ERROR(texs_avg)
        for(int i = 0; i < valid; i++) {
                texs_avg[i * 3 + 0] = texs[i].ar;
                texs_avg[i * 3 + 1] = texs[i].ag;
                texs_avg[i * 3 + 2] = texs[i].ab;
        }

        for(int i = 0; i < valid; i++) {
                int col = i % grid;
                int row = i / grid;
                int ox = col * CUSTOM_BLOCK_TILE;
                int oy = row * CUSTOM_BLOCK_TILE;
                struct custom_tex* t = texs + i;
                int sw = t->w, sh = t->h;
                /* Nearest-neighbour resample (up or down) into the 16x16 slot.
                   Alpha is forced to 255: blocks are opaque. */
                for(int y = 0; y < CUSTOM_BLOCK_TILE; y++) {
                        int sy = (int)((double)y / CUSTOM_BLOCK_TILE * sh);
                        if(sy >= sh) sy = sh - 1;
                        for(int x = 0; x < CUSTOM_BLOCK_TILE; x++) {
                                int sx = (int)((double)x / CUSTOM_BLOCK_TILE * sw);
                                if(sx >= sw) sx = sw - 1;
                                unsigned char* src = t->pixels + (size_t)(sy * sw + sx) * 4;
                                unsigned char* dst = atlas + (size_t)((oy + y) * atlas_w + (ox + x)) * 4;
                                dst[0] = src[0];
                                dst[1] = src[1];
                                dst[2] = src[2];
                                dst[3] = 255;
                        }
                }
                free(t->pixels);
        }
        free(texs);

        /* Upload the atlas directly (no power-of-two resample, which would
           bleed neighbouring tiles together). Match the default atlas' filtering. */
        texture_blocks_custom.width = atlas_w;
        texture_blocks_custom.height = atlas_h;
        texture_blocks_custom.pixels = atlas;
        glGenTextures(1, &texture_blocks_custom.texture_id);
        glBindTexture(GL_TEXTURE_2D, texture_blocks_custom.texture_id);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas_w, atlas_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        g_custom_grid = grid;
        g_custom_tile_count = valid;

        /* Build the colour -> tile lookup table once, so per-block selection
           during chunk meshing is O(1). Each entry holds the tile whose average
           colour is closest (Euclidean RGB distance) to that quantised colour. */
        int dim = CUSTOM_LUT_DIM;
        int step = 256 / dim;
        size_t lut_size = (size_t)dim * dim * dim;
        int* lut = malloc(lut_size * sizeof(int));
        CHECK_ALLOCATION_ERROR(lut)
        for(int bi = 0; bi < dim; bi++) {
                for(int gi = 0; gi < dim; gi++) {
                        for(int ri = 0; ri < dim; ri++) {
                                int cr = ri * step, cg = gi * step, cb = bi * step;
                                int best = 0;
                                int best_d = 0x7FFFFFFF;
                                for(int t = 0; t < valid; t++) {
                                        int dr = cr - texs_avg[t * 3 + 0];
                                        int dg = cg - texs_avg[t * 3 + 1];
                                        int db = cb - texs_avg[t * 3 + 2];
                                        int d = dr * dr + dg * dg + db * db;
                                        if(d < best_d) {
                                                best_d = d;
                                                best = t;
                                        }
                                }
                                lut[(size_t)((bi * dim + gi) * dim + ri)] = best;
                        }
                }
        }
        free(texs_avg);
        g_custom_lut = lut;
        g_custom_lut_dim = dim;

        texture_blocks_custom_loaded = 1;
        log_info("Custom block textures: loaded %i PNG(s) into a %ix%i atlas (%ix%i tiles)",
                 valid, atlas_w, atlas_h, grid, grid);
}

struct texture* texture_blocks_atlas(void) {
        return texture_blocks_custom_loaded ? &texture_blocks_custom : &texture_blocks;
}

int texture_blocks_custom_grid(void) {
        return g_custom_grid;
}

static float texture_luminance(const unsigned char* pixel) {
        return pixel[0] * 0.2126F + pixel[1] * 0.7152F + pixel[2] * 0.0722F;
}

/* Build material maps for any block atlas, including user-provided atlases.
   Alpha stores height, RGB in the normal map stores a tangent-space normal;
   the material map stores roughness/specular/emission. This gives old texture
   packs a useful modern material response without requiring extra files. */
bool texture_blocks_prepare_materials(void) {
        if(texture_blocks_materials_ready)
                return true;
        struct texture* atlas = texture_blocks_atlas();
        if(!atlas || !atlas->pixels || atlas->width < 1 || atlas->height < 1)
                return false;

        int w = atlas->width;
        int h = atlas->height;
        if((size_t)w > SIZE_MAX / 4U / (size_t)h)
                return false;
        size_t bytes = (size_t)w * (size_t)h * 4U;
        unsigned char* normals = malloc(bytes);
        unsigned char* materials = malloc(bytes);
        CHECK_ALLOCATION_ERROR(normals)
        CHECK_ALLOCATION_ERROR(materials)

        int tile = (w >= CUSTOM_BLOCK_TILE && h >= CUSTOM_BLOCK_TILE) ? CUSTOM_BLOCK_TILE : max(w, h);
        for(int y = 0; y < h; y++) {
                for(int x = 0; x < w; x++) {
                        int tile_x = (x / tile) * tile;
                        int tile_y = (y / tile) * tile;
                        int left = max(x - 1, tile_x);
                        int right = min(x + 1, min(tile_x + tile - 1, w - 1));
                        int up = max(y - 1, tile_y);
                        int down = min(y + 1, min(tile_y + tile - 1, h - 1));
                        const unsigned char* px_l = atlas->pixels + ((size_t)y * w + left) * 4;
                        const unsigned char* px_r = atlas->pixels + ((size_t)y * w + right) * 4;
                        const unsigned char* px_u = atlas->pixels + ((size_t)up * w + x) * 4;
                        const unsigned char* px_d = atlas->pixels + ((size_t)down * w + x) * 4;
                        const unsigned char* px = atlas->pixels + ((size_t)y * w + x) * 4;

                        float dx = (texture_luminance(px_r) - texture_luminance(px_l)) / 255.0F;
                        float dy = (texture_luminance(px_d) - texture_luminance(px_u)) / 255.0F;
                        float nx = -dx * 1.8F;
                        float ny = -dy * 1.8F;
                        float nz = 1.0F;
                        float inv_len = 1.0F / sqrtf(nx * nx + ny * ny + nz * nz);
                        nx *= inv_len;
                        ny *= inv_len;
                        nz *= inv_len;

                        size_t o = ((size_t)y * w + x) * 4;
                        float luminance = texture_luminance(px) / 255.0F;
                        float local_contrast = fminf((fabsf(dx) + fabsf(dy)) * 0.5F, 1.0F);
                        normals[o + 0] = (unsigned char)((nx * 0.5F + 0.5F) * 255.0F);
                        normals[o + 1] = (unsigned char)((ny * 0.5F + 0.5F) * 255.0F);
                        normals[o + 2] = (unsigned char)((nz * 0.5F + 0.5F) * 255.0F);
                        normals[o + 3] = (unsigned char)(luminance * 255.0F);
                        materials[o + 0] = (unsigned char)((0.82F - 0.35F * local_contrast) * 255.0F); /* roughness */
                        materials[o + 1] = (unsigned char)((0.05F + 0.20F * luminance) * 255.0F);      /* specular */
                        materials[o + 2] = 0;                                                           /* emission */
                        materials[o + 3] = 255;
                }
        }

        GLint previous_active_texture = GL_TEXTURE0;
        GLint previous_texture0 = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture0);
        texture_create_buffer(&texture_blocks_normal, w, h, normals, 1);
        texture_create_buffer(&texture_blocks_material, w, h, materials, 1);
        texture_filter(&texture_blocks_normal, TEXTURE_WRAP_CLAMP);
        texture_filter(&texture_blocks_material, TEXTURE_WRAP_CLAMP);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture0);
        glActiveTexture((GLenum)previous_active_texture);
        texture_blocks_materials_ready = 1;
        log_info("Generated normal/material maps for the %ix%i block atlas", w, h);
        return true;
}

void texture_blocks_release_materials(void) {
        if(!texture_blocks_materials_ready)
                return;
        texture_delete(&texture_blocks_normal);
        texture_delete(&texture_blocks_material);
        memset(&texture_blocks_normal, 0, sizeof(texture_blocks_normal));
        memset(&texture_blocks_material, 0, sizeof(texture_blocks_material));
        texture_blocks_materials_ready = 0;
}

int texture_blocks_custom_tile(uint32_t color) {
        if(!texture_blocks_custom_loaded || !g_custom_lut)
                return 0;
        int dim = g_custom_lut_dim;
        int step = 256 / dim;
        int ri = min(red(color) / step, dim - 1);
        int gi = min(green(color) / step, dim - 1);
        int bi = min(blue(color) / step, dim - 1);
        return g_custom_lut[(size_t)((bi * dim + gi) * dim + ri)];
}

void texture_init() {
        texture_create(&texture_splash, "png/splash.png");
        texture_create(&texture_splash_icon, "png/splash_icon.png");

        texture_create(&texture_health, "png/health.png");
        texture_create(&texture_block, "png/block.png");
        texture_create(&texture_blocks, "png/multimapblock.png");
        glBindTexture(GL_TEXTURE_2D, texture_blocks.texture_id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        texture_create(&texture_grenade, "png/grenade.png");
        texture_create(&texture_ammo_semi, "png/semiammo.png");
        texture_create(&texture_ammo_smg, "png/smgammo.png");
        texture_create(&texture_ammo_shotgun, "png/shotgunammo.png");

        texture_create(&texture_zoom_semi, "png/semi.png");
        texture_create(&texture_zoom_smg, "png/smg.png");
        texture_create(&texture_zoom_shotgun, "png/shotgun.png");

        texture_create(&texture_white, "png/white.png");
        texture_create(&texture_loader, "png/splashloader.png");
        texture_create(&texture_target, "png/target.png");
        texture_create(&texture_indicator, "png/indicator.png");

        texture_create(&texture_player, "png/player.png");
        texture_create(&texture_medical, "png/medical.png");
        texture_create(&texture_intel, "png/intel.png");
        texture_create(&texture_command, "png/command.png");
        texture_create(&texture_tracer, "png/tracer.png");

        texture_create(&texture_ui_exit, "png/ui/exit.png");
        texture_create(&texture_ui_wait, "png/ui/wait.png");
        texture_filter(&texture_ui_wait, TEXTURE_FILTER_LINEAR);
        texture_create(&texture_ui_join, "png/ui/join.png");
        texture_create(&texture_ui_reload, "png/ui/reload.png");
        texture_create(&texture_ui_bg, texture_get_random_bg());
        texture_create(&texture_ui_input, "png/ui/input.png");
        texture_create(&texture_ui_box_empty, "png/ui/box_empty.png");
        texture_create(&texture_ui_box_check, "png/ui/box_check.png");
        texture_create(&texture_ui_collapsed, "png/ui/collapsed.png");
        texture_create(&texture_ui_expanded, "png/ui/expanded.png");
        texture_create(&texture_ui_flags, "png/ui/flags.png");
        texture_filter(&texture_ui_flags, TEXTURE_FILTER_LINEAR);
        texture_create(&texture_ui_alert, "png/ui/alert.png");
        texture_filter(&texture_ui_alert, TEXTURE_FILTER_LINEAR);

#ifdef USE_TOUCH
        texture_create(&texture_ui_knob, "png/ui/knob.png");
        texture_filter(&texture_ui_knob, TEXTURE_FILTER_LINEAR);
        texture_create(&texture_ui_joystick, "png/ui/joystick.png");
        texture_filter(&texture_ui_joystick, TEXTURE_FILTER_LINEAR);
#endif

        texture_create(&texture_rain1, "png/weather_pack_rain_raindrop_1.png");
        texture_filter(&texture_rain1, TEXTURE_FILTER_LINEAR);
        texture_create(&texture_rain2, "png/weather_pack_rain_raindrop_2.png");
        texture_filter(&texture_rain2, TEXTURE_FILTER_LINEAR);
        texture_create(&texture_rain3, "png/weather_pack_rain_raindrop_3.png");
        texture_filter(&texture_rain3, TEXTURE_FILTER_LINEAR);

        /* Animated block-debris sprite sheet ("Particle animations" setting).
           Pre-process once at load so the sheet can be tinted per block colour:
           1. Sheets saved without transparency (opaque white background) get the
              near-white background keyed out, with a soft edge.
           2. RGB is rescaled so the brightest pixel becomes 255; with GL_MODULATE
              the particle then shows the block's own colour on its lit face
              instead of a darkened version of it. */
        if(texture_create(&texture_particle_anim, "png/particle_block_anim.png") && texture_particle_anim.pixels) {
                struct texture* t = &texture_particle_anim;
                size_t count = (size_t)t->width * (size_t)t->height;
                unsigned char* px = t->pixels;

                int has_alpha = 0;
                for(size_t i = 0; i < count; i++) {
                        if(px[i * 4 + 3] < 255) {
                                has_alpha = 1;
                                break;
                        }
                }

                /* Only near-white pixels connected to the sheet border (the
                   background between frames) are removed.  A plain "white ->
                   transparent" pass would also punch holes in the light, lit
                   faces of the cubes. */
                if(!has_alpha) {
                        int w = t->width, h = t->height;
                        int* stack = malloc(sizeof(int) * (size_t)w * (size_t)h);
                        if(stack) {
                                int sp = 0;
#define PANIM_BG(idx) (px[(idx) * 4 + 3] == 255 && min(px[(idx) * 4], min(px[(idx) * 4 + 1], px[(idx) * 4 + 2])) >= 235)
#define PANIM_PUSH(idx)                          \
        do {                                     \
                int _i = (idx);                  \
                if(PANIM_BG(_i)) {               \
                        px[_i * 4 + 3] = 0;      \
                        stack[sp++] = _i;        \
                }                                \
        } while(0)
                                for(int x = 0; x < w; x++) {
                                        PANIM_PUSH(x);
                                        PANIM_PUSH((h - 1) * w + x);
                                }
                                for(int y = 0; y < h; y++) {
                                        PANIM_PUSH(y * w);
                                        PANIM_PUSH(y * w + w - 1);
                                }
                                while(sp > 0) {
                                        int i = stack[--sp];
                                        int x = i % w, y = i / w;
                                        if(x > 0) PANIM_PUSH(i - 1);
                                        if(x < w - 1) PANIM_PUSH(i + 1);
                                        if(y > 0) PANIM_PUSH(i - w);
                                        if(y < h - 1) PANIM_PUSH(i + w);
                                }
#undef PANIM_PUSH
#undef PANIM_BG
                                free(stack);
                        }
                }

                int brightest = 0;
                for(size_t i = 0; i < count; i++) {
                        unsigned char* c = px + i * 4;
                        if(c[3] > 128)
                                brightest = max(brightest, max(c[0], max(c[1], c[2])));
                }

                for(size_t i = 0; i < count; i++) {
                        unsigned char* c = px + i * 4;
                        if(c[3] == 0) {
                                c[0] = c[1] = c[2] = 0;
                        } else if(brightest > 0 && brightest < 255) {
                                for(int k = 0; k < 3; k++)
                                        c[k] = (unsigned char)min(255, c[k] * 255 / brightest);
                        }
                }

                glBindTexture(GL_TEXTURE_2D, t->texture_id);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t->width, t->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, t->pixels);
                glBindTexture(GL_TEXTURE_2D, 0);
                texture_filter(t, TEXTURE_FILTER_LINEAR);
                texture_filter(t, TEXTURE_WRAP_CLAMP);
        }

        unsigned int pixels[64 * 64];
        memset(pixels, 0, sizeof(pixels));

        for(int y = 0; y < 8; y++) {
                for(int x = 0; x < 8; x++) {
                        for(int ys = 0; ys < 6; ys++) {
                                for(int xs = 0; xs < 6; xs++) {
                                        pixels[(x * 8 + xs) + (y * 8 + ys) * 64] = 0xFF000000 | texture_block_color(x, y);
                                }
                        }
                }
        }

        texture_create_buffer(&texture_color_selection, 64, 64, (unsigned char*)pixels, 1);

        texture_create_buffer(&texture_minimap, map_size_x, map_size_z, NULL, 1);
        texture_filter(&texture_minimap, TEXTURE_WRAP_CLAMP);

        unsigned int* gradient = malloc(512 * 512 * sizeof(unsigned int));
        CHECK_ALLOCATION_ERROR(gradient)
        texture_gradient_fog(gradient);
        texture_create_buffer(&texture_gradient, 512, 512, (unsigned char*)gradient, 1);
        texture_filter(&texture_gradient, TEXTURE_FILTER_LINEAR);

        texture_create_buffer(&texture_dummy, 1, 1, (unsigned char[]) {0, 0, 0, 0}, 1);

        /* Textured Blocks: load any user PNGs from png/textures/ into a custom
           atlas. When present they replace the built-in block atlas. */
        texture_load_custom_blocks();
        /* Skip their CPU/GPU cost when lighting is disabled or unavailable. */
        if(lighting_supported())
                texture_blocks_prepare_materials();
}
