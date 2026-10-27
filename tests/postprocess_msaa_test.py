"""Regression tests for MSAA through the off-screen post-process pipeline.

Run: python3 tests/postprocess_msaa_test.py

The world used to render into a single-sampled FBO even when the window had
4x/8x MSAA, leaving geometric edges completely aliased. This compiles the real
postprocess.c MSAA component against a mocked desktop GL API and verifies target
creation, sample-count fallback/reuse, color+depth resolve, and cleanup. Source
checks also pin the main-frame integration: world -> MSAA FBO -> resolve ->
existing post-process textures.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
post = (root / "src/postprocess.c").read_text()

configure = main.index("postprocess_msaa_configure(")
world_bind = main.index("glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo);")
resolve = main.index("postprocess_msaa_resolve(postproc.fbo);")
composite = main.index("unsigned int scene_texture =", resolve)
assert configure < world_bind < resolve < composite
assert "settings.multisamples, settings.window_width" in main
assert "settings.window_height, scene_internal_format" in main
assert "GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST" in post
assert "GL_DEPTH_COMPONENT24, width, height" in post

release = main.split("static void postproc_release_targets(void) {", 1)[1].split(
    "static void postproc_release_all(void)", 1
)[0]
assert "postprocess_msaa_target()" in release
assert "postprocess_msaa_reset();" in release

# Both window backends must request samples before creating their drawable.
# Calling glEnable(GL_MULTISAMPLE) later cannot retrofit an existing window.
window = (root / "src/window.c").read_text()
sdl = window.split("#ifdef USE_SDL", 1)[1]
buffers_attr = sdl.index("SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS")
samples_attr = sdl.index("SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES")
sdl_create = sdl.index("= SDL_CreateWindow", samples_attr)
assert buffers_attr < samples_attr < sdl_create
assert "settings.multisamples > 1 ? 1 : 0" in sdl
assert "settings.multisamples > 1 ? settings.multisamples : 0" in sdl
assert window.index("glfwWindowHint(GLFW_SAMPLES") < window.index("= glfwCreateWindow")

GLEW_STUB = r"""
#ifndef GL_GLEW_STUB_H
#define GL_GLEW_STUB_H
#include <stddef.h>

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef unsigned char GLboolean;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;

#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_MAX_SAMPLES 0x8D57
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_NEAREST 0x2600
#define GL_RGBA 0x1908
#define GL_RGBA16F 0x881A
#define GL_RGBA8 0x8058

void glDeleteFramebuffers(GLsizei n, const GLuint* ids);
void glDeleteRenderbuffers(GLsizei n, const GLuint* ids);
void glGetIntegerv(GLenum pname, GLint* value);
void glGenFramebuffers(GLsizei n, GLuint* ids);
void glGenRenderbuffers(GLsizei n, GLuint* ids);
void glBindFramebuffer(GLenum target, GLuint fbo);
void glBindRenderbuffer(GLenum target, GLuint rb);
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rb_target, GLuint rb);
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum format,
                                      GLsizei width, GLsizei height);
GLenum glCheckFramebufferStatus(GLenum target);
void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                       GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                       GLbitfield mask, GLenum filter);
#endif
"""

HARNESS = r"""
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "common.h"
#include "glx.h"
#include "log.h"
#include "postprocess.h"

static bool runtime_gl3 = true;
static int driver_max_samples = 4;
static int complete_max_samples = 4;
static int next_fbo = 100;
static int next_rb = 200;
static int gen_fbo_calls = 0;
static int gen_rb_calls = 0;
static int delete_fbo_calls = 0;
static int delete_rb_calls = 0;
static int storage_calls = 0;
static int last_samples = 0;
static GLenum last_color_format = 0;
static GLenum last_depth_format = 0;
static GLint bound_read_fbo = 0;
static GLint bound_draw_fbo = 0;
static GLint bound_rb = 0;
static int blit_calls = 0;
static GLint blit_read_fbo = 0;
static GLint blit_draw_fbo = 0;
static GLbitfield blit_mask = 0;
static GLenum blit_filter = 0;
static GLint blit_width = 0;
static GLint blit_height = 0;

