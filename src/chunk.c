
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

#include <pthread.h>
#include <signal.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <string.h>

#include "common.h"
#include "window.h"
#include "config.h"
#include "texture.h"
#include "log.h"
#include "matrix.h"
#include "map.h"
#include "camera.h"
#include "tesselator.h"
#include "chunk.h"
#include "channel.h"
#include "utils.h"
#include "water.h"
/* pthread_spinlock_t is Linux-only. macOS and Android's Bionic lack it.
   Use a mutex on those platforms; the spinlock is a micro-optimisation
   that is only relevant on multi-core Linux anyway. */
#if defined(__linux__) && !defined(__ANDROID__)
#  define CHUNK_LOCK_T          pthread_spinlock_t
#  define chunk_lock_init(l)    pthread_spin_init(l, PTHREAD_PROCESS_PRIVATE)
#  define chunk_lock_lock(l)    pthread_spin_lock(l)
#  define chunk_lock_unlock(l)  pthread_spin_unlock(l)
#else
#  define CHUNK_LOCK_T          pthread_mutex_t
#  define chunk_lock_init(l)    pthread_mutex_init(l, NULL)
#  define chunk_lock_lock(l)    pthread_mutex_lock(l)
#  define chunk_lock_unlock(l)  pthread_mutex_unlock(l)
#endif


struct chunk chunks[CHUNKS_PER_DIM * CHUNKS_PER_DIM];

HashTable chunk_block_queue;
struct channel chunk_work_queue;
struct channel chunk_result_queue;
CHUNK_LOCK_T chunk_block_queue_lock;

int chunk_gen = 0;
static int chunk_bulk_pending = 0;

struct chunk_work_packet {
        size_t chunk_x;
        size_t chunk_y;
        struct chunk* chunk;
};

struct chunk_result_packet {
        struct chunk* chunk;
        int max_height;
        struct tesselator tesselator;
        uint32_t* minimap_data;
        int gen;
};

struct chunk_render_call {
        struct chunk* chunk;
        int mirror_x;
        int mirror_y;
        float dist_sq; // cached camera distance², computed once, not per qsort compare
};

void chunk_init() {
        for(size_t x = 0; x < CHUNKS_PER_DIM; x++) {
                for(size_t y = 0; y < CHUNKS_PER_DIM; y++) {
                        struct chunk* c = chunks + x + y * CHUNKS_PER_DIM;
                        c->created = false;
                        c->max_height = 1;
                        c->gen = 0;
                        c->x = x;
                        c->y = y;
                }
        }

        channel_create(&chunk_work_queue, sizeof(struct chunk_work_packet), CHUNKS_PER_DIM * CHUNKS_PER_DIM);
        channel_create(&chunk_result_queue, sizeof(struct chunk_result_packet), CHUNKS_PER_DIM * CHUNKS_PER_DIM);
        ht_setup(&chunk_block_queue, sizeof(struct chunk*), sizeof(struct chunk_work_packet), 64);

        chunk_lock_init(&chunk_block_queue_lock);

        int chunk_enabled_cores = min(max(window_cpucores() - 1, 1), CHUNK_WORKERS_MAX);
        log_info("%i cores enabled for chunk generation", chunk_enabled_cores);

        pthread_t threads[chunk_enabled_cores];

        for(size_t k = 0; k < chunk_enabled_cores; k++)
                pthread_create(threads + k, NULL, chunk_generate, NULL);
}

static int chunk_sort(const void* a, const void* b) {
        // near-to-far so opaque geometry benefits from early-z rejection.
        // old version recomputed both distances every comparison AND
        // truncated a float difference to int — cached key fixes both.
        float da = ((const struct chunk_render_call*)a)->dist_sq;
        float db = ((const struct chunk_render_call*)b)->dist_sq;
        return (da > db) - (da < db);
}

void chunk_render(struct chunk_render_call* c) {
        if(c->chunk->created) {
                matrix_push(matrix_model);
                matrix_translate(matrix_model, c->mirror_x * map_size_x, 0.0F, c->mirror_y * map_size_z);
                matrix_upload();

                if(c->chunk->display_list.has_texcoord) {
#if !defined(OPENGL_ES)
                        glEnable(GL_TEXTURE_2D);
#endif
                        glBindTexture(GL_TEXTURE_2D, texture_blocks.texture_id);
                }

                glx_displaylist_draw(&c->chunk->display_list, GLX_DISPLAYLIST_NORMAL);

                if(c->chunk->display_list.has_texcoord) {
#if !defined(OPENGL_ES)
                        glDisable(GL_TEXTURE_2D);
#endif
                }

                matrix_pop(matrix_model);
        }
}

