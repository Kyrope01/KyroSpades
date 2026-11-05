# Default rifle high-resolution update

The bundled rifle choices are Default, Classic and Kar-98K. The Classic rifle
is unchanged; Kar-98K already has its high-resolution replacement. Default
now uses a 176-voxel scoped rifle inspired by the original 44-voxel model,
with the same dark body, optic, pale stock collar, magazine band and barrel.
It is an original interpretation, not a literal upscaled copy of the voxels.
The scope model has an open bore with small recessed details; see
`docs/previews/default-rifle-model-scope.png` for the actual voxel close-up.

The default scope keeps the original dark stepped optic, crosshairs, red
dot, transparent opening, placement and aspect ratio. The housing is redrawn
with straight-edged voxel steps and a flat dark finish; the original reticle is
retained rather than replacing it with an iron sight. The model and scope previews
are in `docs/previews/`. The default, Classic and Kar-98K firing sounds have
not been modified.

The 176-voxel model is scaled to the original 44-voxel physical length at
startup, in skin previews and in gameplay. Its pivot is matched to the old
default; hand and model alignment still need an in-game check. Other named
high-resolution weapon skins retain the existing 36-voxel sizing rule.

To regenerate, install Pillow and run `python3 tools/default_rifle/generate.py`.
The original scope used as a source is `resources/png/semi.png`. Run
`python3 tests/default_rifle_hd_test.py` and `python3 tests/weapon_skin_scale_test.py`
for structure, sizing and integration checks.
