
/*
        Copyright (c) 2017-2020 ByteBit

        This file is part of KyroSpades.

        KyroSpades is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        KyroSpades is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "window.h"
#include "config.h"
#include "matrix.h"
#include "map.h"
#include "camera.h"
#include "tesselator.h"
#include "water.h"

#define WATER_RAY_STEPS 96
#ifdef OPENGL_ES
#define WATER_CELL_BUDGET 4096
#else
#define WATER_CELL_BUDGET 16384
#endif
#define WATER_TILE 16

static struct {
        uint32_t* cache;
        int size_x, size_z;
        unsigned int cursor;
        int span;

        float last_cam_x, last_cam_z;

        struct tesselator tess;
        struct glx_displaylist dl;
        bool gl_init;

        pthread_t thread;
        bool thread_started;
        bool pending;
        struct {
                float cx, cy, cz;
                float rd;
                float time;
                int span;
        } job;
} wr = {0};

static pthread_mutex_t water_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t water_cond = PTHREAD_COND_INITIALIZER;

bool water_shader_active(void) {
        return camera_y > WATER_LEVEL + 0.05F && map_size_x > 0 && map_size_z > 0;
}

static int water_span(void) {
        int span = (int)(settings.render_distance * 2.0F) + 8;
        int m = max(map_size_x, map_size_z);
        span = min(span, m);
        span = min(span, 512);
        span = max(span, 64);
        return span;
}

static float cell_hash(int x, int z) {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)(h & 0xFFFF) / 65535.0F;
}

static bool reflect_raycast(float ox, float oz, float dx, float dy, float dz, float max_dist, int max_steps,
                                                        float* out_r, float* out_g, float* out_b) {
        int gx = (int)floorf(ox);
        int gy = (int)WATER_LEVEL;
        int gz = (int)floorf(oz);

        int step_x = (dx > 0.0F) ? 1 : -1;
        int step_z = (dz > 0.0F) ? 1 : -1;

        float tdelta_x = (dx != 0.0F) ? fabsf(1.0F / dx) : 1e30F;
        float tdelta_y = (dy != 0.0F) ? fabsf(1.0F / dy) : 1e30F;
        float tdelta_z = (dz != 0.0F) ? fabsf(1.0F / dz) : 1e30F;

        float tmax_x = (dx > 0.0F) ? ((gx + 1) - ox) * tdelta_x : (dx < 0.0F ? (ox - gx) * tdelta_x : 1e30F);
        float tmax_y = tdelta_y;
        float tmax_z = (dz > 0.0F) ? ((gz + 1) - oz) * tdelta_z : (dz < 0.0F ? (oz - gz) * tdelta_z : 1e30F);

        float t = 0.0F;

        for(int i = 0; i < max_steps; i++) {
                int axis;
                if(tmax_x < tmax_y && tmax_x < tmax_z) {
                        gx += step_x;
                        t = tmax_x;
                        tmax_x += tdelta_x;
                        axis = 0;
                } else if(tmax_y < tmax_z) {
                        gy++;
                        t = tmax_y;
                        tmax_y += tdelta_y;
                        axis = 1;
                } else {
                        gz += step_z;
                        t = tmax_z;
                        tmax_z += tdelta_z;
                        axis = 2;
                }

                if(gy >= map_size_y || t > max_dist)
                        return false;

                int wx = ((gx % map_size_x) + map_size_x) % map_size_x;
                int wz = ((gz % map_size_z) + map_size_z) % map_size_z;

                if(!map_isair_nolock(wx, gy, wz)) {
                        uint32_t c = map_get_nolock(wx, gy, wz);
                        float shade = (axis == 1) ? 0.60F : 0.85F;
                        *out_r = red(c) * shade;
                        *out_g = green(c) * shade;
                        *out_b = blue(c) * shade;
                        return true;
                }
        }

        return false;
}

static int water_wrap(int v, int m) {
        return ((v % m) + m) % m;
}

/* Return the actual block color below the water at (wx, wz) so each
   water cell has individual color variation. Returns a default
   deep-water tone if nothing is found (void column). */
