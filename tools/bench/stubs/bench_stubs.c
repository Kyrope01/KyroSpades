/* Headless benchmark harness stubs.
 *
 * The benchmarks link the real water.c / chunk.c / tesselator.c / libvxl.c and
 * replace only the parts that need a window, a GPU, a network or a full map
 * lifecycle. Everything here is either a counter, a no-op, or a direct
 * libvxl probe identical to the one map.c performs — so the measured CPU cost
 * is the real one.
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "config.h"
#include "map.h"
#include "camera.h"
#include "matrix.h"
#include "texture.h"
#include "log.h"
#include "glx.h"
#include "lighting.h"
#include "shadow.h"
#include "tesselator.h"
#include "hashtable.h"

/* ── Map ────────────────────────────────────────────────────────────────── */

struct libvxl_map bench_map;

int map_size_x = 512;
int map_size_y = 64;
int map_size_z = 512;

float fog_color[4] = {0.5F, 0.9098F, 1.0F, 1.0F};
float sun_dir[3] = {-0.5F, 0.8F, -0.3F};

/* Same coordinate convention as map.c: the engine's Y is flipped into the
   voxel column. */
bool map_isair_nolock(int x, int y, int z) {
        return !libvxl_map_issolid(&bench_map, x, z, map_size_y - 1 - y);
}

unsigned int map_get_nolock(int x, int y, int z) {
        return rgb2bgr(libvxl_map_get(&bench_map, x, z, map_size_y - 1 - y));
}

bool map_isair(int x, int y, int z) {
        return map_isair_nolock(x, y, z);
}

unsigned int map_get(int x, int y, int z) {
        return map_get_nolock(x, y, z);
}

void map_read_lock(void) {}
void map_read_unlock(void) {}

void map_copy_blocks(struct libvxl_chunk_copy* copy, size_t x, size_t y) {
        libvxl_copy_chunk(&bench_map, copy, x, y);
}

void fog_color_render(float out[3]) {
        out[0] = fog_color[0];
        out[1] = fog_color[1];
        out[2] = fog_color[2];
}

/* Mirrors the DDA in map.c. It is duplicated here because map.c pulls in the
   whole map lifecycle (network, workers, GL textures); keeping the probe
   identical is what makes the textured-mesher timings meaningful. If map.c's
   raycast changes, update this copy — tools/bench/README.md says so too. */
float map_sun_shadow(int x, int y, int z, int steps) {
        float ox = x + 0.5F + sun_dir[0] * 0.1F;
        float oy = y + 0.5F + sun_dir[1] * 0.1F;
        float oz = z + 0.5F + sun_dir[2] * 0.1F;

        int gx = (int)floorf(ox);
        int gy = (int)floorf(oy);
        int gz = (int)floorf(oz);

        int step_x = sun_dir[0] > 0.0F ? 1 : -1;
        int step_y = sun_dir[1] > 0.0F ? 1 : -1;
        int step_z = sun_dir[2] > 0.0F ? 1 : -1;

        float tdelta_x = sun_dir[0] != 0.0F ? fabsf(1.0F / sun_dir[0]) : 1e30F;
        float tdelta_y = sun_dir[1] != 0.0F ? fabsf(1.0F / sun_dir[1]) : 1e30F;
        float tdelta_z = sun_dir[2] != 0.0F ? fabsf(1.0F / sun_dir[2]) : 1e30F;

        float tmax_x = sun_dir[0] > 0.0F ? ((gx + 1) - ox) * tdelta_x : (ox - gx) * tdelta_x;
        float tmax_y = sun_dir[1] > 0.0F ? ((gy + 1) - oy) * tdelta_y : (oy - gy) * tdelta_y;
        float tmax_z = sun_dir[2] > 0.0F ? ((gz + 1) - oz) * tdelta_z : (oz - gz) * tdelta_z;

        for(int i = 0; i < steps; i++) {
                if(tmax_x < tmax_y && tmax_x < tmax_z) {
                        gx += step_x;
                        tmax_x += tdelta_x;
                } else if(tmax_y < tmax_z) {
                        gy += step_y;
                        tmax_y += tdelta_y;
                } else {
                        gz += step_z;
                        tmax_z += tdelta_z;
                }

                if(gy >= map_size_y || gy < 0)
                        return 1.0F;
                if(gx < 0 || gx >= map_size_x || gz < 0 || gz >= map_size_z)
                        return 1.0F;
                if(!map_isair_nolock(gx, gy, gz))
                        return 0.0F;
        }

        return 1.0F;
}