bool glx_version_at_least(int major, int minor) {
    (void)minor;
    return runtime_gl3 && major <= 3;
}

void glDeleteFramebuffers(GLsizei n, const GLuint* ids) {
    (void)ids;
    delete_fbo_calls += n;
}
void glDeleteRenderbuffers(GLsizei n, const GLuint* ids) {
    (void)ids;
    delete_rb_calls += n;
}
void glGetIntegerv(GLenum pname, GLint* value) {
    if (pname == GL_MAX_SAMPLES) *value = driver_max_samples;
    else if (pname == GL_FRAMEBUFFER_BINDING) *value = bound_draw_fbo;
    else if (pname == GL_RENDERBUFFER_BINDING) *value = bound_rb;
    else *value = 0;
}
void glGenFramebuffers(GLsizei n, GLuint* ids) {
    gen_fbo_calls += n;
    for (GLsizei i = 0; i < n; i++) ids[i] = (GLuint)next_fbo++;
}
void glGenRenderbuffers(GLsizei n, GLuint* ids) {
    gen_rb_calls += n;
    for (GLsizei i = 0; i < n; i++) ids[i] = (GLuint)next_rb++;
}
void glBindFramebuffer(GLenum target, GLuint fbo) {
    if (target == GL_FRAMEBUFFER || target == GL_READ_FRAMEBUFFER)
        bound_read_fbo = (GLint)fbo;
    if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
        bound_draw_fbo = (GLint)fbo;
}
void glBindRenderbuffer(GLenum target, GLuint rb) {
    (void)target;
    bound_rb = (GLint)rb;
}
void glFramebufferRenderbuffer(GLenum target, GLenum attachment, GLenum rb_target, GLuint rb) {
    (void)target; (void)attachment; (void)rb_target; (void)rb;
}
void glRenderbufferStorageMultisample(GLenum target, GLsizei samples, GLenum format,
                                      GLsizei width, GLsizei height) {
    (void)target; (void)width; (void)height;
    storage_calls++;
    last_samples = samples;
    if (storage_calls & 1) last_color_format = format;
    else last_depth_format = format;
}
GLenum glCheckFramebufferStatus(GLenum target) {
    (void)target;
    if (bound_draw_fbo && last_samples >= 2 && last_samples <= complete_max_samples)
        return GL_FRAMEBUFFER_COMPLETE;
    return 0x8CD6; /* GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT */
}
void glBlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1,
                       GLint dx0, GLint dy0, GLint dx1, GLint dy1,
                       GLbitfield mask, GLenum filter) {
    (void)sx0; (void)sy0; (void)dx0; (void)dy0;
    blit_calls++;
    blit_read_fbo = bound_read_fbo;
    blit_draw_fbo = bound_draw_fbo;
    blit_width = sx1 - sx0;
    blit_height = sy1 - sy0;
    blit_mask = mask;
    blit_filter = filter;
    assert(dx1 - dx0 == blit_width);
    assert(dy1 - dy0 == blit_height);
}