static uint32_t water_seabed_color(int wx, int wz) {
        for (int y = (int)WATER_LEVEL - 1; y >= 0; y--) {
                if (!map_isair_nolock(wx, y, wz))
                        return map_get_nolock(wx, y, wz);
        }
        return rgba(30, 60, 90, 255);
}

static uint32_t water_cell_compute(int x, int z, int wx, int wz) {
        /* When water shader is OFF, skip the expensive reflection raycast
           and use the original water color computation (unchanged). */
        if(!settings.water_shader) {
                uint32_t wc = water_seabed_color(wx, wz);
                float h = cell_hash(wx, wz);
                float shimmer = 0.85F + 0.22F * h + 0.08F * sinf(wr.job.time * (0.6F + h) + h * 6.2831F);
                float wr_ = red(wc) * shimmer;
                float wg_ = green(wc) * shimmer;
                float wb_ = blue(wc) * shimmer;
                float hr = fog_color[0] * 255.0F;
                float hg = fog_color[1] * 255.0F;
                float hb = fog_color[2] * 255.0F;
                float w = 0.20F + 0.70F * 0.25F;
                float r = wr_ + (hr - wr_) * w;
                float g = wg_ + (hg - wg_) * w;
                float b = wb_ + (hb - wb_) * w;
                return rgba((int)fminf(r, 255.0F), (int)fminf(g, 255.0F), (int)fminf(b, 255.0F), 255);
        }

        float px = x + 0.5F;
        float pz = z + 0.5F;

        float dx = px - wr.job.cx;
        float dy = WATER_LEVEL - wr.job.cy;
        float dz = pz - wr.job.cz;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if(len < 0.01F)
                len = 0.01F;
        dx /= len;
        dy = -dy / len;
        dz /= len;

        float frac = fminf(sqrtf((px - wr.job.cx) * (px - wr.job.cx) + (pz - wr.job.cz) * (pz - wr.job.cz)) / wr.job.rd,
                                           1.0F);
        int max_steps = WATER_RAY_STEPS - (int)((WATER_RAY_STEPS - 16) * frac);

        float hr, hg, hb;
        bool hit = false;
        if(dy > 0.001F)
                hit = reflect_raycast(px, pz, dx, dy, dz, wr.job.rd, max_steps, &hr, &hg, &hb);

        if(!hit) {
                hr = fog_color[0] * 255.0F;
                hg = fog_color[1] * 255.0F;
                hb = fog_color[2] * 255.0F;
        }

        uint32_t wc = water_seabed_color(wx, wz);
        float h = cell_hash(wx, wz);
        float shimmer = 0.85F + 0.22F * h + 0.08F * sinf(wr.job.time * (0.6F + h) + h * 6.2831F);
        float wr_ = red(wc) * shimmer;
        float wg_ = green(wc) * shimmer;
        float wb_ = blue(wc) * shimmer;

        float f = 1.0F - fminf(fmaxf(dy, 0.0F), 1.0F);
        float w = 0.20F + 0.70F * f * f;

        float r = wr_ + (hr - wr_) * w;
        float g = wg_ + (hg - wg_) * w;
        float b = wb_ + (hb - wb_) * w;

        if(!hit) {
                float sx = 0.35F, sy = 0.90F, sz = 0.45F;
                float sl = sqrtf(sx * sx + sy * sy + sz * sz);
                float dotp = (dx * sx + dy * sy + dz * sz) / sl;
                if(dotp > 0.0F) {
                        float spec = powf(dotp, 60.0F) * (100.0F + 250.0F * h);
                        r += spec;
                        g += spec;
                        b += spec;
                }
        }

        int ri = (int)fminf(r, 255.0F);
        int gi = (int)fminf(g, 255.0F);
        int bi = (int)fminf(b, 255.0F);
        return rgba(ri, gi, bi, 255);
}

