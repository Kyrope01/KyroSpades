/* Client-session registry for locally placed glowing blocks. */

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "glowing_blocks.h"
#include "lighting.h"
#include "map.h"
#include "window.h"

#define GLOWING_PENDING_CAPACITY 128
#define GLOWING_PENDING_TIMEOUT 10.0

struct glowing_block_record {
        int x, y, z;
        uint32_t color;
};

struct glowing_pending_record {
        int x, y, z;
        uint32_t color;
        double created;
        bool active;
};

static struct glowing_block_record* glowing_records;
static size_t glowing_count;
static size_t glowing_capacity;
static struct glowing_pending_record glowing_pending[GLOWING_PENDING_CAPACITY];
static struct glowing_block_light glowing_frame[GLOWING_BLOCK_FRAME_LIGHTS];
static int glowing_frame_count;
static bool glowing_placement_enabled;
static pthread_mutex_t glowing_lock = PTHREAD_MUTEX_INITIALIZER;

static int glowing_record_find(int x, int y, int z) {
        for(size_t i = 0; i < glowing_count; i++)
                if(glowing_records[i].x == x && glowing_records[i].y == y && glowing_records[i].z == z)
                        return (int)i;
        return -1;
}

static bool glowing_reserve(size_t required) {
        if(required <= glowing_capacity)
                return true;
        size_t capacity = 64U;
        if(glowing_capacity) {
                if(glowing_capacity > SIZE_MAX / 2U)
                        capacity = required;
                else
                        capacity = glowing_capacity * 2U;
        }
        if(capacity < required)
                capacity = required;
        if(capacity > SIZE_MAX / sizeof(*glowing_records))
                return false;
        struct glowing_block_record* records = realloc(glowing_records, capacity * sizeof(*records));
        if(!records)
                return false;
        glowing_records = records;
        glowing_capacity = capacity;
        return true;
}

static void glowing_record_remove(size_t index) {
        if(index >= glowing_count)
                return;
        glowing_count--;
        if(index != glowing_count)
                glowing_records[index] = glowing_records[glowing_count];
}

static int glowing_pending_find(int x, int y, int z) {
        for(int i = 0; i < GLOWING_PENDING_CAPACITY; i++)
                if(glowing_pending[i].active && glowing_pending[i].x == x
                   && glowing_pending[i].y == y && glowing_pending[i].z == z)
                        return i;
        return -1;
}

bool glowing_blocks_placement_enabled(void) {
        pthread_mutex_lock(&glowing_lock);
        bool enabled = glowing_placement_enabled;
        pthread_mutex_unlock(&glowing_lock);
        return enabled;
}

void glowing_blocks_set_placement_enabled(bool enabled) {
        pthread_mutex_lock(&glowing_lock);
        glowing_placement_enabled = enabled;
        pthread_mutex_unlock(&glowing_lock);
}

void glowing_blocks_note_local_build(int x, int y, int z, uint32_t color) {
        pthread_mutex_lock(&glowing_lock);
        if(!glowing_placement_enabled) {
                pthread_mutex_unlock(&glowing_lock);
                return;
        }

        int slot = glowing_pending_find(x, y, z);
        if(slot < 0) {
                double oldest = 0.0;
                int oldest_slot = 0;
                for(int i = 0; i < GLOWING_PENDING_CAPACITY; i++) {
                        if(!glowing_pending[i].active) {
                                slot = i;
                                break;
                        }
                        if(i == 0 || glowing_pending[i].created < oldest) {
                                oldest = glowing_pending[i].created;
                                oldest_slot = i;
                        }
                }
                if(slot < 0)
                        slot = oldest_slot;
        }

        glowing_pending[slot].x = x;
        glowing_pending[slot].y = y;
        glowing_pending[slot].z = z;
        glowing_pending[slot].color = color & 0x00FFFFFFU;
        glowing_pending[slot].created = window_time();
        glowing_pending[slot].active = true;
        pthread_mutex_unlock(&glowing_lock);
}

