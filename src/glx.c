
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
#include <math.h>
#include <string.h>
#ifdef OPENGL_ES
#include <dlfcn.h>
#endif

#include "common.h"
#include "camera.h"
#include "config.h"
#include "map.h"
#include "matrix.h"
#include "texture.h"
#include "glx.h"

int glx_version = 0;
int glx_fog = 0;
int gles_version = 0;
float gles_current_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};

/* ── ES 2.0 default shader ──────────────────────────────────────────────── */

#ifdef OPENGL_ES
static GLuint default_shader = 0;
static GLint loc_u_MVP = -1;
static GLint loc_u_Color = -1;
static GLint loc_u_HasVertexColor = -1;
static GLint loc_u_TextureEnabled = -1;
static GLint loc_u_Texture = -1;
static GLint loc_u_TexCoordScale = -1;
static GLint loc_u_TeamColor = -1;
static GLint loc_u_Model = -1;
static GLint loc_u_Camera = -1;
static GLint loc_u_FogDist = -1;
static GLint loc_u_FogColor = -1;
static GLuint quad_vbo = 0;
static GLuint line_quad_vbo = 0;

static const char* default_vs =
        "attribute vec4 a_Position;\n"
        "attribute vec4 a_Color;\n"
        "attribute vec2 a_TexCoord;\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "uniform vec4 u_Color;\n"
        "uniform float u_HasVertexColor;\n"
        "uniform float u_TexCoordScale;\n"
        "uniform vec4 u_TeamColor;\n"
        "uniform vec3 u_Camera;\n"
        "uniform float u_FogDist;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying float v_Fog;\n"
        "void main() {\n"
        "    v_Color = mix(u_Color, a_Color, u_HasVertexColor) * u_TeamColor;\n"
        "    v_TexCoord = a_TexCoord * u_TexCoordScale;\n"
        "    vec3 world = (u_Model * a_Position).xyz;\n"
        "    v_Fog = clamp(length(world.xz - u_Camera.xz) * u_FogDist, 0.0, 1.0);\n"
        "    gl_Position = u_MVP * a_Position;\n"
        "}\n";

static const char* default_fs =
        "precision mediump float;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying float v_Fog;\n"
        "uniform sampler2D u_Texture;\n"
        "uniform float u_TextureEnabled;\n"
        "uniform vec3 u_FogColor;\n"
        "void main() {\n"
        "    vec4 tex = texture2D(u_Texture, v_TexCoord);\n"
        "    vec4 c = mix(vec4(1.0), tex, u_TextureEnabled) * v_Color;\n"
        "    gl_FragColor = vec4(mix(c.rgb, u_FogColor, v_Fog), c.a);\n"
        "}\n";
#endif

/* ── Color tracking ──────────────────────────────────────────────────────── */

void glx_set_color4f(float r, float g, float b, float a) {
        gles_current_color[0] = r;
        gles_current_color[1] = g;
        gles_current_color[2] = b;
        gles_current_color[3] = a;
#ifdef OPENGL_ES
        if(gles_version < 2) {
                /* ES 1.1: call real glColor4f via dlsym to avoid macro recursion */
                static void (*real_glColor4f)(float, float, float, float) = NULL;
                static int resolved = 0;
                if(!resolved) {
                        real_glColor4f = (void (*)(float, float, float, float))dlsym(RTLD_DEFAULT, "glColor4f");
                        resolved = 1;
                }
                if(real_glColor4f)
                        real_glColor4f(r, g, b, a);
        }
#endif
}

void glx_get_current_color(float* dst) {
        dst[0] = gles_current_color[0];
        dst[1] = gles_current_color[1];
        dst[2] = gles_current_color[2];
        dst[3] = gles_current_color[3];
}

void glx_set_team_color(float r, float g, float b) {
#ifdef OPENGL_ES
        if(gles_version >= 2) {
                GLint prog;
                glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
                if(prog) {
                        GLint loc = glGetUniformLocation(prog, "u_TeamColor");
                        if(loc >= 0)
                                glUniform4f(loc, r, g, b, 1.0F);
                }
        }
#endif
}

