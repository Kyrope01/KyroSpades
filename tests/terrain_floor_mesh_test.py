"""Behavioral regression for submerged terrain and dry-column floor holes.

Run: python3 tests/terrain_floor_mesh_test.py

Compiles and executes the real chunk/water/map/tesselator/libvxl sources in both
legacy and OPENGL_CORE modes, and verifies:
1. With the dedicated water surface active, every terrain mesher omits static
   y < WATER_LEVEL blocks in real water columns (air at WATER_LEVEL), regardless
   of camera height, so they cannot poke through animated wave troughs.
2. The classic path (water shader and waves both off) still meshes that floor.
3. Dry columns (solid at WATER_LEVEL) keep their y = 0 floor geometry. A missing
   neighbouring floor block exposes the dry block's side face instead of leaving
   a see-through hole; terrain at y >= WATER_LEVEL also remains visible.
4. water_render() covers exposed water immediately on frame 1, including before
   its asynchronous reflection cache is populated and from low camera angles.
5. Damage overlays follow the same rule: submerged cubes are omitted, while dry
   floor cubes and solid blocks at WATER_LEVEL keep their damage tint.
6. The culling height from each mesher contains every vertex, including the top
   faces of a one-block floor and a block at the top of the map.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]

chunk_src = (root / "src/chunk.c").read_text()
water_src = (root / "src/water.c").read_text()

hide_call = "chunk_block_hidden_by_water(blocks, x, y, z, separate_water_surface)"
for name, end, expected_sites in (
    ("greedy", "static void chunk_add_ao_face", 3),
    ("naive", "static void emit_textured_face", 1),
    ("textured", "void chunk_update_all", 1),
):
    body = chunk_src.split(f"void chunk_generate_{name}(", 1)[1].split(end, 1)[0]
    assert "bool separate_water_surface = water_surface_enabled();" in body
    assert body.count(hide_call) == expected_sites, (
        f"{name} must apply the submerged-block filter in every face pass"
    )
    assert "camera_y" not in body, f"{name} must not read camera_y on worker threads"

# The filter must check the actual water column, not blanket-strip every y=0
# block (which caused the dry-ground holes fixed by PR #12).
hide_helper = chunk_src.split("inline bool chunk_block_hidden_by_water(", 1)[1].split(
    "static __attribute__", 1
)[0]
assert "(float)y < WATER_LEVEL" in hide_helper
assert "solid_array_isair(blocks, x, (int)WATER_LEVEL, z)" in hide_helper

# Damage overlays use the same exposed-water test as the terrain meshers.
map_src = (root / "src/map.c").read_text()
damage_body = map_src.split("static bool damaged_voxel_update(", 1)[1].split(
    "void map_damaged_voxels_render(", 1
)[0]
assert "water_surface_enabled()" in damage_body
assert "map_isair(x, (int)WATER_LEVEL, z)" in damage_body

water_surface_fn = water_src.split("bool water_surface_enabled(void) {", 1)[1].split("}", 1)[0]
assert "camera_y" not in water_surface_fn

GLEW_STUB = r"""
#ifndef GL_GLEW_STUB_H
#define GL_GLEW_STUB_H
#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef short GLshort;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef ptrdiff_t GLintptr;
typedef ptrdiff_t GLsizeiptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_TRIANGLES 0x0004
#define GL_QUADS 0x0007
#define GL_LESS 0x0201
#define GL_EQUAL 0x0202
#define GL_LEQUAL 0x0203
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_BACK 0x0405
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_TEXTURE_2D 0x0DE1
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_STREAM_DRAW 0x88E0

