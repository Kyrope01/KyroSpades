# main.c review follow-up

## Confirmed and fixed

- History rotation omitted entry 126 when shifting to slot 127; text, color and time now move together over all 126 entries.
- The MS-compatible random generator now uses defined 32-bit unsigned wraparound. Its output sequence retains the intended low 32-bit recurrence.
- Text insertion and HUD clipboard/delete paths clamp stale cursor and selection indices before using them.
- Dropped intel uses protocol z (downward) to search toward the ground, under one map read lock, converting to engine y for each probe. The solid voxel's top is `map_size_y - protocol_z`; the KV6 mesh subtracts `zpiv` vertically. Bundled intel's lowest voxel is at z=0, so placing its origin at top + `zpiv * scale` aligns its bottom with the surface. Invalid horizontal/nonfinite coordinates are skipped before conversion or rendering; a valid empty column falls back to the network height. A custom model with a nonzero minimum voxel z may need additional adjustment. Visual validation on ramps, water, unusual maps and custom intel models is still needed.
- Screenshot animation texture uses its actual GL type rather than aliasing an `int` through a `GLuint *`. Screenshot buffer multiplication and pixel indexing are size-safe. Startup error draining is bounded for persistent GL errors.

## Claims requiring qualification / deferred work

- FPS is already stored as `int`, and the existing positive-delta test and `fmin` cap prevent zero, tiny, negative, NaN and infinite deltas from producing an invalid integer conversion. No extra division workaround was necessary.
- The two `player_update` calls are not duplicate movement: locked fixed steps call `player_move`; the unlocked pass does remote orientation and correction smoothing and updates corpses. Both are required. Their contract is documented in `player.h`.
- A broad `display()`/framebuffer/screenshot/render-order refactor is deferred: there is no full GL runtime here to establish state and frame-order equivalence. No performance improvement is claimed without measurement.
- This document covers the findings preserved in the session context, not a verified item-by-item disposition of the original 32-point review; the full list was not available in the continued context. Other claims remain unverified.

## Verification limitations

`tests/main_review_test.py` exercises the actual extracted RNG, history and objective helper with UBSan and fake map data. Full native build and GPU/playtesting are unavailable in this environment; fake tests do not prove appearance or GL correctness.
