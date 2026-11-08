#ifndef BLOCK_PLACEMENT_H
#define BLOCK_PLACEMENT_H
#include <stdbool.h>
#include <math.h>

/* KS map Y is inverted relative to the protocol: a protocol voxel at Z maps
 * to KS cell 63-Z, while continuous positions map to 64-Z. The physics eye
 * stored by KS uses 63-Z, so add one before comparing against map cells. */
static inline bool block_placement_in_range(int x, int y, int z, float eye_x, float eye_y, float eye_z) {
    return fabsf(x + 0.5F - eye_x) < 3.0F &&
           fabsf(y + 0.5F - (eye_y + 1.0F)) < 3.0F &&
           fabsf(z + 0.5F - eye_z) < 3.0F;
}

static inline bool block_placement_overlaps(int x, int y, int z, float eye_x, float eye_y, float eye_z, bool crouched) {
    float feet = eye_y - (crouched ? 0.35F : 1.25F);
    float head = eye_y + 1.45F;
    return (float)x < eye_x + 0.45F && (float)(x + 1) > eye_x - 0.45F &&
           (float)z < eye_z + 0.45F && (float)(z + 1) > eye_z - 0.45F &&
           (float)y < head && (float)(y + 1) > feet;
}
#endif