void glEnable(GLenum cap);
void glDisable(GLenum cap);
GLboolean glIsEnabled(GLenum cap);
void glGetFloatv(GLenum pname, GLfloat* data);
void glPolygonOffset(GLfloat factor, GLfloat units);
void glActiveTexture(GLenum texture);
void glBindTexture(GLenum target, GLuint texture);
void glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
void glDepthFunc(GLenum func);
void glDepthMask(GLboolean flag);
void glBlendFunc(GLenum sfactor, GLenum dfactor);
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
void glEnableClientState(GLenum cap);
void glDisableClientState(GLenum cap);
void glVertexPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void glColorPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void glNormalPointer(GLenum type, GLsizei stride, const void* ptr);
void glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const void* ptr);
void glDrawArrays(GLenum mode, GLint first, GLsizei count);
void glUniform1f(GLint location, GLfloat v0);
void glUniform2f(GLint location, GLfloat v0, GLfloat v1);
void glUniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
void glGenBuffers(GLsizei n, GLuint* buffers);
void glBindBuffer(GLenum target, GLuint buffer);
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void* pointer);
void glEnableVertexAttribArray(GLuint index);
void glDisableVertexAttribArray(GLuint index);
#endif
"""

HARNESS = r"""
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "camera.h"
#include "chunk.h"
#include "glx.h"
#include "lighting.h"
#include "map.h"
#include "matrix.h"
#include "particle.h"
#include "shadow.h"
#include "sound.h"
#include "texture.h"
#include "tesselator.h"
#include "water.h"
#include "window.h"
#include "glowing_blocks.h"

struct RENDER_OPTIONS settings;
float camera_x = 8.0F, camera_y = 10.0F, camera_z = 8.0F;
int glx_version = 1;
int glx_fog = 1;
int glx_context_version_major = 3;
int glx_context_version_minor = 3;
int gles_version = 0;
float gles_current_color[4];
mat4 matrix_view;
mat4 matrix_model;
mat4 matrix_projection;
struct texture texture_minimap;
int texture_blocks_custom_loaded = 0;
struct Sound_wav sound_bounce;
struct Sound_wav sound_debris;
int ms_seed = 1;

int ms_rand(void) {
    ms_seed = ms_seed * 0x343FD + 0x269EC3;
    return (ms_seed >> 16) & 0x7FFF;
}

static GLboolean cap_cull = GL_TRUE;
static GLboolean cap_poly_offset = GL_FALSE;
static GLboolean cull_at_water_draw = GL_TRUE;
static GLfloat poly_factor = 0.0F, poly_units = 0.0F;
static size_t last_uploaded_vertices = 0;
static size_t draw_calls = 0;
static size_t immediate_draw_quads = 0;
static float current_time = 1.25F;