void glowing_blocks_confirm_local_build(int x, int y, int z) {
        pthread_mutex_lock(&glowing_lock);
        int pending = glowing_pending_find(x, y, z);
        if(pending < 0) {
                pthread_mutex_unlock(&glowing_lock);
                return;
        }

        uint32_t emitted_color = glowing_pending[pending].color;
        glowing_pending[pending].active = false;
        int existing = glowing_record_find(x, y, z);
        if(existing >= 0) {
                glowing_records[existing].color = emitted_color;
        } else if(glowing_reserve(glowing_count + 1U)) {
                glowing_records[glowing_count++] = (struct glowing_block_record) {
                        .x = x,
                        .y = y,
                        .z = z,
                        /* The captured colour is authoritative for this local,
                           acknowledged placement, including true black. */
                        .color = emitted_color,
                };
        }
        pthread_mutex_unlock(&glowing_lock);
}

void glowing_blocks_map_changed(int x, int y, int z, uint32_t color) {
        pthread_mutex_lock(&glowing_lock);
        int existing = glowing_record_find(x, y, z);
        if(color == 0xFFFFFFFFU) {
                if(existing >= 0)
                        glowing_record_remove((size_t)existing);
                int pending = glowing_pending_find(x, y, z);
                if(pending >= 0)
                        glowing_pending[pending].active = false;
        } else if(existing >= 0) {
                /* Keep an acknowledged glowing block's light colour in sync if
                   the map replaces that voxel without first clearing it. */
                glowing_records[existing].color = color & 0x00FFFFFFU;
        }
        pthread_mutex_unlock(&glowing_lock);
}

void glowing_blocks_clear(void) {
        pthread_mutex_lock(&glowing_lock);
        glowing_count = 0;
        glowing_frame_count = 0;
        glowing_placement_enabled = false;
        memset(glowing_pending, 0, sizeof(glowing_pending));
        pthread_mutex_unlock(&glowing_lock);
}

void glowing_blocks_deinit(void) {
        pthread_mutex_lock(&glowing_lock);
        free(glowing_records);
        glowing_records = NULL;
        glowing_count = 0;
        glowing_capacity = 0;
        glowing_frame_count = 0;
        glowing_placement_enabled = false;
        memset(glowing_pending, 0, sizeof(glowing_pending));
        pthread_mutex_unlock(&glowing_lock);
}

bool glowing_blocks_has_active(void) {
        pthread_mutex_lock(&glowing_lock);
        bool active = glowing_count > 0;
        pthread_mutex_unlock(&glowing_lock);
        return active;
}

bool glowing_blocks_has_nearby(float view_x, float view_y, float view_z, float range) {
        if(range <= 0.0F)
                return false;
        float range_sq = range * range;
        bool nearby = false;

        pthread_mutex_lock(&glowing_lock);
        for(size_t i = 0; i < glowing_count; i++) {
                float dx = glowing_records[i].x + 0.5F - view_x;
                float dy = glowing_records[i].y + 0.5F - view_y;
                float dz = glowing_records[i].z + 0.5F - view_z;
                if(map_size_x > 0)
                        dx -= roundf(dx / map_size_x) * map_size_x;
                if(map_size_z > 0)
                        dz -= roundf(dz / map_size_z) * map_size_z;
                if(dx * dx + dy * dy + dz * dz < range_sq) {
                        nearby = true;
                        break;
                }
        }
        pthread_mutex_unlock(&glowing_lock);
        return nearby;
}

