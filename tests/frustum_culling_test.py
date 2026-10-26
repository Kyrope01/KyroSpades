"""Regression test for the camera_CubeInFrustum AABB bottom-extent bug.

Run: python3 tests/frustum_culling_test.py

The chunk and water render passes call camera_CubeInFrustum() with the chunk's
real bottom as the base y (always 0 today) and an upward extent size_y. The
function used (y - size) for its bottom corners, so the tested cube jutted
`size` blocks (8 for a chunk) below the actual AABB. It could therefore never
false-cull current terrain (nothing exists below y = 0), but it over-included
chunks and would cull real geometry as soon as negative-depth terrain existed.

This test compiles the REAL camera_ExtractFrustum / camera_PointInFrustum /
camera_CubeInFrustum functions extracted from src/camera.c (no GL headers
needed — they are pure frustum math) and verifies over 300,000 randomized
cameras and AABBs, plus 5,000 steep downward views, that:
  1. every AABB with a real corner inside the frustum is kept (zero false
     culls, the bug that would open holes in the terrain), and
  2. the visibility decision matches an exact 8-corner reference exactly.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
camera_src = (root / "src" / "camera.c").read_text()


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


cube_body = extract_function(camera_src, "camera_CubeInFrustum")
# Source-level guard: bottom corners must sit on the real base y, never y-size.
assert "* (y - size)" not in cube_body, (
    "camera_CubeInFrustum bottom corners must use the real base y, not y - size"
)
assert cube_body.count("frustum[p][1] * (y) +") == 4, (
    "all four bottom AABB corners must be tested at the base y"
)

extracted = "\n\n".join((
    extract_function(camera_src, "camera_ExtractFrustum"),
    extract_function(camera_src, "camera_PointInFrustum"),
    cube_body,
))

HARNESS = r"""
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef float vec4[4];
typedef vec4 mat4[4]; /* column-major, cglm-compatible layout */

static mat4 matrix_view, matrix_model, matrix_projection;
float frustum[6][4];

void matrix_load(mat4 m, mat4 n) { memcpy(m, n, sizeof(mat4)); }

/* cglm semantics used by matrix.c: m = n * m */
void matrix_multiply(mat4 m, mat4 n) {
    mat4 r;
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++) s += n[k][row] * m[c][k];
            r[c][row] = s;
        }
    memcpy(m, r, sizeof(mat4));
}

__EXTRACTED__

/* Exact reference: AABB [x-size,x+size] x [y,y+size_y] x [z-size,z+size].
   Culled only when all 8 real corners are outside one shared plane. */
static int reference_cube(float x, float y, float z, float size, float size_y) {
    float xs[2] = {x - size, x + size};
    float ys[2] = {y, y + size_y};
    float zs[2] = {z - size, z + size};
    for (int p = 0; p < 6; p++) {
        int inside = 0;
        for (int i = 0; i < 2 && !inside; i++)
            for (int j = 0; j < 2 && !inside; j++)
                for (int k = 0; k < 2 && !inside; k++) {
                    float d = frustum[p][0] * xs[i] + frustum[p][1] * ys[j]
                            + frustum[p][2] * zs[k] + frustum[p][3];
                    if (d > 0.0f) inside = 1;
                }
        if (!inside) return 0;
    }
    return 1;
}

static int aabb_has_inside_point(float x, float y, float z, float size, float size_y) {
    float xs[2] = {x - size, x + size};
    float ys[2] = {y, y + size_y};
    float zs[2] = {z - size, z + size};
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            for (int k = 0; k < 2; k++)
                if (camera_PointInFrustum(xs[i], ys[j], zs[k]))
                    return 1;
    return 0;
}

static void perspective(mat4 m, float fovy, float aspect, float near, float far) {
    memset(m, 0, sizeof(mat4));
    float f = 1.0f / tanf(fovy / 2.0f);
    m[0][0] = f / aspect;
    m[1][1] = f;
    m[2][2] = (far + near) / (near - far);
    m[2][3] = -1.0f;
    m[3][2] = (2.0f * far * near) / (near - far);
}

static void normalize3(float* v) {
    float l = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    v[0] /= l; v[1] /= l; v[2] /= l;
}

static void look_at(mat4 m, const float eye[3], const float center[3], const float up[3]) {
    float f[3] = {center[0]-eye[0], center[1]-eye[1], center[2]-eye[2]};
    normalize3(f);
    float s[3] = {f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0]};
    normalize3(s);
    float u[3] = {s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0]};
    memset(m, 0, sizeof(mat4));
    m[0][0] = s[0]; m[1][0] = s[1]; m[2][0] = s[2];
    m[0][1] = u[0]; m[1][1] = u[1]; m[2][1] = u[2];
    m[0][2] = -f[0]; m[1][2] = -f[1]; m[2][2] = -f[2];
    m[3][0] = -(s[0]*eye[0] + s[1]*eye[1] + s[2]*eye[2]);
    m[3][1] = -(u[0]*eye[0] + u[1]*eye[1] + u[2]*eye[2]);
    m[3][2] = f[0]*eye[0] + f[1]*eye[1] + f[2]*eye[2];
    m[3][3] = 1.0f;
}

