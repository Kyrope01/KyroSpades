#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aimtrainer.h"
#include "camera.h"
#include "chunk.h"
#include "config.h"
#include "demo.h"
#include "map.h"
#include "network.h"
#include "player.h"
#include "water.h"
#include "weapon.h"
#include "window.h"

static bool active;
static struct GameState saved_gamestate;
static float saved_fog[4];
static float respawn[AIMTRAINER_TARGETS + 1];
static float fall_speed[AIMTRAINER_TARGETS + 1];
static const float center = AIMTRAINER_CENTER + 0.5F;
/* Match the standing collision hull in player_boxclipmove (0.9 + 1.35). */
#define STANDING_CLEARANCE 2.25F
static const float standing_y = AIMTRAINER_GROUND + 1.0F + STANDING_CLEARANCE;

bool aimtrainer_active(void) { return active; }

int aimtrainer_column_distance(int x, int z) {
    int dx = abs(x - AIMTRAINER_CENTER), dz = abs(z - AIMTRAINER_CENTER);
    return dx > dz ? dx : dz;
}

static void place_player(struct Player* p, float x, float y, float z) {
    p->pos.x = p->physics.eye.x = x;
    p->pos.y = p->physics.eye.y = y;
    p->pos.z = p->physics.eye.z = z;
}

static void spawn_target(int id, float angle) {
    struct Player* p = &players[id];
    player_reset(p);
    respawn[id] = fall_speed[id] = 0.0F;
    float radius = fminf(250.0F, fmaxf(14.0F, settings.render_distance - 0.75F));
    float x = cosf(angle), z = sinf(angle);
    place_player(p, center + x * radius, standing_y, center + z * radius);
    p->orientation.x = -x;
    p->orientation.y = 0;
    p->orientation.z = -z;
    p->orientation_smooth = p->orientation;
    p->team = TEAM_2;
    p->held_item = TOOL_SPADE;
    p->connected = p->alive = 1;
    p->sound.feet_started_cycle = game_time();
    snprintf(p->name, sizeof(p->name), "Target %02d", id);
    player_prev_snap(id);
}

bool aimtrainer_start(void) {
    if(active || network_connected)
        return false;
    if(!map_load_aimtrainer())
        return false;
    saved_gamestate = gamestate;
    memcpy(saved_fog, fog_color, sizeof(saved_fog));
    memset(&gamestate, 0, sizeof(gamestate));
    gamestate.gamemode_type = GAMEMODE_TC; /* No bases, flags or objectives. */
    gamestate.team_1 = (struct Team){"Trainee", 255, 255, 255};
    gamestate.team_2 = (struct Team){"Targets", 26, 255, 0};
    fog_color[0] = fog_color[1] = fog_color[2] = 0.55F;

    player_init();
    local_player_id = 0;
    local_player_health = 100;
    local_player_blocks = local_player_grenades = 0;
    local_player_lasttool = TOOL_GUN;
    memset(button_map, 0, sizeof(button_map));
    struct Player* p = &players[0];
    place_player(p, center, standing_y, center);
    p->connected = p->alive = 1;
    p->team = TEAM_1;
    p->weapon = WEAPON_RIFLE;
    p->orientation = (struct Orientation){0, 0, 1};
    p->orientation_smooth = p->orientation;
    snprintf(p->name, sizeof(p->name), "%s", settings.name);
    p->item_disabled = p->item_showup = window_time() - 1.0F;
    p->items_show = 0;
    p->physics.lastclimb = game_time() - 1.0F;
    player_prev_snap(0);
    view_offset = (struct Position){0};
    weapon_set(false);
    camera_mode = CAMERAMODE_FPS;
    camera_rot_x = 0.0F;
    camera_rot_y = 1.57079633F;
    camera_crosshair_rot_x = camera_muzzle_rot_x = camera_rot_x;
    camera_crosshair_rot_y = camera_muzzle_rot_y = camera_rot_y;
    camera_x = center;
    camera_y = standing_y + player_height(p);
    camera_z = center;

    float phase = (float)rand() / (float)RAND_MAX;
    for(int id = 1; id <= AIMTRAINER_TARGETS; ++id)
        spawn_target(id, 6.28318531F * (phase + (id - 1.0F) / AIMTRAINER_TARGETS));
    active = true;
    chunk_rebuild_all();
    return true;
}

void aimtrainer_stop(void) {
    if(!active)
        return;
    active = false;
    player_init();
    gamestate = saved_gamestate;
    memcpy(fog_color, saved_fog, sizeof(saved_fog));
    camera_mode = CAMERAMODE_SPECTATOR;
}

void aimtrainer_hit(int id) {
    if(!active || id < 1 || id > AIMTRAINER_TARGETS || !players[id].alive)
        return;
    players[id].alive = players[id].connected = 0;
    respawn[id] = 0.35F;
}

void aimtrainer_tick(float dt) {
    if(!active)
        return;
    for(int id = 1; id <= AIMTRAINER_TARGETS; ++id) {
        struct Player* p = &players[id];
        if(!p->alive) {
            respawn[id] -= dt;
            if(respawn[id] <= 0.0F)
                spawn_target(id, 6.28318531F * ((float)rand() / (float)RAND_MAX));
            continue;
        }
        float speed = 3.0F + 0.07F * id;
        float x = p->pos.x + p->orientation.x * speed * dt;
        float z = p->pos.z + p->orientation.z * speed * dt;
        float y = p->pos.y;
        p->physics.velocity.x = p->orientation.x * speed / 32.0F;
        p->physics.velocity.z = p->orientation.z * speed / 32.0F;
        if(game_time() - p->sound.feet_started_cycle >= 0.5F) {
            p->sound.feet_started_cycle = game_time();
            p->sound.feet_cylce = !p->sound.feet_cylce;
        }
        if(aimtrainer_column_distance((int)x, (int)z) <= AIMTRAINER_PLATFORM_RADIUS + AIMTRAINER_MOAT_WIDTH
           || fall_speed[id] > 0.0F) {
            fall_speed[id] += 24.0F * dt;
            y -= fall_speed[id] * dt;
            p->physics.airborne = 1;
        }
        place_player(p, x, y, z);
        if(y <= WATER_LEVEL + STANDING_CLEARANCE)
            aimtrainer_hit(id);
    }
}