static bool water_buffers_ensure(void) {
        if(wr.cache && (wr.size_x != map_size_x || wr.size_z != map_size_z)) {
                free(wr.cache);
                wr.cache = NULL;
        }

        if(!wr.cache) {
                wr.size_x = map_size_x;
                wr.size_z = map_size_z;
                wr.cache = calloc((size_t)wr.size_x * wr.size_z, sizeof(uint32_t));
                if(!wr.cache)
                        return false;
        }

        if(!wr.gl_init) {
                tesselator_create(&wr.tess, VERTEX_FLOAT, 0, 0);
                glx_displaylist_create(&wr.dl, true, false);
                wr.gl_init = true;
        }

        return true;
}

static void water_slice(void) {
        int span = wr.job.span;
        int half = span / 2;
        int z0 = (int)wr.job.cz - half;
        float rd = wr.job.rd;

        int budget = WATER_CELL_BUDGET;

        for(int rows = 0; rows < span && budget > 0; rows++) {
                int z = z0 + (int)(wr.cursor++ % span);

                float dz = z + 0.5F - wr.job.cz;
                float w2 = rd * rd - dz * dz;
                if(w2 < 0.0F)
                        continue;
                int hw = (int)sqrtf(w2) + 1;

                int xa = (int)wr.job.cx - hw;
                int xb = (int)wr.job.cx + hw + 1;

                map_read_lock();

                if(map_size_x != wr.size_x || map_size_z != wr.size_z) {
                        map_read_unlock();
                        return;
                }

                int wz = water_wrap(z, wr.size_z);
                uint32_t* row = wr.cache + (size_t)wz * wr.size_x;

                for(int x = xa; x < xb; x++) {
                        int wx = water_wrap(x, wr.size_x);
                        if(map_isair_nolock(wx, (int)WATER_LEVEL, wz)) {
                                row[wx] = water_cell_compute(x, z, wx, wz);
                                budget--;
                        } else {
                                row[wx] = 0;
                        }
                }
                map_read_unlock();
        }
}

static void* water_worker(void* arg) {
        (void)arg;
        pthread_mutex_lock(&water_lock);
        while(1) {
                while(!wr.pending)
                        pthread_cond_wait(&water_cond, &water_lock);
                wr.pending = false;
                water_slice();
        }
        return NULL;
}

void water_reflection_pass(void) {
        if(!water_shader_active())
                return;

        wr.last_cam_x = camera_x;
        wr.last_cam_z = camera_z;

        if(pthread_mutex_trylock(&water_lock) != 0)
                return;

        if(!water_buffers_ensure()) {
                pthread_mutex_unlock(&water_lock);
                return;
        }

        /* Invalidate the cache when the camera has moved more than 1 block
           horizontally so that stale reflection rays from the old position
           aren't rendered at the wrong world coordinates. */
        if(wr.cache && (fabsf(camera_x - wr.last_cam_x) > 1.0F || fabsf(camera_z - wr.last_cam_z) > 1.0F)) {
                memset(wr.cache, 0, (size_t)wr.size_x * wr.size_z * sizeof(uint32_t));
                wr.cursor = 0;
        }

        if(!wr.thread_started) {
                if(pthread_create(&wr.thread, NULL, water_worker, NULL) != 0) {
                        pthread_mutex_unlock(&water_lock);
                        return;
                }
                wr.thread_started = true;
        }

        wr.span = water_span();
        wr.job.cx = camera_x;
        wr.job.cy = camera_y;
        wr.job.cz = camera_z;
        wr.job.rd = settings.render_distance;
        wr.job.time = (float)window_time();
        wr.job.span = wr.span;
        wr.pending = true;

        pthread_cond_signal(&water_cond);
        pthread_mutex_unlock(&water_lock);
}

