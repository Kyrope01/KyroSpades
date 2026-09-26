
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

#ifndef GLX_H
#define GLX_H

#include <stdint.h>
#include <stdbool.h>

#if defined(OPENGL_ES) || defined(OPENGL_CORE)
#ifndef GLX_PROGRAMMABLE
#define GLX_PROGRAMMABLE
#endif
#endif

extern int glx_version;
extern int glx_fog;
extern int glx_context_version_major;
extern int glx_context_version_minor;

/* True when the active desktop GL / mobile GLES context meets a version.
 * Renderer migrations use this instead of assuming the preferred context was
 * actually created, because window creation deliberately has legacy fallbacks. */
bool glx_version_at_least(int major, int minor);

/* ES 2.0 runtime version: 1 = GLES 1.1 fallback, 2 = GLES 2.0 (set by window.c) */
extern int gles_version;

/* Color tracking — used by both ES 1.1 (fixed-function) and ES 2.0 (shaders).
 * glColor* macros in common.h route through glx_set_color4f(). */
extern float gles_current_color[4];
void glx_set_color4f(float r, float g, float b, float a);
void glx_get_current_color(float* dst);
void glx_set_line_width(float width);
void glx_set_team_color(float r, float g, float b);

struct glx_displaylist {
	uint32_t legacy;
	uint32_t modern;
	size_t size;        /* vertex count */
	size_t buffer_size; /* allocated VBO bytes */
	bool has_normal;
	bool has_color;
	bool has_texcoord;
};

enum {
	GLX_DISPLAYLIST_NORMAL,
	GLX_DISPLAYLIST_ENHANCED,
	GLX_DISPLAYLIST_POINTS,
};

void glx_init(void);
void glx_deinit(void);

int glx_shader(const char* vertex, const char* fragment);

/* Tracked shader-program state. All renderer-owned program changes pass through
 * this API so hot draw paths can avoid synchronous GL_CURRENT_PROGRAM queries
 * and repeated uniform-location lookups. Deletion also invalidates locations,
 * which keeps reused driver object names safe. */
void glx_use_program(unsigned int program);
unsigned int glx_current_program(void);
int glx_uniform_location(unsigned int program, const char* name);
void glx_delete_program(unsigned int program);

void glx_enable_sphericalfog(void);
void glx_disable_sphericalfog(void);

void glx_displaylist_create(struct glx_displaylist* x, bool has_color, bool has_normal);
void glx_displaylist_destroy(struct glx_displaylist* x);
void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type, void* color, void* vertex, void* normal,
							void* texcoord);
void glx_displaylist_draw(struct glx_displaylist* x, int type);

/* Select generic vertex attributes (locations 0..3) for a programmable draw.
 * Legacy draws retain the client-state array path until their shaders are
 * migrated. */
void glx_set_explicit_attributes(bool enabled);
bool glx_uses_explicit_attributes(void);

/* Cross-renderer alpha cutout: shader discard on programmable paths, fixed
 * alpha test on legacy paths. */
void glx_set_alpha_test(bool enabled, float threshold);

/* 2D draw helpers — programmable renderers use streamed VBO geometry. */
void glx_draw_vertices_3d(const float* vertices, size_t count, unsigned int mode);
void glx_draw_line_2d(float x1, float y1, float x2, float y2);
void glx_draw_quad_2d(float x, float y, float w, float h);
void glx_draw_ring_segment_2d(float cx, float cy, float inner_radius, float outer_radius,
                              float start_angle, float end_angle, int steps);
/* Vertical 2-color gradient quad: top edge = (r1,g1,b1), bottom edge = (r2,g2,b2). */
void glx_draw_gradient_quad_2d(float x, float y, float w, float h, float r1, float g1, float b1, float r2, float g2,
								float b2);

#ifdef GLX_PROGRAMMABLE
void glx_draw_screen_quad(void);
void glx_use_default_shader(void);
int glx_default_shader_program(void);
void glx_default_shader_set_draw_state(int with_vertex_color, int texture_enabled, int lighting_enabled);
void glx_default_shader_set_texcoord_scale(float scale);
void glx_default_shader_set_alpha_cutoff(float threshold);
void glx_default_shader_set_light_scale(float scale);
#endif

#endif
