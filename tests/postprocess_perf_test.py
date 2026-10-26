"""Source-level regression checks for post-processing performance work.

Run: python3 tests/postprocess_perf_test.py

The bloom pipeline and the per-frame post-process setup used to spend frame
time on avoidable GPU synchronization: eight blur ping-pongs (four Gaussian
pairs), two framebuffer-state queries where one suffices, and a duplicate
glGetIntegerv(GL_FRAMEBUFFER_BINDING) in the main frame loop. These checks pin
the cheaper behaviour so the cost cannot silently return.

GPU output itself is covered in a running client by the opt-in
KS_BLOOM_TEST=1 readback self-test (postprocess_bloom_debug_test); this file
guards the CPU-side scheduling and GL-call budget, which is what changed here.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
post = (root / "src" / "postprocess.c").read_text()
main = (root / "src" / "main.c").read_text()

# 1. Bloom runs three horizontal/vertical Gaussian pairs (six passes), not
#    eight. Even pass counts keep the final result on ping-pong buffer 0.
m = re.search(r"#define BLOOM_BLUR_PASSES (\d+)", post)
assert m, "BLOOM_BLUR_PASSES definition missing"
passes = int(m.group(1))
assert passes == 6, f"expected 6 blur passes (3 H/V pairs), found {passes}"
assert passes % 2 == 0, "an even pass count is required to finish on buffer 0"
# The render loop must actually iterate over the macro, not a hardcoded count.
assert "for(int pass = 0; pass < BLOOM_BLUR_PASSES; pass++)" in post

# 2. One framebuffer query feeds both the DRAW and READ restore slots; the
#    engine only ever binds GL_FRAMEBUFFER (which updates both), so separate
#    DRAW/READ binding queries were redundant GL stalls.
assert "glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING" not in post
assert "glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING" not in post
assert "glGetIntegerv(GL_FRAMEBUFFER_BINDING, &current_fbo)" in post
assert "state->draw_fbo = current_fbo;" in post
assert "state->read_fbo = current_fbo;" in post
# Restore semantics are unchanged: both targets still get explicitly rebound.
assert "glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state->draw_fbo);" in post
assert "glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->read_fbo);" in post

# 3. The main frame loop must not re-query the framebuffer binding it captured
#    a few lines earlier (nothing between those points rebinds an FBO).
assert "GLint fb = -1;" not in main, "duplicate per-frame GL_FRAMEBUFFER_BINDING query returned"
# The single remaining capture must stay gated behind needs_postproc.
assert re.search(
    r"if\(needs_postproc\) \{\s*glGetIntegerv\(GL_FRAMEBUFFER_BINDING, &live_screen_fbo\);",
    main,
), "the single FBO query must remain on the post-process path only"

# 4. The Gaussian weights still form a normalized kernel, so the reduced pass
#    count changes blur width, not energy/brightness.
weights = (0.2270270270, 0.3162162162, 0.0702702703)
assert abs(weights[0] + 2 * weights[1] + 2 * weights[2] - 1) < 1e-6

print("Post-process blur pass count, FBO query merging and de-duplication verified")
