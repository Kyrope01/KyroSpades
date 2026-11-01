# Replacement Kar-98K rifle skin

Choose **Skins → Rifle → Kar-98K**. The override replaces the older bundled
Kar-98K model, scope overlay and firing sound without changing the game code.
The new unscoped model is inspired by the reference photographs supplied by
the user: a wooden stock, iron sights, bent bolt and barrel bands. It is an
interpretation, not a scan. The rifle is deliberately shown without a sling;
a rigid voxel sling would hang in front of the player's body and hands.

The redesigned model is 144 voxels long (4.5 times the earlier model's
32-voxel length), with a pivot proportionally matched to the earlier version.
Automatic weapon skin sizing renders it at 0.0125 game units per voxel; see
[weapon skin sizing](weapon-skin-sizing.md). The resulting weapon is about
12.5% longer than the previous 32-voxel model and needs an in-game alignment
check.

The ADS overlay is rendered from a copy of the voxel rifle, with an ADS-only
hollow rectangular front sight, centered post and squared side guards for a clearer aiming point. The blade tip
sits at screen center. The sight-base portion of the PNG is shortened for
clarity; these overlay refinements do not change the held model. The firing sound is the user's replacement recording, copied byte-for-byte
without audio processing. The game downmixes the stereo recording to mono and
applies its normal gun gain during playback. Source and processing details are in `tools/kar98k/audio/README.md`.
Reload audio remains the standard rifle reload. The preview images in
`docs/previews/` show a side projection of the actual voxel model and the scope
composited on a test scene; the colored landscape is not part of the PNG.

The model and preview generator `tools/kar98k/generate.py` requires Python and
Pillow. The separate `generate_sound.py` uses only the Python standard library. References include the user's
photographs, the [US Military Intelligence Service 1943 identification guide](https://www.lonesentry.com/manuals/german-infantry-weapons/kar98-german-rifle.html),
and the [Mauser 98k overview](https://www.ww2-weapons.com/mauser-karabiner-98k/).
Desktop post-build copying and Android asset packaging overlay these files on
top of `bsresources.zip`. Rebuild/repackage after changes. Run
`python3 tests/kar98k_assets_test.py` to check asset structure; visual
positioning and audio balance still need an in-game check.