void glEnable(GLenum cap) {
    if (cap == GL_CULL_FACE) cap_cull = GL_TRUE;
    if (cap == GL_POLYGON_OFFSET_FILL) cap_poly_offset = GL_TRUE;
}
void glDisable(GLenum cap) {
    if (cap == GL_CULL_FACE) cap_cull = GL_FALSE;
    if (cap == GL_POLYGON_OFFSET_FILL) cap_poly_offset = GL_FALSE;
}
GLboolean glIsEnabled(GLenum cap) {
    if (cap == GL_CULL_FACE) return cap_cull;
    if (cap == GL_POLYGON_OFFSET_FILL) return cap_poly_offset;
    return GL_FALSE;
}
void glGetFloatv(GLenum pname, GLfloat* data) {
    if (pname == GL_POLYGON_OFFSET_FACTOR) *data = poly_factor;
    else if (pname == GL_POLYGON_OFFSET_UNITS) *data = poly_units;
    else *data = 0.0F;
}
void glPolygonOffset(GLfloat factor, GLfloat units) { poly_factor = factor; poly_units = units; }
void glActiveTexture(GLenum t) { (void)t; }
void glBindTexture(GLenum t, GLuint id) { (void)t; (void)id; }
void glTexSubImage2D(GLenum t, GLint l, GLint xo, GLint yo, GLsizei w, GLsizei h,
                     GLenum f, GLenum ty, const void* p) {
    (void)t; (void)l; (void)xo; (void)yo; (void)w; (void)h; (void)f; (void)ty; (void)p;
}
void glDepthFunc(GLenum f) { (void)f; }
void glDepthMask(GLboolean f) { (void)f; }
void glBlendFunc(GLenum s, GLenum d) { (void)s; (void)d; }
void glColorMask(GLboolean r, GLboolean g, GLboolean b, GLboolean a) { (void)r; (void)g; (void)b; (void)a; }
void glEnableClientState(GLenum c) { (void)c; }
void glDisableClientState(GLenum c) { (void)c; }
void glVertexPointer(GLint s, GLenum t, GLsizei st, const void* p) { (void)s; (void)t; (void)st; (void)p; }
void glColorPointer(GLint s, GLenum t, GLsizei st, const void* p) { (void)s; (void)t; (void)st; (void)p; }
void glNormalPointer(GLenum t, GLsizei st, const void* p) { (void)t; (void)st; (void)p; }
void glTexCoordPointer(GLint s, GLenum t, GLsizei st, const void* p) { (void)s; (void)t; (void)st; (void)p; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count) {
    (void)mode; (void)first;
    immediate_draw_quads += (size_t)count;
}
void glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; }
void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; }
void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; }
void glGenBuffers(GLsizei n, GLuint* buffers) {
    for (GLsizei i = 0; i < n; i++) buffers[i] = (GLuint)(i + 1);
}
void glBindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)target; (void)size; (void)data; (void)usage;
}
void glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
    (void)target; (void)offset; (void)size; (void)data;
}
void glVertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                           GLsizei stride, const void* pointer) {
    (void)index; (void)size; (void)type; (void)normalized; (void)stride; (void)pointer;
}
void glEnableVertexAttribArray(GLuint index) { (void)index; }
void glDisableVertexAttribArray(GLuint index) { (void)index; }

void glx_set_color4f(float r, float g, float b, float a) {
    gles_current_color[0] = r; gles_current_color[1] = g;
    gles_current_color[2] = b; gles_current_color[3] = a;
}
bool glx_version_at_least(int major, int minor) { (void)major; (void)minor; return true; }
int glx_shader(const char* v, const char* f) { (void)v; (void)f; return 1; }
static unsigned int cur_prog = 0;
void glx_use_program(unsigned int p) { cur_prog = p; }
unsigned int glx_current_program(void) { return cur_prog; }
int glx_uniform_location(unsigned int p, const char* n) { (void)p; (void)n; return 1; }
void glx_delete_program(unsigned int p) { (void)p; }
void glx_displaylist_create(struct glx_displaylist* x, bool hc, bool hn) {
    memset(x, 0, sizeof(*x));
    x->has_color = hc;
    x->has_normal = hn;
}
void glx_displaylist_destroy(struct glx_displaylist* x) { memset(x, 0, sizeof(*x)); }
void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type,
                            void* color, void* vertex, void* normal, void* texcoord) {
    (void)type; (void)color; (void)vertex; (void)normal;
    x->size = size;
    x->has_texcoord = texcoord != NULL;
    last_uploaded_vertices = size;
}
void glx_displaylist_draw(struct glx_displaylist* x, int type) {
    (void)type;
    if (x->size > 0) {
        draw_calls++;
        cull_at_water_draw = cap_cull;
    }
}

/* The chunk-draw client-state scope is inert in these harnesses: nothing draws. */
void glx_client_state_begin(void) {}
void glx_client_state_end(void) {}
void glx_client_state_invalidate(void) {}
void glx_set_explicit_attributes(bool e) { (void)e; }
bool glx_uses_explicit_attributes(void) { return false; }
#ifdef GLX_PROGRAMMABLE
void glx_use_default_shader(void) {}
int glx_default_shader_program(void) { return 2; }
void glx_default_shader_set_draw_state(int vc, int te, int le) { (void)vc; (void)te; (void)le; }
void glx_draw_vertices_3d(const float* v, size_t count, unsigned int m) {
    (void)v; (void)m;
    immediate_draw_quads += count;
}
#endif

