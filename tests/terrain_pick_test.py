"""Differential test for the real camera_terrain_pickEx() voxel traversal.

src/camera.c says its traversal is Amanatides & Woo and that it replaced the
old BetterSpades error-term DDA. rzrn/tigerspades 02c0f50 made the same
replacement, so rather than take the comment on faith this exercises the
shipped function against a brute-force ray march over a pseudo-random map and
checks they agree on the hit voxel and the cell before it.

Run: python3 tests/terrain_pick_test.py
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / 'src' / 'camera.c').read_text()

start = source.index('int* camera_terrain_pickEx(unsigned char mode,')
end = source.index('\n}\n', start) + len('\n}\n')
ddasrc = source[start:end]
assert 'tmaxx' in ddasrc and 'INFINITY' in ddasrc, \
    'camera_terrain_pickEx is no longer the tMax/dDelta traversal; update this test'

harness = r'''
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define MAP_N 32
int map_size_x = MAP_N, map_size_y = MAP_N, map_size_z = MAP_N;

/* Deterministic pseudo-random occupancy, ~30% solid. */
static uint8_t solid[MAP_N][MAP_N][MAP_N];
static uint32_t rng_state = 12345;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}
bool map_isair(int x, int y, int z) {
    if(x < 0 || x >= MAP_N || y < 0 || y >= MAP_N || z < 0 || z >= MAP_N)
        return true;
    return !solid[x][y][z];
}

/* --- the real function, copied verbatim out of src/camera.c --- */
DDA_HERE

/* Brute-force reference: march in small steps, recording each voxel entered. */
static int brute(float ox, float oy, float oz, float dx, float dy, float dz,
                 int hitv[3], int prev[3]) {
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    dx /= len; dy /= len; dz /= len;

    int last[3] = {(int)floorf(ox), (int)floorf(oy), (int)floorf(oz)};
    int prevv[3] = {last[0], last[1], last[2]};
    /* Step must be finer than the closest pair of boundary crossings the
       reference will be asked about: a corner crossing puts two crossings
       ~1e-4 apart, and a coarser step silently skips the voxel between them
       and blames the traversal. */
    const float step = 1.0F / 16384.0F;
    const float maxd = 20.0F;

    for(float t = 0.0F; t <= maxd; t += step) {
        int vx = (int)floorf(ox + dx * t);
        int vy = (int)floorf(oy + dy * t);
        int vz = (int)floorf(oz + dz * t);

        if(vx < 0 || vx >= MAP_N || vy < 0 || vy >= MAP_N || vz < 0 || vz >= MAP_N)
            return 0; /* left the map */

        if(vx != last[0] || vy != last[1] || vz != last[2]) {
            prevv[0] = last[0]; prevv[1] = last[1]; prevv[2] = last[2];
            last[0] = vx; last[1] = vy; last[2] = vz;
        }
        if(!map_isair(vx, vy, vz)) {
            hitv[0] = vx; hitv[1] = vy; hitv[2] = vz;
            prev[0] = prevv[0]; prev[1] = prevv[1]; prev[2] = prevv[2];
            return 1;
        }
    }
    return -1; /* ran out of range before deciding -- not comparable */
}

int main(void) {
    for(int x = 0; x < MAP_N; x++)
        for(int y = 0; y < MAP_N; y++)
            for(int z = 0; z < MAP_N; z++)
                solid[x][y][z] = (rng() % 100) < 30;

    int rays = 0, hits = 0, misses = 0, mismatch = 0;
    int axis_aligned = 0;

    for(int trial = 0; trial < 4000; trial++) {
        /* origin in an air cell */
        int ox, oy, oz;
        do {
            ox = 4 + (int)(rng() % (MAP_N - 8));
            oy = 4 + (int)(rng() % (MAP_N - 8));
            oz = 4 + (int)(rng() % (MAP_N - 8));
        } while(solid[ox][oy][oz]);

        float px = ox + 0.1F + (float)(rng() % 80) / 100.0F;
        float py = oy + 0.1F + (float)(rng() % 80) / 100.0F;
        float pz = oz + 0.1F + (float)(rng() % 80) / 100.0F;

        float dx, dy, dz;
        if(trial % 5 == 0) {
            /* axis-aligned and near-axis-aligned: the directions the old
               error-term DDA was reported to misbehave on */
            dx = dy = dz = 0.0F;
            switch(trial % 15 / 5) {
                case 0: dx = (rng() & 1) ? 1.0F : -1.0F; break;
                case 1: dy = (rng() & 1) ? 1.0F : -1.0F; break;
                default: dz = (rng() & 1) ? 1.0F : -1.0F; break;
            }
            if(trial % 15 >= 9) dx += 1e-4F; /* nudge off the axis */
            axis_aligned++;
        } else {
            dx = (float)(rng() % 2001) / 1000.0F - 1.0F;
            dy = (float)(rng() % 2001) / 1000.0F - 1.0F;
            dz = (float)(rng() % 2001) / 1000.0F - 1.0F;
        }
        if(fabsf(dx) + fabsf(dy) + fabsf(dz) < 1e-6F)
            continue;

        int bh[3], bp[3];
        int bhit = brute(px, py, pz, dx, dy, dz, bh, bp);
        if(bhit < 0)
            continue; /* reference ran out of range; nothing to compare */

        rays++;
        int* got = camera_terrain_pickEx(1, px, py, pz, dx, dy, dz);

        if((got != NULL) != (bhit != 0)) {
            if(mismatch++ < 5)
                printf("  MISMATCH hit/miss: dda=%s brute=%s  origin (%.3f,%.3f,%.3f) dir (%.3f,%.3f,%.3f)\n",
                       got ? "hit" : "miss", bhit ? "hit" : "miss", px, py, pz, dx, dy, dz);
            continue;
        }
        if(!bhit) { misses++; continue; }
        hits++;
        if(got[0] != bh[0] || got[1] != bh[1] || got[2] != bh[2]
           || got[3] != bp[0] || got[4] != bp[1] || got[5] != bp[2]) {
            if(mismatch++ < 5)
                printf("  MISMATCH voxel: dda hit(%d,%d,%d) pre(%d,%d,%d)  brute hit(%d,%d,%d) pre(%d,%d,%d)\n"
                       "      origin (%.3f,%.3f,%.3f) dir (%.3f,%.3f,%.3f)\n",
                       got[0], got[1], got[2], got[3], got[4], got[5],
                       bh[0], bh[1], bh[2], bp[0], bp[1], bp[2],
                       px, py, pz, dx, dy, dz);
        }
    }

    printf("%d comparable rays (%d axis/near-axis-aligned), %d hits, %d map exits,\n"
           "%d mismatches\n", rays, axis_aligned, hits, misses, mismatch);

    int failures = 0;
    printf("  %-58s %s\n", "traversal agrees with brute force on every ray",
           mismatch == 0 ? "ok" : "FAIL");
    if(mismatch) failures++;
    printf("  %-58s %s\n", "test actually exercised hits", hits > 500 ? "ok" : "FAIL");
    if(hits <= 500) failures++;
    printf("  %-58s %s\n", "test exercised axis-aligned rays",
           axis_aligned > 200 ? "ok" : "FAIL");
    if(axis_aligned <= 200) failures++;

    /* Degenerate direction must not divide by zero or loop forever. */
    int* degen = camera_terrain_pickEx(1, 8.5F, 8.5F, 8.5F, 0.0F, 0.0F, 0.0F);
    printf("  %-58s %s\n", "zero-length direction returns NULL",
           degen == NULL ? "ok" : "FAIL");
    if(degen != NULL) failures++;

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'terrain_pick.c'
    exe = Path(d) / 'terrain_pick'
    c.write_text(harness.replace('DDA_HERE', ddasrc))
    subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', str(c), '-lm',
                    '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=600)
    print(result.stdout, end='')
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0 or 'FAIL' in result.stdout \
            or 'all checks passed' not in result.stdout:
        raise SystemExit('terrain pick tests failed')

print('Voxel traversal tests passed')