void chunk_draw_visible() {
        struct chunk_render_call chunks_draw[CHUNKS_PER_DIM * CHUNKS_PER_DIM * 2];
        int index = 0;

        int overshoot = (settings.render_distance + CHUNK_SIZE - 1) / CHUNK_SIZE + 1;

        // hoisted: was a libm pow() call inside the double loop, every frame
        float rd = settings.render_distance + 1.414F * CHUNK_SIZE;
        float rd_sq = rd * rd;

        // go through all possible chunks and store all in range and view
        for(int y = -overshoot; y < CHUNKS_PER_DIM + overshoot; y++) {
                for(int x = -overshoot; x < CHUNKS_PER_DIM + overshoot; x++) {
                        float d = distance2D((x + 0.5F) * CHUNK_SIZE, (y + 0.5F) * CHUNK_SIZE, camera_x, camera_z);
                        if(d <= rd_sq) {
                                uint32_t tmp_x = ((uint32_t)x) % CHUNKS_PER_DIM;
                                uint32_t tmp_y = ((uint32_t)y) % CHUNKS_PER_DIM;

                                struct chunk* c = chunks + tmp_x + tmp_y * CHUNKS_PER_DIM;

                                if(camera_CubeInFrustum((x + 0.5F) * CHUNK_SIZE, 0.0F, (y + 0.5F) * CHUNK_SIZE, CHUNK_SIZE / 2,
                                                                                c->max_height))
                                        chunks_draw[index++] = (struct chunk_render_call) {
                                                .chunk = c,
                                                .mirror_x = (x < 0) ? -1 : ((x >= CHUNKS_PER_DIM) ? 1 : 0),
                                                .mirror_y = (y < 0) ? -1 : ((y >= CHUNKS_PER_DIM) ? 1 : 0),
                                                .dist_sq = d,
                                        };
                        }
                }
        }

        // sort near→far: chunks are opaque, so drawing front-first lets
        // early-z reject occluded fragments (correct — do NOT reverse for "transparency",
        // water is rendered separately)
        qsort(chunks_draw, index, sizeof(struct chunk_render_call), chunk_sort);

        for(int k = 0; k < index; k++)
                chunk_render(chunks_draw + k);
}

static __attribute__((always_inline)) inline bool solid_array_isair(struct libvxl_chunk_copy* blocks, uint32_t x,
                                                                                                                                        int32_t y, uint32_t z) {
        if(y < 0)
                return false;
        if(y >= map_size_y)
                return true;

        return !libvxl_copy_chunk_is_solid(blocks, x % map_size_x, z % map_size_z, map_size_y - 1 - y);
}

static __attribute__((always_inline)) inline float solid_sunblock(struct libvxl_chunk_copy* blocks, uint32_t x,
                                                                                                                                  uint32_t y, uint32_t z) {
        int dec = 18;
        int i = 127;

        while(dec && y < map_size_y) {
                if(!solid_array_isair(blocks, x, ++y, --z))
                        i -= dec;
                dec -= 2;
        }

        return (float)i / 127.0F;
}

void* chunk_generate(void* data) {
        pthread_detach(pthread_self());

        while(1) {
                struct chunk_work_packet work;
                channel_await(&chunk_work_queue, &work);

                if(!work.chunk)
                        break;

                struct chunk_result_packet result;
                result.chunk = work.chunk;
                result.gen = work.chunk->gen;
                result.minimap_data = malloc(CHUNK_SIZE * CHUNK_SIZE * sizeof(uint32_t));
                tesselator_create(&result.tesselator, VERTEX_INT, 0, settings.textured_blocks);

                struct libvxl_chunk_copy blocks;
                map_copy_blocks(&blocks, work.chunk_x * CHUNK_SIZE, work.chunk_y * CHUNK_SIZE);

                if(settings.textured_blocks) {
                        chunk_generate_textured(&blocks, &result.tesselator, &result.max_height);
                } else if(settings.greedy_meshing)
                        chunk_generate_greedy(&blocks, work.chunk_x * CHUNK_SIZE, work.chunk_y * CHUNK_SIZE, &result.tesselator,
                                                                  &result.max_height);
                else
                        chunk_generate_naive(&blocks, &result.tesselator, &result.max_height, settings.ambient_occlusion);

                // use the fact that libvxl orders libvxl_blocks by top-down coordinate first in its data structure
                size_t chunk_x = work.chunk_x * CHUNK_SIZE;
                size_t chunk_y = work.chunk_y * CHUNK_SIZE;
                uint32_t last_position = 0;
                for(int k = blocks.blocks_sorted_count - 1; k >= 0; k--) {
                        struct libvxl_block* blk = blocks.blocks_sorted + k;

                        if(blk->position != last_position || k == blocks.blocks_sorted_count - 1) {
                                last_position = blk->position;

                                int x = key_getx(blk->position);
                                int z = key_gety(blk->position);

                                uint32_t* out = result.minimap_data + (x - chunk_x + (z - chunk_y) * CHUNK_SIZE);

                                if((x % 64) > 0 && (z % 64) > 0) {
                                        *out = rgb2bgr(blk->color) | 0xFF000000;
                                } else {
                                        *out = rgba(255, 255, 255, 255);
                                }
                        }
                }

                libvxl_copy_chunk_destroy(&blocks);

                channel_put(&chunk_result_queue, &result);
        }

        return NULL;
}

