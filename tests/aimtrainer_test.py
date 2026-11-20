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
#define PLAYERS_MAX 64
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
    struct { struct Position eye, velocity; int airborne, jump; float lastclimb; } physics;
    struct { float feet_started_cycle; bool feet_cylce; } sound;
    struct { struct { bool sprint, crouch, up, left, right; } keys; } input;
    int team, held_item, connected, alive, weapon, items_show;
    float item_disabled, item_showup;
    char name[32];
} players[PLAYERS_MAX];
int local_player_id, local_player_health, local_player_blocks, local_player_grenades, local_player_lasttool;
int camera_mode, network_connected, button_map[3], rebuilds;
float camera_x, camera_y, camera_z, camera_rot_x, camera_rot_y;
float camera_crosshair_rot_x, camera_crosshair_rot_y, camera_muzzle_rot_x, camera_muzzle_rot_y;
float fog_color[4] = {.1F,.2F,.3F,1};
double now;
static char message_log[200];
static unsigned rgb(int r, int g, int b) { return (r << 16) | (g << 8) | b; }
static void chat_add(int channel, unsigned color, const char* message) {
    (void)channel; (void)color; snprintf(message_log, sizeof(message_log), "%s", message);
}
static void chat_showpopup(const char* message, float duration, unsigned color) {
    (void)message; (void)duration; (void)color;
}
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
static unsigned char* file_load_sized(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    assert(!fseek(f, 0, SEEK_END));
    *size = (size_t)ftell(f);
    rewind(f);
    unsigned char* bytes = malloc(*size);
    assert(bytes && fread(bytes, 1, *size, f) == *size);
    fclose(f);
    return bytes;
}
static int map_height_at(int x, int z) {
    uint32_t top[2];
    assert(libvxl_map_gettop(&map, x, z, top));
    return 63 - (int)top[1];
}
static bool map_isair(int x, int y, int z) {
    return !libvxl_map_issolid(&map, x, z, 63-y);
}
/* Stub the normal player movement routine to verify AI input and dispatch. */
static int player_move(struct Player* p, float dt, int id) {
    assert(id > 0 && p == &players[id]);
    float speed = p->input.keys.sprint ? 10.4F : 8.F;
    float strafe = (int)p->input.keys.right - (int)p->input.keys.left;
    p->physics.velocity.x = (p->orientation.x - strafe * p->orientation.z) * speed / 32.F;
    p->physics.velocity.z = (p->orientation.z + strafe * p->orientation.x) * speed / 32.F;
    p->pos.x += p->physics.velocity.x * dt * 32.F;
    p->pos.z += p->physics.velocity.z * dt * 32.F;
    p->physics.airborne = p->physics.jump;
    if(p->physics.jump) p->pos.y += 1.F;
    p->physics.jump = 0;
    return 0;
}

