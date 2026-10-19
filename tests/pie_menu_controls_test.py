"""Compile the actual HUD input branches with small fakes; no SDL/GL needed.
Run: python3 tests/pie_menu_controls_test.py
"""
from pathlib import Path
import subprocess
import tempfile

hud = (Path(__file__).resolve().parents[1] / 'src/hud.c').read_text()
key = hud[hud.index('        if(key == WINDOW_KEY_PIE_MENU) {', hud.index('static void hud_ingame_keyboard(')):
          hud.index('        if(pie_menu_opened() && action == WINDOW_PRESS && key == WINDOW_KEY_ESCAPE)',
                    hud.index('static void hud_ingame_keyboard('))]
mouse = hud[hud.index('        /* The menu owns attack buttons:'):
            hud.index('        if(chat_input_mode != CHAT_NO_INPUT) {',
                      hud.index('        /* The menu owns attack buttons:'))]
assert 'WINDOW_MOUSE_MMB' not in mouse  # middle click is no longer a menu control
assert 'if(action == WINDOW_RELEASE) pie_key_down = 0' in key
assert 'pie_menu_close(true)' not in key  # releasing G must never select
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define WINDOW_KEY_PIE_MENU 1
#define WINDOW_MOUSE_LMB 1
#define WINDOW_MOUSE_RMB 2
#define WINDOW_PRESS 1
#define WINDOW_RELEASE 0
#define WINDOW_REPEAT 2
#define CHAT_NO_INPUT 0
#define SCREEN_NONE 0
static int opened, page, selected, commits, cancels, opens, cycles;
static int pie_key_down, pie_attack_suppressed[2], button_map[3], local_player_drag_active;
static int show_exit, cp_open, chat_input_mode, screen_current;
static int demo_is_playing(void) {return 0;}
static int pie_menu_opened(void) {return opened;}
static int pie_menu_has_selection(void) {return opened && selected;}
static int pie_menu_open(void) { assert(!opened); opened=1; opens++; return 1; }
static void pie_menu_cycle(int direction) {assert(opened && direction==1); page=(page+1)%3; cycles++;}
static void pie_menu_close(int commit) { assert(opened); opened=0; if(commit) commits++; else cancels++; }
static void on_key(int key,int action) {
''' + key + r'''
}
static void on_mouse(int button,int action) {
''' + mouse + r'''
}
int main(void) {
    button_map[0]=1; local_player_drag_active=1;
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_PRESS);
    assert(opened && opens==1 && button_map[0]==0 && local_player_drag_active==0);
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_PRESS); /* SDL key auto-repeat */
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_REPEAT); /* GLFW key auto-repeat */
    assert(page==0 && cycles==0);
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_RELEASE);
    assert(opened && !commits && !cancels);
    for(int n=0;n<4;n++) {
        on_key(WINDOW_KEY_PIE_MENU,WINDOW_PRESS);
        on_key(WINDOW_KEY_PIE_MENU,WINDOW_RELEASE);
    }
    assert(page==1 && cycles==4 && opened && !commits);
    on_mouse(WINDOW_MOUSE_LMB,WINDOW_RELEASE); /* old attack release consumed */
    assert(opened && !button_map[0]);
    on_mouse(WINDOW_MOUSE_LMB,WINDOW_PRESS); /* center/deadzone: no option */
    assert(opened && !commits && !button_map[0]);
    on_mouse(WINDOW_MOUSE_LMB,WINDOW_RELEASE);
    selected=1;
    on_mouse(WINDOW_MOUSE_LMB,WINDOW_PRESS);
    assert(commits==1 && !opened && !button_map[0]);
    on_mouse(WINDOW_MOUSE_LMB,WINDOW_RELEASE);
    assert(!button_map[0] && !pie_attack_suppressed[0]);
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_PRESS);
    on_key(WINDOW_KEY_PIE_MENU,WINDOW_RELEASE);
    assert(opened && opens==2);
    on_mouse(WINDOW_MOUSE_RMB,WINDOW_PRESS);
    assert(cancels==1 && !opened && !button_map[1]);
    on_mouse(WINDOW_MOUSE_RMB,WINDOW_RELEASE);
    assert(!button_map[1] && !pie_attack_suppressed[1] && commits==1);
    puts("Pie menu key/click control tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp) / 'controls.c'
    exe = Path(tmp) / 'controls'
    file.write_text(stubs)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(file), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
