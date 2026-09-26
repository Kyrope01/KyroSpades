
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
#include <assert.h>
#include <math.h>

#include "common.h"
#include "config.h"
#include "glx.h"
#include "tesselator.h"

static size_t vertex_type_size(enum tesselator_vertex_type type) {
        switch(type) {
                case VERTEX_INT: return sizeof(int16_t);
                case VERTEX_FLOAT: return sizeof(float);
                default: return 0;
        }
}

void tesselator_create(struct tesselator* t, enum tesselator_vertex_type type, int has_normal, int has_texcoord) {
        t->quad_count = 0;
        t->quad_space = 128;
        t->vertices = NULL;
        t->colors = NULL;
        t->texcoords = NULL;
        t->vertex_type = type;
        t->has_normal = has_normal;
        t->has_texcoord = has_texcoord;
        t->normal_explicit = 0;
        t->normal[0] = t->normal[1] = t->normal[2] = 0;

#ifdef TESSELATE_QUADS
        t->vertices = malloc(t->quad_space * vertex_type_size(t->vertex_type) * 3 * 4);
        CHECK_ALLOCATION_ERROR(t->vertices)
        t->colors = malloc(t->quad_space * sizeof(uint32_t) * 4);
        CHECK_ALLOCATION_ERROR(t->colors)

        if(t->has_texcoord) {
                t->texcoords = malloc(t->quad_space * sizeof(float) * 2 * 4);
                CHECK_ALLOCATION_ERROR(t->texcoords)
        }

        if(t->has_normal) {
                t->normals = malloc(t->quad_space * sizeof(int8_t) * 3 * 4);
                CHECK_ALLOCATION_ERROR(t->normals)
        } else {
                t->normals = NULL;
        }
#endif

#ifdef TESSELATE_TRIANGLES
        t->vertices = malloc(t->quad_space * vertex_type_size(t->vertex_type) * 3 * 6);
        CHECK_ALLOCATION_ERROR(t->vertices)
        t->colors = malloc(t->quad_space * sizeof(uint32_t) * 6);
        CHECK_ALLOCATION_ERROR(t->colors)

        if(t->has_texcoord) {
                t->texcoords = malloc(t->quad_space * sizeof(float) * 2 * 6);
                CHECK_ALLOCATION_ERROR(t->texcoords)
        }

        if(t->has_normal) {
                t->normals = malloc(t->quad_space * sizeof(int8_t) * 3 * 6);
                CHECK_ALLOCATION_ERROR(t->normals)
        } else {
                t->normals = NULL;
        }
#endif
}

void tesselator_clear(struct tesselator* t) {
        t->quad_count = 0;
}

void tesselator_free(struct tesselator* t) {
        if(t->vertices) {
                free(t->vertices);
                t->vertices = NULL;
        }

        if(t->colors) {
                free(t->colors);
                t->colors = NULL;
        }

        if(t->texcoords) {
                free(t->texcoords);
                t->texcoords = NULL;
        }

        if(t->normals) {
                free(t->normals);
                t->normals = NULL;
        }
}

