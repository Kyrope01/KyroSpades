"""Compile real KyroSpades sound_update() and check the OpenAL listener basis
is never degenerate.

OpenAL builds the listener basis from a forward vector and an up vector that
must not be parallel. The up vector used to be hardcoded to (0, 1, 0), which is
exactly the forward direction when looking straight up (camera_rot_y clamps to
EPSILON) or straight down (clamps to 3.14) -- see camera_overflow_adjust() in
src/camera.c. Same class of fix as rzrn/tigerspades src/sound.c.

Run: python3 tests/sound_orientation_test.py
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src' / 'sound.c').read_text()

start = source.index('void sound_update() {')
end = source.index('\n}\n', start) + len('\n}\n')
handler = source[start:end]
assert 'alListenerfv(AL_ORIENTATION, orientation)' in handler, \
    'sound_update no longer sets AL_ORIENTATION; update this test'

harness = r'''
#define USE_SOUND 1
#include <assert.h>
#include <math.h>
#include <stdio.h>

#ifndef M_PI            /* hidden by -std=c99 without _GNU_SOURCE */
#define M_PI 3.14159265358979323846
#endif

#define AL_POSITION   0x1004
#define AL_VELOCITY   0x1006
#define AL_ORIENTATION 0x100F
#define SOUND_SCALE 0.6F
#define EPSILON 0.005F          /* matches src/common.h */
#define PITCH_MAX 3.14F         /* matches camera_overflow_adjust() */

/* --- the globals and callees sound_update() touches --- */
static int sound_enabled = 1;
static int sound_frame = 0;
static int efx_ok = 0;          /* keeps the EFX path out of the test */
float camera_rot_x = 0.0F, camera_rot_y = (float)M_PI / 2.0F;
float camera_x = 0.0F, camera_y = 0.0F, camera_z = 0.0F;
float camera_vx = 0.0F, camera_vy = 0.0F, camera_vz = 0.0F;

static float captured[6];
static int captures = 0;

static void alListener3f(int param, float a, float b, float c) {
    (void)param; (void)a; (void)b; (void)c;
}
static void alListenerfv(int param, const float* values) {
    if(param == AL_ORIENTATION) {
        for(int i = 0; i < 6; i++) captured[i] = values[i];
        captures++;
    }
}
struct entity_system { int dummy; };
static struct entity_system sound_sources;
static int sound_update_single(void* object, void* user) { (void)object; (void)user; return 1; }
static void entitysys_iterate(struct entity_system* es, void* user,
                              int (*cb)(void*, void*)) { (void)es; (void)user; (void)cb; }
static void sound_room_probe(void) {}
static void sound_room_apply(void) {}

/* --- the real function, copied verbatim out of src/sound.c --- */
HANDLER_HERE

static float dot3(const float* a, const float* b) {
    return a[0]*b[0] + a[1]*b[1] + a[2]*b[2];
}
static float len3(const float* a) { return sqrtf(dot3(a, a)); }

static int failures = 0;
static void check(const char* what, int ok) {
    printf("  %-56s %s\n", what, ok ? "ok" : "FAIL");
    if(!ok) failures++;
}

int main(void) {
    /* Sweep the whole reachable pitch range at several yaws. */
    float worst_dot = 0.0F;
    int samples = 0;
    for(int yi = 0; yi < 8; yi++) {
        camera_rot_x = (float)M_PI * 2.0F * (float)yi / 8.0F;
        for(int pi = 0; pi <= 180; pi++) {
            /* cover the clamp endpoints exactly, plus a fine sweep between */
            camera_rot_y = (pi == 0)   ? EPSILON
                         : (pi == 180) ? PITCH_MAX
                                       : EPSILON + (PITCH_MAX - EPSILON) * (float)pi / 180.0F;
            sound_update();
            float d = fabsf(dot3(captured, captured + 3));
            if(d > worst_dot) worst_dot = d;
            samples++;
        }
    }
    printf("%d listener orientations checked\n", samples);
    printf("  worst |forward . up| = %.9f\n", worst_dot);
    check("forward and up are perpendicular everywhere", worst_dot < 1e-5F);

    /* Both must stay unit length or OpenAL's basis is skewed. */
    camera_rot_x = 1.234F; camera_rot_y = 0.7F;
    sound_update();
    printf("  |forward| = %.9f   |up| = %.9f\n", len3(captured), len3(captured + 3));
    check("forward is unit length", fabsf(len3(captured) - 1.0F) < 1e-6F);
    check("up is unit length", fabsf(len3(captured + 3) - 1.0F) < 1e-6F);

    /* Behavioural continuity: at horizontal pitch the old hardcoded
       (0, 1, 0) was already correct, so the derived up must match it. */
    camera_rot_y = (float)M_PI / 2.0F;
    camera_rot_x = 2.04F;
    sound_update();
    printf("  up at horizontal pitch = (%.6f, %.6f, %.6f)\n",
           captured[3], captured[4], captured[5]);
    check("up reduces to (0, 1, 0) when looking horizontally",
          fabsf(captured[3]) < 1e-6F && fabsf(captured[4] - 1.0F) < 1e-6F
                  && fabsf(captured[5]) < 1e-6F);

    /* The two degenerate poles must now be perpendicular. */
    for(int k = 0; k < 2; k++) {
        camera_rot_x = 0.7F;
        camera_rot_y = k ? PITCH_MAX : EPSILON;
        sound_update();
        float d = dot3(captured, captured + 3);
        printf("  pitch %-8s forward=(%+.5f,%+.5f,%+.5f) up=(%+.5f,%+.5f,%+.5f) dot=%+.9f\n",
               k ? "3.14" : "EPSILON", captured[0], captured[1], captured[2],
               captured[3], captured[4], captured[5], d);
        check(k ? "straight down: basis is not degenerate"
                : "straight up: basis is not degenerate",
              fabsf(d) < 1e-5F);
    }

    /* What the old hardcoded up produced at the same poles, for contrast. */
    printf("\n  for contrast, the old hardcoded up (0,1,0) gave:\n");
    for(int k = 0; k < 2; k++) {
        float ry = k ? PITCH_MAX : EPSILON;
        float f[3] = {sinf(0.7F) * sinf(ry), cosf(ry), cosf(0.7F) * sinf(ry)};
        float up[3] = {0.0F, 1.0F, 0.0F};
        printf("    pitch %-8s forward . up = %+.6f  (|forward| = %.6f)\n",
               k ? "3.14" : "EPSILON", dot3(f, up), len3(f));
    }

    check("sound_update actually set AL_ORIENTATION", captures > 0);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'sound_orientation.c'
    exe = Path(d) / 'sound_orientation'
    c.write_text(harness.replace('HANDLER_HERE', handler))
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', str(c), '-lm',
                    '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0 or 'FAIL' in result.stdout \
            or 'all checks passed' not in result.stdout:
        raise SystemExit('sound orientation tests failed')

print('Sound listener orientation tests passed')
