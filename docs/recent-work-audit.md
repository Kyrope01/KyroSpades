# Recent work audit

Scope: changes on this branch since `756a16d`, including weapon assets and skin selection, model rendering and sizing, block placement, multiplayer menu simulation, flashlight shaders, default graphics, and packaging. The branch history available for this review starts on November 1; commit timestamps include future-dated entries relative to the review environment. This is not a security audit of the entire engine.

## Fixed

- **GPU resource leak on model replacement:** `kv6_load` cleared the display-list flag before `kv6_reload` attempted to release the old display lists. Reloading a skin or tracer could leak GPU resources. Old lists are now released before loading the replacement.
- **Preview resource leaks:** every visit to the Skins menu recreated previews but freed only their containing structures, not voxel data or GPU lists. Rescans now release both.
- **Broken/malformed custom KV6 files:** reloads previously returned success for a bad magic value and could read past truncated data or write past the voxel array for inconsistent column counts. They now check the header, dimensions, pivots, file bounds, z coordinates and total column counts before changing the current model. An invalid custom tracer falls back to its default; a failed skin selection no longer partially swaps the sight, sounds and tracer. Initial model loads use the same validation.
- **Large model stack use:** voxel-count-sized temporary mesh and point-render arrays are heap allocated with allocation checks, reducing the chance of stack overflow with high-resolution models, especially on mobile. This is a reliability improvement, not a measured frame-rate improvement.
- **Packaged Android skin discovery:** skin enumeration now uses the existing platform-aware directory listing API rather than `opendir`/`stat` directly. This allows packaged assets to be listed when no filesystem `kv6` directory shadows them. Real-device validation remains necessary.

## Remaining risks / recommendations

- **Selected skins are still stored by sorted numeric index**, not filename. Installing/removing a skin that sorts earlier can silently select another skin after a restart or menu rescan. Flip preferences themselves are stored by name, but selection needs a backwards-compatible migration to name-based persistence, including missing/reinstalled mods and external config edits. Avoid adding/removing skins while relying on an existing saved selection until that migration is implemented.
- **Android directory overlay:** the existing `file_dir_list` uses the filesystem directory *or* APK directory, not a merged listing. If an Android filesystem `kv6` directory exists, packaged-only named skins may not appear. A general merged overlay needs duplicate precedence and real-device tests.
- **Automatic sizing is heuristic:** weapon models with a Y dimension of 64 or more are treated as high-resolution, which can resize intentionally large legacy mods. A future explicit per-skin scale override would remove the ambiguity without changing old assets.
- Multiplayer block placement, menu input suppression, flashlight visuals, flip orientation and projectile appearance still require end-to-end testing on live servers and actual graphics backends. Automated tests do not prove server compatibility, GPU rendering, or subjective visual quality.
- KV6 startup and reload now validate structural bounds, but this does not constitute a complete hostile-asset security review of all rendering or resource loaders. Malformed PNG/WAV files and extreme but structurally valid models need separate limits/testing.

## Verification

Python regression scripts, including compiled C harnesses, were run for the changes. The KV6 audit harness checks bundled models, malformed/truncated files, unchanged state after failed reload, and release of old GPU handles; the scan harness exercises filename filtering and preview selection. Most graphics/integration tests are source-level or stubbed checks. The sandbox lacks the native CMake and graphics development dependencies, so no complete native build, real Android launch, performance benchmark, or in-game playtest was performed.