void chunk_generate_greedy(struct libvxl_chunk_copy* blocks, size_t start_x, size_t start_z, struct tesselator* tess,
                                                   int* max_height) {
        *max_height = 0;

        int checked_voxels[2][CHUNK_SIZE * CHUNK_SIZE];
        int checked_voxels2[2][CHUNK_SIZE * map_size_y];

        for(int z = start_z; z < start_z + CHUNK_SIZE; z++) {
                memset(checked_voxels2[0], 0, sizeof(int) * CHUNK_SIZE * map_size_y);
                memset(checked_voxels2[1], 0, sizeof(int) * CHUNK_SIZE * map_size_y);

                for(int x = start_x; x < start_x + CHUNK_SIZE; x++) {
                        for(int y = 0; y < map_size_y; y++) {
                                if(!solid_array_isair(blocks, x, y, z)) {
                                        if(*max_height < y) {
                                                *max_height = y;
                                        }

                                        if(water_shader_active() && (float)y < WATER_LEVEL)
                                                continue;

                                        uint32_t col = libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - y);
                                        int r = blue(col);
                                        int g = green(col);
                                        int b = red(col);

                                        if((z == 0 && solid_array_isair(blocks, x, y, map_size_z - 1))
                                           || (z > 0 && solid_array_isair(blocks, x, y, z - 1))) {
                                                if(checked_voxels2[0][y + (x - start_x) * map_size_y] == 0) {
                                                        int len_y = 1;
                                                        int len_x = 1;

                                                        for(int a = 1; a < map_size_y - y; a++) {
                                                                if(!solid_array_isair(blocks, x, y + a, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - (y + a)) == col
                                                                   && checked_voxels2[0][y + a + (x - start_x) * map_size_y] == 0
                                                                   && ((z == 0 && solid_array_isair(blocks, x, y + a, map_size_z - 1))
                                                                           || (z > 0 && solid_array_isair(blocks, x, y + a, z - 1))))
                                                                        len_y++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_x + CHUNK_SIZE - x); b++) {
                                                                int a;
                                                                for(a = 0; a < len_y; a++) {
                                                                        if(solid_array_isair(blocks, x + b, y + a, z)
                                                                           || libvxl_copy_chunk_get_color(blocks, x + b, z, map_size_y - 1 - (y + a)) != col
                                                                           || checked_voxels2[0][y + a + (x + b - start_x) * map_size_y] != 0
                                                                           || !((z == 0 && solid_array_isair(blocks, x + b, y + a, map_size_z - 1))
                                                                                        || (z > 0 && solid_array_isair(blocks, x + b, y + a, z - 1))))
                                                                                break;
                                                                }
                                                                if(a == len_y)
                                                                        len_x++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 0; b < len_x; b++)
                                                                for(int a = 0; a < len_y; a++)
                                                                        checked_voxels2[0][y + a + (x + b - start_x) * map_size_y] = 1;

                                                        tesselator_set_color(tess, rgba(r * 0.875F, g * 0.875F, b * 0.875F, 255));
                                                        tesselator_addi_simple(
                                                                tess, (int16_t[]) {x, y, z, x, y + len_y, z, x + len_x, y + len_y, z, x + len_x, y, z});
                                                }
                                        }

                                        if((z == map_size_z - 1 && solid_array_isair(blocks, x, y, 0))
                                           || (z < map_size_z - 1 && solid_array_isair(blocks, x, y, z + 1))) {
                                                if(checked_voxels2[1][y + (x - start_x) * map_size_y] == 0) {
                                                        int len_y = 1;
                                                        int len_x = 1;

                                                        for(int a = 1; a < map_size_y - y; a++) {
                                                                if(!solid_array_isair(blocks, x, y + a, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - (y + a)) == col
                                                                   && checked_voxels2[1][y + a + (x - start_x) * map_size_y] == 0
                                                                   && ((z == map_size_z - 1 && solid_array_isair(blocks, x, y + a, 0))
                                                                           || (z < map_size_z - 1 && solid_array_isair(blocks, x, y + a, z + 1))))
                                                                        len_y++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_x + CHUNK_SIZE - x); b++) {
                                                                int a;
                                                                for(a = 0; a < len_y; a++) {
                                                                        if(solid_array_isair(blocks, x + b, y + a, z)
                                                                           || libvxl_copy_chunk_get_color(blocks, x + b, z, map_size_y - 1 - (y + a)) != col
                                                                           || checked_voxels2[1][y + a + (x + b - start_x) * map_size_y] != 0
                                                                           || !((z == map_size_z - 1 && solid_array_isair(blocks, x + b, y + a, 0))
                                                                                        || (z < map_size_z - 1 && solid_array_isair(blocks, x + b, y + a, z + 1))))
                                                                                break;
                                                                }
                                                                if(a == len_y)
                                                                        len_x++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 0; b < len_x; b++)
                                                                for(int a = 0; a < len_y; a++)
                                                                        checked_voxels2[1][y + a + (x + b - start_x) * map_size_y] = 1;

                                                        tesselator_set_color(tess, rgba(r * 0.625F, g * 0.625F, b * 0.625F, 255));
                                                        tesselator_addi_simple(tess,
                                                                                                   (int16_t[]) {x, y, z + 1, x + len_x, y, z + 1, x + len_x, y + len_y,
                                                                                                                                z + 1, x, y + len_y, z + 1});
                                                }
                                        }
                                }
                        }
                }
        }

        for(int x = start_x; x < start_x + CHUNK_SIZE; x++) {
                memset(checked_voxels2[0], 0, sizeof(int) * CHUNK_SIZE * map_size_y);
                memset(checked_voxels2[1], 0, sizeof(int) * CHUNK_SIZE * map_size_y);

                for(int z = start_z; z < start_z + CHUNK_SIZE; z++) {
                        for(int y = 0; y < map_size_y; y++) {
                                if(!solid_array_isair(blocks, x, y, z)) {
                                        if(*max_height < y) {
                                                *max_height = y;
                                        }

                                        unsigned int col = libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - y);
                                        int r = blue(col);
                                        int g = green(col);
                                        int b = red(col);

                                        if((x == 0 && solid_array_isair(blocks, map_size_x - 1, y, z))
                                           || (x > 0 && solid_array_isair(blocks, x - 1, y, z))) {
                                                if(checked_voxels2[0][y + (z - start_z) * map_size_y] == 0) {
                                                        int len_y = 1;
                                                        int len_z = 1;

                                                        for(int a = 1; a < map_size_y - y; a++) {
                                                                if(!solid_array_isair(blocks, x, y + a, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - (y + a)) == col
                                                                   && checked_voxels2[0][y + a + (z - start_z) * map_size_y] == 0
                                                                   && ((x == 0 && solid_array_isair(blocks, map_size_x - 1, y + a, z))
                                                                           || (x > 0 && solid_array_isair(blocks, x - 1, y + a, z))))
                                                                        len_y++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_z + CHUNK_SIZE - z); b++) {
                                                                int a;
                                                                for(a = 0; a < len_y; a++) {
                                                                        if(solid_array_isair(blocks, x, y + a, z + b)
                                                                           || libvxl_copy_chunk_get_color(blocks, x, z + b, map_size_y - 1 - (y + a)) != col
                                                                           || checked_voxels2[0][y + a + (z + b - start_z) * map_size_y] != 0
                                                                           || !((x == 0 && solid_array_isair(blocks, map_size_x - 1, y + a, z + b))
                                                                                        || (x > 0 && solid_array_isair(blocks, x - 1, y + a, z + b))))
                                                                                break;
                                                                }
                                                                if(a == len_y)
                                                                        len_z++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 0; b < len_z; b++)
                                                                for(int a = 0; a < len_y; a++)
                                                                        checked_voxels2[0][y + a + (z + b - start_z) * map_size_y] = 1;

                                                        tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                                        tesselator_addi_simple(
                                                                tess, (int16_t[]) {x, y, z, x, y, z + len_z, x, y + len_y, z + len_z, x, y + len_y, z});
                                                }
                                        }

                                        if((x == map_size_x - 1 && solid_array_isair(blocks, 0, y, z))
                                           || (x < map_size_x - 1 && solid_array_isair(blocks, x + 1, y, z))) {
                                                if(checked_voxels2[1][y + (z - start_z) * map_size_y] == 0) {
                                                        int len_y = 1;
                                                        int len_z = 1;

                                                        for(int a = 1; a < map_size_y - y; a++) {
                                                                if(!solid_array_isair(blocks, x, y + a, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - (y + a)) == col
                                                                   && checked_voxels2[1][y + a + (z - start_z) * map_size_y] == 0
                                                                   && ((x == map_size_x - 1 && solid_array_isair(blocks, 0, y + a, z))
                                                                           || (x < map_size_x - 1 && solid_array_isair(blocks, x + 1, y + a, z))))
                                                                        len_y++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_z + CHUNK_SIZE - z); b++) {
                                                                int a;
                                                                for(a = 0; a < len_y; a++) {
                                                                        if(solid_array_isair(blocks, x, y + a, z + b)
                                                                           || libvxl_copy_chunk_get_color(blocks, x, z + b, map_size_y - 1 - (y + a)) != col
                                                                           || checked_voxels2[1][y + a + (z + b - start_z) * map_size_y] != 0
                                                                           || !((x == map_size_x - 1 && solid_array_isair(blocks, 0, y + a, z + b))
                                                                                        || (x < map_size_x - 1 && solid_array_isair(blocks, x + 1, y + a, z + b))))
                                                                                break;
                                                                }
                                                                if(a == len_y)
                                                                        len_z++;
                                                                else
                                                                        break;
                                                        }

                                                        for(unsigned char b = 0; b < len_z; b++)
                                                                for(unsigned char a = 0; a < len_y; a++)
                                                                        checked_voxels2[1][y + a + (z + b - start_z) * map_size_y] = 1;

                                                        tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                                        tesselator_addi_simple(tess,
                                                                                                   (int16_t[]) {x + 1, y, z, x + 1, y + len_y, z, x + 1, y + len_y,
                                                                                                                                z + len_z, x + 1, y, z + len_z});
                                                }
                                        }
                                }
                        }
                }
        }

        for(int y = 0; y < map_size_y; y++) {
                memset(checked_voxels[0], 0, sizeof(int) * CHUNK_SIZE * CHUNK_SIZE);
                memset(checked_voxels[1], 0, sizeof(int) * CHUNK_SIZE * CHUNK_SIZE);

                for(int x = start_x; x < start_x + CHUNK_SIZE; x++) {
                        for(int z = start_z; z < start_z + CHUNK_SIZE; z++) {
                                if(!solid_array_isair(blocks, x, y, z)) {
                                        if(*max_height < y) {
                                                *max_height = y;
                                        }

                                        unsigned int col = libvxl_copy_chunk_get_color(blocks, x, z, map_size_y - 1 - y);
                                        int r = blue(col);
                                        int g = green(col);
                                        int b = red(col);

                                        if(y == map_size_y - 1 || solid_array_isair(blocks, x, y + 1, z)) {
                                                if(checked_voxels[0][(x - start_x) + (z - start_z) * CHUNK_SIZE] == 0) {
                                                        int len_x = 1;
                                                        int len_z = 1;

                                                        for(int a = 1; a < (start_x + CHUNK_SIZE - x); a++) {
                                                                if(!solid_array_isair(blocks, x + a, y, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x + a, z, map_size_y - 1 - y) == col
                                                                   && checked_voxels[0][(x + a - start_x) + (z - start_z) * CHUNK_SIZE] == 0
                                                                   && (y == map_size_y - 1 || solid_array_isair(blocks, x + a, y + 1, z)))
                                                                        len_x++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_z + CHUNK_SIZE - z); b++) {
                                                                int a;
                                                                for(a = 0; a < len_x; a++) {
                                                                        if(solid_array_isair(blocks, x + a, y, z + b)
                                                                           || libvxl_copy_chunk_get_color(blocks, x + a, z + b, map_size_y - 1 - y) != col
                                                                           || checked_voxels[0][(x + a - start_x) + (z + b - start_z) * CHUNK_SIZE] != 0
                                                                           || !(y == map_size_y - 1 || solid_array_isair(blocks, x + a, y + 1, z + b)))
                                                                                break;
                                                                }
                                                                if(a == len_x)
                                                                        len_z++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 0; b < len_z; b++)
                                                                for(int a = 0; a < len_x; a++)
                                                                        checked_voxels[0][(x + a - start_x) + (z + b - start_z) * CHUNK_SIZE] = 1;

                                                        tesselator_set_color(tess, rgba(r, g, b, 255));
                                                        tesselator_addi_simple(tess,
                                                                                                   (int16_t[]) {x, y + 1, z, x, y + 1, z + len_z, x + len_x, y + 1,
                                                                                                                                z + len_z, x + len_x, y + 1, z});
                                                }
                                        }

                                        if(y > 0 && solid_array_isair(blocks, x, y - 1, z)) {
                                                if(checked_voxels[1][(x - start_x) + (z - start_z) * CHUNK_SIZE] == 0) {
                                                        int len_x = 1;
                                                        int len_z = 1;

                                                        for(int a = 1; a < (start_x + CHUNK_SIZE - x); a++) {
                                                                if(!solid_array_isair(blocks, x + a, y, z)
                                                                   && libvxl_copy_chunk_get_color(blocks, x + a, z, map_size_y - 1 - y) == col
                                                                   && checked_voxels[1][(x + a - start_x) + (z - start_z) * CHUNK_SIZE] == 0
                                                                   && (y > 0 && solid_array_isair(blocks, x + a, y - 1, z)))
                                                                        len_x++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 1; b < (start_z + CHUNK_SIZE - z); b++) {
                                                                int a;
                                                                for(a = 0; a < len_x; a++) {
                                                                        if(solid_array_isair(blocks, x + a, y, z + b)
                                                                           || libvxl_copy_chunk_get_color(blocks, x + a, z + b, map_size_y - 1 - y) != col
                                                                           || checked_voxels[1][(x + a - start_x) + (z + b - start_z) * CHUNK_SIZE] != 0
                                                                           || !(y > 0 && solid_array_isair(blocks, x + a, y - 1, z + b)))
                                                                                break;
                                                                }
                                                                if(a == len_x)
                                                                        len_z++;
                                                                else
                                                                        break;
                                                        }

                                                        for(int b = 0; b < len_z; b++)
                                                                for(int a = 0; a < len_x; a++)
                                                                        checked_voxels[1][(x + a - start_x) + (z + b - start_z) * CHUNK_SIZE] = 1;

                                                        tesselator_set_color(tess, rgba(r * 0.5F, g * 0.5F, b * 0.5F, 255));
                                                        tesselator_addi_simple(
                                                                tess, (int16_t[]) {x, y, z, x + len_x, y, z, x + len_x, y, z + len_z, x, y, z + len_z});
                                                }
                                        }
                                }
                        }
                }
        }

        (*max_height)++;
}

