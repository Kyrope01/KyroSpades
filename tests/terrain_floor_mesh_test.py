"""Source regression for bottom-layer terrain disappearing with enhanced water.

Run: python3 tests/terrain_floor_mesh_test.py
The water render pass may return without drawing while its asynchronous snapshot
is unavailable. Every terrain generator must still mesh solid y=0 blocks.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
chunk = (root / "src/chunk.c").read_text()
water = (root / "src/water.c").read_text()

# This guard is the missing-water-mesh case: a chunk cannot assume the water
# renderer will cover its floor, even when the enhanced-water option is on.
water_render = water.split("void water_render(void) {", 1)[1].split("static ", 1)[0]
assert "!wr.render_cache" in water_render
assert "!wr.render_state" in water_render

for name, end in (
    ("greedy", "static void chunk_add_ao_face"),
    ("naive", "static void emit_textured_face"),
    ("textured", "void chunk_update_all"),
):
    start = f"void chunk_generate_{name}("
    body = chunk.split(start, 1)[1].split(end, 1)[0]
    # All modes must generate the floor from map solidity/color, regardless
    # of which camera height was visible on the background worker thread.
    assert "water_shader_active()" not in body, name
    assert "separate_water_surface" not in body, name
    assert "WATER_LEVEL" not in body, name
    assert "*max_height" in body, name
    assert "tesselator_add" in body or "emit_textured_face" in body, name

print("All terrain meshing modes keep the solid floor independent of water state")
