"""Renderer hygiene checks: dead pipelines, latched failures and default drift.

These are the classes of mistake that survive a review because nothing crashes:
a feature is removed but its texture/fetch stays in the pass, a failure is
remembered as success, or the sample config quietly disagrees with the defaults.
Every assertion here is a *behaviour* claim about the frame, not a snapshot of a
line, so refactors that keep the behaviour keep the test green.
"""
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lighting = (root / 'src/lighting.c').read_text()
texture = (root / 'src/texture.c').read_text()
texture_h = (root / 'src/texture.h').read_text()
sky = (root / 'src/sky.c').read_text()
main = (root / 'src/main.c').read_text()
config_h = (root / 'src/config.h').read_text()
render_doc = (root / 'docs/rendering-architecture.md').read_text()
readme = (root / 'README.md').read_text()

# --- the derived material map is gone, the normal map is not -----------------
# Only the emission channel was still read, and it was generated as 0, so the
# whole map was a per-fragment texture fetch that always multiplied by zero.
for source, name in ((lighting, 'lighting.c'), (texture, 'texture.c'), (texture_h, 'texture.h')):
    assert 'texture_blocks_material' not in source, name
    assert 'u_MaterialMap' not in source, name
    assert 'u_MaterialMapsEnabled' not in source, name
assert 'material.b' not in lighting
assert '/* emission */' not in texture
assert 'materials[' not in texture

# The normal map must survive the cleanup, still bound on its own unit.
assert 'u_NormalMapEnabled' in lighting
assert lighting.count('uniform sampler2D u_NormalMap;') == 2  # GLSL 120 and 330
assert lighting.count('uniform float u_NormalMapEnabled;') == 2
assert 'glUniform1i(uniform_normal_map, 1)' in lighting
assert 'texture_create_buffer(&texture_blocks_normal' in texture
assert 'texture_blocks_prepare_normal_map' in texture_h and 'texture_blocks_release_normal_map' in texture_h

# The terrain pass must no longer disturb texture unit 2 (or leak unit 1).
assert 'GL_TEXTURE2' not in lighting
assert 'previous_texture2' not in lighting
assert 'glBindTexture(GL_TEXTURE_2D, texture_blocks_normal.texture_id)' in lighting
assert 'glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture1)' in lighting
assert 'glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture1)' in lighting

# u_AmbientLight belongs to glx.c's default shader (used by the entity, model and
# water paths), which follows the map's day/night level. The terrain shaders must
# keep shading ambient themselves instead of declaring it, and the setter must
# stay guarded so the world program's -1 lookup is a no-op.
glx = (root / 'src/glx.c').read_text()
assert 'uniform vec3 u_AmbientLight;' in glx
assert 'uniform vec3 u_AmbientLight;' not in lighting
ambient = lighting[lighting.index('"u_AmbientLight"'):]
assert ambient.startswith('"u_AmbientLight");\n        if(location >= 0)'), ambient[:80]
assert 'uniform vec3 u_AmbientLight' not in lighting

# --- a failed sky compile must not latch ------------------------------------
assert 'sky_attempted' not in sky
assert 'SKY_COMPILE_ATTEMPTS' in sky
assert '++sky_compile_attempts' in sky
# Capability checks come first, the compile is attempted only while unbuilt and
# only while attempts remain, and the failure path returns without latching.
guard = sky.index('sky_compile_attempts < SKY_COMPILE_ATTEMPTS')
compile_at = sky.index('glx_shader(sky_vertex_shader, sky_fragment_shader)')
assert guard < compile_at
assert sky.index('if(!glx_version)') < compile_at
assert sky.index('if(gles_version < 2)') < compile_at
assert 'if(!sky_program)\n                return false;' in sky

# --- the sky restores every state it changes -------------------------------
assert 'glx_use_program((GLuint)previous_program)' in sky
assert 'glDepthMask(depth_mask)' in sky
assert 'glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask)' in sky
for field, cap in (('depth', 'GL_DEPTH_TEST'), ('blend', 'GL_BLEND'), ('cull', 'GL_CULL_FACE')):
    assert f'glIsEnabled({cap})' in sky, cap
    assert f'if({field}_enabled) glEnable({cap})' in sky, cap
    assert f'else glDisable({cap})' in sky, cap
# It changes no depth function, colour mask or texture unit, so it must not
# start pretending to restore them either.
assert 'glDepthFunc' not in sky and 'glColorMask' not in sky and 'glActiveTexture' not in sky

# --- settings that could never change are gone ------------------------------
for dead in ('opengl14', 'color_correction', 'shadow_entities'):
    assert f'int {dead};' not in config_h, dead
    assert f'settings.{dead}' not in main, dead
    assert f'settings.{dead}' not in sky, dead
# The projection setup that used to be gated on it still runs every frame.
assert 'matrix_perspective(matrix_projection, camera_fov_scaled(dt_float)' in main
assert main.index('camera_ads_update(dt_float);') < main.index('matrix_perspective(matrix_projection')

# --- the sample config must agree with the defaults it documents ------------
# It is a reference file (the client reads config.ini from the user directory),
# so a stale value there teaches the wrong default.
body = main[main.index('int main(int argc'):]
client = (root / 'resources/config.ini').read_text()
client = client[client.index('[client]'):client.index('[controls]')]
ini = {}
for line in client.splitlines()[1:]:
    if '=' in line:
        key, value = line.split('=', 1)
        ini[key.strip()] = value.strip()

for key in ('filmic_tonemapping', 'chromatic_aberration', 'healthbar', 'camera_fov'):
    match = re.search(rf'settings\.{re.escape(key)}\s*=\s*([^;]+);', body)
    assert match, key
    default = match.group(1).strip().rstrip('Ff')
    if not re.fullmatch(r'-?[0-9.]+', default):
        define = re.search(rf'#define\s+{re.escape(default)}\s+([0-9.]+)', 
                           (root / 'src/camera.h').read_text())
        assert define, f'{key}: cannot resolve {default}'
        default = define.group(1)
    assert key in ini, f'{key} missing from the sample config'
    assert abs(float(default) - float(ini[key])) < 1e-4, (
        f'{key}: sample config says {ini[key]}, main() default is {default}')

# --- the post-process passes still put GL state back -----------------------
# The old tests asserted this against the bloom chain; bloom is gone, the
# invariant is not. Anything that configures a pass must restore the caller's
# framebuffer, and the MSAA resolve must leave a matched binding behind.
post = (root / 'src/postprocess.c').read_text()
assert 'glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo)' in post
assert 'glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_rb)' in post
assert post.count('glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo)') == 2  # success + failure
assert post.count('glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previous_rb)') == 2
resolve = post[post.index('void postprocess_msaa_resolve'):]
assert 'glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_msaa.fbo)' in resolve
assert 'glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)resolve_fbo)' in resolve
assert resolve.index('glBlitFramebuffer') < resolve.index('glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)resolve_fbo)')

# --- the new pass is documented where the next author will look -------------
assert '### Sky' in render_doc
assert 'sky_render()' in render_doc
assert 'drawScene()' in render_doc
assert render_doc.index('sky_render()') < render_doc.index('drawScene()')  # order, in the frame listing
assert 'Atmospheric Sky' in readme
assert 'sky_clouds' in readme or 'clouds' in readme.lower()

print('Renderer hygiene checks passed')
