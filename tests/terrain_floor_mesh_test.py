"""Behavioral + source regression test for hiding blocks below water (y < WATER_LEVEL).

Run: python3 tests/terrain_floor_mesh_test.py

Verifies by compiling and executing the real src/chunk.c, src/water.c, src/map.c,
src/tesselator.c, and deps/libvxl/libvxl.c:
1. Both legacy and OPENGL_CORE translation units compile cleanly.
2. When water_waves or water_shader is enabled, blocks below WATER_LEVEL (y = 0)
   are omitted (0 quads) across ALL terrain meshers (greedy, naive, AO, textured),
   independent of camera_y, while terrain at y >= 1 still renders normally.
3. When both water_waves and water_shader are off, all terrain meshers still
   emit the y = 0 floor for the classic flat-water path.
4. water_render() emits water geometry immediately on Frame 1 even before the
   asynchronous reflection worker has populated render_cache/render_state, and
   keeps rendering (with backface culling temporarily lifted) at low camera_y.
5. map_damaged_voxels_render() drops y < WATER_LEVEL entries when the separate
   water surface is active so shooting water never draws opaque-looking damage
   cubes through wave troughs.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]

chunk_src = (root / "src/chunk.c").read_text()
water_src = (root / "src/water.c").read_text()

for name, end in (
    ("greedy", "static void chunk_add_ao_face"),
    ("naive", "static void emit_textured_face"),
    ("textured", "void chunk_update_all"),
):
    body = chunk_src.split(f"void chunk_generate_{name}(", 1)[1].split(end, 1)[0]
    assert "water_surface_enabled()" in body, f"{name} missing water_surface_enabled()"
    assert "(float)y < WATER_LEVEL" in body, f"{name} missing WATER_LEVEL skip"
    assert "camera_y" not in body, f"{name} must not read camera_y on worker threads"

# Greedy meshing has 3 separate face passes (Z, X, Y); all 3 must skip y < WATER_LEVEL.
greedy_body = chunk_src.split("void chunk_generate_greedy(", 1)[1].split(
    "static void chunk_add_ao_face", 1
)[0]
assert greedy_body.count("(float)y < WATER_LEVEL") == 3, (
    "All three greedy-meshing face loops (Z, X, Y) must skip y < WATER_LEVEL"
)

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

static uint32_t run_mesher(int mode, struct libvxl_chunk_copy* copy, int16_t* min_y_out) {
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

    int16_t min_y = 32767;
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
    }
    if (min_y_out)
        *min_y_out = min_y;
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

    /* 1. Pure water chunk (only y = 0 solid blocks exist in default map):
          With water_waves / water_shader enabled, ALL meshers must emit 0 quads,
          even if camera_y is underwater. */
    struct libvxl_chunk_copy copy;
    map_copy_blocks(&copy, 0, 0);
    for (int cam_case = 0; cam_case < 2; cam_case++) {
        camera_y = (cam_case == 0) ? 12.0F : 0.4F;
        assert(water_surface_enabled());
        for (int mode = 0; mode <= 4; mode++) {
            uint32_t quads = run_mesher(mode, &copy, NULL);
            assert(quads == 0);
        }
    }

    /* 2. Classic path (both water_shader and water_waves disabled):
          Every mesher must emit the y = 0 water floor again. */
    settings.water_shader = 0;
    settings.water_waves = 0;
    assert(!water_surface_enabled());
    for (int mode = 0; mode <= 4; mode++) {
        uint32_t quads = run_mesher(mode, &copy, NULL);
        assert(quads > 0);
    }
    libvxl_copy_chunk_destroy(&copy);

    /* 3. Shoreline / elevated block at y = 1 with water enabled:
          y = 1 block is rendered (quads > 0), and no vertex dips below y = 1
          (i.e. zero faces from y = 0 blocks). */
    settings.water_shader = 1;
    settings.water_waves = 1;
    map_set(4, 1, 4, rgb(80, 160, 60));
    map_copy_blocks(&copy, 0, 0);
    for (int mode = 0; mode <= 4; mode++) {
        int16_t min_y = 0;
        uint32_t quads = run_mesher(mode, &copy, &min_y);
        assert(quads > 0);
        assert(min_y >= 1);
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

    /* 5. Damaged-voxel overlay must ignore y = 0 when water surface is active,
          while still rendering damage on y >= 1 terrain blocks. */
    camera_y = 10.0F;
    immediate_draw_quads = 0;
    map_damage(2, 0, 2, 50);
    map_damaged_voxels_render();
    assert(immediate_draw_quads == 0);

    immediate_draw_quads = 0;
    map_damage(4, 1, 4, 50);
    map_damaged_voxels_render();
    assert(immediate_draw_quads > 0);

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

print("All terrain meshing modes omit y < WATER_LEVEL blocks when water is active and water_render covers frame 1")
