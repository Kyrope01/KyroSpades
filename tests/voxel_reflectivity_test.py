"""Regression checks for the independently configurable terrain sheen."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
lighting = (root / 'src/lighting.c').read_text()
config = (root / 'src/config.c').read_text()
main = (root / 'src/main.c').read_text()
hud = (root / 'src/hud.c').read_text()
chunk = (root / 'src/chunk.c').read_text()
texture = (root / 'src/texture.c').read_text()

# The retired terrain sheen and pre-existing terrain specular must both stay off.
assert 'u_VoxelReflectivity' not in lighting
assert 'reflected_sky' not in lighting
assert 'float specular =' not in lighting
assert 'settings.voxel_reflectivity' not in lighting + config + main + hud
assert '"voxel_reflectivity"' in config  # remove the obsolete key on save
assert 'voxel_reflectivity' not in (root / 'resources/config.ini').read_text()
assert 'if(!world_program || !lighting_world_program_required())' in lighting
assert 'VERTEX_INT, lighting_world_supported(), settings.textured_blocks' in chunk
assert 'if(lighting_world_supported())\n                texture_blocks_prepare_normal_map();' in texture
print('Retired voxel reflectivity checks passed')

# Exercise the real tesselator upload entry point, not just source strings:
# a previously created chunk can gain or lose its normal buffer after remesh.
import subprocess
import tempfile
with tempfile.TemporaryDirectory() as directory:
    path = Path(directory)
    harness = path / 'mesh.c'
    harness.write_text(r'''
#include <assert.h>
#include "common.h"
#include "glx.h"
#include "tesselator.h"
static int calls;
void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type,
                            void* color, void* vertex, void* normal, void* texcoord) {
    (void)size; (void)type; (void)color; (void)vertex; (void)texcoord;
    assert(x->has_normal == (normal != NULL)); ++calls;
}
int main(void) {
    struct tesselator t = {0}; struct glx_displaylist list = {0}; int8_t normals[12] = {0};
    t.vertex_type = VERTEX_INT; t.has_normal = 1; t.normals = normals;
    tesselator_glx(&t, &list); assert(list.has_normal);
    t.has_normal = 0; t.normals = NULL;
    tesselator_glx(&t, &list); assert(!list.has_normal && calls == 2);
    return 0;
}
''')
    exe = path / 'mesh'
    subprocess.run(['cc', '-std=gnu99', '-ffunction-sections', '-fdata-sections',
                    '-I', str(root / 'src'), '-I', str(root / 'deps'),
                    '-I', str(root / 'deps/libvxl'), '-I', str(root / 'tools/bench/stubs'),
                    str(root / 'src/tesselator.c'), str(harness), '-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Terrain normal layout changes verified')