void tesselator_draw(struct tesselator* t, int with_color) {
        if(t->quad_count == 0)
                return;
#if defined(OPENGL_CORE)
        {
                static GLuint stream_vbo = 0;
                size_t count = (size_t)t->quad_count * 6;
                size_t vertex_stride = t->vertex_type == VERTEX_INT ? sizeof(GLshort) * 3 : sizeof(GLfloat) * 3;
                size_t vertex_bytes = count * vertex_stride;
                size_t color_bytes = with_color ? count * sizeof(uint32_t) : 0;
                size_t normal_bytes = t->has_normal ? count * sizeof(int8_t) * 3 : 0;
                size_t texcoord_bytes = t->texcoords ? count * sizeof(float) * 2 : 0;
                size_t total = vertex_bytes + color_bytes + normal_bytes + texcoord_bytes;
                size_t offset = 0;

                if(!stream_vbo)
                        glGenBuffers(1, &stream_vbo);
                glBindBuffer(GL_ARRAY_BUFFER, stream_vbo);
                glBufferData(GL_ARRAY_BUFFER, total, NULL, GL_STREAM_DRAW);
                glBufferSubData(GL_ARRAY_BUFFER, offset, vertex_bytes, t->vertices);
                glVertexAttribPointer(0, 3, t->vertex_type == VERTEX_INT ? GL_SHORT : GL_FLOAT,
                                      GL_FALSE, 0, (void*)offset);
                glEnableVertexAttribArray(0);
                offset += vertex_bytes;

                if(with_color) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, color_bytes, t->colors);
                        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (void*)offset);
                        glEnableVertexAttribArray(1);
                        offset += color_bytes;
                }
                if(t->has_normal) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, normal_bytes, t->normals);
                        glVertexAttribPointer(3, 3, GL_BYTE, GL_TRUE, 0, (void*)offset);
                        glEnableVertexAttribArray(3);
                        offset += normal_bytes;
                }
                if(t->texcoords) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, texcoord_bytes, t->texcoords);
                        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, (void*)offset);
                        glEnableVertexAttribArray(2);
                }

                glx_use_default_shader();
                glx_default_shader_set_draw_state(with_color, t->texcoords != NULL, t->has_normal);
                glDrawArrays(GL_TRIANGLES, 0, (GLsizei)count);

                glDisableVertexAttribArray(0);
                if(with_color) glDisableVertexAttribArray(1);
                if(t->has_normal) glDisableVertexAttribArray(3);
                if(t->texcoords) glDisableVertexAttribArray(2);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
#elif defined(OPENGL_ES)
        if(gles_version >= 2) {
                glx_use_default_shader();
                glx_default_shader_set_draw_state(with_color, t->texcoords != NULL,
                                                   t->has_normal && settings.dynamic_lights);

                switch(t->vertex_type) {
                        case VERTEX_INT:
                                glVertexAttribPointer(0, 3, GL_SHORT, GL_FALSE, 0, t->vertices);
                                break;
                        case VERTEX_FLOAT:
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, t->vertices);
                                break;
                }
                glEnableVertexAttribArray(0);

                if(with_color) {
                        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, t->colors);
                        glEnableVertexAttribArray(1);
                }

                if(t->has_normal) {
                        glVertexAttribPointer(3, 3, GL_BYTE, GL_TRUE, 0, t->normals);
                        glEnableVertexAttribArray(3);
                }

                if(t->texcoords) {
                        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, t->texcoords);
                        glEnableVertexAttribArray(2);
                }

                glDrawArrays(GL_TRIANGLES, 0, t->quad_count * 6);

                glDisableVertexAttribArray(0);
                if(with_color) glDisableVertexAttribArray(1);
                if(t->has_normal) glDisableVertexAttribArray(3);
                if(t->texcoords) glDisableVertexAttribArray(2);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glEnableClientState(GL_VERTEX_ARRAY);

        if(t->has_normal) {
                glEnableClientState(GL_NORMAL_ARRAY);
                glNormalPointer(GL_BYTE, 0, t->normals);
        }

        switch(t->vertex_type) {
                case VERTEX_INT: glVertexPointer(3, GL_SHORT, 0, t->vertices); break;
                case VERTEX_FLOAT: glVertexPointer(3, GL_FLOAT, 0, t->vertices); break;
        }

        if(with_color) {
                glEnableClientState(GL_COLOR_ARRAY);
                glColorPointer(4, GL_UNSIGNED_BYTE, 0, t->colors);
        }

        if(t->texcoords) {
                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                glTexCoordPointer(2, GL_FLOAT, 0, t->texcoords);
        }

#ifdef TESSELATE_QUADS
        glDrawArrays(GL_QUADS, 0, t->quad_count * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        glDrawArrays(GL_TRIANGLES, 0, t->quad_count * 6);
#endif

        if(t->texcoords)
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        if(with_color)
                glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        if(t->has_normal)
                glDisableClientState(GL_NORMAL_ARRAY);
#endif
}

