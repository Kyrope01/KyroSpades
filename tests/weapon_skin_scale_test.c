#include <assert.h>
#include <math.h>
#include "../src/weapon_skin_scale.h"

int main(void) {
    const float s = 0.05f;
    /* Bundled and other legacy sizes, including the default 44-voxel rifle. */
    assert(weapon_skin_scale(s, 0) == s);
    assert(weapon_skin_scale(s, 32) == s);
    assert(weapon_skin_scale(s, 44) == s);
    assert(weapon_skin_scale(s, 63) == s);
    /* All high-resolution lengths fit the same target; no quantization jumps. */
    assert(fabsf(weapon_skin_scale(s, 64) * 64 - s * 36) < 0.00001f);
    assert(fabsf(weapon_skin_scale(s, 128) * 128 - s * 36) < 0.00001f);
    assert(fabsf(weapon_skin_scale(s, 143) * 143 - s * 36) < 0.00001f);
    return 0;
}
