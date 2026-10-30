"""Guards against per-context GL state leaking out of a single draw call.

Run: python3 tests/texture_matrix_state_test.py

Regression: the glyph atlas used to be sampled with fixed-point texture
coordinates (`s * 8192`) plus a matching `GL_TEXTURE` matrix holding a 1/8192
scale. That matrix is *per-context global state*, so installing it once and
leaving it installed made every other textured draw in the process divide its
own texture coordinates by 8192 — which samples the texel at (0, 0) and made
every image in the client (server-list banners, flags, crosshairs, the loading
screen) collapse into a single, usually transparent, texel. Text kept working,
and solid quads kept working, because neither goes through the texture matrix.

Glyph texture coordinates are now normalised on the CPU, so no draw path owns
matrix state and identity is the only texture matrix the renderer ever needs.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
src = root / "src"
font_src = (src / "font.c").read_text()

# ---- No GL_TEXTURE matrix anywhere -----------------------------------------

offenders = []
for path in sorted(src.glob("*.c")):
    text = path.read_text()
    if "glMatrixMode(GL_TEXTURE)" in text:
        offenders.append(path.name)
assert not offenders, (
    "GL_TEXTURE matrix state is process-global; a draw path that changes it "
    "breaks every other textured draw. Offending files: %s" % offenders
)

# ---- Font texture coordinates are normalised floats ------------------------

assert "static float* font_coords_buffer;" in font_src, (
    "glyph texture coordinates must be normalised floats, not fixed-point shorts"
)
assert "sizeof(float)" in font_src.split("font_coords_buffer = malloc", 1)[1].split(";", 1)[0], (
    "the coordinate buffer must be allocated as floats"
)
assert "* 8192.0F" not in font_src, "glyph coordinates must not be pre-scaled"
assert "1.0F / 8192.0F" not in font_src, "the font must not rely on a 1/8192 scale"
assert "glTexCoordPointer(2, GL_FLOAT, 0, font_coords_buffer);" in font_src, (
    "the legacy font path must hand the shader normalised float coordinates"
)
assert "glx_default_shader_set_texcoord_scale(1.0F);" in font_src, (
    "the programmable font path must keep the texcoord scale neutral"
)

# ---- The font draw still owns its own client state -------------------------

assert "glx_batch2d_flush();" in font_src, "text must submit batched quads first"
assert "glDisable(GL_TEXTURE_2D);" in font_src, "text must restore texturing"

print("Font texture coordinates are normalised and no draw path installs a"
      " GL_TEXTURE matrix; identity is the only texture matrix the renderer uses")
