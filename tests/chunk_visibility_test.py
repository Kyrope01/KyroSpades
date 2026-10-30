"""Exercise real terrain draw selection around the camera.

Run: python3 tests/chunk_visibility_test.py

Compile src/chunk.c with stubbed GL calls and the real camera frustum math.
The captured draw calls must keep the camera chunk and two chunks in every
horizontal direction even if the frustum rejects them, including at wrapped map
seams and the minimum spectator distance. Distant terrain and the shadow pass
must retain their normal culling. Also sweep high-FOV views while moving across
chunk boundaries and compare all distant chunks to the actual frustum.
"""
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
camera_src = (root / "src/camera.c").read_text()


def extract_function(src, name):
    start = src.index(name + "(")
    start = src.rindex("\n", 0, start) + 1
    brace = src.index("{", start)
    depth = 0
    for end in range(brace, len(src)):
        if src[end] == "{":
            depth += 1
        elif src[end] == "}":
            depth -= 1
            if depth == 0:
                return src[start:end + 1]
    raise AssertionError(f"could not extract {name}")


frustum_functions = "\n\n".join((
    extract_function(camera_src, "camera_ExtractFrustum"),
    extract_function(camera_src, "camera_CubeInFrustum").replace(
        "camera_CubeInFrustum(", "real_camera_CubeInFrustum(", 1
    ),
))

