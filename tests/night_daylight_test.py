"""Night fog/daylight regression checks. Run: python3 tests/night_daylight_test.py.

The C harness checks the real sky-level conversion; source checks cover the
renderer wiring without requiring a GPU or network server.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory() as tmp:
    harness = Path(tmp) / "night.c"
    harness.write_text(r'''
#include <assert.h>
#include "lighting.h"
int main(void) {
    assert(lighting_sky_level(0.5f, 0.9098f, 1.0f) == 255); /* default day */
    assert(lighting_sky_level(0.0f, 0.0f, 0.0f) == 20);  /* night floor */
    assert(lighting_sky_level(0.1f, 0.0f, 0.0f) == 26);  /* saturated dusk */
    assert(lighting_sky_level(2.0f, 0.0f, 0.0f) == 255);
    assert(lighting_sky_level(-1.0f, 0.0f, 0.0f) == 20);
    return 0;
}
''')
    executable = Path(tmp) / "night"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-I", str(root / "src"), str(harness), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

lighting = (root / "src/lighting.c").read_text()
main = (root / "src/main.c").read_text()
map_source = (root / "src/map.c").read_text()
chunk = (root / "src/chunk.c").read_text()
assert 'lighting_update_sky_color(fog_color[0], fog_color[1], fog_color[2])' in main
assert '__atomic_store_n(&sky_level, level, __ATOMIC_RELAXED)' in lighting
assert '__atomic_load_n(&sky_level, __ATOMIC_RELAXED)' in lighting
assert 'settings.sunlight_intensity * 0.01F * ((float)level / 255.0F)' in lighting
assert 'chunk_rebuild_all();' in lighting
assert 'float baked_sunlight = lighting_world_supported() ? 1.0F : lighting_sunlight_scale();' in chunk
assert 'int r = blue(col) * baked_sunlight;' in chunk
assert 'lighting_sunlight_scale()' not in map_source[map_source.index('void fog_color_render('):map_source.index('struct damaged_voxel')]
# Independent flash/point contributions must be added, not scaled by daylight.
assert 'added += color_intensity.rgb * color_intensity.a * attenuation * facing' in lighting
assert 'added += u_SpotColorCone[3].rgb * ci.a' in lighting
print('Night sky conversion and renderer wiring checks passed')
