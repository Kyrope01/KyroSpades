#ifndef WEAPON_SKIN_SCALE_H
#define WEAPON_SKIN_SCALE_H

/* KV6 rifle, SMG and shotgun models use the file's Y axis for length.
 * Normal-resolution skins retain their historical size. Longer models are
 * interpreted as high-resolution and uniformly fitted to 36 legacy voxels.
 * This is intentionally only used for weapon skins, not arbitrary KV6s. */
static inline float weapon_skin_scale(float legacy_scale, unsigned int length) {
    if(length < 64)
        return legacy_scale;
    return legacy_scale * 36.0f / (float)length;
}

#endif