/* ── GL version detection ────────────────────────────────────────────────── */

static int glx_major_ver() {
#ifdef OPENGL_ES
        return gles_version;
#else
        return atoi(glGetString(GL_VERSION));
#endif
}

void glx_init() {
#ifndef OPENGL_ES
        glx_version = glx_major_ver() >= 2;
#else
        if(gles_version >= 2) {
                default_shader = glx_shader(default_vs, default_fs);
                if(default_shader) {
                        loc_u_MVP = glGetUniformLocation(default_shader, "u_MVP");
                        loc_u_Color = glGetUniformLocation(default_shader, "u_Color");
                        loc_u_HasVertexColor = glGetUniformLocation(default_shader, "u_HasVertexColor");
                        loc_u_TextureEnabled = glGetUniformLocation(default_shader, "u_TextureEnabled");
                        loc_u_Texture = glGetUniformLocation(default_shader, "u_Texture");
                        loc_u_TexCoordScale = glGetUniformLocation(default_shader, "u_TexCoordScale");
                        loc_u_TeamColor = glGetUniformLocation(default_shader, "u_TeamColor");
                        loc_u_Model = glGetUniformLocation(default_shader, "u_Model");
                        loc_u_Camera = glGetUniformLocation(default_shader, "u_Camera");
                        loc_u_FogDist = glGetUniformLocation(default_shader, "u_FogDist");
                        loc_u_FogColor = glGetUniformLocation(default_shader, "u_FogColor");
                        glUseProgram(default_shader);
                        glUniform1i(loc_u_Texture, 0);
                        glUniform4f(loc_u_Color, 1.0F, 1.0F, 1.0F, 1.0F);
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                        glUniform4f(loc_u_TeamColor, 1.0F, 1.0F, 1.0F, 1.0F);
                        glUniformMatrix4fv(loc_u_Model, 1, GL_FALSE, (float[]) {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
                        glUniform3f(loc_u_Camera, 0.0F, 0.0F, 0.0F);
                        glUniform1f(loc_u_FogDist, 0.0F);
                        glUniform3f(loc_u_FogColor, 0.0F, 0.0F, 0.0F);
                        glUseProgram(0);
                        log_info("GLES 2.0 default shader compiled (program %u)", default_shader);
                } else {
                        log_error("GLES 2.0 default shader compilation failed");
                }
                glx_version = 1;
        } else {
                glx_version = 0;
                log_info("GLES 1.1 fallback path");
        }
#endif
}

/* ── Shader compilation (unified for desktop GL + ES 2.0) ────────────────── */

int glx_shader(const char* vertex, const char* fragment) {
#ifndef OPENGL_ES
        if(!glx_version)
                return 0;
#endif
        int v = 0, f = 0;
        if(vertex) {
                v = glCreateShader(GL_VERTEX_SHADER);
                glShaderSource(v, 1, (const GLchar* const*)&vertex, NULL);
                glCompileShader(v);
                GLint ok;
                glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
                if(!ok) {
                        char log[1024];
                        glGetShaderInfoLog(v, sizeof(log), NULL, log);
                        log_error("Vertex shader compile error: %s", log);
                }
        }

        if(fragment) {
                f = glCreateShader(GL_FRAGMENT_SHADER);
                glShaderSource(f, 1, (const GLchar* const*)&fragment, NULL);
                glCompileShader(f);
                GLint ok;
                glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
                if(!ok) {
                        char log[1024];
                        glGetShaderInfoLog(f, sizeof(log), NULL, log);
                        log_error("Fragment shader compile error: %s", log);
                }
        }

        int program = glCreateProgram();
        if(vertex)
                glAttachShader(program, v);
        if(fragment)
                glAttachShader(program, f);
        glBindAttribLocation(program, 0, "a_Position");
        glBindAttribLocation(program, 1, "a_Color");
        glBindAttribLocation(program, 2, "a_TexCoord");
        glBindAttribLocation(program, 3, "a_Normal");
        glLinkProgram(program);

        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if(!linked) {
                char log[1024];
                glGetProgramInfoLog(program, sizeof(log), NULL, log);
                log_error("Shader link error: %s", log);
        }

        if(v) glDeleteShader(v);
        if(f) glDeleteShader(f);

        /* Previously this returned `program` unconditionally, so a shader that
           failed to compile or link still handed back a non-zero (but unusable)
           program object. Callers that gate on `if(shader)` were then fooled into
           using a dead program, whose draw call no-ops — which on iOS looked like
           "the post-processed frame never reaches the screen". Return 0 on failure
           so callers can detect it and fall back. */
        if(!linked) {
                glDeleteProgram(program);
                return 0;
        }
        return program;
}

/* ── Display list abstraction ─────────────────────────────────────────────── */

void glx_displaylist_create(struct glx_displaylist* x, bool has_color, bool has_normal) {
        x->has_color = has_color;
        x->has_normal = has_normal;
        x->has_texcoord = false;

#ifndef OPENGL_ES
        if(!glx_version || settings.force_displaylist) {
                x->legacy = glGenLists(1);
        } else {
                glGenBuffers(1, &x->modern);
        }
#else
        glGenBuffers(1, &x->modern);
#endif
        x->buffer_size = 0;
}

void glx_displaylist_destroy(struct glx_displaylist* x) {
#ifndef OPENGL_ES
        if(!glx_version || settings.force_displaylist) {
                glDeleteLists(x->legacy, 1);
        } else {
                glDeleteBuffers(1, &x->modern);
        }
#else
        glDeleteBuffers(1, &x->modern);
#endif
}

void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type, void* color, void* vertex, void* normal,
                                                        void* texcoord) {
        x->has_texcoord = (texcoord != NULL);
        int grow_buffer = size > x->buffer_size;
        x->buffer_size = max(x->buffer_size, size);
        x->size = size;

#ifndef OPENGL_ES
        if(!glx_version || settings.force_displaylist) {
                glEnableClientState(GL_VERTEX_ARRAY);
                if(x->has_color)
                        glEnableClientState(GL_COLOR_ARRAY);
                if(x->has_normal)
                        glEnableClientState(GL_NORMAL_ARRAY);
                if(x->has_texcoord)
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

                glNewList(x->legacy, GL_COMPILE);
                if(size > 0) {
                        if(x->has_color)
                                glColorPointer(4, GL_UNSIGNED_BYTE, 0, color);

                        switch(type) {
                                case GLX_DISPLAYLIST_NORMAL: glVertexPointer(3, GL_SHORT, 0, vertex); break;
                                case GLX_DISPLAYLIST_POINTS:
                                case GLX_DISPLAYLIST_ENHANCED: glVertexPointer(3, GL_FLOAT, 0, vertex); break;
                        }

                        if(x->has_normal)
                                glNormalPointer(GL_BYTE, 0, normal);
                        if(x->has_texcoord)
                                glTexCoordPointer(2, GL_FLOAT, 0, texcoord);
                        glDrawArrays((type == GLX_DISPLAYLIST_POINTS) ? GL_POINTS : GL_QUADS, 0, x->size);
                }
                glEndList();

                glDisableClientState(GL_VERTEX_ARRAY);
                if(x->has_color)
                        glDisableClientState(GL_COLOR_ARRAY);
                if(x->has_normal)
                        glDisableClientState(GL_NORMAL_ARRAY);
                if(x->has_texcoord)
                        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
#endif
                size_t len_vertex = ((type == GLX_DISPLAYLIST_NORMAL) ? sizeof(GLshort) : sizeof(GLfloat)) * 3;
                size_t len_color = x->has_color ? (sizeof(GLubyte) * 4) : 0;
                size_t len_normal = x->has_normal ? (sizeof(GLbyte) * 3) : 0;
                size_t len_texcoord = x->has_texcoord ? (sizeof(float) * 2) : 0;

                glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                if(grow_buffer) {
                        glBufferData(GL_ARRAY_BUFFER, x->size * (len_vertex + len_color + len_normal + len_texcoord), NULL,
                                                GL_STATIC_DRAW);
                }

                glBufferSubData(GL_ARRAY_BUFFER, 0, x->size * len_vertex, vertex);

                size_t offset = x->size * len_vertex;

                if(x->has_color) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_color, color);
                        offset += x->size * len_color;
                }

                if(x->has_normal) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_normal, normal);
                        offset += x->size * len_normal;
                }

                if(x->has_texcoord) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_texcoord, texcoord);
                }

                glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifndef OPENGL_ES
        }