//+X = 0.75
//-X = 0.75
//+Y = 1.0
//-Y = 0.5
//+Z = 0.625
//-Z = 0.875

// credit: https://0fps.net/2013/07/03/ambient-occlusion-for-minecraft-like-worlds/
// returns index into ao_curve / color LUT (1..4)
static __attribute__((always_inline)) inline int vertexAO_idx(int side1, int side2, int corner) {
        if(!side1 && !side2)
                return 1;

        return 4 - (!side1 + !side2 + !corner);
}

void chunk_generate_naive(struct libvxl_chunk_copy* blocks, struct tesselator* tess, int* max_height, int ao) {
        *max_height = 0;
        float ao_mult = settings.ao_multiplier > 0.0F ? settings.ao_multiplier : 1.0F;
        float ao_curve[5];
        ao_curve[1] = powf(0.25F, ao_mult);
        ao_curve[2] = powf(0.50F, ao_mult);
        ao_curve[3] = powf(0.75F, ao_mult);
        ao_curve[4] = 1.0F;

        if(settings.shadow_quality)
                map_read_lock();

        for(size_t k = 0; k < blocks->blocks_sorted_count; k++) {
                struct libvxl_block* blk = blocks->blocks_sorted + k;

                int x = key_getx(blk->position);
                int y = map_size_y - 1 - key_getz(blk->position);
                int z = key_gety(blk->position);

                *max_height = max(*max_height, y);

                if(water_shader_active() && (float)y < WATER_LEVEL)
                        continue;

                uint32_t col = blk->color;
                int r = blue(col);
                int g = green(col);
                int b = red(col);

                float shade = solid_sunblock(blocks, x, y, z);
                if(settings.shadow_quality) {
                        float dir_shade = map_sun_shadow(x, y, z, 32);
                        float sf = (1.0F - settings.shadow_intensity) + settings.shadow_intensity * dir_shade;
                        shade *= sf;
                }
                r *= shade;
                g *= shade;
                b *= shade;

                if(ao) {
                        // wrap x/z once per block instead of per neighbor lookup
                        uint32_t X[3] = {(uint32_t)(x - 1 + map_size_x) % map_size_x, (uint32_t)x % map_size_x,
                                                         (uint32_t)(x + 1) % map_size_x};
                        uint32_t Z[3] = {(uint32_t)(z - 1 + map_size_z) % map_size_z, (uint32_t)z % map_size_z,
                                                         (uint32_t)(z + 1) % map_size_z};

                        // sample the full 3x3x3 air neighborhood exactly once
                        // (replaces up to ~54 redundant lookups, each with modulo, in the old code)
                        int n[3][3][3];
                        for(int dy = 0; dy < 3; dy++) {
                                int yy = y + dy - 1;
                                if(yy < 0) {
                                        for(int dx = 0; dx < 3; dx++)
                                                for(int dz = 0; dz < 3; dz++)
                                                        n[dx][dy][dz] = 0;
                                } else if(yy >= map_size_y) {
                                        for(int dx = 0; dx < 3; dx++)
                                                for(int dz = 0; dz < 3; dz++)
                                                        n[dx][dy][dz] = 1;
                                } else {
                                        int wy = map_size_y - 1 - yy;
                                        for(int dx = 0; dx < 3; dx++)
                                                for(int dz = 0; dz < 3; dz++)
                                                        n[dx][dy][dz] = !libvxl_copy_chunk_is_solid(blocks, X[dx], Z[dz], wy);
                                }
                        }

                        // per-face color LUT: 4 packed colors per face instead of
                        // 3 float multiplies + rgba pack per vertex
                        uint32_t clut[5];
#define BUILD_CLUT(f)                                                                                                  \
        do {                                                                                                               \
                float fr = r * (f), fg = g * (f), fb = b * (f);                                                                \
                clut[1] = rgba(fr * ao_curve[1], fg * ao_curve[1], fb * ao_curve[1], 255);                                     \
                clut[2] = rgba(fr * ao_curve[2], fg * ao_curve[2], fb * ao_curve[2], 255);                                     \
                clut[3] = rgba(fr * ao_curve[3], fg * ao_curve[3], fb * ao_curve[3], 255);                                     \
                clut[4] = rgba(fr * ao_curve[4], fg * ao_curve[4], fb * ao_curve[4], 255);                                     \
        } while(0)

                        if(n[1][1][0]) { // -Z
                                BUILD_CLUT(0.875F);
                                tesselator_addi(tess, (int16_t[]) {x, y, z, x, y + 1, z, x + 1, y + 1, z, x + 1, y, z},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[0][1][0], n[1][0][0], n[0][0][0])],
                                                                        clut[vertexAO_idx(n[0][1][0], n[1][2][0], n[0][2][0])],
                                                                        clut[vertexAO_idx(n[2][1][0], n[1][2][0], n[2][2][0])],
                                                                        clut[vertexAO_idx(n[2][1][0], n[1][0][0], n[2][0][0])],
                                                                },
                                                                NULL);
                        }

                        if(n[1][1][2]) { // +Z
                                BUILD_CLUT(0.625F);
                                tesselator_addi(tess, (int16_t[]) {x, y, z + 1, x + 1, y, z + 1, x + 1, y + 1, z + 1, x, y + 1, z + 1},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[0][1][2], n[1][0][2], n[0][0][2])],
                                                                        clut[vertexAO_idx(n[2][1][2], n[1][0][2], n[2][0][2])],
                                                                        clut[vertexAO_idx(n[2][1][2], n[1][2][2], n[2][2][2])],
                                                                        clut[vertexAO_idx(n[0][1][2], n[1][2][2], n[0][2][2])],
                                                                },
                                                                NULL);
                        }

                        if(n[0][1][1]) { // -X
                                BUILD_CLUT(0.75F);
                                tesselator_addi(tess, (int16_t[]) {x, y, z, x, y, z + 1, x, y + 1, z + 1, x, y + 1, z},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[0][0][1], n[0][1][0], n[0][0][0])],
                                                                        clut[vertexAO_idx(n[0][0][1], n[0][1][2], n[0][0][2])],
                                                                        clut[vertexAO_idx(n[0][2][1], n[0][1][2], n[0][2][2])],
                                                                        clut[vertexAO_idx(n[0][2][1], n[0][1][0], n[0][2][0])],
                                                                },
                                                                NULL);
                        }

                        if(n[2][1][1]) { // +X
                                BUILD_CLUT(0.75F);
                                tesselator_addi(tess, (int16_t[]) {x + 1, y, z, x + 1, y + 1, z, x + 1, y + 1, z + 1, x + 1, y, z + 1},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[2][0][1], n[2][1][0], n[2][0][0])],
                                                                        clut[vertexAO_idx(n[2][2][1], n[2][1][0], n[2][2][0])],
                                                                        clut[vertexAO_idx(n[2][2][1], n[2][1][2], n[2][2][2])],
                                                                        clut[vertexAO_idx(n[2][0][1], n[2][1][2], n[2][0][2])],
                                                                },
                                                                NULL);
                        }

                        if(n[1][2][1]) { // +Y (n handles y == map_size_y - 1 as air)
                                BUILD_CLUT(1.0F);
                                tesselator_addi(tess, (int16_t[]) {x, y + 1, z, x, y + 1, z + 1, x + 1, y + 1, z + 1, x + 1, y + 1, z},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[0][2][1], n[1][2][0], n[0][2][0])],
                                                                        clut[vertexAO_idx(n[0][2][1], n[1][2][2], n[0][2][2])],
                                                                        clut[vertexAO_idx(n[2][2][1], n[1][2][2], n[2][2][2])],
                                                                        clut[vertexAO_idx(n[2][2][1], n[1][2][0], n[2][2][0])],
                                                                },
                                                                NULL);
                        }

                        if(y > 0 && n[1][0][1]) { // -Y
                                BUILD_CLUT(0.5F);
                                tesselator_addi(tess, (int16_t[]) {x, y, z, x + 1, y, z, x + 1, y, z + 1, x, y, z + 1},
                                                                (uint32_t[]) {
                                                                        clut[vertexAO_idx(n[0][0][1], n[1][0][0], n[0][0][0])],
                                                                        clut[vertexAO_idx(n[2][0][1], n[1][0][0], n[2][0][0])],
                                                                        clut[vertexAO_idx(n[2][0][1], n[1][0][2], n[2][0][2])],
                                                                        clut[vertexAO_idx(n[0][0][1], n[1][0][2], n[0][0][2])],
                                                                },
                                                                NULL);
                        }