static uint64_t rng_state = 0x123456789abcdefULL;
static float rnd(float lo, float hi) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    float t = (float)(rng_state >> 11) * (1.0f / 9007199254740992.0f);
    return lo + t * (hi - lo);
}

int main(void) {
    memset(matrix_model, 0, sizeof(mat4));
    matrix_model[0][0] = matrix_model[1][1] =
        matrix_model[2][2] = matrix_model[3][3] = 1.0f;

    const int N = 300000;
    int disagreements = 0, false_culls = 0;

    for (int iter = 0; iter < N; iter++) {
        float eye[3] = {rnd(0, 512), rnd(1, 60), rnd(0, 512)};
        float yaw = rnd(0, 6.2831853f);
        float pitch = rnd(-1.55f, 1.55f);
        float dir[3] = {cosf(pitch) * cosf(yaw), sinf(pitch),
                        cosf(pitch) * sinf(yaw)};
        float center[3] = {eye[0] + dir[0], eye[1] + dir[1], eye[2] + dir[2]};
        float up[3] = {0, 1, 0};
        look_at(matrix_view, eye, center, up);
        perspective(matrix_projection, rnd(0.5f, 1.8f), 1.333f, 0.1f, 320.0f);
        camera_ExtractFrustum();

        /* Match the three real call-site shapes: chunk, water tile, player. */
        float size, base_y, h;
        int kind = iter % 3;
        if (kind == 0) {
            size = 8.0f;
            base_y = 0.0f;
            h = (float)(1 << (int)rnd(0, 6));
        } else if (kind == 1) {
            size = 8.0f;
            base_y = 0.0f;
            h = 2.0f;
        } else {
            size = 1.0f;
            base_y = rnd(0, 40);
            h = 2.0f;
        }
        float cx = rnd(-64, 576), cz = rnd(-64, 576);

        int ref = reference_cube(cx, base_y, cz, size, h);
        int got = camera_CubeInFrustum(cx, base_y, cz, size, h);
        int real_inside = aabb_has_inside_point(cx, base_y, cz, size, h);

        if ((got != 0) != (ref != 0)) disagreements++;
        if (real_inside && !got) false_culls++;
    }

    /* Targeted: single-block floor chunks seen while looking steeply down,
       the exact view in which missing ground was reported. */
    int targeted_bad = 0;
    for (int t = 0; t < 5000; t++) {
        float eye[3] = {rnd(0, 512), rnd(2, 20), rnd(0, 512)};
        float yaw = rnd(0, 6.2831853f);
        float pitch = rnd(0.9f, 1.55f);
        float dir[3] = {cosf(pitch)*cosf(yaw), sinf(pitch), cosf(pitch)*sinf(yaw)};
        float center[3] = {eye[0]+dir[0], eye[1]+dir[1], eye[2]+dir[2]};
        float up[3] = {0, 1, 0};
        look_at(matrix_view, eye, center, up);
        perspective(matrix_projection, rnd(0.7f, 1.4f), 1.333f, 0.1f, 256.0f);
        camera_ExtractFrustum();
        float cx = floorf(eye[0] / 16) * 16 + 8 + rnd(-32, 32);
        float cz = floorf(eye[2] / 16) * 16 + 8 + rnd(-32, 32);
        if (aabb_has_inside_point(cx, 0, cz, 8.0f, 1.0f)
            && !camera_CubeInFrustum(cx, 0, cz, 8.0f, 1.0f))
            targeted_bad++;
    }

    printf("random cases: %d, reference disagreements: %d, false culls: %d\n",
           N, disagreements, false_culls);
    printf("steep-down floor false culls: %d/5000\n", targeted_bad);
    if (disagreements || false_culls || targeted_bad) return 1;
    puts("OK: camera_CubeInFrustum matches the exact AABB reference with zero false culls");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    harness = Path(tmp) / "frustum_harness.c"
    harness.write_text(HARNESS.replace("__EXTRACTED__", extracted))
    exe = Path(tmp) / "frustum_test"
    cmd = [
        "cc", "-std=gnu11", "-O2", "-Wall", "-Wextra",
        "-I", str(root / "src"), "-I", str(root / "deps/libvxl"),
        str(harness), "-lm", "-o", str(exe),
    ]
    subprocess.run(cmd, check=True)
    subprocess.run([str(exe)], check=True)
