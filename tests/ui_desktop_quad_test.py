"""Verify that classic GL receives visible, untextured menu quads."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
source = (root / "src/glx.c").read_text()
quad = source[source.index("void glx_draw_quad_2d("):source.index("/* Vertical gradient quad")]
harness = r'''
#include <assert.h>
#define GL_TEXTURE_2D 1
#define GL_QUADS 2
typedef int GLboolean;
static int batch2d_enabled, textured, n;
static float vertices[4][2];
static void batch2d_add_quad(float x, float y, float w, float h) { assert(0); }
static void glx_client_state_invalidate(void) {}
static void glx_stats_draw(void) {}
static int glIsEnabled(int cap) { assert(cap == GL_TEXTURE_2D); return textured; }
static void glDisable(int cap) { assert(cap == GL_TEXTURE_2D); textured = 0; }
static void glEnable(int cap) { assert(cap == GL_TEXTURE_2D); textured = 1; }
static void glBegin(int mode) { assert(mode == GL_QUADS && !textured); n = 0; }
static void glVertex2f(float x, float y) {
    assert(n < 4);
    vertices[n][0] = x;
    vertices[n++][1] = y;
}
static void glEnd(void) {
    assert(n == 4);
    float area = 0;
    for (int i = 0; i < 4; i++) {
        int j = (i + 1) % 4;
        area += vertices[i][0] * vertices[j][1] - vertices[i][1] * vertices[j][0];
    }
    assert(area == 2 * 80 * 3); /* CCW: survives GL_BACK / GL_CCW culling. */
}
''' + quad + r'''
int main(void) {
    for (int enabled = 0; enabled <= 1; enabled++) {
        textured = enabled;
        glx_draw_quad_2d(11.5f, 200, 80, 3);
        assert(textured == enabled);
    }
}
'''
with TemporaryDirectory() as tmp:
    path = Path(tmp) / "quad.c"
    executable = Path(tmp) / "quad"
    path.write_text(harness)
    run(["cc", "-std=c99", str(path), "-o", str(executable)], check=True)
    run([str(executable)], check=True)
print("Classic desktop GL menu quad winding and texture state passed")