#endif
}

void glx_displaylist_draw(struct glx_displaylist* x, int type) {
#ifndef OPENGL_ES
        if(!glx_version || settings.force_displaylist) {
                if(x->has_texcoord) {
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                        glCallList(x->legacy);
                        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                } else {
                        glCallList(x->legacy);
                }
        } else {
#endif
                size_t len_vertex = ((type == GLX_DISPLAYLIST_NORMAL) ? sizeof(GLshort) : sizeof(GLfloat)) * 3;
                size_t len_color = x->has_color ? (sizeof(GLubyte) * 4) : 0;
                size_t len_normal = x->has_normal ? (sizeof(GLbyte) * 3) : 0;
                size_t len_texcoord = x->has_texcoord ? (sizeof(float) * 2) : 0;

#if defined(OPENGL_ES)
                if(gles_version >= 2 && default_shader) {
                        /* ES 2.0 path: use vertex attributes */
                        glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                        /* a_Position (location 0) — tightly packed, offset 0 */
                        glVertexAttribPointer(0, 3,
                                (type == GLX_DISPLAYLIST_NORMAL) ? GL_SHORT : GL_FLOAT,
                                GL_FALSE, 0, (void*)0);
                        glEnableVertexAttribArray(0);

                        /* a_Color (location 1) — tightly packed after vertex block */
                        if(x->has_color) {
                                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0,
                                        (void*)(x->size * len_vertex));
                                glEnableVertexAttribArray(1);
                                /* Only set uniforms if default_shader is active */
                                GLint cur_prog;
                                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                                if(cur_prog == (GLint)default_shader)
                                        glUniform1f(loc_u_HasVertexColor, 1.0F);
                        } else {
                                GLint cur_prog;
                                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                                if(cur_prog == (GLint)default_shader)
                                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        }

                        /* a_TexCoord (location 2) — tightly packed after vertex+color+normal */
                        if(x->has_texcoord) {
                                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0,
                                        (void*)(x->size * (len_vertex + len_color + len_normal)));
                                glEnableVertexAttribArray(2);
                                GLint cur_prog;
                                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                                if(cur_prog == (GLint)default_shader) {
                                        glUniform1f(loc_u_TextureEnabled, 1.0F);
                                        /* Reset texcoord scale — font rendering sets 1/8192 */
                                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                                }
                        } else {
                                GLint cur_prog;
                                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                                if(cur_prog == (GLint)default_shader) {
                                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                                }
                        }

                        /* a_Normal (location 3) — for KV6 shader lighting */
                        if(x->has_normal) {
                                glVertexAttribPointer(3, 3, GL_BYTE, GL_FALSE, 0,
                                        (void*)(x->size * (len_vertex + len_color)));
                                glEnableVertexAttribArray(3);
                        }

                        GLenum draw_mode = (type == GLX_DISPLAYLIST_POINTS) ? GL_POINTS : GL_TRIANGLES;
                        glDrawArrays(draw_mode, 0, x->size);

                        glDisableVertexAttribArray(0);
                        if(x->has_color) glDisableVertexAttribArray(1);
                        if(x->has_texcoord) glDisableVertexAttribArray(2);
                        if(x->has_normal) glDisableVertexAttribArray(3);
                        glBindBuffer(GL_ARRAY_BUFFER, 0);
                } else
