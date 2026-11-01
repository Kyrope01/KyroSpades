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

    /* The mesher snapshots the registry per chunk; the box is inclusive and in
       map axes, and the cap is honoured. */
    glowing_blocks_set_placement_enabled(true);
    glowing_blocks_note_local_build(10, 4, 20, 0x0000FF00);
    glowing_blocks_confirm_local_build(10, 4, 20);
    glowing_blocks_note_local_build(11, 4, 20, 0x000000FF);
    glowing_blocks_confirm_local_build(11, 4, 20);
    struct glowing_block_sample samples[8];
    assert(glowing_blocks_collect(0, 0, 0, 15, 63, 31, samples, 8) == 2);
    assert(samples[0].x == 10 && samples[0].y == 4 && samples[0].z == 20);
    assert(samples[0].color == 0x0000FF00 && samples[1].color == 0x000000FF);
    assert(glowing_blocks_collect(0, 0, 0, 9, 63, 31, samples, 8) == 0);   /* box excludes it */
    assert(glowing_blocks_collect(10, 4, 20, 10, 4, 20, samples, 8) == 1); /* exact voxel */
    assert(glowing_blocks_collect(0, 0, 0, 15, 63, 31, samples, 1) == 1);  /* cap respected */
    assert(glowing_blocks_collect(0, 0, 0, 15, 63, 31, NULL, 8) == 0);     /* no output buffer */
    assert(glowing_blocks_collect(0, 0, 0, 15, 63, 31, samples, 0) == 0);  /* no room */

    /* The block's own faces and its point light share one colour: the picked
       hue with its brightest channel normalised to full, so a dark pick still
       reads as a light. */
    float rgb[3];
    float peak = -1.0F;
    glowing_blocks_light_color(0x00FF00FF, rgb, &peak); /* magenta */
    assert(fabsf(rgb[0] - 1.0F) < 0.001F && fabsf(rgb[1]) < 0.001F && fabsf(rgb[2] - 1.0F) < 0.001F);
    assert(fabsf(peak - 1.0F) < 0.001F);
    glowing_blocks_light_color(0x00201008, rgb, &peak); /* dark blue: 8, 16, 32 */
    assert(fabsf(rgb[0] - 0.25F) < 0.01F && fabsf(rgb[1] - 0.5F) < 0.01F && fabsf(rgb[2] - 1.0F) < 0.01F);
    assert(fabsf(peak - (32.0F / 255.0F)) < 0.01F);
    glowing_blocks_light_color(0x00000000, rgb, &peak); /* black still glows */
    assert(fabsf(rgb[0] - 0.75F) < 0.01F && fabsf(rgb[1] - 0.85F) < 0.01F && fabsf(rgb[2] - 1.0F) < 0.01F);
    assert(peak == 0.0F);

    glowing_blocks_deinit();
    puts("Glowing blocks submit wrapped, server-confirmed forward lights and share one emissive colour");
}
'''.replace('GLOW_SOURCE', source)
# ── the shaders must honour the marker the mesher writes ────────────────────
lighting = (root / "src/lighting.c").read_text()
glx = (root / "src/glx.c").read_text()
chunk = (root / "src/chunk.c").read_text()
header = (root / "src/glowing_blocks.h").read_text()
assert "GLOWING_BLOCK_VERTEX_ALPHA 254" in header
# Both terrain shader variants and the default shader skip the lighting
# multiply for an emissive face, and lift it slightly so HDR bloom reacts.
assert lighting.count("1.0 - step(0.9995, v_Color.a)") == 2
assert glx.count("1.0 - step(0.9995, v_Color.a)") == 1
assert lighting.count("mix(lit, vec3(1.25), emissive)") == 2
assert "mix(light, vec3(1.25), emissive)" in glx
# The mesher paints glowing faces flat and unshaded in every mesher, and never
# lets greedy merging cross a glowing voxel.
assert chunk.count("chunk_glow_vertex_color(glow") == 25
assert chunk.count("chunk_glow_merge_matches(blocks, glows, glow_count, x, y, z,") == 12

with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "glow.c"
    exe = Path(d) / "glow"
    c.write_text(harness)
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', str(c),
                    '-o', str(exe), '-pthread', '-lm'], check=True)
    subprocess.run([str(exe)], check=True)
