"""Headless smoke test: run the trainer and decode its arena with the real VXL loader."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
trainer = (root / 'src/aimtrainer.c').read_text().split('static bool active;', 1)[1]
map_source = (root / 'src/map.c').read_text()
layout = map_source[map_source.index('bool map_load_aimtrainer(void) {'):map_source.index('void map_save_file')]

stubs = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aimtrainer.h"
#include "libvxl.h"
#define WATER_LEVEL 1.0F
#define PLAYERS_MAX 32
enum { TEAM_1, TEAM_2, TOOL_GUN, TOOL_SPADE, WEAPON_RIFLE, GAMEMODE_TC, CAMERAMODE_FPS, CAMERAMODE_SPECTATOR };
struct GameState {
    struct Team { char name[11]; unsigned char red, green, blue; } team_1, team_2;
    unsigned char gamemode_type;
} gamestate;
struct Position { float x,y,z; } view_offset;
struct Orientation { float x,y,z; };
struct Player {
    struct Position pos, prev_pos;
    struct Orientation orientation, orientation_smooth;
    struct { struct Position eye, velocity; int airborne; float lastclimb; } physics;
    struct { float feet_started_cycle; bool feet_cylce; } sound;
    int team, held_item, connected, alive, weapon, items_show;
    float item_disabled, item_showup;
    char name[32];
} players[PLAYERS_MAX];
int local_player_id, local_player_health, local_player_blocks, local_player_grenades, local_player_lasttool;
int camera_mode, network_connected, button_map[3], rebuilds;
float camera_x, camera_y, camera_z, camera_rot_x, camera_rot_y;
float camera_crosshair_rot_x, camera_crosshair_rot_y, camera_muzzle_rot_x, camera_muzzle_rot_y;
float fog_color[4] = {.1F,.2F,.3F,1};
float now;
struct { float render_distance; char name[32]; } settings = {128, "Player"};
static void player_reset(struct Player* p) { memset(p, 0, sizeof(*p)); }
static void player_init(void) { memset(players, 0, sizeof(players)); }
static void player_prev_snap(int id) { players[id].prev_pos = players[id].pos; }
static float player_height(struct Player* p) { (void)p; return 1.1F; }
static float game_time(void) { return now; }
static float window_time(void) { return now; }
static void weapon_set(bool restock) { assert(!restock); }
static void chunk_rebuild_all(void) { ++rebuilds; }
static struct libvxl_map map;
static bool map_replace_vxl(const void* data, size_t size) {
    return libvxl_create(&map, 512, 512, 64, data, size);
}
'''

checks = r'''
int main(void) {
    const int c = AIMTRAINER_CENTER, top = 63 - AIMTRAINER_GROUND;
    gamestate.team_1.red = 11;
    network_connected = 1;
    assert(!aimtrainer_start());
    network_connected = 0;
    assert(aimtrainer_start() && aimtrainer_active() && rebuilds == 1);
    assert(!aimtrainer_start());
    assert(local_player_id == 0 && players[0].team == TEAM_1);
    assert(gamestate.team_1.red == 255 && gamestate.team_1.green == 255 && gamestate.team_1.blue == 255);
    assert(gamestate.team_2.red == 26 && gamestate.team_2.green == 255 && !gamestate.team_2.blue);
    assert((libvxl_map_get(&map, c, c, top) & 0xffffff) == 0xff0000);
    assert((libvxl_map_get(&map, 0, 0, top) & 0xffffff) == 0x323232);
    for(int z = -9; z <= 9; ++z)
        for(int x = -9; x <= 9; ++x) {
            int d = abs(x) > abs(z) ? abs(x) : abs(z);
            bool dry = d <= 2 || d >= 8;
            assert(libvxl_map_issolid(&map, c+x, c+z, top) == dry);
            assert(libvxl_map_issolid(&map, c+x, c+z, 62) == dry);
            assert(libvxl_map_issolid(&map, c+x, c+z, 63));
            assert(!libvxl_map_issolid(&map, c+x, c+z, top-1));
        }
    for(int id = 1; id <= AIMTRAINER_TARGETS; ++id) {
        struct Player* p = &players[id];
        assert(p->alive && p->connected && p->team == TEAM_2 && p->held_item == TOOL_SPADE);
        float x = p->pos.x - camera_x, z = p->pos.z - camera_z;
        assert(fabsf(hypotf(x,z) - 127.25F) < .01F);
        assert(x*p->orientation.x + z*p->orientation.z < -127.F);
        assert(p->pos.x == p->prev_pos.x);
    }
    const struct Position spot = players[0].pos;
    aimtrainer_hit(0);
    assert(players[0].alive);
    aimtrainer_hit(1);
    assert(!players[1].alive && !players[1].connected);
    bool fell = false, drowned = false;
    for(int tick = 0; tick < 2600; ++tick) {
        now += 1.F/60.F;
        aimtrainer_tick(1.F/60.F);
        assert(players[0].pos.x == spot.x && players[0].pos.y == spot.y && players[0].pos.z == spot.z);
        if(tick == 25) assert(players[1].alive);
        if(players[2].physics.airborne && players[2].alive) {
            assert(players[2].pos.y < spot.y);
            fell = true;
        }
        if(!players[2].alive) {
            assert(fell && players[2].pos.y <= 3.25F);
            drowned = true;
        }
    }
    assert(fell && drowned && players[2].alive);
    aimtrainer_stop();
    assert(!aimtrainer_active() && !players[1].connected && gamestate.team_1.red == 11 && fog_color[0] == .1F);
    libvxl_free(&map);
}
'''

with TemporaryDirectory() as directory:
    test = Path(directory) / 'trainer.c'
    exe = test.with_suffix('')
    test.write_text(stubs + layout + 'static bool active;' + trainer + checks)
    run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src'),
         '-I', str(root / 'deps/libvxl'), str(test), str(root / 'deps/libvxl/libvxl.c'),
         '-lm', '-o', str(exe)], check=True)
    run([str(exe)], check=True)