'''

checks = r'''
int main(void) {
    const int c = 256;
    gamestate.team_1.red = 11;
    network_connected = 1;
    assert(!aimtrainer_start());
    network_connected = 0;
    assert(aimtrainer_start() && aimtrainer_active() && rebuilds == 1);
    assert(!aimtrainer_start());
    assert(local_player_id == 0 && players[0].team == TEAM_1);
    assert(gamestate.team_1.red == 255 && gamestate.team_1.green == 255 && gamestate.team_1.blue == 255);
    assert(gamestate.team_2.red == 26 && gamestate.team_2.green == 255 && !gamestate.team_2.blue);
    assert(map_height_at(c, c) == 5 && map_height_at(0, 0) == 5);
    assert(map_height_at(254, 251) == 0);
    assert(players[0].pos.y == 8.25F && players[0].pos.z < 249.F);
    for(int id = 1; id <= 16; ++id) {
        struct Player* p = &players[id];
        assert(p->alive && p->connected && p->team == TEAM_2 && p->held_item == TOOL_SPADE);
        float x = p->pos.x - camera_x, z = p->pos.z - camera_z;
        assert(fabsf(hypotf(x,z) - 127.25F) < .01F);
        assert(x*p->orientation.x + z*p->orientation.z < -127.F);
        assert(p->pos.x == p->prev_pos.x);
    }
    const struct Position spot = players[0].pos;
    aimtrainer_damage(0, 100);
    assert(players[0].alive);
    aimtrainer_damage(1, 49);
    aimtrainer_damage(1, 49);
    assert(players[1].alive);
    aimtrainer_damage(1, 33);
    assert(!players[1].alive && !players[1].connected);
    players[0].pos.x = c + 30.F;
    for(int tick = 0; tick < 26; ++tick) {
        now += 1.F/60.F;
        aimtrainer_tick(1.F/60.F);
    }
    assert(players[1].alive && players[0].pos.x == c + 30.F);
    assert(players[2].orientation.x * (players[0].pos.x - players[2].pos.x) +
           players[2].orientation.z * (players[0].pos.z - players[2].pos.z) > 0.F);
    players[0].pos = spot;
    assert(aimtrainer_command("/medium") && players[25].alive && !players[26].alive);
    assert(players[25].input.keys.sprint);
    float prior = hypotf(players[25].pos.x - camera_x, players[25].pos.z - camera_z);
    aimtrainer_tick(1.F/60.F);
    float after = hypotf(players[25].pos.x - camera_x, players[25].pos.z - camera_z);
    assert(fabsf((prior - after) * 60.F - 10.4F) < .01F);
    assert(aimtrainer_command("/hard") && players[30].alive && !players[31].alive);
    assert(aimtrainer_command("/challenge") && strstr(message_log, "Usage:"));
    assert(aimtrainer_command("/challenge pro") && players[40].alive);
    assert(!aimtrainer_can_fire());
    aimtrainer_damage(1, 100);
    assert(players[1].alive);
    assert(aimtrainer_command("/easy") && players[40].alive);
    now = challenge.start - 0.01;
    aimtrainer_update_clock();
    assert(!aimtrainer_can_fire());
    now = challenge.start;
    aimtrainer_update_clock();
    assert(aimtrainer_can_fire());
    for(int id = 1; id <= 5; ++id) {
        aimtrainer_damage(id, 100);
        aimtrainer_record_shot(true);
    }
    assert(challenge.points == 12);
    aimtrainer_record_shot(false);
    assert(challenge.streak == 0);
    place_player(&players[6], 254.5F, WATER_LEVEL + STANDING_CLEARANCE - 0.1F, 251.5F);
    aimtrainer_tick(1.F/60.F);
    assert(challenge.points == 11 && challenge.kills == 5);
    now = challenge.finish;
    aimtrainer_update_clock();
    assert(strstr(message_log, "5 kills") && strstr(message_log, "83.3%") && strstr(message_log, "16.5 points"));
    assert(!aimtrainer_status(message_log, sizeof(message_log)));
    bool strafed = false, crouched = false, jumped = false;
    for(int tick = 0; tick < 180; ++tick) {
        now += 1.F/60.F;
        aimtrainer_tick(1.F/60.F);
        for(int id = 1; id <= 40; ++id) {
            struct Player* p = &players[id];
            strafed |= fabsf(p->physics.velocity.x * p->orientation.z -
                             p->physics.velocity.z * p->orientation.x) > .05F;
            crouched |= p->input.keys.crouch;
            jumped |= p->physics.airborne && p->pos.y > standing_y(p->pos.x, p->pos.z);
        }
    }
    assert(strafed && crouched && jumped);
    assert(aimtrainer_command("/easy") && players[16].alive && !players[17].alive);
    aimtrainer_stop();
    assert(!aimtrainer_active() && !players[1].connected && gamestate.team_1.red == 11 && fog_color[0] == .1F);
    libvxl_free(&map);
}
'''

# Trainer hits return before the network hit packet, but must still emit the
# shared HUD indicator, including on lethal hits that remove the bot.
weapon = (root / 'src/weapon.c').read_text()
shot = weapon.split('void weapon_shoot() {', 1)[1]
player_hit = shot.split('case CAMERA_HITTYPE_PLAYER:', 1)[1].split('case CAMERA_HITTYPE_BLOCK:', 1)[0]
assert player_hit.index('damagenumbers_add(hit.player_id, dmg, hit_x, hit_y, hit_z)') < player_hit.index('if(aimtrainer_active())')
assert 'aimtrainer_damage(hit.player_id, dmg)' in player_hit

with TemporaryDirectory() as directory:
    test = Path(directory) / 'trainer.c'
    exe = test.with_suffix('')
    test.write_text(stubs + layout + 'static bool active;' + trainer + checks)
    run(['cc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'src'),
         '-I', str(root / 'deps/libvxl'), str(test), str(root / 'deps/libvxl/libvxl.c'),
         '-lm', '-o', str(exe)], check=True)
    run([str(exe)], cwd=root / "resources", check=True)

# The ordinary fixed-tick movement path still owns the local trainee.
player = (root / 'src/player.c').read_text()
assert 'if(!aimtrainer_active() || k == local_player_id)' in player
controller = (root / 'src/cameracontroller.c').read_text()
assert 'if(!aimtrainer_active() && chat_input_mode' not in controller