float window_time(void) { return current_time; }
int window_cpucores(void) { return 1; }

int camera_CubeInFrustum(float x, float y, float z, float size, float size_y) {
    (void)x; (void)y; (void)z; (void)size; (void)size_y;
    return 1;
}

void matrix_identity(mat4 m) {
    memset(m, 0, sizeof(mat4));
    m[0][0] = m[1][1] = m[2][2] = m[3][3] = 1.0F;
}
void matrix_push(mat4 m) { (void)m; }
void matrix_pop(mat4 m) { (void)m; }
void matrix_translate(mat4 m, float x, float y, float z) { (void)m; (void)x; (void)y; (void)z; }
void matrix_rotate(mat4 m, float a, float x, float y, float z) { (void)m; (void)a; (void)x; (void)y; (void)z; }
void matrix_vector(mat4 m, vec4 v) { (void)m; (void)v; }
void matrix_upload(void) {}

bool lighting_supported(void) { return false; }
bool lighting_world_supported(void) { return false; }
float lighting_sunlight_scale(void) { return 1.0F; }
void lighting_world_update_matrices(void) {}
void lighting_apply_program(unsigned int p) { (void)p; }

bool shadow_baked_enabled(void) { return false; }
void shadow_invalidate(void) {}
void shadow_apply_program(unsigned int p) { (void)p; }
void shadow_finish_program(void) {}

struct texture* texture_blocks_atlas(void) { return &texture_minimap; }
int texture_blocks_custom_tile(uint32_t col) { (void)col; return 0; }
int texture_blocks_custom_grid(void) { return 4; }

void glowing_blocks_clear(void) {}
void glowing_blocks_map_changed(int x, int y, int z, unsigned int color) {
    (void)x; (void)y; (void)z; (void)color;
}
void particle_create_block(unsigned int c, float x, float y, float z,
                           float v, float vy, int count, float min_s, float max_s) {
    (void)c; (void)x; (void)y; (void)z; (void)v; (void)vy; (void)count; (void)min_s; (void)max_s;
}
void sound_create(enum sound_space local, struct Sound_wav* w, float x, float y, float z) {
    (void)local; (void)w; (void)x; (void)y; (void)z;
}

/* libvxl refuses to dig the bottom layer through map_set() (permanent bedrock),
   but maps loaded from .vxl data can contain gaps there — e.g. low cave floors.
   Recreate that geometry directly inside an immutable chunk copy: clear the
   solidity bit and drop the matching sorted block entry so every mesher treats
   the cell exactly like missing floor geometry. */
static void copy_remove_floor_block(struct libvxl_chunk_copy* copy, int x, int z) {
    size_t lx = ((size_t)x + copy->width - copy->geo_x) % copy->width;
    size_t ly = ((size_t)z + copy->height - copy->geo_y) % copy->height;
    size_t bottom = copy->depth - 1; /* engine y = 0 */
    assert(lx < copy->geo_w && ly < copy->geo_h);
    size_t off = bottom + (lx + ly * copy->geo_w) * copy->depth;
    copy->geometry[off / (sizeof(size_t) * 8)] &= ~((size_t)1 << (off % (sizeof(size_t) * 8)));

    /* map.h #undefs libvxl's pos_key macro, so pack the key with its layout:
       (column-z << 20) | (column-x << 8) | libvxl-depth-z. */
    uint32_t target = ((uint32_t)z << 20) | ((uint32_t)x << 8) | (uint32_t)bottom;
    for (size_t i = 0; i < copy->blocks_sorted_count; i++) {
        if (copy->blocks_sorted[i].position == target) {
            memmove(copy->blocks_sorted + i, copy->blocks_sorted + i + 1,
                    (copy->blocks_sorted_count - i - 1) * sizeof(struct libvxl_block));
            copy->blocks_sorted_count--;
            return;
        }
    }
    assert(0); /* the default floor block must be present to remove */
}

