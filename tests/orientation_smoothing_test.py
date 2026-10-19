"""Compile real KyroSpades src/player.c and check that remote player
orientation smoothing settles at a rate independent of the call rate.

Ported from zerospades/zerospades PR #56.

Run: python3 tests/orientation_smoothing_test.py

player.c pulls in common.h, which needs the GL headers. On a machine with the
GL dev packages installed nothing else is needed; otherwise point the compiler
at them (and pick a backend) with KS_CC_FLAGS, e.g.

    KS_CC_FLAGS="-DOPENGL_ES -DUSE_SDL -I /path/to/sysinc" \\
        python3 tests/orientation_smoothing_test.py

Only network_correction_rate() and settings are reachable from the code under
test; the other ~120 symbols player.c imports from sibling translation units
are satisfied with generated weak stubs so the link resolves without dragging
in the rest of the engine.
"""
import os
import re
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
test_c = root / 'tests' / 'orientation_smoothing_test.c'

FLAGS = [
    '-I', str(root / 'src'),
    '-I', str(root / 'deps'),
    '-I', str(root / 'deps' / 'libvxl'),
    '-DGIT_COMMIT_HASH="t"',
    '-DKYROSPADES_MAJOR=0', '-DKYROSPADES_MINOR=1', '-DKYROSPADES_PATCH=5',
    '-DKYROSPADES_VERSION="v0.1.5"',
]
FLAGS += os.environ.get('KS_CC_FLAGS', '').split()

# libc/libm symbols player.c needs real implementations of.
LIBC_MATH = {
    'cos', 'cosf', 'sin', 'sinf', 'tan', 'tanf', 'exp', 'expf', 'pow', 'powf',
    'sqrt', 'sqrtf', 'floor', 'floorf', 'ceil', 'ceilf', 'fabs', 'fabsf',
    'fmax', 'fmaxf', 'fmin', 'fminf', 'atan2', 'atan2f', 'log', 'logf',
    'round', 'roundf', 'memcpy', 'memset', 'memmove', 'strlen', 'strcmp',
    'strncmp', 'strcpy', 'strncpy', 'sprintf', 'snprintf', 'printf', 'puts',
    'qsort', 'rand', 'srand', 'malloc', 'free', 'calloc', 'realloc',
}

with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    obj = d / 'player.o'
    stubs = d / 'stubs.c'
    exe = d / 'orientation_smoothing_test'

    subprocess.run(['cc', '-c', '-std=gnu99', *FLAGS, str(root / 'src' / 'player.c'),
                    '-o', str(obj)], check=True)

    out = subprocess.run(['nm', '-u', str(obj)], check=True,
                         capture_output=True, text=True).stdout
    undefined = sorted({line.split()[-1].lstrip('@') for line in out.splitlines()
                        if line.strip()})
    to_stub = [s for s in undefined
               if s and not s.startswith('_') and s not in LIBC_MATH]

    stubs.write_text(
        '/* generated: weak stubs for symbols player.c imports from sibling\n'
        ' * translation units. Weak so the real libc/libm definitions win. */\n'
        + ''.join('__attribute__((weak)) void %s(void) {}\n' % s for s in to_stub))

    subprocess.run(['cc', '-std=gnu99', '-Wall', *FLAGS,
                    str(test_c), str(root / 'src' / 'player.c'), str(stubs),
                    '-lm', '-o', str(exe)], check=True)

    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0:
        raise SystemExit('orientation smoothing tests failed')

    # Guard against a silently vacuous run.
    for expected in ('all checks passed', 'new spread', '60 fps tuning is preserved'):
        if expected not in result.stdout:
            raise SystemExit('test output missing %r' % expected)
    if re.search(r'\bFAIL\b', result.stdout):
        raise SystemExit('test reported failures')

print('Orientation smoothing tests passed')