void tesselator_glx(struct tesselator* t, struct glx_displaylist* x) {
#ifdef TESSELATE_QUADS
        switch(t->vertex_type) {
                case VERTEX_INT:
                        glx_displaylist_update(x, t->quad_count * 4, GLX_DISPLAYLIST_NORMAL, t->colors, t->vertices, t->normals,
                                                                   t->texcoords);
                        break;
                case VERTEX_FLOAT:
                        glx_displaylist_update(x, t->quad_count * 4, GLX_DISPLAYLIST_ENHANCED, t->colors, t->vertices, t->normals,
                                                                   t->texcoords);
                        break;
        }
#endif

#ifdef TESSELATE_TRIANGLES
        switch(t->vertex_type) {
                case VERTEX_INT:
                        glx_displaylist_update(x, t->quad_count * 6, GLX_DISPLAYLIST_NORMAL, t->colors, t->vertices, t->normals,
                                                                   t->texcoords);
                        break;
                case VERTEX_FLOAT:
                        glx_displaylist_update(x, t->quad_count * 6, GLX_DISPLAYLIST_ENHANCED, t->colors, t->vertices, t->normals,
                                                                   t->texcoords);
                        break;
        }
#endif
}

void tesselator_set_color(struct tesselator* t, uint32_t color) {
        t->color = color;
}

void tesselator_set_normal(struct tesselator* t, int8_t x, int8_t y, int8_t z) {
        t->normal[0] = x;
        t->normal[1] = y;
        t->normal[2] = z;
        t->normal_explicit = 1;
}

static void tesselator_check_space(struct tesselator* t) {
        if(t->quad_count >= t->quad_space) {
                t->quad_space *= 2;

#ifdef TESSELATE_QUADS
                t->vertices = realloc(t->vertices, t->quad_space * vertex_type_size(t->vertex_type) * 3 * 4);
                CHECK_ALLOCATION_ERROR(t->vertices)
                t->colors = realloc(t->colors, t->quad_space * sizeof(uint32_t) * 4);
                CHECK_ALLOCATION_ERROR(t->colors)

                if(t->has_texcoord) {
                        t->texcoords = realloc(t->texcoords, t->quad_space * sizeof(float) * 2 * 4);
                        CHECK_ALLOCATION_ERROR(t->texcoords)
                }

                if(t->has_normal) {
                        t->normals = realloc(t->normals, t->quad_space * sizeof(int8_t) * 3 * 4);
                        CHECK_ALLOCATION_ERROR(t->normals)
                }
#endif

#ifdef TESSELATE_TRIANGLES
                t->vertices = realloc(t->vertices, t->quad_space * vertex_type_size(t->vertex_type) * 3 * 6);
                CHECK_ALLOCATION_ERROR(t->vertices)
                t->colors = realloc(t->colors, t->quad_space * sizeof(uint32_t) * 6);
                CHECK_ALLOCATION_ERROR(t->colors)

                if(t->has_texcoord) {
                        t->texcoords = realloc(t->texcoords, t->quad_space * sizeof(float) * 2 * 6);
                        CHECK_ALLOCATION_ERROR(t->texcoords)
                }

                if(t->has_normal) {
                        t->normals = realloc(t->normals, t->quad_space * sizeof(int8_t) * 3 * 6);
                        CHECK_ALLOCATION_ERROR(t->normals)
                }
#endif
        }
}