int main(void) {
    log_set_quiet(1);

    /* Legacy drivers retain the established single-sampled scene path. */
    runtime_gl3 = false;
    assert(postprocess_msaa_configure(4, 64, 64, GL_RGBA16F) == 0);
    assert(gen_fbo_calls == 0);

    /* Requesting 8x clamps to the driver's 4x maximum and restores caller GL
       state. Repeating the same configuration reuses the target exactly. */
    runtime_gl3 = true;
    bound_read_fbo = bound_draw_fbo = 777;
    bound_rb = 888;
    unsigned int first = postprocess_msaa_configure(8, 64, 64, GL_RGBA16F);
    assert(first != 0);
    assert(first == postprocess_msaa_target());
    assert(last_samples == 4);
    assert(storage_calls == 2);
    assert(last_color_format == GL_RGBA16F);
    assert(last_depth_format == GL_DEPTH_COMPONENT24);
    assert(bound_read_fbo == 777 && bound_draw_fbo == 777 && bound_rb == 888);
    assert(postprocess_msaa_configure(8, 64, 64, GL_RGBA16F) == first);
    assert(storage_calls == 2 && gen_fbo_calls == 1 && gen_rb_calls == 2);

    /* A format-specific limit retries lower counts instead of disabling all
       post-processing or repeatedly rebuilding on later frames. */
    complete_max_samples = 2;
    unsigned int second = postprocess_msaa_configure(4, 64, 64, GL_RGBA16F);
    assert(second != 0 && second != first);
    assert(last_samples == 2);
    assert(storage_calls == 8); /* two old + 4, 3, 2 sample attempts */
    assert(postprocess_msaa_configure(4, 64, 64, GL_RGBA16F) == second);
    assert(storage_calls == 8);

    /* Resolve both attachments into the established single-sampled FBO and
       leave both framebuffer bindings at that resolve target. */
    postprocess_msaa_resolve(555);
    assert(blit_calls == 1);
    assert(blit_read_fbo == (GLint)second && blit_draw_fbo == 555);
    assert(blit_mask == (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
    assert(blit_filter == GL_NEAREST);
    assert(blit_width == 64 && blit_height == 64);
    assert(bound_read_fbo == 555 && bound_draw_fbo == 555);

    /* A color-format change (HDR <-> compatibility) also recreates the target. */
    unsigned int third = postprocess_msaa_configure(4, 64, 64, GL_RGBA);
    assert(third != 0 && third != second);
    assert(last_color_format == GL_RGBA8); /* generic texture format -> sized RB format */
    postprocess_msaa_reset();
    assert(postprocess_msaa_target() == 0);
    assert(delete_fbo_calls >= 3 && delete_rb_calls >= 6);

    /* If no sample count works, cache the failure and preserve the caller's
       framebuffer instead of hammering the driver every frame. */
    complete_max_samples = 1;
    bound_read_fbo = bound_draw_fbo = 777;
    bound_rb = 888;
    int before_failure = storage_calls;
    assert(postprocess_msaa_configure(4, 128, 64, GL_RGBA16F) == 0);
    assert(storage_calls == before_failure + 6); /* 4, 3, 2, two buffers each */
    assert(bound_read_fbo == 777 && bound_draw_fbo == 777 && bound_rb == 888);
    int after_failure = storage_calls;
    assert(postprocess_msaa_configure(4, 128, 64, GL_RGBA16F) == 0);
    assert(storage_calls == after_failure);

    /* A real configuration change permits a retry and can succeed. */
    complete_max_samples = 2;
    assert(postprocess_msaa_configure(4, 128, 65, GL_RGBA16F) != 0);
    postprocess_msaa_resolve(555);
    assert(blit_calls == 2 && blit_width == 128 && blit_height == 65);

    postprocess_deinit();
    assert(postprocess_msaa_target() == 0);
    puts("Post-process MSAA allocation, fallback, resolve and cleanup verified");
    return 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    tmp_path = Path(tmp)
    gl_dir = tmp_path / "GL"
    gl_dir.mkdir()
    (gl_dir / "glew.h").write_text(GLEW_STUB)
    harness = tmp_path / "harness.c"
    harness.write_text(HARNESS)
    exe = tmp_path / "postprocess_msaa"
    subprocess.run(
        [
            "cc", "-std=gnu11", "-O2", "-Wall", "-Wextra", "-Werror",
            "-I", str(tmp_path), "-I", str(root / "src"),
            str(harness), str(root / "src/postprocess.c"), str(root / "src/log.c"),
            "-lpthread", "-o", str(exe),
        ],
        check=True,
    )
    subprocess.run([str(exe)], check=True)

print("Main-frame MSAA scene binding and pre-composite resolve verified")
