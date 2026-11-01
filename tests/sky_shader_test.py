"""Sky settings and renderer integration regression checks (no GPU required)."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
sky = (root / 'src/sky.c').read_text()
main = (root / 'src/main.c').read_text()
config = (root / 'src/config.c').read_text()
ini = (root / 'resources/config.ini').read_text()
assert 'sky.c' in (root / 'src/CMakeLists.txt').read_text()
assert 'glmc_mat4_inv(matrix_projection, inverse_projection)' in sky
assert 'glmc_mat4_inv(matrix_view, inverse_view)' in sky
assert 'u_InverseView * vec4(eye.xyz, 0.0)' in sky  # no camera translation
assert 'fog_color_render(fog)' in sky
assert 'lighting_sunlight_scale()' in sky and 'sun_dir[0]' in sky
assert 'smoothstep(220.0, 320.0, distance)' in sky  # bounded horizon fade
assert 'mod(p, 256.0)' in sky and 'fmod(window_time()' in sky
assert 'if(gles_version < 2)' in sky and 'if(!sky_program)' in sky
assert 'glx_use_program((GLuint)previous_program)' in sky
assert 'glDepthMask(depth_mask)' in sky
for state in ('depth_enabled', 'blend_enabled', 'cull_enabled'):
    assert f'if({state}) glEnable' in sky
assert 'if(!sky_drawn && settings.sky_gradient)' in main  # old gradient fallback
assert main.index('&& sky_render()') < main.index('water_reflection_pass();')
assert 'sky_deinit();' in main
assert 'floor(p / 8.0)' in sky and 'smoothstep(threshold - 0.035' in sky
assert '(96.0 - camera.y) / ray.y' in sky and '(128.0 - camera.y) / ray.y' in sky
assert 'max(ray.y, 0.15)' not in sky
assert '(voxel.xz + 0.5) * 0.20' in sky  # smaller formations
assert 'mix(0.85, 0.48, u_CloudCoverage)' in sky  # more space between clouds
assert 'fmod(window_time() * 0.05, 1280.0)' in sky  # seamless noise wrap
assert 'for(int i = 0; i < 24; ++i)' in sky
assert 'SKY_OUT = vec4(color, 1.0 - cloud_opacity)' in sky
assert main.count('uniform float useCloudMask;') == 2
assert main.count('texture2D(rendered, clamp(sunUV') == 1
assert main.count('texture2D(rendered,clamp(sunUV') == 1
assert 'sky_drawn && settings.sky_clouds ? 1.0F : 0.0F' in main
assert 'step(1.0, sunDepth)' in main and 'step(1.0,sunDepth)' in main
assert 'flashlight_beams_render' not in main
assert not (root / 'src/flashlight_beams.c').exists()
assert 'lighting_beam_snapshot' not in (root / 'src/lighting.c').read_text()
assert 'window_time() * 0.05' in sky  # former 5x setting
assert 'sky_cloud_speed' not in sky
for setting in ('sky_clouds', 'sky_cloud_coverage'):
    assert f'"{setting}", settings.{setting}' in config
    assert f'IMPORT_SETTING(settings.{setting}, {setting},' in config
    assert f'&settings_tmp.{setting}' in config
    assert setting in ini
assert 'settings.sky_clouds = 0;' in main
print('Camera sky shader and settings integration checks passed')
