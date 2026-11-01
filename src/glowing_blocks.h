/*
 * Client-only glowing blocks selected by the advanced colour picker.
 *
 * Only placements made locally while the picker toggle is enabled are tracked.
 * The registry is cleared with the current map/server session and never sends
 * any custom state to the server.
 */
#ifndef GLOWING_BLOCKS_H
#define GLOWING_BLOCKS_H

#include <stdbool.h>
#include <stdint.h>

#define GLOWING_BLOCK_FRAME_LIGHTS 16
#define GLOWING_BLOCK_LIGHT_RANGE 50.0F

/* Vertex alpha the mesher writes on a glowing block's own faces. The terrain
 * shaders read anything below 1.0 as "this surface is the light" and skip the
 * sun/ambient/point-light multiply, so the block stays bright at night and in
 * shadow. Every other terrain vertex is 255. */
#define GLOWING_BLOCK_VERTEX_ALPHA 254

struct glowing_block_sample {
        int x, y, z;
        uint32_t color;
};

struct glowing_block_light {
        float position[3];
        float color[3];
        float radius;
        float intensity;
};

bool glowing_blocks_placement_enabled(void);
void glowing_blocks_set_placement_enabled(bool enabled);

/* Record a local placement request and activate it only when the matching
 * server build packet is received. Coordinates use the renderer/map axes. */
void glowing_blocks_note_local_build(int x, int y, int z, uint32_t color);
void glowing_blocks_confirm_local_build(int x, int y, int z);

/* Keep the local registry aligned with ordinary map edits and replacements. */
void glowing_blocks_map_changed(int x, int y, int z, uint32_t color);
void glowing_blocks_clear(void);
void glowing_blocks_deinit(void);

/* Submit nearby coloured point lights to the forward-lighting queue. */
void glowing_blocks_prepare_frame(float view_x, float view_y, float view_z);

/* The colour a glowing block emits, normalised so its brightest channel is 1.0
 * (even a dark pick reads as a light). Shared by the point light and the
 * block's own faces so the two can never disagree. `peak` may be NULL. */
void glowing_blocks_light_color(uint32_t color, float rgb[3], float* peak);

/* Snapshot the blocks inside an inclusive map-space box. The terrain mesher
 * runs on a worker thread, so it takes one snapshot per chunk and looks voxels
 * up in that instead of locking the registry once per voxel. */
int glowing_blocks_collect(int min_x, int min_y, int min_z, int max_x, int max_y, int max_z,
                           struct glowing_block_sample* out, int capacity);

#endif
