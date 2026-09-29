/* ZeroSpades PieMenuView content/behavior adapted to KyroSpades C renderer.
 * Copyright (c) 2026 Francois ND; based on OpenSpades (c) yvt 2013.
 * GPL-3.0-or-later. See LICENSE. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "camera.h"
#include "config.h"
#include "font.h"
#include "glx.h"
#include "map.h"
#include "network.h"
#include "pie_menu.h"
#include "player.h"
#include "teamplay.h"
#include "texture.h"
#include "window.h"
#include "sound.h"

#define PIEM_PI 3.14159265358979323846f
#define PIEM_SLICES 6
#define PIEM_DEADZONE 66.f
/* Source slice order: top, clockwise. The Point page is shared by all contexts. */
typedef struct PiePage { const char* name; bool global, pings; const char* label[6]; } PiePage;
static const PiePage world_pages[] = {
    {"Point", false, true, {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"}},
    {"Social", false, false, {"Affirmative", "Thank You", "Hi!", "Negative", "Sorry!", "Help Me"}},
    {"Tactics", false, false, {"Attack!", "Get the Intel!", "Enemy Has the Intel!", "Fall Back!", "Regroup on Me", "Defend the Intel!"}}
};
static const PiePage teammate_pages[] = {
    {"Warn", false, false, {"Above You!", "On Your Right!", "Behind You!", "Below You!", "Sniper on You!", "On Your Left!"}},
    {"Social", false, false, {"Affirmative", "Thank You", "Hi!", "Negative", "Sorry!", "Help Me"}},
    {"Cooperate", false, false, {"Follow Me", "Cover Me", "Let Me Through", "Stay Here", "Boost Me Up", "Help Me Build"}},
    {"Point", false, true, {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"}}
};
static const PiePage enemy_pages[] = {
    {"Taunt", true, false, {"I See You", "Nice Try", "Miss Me?", "Too Easy", "Behind You...", "Say Goodbye"}},
    {"Point", false, true, {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"}}
};
static struct {
    bool open, ping_valid;
    int variant, target, page, selection, remembered[3];
    float dx, dy, px, py, pz, phase, page_phase, highlight[6];
} pm = {.target = -1, .selection = -1};

static const PiePage* pages(int variant, int* count) {
    if(variant == 1) { *count = 4; return teammate_pages; }
    if(variant == 2) { *count = 2; return enemy_pages; }
    *count = 3; return world_pages;
}
static const PiePage* current(void) { int n; return pages(pm.variant, &n) + pm.page; }

/* Use the local physics eye and the free-aim crosshair, not a block-placement
 * cell or a bobbed render camera. camera_hit_fromplayer reports a distance to
 * the nearest player/voxel; reconstruct the actual contact point on that ray. */
static bool aim(float* x, float* y, float* z, int* target) {
    if(!players[local_player_id].alive || camera_mode != CAMERAMODE_FPS) return false;
    float ox, oy, oz, vx, vy, vz;
    camera_local_eye(&ox, &oy, &oz);
    if(settings.free_aim) camera_vector_from_angles(camera_crosshair_rot_x, camera_crosshair_rot_y, &vx, &vy, &vz);
    else camera_vector_from_angles(camera_rot_x, camera_rot_y, &vx, &vy, &vz);
    if(!map_isair((int)floorf(ox), (int)floorf(oy), (int)floorf(oz))) return false;
    struct Camera_HitType hit;
    camera_hit_fromplayer(&hit, local_player_id, 256.f);
    if(hit.type != CAMERA_HITTYPE_BLOCK && hit.type != CAMERA_HITTYPE_PLAYER) return false;
    if(!isfinite(hit.distance) || hit.distance < 0 || hit.distance > 256) return false;
    *x = ox + vx * hit.distance;
    *y = oy + vy * hit.distance;
    *z = oz + vz * hit.distance;
    if(target) *target = hit.type == CAMERA_HITTYPE_PLAYER && hit.player_id != local_player_id ? hit.player_id : -1;
    return isfinite(*x) && isfinite(*y) && isfinite(*z);
}
bool pie_menu_opened(void) { return pm.open; }
bool pie_menu_has_selection(void) { return pm.open && pm.selection >= 0; }
bool pie_menu_open(void) {
    if(pm.open || !network_connected || !network_logged_in || network_map_transfer
       || players[local_player_id].team == TEAM_SPECTATOR) return false;
    int target = -1;
    float x, y, z;
    pm.ping_valid = aim(&x, &y, &z, &target);
    pm.target = target;
    pm.variant = target < 0 ? 0 : players[target].team == players[local_player_id].team ? 1 : 2;
    if(pm.ping_valid) { pm.px = x; pm.py = y; pm.pz = z; }
    int count; pages(pm.variant, &count);
    pm.page = pm.remembered[pm.variant] % count;
    pm.dx = pm.dy = 0;
    pm.selection = -1;
    pm.phase = 0; pm.page_phase = 1;
    memset(pm.highlight, 0, sizeof(pm.highlight));
    pm.open = true;
    return true;
}
void pie_menu_move(float dx, float dy) {
    if(!pm.open) return;
    pm.dx += dx; pm.dy += dy;
    float len = hypotf(pm.dx, pm.dy);
    if(len > 196.f) { pm.dx *= 196.f / len; pm.dy *= 196.f / len; }
    if(len < PIEM_DEADZONE) { pm.selection = -1; return; }
    float a = atan2f(pm.dx, -pm.dy);
    if(a < 0) a += 2.f * PIEM_PI;
    pm.selection = ((int)floorf((a + PIEM_PI / 6.f) / (PIEM_PI / 3.f))) % 6;
}
void pie_menu_cycle(int direction) {
    if(!pm.open) return;
    int count; pages(pm.variant, &count);
    pm.page = (pm.page + count + direction % count) % count;
    pm.remembered[pm.variant] = pm.page;
    pm.page_phase = 0;
}
void pie_menu_update(float dt) {
    if(!pm.open || !isfinite(dt) || dt < 0) return;
    pm.phase = fminf(1, pm.phase + dt / .12f);
    pm.page_phase = fminf(1, pm.page_phase + dt / .10f);
    for(int i = 0; i < 6; ++i) {
        float to = i == pm.selection ? 1.f : 0.f;
        float rate = to > pm.highlight[i] ? 10.f : 1.f / .15f;
        if(to > pm.highlight[i]) pm.highlight[i] = fminf(to, pm.highlight[i] + dt * rate);
        else pm.highlight[i] = fmaxf(to, pm.highlight[i] - dt * rate);
    }
}
static void send_chat(const char* text, bool global) {
    if(!network_connected || !network_logged_in || !text) return;
    struct PacketChatMessage msg = {0};
    msg.player_id = local_player_id;
    msg.chat_type = global ? CHAT_ALL : CHAT_TEAM;
    snprintf(msg.message, sizeof(msg.message), "%s", text);
    /* ZeroSpades sends the request without pretending the server accepted it.
       Wait for the server's chat echo or /pm reply instead of displaying a
       local "success" even when a command was rejected or throttled. */
    network_send(PACKET_CHATMESSAGE_ID, &msg, (int)(2 + strlen(msg.message) + 1));
}
void pie_menu_close(bool commit) {
    if(!pm.open) return;
    int selected = pm.selection, target = pm.target;
    const PiePage page = *current();
    float x = pm.px, y = pm.py, z = pm.pz;
    bool valid = pm.ping_valid;
    pm.open = false; pm.target = -1; pm.ping_valid = false;
    pm.selection = -1; pm.dx = pm.dy = 0;
    if(!commit || selected < 0 || selected >= 6 || !network_connected || !network_logged_in) return;
    const char* label = page.label[selected];
    if(page.pings) {
        if(valid && teamplay_send_ping(x, z, 63.f - y, label)) return;
        send_chat(label, false);
    } else if(target >= 0 && target < 255 && players[target].connected && page.global) {
        char text[255];
        snprintf(text, sizeof(text), "%s, %s", players[target].name, label);
        send_chat(text, true);
    } else if(target >= 0 && target < 255 && players[target].connected) {
        char text[255];
        snprintf(text, sizeof(text), "/pm #%d %s", target, label);
        send_chat(text, false);
    } else send_chat(label, page.global);
}
/* Keep glyphs on integer pixels. Two displaced shadow passes at 11-12px
 * made the pie text look smeared; one crisp shadow retains contrast. The
 * selected wedge is nearly white, so its lettering must turn dark. */
static void text(float x, float y, float size, const char* str, float alpha, float selected) {
    if(!str || alpha <= 0) return;
    float px = roundf(x - font_length(size, (char*)str) * .5f);
    float py = roundf(y + size * .5f);
    float ink = selected > .55f ? .06f : 1.f;
    glColor4f(0, 0, 0, .65f * alpha);
    font_render(px, py - 1.f, size, (char*)str);
    glColor4f(ink, ink, ink, alpha);
    font_render(px, py, size, (char*)str);
}
/* Horizontal labels can cross a 60-degree wedge even when they fit in
 * the circle. Measure the actual font and split the longer commands at a
 * word boundary; each line fits inside an 82px-wide region at the slice's
 * centre radius, including the entrance animation's smaller radius. */
static void slice_label(float x, float y, const char* label, float alpha, float fit, float selected) {
    const float max_width = 82.f * fit;
    size_t length = strlen(label);
    for(float size = 14.f * fit; size >= 8.f * fit; size -= fit) {
        if(font_length(size, (char*)label) <= max_width) {
            text(x, y, size, label, alpha, selected);
            return;
        }
        char first[64], second[64], best_first[64] = {0}, best_second[64] = {0};
        float best = 1e9f;
        if(length < sizeof(first)) {
            for(size_t i = 1; i < length; i++) {
                if(label[i] != ' ') continue;
                memcpy(first, label, i); first[i] = 0;
                snprintf(second, sizeof(second), "%s", label + i + 1);
                float left = font_length(size, first), right = font_length(size, second);
                float widest = fmaxf(left, right);
                if(left <= max_width && right <= max_width && widest < best) {
                    best = widest;
                    strcpy(best_first, first);
                    strcpy(best_second, second);
                }
            }
        }
        if(best_first[0]) {
            text(x, y + 8.f * fit, size, best_first, alpha, selected);
            text(x, y - 8.f * fit, size, best_second, alpha, selected);
            return;
        }
    }
}

void pie_menu_draw(void) {
    if(!pm.open) return;
    const PiePage* page = current();
    float cx = settings.window_width * .5f, cy = settings.window_height * .5f;
    float eased = 1.f - (1.f - pm.phase) * (1.f - pm.phase);
    /* Leave room below for the controls even on compact viewports. */
    float fit = fminf(1.f, fminf((settings.window_width - 32.f) / 390.f,
                                (settings.window_height - 110.f) / 360.f));
    if(fit <= 0) return;
    float scale = fit * (.85f + .15f * eased);
    float page_alpha = 1.f - (1.f - pm.page_phase) * (1.f - pm.page_phase);
    float inner = 77.f * scale, outer = 176.f * scale;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0, 0, 0, .55f * eased);
    glx_draw_ring_segment_2d(cx, cy, 0, outer, 0, 2 * PIEM_PI, 256);
    for(int i = 0; i < 6; ++i) {
        float a = 2.f * PIEM_PI * i / 6.f;
        float from = a - PIEM_PI / 6.f + PIEM_PI / 360.f;
        float to = a + PIEM_PI / 6.f - PIEM_PI / 360.f;
        float h = pm.highlight[i], ro = outer + h * 10.f;
        float v = (.08f + .77f * h) * eased;
        glColor4f(1, 1, 1, v);
        glx_draw_ring_segment_2d(cx, cy, inner, ro, from, to, 48);
        /* One 1px contour, not overlapping rings and a translucent halo. */
        glColor4f(1, 1, 1, .5f * eased);
        glx_draw_ring_segment_2d(cx, cy, ro - 1.f, ro, from, to, 48);
        font_select(FONT_FIXEDSYS);
        float tx = cx + sinf(a) * 126.5f * scale;
        float ty = cy + cosf(a) * 126.5f * scale;
        slice_label(tx, ty, page->label[i], eased * page_alpha, fit, h);
        glEnable(GL_BLEND);
    }
    glColor4f(1, 1, 1, .5f * eased);
    glx_draw_ring_segment_2d(cx, cy, inner, inner + 1, 0, 2.f * PIEM_PI, 256);
    font_select(FONT_FIXEDSYS);
    if(pm.selection < 0) text(cx, cy, 18.f * fit, page->name, .7f * eased * page_alpha, 0.f);
    else {
        const char* chosen = page->label[pm.selection];
        float size = 18.f * fit;
        while(size > 9.f * fit && font_length(size, (char*)chosen) > inner * 1.6f) size -= fit;
        text(cx, cy, size, chosen, pm.highlight[pm.selection] * eased * page_alpha, 0.f);
    }
    glEnable(GL_BLEND);
    int count; pages(pm.variant, &count);
    for(int i = 0; i < count; i++) {
        float x = cx + (i - (count - 1) * .5f) * 13.f * fit;
        glColor4f(1, 1, 1, eased * (i == pm.page ? .95f : .35f));
        glx_draw_ring_segment_2d(x, cy - outer - 16 * fit, 0, i == pm.page ? 3 * fit : 2 * fit, 0, 2 * PIEM_PI, 24);
    }
    font_select(FONT_FIXEDSYS);
    text(cx, cy - outer - 36 * fit, 14.f * fit, "Move Mouse: Choose    G: Next Page", eased, 0.f);
    text(cx, cy - outer - 54 * fit, 14.f * fit, "LMB: Select    RMB: Cancel", eased, 0.f);
    glColor3f(1,1,1);
    glDisable(GL_BLEND);
}