/* A real libvxl edit also adds newly exposed solid blocks to the chunk's
   surface-block list used by the naive/textured meshers. Mirror that update
   after changing the copied geometry directly for the bedrock-gap fixture. */
static void copy_ensure_floor_block(struct libvxl_chunk_copy* copy, int x, int z) {
    size_t bottom = copy->depth - 1;
    uint32_t target = ((uint32_t)z << 20) | ((uint32_t)x << 8) | (uint32_t)bottom;
    for (size_t i = 0; i < copy->blocks_sorted_count; i++)
        if (copy->blocks_sorted[i].position == target)
            return;

    uint32_t color = map_get(x, 0, z);
    struct libvxl_block* grown = realloc(
        copy->blocks_sorted, (copy->blocks_sorted_count + 1) * sizeof(*grown));
    assert(grown);
    copy->blocks_sorted = grown;
    copy->blocks_sorted[copy->blocks_sorted_count++] =
        (struct libvxl_block){.position = target, .color = color};

    size_t lx = (size_t)x - copy->grid_x;
    size_t lz = (size_t)z - copy->grid_y;
    assert(lx < LIBVXL_CHUNK_SIZE && lz < LIBVXL_CHUNK_SIZE);
    copy->colors[bottom + (lx + lz * LIBVXL_CHUNK_SIZE) * copy->depth] = color & 0xFFFFFF;
}

static uint32_t run_mesher(int mode, struct libvxl_chunk_copy* copy,
                           int16_t* min_y_out, int16_t* max_y_out) {
    struct tesselator tess;
    int max_h = 0;
    tesselator_create(&tess, VERTEX_INT, 0, mode >= 3 ? 1 : 0);
    texture_blocks_custom_loaded = (mode == 4) ? 1 : 0;
    if (mode == 0)
        chunk_generate_greedy(copy, 0, 0, &tess, &max_h);
    else if (mode == 1)
        chunk_generate_naive(copy, &tess, &max_h, 0);
    else if (mode == 2)
        chunk_generate_naive(copy, &tess, &max_h, 1);
    else
        chunk_generate_textured(copy, &tess, &max_h);
    texture_blocks_custom_loaded = 0;

    int16_t min_y = 32767, max_y = -32768;
#ifdef TESSELATE_TRIANGLES
    size_t vcount = (size_t)tess.quad_count * 6;
#else
    size_t vcount = (size_t)tess.quad_count * 4;
#endif
    const int16_t* verts = (const int16_t*)tess.vertices;
    for (size_t i = 0; i < vcount; i++) {
        int16_t vy = verts[i * 3 + 1];
        if (vy < min_y)
            min_y = vy;
        if (vy > max_y)
            max_y = vy;
    }
    /* Frustum culling must bound the actual top face, not just the y of the
       highest block. Otherwise visible floors disappear near the screen edge. */
    if (vcount)
        assert(max_h >= max_y);
    if (min_y_out)
        *min_y_out = min_y;
    if (max_y_out)
        *max_y_out = max_y;
    uint32_t q = tess.quad_count;
    tesselator_free(&tess);
    return q;
}

