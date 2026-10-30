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
  chunk_update_all()          <- terrain uploads, budgeted (see below)
  glClear()
  [scene]
    water_reflection_pass()   <- queues one water slice for the worker thread
    drawScene()               <- chunks, entities, water, particles
    post-process              <- only when needs_postproc
  HUD (microui + 2D + text)
  overlay (F9 profiler)       <- only when enabled
```

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
call `glx_client_state_invalidate()` — every existing one does. Without the
scope the per-mesh path is used, so a stray draw outside a scope is safe; a
draw *inside* a scope that forgets to invalidate is not.

## Rules of thumb

- Per-frame work must be justified per frame; per-load work belongs on the
  worker threads.
- Nothing that is disabled may keep a pass alive. "Off" means no FBO, no
  shader, no upload.
- Uploads: `glx_displaylist_update()` orphans the buffer before rewriting it.
  Do not call `glBufferSubData` on a buffer the previous frame may still be
  reading.
- Measure with `make -C tools/bench run` for CPU-side passes and the F9 overlay
  for everything else. `make -C tools/bench check` type-checks the renderer's
  translation units without a GPU.
