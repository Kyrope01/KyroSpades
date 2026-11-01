"""Retired height fog must not affect normal distance fog."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
assert not (root / 'src/height_fog.h').exists()
for file in ('lighting.c', 'glx.c', 'water.c', 'main.c', 'config.h', 'hud.c'):
    s = (root / 'src' / file).read_text()
    assert 'height_fog' not in s and 'u_HeightFog' not in s
config = (root / 'src/config.c').read_text()
for key in ('height_fog_density', 'height_fog_top'):
    assert f'!strcmp(e->name, "{key}")' in config
    assert key not in (root / 'resources/config.ini').read_text()
assert 'glx_enable_sphericalfog()' in (root / 'src/glx.c').read_text()
assert 'u_FogDistance' in (root / 'src/lighting.c').read_text()
print('Height fog removed; normal fog remains')