static struct glowing_block_light glowing_make_light(const struct glowing_block_record* block,
                                                       float view_x, float view_z, float* distance_sq) {
        struct glowing_block_light light;
        light.position[0] = block->x + 0.5F;
        light.position[1] = block->y + 0.5F;
        light.position[2] = block->z + 0.5F;
        if(map_size_x > 0)
                light.position[0] += roundf((view_x - light.position[0]) / map_size_x) * map_size_x;
        if(map_size_z > 0)
                light.position[2] += roundf((view_z - light.position[2]) / map_size_z) * map_size_z;

        float r = (float)(block->color & 0xFFU) / 255.0F;
        float g = (float)((block->color >> 8) & 0xFFU) / 255.0F;
        float b = (float)((block->color >> 16) & 0xFFU) / 255.0F;
        float peak = fmaxf(r, fmaxf(g, b));
        if(peak < 1.0F / 255.0F) {
                light.color[0] = 0.75F;
                light.color[1] = 0.85F;
                light.color[2] = 1.0F;
                peak = 0.0F;
        } else {
                /* Preserve hue but make even dark selected colours visibly
                   emissive; intensity below still retains brightness variation. */
                light.color[0] = r / peak;
                light.color[1] = g / peak;
                light.color[2] = b / peak;
        }
        light.radius = 10.0F + peak * 3.0F;
        light.intensity = 1.15F + peak * 0.65F;

        float dx = light.position[0] - view_x;
        float dz = light.position[2] - view_z;
        *distance_sq = dx * dx + dz * dz;
        return light;
}

void glowing_blocks_prepare_frame(float view_x, float view_y, float view_z) {
        struct glowing_block_light selected[GLOWING_BLOCK_FRAME_LIGHTS];
        float selected_distance[GLOWING_BLOCK_FRAME_LIGHTS];
        int selected_count = 0;
        double now = window_time();

        pthread_mutex_lock(&glowing_lock);
        for(int i = 0; i < GLOWING_PENDING_CAPACITY; i++)
                if(glowing_pending[i].active && now - glowing_pending[i].created > GLOWING_PENDING_TIMEOUT)
                        glowing_pending[i].active = false;

        for(size_t i = 0; i < glowing_count; i++) {
                float distance_sq;
                struct glowing_block_light candidate
                        = glowing_make_light(&glowing_records[i], view_x, view_z, &distance_sq);
                float dy = candidate.position[1] - view_y;
                distance_sq += dy * dy;
                if(distance_sq >= GLOWING_BLOCK_RAY_RANGE * GLOWING_BLOCK_RAY_RANGE)
                        continue;

                int insert = selected_count;
                if(insert >= GLOWING_BLOCK_FRAME_LIGHTS) {
                        if(distance_sq >= selected_distance[GLOWING_BLOCK_FRAME_LIGHTS - 1])
                                continue;
                        insert = GLOWING_BLOCK_FRAME_LIGHTS - 1;
                } else {
                        selected_count++;
                }
                while(insert > 0 && distance_sq < selected_distance[insert - 1]) {
                        if(insert < GLOWING_BLOCK_FRAME_LIGHTS) {
                                selected[insert] = selected[insert - 1];
                                selected_distance[insert] = selected_distance[insert - 1];
                        }
                        insert--;
                }
                selected[insert] = candidate;
                selected_distance[insert] = distance_sq;
        }

        glowing_frame_count = selected_count;
        if(selected_count > 0)
                memcpy(glowing_frame, selected, (size_t)selected_count * sizeof(*selected));
        pthread_mutex_unlock(&glowing_lock);

        for(int i = 0; i < selected_count; i++)
                lighting_submit_point(selected[i].position[0], selected[i].position[1], selected[i].position[2],
                                      selected[i].color[0], selected[i].color[1], selected[i].color[2],
                                      selected[i].radius, selected[i].intensity);
}

int glowing_blocks_frame_lights(struct glowing_block_light* output, int capacity) {
        if(!output || capacity <= 0)
                return 0;
        pthread_mutex_lock(&glowing_lock);
        int count = glowing_frame_count < capacity ? glowing_frame_count : capacity;
        if(count > 0)
                memcpy(output, glowing_frame, (size_t)count * sizeof(*output));
        pthread_mutex_unlock(&glowing_lock);
        return count;
}
