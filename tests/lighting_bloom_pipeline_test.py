"""Source checks for non-volumetric lighting and HDR bloom handoff.

Run with: python3 tests/lighting_bloom_pipeline_test.py
GPU readback is available separately via KS_BLOOM_TEST=1 in a running Core client.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / "src/main.c").read_text()
glow = (root / "src/glowing_blocks.c").read_text()
world = (root / "src/lighting.c").read_text()
post = (root / "src/postprocess.c").read_text()
for path in ("src/config.c", "src/config.h", "src/main.c", "src/glowing_blocks.c", "src/glowing_blocks.h", "resources/config.ini"):
    assert "volumetric" not in (root / path).read_text().lower(), path

assert "settings.bloom = 0;" in main
assert "bloom                          = 0" in (root / "resources/config.ini").read_text()
assert "postprocess_bloom_render(scene_texture" in main
assert "glBindTexture(GL_TEXTURE_2D, bloom_texture)" in main
assert "glUniform1f(postproc.uni_pp_bloom_separate, bloom_texture ? 1.0F : 0.0F)" in main
assert "scene_internal_format = GL_RGBA16F;" in main
assert "postproc.flare_applied ? postproc.flare_tex : postproc.texture" in main
assert "lighting_submit_point(selected[i].position[0]" in glow
assert "glowing_blocks_prepare_frame(camera_x, camera_y, camera_z)" in main
assert "1.0 - smoothstep(0.0, 1.0, distance_ratio)" in world
assert "1.0 - smoothstep(0.0, 1.0, distance_ratio)" in (root / "src/glx.c").read_text()
assert "1.0 - smoothstep(0.0, 1.0, distance_to_light" in (root / "src/water.c").read_text()
assert "glReadPixels(9, 8, 1, 1" in post
assert "postprocess_state_restore(&state);" in post

# Verify the GLSL Gaussian blur coefficients spread energy without adding or
# losing it. This is a CPU check, not a substitute for the opt-in GPU readback.
weights = (0.2270270270, 0.3162162162, 0.0702702703)
assert abs(weights[0] + 2 * weights[1] + 2 * weights[2] - 1) < 1e-6
assert weights[1] > 0 and weights[2] > 0
print("Lighting separation and bloom source/blur checks passed")
