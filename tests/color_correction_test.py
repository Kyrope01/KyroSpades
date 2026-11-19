"""Check the neutral ZeroSpades-derived grade and check both post-process shader paths."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.c').read_text()
assert main.count('COLOR_CORRECTION_GLSL') == 2
assert 'pp_cc_tint' not in main
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
    assert(!strstr(COLOR_CORRECTION_GLSL, "pp_cc_tint"));
    assert(strstr(COLOR_CORRECTION_GLSL, "hdr ? 0.9 : 1.0"));
    assert(strstr(COLOR_CORRECTION_GLSL, "hdr ? 0.0 : 0.3"));
}
''')
    exe = src.with_suffix('')
    run(['cc', '-std=c99', '-Wall', '-Werror', '-I', str(root / 'src'),
         str(src), '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)
