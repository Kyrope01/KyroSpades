"""Regression checks for multiplayer simulation while a menu covers the world.

These are source-level integration checks; an in-game multiplayer playtest is
still needed to verify motion and input behavior end to end.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
main = (root / 'src/main.c').read_text()
controller = (root / 'src/cameracontroller.c').read_text()
hud = (root / 'src/hud.c').read_text()
network = (root / 'src/network.c').read_text()

# Escape opens a HUD that doesn't itself request world simulation.
assert 'show_exit ^= 1;' in hud and 'hud_change(&hud_settings);' in hud
settings_hud = hud.split('struct hud hud_settings = {', 1)[1].split('};', 1)[0]
assert 'hud_settings_ui_images,\n        0,' in settings_hud

loop = main.split('while(!window_closed()) {', 1)[1]
menu_clear = loop.index('if(network_connected && !hud_active->render_world)')
simulation = loop.index('if(hud_active->render_world || network_connected)')
fixed_tick = loop.index('player_update(PHYSICS_STEP_TIME, 1);')
assert menu_clear < simulation < fixed_tick
assert 'input.keys.packed &= 0b00100000' in loop[menu_clear:simulation]
assert 'input.buttons.packed = 0' in loop[menu_clear:simulation]
assert 'button_map[0] = button_map[1] = button_map[2] = 0' in loop[menu_clear:simulation]
assert 'if(!demo_is_frozen())' in loop[simulation:fixed_tick]
assert 'if(hud_active->render_world || camera_mode == CAMERAMODE_FPS)' in loop

fps = controller.split('void cameracontroller_fps(float dt) {', 1)[1].split('void cameracontroller_fps_render()', 1)[0]
assert 'if(chat_input_mode == CHAT_NO_INPUT && hud_active->render_world)' in fps
assert 'if(chat_input_mode != CHAT_NO_INPUT || !hud_active->render_world)' in fps
assert 'input.buttons.packed = 0' in fps
assert 'players[local_player_id].pos.x' in network.split('int network_update()', 1)[1]
print('Menu simulation, input suppression and demo-freeze integration checks passed')