#undef BUILD_CLUT
                } else {
                        if(solid_array_isair(blocks, x, y, z - 1)) {
                                tesselator_set_color(tess, rgba(r * 0.875F, g * 0.875F, b * 0.875F, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_Z_N, x, y, z);
                        }

                        if(solid_array_isair(blocks, x, y, z + 1)) {
                                tesselator_set_color(tess, rgba(r * 0.625F, g * 0.625F, b * 0.625F, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_Z_P, x, y, z);
                        }

                        if(solid_array_isair(blocks, x - 1, y, z)) {
                                tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_X_N, x, y, z);
                        }

                        if(solid_array_isair(blocks, x + 1, y, z)) {
                                tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_X_P, x, y, z);
                        }

                        if(y == map_size_y - 1 || solid_array_isair(blocks, x, y + 1, z)) {
                                tesselator_set_color(tess, rgba(r, g, b, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_Y_P, x, y, z);
                        }

                        if(y > 0 && solid_array_isair(blocks, x, y - 1, z)) {
                                tesselator_set_color(tess, rgba(r * 0.5F, g * 0.5F, b * 0.5F, 255));
                                tesselator_addi_cube_face(tess, CUBE_FACE_Y_N, x, y, z);
                        }
                }
        }

        if(settings.shadow_quality)
                map_read_unlock();
}

