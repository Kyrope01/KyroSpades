"""Source-level regression checks for post-processing performance work.

Run: python3 tests/postprocess_perf_test.py

Ensure the main frame loop does not repeat framebuffer binding queries.
"""
import re
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
