"""The removed volume pass must not force post-processing or settings."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
assert not (root / 'src/volumetric_fog.c').exists()
assert not (root / 'src/volumetric_fog.h').exists()
assert 'volumetric_fog' not in (root / 'src/main.c').read_text()
assert 'volumetric_fog' not in (root / 'src/config.h').read_text()
assert 'sky_cloud_speed' not in (root / 'src/config.h').read_text()
assert 'volumetric_fog' not in (root / 'resources/config.ini').read_text()
assert 'sky_cloud_speed' not in (root / 'resources/config.ini').read_text()
config = (root / 'src/config.c').read_text()
assert '&settings_tmp.volumetric_fog' not in config
assert '&settings_tmp.sky_cloud_speed' not in config
assert 'strcmp(e->section, "client") == 0' in config
assert '!strcmp(e->name, "volumetric_fog")' in config
assert '!strcmp(e->name, "sky_cloud_speed")' in config
print('Retired volumetric fog and cloud speed settings checks passed')