static void tesselator_emit_color(struct tesselator* t, uint32_t* colors) {
#ifdef TESSELATE_QUADS
        memcpy(t->colors + t->quad_count * 4, colors, sizeof(uint32_t) * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        memcpy(t->colors + t->quad_count * 6, colors, sizeof(uint32_t) * 3);
        t->colors[t->quad_count * 6 + 3] = colors[0];
        memcpy(t->colors + t->quad_count * 6 + 4, colors + 2, sizeof(uint32_t) * 2);
#endif
}

static void tesselator_flat_normal(float ax, float ay, float az, float bx, float by, float bz,
                                   float cx, float cy, float cz, int8_t normals[12]) {
        float ux = bx - ax;
        float uy = by - ay;
        float uz = bz - az;
        float vx = cx - ax;
        float vy = cy - ay;
        float vz = cz - az;
        float nx = uy * vz - uz * vy;
        float ny = uz * vx - ux * vz;
        float nz = ux * vy - uy * vx;
        float length = sqrtf(nx * nx + ny * ny + nz * nz);
        int8_t ix = 0, iy = 0, iz = 0;
        if(length > 0.000001F) {
                ix = (int8_t)(nx / length * 127.0F);
                iy = (int8_t)(ny / length * 127.0F);
                iz = (int8_t)(nz / length * 127.0F);
        }
        for(int i = 0; i < 4; i++) {
                normals[i * 3 + 0] = ix;
                normals[i * 3 + 1] = iy;
                normals[i * 3 + 2] = iz;
        }
}

static int8_t* tesselator_flat_normal_i(const int16_t* coords, int8_t normals[12]) {
        tesselator_flat_normal(coords[0], coords[1], coords[2], coords[3], coords[4], coords[5],
                               coords[6], coords[7], coords[8], normals);
        return normals;
}

static int8_t* tesselator_flat_normal_f(const float* coords, int8_t normals[12]) {
        tesselator_flat_normal(coords[0], coords[1], coords[2], coords[3], coords[4], coords[5],
                               coords[6], coords[7], coords[8], normals);
        return normals;
}

static void tesselator_emit_normals(struct tesselator* t, int8_t* normals) {
        if(t->has_normal) {
#ifdef TESSELATE_QUADS
                memcpy(t->normals + t->quad_count * 3 * 4, normals, sizeof(int8_t) * 3 * 4);
#endif

#ifdef TESSELATE_TRIANGLES
                memcpy(t->normals + t->quad_count * 3 * 6 + 3 * 0, normals, sizeof(int8_t) * 3 * 3);
                memcpy(t->normals + t->quad_count * 3 * 6 + 3 * 3, normals + 3 * 0, sizeof(int8_t) * 3);
                memcpy(t->normals + t->quad_count * 3 * 6 + 3 * 4, normals + 3 * 2, sizeof(int8_t) * 3 * 2);
#endif
        }
}

void tesselator_addi(struct tesselator* t, int16_t* coords, uint32_t* colors, int8_t* normals) {
        assert(t->vertex_type == VERTEX_INT);

        int8_t generated_normals[12];
        if(t->has_normal && !normals)
                normals = tesselator_flat_normal_i(coords, generated_normals);

        tesselator_check_space(t);
        tesselator_emit_color(t, colors);
        tesselator_emit_normals(t, normals);

#ifdef TESSELATE_QUADS
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 4, coords, sizeof(int16_t) * 3 * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 0, coords, sizeof(int16_t) * 3 * 3);
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 3, coords + 3 * 0, sizeof(int16_t) * 3);
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 4, coords + 3 * 2, sizeof(int16_t) * 3 * 2);
#endif

        t->quad_count++;
}

void tesselator_addf(struct tesselator* t, float* coords, uint32_t* colors, int8_t* normals) {
        assert(t->vertex_type == VERTEX_FLOAT);

        int8_t generated_normals[12];
        if(t->has_normal && !normals)
                normals = tesselator_flat_normal_f(coords, generated_normals);

        tesselator_check_space(t);
        tesselator_emit_color(t, colors);
        tesselator_emit_normals(t, normals);

#ifdef TESSELATE_QUADS
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 4, coords, sizeof(float) * 3 * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 0, coords, sizeof(float) * 3 * 3);
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 3, coords + 3 * 0, sizeof(float) * 3);
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 4, coords + 3 * 2, sizeof(float) * 3 * 2);
#endif

        t->quad_count++;
}

void tesselator_addi_simple(struct tesselator* t, int16_t* coords) {
        tesselator_addi(t, coords, (uint32_t[]) {t->color, t->color, t->color, t->color},
                                        t->has_normal && t->normal_explicit
                                                ? (int8_t[]) {t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1],
                                                              t->normal[2], t->normal[0], t->normal[1], t->normal[2], t->normal[0],
                                                              t->normal[1], t->normal[2]}
                                                : NULL);
}

void tesselator_addi_uv(struct tesselator* t, int16_t* coords, float* uvs) {
        assert(t->vertex_type == VERTEX_INT);

        int8_t generated_normals[12];
        int8_t explicit_normals[12] = {
                t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1], t->normal[2],
                t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1], t->normal[2]
        };
        int8_t* normals = NULL;
        if(t->has_normal)
                normals = t->normal_explicit ? explicit_normals : tesselator_flat_normal_i(coords, generated_normals);

        tesselator_check_space(t);
        tesselator_emit_color(t, (uint32_t[]) {t->color, t->color, t->color, t->color});
        tesselator_emit_normals(t, normals);