int main(void) {
    memset(&settings, 0, sizeof(settings));
    settings.render_distance = 32.0F;
    settings.sunlight_intensity = 100;
    settings.ao_multiplier = 1.0F;
    settings.water_shader = 1;
    settings.water_waves = 1;
    settings.water_wave_intensity = 2.0F;
    settings.water_wave_speed = 1.0F;
    settings.water_wave_mode = 1;
    settings.water_wave_tile_size = 2;

    matrix_identity(matrix_projection);
    matrix_identity(matrix_view);
    matrix_identity(matrix_model);

    map_init();
    chunk_init();
    water_init();

    /* 1. The default map has a solid y = 0 floor and air at WATER_LEVEL, so
          every column is covered by the separate water pass. No mesher may
          emit the static seabed through animated waves, above or below water. */
    struct libvxl_chunk_copy copy;
    map_copy_blocks(&copy, 0, 0);
    for (int cam_case = 0; cam_case < 2; cam_case++) {
        camera_y = (cam_case == 0) ? 12.0F : 0.4F;
        assert(water_surface_enabled());
        for (int mode = 0; mode <= 4; mode++)
            assert(run_mesher(mode, &copy, NULL, NULL) == 0);
    }

    /* 2. Classic flat water has no separate replacement surface, so the exact
          same submerged blocks return when both dedicated-water features are
          disabled. */
    settings.water_shader = 0;
    settings.water_waves = 0;
    assert(!water_surface_enabled());
    for (int mode = 0; mode <= 4; mode++) {
        int16_t max_y;
        assert(run_mesher(mode, &copy, NULL, &max_y) > 0);
        assert(max_y == 1); /* the flat floor's top face */
    }
    libvxl_copy_chunk_destroy(&copy);

    /* The top-of-map block has a face at y = 64. Check that every mesher's
       culling bounds include it as well as the low floor above. */
    map_set(7, 63, 7, rgb(120, 160, 80));
    map_copy_blocks(&copy, 0, 0);
    for (int mode = 0; mode <= 4; mode++) {
        int16_t max_y;
        assert(run_mesher(mode, &copy, NULL, &max_y) > 0);
        assert(max_y == 64);
    }
    libvxl_copy_chunk_destroy(&copy);
    map_set(7, 63, 7, 0xFFFFFFFFU);

    /* 3. Do not reintroduce PR #12's dry-ground holes: a solid block at y = 1
          makes this column dry, so its y = 0 floor must stay in the mesh.
          Removing the neighbouring floor in the immutable copy exposes the dry
          block's +X side, giving every mesher an observable y = 0 face. */
    settings.water_shader = 1;
    settings.water_waves = 1;
    map_set(4, 1, 4, rgb(80, 160, 60)); /* dry column: solid at WATER_LEVEL */
    map_copy_blocks(&copy, 0, 0);
    copy_remove_floor_block(&copy, 5, 4);
    copy_ensure_floor_block(&copy, 4, 4);
    for (int mode = 0; mode <= 4; mode++) {
        int16_t min_y = 32767;
        uint32_t quads = run_mesher(mode, &copy, &min_y, NULL);
        assert(quads > 0); /* the y = 1 block and exposed dry-floor side remain */
        assert(min_y == 0);
    }
    libvxl_copy_chunk_destroy(&copy);

    /* 4. Frame-1 water_render() BEFORE water_reflection_pass() has ever run:
          water geometry must render immediately in tile, vertex, and flat modes,
          and low-camera views must temporarily lift GL_CULL_FACE. */
    water_invalidate();
    camera_x = 8.0F;
    camera_y = 10.0F;
    camera_z = 8.0F;

    draw_calls = 0;
    last_uploaded_vertices = 0;
    cap_cull = GL_TRUE;
    water_render();
    assert(draw_calls == 1);
    assert(last_uploaded_vertices > 0);
    assert(cull_at_water_draw == GL_TRUE);
    assert(cap_cull == GL_TRUE);

    settings.water_wave_mode = 0; /* vertex waves */
    current_time += 0.5F;
    draw_calls = 0;
    last_uploaded_vertices = 0;
    water_render();
    assert(draw_calls == 1);
    assert(last_uploaded_vertices > 0);

    camera_y = 0.5F; /* underwater / inside wave trough */
    current_time += 0.5F;
    draw_calls = 0;
    cap_cull = GL_TRUE;
    water_render();
    assert(draw_calls == 1);
    assert(cull_at_water_draw == GL_FALSE);
    assert(cap_cull == GL_TRUE); /* restored after water_render() */

    /* Flat shader mode + live map_set at y == WATER_LEVEL:
       mesh_map_revision must trigger an immediate water mesh rebuild even when
       waves are off and before the async reflection worker finishes. */
    camera_y = 10.0F;
    settings.water_waves = 0;
    settings.water_shader = 1;
    water_reflection_pass();
    last_uploaded_vertices = 0;
    water_render();
    size_t verts_before = last_uploaded_vertices;
    assert(verts_before > 0);

    map_set(6, 1, 6, rgb(120, 100, 80));
    last_uploaded_vertices = 0;
    water_render();
    assert(last_uploaded_vertices > 0 && last_uploaded_vertices < verts_before);

    map_set(6, 1, 6, 0xFFFFFFFFU);
    last_uploaded_vertices = 0;
    water_render();
    assert(last_uploaded_vertices == verts_before);
    settings.water_waves = 1;

    /* 5. Damage overlays match the terrain: no dark cube on a submerged block,
          but both the retained dry floor and the solid y = 1 block can be
          damaged normally. */
    camera_y = 10.0F;
    immediate_draw_quads = 0;
    map_damage(2, 0, 2, 50); /* air at WATER_LEVEL: submerged */
    map_damaged_voxels_render();
    assert(immediate_draw_quads == 0);

    immediate_draw_quads = 0;
    map_damage(4, 0, 4, 50); /* solid at WATER_LEVEL: dry floor */
    map_damaged_voxels_render();
    assert(immediate_draw_quads > 0);

    immediate_draw_quads = 0;
    map_damage(4, 1, 4, 50);
    map_damaged_voxels_render();
    assert(immediate_draw_quads > 0);
    size_t two_entry_quads = immediate_draw_quads;

    /* Damage on an air voxel is still discarded (block was never there). The
       two solid entries above stay in the table and re-render every pass, so
       the rebuilt overlay must contain exactly the same faces — none from the
       air entry. */
    map_damage(8, 5, 8, 50);
    immediate_draw_quads = 0;
    map_damaged_voxels_render();
    assert(immediate_draw_quads == two_entry_quads);

    water_deinit();
    map_deinit();
    return 0;
}
"""

COMMON_SRCS = [
    str(root / "src/chunk.c"),
    str(root / "src/water.c"),
    str(root / "src/map.c"),
    str(root / "src/tesselator.c"),
    str(root / "src/hashtable.c"),
    str(root / "src/channel.c"),
    str(root / "src/minheap.c"),
    str(root / "src/entitysystem.c"),
    str(root / "src/log.c"),
    str(root / "src/utils.c"),
    str(root / "deps/libvxl/libvxl.c"),
]

CGLM_STUB = r"""
#ifndef CGLM_CALL_STUB_H
#define CGLM_CALL_STUB_H
typedef float vec3[3];
typedef float vec4[4];
typedef vec4 mat4[4];
#endif
"""

with tempfile.TemporaryDirectory() as tmp:
    tmp_path = Path(tmp)
    gl_dir = tmp_path / "GL"
    gl_dir.mkdir()
    (gl_dir / "glew.h").write_text(GLEW_STUB)
    cglm_dir = tmp_path / "cglm"
    cglm_dir.mkdir()
    (cglm_dir / "call.h").write_text(CGLM_STUB)
    harness_path = tmp_path / "harness.c"
    harness_path.write_text(HARNESS)

    for variant, extra_defines in (
        ("legacy", []),
        ("opengl_core", ["-DOPENGL_CORE"]),
    ):
        exe = tmp_path / f"water_floor_{variant}"
        cmd = [
            "cc",
            "-std=gnu11",
            "-O2",
            "-Wall",
            "-Wextra",
            *extra_defines,
            "-I",
            str(tmp_path),
            "-I",
            str(root / "src"),
            "-I",
            str(root / "deps/libvxl"),
            str(harness_path),
            *COMMON_SRCS,
            "-lm",
            "-lpthread",
            "-o",
            str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)

print("Submerged terrain is hidden in every mesher, dry-column floors remain, and water/damage rendering stays consistent")
