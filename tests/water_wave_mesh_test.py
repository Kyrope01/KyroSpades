"""Regression tests for vertex-mode water waves and water mesh performance.

Run: python3 tests/water_wave_mesh_test.py

Covers the bug where unclamped per-vertex wave displacement in non-tiled wave
mode sank troughs below the y = 1 water plane into the seabed zone, while the
negative polygon offset pushed sub-pixel far-range water fragments past ground
in the coarse depth buffer — together producing flickering water-colored dots
and streaks on distant ground.

Also locks in the GPU-wave optimization: on OpenGL Core with the water program
available, vertex-mode waves are displaced in the vertex shader from a cached
flat mesh, so advancing wave time no longer rebuilds ~50k cells / re-uploads
the whole VBO every frame. Tiled waves and the legacy/GLES paths keep their
CPU animation (now clamped), and their per-frame rebuilds.

The real src/water.c, src/map.c, src/tesselator.c and deps/libvxl/libvxl.c are
compiled against stub GL that captures uploaded vertices, buffer usage hints
and polygon-offset values.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
water_src = (root / "src" / "water.c").read_text()
glx_src = (root / "src" / "glx.c").read_text()

# ---- Source-level guards ---------------------------------------------------

# 1. CPU vertex path clamps all four corners, matching tiled mode's floor.
assert "float wave_floor = y - 1.0F + 0.05F;" in water_src
assert water_src.count("fmaxf(y + WAVE3") == 4, "all four WAVE3 corners must be clamped"

# 2. The Core vertex shader displaces vertex-mode waves itself, with the same
#    coefficients and the same clamp, and never touches tiled-mode geometry.
assert "float wave_displacement(vec2 p)" in water_src
assert "u_WaveMode < 0.5" in water_src
assert "float wave_floor = world.y - 0.95;" in water_src
for coeff in ("* 0.08", "* 0.06", "* 0.04"):
    assert coeff in water_src

# 3. Waves must not force a per-frame CPU mesh rebuild on the GPU path; only
#    CPU-animated cases (no program, or tiled mode) may do that.
assert "bool cpu_wave_animation = waves_enabled" in water_src
assert "!water_program || settings.water_wave_mode == 1" in water_src
assert "wr.mesh_had_wave_mode = settings.water_wave_mode;" in water_src

# 4. No negative polygon offset while waves are active.
assert "if(waves_enabled)" in water_src
assert "glPolygonOffset(0.0F, 0.0F);" in water_src
assert "glPolygonOffset(-2.0F, -2.0F);" in water_src

# 5. Frequently re-streamed displaylist VBOs must use a dynamic usage hint.
dl_update = glx_src.split("void glx_displaylist_update(", 1)[1].split(
    "void glx_displaylist_draw(", 1
)[0]
assert "GL_DYNAMIC_DRAW" in dl_update
assert "GL_STATIC_DRAW" not in dl_update

# 6. Walking one block must not invalidate the cached water mesh any more.
assert "#define WATER_MESH_RECENTER_DISTANCE 2.0F" in water_src

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
#define GL_STATIC_DRAW 0x8892
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_STREAM_DRAW 0x88E0
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892

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
void glNormalPointer(GLint type, GLsizei stride, const void* ptr);
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

CGLM_STUB = r"""
#ifndef CGLM_CALL_STUB_H
#define CGLM_CALL_STUB_H
typedef float vec3[3];
typedef float vec4[4];
typedef vec4 mat4[4];
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
static GLfloat poly_factor = 0.0F, poly_units = 0.0F;
static size_t draw_calls = 0;
static size_t update_calls = 0;
static size_t last_uploaded_vertices = 0;
static GLenum last_buffer_usage = 0;
static GLfloat poly_factor_at_draw = 0.0F;
static float current_time = 1.25F;