static void emit_textured_face(struct tesselator* tess, enum tesselator_cube_face face,
                                int16_t x, int16_t y, int16_t z,
                                float u0, float v0, float u1, float v1) {
        switch(face) {
                case CUBE_FACE_Z_N:
                        tesselator_addi_uv(tess, (int16_t[]){x, y, z, x, y+1, z, x+1, y+1, z, x+1, y, z},
                                                           (float[]){u0, v1, u0, v0, u1, v0, u1, v1});
                        break;
                case CUBE_FACE_Z_P:
                        tesselator_addi_uv(tess, (int16_t[]){x, y, z+1, x+1, y, z+1, x+1, y+1, z+1, x, y+1, z+1},
                                                           (float[]){u0, v1, u1, v1, u1, v0, u0, v0});
                        break;
                case CUBE_FACE_X_N:
                        tesselator_addi_uv(tess, (int16_t[]){x, y, z, x, y, z+1, x, y+1, z+1, x, y+1, z},
                                                           (float[]){u0, v1, u1, v1, u1, v0, u0, v0});
                        break;
                case CUBE_FACE_X_P:
                        tesselator_addi_uv(tess, (int16_t[]){x+1, y, z, x+1, y+1, z, x+1, y+1, z+1, x+1, y, z+1},
                                                           (float[]){u0, v1, u0, v0, u1, v0, u1, v1});
                        break;
                case CUBE_FACE_Y_P:
                        tesselator_addi_uv(tess, (int16_t[]){x, y+1, z, x, y+1, z+1, x+1, y+1, z+1, x+1, y+1, z},
                                                           (float[]){u0, v0, u0, v1, u1, v1, u1, v0});
                        break;
                case CUBE_FACE_Y_N:
                        tesselator_addi_uv(tess, (int16_t[]){x, y, z, x+1, y, z, x+1, y, z+1, x, y, z+1},
                                                           (float[]){u0, v0, u1, v0, u1, v1, u0, v1});
                        break;
        }
}

