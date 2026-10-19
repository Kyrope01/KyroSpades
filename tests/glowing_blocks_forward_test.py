"""Compile the actual glowing-block registry and test forward-light submissions.

Run: python3 tests/glowing_blocks_forward_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/glowing_blocks.c").as_posix()
# Stub just the unavailable renderer/map dependencies; compile the real C file.
harness = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#define LIGHTING_H
#define MAP_H
#define WINDOW_H
int map_size_x = 512, map_size_z = 512;
static float submitted_x, submitted_r, submitted_g, submitted_b;
static int submissions;
static double now = 1.0;
static double window_time(void) { return now; }
static void lighting_submit_point(float x, float y, float z, float r, float g,
                                  float b, float radius, float intensity) {
    (void)y; (void)z;
    assert(radius > 0 && intensity > 0);
    submitted_x=x;submitted_r=r;submitted_g=g;submitted_b=b;submissions++;
}
#include "GLOW_SOURCE"
int main(void) {
    glowing_blocks_set_placement_enabled(true);
    glowing_blocks_note_local_build(511, 4, 20, 0x000000FF); /* packed R,G,B bytes */
    glowing_blocks_prepare_frame(0.0f, 4.5f, 20.5f);
    assert(submissions == 0); /* must wait for authoritative server build */
    glowing_blocks_confirm_local_build(511, 4, 20);
    glowing_blocks_prepare_frame(0.0f, 4.5f, 20.5f);
    assert(submissions == 1 && fabsf(submitted_x + 0.5f) < 0.01f);
    assert(submitted_r == 1.0f && submitted_g == 0.0f && submitted_b == 0.0f);
    glowing_blocks_map_changed(511, 4, 20, 0xFFFFFFFFu);
    glowing_blocks_prepare_frame(0.0f, 4.5f, 20.5f);
    assert(submissions == 1); /* block removal stops its point light */
    glowing_blocks_deinit();
    puts("Glowing blocks submit wrapped, server-confirmed forward lights");
}
'''.replace('GLOW_SOURCE', source)
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "glow.c"
    exe = Path(d) / "glow"
    c.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c),
                    '-o', str(exe), '-pthread', '-lm'], check=True)
    subprocess.run([str(exe)], check=True)