static void water_render_tile(int tx, int tz, int x0, int x1, int z0, int z1,
                               float rd, float y, size_t* n) {
        int za = max(tz, z0);
        int zb = min(tz + WATER_TILE, z1);

        for(int z = za; z < zb; z++) {
                float dz = z + 0.5F - camera_z;
                float w2 = rd * rd - dz * dz;
                if(w2 < 0.0F)
                        continue;
                int hw = (int)sqrtf(w2) + 1;

                int xa = max(max(tx, x0), (int)camera_x - hw);
                int xb = min(min(tx + WATER_TILE, x1), (int)camera_x + hw + 1);

                uint32_t* row = wr.cache + (size_t)water_wrap(z, wr.size_z) * wr.size_x;

                for(int x = xa; x < xb; x++) {
                        uint32_t c = row[water_wrap(x, wr.size_x)];
                        /* If the cell hasn't been computed yet (alpha == 0),
                           use a fallback color instead of skipping — this
                           ensures the water surface is always fully visible.
                           Don't call water_seabed_color() here (no map lock). */
                        if(!(c >> 24)) {
                                /* Fallback: water-blue tint, no fog color */
                                c = rgba(60, 100, 160, 255);
                        }

                        if(settings.water_waves) {
                                float t = wr.job.time * settings.water_wave_speed;
                                float amp = settings.water_wave_intensity;

                                if(settings.water_wave_mode == 1) {
                                        /* Tile mode: the tile acts as a single solid unit.
                                           - Wave height is computed from tile coordinate (quantized)
                                           - Color is the average of all blocks in the tile
                                           - All 5 faces (top + 4 sides) are drawn as a solid block
                                           - Sides only drawn at tile boundaries (edges of the tile)
                                             so interior blocks don't waste quads */
                                        int ts = settings.water_wave_tile_size;
                                        if(ts < 1) ts = 1;

                                        /* Only render the block at the top-left corner of its tile.
                                           That single render draws the ENTIRE tile as one big block. */
                                        if(x % ts != 0 || z % ts != 0)
                                                continue;

                                        /* Compute tile-averaged color from all blocks in this tile */
                                        int ar = 0, ag = 0, ab = 0, acount = 0;
                                        for(int dz2 = 0; dz2 < ts && (z + dz2) < zb; dz2++) {
                                                uint32_t* row2 = wr.cache + (size_t)water_wrap(z + dz2, wr.size_z) * wr.size_x;
                                                for(int dx2 = 0; dx2 < ts && (x + dx2) < xb; dx2++) {
                                                        uint32_t bc = row2[water_wrap(x + dx2, wr.size_x)];
                                                        if(bc >> 24) {
                                                                ar += red(bc); ag += green(bc); ab += blue(bc); acount++;
                                                        }
                                                }
                                        }
                                        /* Skip tiles that contain no water cells.
                                           Without this, ar/ag/ab stay 0 and the
                                           tile would be rendered as a solid black
                                           block (rgba(0,0,0,255)). Its 4 side
                                           faces sit at tile boundaries and, thanks
                                           to GL_POLYGON_OFFSET_FILL, bleed on top
                                           of adjacent non-water blocks — visible
                                           as a grid of black lines the size of the
                                           wave tile. Skipping the tile entirely is
                                           correct: there is no water to render
                                           here. */
                                        if(acount == 0)
                                                continue;
                                        uint32_t tile_color = rgba(ar / acount, ag / acount, ab / acount, 255);

                                        /* Tile coordinate for wave */
                                        int tlx = x / ts;
                                        int tlz = z / ts;
                                        float wave = (sinf(t * 0.9F + (float)tlx * 2.8F + (float)tlz * 2.0F) * 0.12F
                                                     + sinf(t * 1.3F + (float)tlx * 1.6F + (float)tlz * 4.4F) * 0.09F
                                                     + sinf(t * 1.8F + (float)tlx * 6.0F + (float)tlz * 0.8F) * 0.06F) * amp;
                                        float yw = y + wave;
                                        float yb = y - 1.0F;
                                        float w = (float)ts; /* tile width/depth */

                                        /* Top face (CW winding: viewed from above) */
                                        tesselator_set_color(&wr.tess, tile_color);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yw, z,  x, yw, z+w,  x+w, yw, z+w,  x+w, yw, z});
                                        (*n)++;

                                        /* X- side (left face, normal points -X) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.75F, green(tile_color)*0.75F, blue(tile_color)*0.75F, 255));
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yb, z,  x, yb, z+w,  x, yw, z+w,  x, yw, z});
                                        (*n)++;

                                        /* X+ side (right face, normal points +X) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.75F, green(tile_color)*0.75F, blue(tile_color)*0.75F, 255));
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x+w, yb, z+w,  x+w, yb, z,  x+w, yw, z,  x+w, yw, z+w});
                                        (*n)++;

                                        /* Z- side (front face, normal points -Z) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.625F, green(tile_color)*0.625F, blue(tile_color)*0.625F, 255));
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x+w, yb, z,  x, yb, z,  x, yw, z,  x+w, yw, z});
                                        (*n)++;

                                        /* Z+ side (back face, normal points +Z) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.625F, green(tile_color)*0.625F, blue(tile_color)*0.625F, 255));
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yb, z+w,  x+w, yb, z+w,  x+w, yw, z+w,  x, yw, z+w});
                                        (*n)++;

                                        tesselator_set_color(&wr.tess, tile_color);
                                } else {
                                        /* Vertex mode (default): each vertex gets its own
                                           wave height — smooth per-vertex displacement. */
                                        tesselator_set_color(&wr.tess, c);
                                        #define WAVE3(vx, vz) \
                                            ((sinf(t * 0.9F + (float)(vx) * 0.7F + (float)(vz) * 0.5F) * 0.08F \
                                           + sinf(t * 1.3F + (float)(vx) * 0.4F + (float)(vz) * 1.1F) * 0.06F \
                                           + sinf(t * 1.8F + (float)(vx) * 1.5F + (float)(vz) * 0.2F) * 0.04F) * amp)
                                        float y0 = y + WAVE3(x    , z    );
                                        float y1 = y + WAVE3(x    , z + 1);
                                        float y2 = y + WAVE3(x + 1, z + 1);
                                        float y3 = y + WAVE3(x + 1, z    );
                                        #undef WAVE3
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, y0, z, x, y1, z + 1.0F, x + 1.0F, y2, z + 1.0F, x + 1.0F, y3, z});
                                        (*n)++;
                                }
                        } else {
                                tesselator_set_color(&wr.tess, c);
                                tesselator_addf_simple(&wr.tess,
                                        (float[]){x, y, z, x, y, z + 1.0F, x + 1.0F, y, z + 1.0F, x + 1.0F, y, z});
                                (*n)++;
                        }
                }
        }
}