void chunk_generate_textured(struct libvxl_chunk_copy* blocks, struct tesselator* tess, int* max_height) {
        *max_height = 0;

        if(settings.shadow_quality)
                map_read_lock();

        for(size_t k = 0; k < blocks->blocks_sorted_count; k++) {
                struct libvxl_block* blk = blocks->blocks_sorted + k;

                int x = key_getx(blk->position);
                int y = map_size_y - 1 - key_getz(blk->position);
                int z = key_gety(blk->position);

                *max_height = max(*max_height, y);

                if(water_shader_active() && (float)y < WATER_LEVEL)
                        continue;

                uint32_t col = blk->color;
                int r = blue(col);
                int g = green(col);
                int b = red(col);

                {
                        float shade = solid_sunblock(blocks, x, y, z);
                        if(settings.shadow_quality) {
                                float dir_shade = map_sun_shadow(x, y, z, 32);
                                float sf = (1.0F - settings.shadow_intensity) + settings.shadow_intensity * dir_shade;
                                shade *= sf;
                        }
                        r = (int)(r * shade);
                        g = (int)(g * shade);
                        b = (int)(b * shade);
                }

                {
                        int tile_x = (r / 64) + ((b / 64 == 1 || b / 64 == 3) ? 4 : 0);
                        int tile_y = (g / 64) + ((b / 64 == 2 || b / 64 == 3) ? 4 : 0);
                        tile_x = min(tile_x, 7);
                        tile_y = min(tile_y, 7);
                        float u0 = tile_x / 8.0f;
                        float v0 = tile_y / 8.0f;
                        float u1 = (tile_x + 1) / 8.0f;
                        float v1 = (tile_y + 1) / 8.0f;

                        if(solid_array_isair(blocks, x, y, z - 1)) {
                                tesselator_set_color(tess, rgba(r * 0.875F, g * 0.875F, b * 0.875F, 255));
                                emit_textured_face(tess, CUBE_FACE_Z_N, x, y, z, u0, v0, u1, v1);
                        }

                        if(solid_array_isair(blocks, x, y, z + 1)) {
                                tesselator_set_color(tess, rgba(r * 0.625F, g * 0.625F, b * 0.625F, 255));
                                emit_textured_face(tess, CUBE_FACE_Z_P, x, y, z, u0, v0, u1, v1);
                        }

                        if(solid_array_isair(blocks, x - 1, y, z)) {
                                tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                emit_textured_face(tess, CUBE_FACE_X_N, x, y, z, u0, v0, u1, v1);
                        }

                        if(solid_array_isair(blocks, x + 1, y, z)) {
                                tesselator_set_color(tess, rgba(r * 0.75F, g * 0.75F, b * 0.75F, 255));
                                emit_textured_face(tess, CUBE_FACE_X_P, x, y, z, u0, v0, u1, v1);
                        }

                        if(y == map_size_y - 1 || solid_array_isair(blocks, x, y + 1, z)) {
                                tesselator_set_color(tess, rgba(r, g, b, 255));
                                emit_textured_face(tess, CUBE_FACE_Y_P, x, y, z, u0, v0, u1, v1);
                        }

                        if(y > 0 && solid_array_isair(blocks, x, y - 1, z)) {
                                tesselator_set_color(tess, rgba(r * 0.5F, g * 0.5F, b * 0.5F, 255));
                                emit_textured_face(tess, CUBE_FACE_Y_N, x, y, z, u0, v0, u1, v1);
                        }
                }
        }

        if(settings.shadow_quality)
                map_read_unlock();

        (*max_height)++;
}