#endif
                {
                        /* GLES 1.1 / desktop GL 2.0+ fallback: use client-state arrays */
                        glEnableClientState(GL_VERTEX_ARRAY);
                        glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                        switch(type) {
                                case GLX_DISPLAYLIST_NORMAL: glVertexPointer(3, GL_SHORT, 0, NULL); break;
                                case GLX_DISPLAYLIST_POINTS:
                                case GLX_DISPLAYLIST_ENHANCED: glVertexPointer(3, GL_FLOAT, 0, NULL); break;
                        }

                        size_t offset = x->size * len_vertex;

                        if(x->has_color) {
                                glEnableClientState(GL_COLOR_ARRAY);
                                glColorPointer(4, GL_UNSIGNED_BYTE, 0, (const void*)offset);
                                offset += x->size * len_color;
                        }

                        if(x->has_normal) {
                                glEnableClientState(GL_NORMAL_ARRAY);
                                glNormalPointer(GL_BYTE, 0, (const void*)offset);
                                offset += x->size * len_normal;
                        }

                        if(x->has_texcoord) {
                                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                                glTexCoordPointer(2, GL_FLOAT, 0, (const void*)offset);
                        }

                        glBindBuffer(GL_ARRAY_BUFFER, 0);

                        if(type == GLX_DISPLAYLIST_POINTS) {
                                glDrawArrays(GL_POINTS, 0, x->size);
                        } else {
#ifdef OPENGL_ES
                                glDrawArrays(GL_TRIANGLES, 0, x->size);
#else
                                glDrawArrays(GL_QUADS, 0, x->size);
#endif
                        }

                        if(x->has_texcoord)
                                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                        if(x->has_normal)
                                glDisableClientState(GL_NORMAL_ARRAY);
                        if(x->has_color)
                                glDisableClientState(GL_COLOR_ARRAY);
                        glDisableClientState(GL_VERTEX_ARRAY);
                }
