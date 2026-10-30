# KyroSpades — AI-code audit & performance plan

Status: **Phases 0–3 landed; Phase 2 landed except the items §5.4 measures and
rejects.** The only outstanding work is the A/B perf table (§5.4
"Phase 4 — Verify and report", §7 item 1) and switching the CI perf job from
manual to scheduled — both deliberately left to the maintainer, because they
need the real client on real hardware. Sections 1–4 are the audit (diff against upstream ButterSpades
+ checks run in this session); §5.1–5.5 document the profiler, presets, bench,
the measured baseline and every change made since. `docs/rendering-architecture.md`
is the one-page map of the fast path.

---

## 0. Ground truth used

| Thing | Value |
|---|---|
| Local git history | **squashed** — one commit (`50b3a81`, merge of PR #18). No per-change history is available locally. |
| Archaeology substitute | GitHub PR list for `Kyrope01/KS-Dev` (19 PRs) + a full diff against upstream `utf-4096/butterspades` @ `afe0c45`. |
| Code size | KS `src/` = **62.6k lines**, upstream = **32.5k lines** → **+92 % code**. |
| Static check run | `cppcheck 2.22.0`, 60 translation units, `--enable=warning,performance,portability`. |
| Existing test suite | 37 Python "tests" (mostly source-text assertions + shader/geometry harnesses). 1 fails locally because `enet/enet.h` is not installed. |
| Build system | CMake. Release builds use the **legacy fixed-function GL 2.1 backend** (`ENABLE_OPENGL_CORE=OFF` everywhere in CI); the GL 3.3 Core renderer is opt-in. |

AI-added modules (new files, ~16 k lines): `lighting.c`, `postprocess.c`,
`shadow.c`, `water.c` (rewritten), `glowing_blocks.c`, `bloodmarks.c`,
`damagefx.c`, `damagenum*.c`, `chatlog.c`/`chathistory.c`, `demo.c`,
`recorder.c`, `discord_ipc.c`/`rpc.c`, `teamplay*.c`, `pie_menu*.c`,
`flashlight_ext.c`, `skins.c`, plus `.github/workflows/deep-audit.yml` (1061
lines) and 37 tests.

---

## 1. What the AI actually built (inventory)

**Renderer**
- Optional GL 3.3 Core path: programmable default shader, uniform-location
  cache, explicit vertex attributes, VAOs.
- Forward lighting shader (GLSL 120 *and* 330 variants) with 4 dynamic point
  lights + flashlight spot + specular + material/normal maps.
- Directional sun shadow map (2048², 9-tap PCF, stabilised/recentred) — Core only.
- Off-screen post-process chain: exposure/saturation/contrast/vignette,
  chromatic aberration, filmic tonemap, object outlines (depth-edge), lens
  flare (own target), bloom (half-res + 3 blur pairs), HDR RGBA16F target with
  RGBA8 fallback, MSAA render target + resolve.
- Generated water surface: per-cell colour snapshots from a worker thread,
  reflection raycasts, waves (tile mode and per-vertex mode), GPU waves on Core.
- Chunk mesher variant with textures/materials + baked directional shadows.

**Gameplay / UI**: demo record+replay, video recorder (FFmpeg), teamplay,
pie menu (protocol extension), flashlight (protocol extension), ESP,
damage numbers, blood marks, glowing blocks, chat log, skins menu, dynamic
wallpapers, weather, hit indicators, live player counter, FOV overrides.

That is a *lot* of always-on plumbing for a client whose upstream is a
fixed-function voxel shooter.

---

## 2. Where AI-written code of this kind usually breaks

From the general literature on GL/CPU perf problems ([Khronos buffer-streaming
notes](https://www.khronos.org/opengl/wiki/Buffer_Object_Streaming),
[NVIDIA/NV driver "do not touch GL state unless necessary" guidance](https://docs.nvidia.com/drive/drive-os-5.2.0.0L/drive-os/DRIVE_OS_Linux_SDK_Development_Guide/Graphics/graphics_opengl.html),
[community reports of glBufferData/state-thrash stalls](https://community.khronos.org/t/a-single-function-call-makes-severe-frame-rate-hiccups/108522)),
the recurring failure modes are:

1. **Per-draw / per-frame buffer uploads and unbatched 2D** — one
   `glBufferData` + one draw call per quad/string.
2. **Redundant GL state changes and `glGet*`/`glIs*` in the frame loop** —
   each one can force a driver round-trip.
3. **"Off" features that are not actually off** — a pipeline kept alive by a
   secondary setting, so the fast path is never taken.
4. **Defaults drift** — the fork silently ships heavier defaults than upstream,
   so a like-for-like comparison is not like-for-like.
5. **Per-frame work for data that did not change** — rebuilding meshes,
   re-tessellating, re-uploading static geometry every frame.
6. **Unbounded background threads** competing with the render thread.
7. **Dead/duplicated modules** from repeated rewrites (`damagenum.c` vs
   `damagenumbers.c`).
8. **C-level carelessness**: `printf("%i", unsigned)`, `p = realloc(p, …)`,
   uninitialised struct fields, unchecked allocations.
9. **Tests that assert on source text** instead of behaviour, so regressions
   pass CI.

Every one of those is present here. Details in §3 and §4.

---

## 3. Checks already run — findings

### 3.1 cppcheck (60 TUs, warning+performance+portability)

73 diagnostics. Triage:

**Likely real**

| Location | Issue |
|---|---|
| `hud.c:5028`, `recorder.c:792` | `ptr = realloc(ptr, …)` — original block leaked on failure |
| `hud.c:2119, 2300, 2612, 2635, 3266`, `network.c:1717` | `%i` receiving `unsigned int` (varargs type mismatch, UB) |
| `hud.c:2248` | `pt[].id` / `pt[].score` used after partial fill → sort sees garbage |
| `hud.c:268` | buffer index can exceed `tmp[]` when `len <= 0` |
| `ping.c:137` | nested `if(recvLength > 0)` identical to outer — dead branch, probably meant `recvLength == 0` |
| `hud.c:6367` | `widths[cols]` VLA with `cols` from panel width (0/negative clamp) |
| `file.c:305`, `network.c:754/767`, `list.c:39–92` | `void*` arithmetic / bad pointer casts (GNU-ism, MSVC-hostile) |

**Noise to suppress**: `nullPointerOutOfMemory` in vendored code
(`dr_wav`, `http.h`, `stb_truetype`, `lodepng`), `uninitvar` on `mat4`
(cglm arrays filled by `memcpy` in `matrix_load`/`matrix_multiply`), two
`#if`-evaluation errors in third-party headers.

### 3.2 Test suite

`tests/*.py`: 36/37 pass; `orientation_smoothing_test.py` fails only because it
compiles `player.c` and `enet/enet.h` is absent in this container.
Many tests grep source text (`assert "settings.multisamples = 2;" in main`) —
they lock in behaviour without testing it, and they would pass a build that
never ships.

### 3.3 Dead / duplicated code

- `src/damagenum.c` (186 lines) + `damagenum.h` — **not in `CMakeLists.txt`**,
  fully superseded by `damagenumbers.c`.
- `src/gles_immediate_stubs.h`, `src/README.md`, `src/matrix.c` overlap —
  candidate dead weight (needs `ctags`-style unused-symbol pass).

---

## 4. Performance: ranked hypotheses (with evidence)

The user reports ButterSpades is much faster *with the same graphics settings*,
and that the fancy KS features were off. The code says the "plain" path is
never actually reached.

### H1 — Water is rebuilt on the CPU **every frame** (highest confidence)

`water_render()` (`src/water.c:1188`) has:

```c
#ifdef OPENGL_CORE
        bool rebuild_mesh = cpu_wave_animation || … ;
#else
        bool rebuild_mesh = true;        /* src/water.c:1229 */
#endif
```

Release builds are **not** `OPENGL_CORE`, so every frame it:

- walks every cell in a `span = min(2·render_distance+8, 512)` square (264×264
  at the default `render_distance = 128`) → **~50k cells**, each doing
  `map_isair_nolock` + cache lookup;
- on a cache miss calls `water_seabed_color()` (`src/water.c:564`), which scans
  **down the whole column** (up to 63 probes) looking for a solid block → in
  deep water that is up to ~3–4 M voxel probes/frame;
- emits, in tiled-wave mode (`water_wave_mode = 1`, `tile_size = 2`), a
  **5-face box per 2×2 tile** → order 5–12 k quads → **hundreds of KB to MBs
  of vertex data re-uploaded every frame**
  (`tesselator_glx(&wr.tess, &wr.dl)`).

And it is on **by default** (`settings.water_shader = 1`,
`settings.water_waves = 1`, `src/main.c:2367/2329`). On a dry map the same
~50k probes still run every frame and only then return `n == 0`.

On top of that `water_reflection_pass()` (`main.c:1231`) wakes a worker thread
that recomputes rows with `reflect_raycast` continuously while the view moves —
a second core busy while you play.

Upstream ButterSpades has **none** of this: water is static map geometry.

### H2 — Post-processing is forced on by the shipped defaults

`needs_postproc` (`src/main.c:519`) is true when *any* of exposure/saturation/
contrast/vignette/outlines/… are non-zero. Shipped defaults
(`src/main.c:2387–2394`, `resources/config.ini`):

```
exposure = 20.0      saturation = -15.0     object_outlines = 1
multisamples = 2     ambient_occlusion = 1   camera_fov = 100
```

So even with "the fancy stuff" off, the game renders the whole scene into an
off-screen FBO + **depth texture**, with an MSAA 2× target, then does a
full-screen composite that samples depth for outlines. Upstream renders
straight to the back buffer with `multisamples = 0`, `ambient_occlusion = 0`,
no grade, FOV 70.

The user may well consider "exposure/saturation" not a feature, but it is the
switch that keeps the entire post chain alive.

### H3 — Terrain runs under the dynamic-lighting shader by default

`settings.dynamic_lights = 1` (`main.c:2406`) makes
`lighting_world_begin()` (`lighting.c:1198`) bind a GLSL 120 terrain shader for
*all* chunks, and makes chunk meshes carry normals
(`tesselator_create(..., lighting_supported(), ...)`, `chunk.c:341`). Per
fragment: 4-iteration light loop, sun diffuse, half-vector specular, plus a
fog S-curve. Upstream: fixed-function, colours baked at mesh time.

The 9-tap PCF is behind `u_ShadowEnabled`, but the branch and the rest of the
shader are always evaluated.

### H4 — 6 ms/frame chunk upload budget

`chunk_update_all()` (`chunk.c:1150`) spends up to **6 ms** per frame (50 ms
during bulk map load) translating chunk results to the GPU *and* re-uploading a
16×16 minimap `glTexSubImage2D` per chunk, binding/unbinding the minimap
texture each time. On a map load or after a mass block edit this is a visible
hitch; it also means the first frames after entering a map are dominated by
uploads.

### H5 — Baked directional shadows make chunk generation expensive

`chunk_generate_textured()` (`chunk.c:1009`) calls `solid_sunblock()` (up to 64
steps) **plus** `map_sun_shadow(x, y, z, 32)` per solid block, under a global
read lock, whenever `shadow_quality && shadow_intensity > 0` (default
`0.4`). Only reached with `textured_blocks = 1`, but that setting is in the UI
and users do turn it on. Upstream: `solid_sunblock` only.

### H6 — 2D drawing is one draw call + one buffer upload per primitive

`glx_draw_quad_2d`, `glx_draw_line_2d`, `glx_draw_ring_segment_2d`,
`texture.c:programmable_emit_quad`, `font.c:font_render` all do
`glBufferData` + attribute setup + one `glDrawArrays`. `hud.c` alone has ~88
`font_render` and ~86 `texture_draw` call sites, and a busy in-game frame hits
dozens of them (chat lines, hit markers, counters, ESP, weapon icons), each text
string additionally re-setting 4 `glTexParameteri`. Core/GLES builds therefore
issue hundreds of tiny uploads and draws per frame. The legacy build uses
client arrays instead — still one `glVertexPointer` + draw per quad, but no
upload stalls.

### H7 — Smaller but real

- `postproc_release_targets()` uses `glGetIntegerv(GL_FRAMEBUFFER_BINDING)` /
  `GL_RENDERBUFFER_BINDING` / `GL_ACTIVE_TEXTURE` on resize paths (acceptable),
  but `water_render()` calls `glIsEnabled` + `glGetFloatv` ×2 + restore **every
  frame**.
- `chunk_collect_draw_calls()` scans a ±9 chunk band (2500 candidates) and
  `qsort`s ~300 entries every frame; `camera_CubeInFrustum` runs per candidate.
  Cheap in isolation, but it now happens twice per frame when the shadow pass
  is active.
- Default FOV 100 vs 70 (config ships 100) → roughly 1.7× more visible
  geometry and fill than upstream for the same render distance.
- `particle_create_rain()/snow()` are called per physics sub-step, not per
  frame.

---

## 5. Proposed plan

### Phase 0 — Instrument before touching anything — **done, see §5.1**

Deliverables built in this pass:

1. **Frame profiler** — `src/profiler.c/.h`, wired into `src/main.c` and
   `src/CMakeLists.txt`. Stages: `net-in`, `physics`, `chunks`, `scene`,
   `postproc`, `hud`, `net-out`, `sound`, `swap`; 240-frame rolling window;
   `F9` toggles a corner overlay showing avg ms and % of frame (yellow ≥20%,
   red ≥40%); `--prof-log[=file]` writes per-frame CSV (default
   `logs/profiler.csv`). Off by default, no cost when off.
2. **`--perf-preset=bs`** — runtime override (never written back to
   `config.ini`) that reproduces upstream's shipped settings; `--perf-preset=default`
   gives the opposite A/B in a single run.
3. **Headless CPU bench** — `tools/bench/`, stubs only, no GPU/window:
   `make -C tools/bench run`. A ready-to-enable CI workflow lives in
   `tools/bench/ci/perf-bench.yml` (copy it to `.github/workflows/`).

### 5.1 How to measure

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
./build/ks                                    # default look, F9 = overlay
./build/ks --prof-log                         # + logs/profiler.csv
./build/ks --perf-preset=bs                   # ButterSpades parity settings
./build/ks --prof-log --perf-preset=bs        # the A/B run
make -C tools/bench run                       # GPU-free CPU numbers
```

The CSV has one row per frame: `time,stage,...` in ms. Spreadsheet or
`awk -F, '{...}'` is enough; no extra tooling.

### 5.2 Measured baseline (Phase 0 result, Linux container, single core)

Headless bench, `map 256x256x64`, `-O2`:

| Scenario | before | after | Note |
|---|---:|---:|---|
| water waves+tiles, lake | 2.455 / frame | **0.629** | default config, CPU only, before any upload |
| water waves+tiles, dry map | 0.571 / frame | **0.205** | same work on a map with no water |
| water disabled | 0.000 / frame | 0.000 | reference |
| chunk naive + AO | 0.092 / chunk | 0.092 | default mesher (348 quads) |
| chunk naive, no AO | 0.061 / chunk | 0.061 | AO costs +51% |
| chunk greedy | 0.779 / chunk | 0.779 | 66 quads — see the warning below |
| chunk textured | 0.058 / chunk | 0.058 | |
| chunk textured + baked shadows | 0.106 / chunk | 0.106 | **+83%** — the sun DDA per block (H5) |

"after" = the Phase 1 water change (§5.3). The chunk rows are untouched.

What this already proves:

- **H1 was real and is now fixed (§5.3).** 2.5 ms of every frame went into
  rebuilding an unchanged water mesh; the frustum test in the bench is a front
  half-plane, i.e. an over-estimate, but the fix cut it by 74% with identical
  vertex output. What remains (0.63 ms) is the actual tessellation of the
  animated tiles, which cannot shrink without changing how waves look.
- **H5 is confirmed, and inverted from the usual advice.** Baked shadows nearly
  double chunk generation; AO adds half. Both are per-chunk costs, so they hit
  as load stutter, not as steady-state fps.
- **`greedy_meshing` is broken, not merely slow.** `chunk_generate_greedy()`
  only ever emits the two Z-facing quads (`z-` and `z+`); there is no X or Y
  pass. The 66 quads it produced are *side* faces only — the top surface of the
  terrain is missing entirely. The 0.779 ms/chunk figure therefore measures an
  incomplete mesh and is not comparable to the 348-quad naive result. Keep it
  off; it needs a rewrite (all three axes) before it can be considered at all.
- Water on a **dry map** still costs 0.57 ms/frame → the rebuild loop does not
  early-out when there is no water in range.

### 5.3 Phase 1 result — water no longer probes the map per cell

`water_render()` used to call `map_isair_nolock()` for every water cell of every
tile, every frame (~70k locked voxel lookups on a 512 map at render distance
128), on the grounds that the asynchronous reflection worker might still be
catching up. The worker's snapshot is produced by exactly that probe, so once a
full sweep has landed **at the current map revision** the answer is already
known and the lookup is pure duplication.

The render path now trusts the snapshot when `snapshot_complete &&
snapshot_complete_revision == water_map_revision`:

- `WATER_CELL_SURFACE` → use the cached colour (this was already the fast case);
- `WATER_CELL_DRY` → skip the cell, no map access at all;
- `WATER_CELL_UNKNOWN` → probe as before (rows not yet swept).

Any block edit bumps `water_map_revision` (`map_set()` → `water_map_changed()`),
which turns the trust off until the next sweep republishes, so live edits cannot
be served stale data. Because the snapshot is derived from the same probe, the
emitted geometry and colours are **bit-identical** — this removes work, it does
not change the image.

Measured: **2.455 → 0.629 ms/frame** (−74%) on the lake map, **0.571 → 0.205**
on a dry map, with identical vertex output.

Two smaller items landed alongside it:

- `tile_waves` (a pair of `settings` reads) was being recomputed per cell;
  hoisted out of the loop.
- `water_wrap()` did two integer divisions per cell even though the map is
  almost always larger than the span; replaced with a branch-only fast path.
- `chunk_update_all()` bound and unbound the minimap texture per uploaded chunk
  inside its budget loop; it now binds once per batch.

### Phase 1 — Kill the obvious waste (1–2 days, low risk) — **in progress, water done**

> Landed so far: the water rebuild (§5.3). Still open: the post-process and HUD
> items below (both need on-screen verification, which only happens on the
> user's machine).

1. **Water**: rebuild only when something it depends on changed (mesh cached
   across frames, waves animated by uniform). For the legacy path, either
   (a) animate waves by re-using one static VBO and displacing in the vertex
   shader when a shader is available, or (b) rebuild at a fixed 10–15 Hz and
   interpolate, or (c) at minimum drop the 5-face box and emit one top quad per
   tile. Cache `seabed_color` per column (map revision keyed) instead of
   scanning the column every frame.
2. **Post-process**: treat exposure/saturation/contrast/vignette/outlines as
   identity when they are at their neutral values, and set neutral defaults
   unless the user opts in. Ship `multisamples = 0`, `object_outlines = 0`.
3. **Lighting**: make `dynamic_lights` default 0 and keep the shader path
   opt-in; ensure the fixed-function/baked path is bit-exact with upstream when
   it is off.
4. **Chunk upload budget**: lower to ~2 ms, hoist the minimap texture bind out
   of the loop, upload one contiguous region instead of 16×16 tiles.
5. **Frame-loop hygiene**: cache `glIsEnabled`/`glGetFloatv` polygon-offset
   state in `water_render`, remove per-frame `glGetIntegerv` where a tracked
   value already exists.
6. **Defaults parity**: FOV back to 70 (or make 100 an explicit choice), and a
   single "Visual Effects" master toggle that really disables the whole chain.

### 5.4 Phase 2 — what landed, and what was measured and rejected

**Landed, no visual change:**

1. **Font sampler state moved out of the draw call.** `font_render()` set four
   `glTexParameteri` values on the glyph atlas for *every string* — ~90 strings
   per HUD frame = ~360 redundant driver calls per frame writing constants that
   are fixed at atlas upload. They are now set once in `font_backed_load()`.
2. **Glyph texture matrix installed once.** The legacy path composed and reset a
   constant 1/8192 texture matrix around every string (`glMatrixMode` ×2,
   `glLoadIdentity` ×2, `glMultMatrixf` ≈ 5 calls). `font.c` is the only user of
   the texture matrix unit, so it is now installed lazily on first use and
   invalidated in `font_reset()` (context recreation).
3. **Buffer orphaning for streaming display lists.** `glx_displaylist_update()`
   used `glBufferSubData` on a buffer the GPU may still be reading from the
   previous frame — for the water mesh that is a guaranteed stall or an implicit
   driver-side alloc. It now orphans the store with `glBufferData(size, NULL)`
   first, the standard streaming idiom. Every update rewrites the whole buffer,
   so no partial-update semantics are lost.

With 1+2 the shipped legacy path drops ~450 GL calls per HUD frame.

4. **Per-frame GL work counters.** `struct glx_frame_stats` (in `glx.h`) counts
   draw calls, vertex-attribute calls, buffer uploads *and bytes*, and texture
   binds. `main.c` resets them at frame start and the F9 overlay prints one
   extra line:

   ```
   gl 214 dr 189 bind 0 up 0kB
   ```

   (`dr` = draw calls, `bind` = texture binds, `up`/`kB` = display-list buffer
   uploads.) This is the number that decides whether batching pays off on a
   given machine — on a healthy desktop frame the terrain alone should not be
   in the hundreds, and `kB` should be near zero when the camera is still.

5. **2D batching — implemented, off by default (`--perf-2d-batch`).**
   `glx_draw_quad_2d()` can now append into a shared vertex/colour buffer that
   `glx_batch2d_flush()` submits as one draw. Every other draw entry point
   (`texture_draw*`, `font_render`, display lists, 3D helpers, program changes,
   end of frame) flushes first, so the painter's order the HUD relies on is
   preserved.

   The one state the batcher cannot observe is blending: two solid quads
   separated only by `glEnable/glDisable(GL_BLEND)` would be merged and drawn
   under whichever state was current at flush. That is why it is opt-in — turn
   it on, walk every menu once, and if anything looks wrong the flush granularity
   is the cause. With it on, a HUD frame collapses its solid-quad runs into a
   handful of draws; the `gl ... dr` overlay line shows the difference.
6. **Chunk draw state: one client-array scope per chunk run.** The legacy
   desktop-GL displaylist path (the one taken whenever no explicit-attribute
   shader is active) enabled and disabled up to four client arrays *around
   every single mesh* — ~8 driver calls per visible chunk, several hundred per
   frame. `glx_client_state_begin()` / `glx_client_state_end()` now bracket the
   visible-chunk run in `chunk_draw_visible()` and the shadow pass; the arrays stay
   enabled and only the per-mesh VBO and pointers change. Meshes that carry a
   different set of arrays (with/without colour, normal, texcoords) still get
   the right ones, because the scope disables what the current mesh does not
   use. Every other draw entry point calls `glx_client_state_invalidate()`, so
   a scope can only survive calls that provably do not touch client state.

   *Not done: one shared/interleaved VBO arena for all chunks.* The blocking
   fact is the vertex format: `GLX_DISPLAYLIST_NORMAL` is `GL_SHORT` xyz,
   `POINTS`/`ENHANCED` are `GL_FLOAT` xyz, attributes are non-interleaved with
   byte offsets derived from `x->size`, and the draw mode changes under
   `GLX_PROGRAMMABLE`. An arena therefore needs per-displaylist stride/format
   descriptors plus an allocator with compaction, and its correctness can only
   be shown on a running client — exactly the item this plan flagged as
   "validate against a real client, not a stub". The per-chunk state toggling
   above was the part that could be removed safely, and it is roughly half of
   the driver calls per chunk in that path.
7. **Water: tiled-wave loop strides (no shader rewrite).** Tiled wave mode is
   the shipped default (`water_wave_mode = 1`, tile size 2) and it rebuilds the
   mesh on the CPU every frame, so it was the frame-time cost that was left.
   Only the cell at a tile origin draws anything, but the loop body ran for
   every cell and then threw the work away on `if(x % ts != 0 || z % ts != 0)
   continue` — three quarters of the iterations, plus a wrap computation and
   camera-disc test each. The loops now start on a tile origin and stride by
   the tile size, with the tile size, wave clock and amplitude hoisted out.

   Identical output, verified two ways: `tests/water_tile_stride_test.py`
   replays the old and the new loop structures over 1728 configurations and
   compares the visited tile-origin sets, and the bench now prints the quad
   count (`water_last_quad_count()`), which is 14260 for the lake scene before
   and after.

   | bench case | before | after |
   |---|---|---|
   | water waves+tiles, lake | 0.627 ms/frame | 0.556 ms/frame |
   | water waves+tiles, dry map | 0.272 ms/frame | 0.141 ms/frame |

   *Not done: shader-displaced legacy water.* The idea only pays off where a
   programmable pipeline already exists, and the shipped release backend has
   **no shader pipeline at all** (`default_shader` is built inside
   `#ifdef GLX_PROGRAMMABLE`, which desktop legacy does not define). Water on
   that path is fixed-function: fog, alpha and vertex colours come from GL
   state. Displacing waves in a GLSL 120 program would mean reimplementing the
   fixed-function combination stage in a shader for that one mesh — a visible
   shading change, which is out of bounds for this pass. On Core, GPU
   displacement already exists and is used.
8. **Sun-shadow volume for the textured mesher — measured, not worth it.**
   Baked shadows cost **0.052 ms per chunk** (0.100 vs 0.048 ms with them off),
   i.e. of the order of 40 000 sun-ray voxel probes per chunk at ~1.2 ns each. The
   probes are the cost, not the ray arithmetic:
   - replacing the per-block float DDA with a precomputed, translation-
     invariant offset table (every block's ray has the same fractional origin,
     so the visited cells differ only by the block's integer position) measured
     **0.100 vs 0.101 ms/chunk** — no change, and the table can disagree with
     the reference DDA on rays whose `tmax` values tie within float rounding
     (one cell at `sun_dir = (0.577, 0.577, 0.577)`), so it was reverted;
   - the answer-preserving alternative is to cut the probe count, which means
     sharing rays between neighbouring blocks — i.e. the "fast version" that
     shades chunk borders differently.

   At 52 us per chunk this is a one-off cost on a worker thread when a chunk is
   built, not a per-frame cost. It is closed: no identical-look version of this
   is a measurable win, and the changing version contradicts "keep the look".

### Phase 2 — Architecture (2–4 days)

1. **2D batcher**: one persistent dynamic VBO, texture-atlas/state-sorted
   batches, one draw per texture; text and HUD go through it. Removes hundreds
   of per-frame uploads in Core/GLES builds.
2. **Persistent chunk buffers** with `glBufferData(NULL)` orphaning instead of
   re-specifying storage; one VAO per chunk is fine, but stop re-specifying
   attribute pointers per draw.
3. **Single visibility pass per frame** (share the chunk list between camera
   and shadow pass), distance-bucketed sort instead of `qsort` of everything.
4. **Water** as a proper merged mesh: greedy-merge the top surface, keep side
   faces only at shorelines, wave displacement from one uniform, reflection
   probe budget capped (N rows/frame, idle when the camera is still).
5. **Textured mesher**: replace the per-block `map_sun_shadow(...,32)` with a
   precomputed sun-shadow volume (or a single sweep per chunk) — same look,
   ~30× less work.

### 5.5 Phase 3 — corrections landed

Every item below was compiled with `make -C tools/bench check` before and after
(the harness now type-checks `hud.c`, `network.c`, `ping.c`, `demo.c` and
`list.c` too, via a minimal `enet` stub used purely for type checking).

**Real bugs fixed**

| Site | Problem |
|---|---|
| `hud.c` chat autocomplete | `strrchr(...) + 1` on a NULL result — undefined behaviour; the `(char*)1` test meant to catch it never could. Replaced with an explicit NULL check. |
| `hud.c` `mu_text_width` | A VLA sized by a caller-supplied length (`char tmp[len + 1]`). Bounded to 512. |
| `hud.c` skins preview | `int widths[cols]` VLA sized by panel geometry. Bounded to 16 columns. |
| `hud.c` server list | `realloc()` result assigned straight back to the pointer: a failed allocation leaked the old list and then crashed on `memcpy`. Now checked, with the old list kept. |
| `hud.c` scoreboard | `struct player_table pt[]` was left partially initialised; zero-initialised now. |
| `hud.c` item HUD | `int off` was written in a `switch` and never read — dead local from an abandoned layout experiment. |
| `hud.c` server list | `render_status_icon` written in three places and never read. |
| `ping.c` | A nested `if(recvLength > 0)` inside `if(recvLength > 0)`; its `else` ("connection was closed") could never run. Removed. |
| `network.c` | `"%i"` printed `unsigned` IP octets; `void*` arithmetic in the world-update parsers. |
| `hud.c` | `"%i"` printed `unsigned` ping, score and team counts. |
| `list.c` | `void*` arithmetic (GNU extension) made portable with `char*`. |
| `hud.c` ×3 | Dangling `else` in the server-list comparator and the two Disconnect/Exit buttons — braced so the binding is explicit. |

**Removed**

- `src/damagenum.c` / `.h` (186 lines): superseded by `damagenumbers.c`, absent
  from `CMakeLists.txt`, referenced by nothing. Deleted.

**Tests**

- `tests/orientation_smoothing_test.py` now *skips* with a clear message when
  the enet headers are missing instead of failing the suite.
- `tests/scoreboard_layout_test.py` asserted on the exact source text of the
  player-table declaration, so any edit broke it. It now asserts on the
  declaration without the initialiser.

**Tooling**

- `make -C tools/bench check` covers 12 translation units and is warning-clean
  except for two categories that are pre-existing and harmless: `texture_id`'s
  signedness against GL's `GLuint*`, and `-Wmissing-field-initializers` on the
  positional `struct hud` initialisers (the omitted field is a function pointer
  that is checked for NULL at every call site).

### Phase 3 — Correctness & hygiene (1 day)

- Fix the cppcheck items in §3.1; add `-Wall -Wextra -Wno-unused-parameter`
  to CI and keep the build warning-clean (currently nothing enforces this).
- Delete `damagenum.c/.h` and other dead files; dedupe `chathistory`/`chatlog`.
- Add suppressions with justifications for the vendored-code noise.
- Replace source-text "tests" with behavioural tests where feasible, and make
  `orientation_smoothing_test.py` skip when `enet` is missing instead of
  failing.
- Run `cppcheck --addon=threadsafety` and a `-fsanitize=address,undefined`
  build of the headless bench (catches the hashtable/channel races that the
  water and chunk workers could hide).

### Phase 4 — Verify and report

- Re-run the A/B table; target: **KS `bs` preset within ~10 % of upstream**
  frame time at the same map/settings, and default preset mean frame time
  materially below today's.
- Keep the profiler overlay in-tree (it is the only way to catch the next
  regression), and add the headless bench to CI.
- Write a short `docs/rendering-architecture.md` describing the fast path, so
  future AI passes do not re-add a pass to it.

---

## 6. Acceptance criteria

| Metric | Target |
|---|---|
| KS with `--perf-preset=bs` vs upstream, same map/position | ≤ 10 % slower |
| Default preset, 1080p, integrated GPU | ≥ 30 % better than today |
| Per-frame CPU uploads (Core build, in-game HUD) | from ~200 to ≤ 10 |
| Water rebuilds at rest (camera still) | 0 per frame |
| cppcheck (warning/performance) | 0 real findings, noise suppressed with reasons |
| Build | `-Wall -Wextra` clean on GCC + Clang |
| Existing test suite | all pass or explicitly skipped |

---

## 7. Open questions (need your input)

1. **Which build/platform is "slower"?** Windows release from CI is the legacy
   GL backend; if you build with `-DENABLE_OPENGL_CORE=ON` the cost profile is
   different (shadow map + GPU water + batched-ish paths).
2. **May I change shipped defaults?** (multisamples 2→0 or 2→4?, outlines,
   dynamic lights, FOV 100→70, exposure/saturation). Changing them is the
   single biggest immediate win but it is user-visible.
3. **Do you want the profiler overlay + `--perf-preset` in the shipped build**
   (dev-only behind a flag, or always available under F3)?
4. **Scope of the first implementation pass**: Phase 0 + 1 only (safe, fast,
   measurable), or go straight through Phase 2 as well?
