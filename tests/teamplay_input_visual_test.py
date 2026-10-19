"""Focused regression checks for G menu ownership, fixed ping fill, and no compass.

Run: python3 tests/teamplay_input_visual_test.py
The C fragments are extracted from the production functions; no GL/SDL headers needed.
"""
import pathlib
import re
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[1]
config = (root / "src/config.c").read_text()
hud = (root / "src/hud.c").read_text()
window = (root / "src/window.c").read_text()
draw = (root / "src/teamplay_draw.c").read_text()

# Both backends must map G to the menu, H to the former G action. An existing
# config can override defaults, so also exercise the actual post-parse fixup.
for backend in ("GLFW", "SDL"):
    prefix = "GLFW_KEY_" if backend == "GLFW" else "SDLK_"
    assert re.search(rf'WINDOW_KEY_PIE_MENU, {prefix}G\b' if backend == 'GLFW' else rf'WINDOW_KEY_PIE_MENU, {prefix}g\b', config)
    assert re.search(rf'WINDOW_KEY_LASTTOOL, {prefix}H\b' if backend == 'GLFW' else rf'WINDOW_KEY_LASTTOOL, {prefix}h\b', config)
assert 'case 72:  return SDLK_h;' in config  # legacy GLFW H must migrate on SDL
assert re.search(r'^pie_menu\s*= 71$', (root / 'resources/config.ini').read_text(), re.M)
assert re.search(r'^last_tool\s*= 72$', (root / 'resources/config.ini').read_text(), re.M)
migration = config.split('        struct config_key_pair* pie_key = config_key(WINDOW_KEY_PIE_MENU);', 1)[1]
migration = '        struct config_key_pair* pie_key = config_key(WINDOW_KEY_PIE_MENU);' + migration.split('        if(!list_created(&config_settings))', 1)[0]
harness = r'''
#include <assert.h>
struct config_key_pair { int def; };
#define WINDOW_KEY_PIE_MENU 1
#define WINDOW_KEY_LASTTOOL 2
#define GLFW_KEY_G 71
#define GLFW_KEY_H 72
#define SDLK_g 103
#define SDLK_h 104
static struct config_key_pair menu, last;
static struct config_key_pair* config_key(int k) { return k == 1 ? &menu : &last; }
static void migrate(void) {
''' + migration + r'''
}
#ifdef USE_SDL
#define G SDLK_g
#define H SDLK_h
#else
#define G GLFW_KEY_G
#define H GLFW_KEY_H
#endif
int main(void) {
    menu.def = H; last.def = G; migrate(); assert(menu.def == G && last.def == H); // legacy pair
    menu.def = G; last.def = G; migrate(); assert(menu.def == G && last.def == H); // no saved menu key
    menu.def = G; last.def = H; migrate(); assert(menu.def == G && last.def == H); // new config
    menu.def = 55; last.def = 56; migrate(); assert(menu.def == 55 && last.def == 56); // custom keys
    menu.def = 55; last.def = 55; migrate(); assert(menu.def == 55 && last.def == 55); // other collision
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    c.write_text(harness)
    for backend in ('USE_SDL', 'USE_GLFW'):
        binary = pathlib.Path(tmp) / backend
        subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-D' + backend,
                        str(c), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)

# Desktop cursor motion must be consumed *before* camera look, while SDL touch
# motion has its own direct look path and needs the same menu ownership.
mouse_fn = hud.split('static void hud_ingame_mouselocation(', 1)[1].split('static void ', 1)[0]
assert 'if(pie_menu_opened()) { pie_menu_move(dx, dy); return; }' in mouse_fn
assert mouse_fn.index('pie_menu_move(dx, dy); return;') < mouse_fn.index('camera_look_delta(')
assert window.index('if(pie_menu_opened()) {\n\t\t\t\t\t\tpie_menu_move(fdx, fdy);') < window.index('camera_look_delta(fdx * sens * s, fdy * sens * s);')
assert 'if(key == WINDOW_KEY_PIE_MENU)' in hud
assert 'if(action == WINDOW_RELEASE) pie_key_down = 0' in hud
assert 'if(pie_menu_opened()) pie_menu_cycle(1)' in hud
assert 'pie_menu_has_selection()) pie_menu_close(true)' in hud
assert 'if(idx == 1) pie_menu_close(false)' in hud
assert 'pie_menu_close(true)' not in hud[hud.index('if(key == WINDOW_KEY_PIE_MENU)'):hud.index('if(pie_menu_opened() && action == WINDOW_PRESS && key == WINDOW_KEY_ESCAPE)')]  # G release never commits
assert 'config_key(WINDOW_KEY_LASTTOOL)->def == config_key(WINDOW_KEY_PIE_MENU)->def' in hud

assert 'teamplay_draw_compass' not in draw + hud + (root / 'src/teamplay_draw.h').read_text()
assert 'teamplay_compass' not in config + (root / 'src/config.h').read_text() + (root / 'src/main.c').read_text()

# Record the real diamond renderer's vertices/colors at two ages. The three
# filled layers must be identical; the fourth (border) changes alpha only.
diamond = draw[draw.index('static void diamond('):draw.index('/* Use the sender')]
visual = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#define TP_PI 3.14159265358979323846f
#define GL_BLEND 1
#define GL_TRIANGLES 2
static int calls;
static float colors[4][4], verts[4][72];
static int count[4];
static float rgba[4];
static void glEnable(int v) { assert(v == GL_BLEND); }
static void glColor4f(float r, float g, float b, float a) {
    rgba[0]=r;rgba[1]=g;rgba[2]=b;rgba[3]=a;
}
static void glx_draw_vertices_3d(const float* v, int n, int mode) {
    assert(mode == GL_TRIANGLES && calls < 4);
    memcpy(colors[calls], rgba, sizeof rgba);
    memcpy(verts[calls], v, (size_t)n * 3 * sizeof(float));
    count[calls++] = n;
}
''' + diamond + r'''
int main(void) {
    float first_colors[4][4], first_verts[4][72];
    diamond(100,200,10,255,64,0,.9f,0.f);
    assert(calls == 4 && count[0] == 6 && count[1] == 6 && count[2] == 6 && count[3] == 24);
    memcpy(first_colors, colors, sizeof colors);
    memcpy(first_verts, verts, sizeof verts);
    calls = 0;
    diamond(100,200,10,255,64,0,.9f,.4f);
    assert(calls == 4);
    assert(memcmp(first_verts, verts, sizeof verts) == 0);
    assert(memcmp(first_colors, colors, 3 * sizeof colors[0]) == 0);
    assert(colors[3][3] != first_colors[3][3]);
    assert(colors[3][0] == first_colors[3][0]);
}
'''
with tempfile.TemporaryDirectory() as tmp:
    c = pathlib.Path(tmp) / 'test.c'
    c.write_text(visual)
    binary = pathlib.Path(tmp) / 'test'
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print('Teamplay input/visual regressions passed')