#ifndef OPENGL_ES
        }
#endif
}

/* ── ES 2.0 helper draw functions ────────────────────────────────────────── */

#ifdef OPENGL_ES
void glx_use_default_shader(void) {
        if(default_shader)
                glUseProgram(default_shader);
}

int glx_default_shader_program(void) {
        return default_shader;
}

static void glx_ensure_quad_vbo(void) {
        if(quad_vbo)
                return;
        /* Clip-space fullscreen quad with texcoords */
        static const float quad_verts[] = {
                -1.0f, -1.0f,  0.0f,  0.0f,
                 1.0f, -1.0f,  1.0f,  0.0f,
                 1.0f,  1.0f,  1.0f,  1.0f,
                -1.0f, -1.0f,  0.0f,  0.0f,
                 1.0f,  1.0f,  1.0f,  1.0f,
                -1.0f,  1.0f,  0.0f,  1.0f,
        };
        glGenBuffers(1, &quad_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
}

static void glx_ensure_line_quad_vbo(void) {
        if(line_quad_vbo)
                return;
        glGenBuffers(1, &line_quad_vbo);
}

void glx_draw_screen_quad(void) {
#if defined(OPENGL_ES)
        if(!default_shader)
                return;
        glx_ensure_quad_vbo();
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(2);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}

#endif /* OPENGL_ES */

void glx_draw_line_2d(float x1, float y1, float x2, float y2) {
#if defined(OPENGL_ES)
        if(default_shader) {
                float verts[] = {x1, y1, 0.0f, x2, y2, 0.0f};
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
                GLint cur_prog;
                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform4fv(loc_u_Color, 1, gles_current_color);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                }
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_LINES, 0, 2);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
        {
                float verts[] = {x1, y1, x2, y2};
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_NORMAL_ARRAY);
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 0, verts);
                glDrawArrays(GL_LINES, 0, 2);
                glDisableClientState(GL_VERTEX_ARRAY);
                glEnable(GL_TEXTURE_2D);
        }
#else
        glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
        glEnd();
#endif
}

void glx_draw_quad_2d(float x, float y, float w, float h) {
#if defined(OPENGL_ES)
        if(default_shader) {
                float verts[] = {
                        x, y, 0.0f,     x, y - h, 0.0f,   x + w, y - h, 0.0f,
                        x, y, 0.0f,     x + w, y - h, 0.0f, x + w, y, 0.0f,
                };
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
                GLint cur_prog;
                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform4fv(loc_u_Color, 1, gles_current_color);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                }
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
        {
                float verts[] = {
                        x, y,        x, y - h,     x + w, y - h,
                        x, y,        x + w, y - h, x + w, y,
                };
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_NORMAL_ARRAY);
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 0, verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableClientState(GL_VERTEX_ARRAY);
                glEnable(GL_TEXTURE_2D);
        }
