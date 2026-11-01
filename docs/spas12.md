# SPAS-12 shotgun skin

Select **Skins → Shotgun → SPAS-12**. This is a new optional model, not a
replacement for an existing shotgun. It depicts the stock extended, as in the
lower half of the user's reference photograph. It includes a skeletal stock,
pistol grip, ribbed fore-end, separate barrel and magazine tube. The model is
an original voxel interpretation, not a downloaded or scanned model.

The model is 112 voxels long; automatic weapon skin sizing renders it at 36
legacy shotgun voxels in length. Its pivot is aligned to the default shotgun's
pivot at the new scale, but hand placement still needs to be checked in-game.
The skin includes a voxel-style ADS sight, rendered from the actual shotgun
model with a reference-inspired square hood and a one-pixel centered post. The
landscape in the sight preview is only a background; the actual PNG is
transparent. The sight detail exists only in the PNG, not the held model.
The firing sound is the user's supplied WAV, copied unchanged; see `tools/spas12/audio/README.md`.
No gameplay changes are included.

Desktop post-build copying and Android asset packaging overlay the model from
`resources.override` after the bundled archive. Desktop model selection can be
verified with `python3 tests/spas12_assets_test.py`. Existing shotgun skin
choices are saved as numeric indices, so adding a new sorted skin may change an
existing saved shotgun selection; reselect the desired skin if necessary.

The generator `tools/spas12/generate.py` needs Python and Pillow and produces
the KV6, sight PNG, and the preview images in `docs/previews/`. The previews
are projections of the model, not in-game screenshots.
