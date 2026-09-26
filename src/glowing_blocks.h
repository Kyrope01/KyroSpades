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
#define GLOWING_BLOCK_RAY_RANGE 50.0F

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

bool glowing_blocks_has_active(void);
/* Fast range check used to avoid a post-process pass when every remembered
 * source is already beyond its volumetric cutoff. */
bool glowing_blocks_has_nearby(float view_x, float view_y, float view_z, float range);

/* Select nearby wrapped copies once per frame, submit them to forward lighting,
 * and retain the same bounded selection for the volumetric-light pass. */
void glowing_blocks_prepare_frame(float view_x, float view_y, float view_z);
int glowing_blocks_frame_lights(struct glowing_block_light* output, int capacity);

#endif