#else
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y - h);
        glVertex2f(x, y - h);
        glEnd();
#endif
}

/* Vertical gradient quad — needed because GLES has no glBegin/glVertex2f
 * (immediate mode never existed in any GLES spec, ES 1.1 included), so a
 * plain glBegin(GL_QUADS) two-color quad has to become a 4-vertex
 * GL_TRIANGLE_STRIP with per-vertex color everywhere but classic desktop GL. */
void glx_draw_gradient_quad_2d(float x, float y, float w, float h, float r1, float g1, float b1, float r2, float g2,
                                                                float b2) {
#if defined(OPENGL_ES)
        /* Strip order top-left, bottom-left, top-right, bottom-right. */
        float pos[] = {
                x,     y,     0.0f,
                x,     y - h, 0.0f,
                x + w, y,     0.0f,
                x + w, y - h, 0.0f,
        };
        unsigned char col[] = {
                (unsigned char)(r1 * 255.0f), (unsigned char)(g1 * 255.0f), (unsigned char)(b1 * 255.0f), 255,
                (unsigned char)(r2 * 255.0f), (unsigned char)(g2 * 255.0f), (unsigned char)(b2 * 255.0f), 255,
                (unsigned char)(r1 * 255.0f), (unsigned char)(g1 * 255.0f), (unsigned char)(b1 * 255.0f), 255,
                (unsigned char)(r2 * 255.0f), (unsigned char)(g2 * 255.0f), (unsigned char)(b2 * 255.0f), 255,
        };

        if(gles_version >= 2 && default_shader) {
                unsigned char buf[4 * (3 * sizeof(float) + 4)];
                size_t voff = 4 * 3 * sizeof(float);
                memcpy(buf, pos, voff);
                memcpy(buf + voff, col, sizeof(col));

                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_STREAM_DRAW);

                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (void*)voff);
                glEnableVertexAttribArray(1);

                GLint cur_prog;
                glGetIntegerv(GL_CURRENT_PROGRAM, &cur_prog);
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 1.0F);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                glDisableVertexAttribArray(0);
                glDisableVertexAttribArray(1);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                if(cur_prog == (GLint)default_shader)
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                return;
        }

        /* ES 1.1 fixed-function fallback */
        float posf[] = {x, y, x, y - h, x + w, y, x + w, y - h};
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, posf);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glEnable(GL_TEXTURE_2D);
#else
        glBegin(GL_QUADS);
        glColor3f(r1, g1, b1);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glColor3f(r2, g2, b2);
        glVertex2f(x + w, y - h);
        glVertex2f(x, y - h);
        glEnd();
#endif
}

/* ── Spherical fog ───────────────────────────────────────────────────────── */

