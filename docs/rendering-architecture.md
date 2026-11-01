# Rendering architecture — the fast path

Read this before adding a render pass. Almost every performance regression in
this fork came from a *new* pass being bolted onto the frame loop without
anyone noticing what it cost per frame.

## Backends

| Build | Macro | Reality |
|---|---|---|
| Desktop, default (CI Release) | — | Compatibility GL 2.0+, fixed-function 2D, VBOs for terrain/water |
| Desktop, `ENABLE_OPENGL_CORE=ON` | `OPENGL_CORE` | GL 3.3 core; shaders everywhere, no fixed function |
| Android / iOS ES | `OPENGL_ES` | GLES 1.1 or 2.0; no immediate mode, no `glBegin` |

Code that behaves differently per backend uses `#ifdef OPENGL_CORE`,
`#ifndef OPENGL_CORE` and the runtime `glx_version` / `gles_version` flags.
`water.c` is the place where this matters most (see below).

## One frame, in order

```
display()
  window_apply()
  bind scene FBO              <- only when needs_postproc
  chunk_update_all()          <- terrain uploads, budgeted (see below)
  glClear()
  [scene]
    shadow_render()           <- baked sun depth map, own FBO; skipped at night
    sky_render()              <- only when sky_gradient or sky_clouds (see below)
    water_reflection_pass()   <- queues one water slice for the worker thread
    drawScene()               <- chunks, entities, water, particles
    post-process              <- only when needs_postproc
  HUD (microui + 2D + text)
  overlay (F9 profiler)       <- only when enabled
```

The sky must stay **before** `drawScene()`: it renders with `GL_DEPTH_TEST`
disabled and the depth mask off, so geometry only stays in front of it because
the geometry is drawn afterwards. It also has to be inside the scene FBO, which
is why the bind happens before the frame loop's scene section.

### Terrain

`chunk_draw_visible()` collects visible chunks once (`chunk_collect_draw_calls`),
sorts them near-to-far, binds the block atlas **once**, and then issues one
`glx_displaylist_draw()` per chunk. Do not add per-chunk texture binds or state
changes; the loop is written to change state only when the wrapped map copy
changes.

Meshing happens on worker threads (`chunk_generate`) and lands in
`chunk_update_all()`, which spends at most 6 ms (50 ms during a bulk map load)
per frame. Anything you add per uploaded chunk is multiplied by 1024 on a full
map load.

### Water

The legacy path rebuilds the water surface **every frame**: the wave animation
is baked into vertex colours and heights, so there is nothing static to cache.
`water_render()` therefore costs ~0.6 ms of CPU on a 256² map before any upload.
Two rules keep that cost sane:

1. Do not add per-cell map queries to `water_render_tile()`. The reflection
   worker's snapshot is authoritative while `snapshot_complete` holds at the
   current map revision; `water_sample_surface_cell()` trusts it and skips the
   locked voxel lookup.
2. Do not add another full-surface pass per frame. If water needs something
   new, budget it like `water_reflection_pass()` does (one slice per frame, on
   the worker).

`OPENGL_CORE` builds displace fixed water in the vertex shader, so they skip the
rebuild entirely. That is the model to move the legacy path toward.

### Sky

`sky_render()` (`src/sky.c`) draws one camera-oriented full-screen quad: no depth
writes, no depth test, no cull, no blend. It only runs when `sky_gradient` or
`sky_clouds` is on, and only when a shader path exists (`glx_version` on desktop,
`gles_version >= 2` on ES, plus the default shader on `GLX_PROGRAMMABLE` builds);
otherwise `main()` keeps the old gradient-quad fallback. Those capability checks
come **before** the compile, and the compile itself is retried
(`SKY_COMPILE_ATTEMPTS`), so a transient shader failure cannot retire the sky for
the rest of the session.

Cost model, because this is the one pass that shades the whole screen:

- The cloud march is 24 voxel steps of value noise. It sits behind
  `if(u_CloudCoverage > 0.0 && ...)`, a **uniform** branch, so with clouds off
  (the default) the loop costs nothing and the sky is a handful of ALU ops per
  pixel. Turning clouds on is the single most expensive visual option in this
  file.
- Cloud noise is evaluated in world space (`voxel = floor(p / 8.0)` and the
  camera offset), so clouds do not swim when the camera moves, and the
  `u_CloudOffset` wrap at 1280 units is aligned with the noise period
  (`mod(p, 256.0)`), so the wrap is invisible.
- The ray is reconstructed per fragment from `u_InverseProjection` and
  `u_InverseView`. That is correct but not free; if this ever shows up in a
  profile it can move to the vertex shader as an interpolated, unnormalised
  direction.
- The sky writes sun visibility into the framebuffer alpha. The lens-flare pass
  reads it back as its cloud mask (`useCloudMask`), and screenshots force the
  alpha opaque before encoding.

State handling: `sky_render()` saves and restores depth test, depth mask, blend,
cull and the current program, and `glx_draw_screen_quad()` restores the quad VAO
(core) or disables its attributes and unbinds the array buffer (compat) plus
invalidates the client-state shadow. It touches no texture unit, so it does not
need the unit-2 treatment `shadow_render()` uses. Anything added to the sky pass
that binds a texture or changes depth *function* has to start saving those too.

### Day and night

The server owns the time of day (Daytime and Weather, extension `0x33`; see
`docs/networking.md`). The client stores one number, `sunlight`, from
`daytime_weather_sunlight()`: `1.0` by day and whenever the time is unknown,
`0.0` at night. It is deliberately **one** factor used in **two** places, because
the engine already derives everything sun-like from the sky level:

