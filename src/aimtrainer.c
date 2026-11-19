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
    float respawn, vertical_speed, move_timer, strafe;
    bool falling;
} targets[AIMTRAINER_MAX_TARGETS + 1];
static struct Challenge {
    enum { PRACTICE, COUNTDOWN, RUNNING } phase;
    double start, finish;
    int kills, shots, hits, streak, points;
} challenge;

static float random_unit(void) { return (float)rand() / (float)RAND_MAX; }

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
    targets[id] = (struct Target){.hp = 100, .move_timer = random_unit()};
    p->input.keys.sprint = difficulty >= MEDIUM;
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
    float now = game_time();
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
        if(difficulty >= HARD && bot->move_timer <= 0.0F && !bot->falling) {
            bot->move_timer = 0.35F + random_unit() * 0.9F;
            bot->strafe = (random_unit() < 0.5F ? -1.0F : 1.0F) * (0.5F + random_unit() * 0.7F);
            if(difficulty == PRO && !p->physics.airborne) {
                p->input.keys.crouch = random_unit() < 0.30F;
                if(!p->input.keys.crouch && random_unit() < 0.45F) {
                    bot->vertical_speed = 8.0F;
                    p->physics.airborne = 1;
                }
            }
        }
        p->input.keys.sprint = difficulty >= MEDIUM && !p->input.keys.crouch;
        /* Classic movement: 32 units/s conversion with 4x ground drag;
           sprint is 1.3x and crouch is 0.3x normal walking speed. */
        float speed = p->input.keys.crouch ? 2.4F : (p->input.keys.sprint ? 10.4F : 8.0F);
        float dx = center - p->pos.x, dz = center - p->pos.z;
        float distance = hypotf(dx, dz);
        if(distance > 0.001F) {
            dx /= distance;
            dz /= distance;
            p->orientation.x = dx;
            p->orientation.z = dz;
        } else {
            dx = dz = 0.0F;
        }
        float strafe = bot->falling ? 0.0F : bot->strafe;
        float scale = speed / sqrtf(1.0F + strafe * strafe);
        float vx = (dx - dz * strafe) * scale;
        float vz = (dz + dx * strafe) * scale;
        float x = p->pos.x + vx * dt, z = p->pos.z + vz * dt;
        if(bot->falling && aimtrainer_column_distance((int)x, (int)z) <= AIMTRAINER_PLATFORM_RADIUS) {
            x = p->pos.x;
            z = p->pos.z;
            vx = vz = 0.0F;
        }
        float y = p->pos.y;
        p->physics.velocity.x = vx / 32.0F;
        p->physics.velocity.z = vz / 32.0F;
        if(now - p->sound.feet_started_cycle >= (p->input.keys.sprint ? 0.5F / 1.3F : 0.5F)) {
            p->sound.feet_started_cycle = now;
            p->sound.feet_cylce = !p->sound.feet_cylce;
        }
        if(aimtrainer_column_distance((int)x, (int)z) <= AIMTRAINER_PLATFORM_RADIUS + AIMTRAINER_MOAT_WIDTH) {
            bot->falling = true;
            p->physics.airborne = 1;
        }
        if(p->physics.airborne) {
            bot->vertical_speed -= 32.0F * dt;
            y += bot->vertical_speed * dt;
        }
        float clearance = p->input.keys.crouch ? 1.35F : STANDING_CLEARANCE;
        float floor_y = AIMTRAINER_GROUND + 1.0F + clearance;
        if(!bot->falling && y <= floor_y && bot->vertical_speed <= 0.0F) {
            y = floor_y;
            bot->vertical_speed = 0.0F;
            p->physics.airborne = 0;
        } else if(!p->physics.airborne) {
            y = floor_y;
        }
        place_player(p, x, y, z);
        if(bot->falling && y <= WATER_LEVEL + clearance) {
            remove_target(id);
            if(challenge.phase == RUNNING)
                --challenge.points;
        }
    }
}