GLEW_STUB = r"""
#ifndef GL_GLEW_STUB_H
#define GL_GLEW_STUB_H
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
#define GL_QUADS 0x0007
#define GL_RGBA 0x1908
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_TEXTURE0 0x84C0
void glActiveTexture(GLenum texture);
void glBindTexture(GLenum target, GLuint texture);
void glEnable(GLenum cap);
void glDisable(GLenum cap);
void glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w,
                     GLsizei h, GLenum format, GLenum type, const void* pixels);
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
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "camera.h"
#include "chunk.h"
#include "config.h"
#include "matrix.h"
#include "texture.h"

struct RENDER_OPTIONS settings;
float camera_x, camera_y, camera_z;
int map_size_x = 512, map_size_z = 512;
mat4 matrix_model, matrix_view, matrix_projection;
float frustum[6][4];

/* Same matrix conventions as matrix.c, for the real frustum extractor. */
void matrix_load(mat4 m, mat4 n) { memcpy(m, n, sizeof(mat4)); }
void matrix_multiply(mat4 m, mat4 n) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0.0F;
            for (int k = 0; k < 4; k++) s += n[k][row] * m[c][k];
            r[c][row] = s;
        }
    memcpy(m, r, sizeof(mat4));
}

__FRUSTUM_FUNCTIONS__

enum { REJECT_ALL, ACCEPT_ALL, POSITIVE_X_ONLY, REAL_FRUSTUM };
static int frustum_mode, frustum_calls;
int camera_CubeInFrustum(float x, float y, float z, float size, float height) {
    frustum_calls++;
    if (frustum_mode == REJECT_ALL) return 0;
    if (frustum_mode == ACCEPT_ALL) return 1;
    if (frustum_mode == POSITIVE_X_ONLY) return x > camera_x;
    return real_camera_CubeInFrustum(x, y, z, size, height);
}

/* Capture the world-space chunk position after its wrapped-map transform. */
struct draw { int x, z; float distance_sq; };
static struct draw draws[10000];
static int draw_count, stack_depth;
static float model_x, model_z, saved_x, saved_z;
void matrix_push(mat4 m) {
    (void)m;
    assert(stack_depth == 0);
    stack_depth = 1;
    saved_x = model_x; saved_z = model_z;
}
void matrix_pop(mat4 m) {
    (void)m;
    assert(stack_depth == 1);
    stack_depth = 0;
    model_x = saved_x; model_z = saved_z;
}
void matrix_translate(mat4 m, float x, float y, float z) {
    (void)m; (void)y;
    model_x += x; model_z += z;
}
void matrix_upload(void) {}
void lighting_world_update_matrices(void) {}
void glx_displaylist_draw(struct glx_displaylist* dl, int type) {
    (void)type;
    struct chunk* c = (struct chunk*)((char*)dl - offsetof(struct chunk, display_list));
    assert(c >= chunks && c < chunks + CHUNKS_PER_DIM * CHUNKS_PER_DIM);
    assert(draw_count < (int)(sizeof(draws) / sizeof(*draws)));
    int x = c->x + (int)lroundf(model_x / CHUNK_SIZE);
    int z = c->y + (int)lroundf(model_z / CHUNK_SIZE);
    float dx = (x + 0.5F) * CHUNK_SIZE - camera_x;
    float dz = (z + 0.5F) * CHUNK_SIZE - camera_z;
    draws[draw_count++] = (struct draw){x, z, dx * dx + dz * dz};
}

/* The chunk-draw client-state scope is inert in these harnesses: nothing draws. */
void glx_client_state_begin(void) {}
void glx_client_state_end(void) {}
void glx_client_state_invalidate(void) {}
void glActiveTexture(GLenum t) { (void)t; }
void glBindTexture(GLenum t, GLuint id) { (void)t; (void)id; }
void glEnable(GLenum t) { (void)t; }
void glDisable(GLenum t) { (void)t; }
struct texture* texture_blocks_atlas(void) {
    static struct texture atlas;
    return &atlas;
}
void log_log(int level, const char* file, int line, const char* fmt, ...) {
    (void)level; (void)file; (void)line; (void)fmt;
    assert(0 && "unexpected chunk allocation error");
}

static int find_draw(int x, int z) {
    int found = 0;
    for (int i = 0; i < draw_count; i++)
        found += draws[i].x == x && draws[i].z == z;
    return found;
}
static void reset_draws(void) {
    assert(stack_depth == 0 && model_x == 0.0F && model_z == 0.0F);
    draw_count = frustum_calls = 0;
}
static void assert_neighborhood(void) {
    int cx = (int)floorf(camera_x / CHUNK_SIZE);
    int cz = (int)floorf(camera_z / CHUNK_SIZE);
    for (int z = cz - 2; z <= cz + 2; z++)
        for (int x = cx - 2; x <= cx + 2; x++)
            assert(find_draw(x, z) == 1);
}
static void assert_sorted(void) {
    for (int i = 1; i < draw_count; i++)
        assert(draws[i - 1].distance_sq <= draws[i].distance_sq + 0.001F);
}
static void reject_all_case(float x, float z, float rd) {
    camera_x = x; camera_z = z; settings.render_distance = rd;
    frustum_mode = REJECT_ALL;
    reset_draws();
    chunk_draw_visible();
    assert(draw_count == 25); /* guard is bounded, with no duplicate draws */
    assert_neighborhood();
    assert_sorted();
}

static void perspective(mat4 m, float fovy, float aspect, float near, float far) {
    memset(m, 0, sizeof(mat4));
    float f = 1.0F / tanf(fovy / 2.0F);
    m[0][0] = f / aspect; m[1][1] = f;
    m[2][2] = (far + near) / (near - far); m[2][3] = -1.0F;
    m[3][2] = 2.0F * far * near / (near - far);
}
static void normalize3(float* v) {
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    v[0] /= l; v[1] /= l; v[2] /= l;
}
static void look_at(mat4 m, const float eye[3], const float dir[3]) {
    float f[3] = {dir[0], dir[1], dir[2]};
    normalize3(f);
    float s[3] = {-f[2], 0.0F, f[0]}; /* cross(f, world up) */
    normalize3(s);
    float u[3] = {s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0]};
    memset(m, 0, sizeof(mat4));
    m[0][0]=s[0]; m[1][0]=s[1]; m[2][0]=s[2];
    m[0][1]=u[0]; m[1][1]=u[1]; m[2][1]=u[2];
    m[0][2]=-f[0]; m[1][2]=-f[1]; m[2][2]=-f[2];
    m[3][0]=-(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]);
    m[3][1]=-(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]);
    m[3][2]= f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2];
    m[3][3]=1.0F;
}

int main(void) {
    for (int z = 0; z < CHUNKS_PER_DIM; z++)
        for (int x = 0; x < CHUNKS_PER_DIM; x++) {
            struct chunk* c = chunks + x + z * CHUNKS_PER_DIM;
            c->x = x; c->y = z; c->created = true; c->max_height = 1;
            c->display_list.has_texcoord = ((x + z) & 1) != 0;
        }
    memset(matrix_model, 0, sizeof(mat4));
    matrix_model[0][0] = matrix_model[1][1] = matrix_model[2][2] = matrix_model[3][3] = 1.0F;

    /* Underfoot, behind, sideways and diagonal chunks survive total rejection,
       even at low draw distances, negative coordinates and both map seams. */
    reject_all_case(168.0F, 184.0F, 128.0F);
    reject_all_case(15.999F, 31.999F, 128.0F);
    reject_all_case(16.0F, 32.0F, 128.0F);
    reject_all_case(0.0F, 0.0F, 5.0F);
    reject_all_case(0.01F, 511.99F, 5.0F);
    reject_all_case(511.99F, 0.01F, 5.0F);
    reject_all_case(512.0F, 512.0F, 5.0F);
    reject_all_case(-0.01F, -0.01F, 5.0F);
    reject_all_case(256.0F, 256.0F, 512.0F);
    for (int i = 0; i < 120; i++)
        reject_all_case(fmodf(14.0F + i * 4.7F, 512.0F),
                        fmodf(510.0F - i * 3.1F + 512.0F, 512.0F), 128.0F);
    for (int i = 0; i < 30; i++) /* even jumps of several chunks update immediately */
        reject_all_case(fmodf(1.0F + i * 53.75F, 512.0F),
                        fmodf(510.0F + i * 97.25F, 512.0F), 128.0F);

    /* An unfinished mesh is still skipped safely until a worker uploads it. */
    camera_x = camera_z = 168.0F; settings.render_distance = 128.0F;
    struct chunk* underfoot = chunks + 10 + 10 * CHUNKS_PER_DIM;
    underfoot->created = false;
    frustum_mode = REJECT_ALL;
    reset_draws(); chunk_draw_visible();
    assert(draw_count == 24 && find_draw(10, 10) == 0);
    underfoot->created = true;

    /* Distant chunks still obey the frustum and distance limit. */
    camera_x = camera_z = 168.0F; settings.render_distance = 64.0F;
    frustum_mode = POSITIVE_X_ONLY;
    reset_draws(); chunk_draw_visible();
    assert_neighborhood();
    assert(find_draw(7, 10) == 0);  /* left, outside the guard */
    assert(find_draw(13, 10) == 1); /* right and inside the distance */
    assert(find_draw(16, 10) == 0); /* right but outside the distance */
    assert_sorted();
    frustum_mode = ACCEPT_ALL;
    reset_draws(); chunk_draw_visible();
    assert(find_draw(5, 10) == 1);
    assert(find_draw(16, 10) == 0);

    /* Shadow collection ignores the camera frustum but keeps its own range. */
    frustum_mode = REJECT_ALL;
    reset_draws(); chunk_draw_shadow(0.0F);
    assert(frustum_calls == 0);
    assert(draw_count == 5);
    assert(find_draw(10, 10) == 1 && find_draw(11, 10) == 1);
    assert(find_draw(12, 10) == 0);

    /* Use the actual extracted frustum at high FOVs, different aspect ratios,
       pitches and yaws as the camera moves across chunk boundaries. */
    frustum_mode = REAL_FRUSTUM;
    settings.render_distance = 128.0F;
    for (int step = 0; step < 80; step++) {
        camera_x = 235.0F + step * 0.63F;
        camera_y = 2.0F + (step % 4) * 0.4F;
        camera_z = 245.0F + step * 0.37F;
        float yaw = step * 0.31F;
        float pitch = -1.35F + (step % 15) * 0.15F;
        float eye[3] = {camera_x, camera_y, camera_z};
        float dir[3] = {cosf(pitch)*cosf(yaw), sinf(pitch), cosf(pitch)*sinf(yaw)};
        look_at(matrix_view, eye, dir);
        perspective(matrix_projection, (step % 2 ? 150.0F : 130.0F) * PI / 180.0F,
                    step % 3 ? 16.0F / 9.0F : 32.0F / 9.0F, 0.1F, 192.0F);
        camera_ExtractFrustum();
        reset_draws(); chunk_draw_visible();
        assert_neighborhood();
        assert_sorted();

        int cx = (int)floorf(camera_x / CHUNK_SIZE);
        int cz = (int)floorf(camera_z / CHUNK_SIZE);
        float rd = settings.render_distance + 1.414F * CHUNK_SIZE;
        for (int z = 0; z < CHUNKS_PER_DIM; z++)
            for (int x = 0; x < CHUNKS_PER_DIM; x++) {
                float dx = (x + 0.5F) * CHUNK_SIZE - camera_x;
                float dz = (z + 0.5F) * CHUNK_SIZE - camera_z;
                int near = abs(x-cx) <= 2 && abs(z-cz) <= 2;
                int in_range = dx*dx + dz*dz <= rd*rd;
                int in_frustum = real_camera_CubeInFrustum((x+0.5F)*CHUNK_SIZE, 0.0F,
                                                            (z+0.5F)*CHUNK_SIZE, CHUNK_SIZE/2, 1);
                assert(find_draw(x,z) == (near || (in_range && in_frustum)));
            }
    }
    puts("Nearby terrain remains drawn across movement, wide FOVs and wrapped seams; distant/shadow culling is intact");
    return 0;
}
""".replace("__FRUSTUM_FUNCTIONS__", frustum_functions)

with tempfile.TemporaryDirectory() as tmp:
    tmp_path = Path(tmp)
    (tmp_path / "GL").mkdir()
    (tmp_path / "GL/glew.h").write_text(GLEW_STUB)
    (tmp_path / "cglm").mkdir()
    (tmp_path / "cglm/call.h").write_text(CGLM_STUB)
    harness = tmp_path / "chunk_visibility.c"
    harness.write_text(HARNESS)
    gc_sections = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    for variant, defines in (
        ("legacy", []),
        ("core", ["-DOPENGL_CORE"]),
        # Provide GL types directly rather than depending on an installed SDL/GLES SDK.
        ("gles", ["-DOPENGL_ES", "-include", str(tmp_path / "GL/glew.h")]),
    ):
        exe = tmp_path / f"chunk_visibility_{variant}"
        subprocess.run([
            "cc", "-std=gnu11", "-O2", "-ffunction-sections", "-fdata-sections",
            *defines, "-I", str(tmp_path), "-I", str(root / "src"),
            "-I", str(root / "deps/libvxl"), str(harness), str(root / "src/chunk.c"),
            gc_sections, "-lm", "-lpthread", "-o", str(exe),
        ], check=True)
        subprocess.run([str(exe)], check=True)