#ifdef TESSELATE_QUADS
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 4, coords, sizeof(int16_t) * 3 * 4);
        memcpy(t->texcoords + t->quad_count * 2 * 4, uvs, sizeof(float) * 2 * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 0, coords, sizeof(int16_t) * 3 * 3);
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 3, coords + 3 * 0, sizeof(int16_t) * 3);
        memcpy(((int16_t*)t->vertices) + t->quad_count * 3 * 6 + 3 * 4, coords + 3 * 2, sizeof(int16_t) * 3 * 2);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 0, uvs, sizeof(float) * 2 * 3);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 3, uvs + 2 * 0, sizeof(float) * 2);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 4, uvs + 2 * 2, sizeof(float) * 2 * 2);
#endif

        t->quad_count++;
}

void tesselator_addf_simple(struct tesselator* t, float* coords) {
        tesselator_addf(t, coords, (uint32_t[]) {t->color, t->color, t->color, t->color},
                                        t->has_normal && t->normal_explicit
                                                ? (int8_t[]) {t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1],
                                                              t->normal[2], t->normal[0], t->normal[1], t->normal[2], t->normal[0],
                                                              t->normal[1], t->normal[2]}
                                                : NULL);
}

void tesselator_addf_uv(struct tesselator* t, float* coords, float* uvs) {
        assert(t->vertex_type == VERTEX_FLOAT);

        int8_t generated_normals[12];
        int8_t explicit_normals[12] = {
                t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1], t->normal[2],
                t->normal[0], t->normal[1], t->normal[2], t->normal[0], t->normal[1], t->normal[2]
        };
        int8_t* normals = NULL;
        if(t->has_normal)
                normals = t->normal_explicit ? explicit_normals : tesselator_flat_normal_f(coords, generated_normals);

        tesselator_check_space(t);
        tesselator_emit_color(t, (uint32_t[]) {t->color, t->color, t->color, t->color});
        tesselator_emit_normals(t, normals);

#ifdef TESSELATE_QUADS
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 4, coords, sizeof(float) * 3 * 4);
        memcpy(t->texcoords + t->quad_count * 2 * 4, uvs, sizeof(float) * 2 * 4);
#endif

#ifdef TESSELATE_TRIANGLES
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 0, coords, sizeof(float) * 3 * 3);
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 3, coords + 3 * 0, sizeof(float) * 3);
        memcpy(((float*)t->vertices) + t->quad_count * 3 * 6 + 3 * 4, coords + 3 * 2, sizeof(float) * 3 * 2);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 0, uvs, sizeof(float) * 2 * 3);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 3, uvs + 2 * 0, sizeof(float) * 2);
        memcpy(t->texcoords + t->quad_count * 2 * 6 + 2 * 4, uvs + 2 * 2, sizeof(float) * 2 * 2);
#endif

        t->quad_count++;
}

void tesselator_addi_cube_face_adv(struct tesselator* t, enum tesselator_cube_face face, int16_t x, int16_t y,
                                                                   int16_t z, int16_t sx, int16_t sy, int16_t sz) {
        switch(face) {
                case CUBE_FACE_Z_N:
                        tesselator_addi_simple(t, (int16_t[]) {x, y, z, x, y + sy, z, x + sx, y + sy, z, x + sx, y, z});
                        break;
                case CUBE_FACE_Z_P:
                        tesselator_addi_simple(
                                t, (int16_t[]) {x, y, z + sz, x + sx, y, z + sz, x + sx, y + sy, z + sz, x, y + sy, z + sz});
                        break;
                case CUBE_FACE_X_N:
                        tesselator_addi_simple(t, (int16_t[]) {x, y, z, x, y, z + sz, x, y + sy, z + sz, x, y + sy, z});
                        break;
                case CUBE_FACE_X_P:
                        tesselator_addi_simple(
                                t, (int16_t[]) {x + sx, y, z, x + sx, y + sy, z, x + sx, y + sy, z + sz, x + sx, y, z + sz});
                        break;
                case CUBE_FACE_Y_P:
                        tesselator_addi_simple(
                                t, (int16_t[]) {x, y + sy, z, x, y + sy, z + sz, x + sx, y + sy, z + sz, x + sx, y + sy, z});
                        break;
                case CUBE_FACE_Y_N:
                        tesselator_addi_simple(t, (int16_t[]) {x, y, z, x + sx, y, z, x + sx, y, z + sz, x, y, z + sz});
                        break;
        }
}