/* ── Camera / matrices ──────────────────────────────────────────────────── */

float camera_x = 0.0F;
float camera_y = 0.0F;
float camera_z = 0.0F;

int camera_CubeInFrustum(float x, float y, float z, float radius, float max_height) {
        /* Matches the engine's behaviour closely enough for the benchmark:
           report everything in front of the near hemisphere as visible so the
           benchmark measures drawing, not culling luck. */
        (void)y; (void)max_height;
        /* The real frustum is a 70-degree cone. A front-half-plane test is a
           deliberate over-estimate (it keeps everything in front of the camera)
           so the reported cost is an upper bound rather than a lucky sample.
           The test camera looks down -Z. */
        return (z - radius) < camera_z;
}

mat4 matrix_view;
mat4 matrix_model;
mat4 matrix_projection;

void matrix_identity(mat4 m) {
        memset(m, 0, sizeof(mat4));
        for(int i = 0; i < 4; i++)
                m[i][i] = 1.0F;
}

void matrix_load(mat4 m, mat4 n) { memcpy(m, n, sizeof(mat4)); }
void matrix_multiply(mat4 m, mat4 n) { (void)m; (void)n; }
void matrix_push(mat4 m) { (void)m; }
void matrix_pop(mat4 m) { (void)m; }
void matrix_translate(mat4 m, float x, float y, float z) { (void)m; (void)x; (void)y; (void)z; }
void matrix_upload(void) {}
void matrix_upload_p(void) {}

/* ── Renderer / settings surface ────────────────────────────────────────── */

struct RENDER_OPTIONS settings;

struct texture texture_minimap;
struct texture texture_blocks;

struct texture* texture_blocks_atlas(void) {
        return &texture_blocks;
}

int texture_blocks_custom_grid(void) { return 1; }
int texture_blocks_custom_loaded = 0;

/* Only used by chunk_queue_blocks(), which the benchmark never calls. */
bool ht_iterate(HashTable* ht, void* user, bool (*callback)(void* key, void* value, void* user)) {
        (void)ht; (void)user; (void)callback;
        return false;
}
int texture_blocks_custom_tile(uint32_t color) { (void)color; return 0; }

bool lighting_supported(void) { return false; }
bool lighting_world_supported(void) { return false; }
void lighting_world_update_matrices(void) {}
float lighting_sunlight_scale(void) { return 1.0F; }

/* Set by the benchmark to price the per-block sun-shadow raycast that
   chunk_generate_textured() performs when shadows are enabled. */
int bench_shadow_enabled = 0;

bool shadow_baked_enabled(void) { return bench_shadow_enabled != 0; }
void shadow_invalidate(void) {}

void glx_displaylist_create(struct glx_displaylist* x, bool has_color, bool has_normal) {
        memset(x, 0, sizeof(*x));
        x->has_color = has_color;
        x->has_normal = has_normal;
}
void glx_displaylist_destroy(struct glx_displaylist* x) { (void)x; }
void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type, void* color, void* vertex, void* normal,
                            void* texcoord) {
        (void)type; (void)color; (void)vertex; (void)normal; (void)texcoord;
        x->size = size;
}
void glx_displaylist_draw(struct glx_displaylist* x, int type) { (void)x; (void)type; }
/* The bench never draws, so the chunk-draw client-state scope is a no-op; the
   real implementations live in glx.c, which the bench does not link. */
void glx_client_state_begin(void) {}
void glx_client_state_end(void) {}
void glx_client_state_invalidate(void) {}

/* ── Clock / platform ───────────────────────────────────────────────────── */

int window_cpucores(void) {
        return 1; /* the benchmarks are single-threaded on purpose */
}

double bench_now(void);

float window_time(void) {
        return (float)bench_now();
}

/* ── Logging ────────────────────────────────────────────────────────────── */

void log_log(int level, const char* file, int line, const char* fmt, ...) {
        (void)file;
        (void)line;
        if(level < LOG_WARN)
                return;
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
        fputc('\n', stderr);
}
