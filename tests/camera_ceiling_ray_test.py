"""Verify rays that start above the map (Babel heaven, build ceiling) are
clipped into the map instead of being rejected, while normal in-map rays and
outward misses keep their existing behaviour."""
from pathlib import Path
import subprocess, tempfile

root = Path(__file__).resolve().parents[1]
src = (root / 'src/camera.c').read_text()
start = src.index('static int* camera_terrain_pick_range(unsigned char mode, float gx0')
end = src.index('\nint* camera_terrain_pickEx(')
traverse = src[start:end]

stub = r'''
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

int map_size_x = 16, map_size_y = 64, map_size_z = 16;
/* Solid slab at y = 40; everything else is air. */
bool map_isair(int x, int y, int z) { (void)x; (void)z; return y != 40; }
'''

checks = r'''
int main(void) {
    int* p;

    /* Player above the map ceiling shooting straight down (Babel heaven):
       must hit the slab, not return NULL. */
    p = camera_terrain_pick_range(1, 8.5F, 70.0F, 8.5F, 0.0F, -1.0F, 0.0F, 128.0F);
    assert(p && p[0] == 8 && p[1] == 40 && p[2] == 8);
    assert(p[3] == 8 && p[4] == 41 && p[5] == 8);

    /* Placement pick: the air cell directly above the slab. */
    p = camera_terrain_pick_range(0, 8.5F, 70.0F, 8.5F, 0.0F, -1.0F, 0.0F, 128.0F);
    assert(p && p[0] == 8 && p[1] == 41 && p[2] == 8);

    /* Aiming away from the map must still miss. */
    p = camera_terrain_pick_range(1, 8.5F, 70.0F, 8.5F, 0.0F, 1.0F, 0.0F, 128.0F);
    assert(p == NULL);
    p = camera_terrain_pick_range(1, 8.5F, 70.0F, 8.5F, 1.0F, 0.0F, 0.0F, 128.0F);
    assert(p == NULL);

    /* Range is still measured from the original eye, not the clipped start. */
    p = camera_terrain_pick_range(1, 8.5F, 70.0F, 8.5F, 0.0F, -1.0F, 0.0F, 20.0F);
    assert(p == NULL);

    /* Normal inside-map ray keeps working exactly as before. */
    p = camera_terrain_pick_range(1, 8.5F, 50.5F, 8.5F, 0.0F, -1.0F, 0.0F, 128.0F);
    assert(p && p[1] == 40 && p[4] == 41);

    puts("ceiling ray clipping checks passed");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as d:
    f = Path(d) / 't.c'
    f.write_text(stub + traverse + checks)
    exe = Path(d) / 't'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(f), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
