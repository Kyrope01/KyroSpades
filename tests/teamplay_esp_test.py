"""Compile the actual Teamplay reveal selection (without GL/ENet).

Run: python3 tests/teamplay_esp_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
player = (root / 'src/player.c').read_text()
start = player.index('\t/* Teamplay uses exactly the spectator ESP box geometry')
end = player.index('\n\tplayer_render_corpses();', start)
selection = player[start:end]
obscured = player[player.index('static int player_is_obscured(struct Player* p) {'):
                  player.index('// Check if a player is within frustum',
                               player.index('static int player_is_obscured(struct Player* p) {'))]
config = (root / 'src/config.c').read_text()
hud = (root / 'src/hud.c').read_text()
assert 'WINDOW_KEY_TEAM_OVERLAY, SDLK_j' in config
assert 'WINDOW_KEY_TEAM_OVERLAY, GLFW_KEY_J' in config
assert 'case 74:  return SDLK_j;' in config
assert 'team_overlay                  = 74' in (root / 'resources/config.ini').read_text()
assert 'window_key_down(WINDOW_KEY_TEAM_OVERLAY)' in hud
assert 'teamplay_overlay_hold(false);' in hud
assert 'teamplay_apply_pending();' in (root / 'src/main.c').read_text()
assert 'teamplay_player_spawned(p->player_id)' in (root / 'src/network.c').read_text()
assert 'teamplay_draw_compass' not in (root / 'src/teamplay_draw.c').read_text()

stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define PLAYERS_MAX 256
#define TEAM_SPECTATOR 255
#define CAMERAMODE_FPS 1
#define TEAMPLAY_WORLD 1
struct Player { int connected,alive,team; } players[PLAYERS_MAX];
typedef struct { bool active; unsigned char surfaces,red,green,blue; } TeamplayMark;
TeamplayMark teamplay_marks[PLAYERS_MAX];
static int local_player_id=1, camera_mode=CAMERAMODE_FPS;
static int network_connected=1,network_logged_in=1,network_map_transfer;
static float alpha;
static int draws, last_id;
static unsigned char last_red, last_green, last_blue;
static int demo_is_playing(void) {return 0;}
static float teamplay_overlay_opacity(void) {return alpha;}
static int player_in_view(struct Player* p) {(void)p;return 1;}
static int player_is_obscured(struct Player* p) {(void)p;return 1;}
static void player_esp_team_color(int team,unsigned char* r,unsigned char* g,unsigned char* b) {
    assert(team==0); *r=11;*g=22;*b=33;
}
static void player_draw_esp_box(struct Player* p,unsigned char r,unsigned char g,unsigned char b) {
    draws++;last_id=(int)(p-players);last_red=r;last_green=g;last_blue=b;
}
static void select_esp(void) {
''' + selection + r'''
}
int main(void) {
    players[1]=(struct Player){1,1,0};
    players[2]=(struct Player){1,1,0};
    players[3]=(struct Player){1,1,1};
    players[4]=(struct Player){1,1,255};
    select_esp(); assert(draws==0); /* no permission, no unmarked enemies */
    alpha=1;select_esp();assert(draws==1 && last_id==2 && last_red==11);
    teamplay_marks[3]=(TeamplayMark){.active=1,.surfaces=TEAMPLAY_WORLD,.red=201,.green=43,.blue=87};
    alpha=0;draws=0;select_esp();
    assert(draws==1 && last_id==3 && last_red==201 && last_green==43 && last_blue==87);
    teamplay_marks[3].surfaces=2;draws=0;select_esp();assert(draws==0);
    teamplay_marks[1]=(TeamplayMark){.active=1,.surfaces=TEAMPLAY_WORLD};
    draws=0;select_esp();assert(draws==0); /* never outline local camera */
    network_map_transfer=1;draws=0;alpha=1;select_esp();assert(draws==0);
    puts("Teamplay reveal gates/colours passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'esp.c'
    exe = Path(d) / 'esp'
    c.write_text(stub)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)

# The obscuration ray originates inside the local first-person hitbox. Use
# the real obscuration function to verify that the local hit cannot hide a
# wall from the ESP decision (and a visible target remains unoutlined).
ray_stub = r'''
#include <assert.h>
#include <stdio.h>
#define CAMERA_HITTYPE_BLOCK 1
#define CAMERA_HITTYPE_PLAYER 2
struct Player {struct {struct {float x,y,z;} eye;} physics;};
struct Camera_HitType {int type;};
static struct Player players[3];
static int local_player_id=1, wall_present=1;
static float camera_x=0, camera_y=1, camera_z=0;
static float player_height(struct Player* p) {(void)p;return 1.5f;}
static void camera_hit_mask(struct Camera_HitType* hit,int exclude,float x,float y,float z,
                            float dx,float dy,float dz,float range) {
    (void)x;(void)y;(void)z;(void)dx;(void)dy;(void)dz;(void)range;
    hit->type = exclude==local_player_id && wall_present
              ? CAMERA_HITTYPE_BLOCK : CAMERA_HITTYPE_PLAYER;
}
''' + obscured + r'''
int main(void) {
    players[2].physics.eye.z=10;
    assert(player_is_obscured(&players[2]));
    wall_present=0;
    assert(!player_is_obscured(&players[2]));
    puts("Teamplay self-hit occlusion regression passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'occlusion.c'
    exe = Path(d) / 'occlusion'
    c.write_text(ray_stub)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
