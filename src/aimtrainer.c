#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "aimtrainer.h"
#include "camera.h"
#include "chunk.h"
#include "config.h"
#include "common.h"
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
enum Difficulty { EASY, MEDIUM, HARD, PRO, DIFFICULTY_COUNT };
static const struct DifficultyConfig {
    const char* name;
    int targets;
    float multiplier;
} modes[DIFFICULTY_COUNT] = {
    {"easy", 16, 0.1F}, {"medium", 25, 0.5F}, {"hard", 30, 1.0F}, {"pro", 40, 1.5F},
};
static enum Difficulty difficulty;
static struct Target {
    int hp;
    float respawn, move_timer;
    int strafe;
} targets[AIMTRAINER_MAX_TARGETS + 1];
static struct Challenge {
    enum { PRACTICE, COUNTDOWN, RUNNING } phase;
    double start, finish;
    int kills, shots, hits, streak, points;
} challenge;

static float random_unit(void) { return (float)rand() / (float)RAND_MAX; }

/* Start on the open field north of the moat, not on the isolated island. */
static const float spawn_x = 256.5F, spawn_z = 242.5F;
/* Match the standing collision hull in player_boxclipmove (0.9 + 1.35). */
#define STANDING_CLEARANCE 2.25F
static float standing_y(float x, float z) {
    return map_height_at((int)x, (int)z) + 1.0F + STANDING_CLEARANCE;
}

bool aimtrainer_active(void) { return active; }

static void place_player(struct Player* p, float x, float y, float z) {
    p->pos.x = p->physics.eye.x = x;
    p->pos.y = p->physics.eye.y = y;
    p->pos.z = p->physics.eye.z = z;
}