void chunk_update_all() {
        float start = window_time();
        float budget = (chunk_bulk_pending > 0) ? 0.050F : 0.006F;

        struct chunk_result_packet result;

        while(channel_size(&chunk_result_queue) > 0) {
                channel_await(&chunk_result_queue, &result);

                if(chunk_bulk_pending > 0)
                        chunk_bulk_pending--;

                if(result.gen == result.chunk->gen) {
                        if(!result.chunk->created) {
                                glx_displaylist_create(&result.chunk->display_list, true, false);
                                result.chunk->created = true;
                        }

                        result.chunk->max_height = result.max_height;

                        tesselator_glx(&result.tesselator, &result.chunk->display_list);

                        glBindTexture(GL_TEXTURE_2D, texture_minimap.texture_id);
                        glTexSubImage2D(GL_TEXTURE_2D, 0, result.chunk->x * CHUNK_SIZE, result.chunk->y * CHUNK_SIZE,
                                                        CHUNK_SIZE, CHUNK_SIZE, GL_RGBA, GL_UNSIGNED_BYTE, result.minimap_data);
                        glBindTexture(GL_TEXTURE_2D, 0);
                }

                tesselator_free(&result.tesselator);
                free(result.minimap_data);

                if(window_time() - start > budget)
                        break;
        }
}

void chunk_rebuild_all() {
        channel_clear(&chunk_work_queue);
        chunk_gen++;
        chunk_bulk_pending = CHUNKS_PER_DIM * CHUNKS_PER_DIM;

        for(int k = 0; k < CHUNKS_PER_DIM; k++)
                for(int i = 0; i < CHUNKS_PER_DIM; i++)
                        chunks[i + k * CHUNKS_PER_DIM].gen = chunk_gen;

        for(int k = CHUNKS_PER_DIM / 2; k >= 0; k--) {
                for(int i = k; i < CHUNKS_PER_DIM - k; i++) {
                        struct chunk* build[] = {
                                chunks + i + k * CHUNKS_PER_DIM,
                                chunks + i + (CHUNKS_PER_DIM - k - 1) * CHUNKS_PER_DIM,
                                chunks + k + i * CHUNKS_PER_DIM,
                                chunks + (CHUNKS_PER_DIM - k - 1) + i * CHUNKS_PER_DIM,
                        };

                        for(size_t j = 0; j < sizeof(build) / sizeof(*build); j++) {
                                channel_put(&chunk_work_queue,
                                                        &(struct chunk_work_packet) {
                                                                .chunk = build[j],
                                                                .chunk_x = build[j]->x,
                                                                .chunk_y = build[j]->y,
                                                        });
                        }
                }
        }
}

void chunk_block_update(int x, int y, int z) {
        struct chunk* c = chunks + (x / CHUNK_SIZE) + (z / CHUNK_SIZE) * CHUNKS_PER_DIM;

        chunk_lock_lock(&chunk_block_queue_lock);
        ht_insert(&chunk_block_queue, &c,
                          &(struct chunk_work_packet) {
                                  .chunk = c,
                                  .chunk_x = c->x,
                                  .chunk_y = c->y,
                          });
        chunk_lock_unlock(&chunk_block_queue_lock);
}

static bool iterate_chunk_updates(void* key, void* value, void* user) {
        channel_put(&chunk_work_queue, value);

        return true;
}

void chunk_queue_blocks() {
        chunk_lock_lock(&chunk_block_queue_lock);
        ht_iterate(&chunk_block_queue, NULL, iterate_chunk_updates);
        ht_clear(&chunk_block_queue);
        chunk_lock_unlock(&chunk_block_queue_lock);
}
