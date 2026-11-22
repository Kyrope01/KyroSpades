"""Check the neutral SDR grade and both post-process shader paths."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.c').read_text()
assert main.count('COLOR_CORRECTION_GLSL') == 2
assert 'pp_cc_tint' not in main
assert main.count('colorCorrect(c);') == 2
assert 'use_cc ? 1.0F : 0.0F' in main
assert 'use_cc ? 1.0F : 1.0F + settings.saturation' in main
assert 'use_cc ? 1.0F : 1.0F + settings.exposure' in main
assert 'use_cc ? 1.0F : 1.0F + settings.contrast' in main
assert 'settings.color_correction' not in (root / 'src/map.c').read_text()

with TemporaryDirectory() as directory:
    src = Path(directory) / 'test.c'
    src.write_text(r'''
#include <assert.h>
#include <string.h>
#include "color_correction.h"
int main(void) {
    assert(!strstr(COLOR_CORRECTION_GLSL, "pp_cc_tint"));
    assert(strstr(COLOR_CORRECTION_GLSL, "0.24 * (1.0 - peak)"));
    assert(strstr(COLOR_CORRECTION_GLSL, "0.12 * (1.0 - (hi - lo))"));
    assert(!strstr(COLOR_CORRECTION_GLSL, "smoothstep"));
}
''')
    exe = src.with_suffix('')
    run(['cc', '-std=c99', '-Wall', '-Werror', '-I', str(root / 'src'),
         str(src), '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)

# Reference samples for the shader: low gray lifts without a color cast,
# endpoint colors stay put, and muted colors get a small chroma boost.
def grade(rgb):
    gain = 1 + 0.24 * (1 - max(rgb))
    rgb = tuple(c * gain for c in rgb)
    lo, hi = min(rgb), max(rgb)
    y = sum(c * w for c, w in zip(rgb, (0.299, 0.587, 0.114)))
    boost = min(0.12 * (1 - (hi - lo)), lo / max(y - lo, 0.001),
                (1 - hi) / max(hi - y, 0.001))
    return tuple(c + (c - y) * boost for c in rgb)

for rgb in ((0, 0, 0), (1, 1, 1), (1, 0, 0)):
    assert grade(rgb) == rgb
for gray in (0.08, 0.18, 0.5, 0.9):
    r, g, b = grade((gray,) * 3)
    assert r == g == b and gray < r < 1
r, g, b = grade((0.4, 0.5, 0.6))
assert 0 < r < g < b < 1 and b - r > 0.2
