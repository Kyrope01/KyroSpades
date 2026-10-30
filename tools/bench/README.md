# Headless CPU benchmarks

Measures the CPU cost of the renderer's hot terrain passes **without a GPU,
window system or GL headers**. The real `src/water.c`, `src/chunk.c`,
`src/tesselator.c`, `src/channel.c`, `src/hashtable.c` and
`deps/libvxl/libvxl.c` are compiled unmodified; only the pieces that need a
window, a driver or a map lifecycle are replaced (`stubs/`).

```sh
make -C tools/bench        # build
make -C tools/bench run    # build + run
make -C tools/bench check  # syntax/warning check of the GL-facing sources
```

`check` type-checks `water.c`, `chunk.c`, `tesselator.c`, `glx.c`, `font.c`,
`texture.c` and `postprocess.c` against the stubs with `-Wall -Wextra`. It is
the cheapest way to catch a typo, a wrong GL enum or a bad rename in the
renderer without building the whole client (`hud.c` is excluded: it pulls in
enet, which is not vendored).

## What is measured

| Scenario | Why it matters |
|---|---|
| `water waves+tiles, lake` | The legacy desktop renderer rebuilds the whole water surface on the CPU **every frame** (`water.c`: `rebuild_mesh = true` outside `OPENGL_CORE`). Default config has `water_shader = 1`, `water_waves = 1`. |
| `water waves+tiles, dry map` | Same probe/tessellation walk on a map with no water at all: pure waste. |
| `water disabled` | The fast path, for reference. |
| `chunk naive + AO` | Default mesher (`greedy_meshing = 0`, `ambient_occlusion = 1`). |
| `chunk greedy` | Optional greedy mesher. |
| `chunk textured` | `textured_blocks = 1` mesher. |
| `chunk textured + baked shadows` | Same, with `shadow_quality`/`shadow_intensity` on: one sun DDA raycast **per solid block**. |

## Baseline (container, `-O2`, single core)

```
map 256x256x64
water waves+tiles, lake          0.629 ms/frame   (was 2.455 before the snapshot fix)
water waves+tiles, dry map       0.205 ms/frame   (was 0.571)
water disabled (fast path)       0.000 ms/frame

chunk naive + AO                 0.092 ms/chunk  (348 quads)
chunk naive, no AO               0.061 ms/chunk
chunk greedy                     0.779 ms/chunk  ( 66 quads)
chunk textured                   0.058 ms/chunk
chunk textured + baked shadows   0.106 ms/chunk
```

Absolute numbers depend on the machine; the **ratios** are what the plan is
built on. On a phone or a low-end laptop, expect the same shape, roughly 2–4×
larger in absolute terms.

## CI

`ci/perf-bench.yml` is a ready-made GitHub Actions workflow (build + run this
bench, plus `-Werror`/cppcheck on the new files). It is not enabled yet — copy
it to `.github/workflows/` to turn it on.

## Caveats (read before quoting a number)

- The frustum test is a **front half-plane**, not the engine's 70° cone: it
  over-estimates visible water tiles on purpose, so the water figure is an
  upper bound, not a best case.
- One core only: `window_cpucores()` is stubbed to 1, so this does **not**
  measure the background reflection worker competing with the render thread in
  the real client. The water scenarios do drive `water_reflection_pass()` and
  wait for the first complete sweep, so they measure the steady-state path.
- The greedy row is not comparable to the others: `chunk_generate_greedy()`
  emits Z faces only (no top surfaces), so it is an incomplete mesh.
- `map_sun_shadow()` in `stubs/bench_stubs.c` is a copy of `map.c`'s DDA. It
  exists because `map.c` drags in the whole map lifecycle. If that raycast
  changes, update the copy.
- The synthetic map is generated, not loaded from a `.vxl`: heights and colour
  bands are deliberately simple so runs are comparable between commits.

## Adding a scenario

Add a function to `bench.c`, build the map state it needs with `build_map()`,
time with `bench_now()`, and print one line per scenario so the output stays
diff-able. Keep every scenario deterministic — no random seeds, no wall-clock
dependence.
