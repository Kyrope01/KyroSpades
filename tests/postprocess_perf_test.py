"""Source-level regression checks for post-processing performance work.

Run: python3 tests/postprocess_perf_test.py

Ensure the main frame loop does not repeat framebuffer binding queries.
"""
import re
from subprocess import run
from tempfile import TemporaryDirectory
from pathlib import Path

root = Path(__file__).resolve().parents[1]
post = (root / "src" / "postprocess.c").read_text()
main = (root / "src" / "main.c").read_text()

# 3. The main frame loop must not re-query the framebuffer binding it captured
#    a few lines earlier (nothing between those points rebinds an FBO).
assert "GLint fb = -1;" not in main, "duplicate per-frame GL_FRAMEBUFFER_BINDING query returned"
# The single remaining capture must stay gated behind needs_postproc.
assert re.search(
    r"if\(needs_postproc\) \{\s*glGetIntegerv\(GL_FRAMEBUFFER_BINDING, &live_screen_fbo\);",
    main,
), "the single FBO query must remain on the post-process path only"

assert 'BLOOM_BLUR_PASSES' not in post
print("Post-process FBO query de-duplication verified")

# Compile the real failed-target guard and exercise both retry paths.
start = main.index('        if(needs_postproc && postproc.target_failed) {')
end = main.index('        /* Avoid a driver-synchronising', start)
with TemporaryDirectory() as directory:
    exe = Path(directory) / 'test'
    run(['cc', '-std=c99', '-Wall', '-Werror', '-x', 'c', '-', '-o', str(exe)],
        input=r'''#include <assert.h>
static struct { int w, h, target_failed; } postproc = {800, 600, 1};
static struct { int window_width, window_height; } settings = {800, 600};
static void postproc_release_targets(void) { postproc.target_failed = 0; }
static int retry(int needs_postproc) {
''' + main[start:end] + r'''
    return needs_postproc;
}
int main(void) {
    assert(!retry(1) && postproc.target_failed);
    settings.window_height = 720;
    assert(!retry(0) && postproc.target_failed);
    assert(retry(1) && !postproc.target_failed);
    return 0;
}
''', text=True, check=True)
    run([str(exe)], check=True)