void glx_enable_sphericalfog() {
        /* Effective fog color (darkened 10% when filmic tone mapping is on). */
        float fc[3];
        fog_color_render(fc);
#ifndef OPENGL_ES
        if(!settings.smooth_fog) {
                glActiveTexture(GL_TEXTURE1);
                glEnable(GL_TEXTURE_2D);
                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, (float[]) {fc[0], fc[1], fc[2], 1.0F});
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
                glBindTexture(GL_TEXTURE_2D, texture_gradient.texture_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                glTexGenfv(GL_T, GL_EYE_PLANE,
                                   (float[]) {1.0F / settings.render_distance / 2.0F, 0.0F, 0.0F,
                                                          -camera_x / settings.render_distance / 2.0F + 0.5F});
                glTexGenfv(GL_S, GL_EYE_PLANE,
                                   (float[]) {0.0F, 0.0F, 1.0F / settings.render_distance / 2.0F,
                                                          -camera_z / settings.render_distance / 2.0F + 0.5F});
                glEnable(GL_TEXTURE_GEN_T);
                glEnable(GL_TEXTURE_GEN_S);
                glActiveTexture(GL_TEXTURE0);
        } else {
                matrix_push(matrix_model);
                matrix_identity(matrix_model);
                matrix_upload();
                matrix_pop(matrix_model);

                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT1);
                glEnable(GL_COLOR_MATERIAL);
                glColorMaterial(GL_FRONT, GL_DIFFUSE);
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[]) {fc[0], fc[1], fc[2], 1.0F});

                glLightfv(GL_LIGHT1, GL_POSITION,
                                  (float[]) {camera_x, (settings.render_distance * map_size_y) / 16.0F, camera_z, 1.0F});
                glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, (float[]) {0.0F, -1.0F, 0.0F});
                glLightfv(GL_LIGHT1, GL_DIFFUSE, (float[]) {1.0F, 1.0F, 1.0F, 1.0F});
                glLightfv(GL_LIGHT1, GL_AMBIENT, (float[]) {-fc[0], -fc[1], -fc[2], 1.0F});
                glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, tan(16.0F / map_size_y) / PI * 180.0F);
                glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 128.0F);
                glNormal3f(0.0F, 1.0F, 0.0F);
        }
#elif defined(OPENGL_ES)
        if(gles_version < 2) {
                /* ES 1.1: use fixed-function fog/lighting */
                matrix_push(matrix_model);
                matrix_identity(matrix_model);
                matrix_upload();
                matrix_pop(matrix_model);

                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT1);
                glEnable(GL_COLOR_MATERIAL);
                float amb[4] = {0.0F, 0.0F, 0.0F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);

                float lpos[4] = {camera_x, (settings.render_distance * map_size_y) / 16.0F, camera_z, 1.0F};
                glLightfv(GL_LIGHT1, GL_POSITION, lpos);
                float dir[3] = {0.0F, -1.0F, 0.0F};
                glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, dir);
                float dif[4] = {0.0F, 0.0F, 0.0F, 1.0F};
                glLightfv(GL_LIGHT1, GL_DIFFUSE, dif);
                float amb2[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                glLightfv(GL_LIGHT1, GL_AMBIENT, amb2);
                glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, tan(16.0F / map_size_y) / PI * 180.0F);
                glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 128.0F);
                glNormal3f(0.0F, 1.0F, 0.0F);
                glEnable(GL_FOG);
                glFogf(GL_FOG_MODE, GL_LINEAR);
                glFogf(GL_FOG_START, 0.0F);
                glFogf(GL_FOG_END, settings.render_distance);
                glFogfv(GL_FOG_COLOR, (float[]) {fc[0], fc[1], fc[2], 1.0F});
        }
        if(gles_version >= 2 && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 1.0F / settings.render_distance);
                glUniform3f(loc_u_FogColor, fc[0], fc[1], fc[2]);
                glUniform3f(loc_u_Camera, camera_x, camera_y, camera_z);
        }
#endif
        glx_fog = 1;
}

void glx_disable_sphericalfog() {
#ifndef OPENGL_ES
        if(!settings.smooth_fog) {
                glActiveTexture(GL_TEXTURE1);
                glDisable(GL_TEXTURE_GEN_T);
                glDisable(GL_TEXTURE_GEN_S);
                glBindTexture(GL_TEXTURE_2D, 0);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glDisable(GL_TEXTURE_2D);
                glActiveTexture(GL_TEXTURE0);
        } else {
                glDisable(GL_COLOR_MATERIAL);
                glDisable(GL_LIGHT1);
                glDisable(GL_LIGHTING);
                float a[4] = {0.2F, 0.2F, 0.2F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a);
        }
#elif defined(OPENGL_ES)
        if(gles_version < 2) {
                glDisable(GL_FOG);
                glDisable(GL_COLOR_MATERIAL);
                glDisable(GL_LIGHT1);
                glDisable(GL_LIGHTING);
                float a[4] = {0.2F, 0.2F, 0.2F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a);
        }
        if(gles_version >= 2 && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 0.0F);
        }
#endif
        glx_fog = 0;
}

