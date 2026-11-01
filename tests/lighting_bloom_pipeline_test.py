"""Ensure bloom is removed without removing HDR, flare or dynamic lighting."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.c').read_text()
config = (root / 'src/config.c').read_text()
for file in ('main.c', 'postprocess.c', 'postprocess.h', 'config.h'):
    assert 'bloom' not in (root / 'src' / file).read_text().lower()
for key in ('bloom', 'bloom_strength', 'bloom_threshold'):
    assert f'!strcmp(e->name, "{key}")' in config
    assert key not in (root / 'resources/config.ini').read_text()
assert 'scene_internal_format = GL_RGBA16F;' in main
assert 'postprocess_hdr_supported()' in main
assert 'if(postproc.flare_applied) scene_texture = postproc.flare_tex;' in main
assert 'glowing_blocks_prepare_frame(camera_x, camera_y, camera_z)' in main
assert 'lighting_submit_point(selected[i].position[0]' in (root / 'src/glowing_blocks.c').read_text()
print('Bloom removed; HDR, flare and lighting retained')
