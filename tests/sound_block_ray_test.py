"""Sound occlusion must only look at blocks, and only as far as the sound.

sound_occlusion_probe() fires 27 rays per playing source (every 4th frame) and
sound_room_probe() adds more every frame, and both used to go through
camera_hit_mask() - the gun's hit test. That meant:

  * the full player hitbox test (player_collision, with its glm_rotate_make /
    glmc_mat4_inv / sincosf work) against every player in range, per ray, even
    though audio only cares about blocks; and
  * a block walk that kept going until it hit something or left the map, even
    for a sound three blocks away.

That is the ~35% of main-thread time reported during fights, and it scales with
sounds x players. The fix routes both probes through camera_hit_terrain(): no
player test, and the walk stops at the sound's own distance.

This test compiles the real camera_terrain_pick_range()/camera_hit_terrain()/camera_block_hit() against
a fake map and checks:

  * a hit within range reports exactly the voxel a full-length walk finds, at
    the same AABB entry distance;
  * a block beyond the range is reported as no hit (that is what makes the walk
    safe to cut short);
  * the answer agrees with a brute-force ray march on thousands of random rays;
  * the walk really does stop early - map_isair() calls per ray stay bounded by
    the range instead of walking to the map edge, which is the perf claim;
  * sound.c no longer calls camera_hit_mask() at all.

Run: python3 tests/sound_block_ray_test.py
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
camera_src = (root / 'src' / 'camera.c').read_text()
sound_src = (root / 'src' / 'sound.c').read_text()

failures = 0


def check(what, ok, detail=''):
    global failures
    print('  %-62s %s' % (what, 'ok' if ok else 'FAIL'))
    if not ok:
        failures += 1
        if detail:
            print('      ' + detail)


# ── extraction: the shipped code, verbatim ──────────────────────────────────
start = camera_src.index('static int* camera_terrain_pick_range(unsigned char mode, float gx0')
end = camera_src.index('int* camera_terrain_pickEx(unsigned char mode', start)
traverse = camera_src[start:end]
assert 'tmaxx' in traverse and 'maxdist' in traverse, \
    'camera_terrain_pick_range is no longer the tMax traversal; update this test'

start = camera_src.index('int camera_block_hit(')
end = camera_src.index('\n}\n', start) + 3
block_hit = camera_src[start:end]
assert 'camera_hit_terrain(' in block_hit
terrain_start = camera_src.index('void camera_hit_terrain(')
terrain_end = camera_src.index('\n}', terrain_start) + 2
terrain_hit = camera_src[terrain_start:terrain_end]
assert 'camera_terrain_pick_range(1,' in terrain_hit

harness = r'''
#include <float.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define len3D(x, y, z) sqrt((x) * (x) + (y) * (y) + (z) * (z))

typedef struct {
    union { struct { float min_x, min_y, min_z; }; float min[3]; };
    union { struct { float max_x, max_y, max_z; }; float max[3]; };
} AABB;

typedef struct {
    union { struct { float x, y, z; }; float coords[3]; } origin;
    union { struct { float x, y, z; }; float coords[3]; } direction;
} Ray;

/* aabb.c's aabb_intersection_ray, verbatim (it is what turns the voxel into a
   distance and is what the sound code compares against its ray length). */
extern bool aabb_intersection_ray(AABB* a, Ray* r, float* distance);
bool aabb_intersection_ray(AABB* a, Ray* r, float* distance) {
    const float* min_axis = a->min;
    const float* max_axis = a->max;
    const float* origin = r->origin.coords;
    const float* direction = r->direction.coords;
    double tmin = -INFINITY, tmax = INFINITY;
    for(int axis = 0; axis < 3; axis++) {
        if(direction[axis] == 0.0F) {
            if(origin[axis] < min_axis[axis] || origin[axis] > max_axis[axis])
                return false;
            continue;
        }
        double near = (min_axis[axis] - origin[axis]) / (double)direction[axis];
        double far = (max_axis[axis] - origin[axis]) / (double)direction[axis];
        if(near > far) { double tmp = near; near = far; far = tmp; }
        tmin = fmax(tmin, near);
        tmax = fmin(tmax, far);
        if(tmax < tmin) return false;
    }
    if(tmax <= fmax(tmin, 0.0)) return false;
    if(distance)
        *distance = fmax(tmin, 0.0) * len3D(r->direction.x, r->direction.y, r->direction.z);
    return true;
}

#define CAMERA_HITTYPE_NONE 0
#define CAMERA_HITTYPE_BLOCK 1
#define CAMERA_HITTYPE_PLAYER 2

struct Camera_HitType {
    char type;
    float x, y, z, distance;
    int xb, yb, zb;
    unsigned char player_id, player_section;
};

/* ── fake map ──────────────────────────────────────────────────────────── */
#define MAP_N 32
int map_size_x = MAP_N, map_size_y = MAP_N, map_size_z = MAP_N;

static uint8_t solid[MAP_N][MAP_N][MAP_N];
static long isair_calls = 0;

bool map_isair(int x, int y, int z) {
    isair_calls++;
    if(x < 0 || x >= MAP_N || y < 0 || y >= MAP_N || z < 0 || z >= MAP_N)
        return true;
    return !solid[x][y][z];
}

static int failures = 0;
static void check(const char* what, int ok) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAIL");
    if(!ok) failures++;
}

static uint32_t rng_state = 987654321u;
static uint32_t rng(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/* --- shipped code --- */
#define CAMERA_PICK_MAXDIST 128.0F
Traverse_here
Terrain_hit_here
Block_hit_here

/* Brute force: first solid voxel along the ray, and the AABB entry distance.
   Returns 0 if nothing solid is entered before `limit` units. */
static int brute(float ox, float oy, float oz, float dx, float dy, float dz, float limit,
                 int cell[3], float* entry_d) {
    float len = len3D(dx, dy, dz);
    float ux = dx / len, uy = dy / len, uz = dz / len;
    const float step = 1.0F / 4096.0F;
    int last[3] = {(int)floorf(ox), (int)floorf(oy), (int)floorf(oz)};
    for(float t = 0.0F; t <= limit; t += step) {
        int vx = (int)floorf(ox + ux * t);
        int vy = (int)floorf(oy + uy * t);
        int vz = (int)floorf(oz + uz * t);
        if(vx < 0 || vx >= MAP_N || vy < 0 || vy >= MAP_N || vz < 0 || vz >= MAP_N)
            return 0;
        if(vx == last[0] && vy == last[1] && vz == last[2])
            continue;
        last[0] = vx; last[1] = vy; last[2] = vz;
        if(!map_isair(vx, vy, vz)) {
            cell[0] = vx; cell[1] = vy; cell[2] = vz;
            /* entry point of this voxel along the ray */
            float lo[3] = {(float)vx, (float)vy, (float)vz};
            float hi[3] = {lo[0] + 1.0F, lo[1] + 1.0F, lo[2] + 1.0F};
            float o[3] = {ox, oy, oz}, u[3] = {ux, uy, uz};
            /* slab test for this one voxel; u is unit length so t is distance */
            float tmin = 0.0F, tmax = INFINITY;
            for(int a = 0; a < 3; a++) {
                if(u[a] == 0.0F) continue;
                float t1 = (lo[a] - o[a]) / u[a];
                float t2 = (hi[a] - o[a]) / u[a];
                if(t1 > t2) { float tmp = t1; t1 = t2; t2 = tmp; }
                if(t1 > tmin) tmin = t1;
                if(t2 < tmax) tmax = t2;
            }
            *entry_d = tmin;
            return 1;
        }
    }
    return 0;
}

int main(void) {
    /* ── scene A: one wall far away, sound close to the listener ───────── */
    for(int x = 0; x < MAP_N; x++)
        for(int y = 0; y < MAP_N; y++)
            for(int z = 0; z < MAP_N; z++)
                solid[x][y][z] = 0;
    for(int y = 0; y < MAP_N; y++)
        for(int z = 0; z < MAP_N; z++)
            solid[30][y][z] = 1;

    struct Camera_HitType hit;
    /* listener at 5.5, sound at 2.5: a 3-unit ray, 27.5 units of clear air
       before the wall. */
    isair_calls = 0;
    int close_hit = camera_block_hit(&hit, 2.5F, 5.5F, 5.5F, 3.0F, 0.0F, 0.0F, 3.0F);
    long close_calls = isair_calls;
    check("open air within range: no hit", close_hit == 0 && hit.type == CAMERA_HITTYPE_NONE);

    isair_calls = 0;
    int* full = camera_terrain_pick_range(1, 2.5F, 5.5F, 5.5F, 3.0F, 0.0F, 0.0F, CAMERA_PICK_MAXDIST);
    long full_calls = isair_calls;
    check("full-length walk does find that wall", full != NULL && full[0] == 30);
    printf("  3-unit sound vs 27.5 units of air: %ld map_isair() calls ranged, %ld full-length\n",
           close_calls, full_calls);
    check("ranged walk stops early", close_calls * 4 < full_calls);
    check("ranged walk is bounded by the range", close_calls <= 8);

    /* ── scene B: a wall 2 blocks away ─────────────────────────────────── */
    for(int x = 0; x < MAP_N; x++)
        for(int y = 0; y < MAP_N; y++)
            for(int z = 0; z < MAP_N; z++)
                solid[x][y][z] = 0;
    for(int y = 0; y < MAP_N; y++)
        for(int z = 0; z < MAP_N; z++)
            solid[5][y][z] = 1;

    int wall = camera_block_hit(&hit, 2.5F, 5.5F, 5.5F, 3.0F, 0.0F, 0.0F, 10.0F);
    check("wall within range is reported", wall == 1 && hit.type == CAMERA_HITTYPE_BLOCK);
    check("wall cell is the first solid voxel", hit.x == 5 && hit.y == 5 && hit.z == 5);
    check("distance is the 2.5-unit AABB entry, not the voxel centre",
          fabsf(hit.distance - 2.5F) < 1e-4F);
    check("the cell behind the wall is reported too", hit.xb == 4 && hit.yb == 5 && hit.zb == 5);

    wall = camera_block_hit(&hit, 2.5F, 5.5F, 5.5F, 3.0F, 0.0F, 0.0F, 2.0F);
    check("a wall beyond the range is not a hit", wall == 0 && hit.type == CAMERA_HITTYPE_NONE);

    /* ── differential: random maps, random rays, against brute force ────── */
    int rays = 0, hits = 0, mismatches = 0, full_agree = 0, range_agrees = 0, ambiguous = 0;
    for(int trial = 0; trial < 3000; trial++) {
        for(int x = 0; x < MAP_N; x++)
            for(int y = 0; y < MAP_N; y++)
                for(int z = 0; z < MAP_N; z++)
                    solid[x][y][z] = (rng() % 100) < 18;

        int ox, oy, oz;
        do {
            ox = 4 + (int)(rng() % (MAP_N - 8));
            oy = 4 + (int)(rng() % (MAP_N - 8));
            oz = 4 + (int)(rng() % (MAP_N - 8));
        } while(solid[ox][oy][oz]);

        float px = ox + 0.2F + (float)(rng() % 60) / 100.0F;
        float py = oy + 0.2F + (float)(rng() % 60) / 100.0F;
        float pz = oz + 0.2F + (float)(rng() % 60) / 100.0F;
        float dx = (float)(rng() % 2001) / 1000.0F - 1.0F;
        float dy = (float)(rng() % 2001) / 1000.0F - 1.0F;
        float dz = (float)(rng() % 2001) / 1000.0F - 1.0F;
        if(fabsf(dx) + fabsf(dy) + fabsf(dz) < 1e-3F)
            continue;

        /* Sound rays look like this: unnormalised direction, length = range. */
        float range = 1.0F + (float)(rng() % 1200) / 100.0F;
        float len = len3D(dx, dy, dz);
        float ux = dx / len, uy = dy / len, uz = dz / len; /* unit direction */
        float rx = ux * range, ry = uy * range, rz = uz * range;

        int got = camera_block_hit(&hit, px, py, pz, rx, ry, rz, range);

        int cell[3] = {-1, -1, -1};
        float entry = 0.0F;
        int bhit = brute(px, py, pz, ux, uy, uz, range, cell, &entry);

        /* Rays that touch a voxel exactly on an edge or corner make "which cell
           came first" a coin toss between any two correct traversals: the ray
           never enters either interior cell. Those trials are not comparable
           (the reference march and the DDA legitimately pick different sides),
           so skip them. Test both entries: the DDA's and the reference's. */
        int boundaries = 0;
        float check_t[2] = {got ? hit.distance : -1.0F, bhit ? entry : -1.0F};
        for(int k = 0; k < 2; k++) {
            if(check_t[k] < 0.0F) continue;
            int b = 0;
            for(int a = 0; a < 3; a++) {
                float o = a == 0 ? px : a == 1 ? py : pz;
                float u = a == 0 ? ux : a == 1 ? uy : uz;
                float v = o + u * check_t[k];
                if(fabsf(v - floorf(v + 0.5F)) < 1e-3F)
                    b++;
            }
            if(b >= 2) boundaries = 1;
        }
        if(boundaries) { ambiguous++; continue; }

        rays++;
        if((got != 0) != (bhit != 0)) {
            if(mismatches++ < 5)
                printf("  MISMATCH hit/miss: block_hit=%d brute=%d  o(%.2f,%.2f,%.2f) r=%.2f\n",
                       got, bhit, px, py, pz, range);
            continue;
        }
        if(!bhit) { range_agrees++; continue; }
        hits++;
        if(hit.x != cell[0] || hit.y != cell[1] || hit.z != cell[2]
           || fabsf(hit.distance - entry) > 1e-3F) {
            if(mismatches++ < 5)
                printf("  MISMATCH voxel: got(%.1f,%.1f,%.1f d=%.3f) brute(%d,%d,%d d=%.3f)\n",
                       hit.x, hit.y, hit.z, hit.distance, cell[0], cell[1], cell[2], entry);
            continue;
        }
        range_agrees++;

        /* And it has to agree with what the uncut walk sees for this ray. */
        int* reference = camera_terrain_pick_range(1, px, py, pz, rx, ry, rz, CAMERA_PICK_MAXDIST);
        if(reference && reference[0] == cell[0] && reference[1] == cell[1] && reference[2] == cell[2])
            full_agree++;
        else if(mismatches++ < 5)
            printf("  MISMATCH vs full walk: box(%d,%d,%d) walk(%d,%d,%d)\n", cell[0], cell[1], cell[2],
                   reference ? reference[0] : -1, reference ? reference[1] : -1, reference ? reference[2] : -1);
    }
    printf("  %d random rays (%d edge/corner rays skipped), %d hits, %d clean misses, %d mismatches\n",
           rays, ambiguous, hits, rays - hits, mismatches);
    check("ranged hits agree with brute force", mismatches == 0);
    check("ranged hits agree with the uncut walk", full_agree == hits);
    check("test exercised both hits and misses", hits > 300 && (rays - hits) > 300);

    /* ── degenerate input ──────────────────────────────────────────────── */
    check("zero-length ray is not a hit",
          camera_block_hit(&hit, 5.5F, 5.5F, 5.5F, 0.0F, 0.0F, 0.0F, 10.0F) == 0);
    check("zero range is not a hit",
          camera_block_hit(&hit, 2.5F, 5.5F, 5.5F, 3.0F, 0.0F, 0.0F, 0.0F) == 0);

    printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
    return failures != 0;
}
'''

harness = harness.replace('Traverse_here', traverse).replace('Terrain_hit_here', terrain_hit).replace('Block_hit_here', block_hit)

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'sound_ray.c'
    exe = Path(d) / 'sound_ray'
    c.write_text(harness)
    subprocess.run(['cc', '-std=gnu99', '-O2', '-Wall', '-Wno-unused-function', str(c), '-lm',
                    '-o', str(exe)], check=True)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=600)
    out = result.stdout
    for line in out.splitlines():
        if line.startswith('  '):
            print(line)
            if line.rstrip().endswith('FAIL'):
                failures += 1
        elif line.strip():
            print(line)
    if result.stderr:
        print(result.stderr, end='')
    if result.returncode != 0 or 'all checks passed' not in out:
        failures += 1

# ── the probes themselves ──────────────────────────────────────────────────
print()
check('sound.c no longer runs the gun hit test', 'camera_hit_mask(' not in sound_src)
check('occlusion probe uses the block-only ray',
      'camera_hit_terrain(&hit, ox, oy, oz, rx, ry, rz, sample_dist);' in sound_src)
check('room probe uses the block-only ray',
      sound_src.count('camera_hit_terrain(&hit, camera_x, camera_y, camera_z,') == 2)
check('occlusion still asks only about blocks',
      'hit.type == CAMERA_HITTYPE_BLOCK && hit.distance < sample_dist - 0.5F' in sound_src)

print()
if failures:
    raise SystemExit('sound block ray tests failed')
print('Sound block ray tests passed')