void tesselator_addi_cube_face(struct tesselator* t, enum tesselator_cube_face face, int16_t x, int16_t y, int16_t z) {
        tesselator_addi_cube_face_adv(t, face, x, y, z, 1, 1, 1);
}

void tesselator_addf_cube_face(struct tesselator* t, enum tesselator_cube_face face, float x, float y, float z,
                                                           float sz) {
        switch(face) {
                case CUBE_FACE_Z_N:
                        tesselator_addf_simple(t, (float[]) {x, y, z, x, y + sz, z, x + sz, y + sz, z, x + sz, y, z});
                        break;
                case CUBE_FACE_Z_P:
                        tesselator_addf_simple(
                                t, (float[]) {x, y, z + sz, x + sz, y, z + sz, x + sz, y + sz, z + sz, x, y + sz, z + sz});
                        break;
                case CUBE_FACE_X_N:
                        tesselator_addf_simple(t, (float[]) {x, y, z, x, y, z + sz, x, y + sz, z + sz, x, y + sz, z});
                        break;
                case CUBE_FACE_X_P:
                        tesselator_addf_simple(
                                t, (float[]) {x + sz, y, z, x + sz, y + sz, z, x + sz, y + sz, z + sz, x + sz, y, z + sz});
                        break;
                case CUBE_FACE_Y_P:
                        tesselator_addf_simple(
                                t, (float[]) {x, y + sz, z, x, y + sz, z + sz, x + sz, y + sz, z + sz, x + sz, y + sz, z});
                        break;
                case CUBE_FACE_Y_N:
                        tesselator_addf_simple(t, (float[]) {x, y, z, x + sz, y, z, x + sz, y, z + sz, x, y, z + sz});
                        break;
        }
}

void tesselator_addf_cube_face_uv(struct tesselator* t, enum tesselator_cube_face face, float x, float y, float z,
                                                                  float sz, float u, float v, float us, float vs) {
        switch(face) {
                case CUBE_FACE_Z_N:
                        tesselator_addf_uv(t, (float[]) {x, y, z, x, y + sz, z, x + sz, y + sz, z, x + sz, y, z},
                                                          (float[]) {u, v, u, v + vs, u + us, v + vs, u + us, v});
                        break;
                case CUBE_FACE_Z_P:
                        tesselator_addf_uv(t, (float[]) {x, y, z + sz, x + sz, y, z + sz, x + sz, y + sz, z + sz, x, y + sz, z + sz},
                                                          (float[]) {u, v, u + us, v, u + us, v + vs, u, v + vs});
                        break;
                case CUBE_FACE_X_N:
                        tesselator_addf_uv(t, (float[]) {x, y, z, x, y, z + sz, x, y + sz, z + sz, x, y + sz, z},
                                                          (float[]) {u, v, u + us, v, u + us, v + vs, u, v + vs});
                        break;
                case CUBE_FACE_X_P:
                        tesselator_addf_uv(t, (float[]) {x + sz, y, z, x + sz, y + sz, z, x + sz, y + sz, z + sz, x + sz, y, z + sz},
                                                          (float[]) {u, v, u, v + vs, u + us, v + vs, u + us, v});
                        break;
                case CUBE_FACE_Y_P:
                        tesselator_addf_uv(t, (float[]) {x, y + sz, z, x, y + sz, z + sz, x + sz, y + sz, z + sz, x + sz, y + sz, z},
                                                          (float[]) {u, v, u, v + vs, u + us, v + vs, u + us, v});
                        break;
                case CUBE_FACE_Y_N:
                        tesselator_addf_uv(t, (float[]) {x, y, z, x + sz, y, z, x + sz, y, z + sz, x, y, z + sz},
                                                          (float[]) {u, v, u + us, v, u + us, v + vs, u, v + vs});
                        break;
        }
}
