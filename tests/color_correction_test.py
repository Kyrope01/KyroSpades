"""Exercise the ZeroSpades fog tint and check both post-process shader paths."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.c').read_text()
assert main.count('COLOR_CORRECTION_GLSL') == 2
assert main.count('colorCorrect(c);') == 2
assert 'use_cc ? (postproc.hdr ? 2.0F : 1.0F) : 0.0F' in main
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
    float fog[3] = {.5F, .75F, 1.F}, smoothed[3] = {-1.F, -1.F, -1.F}, tint[3];
    color_correction_tint(smoothed, fog, tint);
    assert(fabsf(tint[0] - 1.F / (0.8F / 1.5F + 0.2F)) < 1.e-6F);
    assert(tint[0] > tint[1] && tint[1] > tint[2] && tint[2] == 1.F);
    color_correction_tint(smoothed, (float[]){0.F, 0.F, 0.F}, tint);
    assert(fabsf(smoothed[2] - .998F) < 1.e-6F);
    smoothed[0] = -1.F;
    color_correction_tint(smoothed, (float[]){.4F, .4F, .4F}, tint);
    assert(tint[0] == 1.F && tint[1] == 1.F && tint[2] == 1.F);
    assert(strstr(COLOR_CORRECTION_GLSL, "hdr ? 0.9 : 1.0"));
    assert(strstr(COLOR_CORRECTION_GLSL, "hdr ? 0.0 : 0.3"));
}
''')
    exe = src.with_suffix('')
    run(['cc', '-std=c99', '-Wall', '-Werror', '-I', str(root / 'src'),
         str(src), '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)
