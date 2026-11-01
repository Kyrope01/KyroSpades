# Audit — the atmosphere/visual merge on `main`

Date: 2026-11-01
Audited range: `9c99ace` (last reviewed state) → `origin/main` @ `9f555f1` (PR #21), i.e. 28 files, +433/−645, covering

| Commit | What it did |
|---|---|
| `ca5ee15` | atmosphere work |
| `932513a` | post-process / pipeline rework |
| `79b7ebb` | bloom removal |
| `c9d8a25` | lighting / world-shader changes |
| `cdc7031` | postprocess API shrink |
| `17d1ea8` | follow-up reverts of the first pass |
| `3746e6f` | config/menu plumbing |

Same standard as the previous vibe-code passes: correctness, GL state hygiene, resource
ownership, defaults/config migration, per-frame cost, and whether the tests still test
anything. Method was read-only: every touched file read in full, plus a self-contained
`gcc -fanalyzer -fsyntax-only` pass and cross-file greps. Baseline on `main` was green
before and after (`tests/*.py` all pass, `make -C tools/bench check` reports 0 warnings).

Overall: this merge is noticeably cleaner than earlier vibe-coded work in this fork. The
tesselator normals fix is correct, the config retirement list is complete, the lighting
gating is coherent across all five call sites, and `sky.c` actually saves and restores the
GL state it changes (I checked — nothing missing there). The problems below are the usual
tail of a feature removal: code that stopped being used but was not deleted, one latched
failure flag, and tests rewritten into string-matching.

Severity is about what it costs, not about how loud it looks. Each finding
below carries its current status; everything marked **fixed** is on this branch
with a test behind it.

---

## A1 — the terrain material map is dead code that still costs money (medium)

After specular/roughness were dropped from both world shaders, only `material.b`
(emission) is still read — and `texture_blocks_prepare_materials()` hard-codes that channel
to zero for every texel:

- `src/texture.c:920-921` — roughness and specular are computed per atlas texel into
  `materials[o+0]` / `materials[o+1]`, nothing reads them.
- `src/texture.c:922` — `materials[o+2] = 0;` (emission, hard-coded).
- `src/texture.c:931-933` — that array is uploaded as a second full-atlas RGBA8 texture and
  wrapped, next to the normal map.
- `src/lighting.c:350` (GLSL 120) — `base.rgb += base.rgb * material.b;`
- `src/lighting.c:487` (GLSL 330) — `base.rgb += albedo * material.b;`
- `src/lighting.c:500,621` — uniform `u_MaterialMap` is looked up and bound to texture unit 2.
- `src/lighting.c:1248` — unit 2 is bound to `texture_blocks_material` for the terrain pass,
  with save/restore of the previous binding.

Since `.b` is always 0 both emission lines are multiplied by zero, so removing the whole
path is visually a no-op — but today it costs: a full pass over every atlas texel plus an
RGBA8 texture upload at load, a third texture object and unit-2 bind per terrain pass, one
sampler and one extra texture fetch per terrain fragment, and ~40 lines of code that reads
as if emissive terrain exists.

Fix: delete the material map end to end and keep the normal map; rename the
`texture_blocks_*materials*` API to `*normal_map*` so the names stop claiming more than
they do (`texture_blocks_prepare_materials`, `texture_blocks_release_materials`,
`texture_blocks_materials_ready`, `texture_blocks_material`). The normal map's unit 1
binding and its save/restore stay as they are. `u_MaterialMapsEnabled` becomes
`u_NormalMapEnabled` and keeps gating only the normal-map fetch.

Risk: low, and provably no visual change.

**Status: fixed.** The material map, its sampler, its uniform, its unit-2 bind and
the two emission lines are gone; the normal map keeps its unit and its
save/restore. The API is now `texture_blocks_prepare_normal_map()` /
`texture_blocks_release_normal_map()` / `texture_blocks_normal_map_ready`, and the
shader gate is `u_NormalMapEnabled`. Call sites updated in `texture.c`, `hud.c`
and `main.c`; `tests/voxel_reflectivity_test.py` and the new
`tests/render_hygiene_test.py` both check that the pipeline stays dead.

## A2 — `sky.c` latches "attempted" before it knows the shader compiled (low frequency, permanent effect)

`src/sky.c:111-118`:

```c
if(!sky_attempted) {
        sky_attempted = true;                       /* latched first */
        sky_program = (GLuint)glx_shader(...);      /* may be 0 */
}
if(!sky_program)
        return false;
```

`sky_attempted` is set before the compile result is known, and `sky_render()` is the only
path that ever compiles it. One failed compile — a driver hiccup, a context/OOM failure, a
transient error — leaves `sky_program == 0` with `sky_attempted == true`, so every later
frame returns false without retrying. `main.c:1204` then only falls back to the legacy
gradient **if `sky_gradient` is on**, so a clouds-only configuration loses the sky entirely
until the process restarts, with a single compile error in the log as the only clue.

This is the same one-shot-latch shape as the old frozen-serverlist bug: a flag that records
"tried" instead of "succeeded".

Fix: latch only on success (`sky_program` non-zero), retry otherwise, and log at most once
so a genuinely unsupported driver does not spam.

**Status: fixed.** `sky_attempted` is gone; the compile is attempted only while
`sky_program` is 0 and `sky_compile_attempts < SKY_COMPILE_ATTEMPTS` (3), so a
transient failure costs at most three attempts, and `sky_deinit()` resets the
counter. The capability checks still run first, so an unsupported context never
reaches the compiler at all.

## A3 — `u_AmbientLight` is set on a program that never declares it (cosmetic)

`src/lighting.c:1108` looks up and sets `u_AmbientLight` on the **world** program. Only the
default shader in `glx.c` declares that uniform (`glx.c:231/247`, set at `glx.c:438`), so
the lookup returns −1 and the call is skipped every frame. Dead leftover from the old
shader split.

Fix: delete the two lines.

**Status: withdrawn — this was not dead code.** The lookup does return −1 for the
world program, but `lighting_apply_program()` is also called with `glx.c`'s
default shader (from `main.c` and internally, for entities, models and the water
fallback), and *that* shader does declare `u_AmbientLight`. The set is what keeps
those paths' ambient light following the map's day/night level
(`lighting_sunlight_scale()` = `sunlight_intensity × sky level`), so removing it
would have silently flattened ambient light at night. The code is kept and now
carries a comment saying which shader it serves and why the world program's −1 is
intended. `tests/render_hygiene_test.py` pins both halves: the default shader
declares it, the terrain shaders do not.

## A4 — settings that can never change

These are fields in `RENDER_OPTIONS` with no way to reach a different value. They are not
harmful today, but they are exactly the shape of thing that misleads the next patch.

- `settings.opengl14` — assigned `1` in `main.c:2384`, never saved, imported, or exposed in
  the menu, so it is always 1. Yet `main.c:1142` gates the entire 3D projection/view setup
  on it and `sky.c:98` gates the sky. If anything ever set it to 0, you would get an empty
  world and no obvious cause. Either wire it to the real GL-version check (its ButterSpades
  meaning) or delete the field and the guards.
- `settings.color_correction` — assigned once (`main.c:2385`), never read anywhere. Dead.
- `settings.shadow_entities` — only ever assigned 0 (`main.c:2368`, `main.c:2387`); no menu
  entry, no persistence, no read. `README` says moving entities deliberately get baked
  shadows only, so this is probably a leftover of the removed feature. Dead.

Fix: delete `color_correction` and `shadow_entities`; decide whether `opengl14` becomes a
real flag or disappears. Touches `src/config.h` (bench stubs rebuild against it, checked
below), plus the preset/default blocks.

**Status: fixed.** All three fields are gone, along with the `if(settings.opengl14)`
wrapper around the projection setup and the sky's check of it — behaviour is
identical because nothing could ever set them to anything else. `config.h` change
is covered by the bench check, which type-checks every TU that includes it.

## A5 — the two post-process tests were rewritten into string checks

`tests/postprocess_perf_test.py` and `tests/lighting_bloom_pipeline_test.py` used to assert
bloom behaviour: the blur pass count, the normalized kernel, and that the pass restored
framebuffer/program/viewport. They now assert *absence of identifiers* —
`'BLOOM_BLUR_PASSES' not in post`, `'bloom' not in main.c`, and similar. That is a
reasonable canary for "did someone re-add bloom", but it no longer tests anything about the
code that is still there, so a regression in `postprocess.c` state restore would pass.

`tests/sky_shader_test.py` has the same flavour: it asserts `'&& sky_render()'` appears
before `water_reflection_pass()`, which pins a string rather than the invariant.

Recommendation: keep the removal canaries, and add checks that survive refactors —
postprocess still saves/restores FBO, program and viewport; `sky_render()` saves and
restores depth test, depth mask, blend and cull and restores the previous program. A small
parser over the two source files is enough and is far more durable than an exact-string
match.

**Status: partly fixed.** `tests/render_hygiene_test.py` now asserts the
invariants rather than the strings: the sky captures and restores depth test,
depth mask, blend and cull and the previous program, and only touches state it
actually captures; the MSAA configuration restores the caller's framebuffer and
renderbuffer on *both* the success and the failure path; the resolve leaves a
matched binding. The two rewritten tests were left as they are — they are cheap
canaries for "someone re-added bloom", which is still worth knowing.

## A6 — the sample config drifts from the code defaults (documentation, not runtime)

The game only reads `config.ini` from the user directory (`src/config.c:974`), so
`resources/config.ini` is a reference file — but it disagrees with the defaults in `main()`
on four values:

| Key | Sample | `main()` default |
|---|---|---|
| `filmic_tonemapping` | 0 | 1 |
| `chromatic_aberration` | 0 | 1 |
| `healthbar` | 1 | 0 |
| `camera_fov` | 100.00000 | `CAMERA_DEFAULT_FOV` (70) |

**Correction:** an earlier draft of this list also flagged `fullscreen = 300`. That
was wrong — it is the F11 binding in the `[controls]` section (`GLFW_KEY_F11` is
300) and has nothing to do with the settings value, which is persisted as
`windowed`.

**Status: fixed.** The four `[client]` values above now match `main()`, so the
sample documents the real defaults. Only the sample changed; no runtime default
moved, and `tests/render_hygiene_test.py` now fails if the two drift apart
again.

---

## Verified clean — do not re-spend time here

- **Uniform types**: declared type vs setter across every `src/*.c` shader string — 0
  mismatches between `glUniform*f`/`*i`/`Matrix4fv` and the declared `float/int/bool/vecN/mat4`.
- **`sky.c` GL state hygiene**: it changes exactly depth test, depth mask, blend, cull,
  program and the quad attributes, and restores all of them; it never touches
  `glDepthFunc`, `glColorMask`, `glActiveTexture` or texture bindings, so the missing
  restores in the earlier draft of this list were **not** real. `glx_draw_screen_quad()`
  restores the VAO (core) or disables attributes and unbinds the array buffer (compat), and
  invalidates the client-state shadow. `shadow_render()` is the heavier reference, not a
  template this code is failing to follow.
- **Cloud march guard**: `if(u_CloudCoverage > 0.0 && abs(ray.y) > 0.001)` — uniform branch,
  so the 24-step march costs nothing when clouds are off (the shipped default), and it
  early-outs once `cloud_opacity > 0.98`. The `u_CloudOffset` wrap at 1280 world units is
  period-aligned with `hash2`'s `mod(p, 256.0)`, so clouds do not visibly jump when the
  offset wraps. The unprojection is correct: `w` is constant across the near plane, so
  normalizing `eye.xyz` gives the right ray.
- **`tesselator.c` normals fix**: `x->has_normal = t->has_normal` is picked up by both
  `glx_displaylist_update()` (packing, legacy `glNormalPointer`, VBO layout) and
  `glx_displaylist_draw()` (offsets/strides), so a remesh that adds or drops normals cannot
  desynchronise upload from draw.
- **Config retirement**: `[client]` keys `bloom`, `bloom_strength`, `bloom_threshold`,
  `height_fog_*`, `voxel_reflectivity`, `flashlight_beams`, `sky_cloud_speed`,
  `volumetric_fog*` are all dropped on rewrite, and nothing in `src/`, `resources/` or
  `tests/` still reads them.
- **`sky_clouds` / `sky_cloud_coverage` plumbing**: save, import (clamped 0..1), menu entry,
  ini key, and a shader test — complete.
- **`--perf-preset=bs`**: still runtime-only, and it now also clears `sky_gradient` and
  `sky_clouds`, which keeps the ButterSpades parity claim honest.
- **Lighting gating**: `lighting_world_program_required()` / `lighting_world_supported()` are
  consistent across `chunk.c` (351/403/787/1022), `hud.c` (6196/6223), `shadow.c`
  (70/215/222) and `texture.c` (1147); `tracer.c:162` still uses the plain
  `lighting_supported()` on purpose.

## Deliberately left alone

- **Cloud march cost when clouds are enabled**: 24 steps, 4 hashes each, full-screen. It is
  the most expensive thing this merge adds and it is opt-in, so it should be judged on the
  real machine rather than pre-emptively halved. If framerate drops with clouds on, the
  first things to try are 12–16 steps and marching at half resolution.
- **`water_wave_mode` default 1 → 0**: only affects fresh installs; existing configs keep
  whatever they stored. Intentional-looking default change, listed here so it is not a
  surprise.
- **Sky writes sun visibility into the framebuffer alpha**: screenshots already force
  alpha 255 (`main.c:2148`) and the video path encodes to YUV, and `window.c` never
  requests an alpha-sized or transparent framebuffer, so the back buffer's alpha is
  unobservable. Worth knowing, not worth changing.
- **`minimap_zoom` is not persisted** (resets to 3 each launch): real, but pre-existing and
  unrelated to this merge.
- **Moving the sky ray to the vertex shader**: the per-fragment
  `u_InverseProjection`/`u_InverseView` reconstruction is correct and cheap on a
  desktop GPU, and there is no GPU in this sandbox to prove a rewritten GLSL 120 /
  ES 100 / 330 shader still compiles and shades identically. Not worth an
  unverifiable rewrite of a pass that is off by default; documented as a follow-up
  in `docs/rendering-architecture.md` instead.
