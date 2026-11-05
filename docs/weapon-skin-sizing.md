# Automatic weapon skin sizing

Rifle, SMG and shotgun KV6 skin models are loaded at their existing scale when
their file Y dimension (the weapon's length) is below 64 voxels. All bundled
legacy weapon models are below this threshold, so their previous sizes and
pivots are unchanged. Other model categories are unaffected.

At 64 voxels or longer, the game assumes a higher-resolution weapon. It
uniformly scales the complete model—including its pivot—so its Y dimension
renders as 36 legacy voxels (1.8 game units). For example, a 144-voxel rifle
uses 0.0125 units per voxel instead of 0.05. All three axes use the same scale,
so different proportions are preserved. The artist should scale the KV6 pivot
along with the model, then adjust it as needed to align with the hands.

The unsuffixed default rifle is a special case: its original model was 44
voxels long, so its high-resolution replacement is fitted to 44 legacy voxels
(2.2 game units). This rule applies at startup and in both the skin preview
and selected model. Named rifle skins continue to use the 36-voxel target.

This is a heuristic, not a way to infer an artist's intent. A deliberately
oversized legacy custom weapon 64 voxels or longer will also be shrunk, and
all longer models get the same length even if the artist intended different
physical lengths. There is a size discontinuity at the threshold. If such
models need exact backward compatibility or individual physical sizes, the
format needs an explicit opt-in or scale metadata rather than guessing.
Unusually dense models may also be expensive to render, especially on mobile.

Run `python3 tests/weapon_skin_scale_test.py` to test the rule and verify the
legacy sizes and the default rifle exception. Visual alignment must be tested in
both the skin menu and gameplay.
