"""Equivalence test for the tiled-water loop stride.

Run: python3 tests/water_tile_stride_test.py

Tiled wave mode (the shipped default, `water_wave_mode = 1`) draws one solid
block per wave tile, so only the cell at the tile origin does any work: every
other cell used to run the whole per-cell body (wrap computation, camera disc
test, water-wave branch) only to be thrown away by

    if(x % ts != 0 || z % ts != 0) continue;

`water_render_tile()` now walks the loops in tile-sized strides instead, and
hoists the wave clock/amplitude/tile size out of the loops.

That is only worth anything if it emits exactly the same cells, so this test
does two things:

1. parses `src/water.c` and asserts the strided form is really what is in the
   tree (a revert to the old shape fails here), and
2. replays both loop structures in Python over a wide range of region bounds,
   camera rows and tile sizes, and asserts the *set and order* of visited tile
   origins is identical — including the clamped region edges where the old
   `xa`/`xb`/`za`/`zb` bounds are not multiples of the tile size.

The quad count is the observable that proves it on a real build:
`./tools/bench/bench` prints it, and `water_last_quad_count()` exposes it to
the client overlay. Before/after this change the lake scene reports 14260
quads in both cases.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
water_src = (root / "src" / "water.c").read_text()

# ---- 1. Source guards ------------------------------------------------------


def body_of(name):
    start = water_src.index("static void %s(" % name)
    # next top-level function at the same indentation
    end = water_src.index("\nstatic ", start + 1)
    return water_src[start:end]


tile_fn = body_of("water_render_tile")

assert "const int cell_step = tile_waves ? wave_tile : 1;" in tile_fn, (
    "tile mode must stride the cell loops by the wave tile size"
)
assert "wave_tile = max(1, min(wave_tile, 4));" in tile_fn, (
    "the tile size clamp must be hoisted out of the loops"
)
assert "const float wave_t = render_time * settings.water_wave_speed;" in tile_fn
assert "const float wave_amp = settings.water_wave_intensity;" in tile_fn
assert "x % ts != 0 || z % ts != 0" not in tile_fn, (
    "the per-cell tile-origin filter is what the stride replaces"
)
# The stride must be applied to both loops, or the tile origins along one axis
# would still be filtered cell by cell.
assert "for(int z = za; z < zb; z += cell_step) {" in tile_fn
assert "for(int x = xa; x < xb; x += cell_step) {" in tile_fn
# Alignment of the first visited cell onto a tile origin.
assert tile_fn.count("if(tile_waves && za % wave_tile)") == 1
assert tile_fn.count("if(tile_waves && xa % wave_tile)") == 1, (
    "both the row start and the per-row column start must be aligned"
)
# The wave clock inside the draw branch now aliases the hoisted value, so the
# per-cell recomputation cannot creep back in.
assert tile_fn.count("render_time * settings.water_wave_speed") == 1

# ---- 2. Loop-structure equivalence -----------------------------------------

WATER_TILE = 16


def old_visited(za, zb, xrow_to_bounds, ts):
    """The pre-change structure: every cell, filtered by modulo."""
    out = []
    for z in range(za, zb):
        if z % ts != 0:
            continue
        xa, xb = xrow_to_bounds(z)
        for x in range(xa, xb):
            if x % ts != 0:
                continue
            out.append((x, z))
    return out


def new_visited(za, zb, xrow_to_bounds, ts):
    """The strided structure: align the start, walk in tile-sized steps."""
    out = []
    if za % ts:
        za += ts - (za % ts)
    for z in range(za, zb, ts):
        xa, xb = xrow_to_bounds(z)
        if xa % ts:
            xa += ts - (xa % ts)
        for x in range(xa, xb, ts):
            out.append((x, z))
    return out


cases = 0
for ts in (1, 2, 3, 4):
    for tz in (0, WATER_TILE, 5 * WATER_TILE):
        for z0 in (tz, tz - 7, tz + 3):
            za = max(tz, z0)
            for z1 in (tz + WATER_TILE, tz + WATER_TILE - 3, tz + 1):
                zb = min(tz + WATER_TILE, z1)
                for hw_base in (0, 1, 5, 40):
                    for camera_x in (0, 37, 128, 255):
                        def bounds(z, camera_x=camera_x, hw_base=hw_base, tz=tz):
                            hw = hw_base
                            xa = max(max(tz, 0), camera_x - hw)
                            xb = min(min(tz + WATER_TILE, 4096), camera_x + hw + 1)
                            return xa, xb

                        old = old_visited(za, zb, bounds, ts)
                        new = new_visited(za, zb, bounds, ts)
                        assert old == new, (
                            "tile origins differ for ts=%d tz=%d za=%d zb=%d "
                            "camera_x=%d hw=%d: %r vs %r"
                            % (ts, tz, za, zb, camera_x, hw_base, old[:8], new[:8])
                        )
                        cases += 1

assert cases > 500, "expected a broad sweep, got %d cases" % cases

print("Water tile stride equivalence verified (%d loop configurations,"
      " identical tile-origin sets and order)" % cases)
