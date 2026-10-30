/* Headless CPU benchmark for the two hottest terrain passes.
 *
 * Measures, on the real engine code:
 *   - the per-frame water surface rebuild (water.c), which the legacy desktop
 *     renderer performs every frame, and
 *   - chunk mesh generation (chunk.c) for the naive/AO, greedy and textured
 *     meshers, which is what map load and block edits pay for.
 *
 * No GPU, no window, no network: everything that needs them is stubbed in
 * tools/bench/stubs. Build and run with `make -C tools/bench`.
 */
#include <math.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "common.h"
#include "config.h"
#include "map.h"
#include "camera.h"
#include "water.h"
#include "chunk.h"
#include "tesselator.h"

#ifndef BENCH_MAP_SIZE
#define BENCH_MAP_SIZE 256
#endif

#define BENCH_WATER_FRAMES 40
#define BENCH_CHUNKS 24

extern struct libvxl_map bench_map;
extern int bench_shadow_enabled;

double bench_now(void) {
#if defined(CLOCK_MONOTONIC)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
        return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

/* ── Synthetic map ──────────────────────────────────────────────────────── */

static void build_map(int with_lake) {
        libvxl_free(&bench_map);

        map_size_x = BENCH_MAP_SIZE;
        map_size_z = BENCH_MAP_SIZE;
        map_size_y = 64;

        if(!libvxl_create(&bench_map, (size_t)map_size_x, (size_t)map_size_z, (size_t)map_size_y, NULL, 0)) {
                fprintf(stderr, "failed to create the benchmark map\n");
                exit(1);
        }

        const int cx = map_size_x / 2;
        const int cz = map_size_z / 2;
        /* Big enough that the lake covers most of the water span. */
        const int lake_r = BENCH_MAP_SIZE / 3;

        for(int z = 0; z < map_size_z; z++) {
                for(int x = 0; x < map_size_x; x++) {
                        float h = 10.0F
                                + 6.0F * sinf((float)x * 0.045F)
                                + 5.0F * cosf((float)z * 0.037F)
                                + 3.0F * sinf((float)(x + z) * 0.021F);
                        int height = (int)h;
                        if(height < 1)
                                height = 1;

                        if(with_lake) {
                                int dx = x - cx;
                                int dz = z - cz;
                                if(dx * dx + dz * dz < lake_r * lake_r)
                                        height = 0; /* y = 1 is air: this column is water */
                        }

                        /* Colour by depth band, like a real map (grass, dirt,
                           stone). Per-column noise would defeat greedy
                           meshing and make that mode look artificially slow. */
                        uint32_t color;
                        if(height > 18)
                                color = rgba(150, 150, 150, 255);
                        else if(height > 12)
                                color = rgba(120, 85, 45, 255);
                        else
                                color = rgba(60, 140, 50, 255);
                        for(int y = 0; y <= height; y++)
                                libvxl_map_set(&bench_map, x, z, map_size_y - 1 - y, color);
                }
        }
}

/* ── Timing helpers ─────────────────────────────────────────────────────── */

static double ms_since(double start) {
        return (bench_now() - start) * 1000.0;
}

/* ── Water ──────────────────────────────────────────────────────────────── */

static void bench_water(const char* label, int lake, int enabled) {
        build_map(lake);

        settings.water_shader = enabled ? 1 : 0;
        settings.water_waves = enabled ? 1 : 0;
        settings.water_wave_mode = 1;
        settings.water_wave_tile_size = 2;
        settings.water_wave_intensity = 2.0F;
        settings.water_wave_speed = 1.0F;
        settings.sunlight_intensity = 100;
        settings.shadow_intensity = 0.4F;
        settings.render_distance = 128.0F;

        camera_x = (float)(map_size_x / 2);
        camera_y = 8.0F;
        camera_z = (float)(map_size_z / 2);

        water_invalidate();

        /* Drive the same sequence the client does: publish the reflection
           worker's rows, then draw. The warm-up waits (bounded) for the first
           complete sweep so the measured frames take the steady-state path
           instead of the "map still unknown, probe everything" path. */
        water_render();
        for(int spin = 0; spin < 4000 && !water_snapshot_ready(); spin++) {
                water_reflection_pass();
                water_render();
                usleep(200);
        }
        water_reflection_pass();
        water_render();
        water_render();

        double best = 1e9;
        double total = 0.0;
        for(int i = 0; i < BENCH_WATER_FRAMES; i++) {
                camera_x += 0.01F; /* tiny movement: no re-centre, just like standing still */
                double t0 = bench_now();
                water_reflection_pass();
                water_render();
                double ms = ms_since(t0);
                total += ms;
                if(ms < best)
                        best = ms;
                camera_x -= 0.01F;
        }

        printf("water %-22s %8.3f ms/frame  (min %7.3f, %zu quads)\n", label,
               total / BENCH_WATER_FRAMES, best, water_last_quad_count());
}

/* ── Chunks ─────────────────────────────────────────────────────────────── */

static void bench_chunks(const char* label, int mode, int ao, int shadows) {
        build_map(0);
        bench_shadow_enabled = shadows;
        settings.textured_blocks = (mode == 2);
        settings.ambient_occlusion = ao;
        settings.ao_multiplier = 1.0F;

        const int chunks_per_dim = map_size_x / CHUNK_SIZE;

        double total = 0.0;
        int measured = 0;
        size_t vertices = 0;

        for(int i = 0; i < BENCH_CHUNKS; i++) {
                /* Spread the sample over the map, like a real camera would. */
                int cx = (i * 7) % chunks_per_dim;
                int cz = (i * 5) % chunks_per_dim;

                struct libvxl_chunk_copy blocks;
                map_copy_blocks(&blocks, (size_t)cx * CHUNK_SIZE, (size_t)cz * CHUNK_SIZE);

                struct tesselator tess;
                /* Matches chunk_generate()'s worker set-up. */
                tesselator_create(&tess, VERTEX_INT, 0, settings.textured_blocks);

                int max_height = 0;
                double t0 = bench_now();
                if(mode == 0)
                        chunk_generate_naive(&blocks, &tess, &max_height, ao);
                else if(mode == 1)
                        chunk_generate_greedy(&blocks, (size_t)cx * CHUNK_SIZE,
                                              (size_t)cz * CHUNK_SIZE, &tess, &max_height);
                else
                        chunk_generate_textured(&blocks, &tess, &max_height);
                total += ms_since(t0);

                vertices += tess.quad_count;
                tesselator_free(&tess);
                libvxl_copy_chunk_destroy(&blocks);
                measured++;
        }

        bench_shadow_enabled = 0;
        printf("chunk %-22s %8.3f ms/chunk (avg quads %7.0f)\n", label,
               total / (double)measured, (double)vertices / (double)measured);
}

int main(int argc, char** argv) {
        (void)argc;
        (void)argv;

        memset(&settings, 0, sizeof(settings));
        settings.ao_multiplier = 1.0F;

        printf("KyroSpades headless CPU bench — map %dx%dx64, %d water frames, %d chunks\n",
               BENCH_MAP_SIZE, BENCH_MAP_SIZE, BENCH_WATER_FRAMES, BENCH_CHUNKS);

        double t0 = bench_now();
        build_map(1);
        printf("map build (excluded from all timings): %.0f ms\n\n", ms_since(t0));

        /* On non-Core renderers water_init() returns false by design: it only
           reports whether the optional GPU shader was created. The CPU surface
           rebuild below runs on every backend, so the benchmark always runs. */
        water_init();
        bench_water("waves+tiles, lake", 1, 1);
        bench_water("waves+tiles, dry map", 0, 1);
        bench_water("disabled (fast path)", 1, 0);
        water_deinit();

        printf("\n");
        bench_chunks("naive + AO", 0, 1, 0);
        bench_chunks("naive, no AO", 0, 0, 0);
        bench_chunks("greedy", 1, 0, 0);
        bench_chunks("textured", 2, 1, 0);
        bench_chunks("textured + baked shadows", 2, 1, 1);

        libvxl_free(&bench_map);
        return 0;
}
