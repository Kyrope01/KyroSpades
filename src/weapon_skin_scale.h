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

/* The bundled default rifle originally spans 44 voxels, unlike the 36-voxel
 * target used for optional high-resolution skins. Preserve its old size and
 * pivot when its default asset is replaced with a high-resolution version. */
static inline float weapon_default_rifle_scale(float legacy_scale, unsigned int length) {
    if(length < 64)
        return legacy_scale;
    return legacy_scale * 44.0f / (float)length;
}

#endif