static void spawn_target(int id, float angle) {
    struct Player* p = &players[id];
    player_reset(p);
    targets[id] = (struct Target){.hp = 100, .move_timer = random_unit()};
    p->input.keys.sprint = difficulty >= MEDIUM;
    float radius = fminf(240.0F, fmaxf(14.0F, settings.render_distance - 0.75F));
    float x = cosf(angle), z = sinf(angle);
    float sx = spawn_x + x * radius, sz = spawn_z + z * radius;
    place_player(p, sx, standing_y(sx, sz), sz);
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

static void reset_targets(enum Difficulty next) {
    difficulty = next;
    for(int id = 1; id <= AIMTRAINER_MAX_TARGETS; ++id)
        players[id].alive = players[id].connected = 0;
    const int count = modes[difficulty].targets;
    float phase = random_unit();
    for(int id = 1; id <= count; ++id)
        spawn_target(id, 6.28318531F * (phase + (id - 1.0F) / count));
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
    place_player(p, spawn_x, standing_y(spawn_x, spawn_z), spawn_z);
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
    camera_x = spawn_x;
    camera_y = p->pos.y + player_height(p);
    camera_z = spawn_z;

    challenge = (struct Challenge){0};
    reset_targets(EASY);
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

static int parse_difficulty(const char* name) {
    for(int i = 0; i < DIFFICULTY_COUNT; ++i)
        if(strcmp(name, modes[i].name) == 0)
            return i;
    return -1;
}

bool aimtrainer_command(const char* text) {
    if(!active)
        return false;
    char cmd[32], arg[32], extra[2];
    int fields = sscanf(text, " /%31s %31s %1s", cmd, arg, extra);
    if(fields < 1)
        return false;
    bool timed = strcmp(cmd, "challenge") == 0 || strcmp(cmd, "chalenge") == 0;
    int mode = timed ? (fields == 2 ? parse_difficulty(arg) : -1) : parse_difficulty(cmd);
    if(!timed && mode < 0)
        return false;
    if(mode < 0 || (!timed && fields != 1)) {
        chat_add(0, rgb(255, 210, 140), timed ? "Usage: /challenge <easy|medium|hard|pro>" : "Use the difficulty command without arguments.");
        return true;
    }
    if(challenge.phase != PRACTICE) {
        chat_add(0, rgb(255, 210, 140), "Finish the current challenge before changing difficulty or starting another.");
        return true;
    }
    reset_targets(mode);
    if(timed) {
        double now = window_time();
        challenge = (struct Challenge){.phase = COUNTDOWN, .start = now + 3, .finish = now + 63};
    } else {
        char message[80];
        snprintf(message, sizeof(message), "Aim Trainer: %s (%d bots)", modes[mode].name, modes[mode].targets);
        chat_add(0, rgb(255, 255, 255), message);
    }
    return true;
}

void aimtrainer_update_clock(void) {
    if(!active || challenge.phase == PRACTICE)
        return;
    double now = window_time();
    if(now >= challenge.finish) {
        float accuracy = challenge.shots ? 100.0F * challenge.hits / challenge.shots : 0.0F;
        float final_points = challenge.points * modes[difficulty].multiplier;
        char message[160];
        snprintf(message, sizeof(message), "%s: %d kills | %.1f%% accuracy | %.1f points (%d x %.1f)",
                 modes[difficulty].name, challenge.kills, accuracy, final_points,
                 challenge.points, modes[difficulty].multiplier);
        chat_add(0, rgb(255, 255, 255), message);
        chat_showpopup(message, 8.0F, rgb(255, 255, 255));
        challenge.phase = PRACTICE;
    } else if(now >= challenge.start) {
        challenge.phase = RUNNING;
    }
}

bool aimtrainer_status(char* text, size_t size) {
    if(!active || challenge.phase == PRACTICE)
        return false;
    double now = window_time();
    if(challenge.phase == COUNTDOWN)
        snprintf(text, size, "%s challenge starts in %d", modes[difficulty].name,
                 (int)ceil(fmax(0.0, challenge.start - now)));
    else
        snprintf(text, size, "%ds  |  %d kills  |  %d points",
                 (int)ceil(fmax(0.0, challenge.finish - now)), challenge.kills, challenge.points);
    return true;
}

bool aimtrainer_can_fire(void) { return !active || challenge.phase != COUNTDOWN; }

static void remove_target(int id) {
    players[id].alive = players[id].connected = 0;
    targets[id].respawn = 0.35F;
}

void aimtrainer_damage(int id, int damage) {
    if(!active || challenge.phase == COUNTDOWN || id < 1 || id > modes[difficulty].targets
       || !players[id].alive || damage <= 0)
        return;
    targets[id].hp -= damage;
    if(targets[id].hp > 0)
        return;
    remove_target(id);
    if(challenge.phase == RUNNING) {
        ++challenge.kills;
        ++challenge.points;
        ++challenge.streak;
        if(challenge.streak == 3) challenge.points += 2;
        if(challenge.streak == 5) challenge.points += 5;
    }
}

void aimtrainer_record_shot(bool hit) {
    if(!active || challenge.phase != RUNNING)
        return;
    ++challenge.shots;
    if(hit) ++challenge.hits;
    else challenge.streak = 0;
}

void aimtrainer_tick(float dt) {
    if(!active || challenge.phase == COUNTDOWN)
        return;
    const struct Position trainee = players[local_player_id].pos;
    for(int id = 1; id <= modes[difficulty].targets; ++id) {
        struct Player* p = &players[id];
        struct Target* bot = &targets[id];
        if(!p->alive) {
            bot->respawn -= dt;
            if(bot->respawn <= 0.0F)
                spawn_target(id, 6.28318531F * random_unit());
            continue;
        }
        bot->move_timer -= dt;
        if(difficulty >= HARD && bot->move_timer <= 0.0F) {
            bot->move_timer = 0.35F + random_unit() * 0.9F;
            bot->strafe = random_unit() < 0.5F ? -1 : 1;
            if(difficulty == PRO && !p->physics.airborne) {
                p->input.keys.crouch = random_unit() < 0.30F;
                p->physics.jump = !p->input.keys.crouch && random_unit() < 0.45F;
            }
        }
        float dx = trainee.x - p->pos.x, dz = trainee.z - p->pos.z;
        float distance = hypotf(dx, dz);
        if(distance > 0.001F) {
            p->orientation.x = dx / distance;
            p->orientation.z = dz / distance;
        }
        p->input.keys.up = distance > 2.0F;
        p->input.keys.left = p->input.keys.up && bot->strafe < 0;
        p->input.keys.right = p->input.keys.up && bot->strafe > 0;
        p->input.keys.sprint = difficulty >= MEDIUM && !p->input.keys.crouch;
        player_move(p, dt, id);
        /* Use the map's real water, not the old generated moat footprint. */
        float clearance = p->input.keys.crouch ? 1.35F : STANDING_CLEARANCE;
        if(p->pos.y <= WATER_LEVEL + clearance && map_isair((int)p->pos.x, (int)WATER_LEVEL, (int)p->pos.z)) {
            remove_target(id);
            if(challenge.phase == RUNNING)
                --challenge.points;
        }
    }
}