/* Captured float vertex positions from the most recent displaylist upload. */
#define MAX_CAPTURED 200000
static float captured_xyz[MAX_CAPTURED * 3];
static size_t captured_count = 0;

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
void glNormalPointer(GLint t, GLsizei st, const void* p) { (void)t; (void)st; (void)p; }
void glTexCoordPointer(GLint s, GLenum t, GLsizei st, const void* p) { (void)s; (void)t; (void)st; (void)p; }
void glDrawArrays(GLenum mode, GLint first, GLsizei count) { (void)mode; (void)first; (void)count; }
void glUniform1f(GLint l, GLfloat v0) { (void)l; (void)v0; }
void glUniform2f(GLint l, GLfloat v0, GLfloat v1) { (void)l; (void)v0; (void)v1; }
void glUniform3f(GLint l, GLfloat v0, GLfloat v1, GLfloat v2) { (void)l; (void)v0; (void)v1; (void)v2; }
void glGenBuffers(GLsizei n, GLuint* buffers) { for (GLsizei i = 0; i < n; i++) buffers[i] = (GLuint)(i + 1); }
void glBindBuffer(GLenum target, GLuint buffer) { (void)target; (void)buffer; }
void glBufferData(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
    (void)target; (void)size; (void)data;
    last_buffer_usage = usage;
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
int glx_shader(const char* v, const char* f) { (void)v; (void)f; return WATER_SHADER_AVAILABLE; }
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
    (void)color; (void)normal; (void)texcoord;
    x->size = size;
    x->has_texcoord = texcoord != NULL;
    update_calls++;
    last_uploaded_vertices = size;
    if (type == GLX_DISPLAYLIST_ENHANCED && size <= MAX_CAPTURED) {
        memcpy(captured_xyz, vertex, size * 3 * sizeof(float));
        captured_count = size;
    }
}
void glx_displaylist_draw(struct glx_displaylist* x, int type) {
    (void)type;
    if (x->size > 0) {
        draw_calls++;
        /* Sample the offset while the draw actually happens: water_render
           restores the previous value before returning. */
        poly_factor_at_draw = poly_factor;
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
void glx_draw_vertices_3d(const float* v, size_t count, unsigned int m) { (void)v; (void)m; (void)count; }
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

static const float WATER_BASE_Y = WATER_LEVEL + 0.008F;

static void assert_all_flat(void) {
    /* GPU-wave meshes are baked at the flat base height; the vertex shader
       performs the displacement, so every uploaded y must equal the base. */
    assert(captured_count > 0);
    for (size_t i = 0; i < captured_count; i++) {
        float y = captured_xyz[i * 3 + 1];
        assert(fabsf(y - WATER_BASE_Y) < 1e-6F);
    }
}

static void assert_within_wave_clamp(float* cum_min, float* cum_max) {
    /* CPU displacement is clamped to [base - 0.95, base + amp * 0.18]. */
    const float floor_y = WATER_BASE_Y - 0.95F;
    const float top_y = WATER_BASE_Y + 5.0F * 0.18F;
    assert(captured_count > 0);
    for (size_t i = 0; i < captured_count; i++) {
        float y = captured_xyz[i * 3 + 1];
        assert(y >= floor_y - 1e-6F);
        assert(y <= top_y + 1e-6F);
        if (y < *cum_min) *cum_min = y;
        if (y > *cum_max) *cum_max = y;
    }
}

int main(void) {
    memset(&settings, 0, sizeof(settings));
    settings.render_distance = 32.0F;
    settings.water_shader = 1;
    settings.water_waves = 1;
    settings.water_wave_intensity = 5.0F; /* maximum: proves the clamp holds */
    settings.water_wave_speed = 1.0F;
    settings.water_wave_mode = 0;         /* vertex (non-tiled) wave mode */
    settings.water_wave_tile_size = 2;

    matrix_identity(matrix_projection);
    matrix_identity(matrix_view);
    matrix_identity(matrix_model);

    map_init();
    chunk_init();
    water_init();

#if defined(OPENGL_CORE) && WATER_SHADER_AVAILABLE
    /* ---- GPU vertex waves: flat cached mesh, no per-frame rebuild ---- */
    water_render();
    assert(update_calls == 1);
    /* GL_DYNAMIC_DRAW usage is enforced by the source guard above: the real
       glx.c buffer allocation is stubbed out in this harness. */
    assert_all_flat();

    size_t uploads = update_calls;
    current_time += 0.5F; /* only wave time changes: the shader owns that */
    water_render();
    assert(update_calls == uploads); /* no CPU rebuild, no VBO re-upload */
    assert(draw_calls == 2);         /* the cached mesh is still drawn */
    assert_all_flat();

    camera_x += 3.0F; /* beyond WATER_MESH_RECENTER_DISTANCE */
    water_render();
    assert(update_calls == uploads + 1); /* real view change rebuilds once */
    camera_x -= 3.0F;

    /* Tiled waves remain CPU-animated and therefore rebuild every frame. */
    settings.water_wave_mode = 1;
    water_render();
    uploads = update_calls;
    current_time += 0.5F;
    water_render();
    assert(update_calls == uploads + 1);

    /* Back to GPU vertex waves: one mode-change rebuild, then cached again. */
    settings.water_wave_mode = 0;
    water_render();
    uploads = update_calls;
    current_time += 0.5F;
    water_render();
    assert(update_calls == uploads);
    assert_all_flat();

    /* Waves off: the flat mesh needs no forward polygon offset any more. */
    settings.water_waves = 0;
    water_render();
    assert(poly_factor_at_draw == -2.0F);
    settings.water_waves = 1;
    water_render();
    assert(poly_factor_at_draw == 0.0F);
#else
    /* ---- CPU vertex waves (legacy/GLES, or Core without the program) ---- */
    float cum_min = 1e9F, cum_max = -1e9F;
    for (int frame = 0; frame < 3; frame++) {
        current_time = 1.25F + (float)frame * 1.13F;
        size_t uploads = update_calls;
        water_render();
        assert(update_calls == uploads + 1); /* CPU animation rebuilds per frame */
        assert_within_wave_clamp(&cum_min, &cum_max);
    }
    /* At intensity 5 over thousands of cells and several times, real crests
       and troughs must occur, and nothing escapes the clamp envelope. */
    assert(cum_max > WATER_BASE_Y + 0.05F);
    assert(cum_min < WATER_BASE_Y - 0.05F);
    assert(cum_min >= WATER_BASE_Y - 0.95F - 1e-6F);

    settings.water_waves = 0;
    water_render();
    assert(poly_factor_at_draw == -2.0F);
    settings.water_waves = 1;
    water_render();
    assert(poly_factor_at_draw == 0.0F);
#endif

    water_deinit();
    map_deinit();
    puts("water wave mesh behavior verified");
    return 0;
}
"""

COMMON_SRCS = [
    str(root / "src" / "chunk.c"),
    str(root / "src" / "water.c"),
    str(root / "src" / "map.c"),
    str(root / "src" / "tesselator.c"),
    str(root / "src" / "hashtable.c"),
    str(root / "src" / "channel.c"),
    str(root / "src" / "minheap.c"),
    str(root / "src" / "entitysystem.c"),
    str(root / "src" / "log.c"),
    str(root / "src" / "utils.c"),
    str(root / "deps" / "libvxl" / "libvxl.c"),
]

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

    # Core with the water program (GPU vertex waves), Core without it (CPU
    # fallback), and the legacy fixed-function build.
    for variant, extra_defines in (
        ("core_gpu", ["-DOPENGL_CORE", "-DWATER_SHADER_AVAILABLE=1"]),
        ("core_fallback", ["-DOPENGL_CORE", "-DWATER_SHADER_AVAILABLE=0"]),
        ("legacy", ["-DWATER_SHADER_AVAILABLE=0"]),
    ):
        exe = tmp_path / f"wave_{variant}"
        cmd = [
            "cc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
            *extra_defines,
            "-I", str(tmp_path),
            "-I", str(root / "src"),
            "-I", str(root / "deps/libvxl"),
            str(harness_path),
            *COMMON_SRCS,
            "-lm", "-lpthread",
            "-o", str(exe),
        ]
        subprocess.run(cmd, check=True)
        subprocess.run([str(exe)], check=True)

print("Water wave clamp, polygon offset, GPU displacement and mesh caching verified")
