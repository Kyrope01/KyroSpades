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

#endif