| Reads | Where | Effect at night |
| --- | --- | --- |
| `lighting_sunlight_scale()` | `lighting.c:37` | sun colour, shadow intensity, `sky.c` `u_Daylight`, lens flare, water sun term, model lighting all reach 0 |
| fog colour scale | `fog_color_render()` in `map.c:59` | the fog and the clear colour go black, so the horizon matches the sky |

Dynamic lights are added on top of the sun term in the shaders and do not read
either, which is the point of the feature: at night a flashlight and torches are
the only light. Do not multiply the ambient term as well — that dims the
flashlight too.

Two things follow from "there is no sun":

- `shadow_render()` is skipped at night (`main.c`). The depth map is only sampled
  by the sun term, which is already zero, so the pass would be pure cost.
- The baked, non-shader paths (`lighting_world_supported()` false) re-bake their
  chunk light once per flip, not per frame; `daytime_weather.c` calls
  `chunk_rebuild_all()` when the time actually changes.

The world is also not drawn until the first Sky has arrived: `drawScene()` and
the sky/fog clear wait on `daytime_weather_awaiting_sky()`, and the HUD shows the
loading screen while it does. That hold is bounded by a 3 s timeout on the
client side so a silent server cannot wedge the frame.

### Glowing blocks

A block placed with the colour picker's glow toggle (`src/glowing_blocks.c`) is a
client-side light. Two things have to agree or it looks like a block standing in
someone else's spotlight: the pool it casts, and its own surface.

* **The pool** is a coloured point light submitted to the same four-light forward
  queue the flashlight uses, so it reaches terrain, water, models and the entity
  shaders through the established `u_LightPositionRadius` contract. Radius and
  intensity come from `glowing_blocks_light_color()`, which normalises the picked
  hue so its brightest channel is 1.0 — a dark pick still reads as a light.
* **The surface** is meshed as emissive. Each chunk snapshots the registry once
  (`glowing_blocks_collect()`, one lock for the whole chunk because meshing runs
  on a worker thread) and the three meshers paint a matching voxel's faces flat:
  no `solid_sunblock()` shade, no per-face factor, no AO, and a vertex alpha of
  `GLOWING_BLOCK_VERTEX_ALPHA` (254). Greedy merging refuses both sides of a
  merge across a glowing voxel, or a neighbour would be repainted as a light.
* **The shaders** read that alpha. Both terrain fragment shaders
  (`lighting.c`, GLSL 120/ES and 330) and `glx.c`'s default shader replace the
  lighting multiply with `1.25` for a marked vertex, so the face keeps its colour
  in shadow, in a cave and at night, and the HDR bloom pass has something to
  bloom on. Every other terrain vertex is 255 and is shaded as before.
* The legacy fixed-function path bakes vertex colours, so it gets the flat
  full-bright colour but no marker; without a shader there is nothing to skip.

Do not reuse alpha for anything else on terrain: 254 is the marker, 255 is
normal. The default shader's alpha cutoff is 0 for terrain, and terrain is drawn
unblended, so a 254 does not punch a hole.

### Post-process

`needs_postproc` in `main.c` is the gate. It is false when every grade value is
neutral and no feature needs the depth texture, in which case the frame renders
straight to the back buffer. Any new effect must extend that condition, or it
will silently force an off-screen target (a full-screen resolve + composite per
frame) even when it is switched off.

### 2D and text

`glx_draw_quad_2d()`, `texture_draw*()` and `font_render()` each issue their own
draw call. `font_render()` used to re-specify four `glTexParameteri` values and
compose a texture matrix per string (~450 driver calls per HUD frame); do not
reintroduce per-string state. Static sampler state belongs in
`font_backed_load()`, and the glyph texture matrix is installed once.

The F9 overlay prints a `gl ...` line with the per-frame draw / bind / upload
counts. Check it before and after touching the 2D path — if the draw count
went up, you added calls.

`--perf-2d-batch` merges runs of solid quads into one draw
(`glx_batch2d_*`). Any new draw entry point **must** call
`glx_batch2d_flush()` before touching GL, or queued quads will be reordered
past it.

The chunk loop holds a client-array scope (`glx_client_state_begin()` /
`glx_client_state_end()` around the visible-chunk run): the legacy desktop-GL
path keeps its vertex/colour/normal/texcoord arrays enabled across meshes
instead of toggling them per draw. Any draw entry point that enables or
disables a client array, or leaves one pointing into a different buffer, must
call `glx_client_state_invalidate()` — every existing one does, including the
compiled-display-list branch of `glx_displaylist_draw()` (a recorded list
re-issues its own client-state commands) and the fixed-function path of the
2D batcher. Without the scope the per-mesh path is used, so a stray draw
outside a scope is safe; a draw *inside* a scope that forgets to invalidate is
not. Invalidating only ends the trust window for the rest of that scope — the
draws after it fall back to the per-draw path and still render correctly.

## Rules of thumb

- Per-frame work must be justified per frame; per-load work belongs on the
  worker threads.
- Nothing that is disabled may keep a pass alive. "Off" means no FBO, no
  shader, no upload. The sky follows this: with `sky_gradient` and `sky_clouds`
  both off it is never called, and its cloud loop is behind a uniform branch
  rather than a per-pixel comparison.
- Uploads: `glx_displaylist_update()` orphans the buffer before rewriting it.
  Do not call `glBufferSubData` on a buffer the previous frame may still be
  reading.
- Measure with `make -C tools/bench run` for CPU-side passes and the F9 overlay
  for everything else. `make -C tools/bench check` type-checks the renderer's
  translation units without a GPU.