void water_render(void) {
        if(!water_shader_active() || !wr.cache || !wr.gl_init)
                return;

        int span = wr.span ? wr.span : water_span();
        int half = span / 2;
        float rd = settings.render_distance;

        int x0 = (int)camera_x - half;
        int z0 = (int)camera_z - half;
        int x1 = (int)camera_x + half;
        int z1 = (int)camera_z + half;

        float y = WATER_LEVEL + 0.008F;
        tesselator_clear(&wr.tess);
        size_t n = 0;

        for(int tz = z0 & ~(WATER_TILE - 1); tz < z1; tz += WATER_TILE)
                for(int tx = x0 & ~(WATER_TILE - 1); tx < x1; tx += WATER_TILE)
                        if(camera_CubeInFrustum(tx + WATER_TILE / 2, 0.0F, tz + WATER_TILE / 2, WATER_TILE / 2, 2.0F))
                                water_render_tile(tx, tz, x0, x1, z0, z1, rd, y, &n);

        if(!n)
                return;

        matrix_push(matrix_model);
        matrix_identity(matrix_model);
        matrix_upload();

        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0F, -2.0F);
        tesselator_glx(&wr.tess, &wr.dl);
        glx_displaylist_draw(&wr.dl, GLX_DISPLAYLIST_ENHANCED);
        glPolygonOffset(0.0F, 0.0F);
        glDisable(GL_POLYGON_OFFSET_FILL);

        matrix_pop(matrix_model);
        matrix_upload();
}


