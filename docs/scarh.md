# SCAR-H SMG skin

Select **Skins → SMG → SCAR-H**. This optional skin is an original voxel
interpretation of the user's reference images: tan receiver, extended stock,
top rail, side details, straight 7.62 magazine and barrel. The model is 144
voxels long and uses the existing high-resolution weapon scaling. The pivot is
matched to the default SMG; hand placement still needs an in-game check.

The 1024×768 transparent ADS overlay projects the actual voxel model and adds
voxel-style front wings and a circular aperture with a one-pixel post, whose
tip is at the screen center. The sight preview's landscape is illustrative;
it is not in the game asset. The firing sound is the user-supplied WAV,
copied unchanged; see `tools/scarh/audio/README.md`. It changes appearance
and sound, not SMG gameplay.

Run `python3 tools/scarh/generate.py` with Pillow installed to regenerate the
KV6, PNG, and previews in `docs/previews/`. Run the `tests/scarh_assets_test.py` and `tests/scarh_sight_test.py` Python
scripts to check structure and integration. The projections are not in-game screenshots.
