"""Exercise the real first-person visibility decision independently of GL."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
player = (root / 'src/player.c').read_text()
start = player.index('int local_fpv = render_fpv')
end = player.index('// Render body parts', start)
code = r'''
#include <assert.h>
enum { CAMERAMODE_FPS, CAMERAMODE_SPECTATOR, TOOL_GUN, TOOL_SPADE };
static int local_player_id, camera_mode;
static float camera_ads_progress, camera_ads_scope_progress;
static struct { int iron_sight, hide_weapon_model_during_ads, player_arms; } settings;
static int visible(int id, int render_fpv, int held_item) {
    struct { int held_item; } player = {held_item}, *p = &player;
''' + player[start:end] + r'''
    return (render_arms ? 2 : 0) | (!hide_ads_viewmodel ? 1 : 0);
}
int main(void) {
    settings.player_arms = 1;
    camera_ads_progress = camera_ads_scope_progress = 1;
    assert(visible(0, 1, TOOL_GUN) == 3); /* Zoom only: both visible. */
    settings.hide_weapon_model_during_ads = 1;
    assert(visible(0, 1, TOOL_GUN) == 0);
    assert(visible(0, 1, TOOL_SPADE) == 3);
    assert(visible(1, 0, TOOL_GUN) == 3);
    camera_mode = CAMERAMODE_SPECTATOR;
    assert(visible(1, 1, TOOL_GUN) == 3);
    camera_mode = CAMERAMODE_FPS;
    camera_ads_progress = 0;
    assert(visible(0, 1, TOOL_GUN) == 3);
    settings.hide_weapon_model_during_ads = 0;
    settings.iron_sight = 1;
    assert(visible(0, 1, TOOL_GUN) == 0); /* Scope texture replaces the model. */
    camera_ads_scope_progress = 0;
    settings.player_arms = 0;
    assert(visible(0, 1, TOOL_GUN) == 1);
}
'''
with TemporaryDirectory() as tmp:
    source = Path(tmp) / 'viewmodel.c'
    exe = Path(tmp) / 'viewmodel'
    source.write_text(code)
    run(['cc', '-std=c99', str(source), '-o', str(exe)], check=True)
    run([str(exe)], check=True)
print('ADS viewmodel visibility checks passed')
