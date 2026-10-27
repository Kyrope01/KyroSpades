/* ZeroSpades PieMenuView content/behavior adapted to KyroSpades C renderer,
 * plus the server-defined menu of aosprotocol Pie Menu v1 (extension 0x31,
 * zerospades/aosprotocol PR #11).  The built-in pages below are the client's
 * own menu: they stand until a server sends a Menu, and never come back once
 * one has.  Copyright (c) 2026 Francois ND; based on OpenSpades (c) yvt 2013.
 * GPL-3.0-or-later. See LICENSE. */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "camera.h"
#include "cameracontroller.h"
#include "common.h"
#include "config.h"
#include "font.h"
#include "glx.h"
#include "map.h"
#include "network.h"
#include "pie_menu.h"
#include "pie_menu_ext.h"
#include "player.h"
#include "teamplay.h"
#include "texture.h"
#include "window.h"
#include "sound.h"

#define PIEM_PI 3.14159265358979323846f
#define PIEM_SLICES 6
#define PIEM_DEADZONE 66.f
/* The extension's aim has no length limit; this only bounds the hit search,
   and is well past the diagonal of a 512x512x64 world. */
#define PIEM_AIM_RANGE 2048.f
#define PIEM_MAX_OFFERED 255

/* Source slice order: top, clockwise. The Point page is shared by all
   contexts. */
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
/* The contexts of Pie Menu v1, as an index into pm.remembered_server. */
enum { CTX_BLOCK, CTX_TEAMMATE, CTX_ENEMY, CTX_SPECTATOR, CTX_DEAD, CTX_SKY, CTX_COUNT };
static const uint8_t context_bits[CTX_COUNT] = {PIE_CONTEXT_BLOCK, PIE_CONTEXT_TEAMMATE, PIE_CONTEXT_ENEMY,
                                                PIE_CONTEXT_SPECTATOR, PIE_CONTEXT_DEAD, PIE_CONTEXT_SKY};
static struct {
    bool open, ping_valid;
    int variant, target, page, selection, remembered[3];
    float dx, dy, px, py, pz, phase, page_phase, highlight[6];
    /* A server menu, all of it taken when the menu opened: the pies offered to
       the context in the order the server sent them, and the aim. Nothing here
       is read again while the menu is held. */
    bool server;
    int context, remembered_server[CTX_COUNT];
    const Pie* offered[PIEM_MAX_OFFERED];
    int offered_count;
    PieAim aim;
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

/* The view direction of the camera being drawn: a followed player's facing in
 * the follow views, and the view angles everywhere else. Free aim decouples the
 * crosshair from the drawn camera, so it is not the aim here. */
static void drawn_view(float* dx, float* dy, float* dz) {
    if(cameracontroller_bodyview_mode && camera_mode != CAMERAMODE_FPS
       && cameracontroller_bodyview_player >= 0 && cameracontroller_bodyview_player < PLAYERS_MAX
       && players[cameracontroller_bodyview_player].alive) {
        const struct Player* p = &players[cameracontroller_bodyview_player];
        float l = sqrtf(p->orientation_smooth.x * p->orientation_smooth.x
                        + p->orientation_smooth.y * p->orientation_smooth.y
                        + p->orientation_smooth.z * p->orientation_smooth.z);
        if(l > 1e-6f) {
            *dx = p->orientation_smooth.x / l;
            *dy = p->orientation_smooth.y / l;
            *dz = p->orientation_smooth.z / l;
            return;
        }
    }
    camera_vector_from_angles(camera_rot_x, camera_rot_y, dx, dy, dz);
}

/* Pie Menu v1 "Substitutions": the aim is a ray from the camera being drawn,
 * along its view direction, with no length limit. The player aimed at is the
 * first player it meets before any block; the block aimed at is the first solid
 * block it meets, which players do not hide. */
static void take_aim(PieAim* out) {
    memset(out, 0, sizeof *out);
    out->player = -1;
    float vx, vy, vz;
    drawn_view(&vx, &vy, &vz);
    float l = sqrtf(vx * vx + vy * vy + vz * vz);
    if(l > 1e-6f) { vx /= l; vy /= l; vz /= l; }
    else { vx = vy = 0; vz = -1.f; }
    out->ray_x = camera_x;
    out->ray_y = camera_y;
    out->ray_z = camera_z;
    out->dir_x = vx;
    out->dir_y = vy;
    out->dir_z = vz;
    int* cell = camera_terrain_pickEx(1, camera_x, camera_y, camera_z, vx, vy, vz);
    if(cell) {
        out->has_block = true;
        /* Kyro renders y up; the wire has z down from the top of the map. */
        out->block_x = cell[0];
        out->block_y = cell[2];
        out->block_z = 63 - cell[1];
    }
    struct Camera_HitType hit;
    int exclude = camera_mode == CAMERAMODE_BODYVIEW && cameracontroller_bodyview_player >= 0
                      && cameracontroller_bodyview_player < PLAYERS_MAX
                      ? cameracontroller_bodyview_player
                      : local_player_id;
    camera_hit(&hit, exclude, camera_x, camera_y, camera_z, vx, vy, vz, PIEM_AIM_RANGE);
    if((hit.type == CAMERA_HITTYPE_BLOCK || hit.type == CAMERA_HITTYPE_PLAYER) && isfinite(hit.distance)
       && hit.distance >= 0) {
        out->hit = true;
        out->hit_x = camera_x + vx * hit.distance;
        out->hit_y = camera_z + vz * hit.distance;
        out->hit_z = 63.f - (camera_y + vy * hit.distance);
        /* player_id is a byte and every byte indexes players[]; take_context
           bounds the int it becomes before it reads the array. */
        if(hit.type == CAMERA_HITTYPE_PLAYER) out->player = hit.player_id;
    }
}

/* What the aim meets first, a block, a player or nothing, decides which pies
 * are offered. A staff member spectating from a team counts as a spectator, and
 * a player who is dead and waiting to respawn is DEAD. */
static int take_context(void) {
    if(players[local_player_id].team == TEAM_SPECTATOR || camera_mode == CAMERAMODE_SPECTATOR) return CTX_SPECTATOR;
    if(!players[local_player_id].alive) return CTX_DEAD;
    /* aim.player is set only when a player came before any block, so the three
       cases below are exactly the spec's block, player and nothing. */
    if(pm.aim.player >= 0 && pm.aim.player < PLAYERS_MAX)
        return players[pm.aim.player].team == players[local_player_id].team ? CTX_TEAMMATE : CTX_ENEMY;
    return pm.aim.has_block ? CTX_BLOCK : CTX_SKY;
}

bool pie_menu_opened(void) { return pm.open; }
bool pie_menu_has_selection(void) { return pm.open && pm.selection >= 0; }
bool pie_menu_server_menu(void) { return pm.open && pm.server; }
bool pie_menu_open(void) {
    if(pm.open || !network_connected || !network_logged_in || network_map_transfer) return false;
    pm.server = pie_menu_ext_owner();
    /* The built-in menu has no pies for a spectator; a server's may. */
    if(!pm.server && players[local_player_id].team == TEAM_SPECTATOR) return false;
    int target = -1;
    float x, y, z;
    pm.ping_valid = aim(&x, &y, &z, &target);
    pm.target = target;
    pm.variant = target < 0 ? 0 : players[target].team == players[local_player_id].team ? 1 : 2;
    if(pm.ping_valid) { pm.px = x; pm.py = y; pm.pz = z; }
    take_aim(&pm.aim);
    if(pm.server) {
        pm.context = take_context();
        uint8_t bit = context_bits[pm.context];
        pm.offered_count = 0;
        for(int i = 0; i < pie_menu_ext_count() && pm.offered_count < PIEM_MAX_OFFERED; i++) {
            const Pie* pie = pie_menu_ext_pie(i);
            if(pie && (pie->contexts & bit)) pm.offered[pm.offered_count++] = pie;
        }
        /* A context with no pies has no menu, and no other context's pies
           stand in. */
        if(!pm.offered_count) { pm.server = false; return false; }
        pm.page = pm.remembered_server[pm.context] % pm.offered_count;
        pie_menu_ext_freeze(); /* the menu itself is taken when it opens */
    } else {
        int count; pages(pm.variant, &count);
        pm.page = pm.remembered[pm.variant] % count;
    }
    pm.dx = pm.dy = 0;
    pm.selection = -1;
    pm.phase = 0; pm.page_phase = 1;
    memset(pm.highlight, 0, sizeof(pm.highlight));
    pm.open = true;
    return true;
}

/* One ring of the open menu, whichever kind it is. */
static int pie_count(void) {
    if(pm.server) return pm.offered_count;
    int n; pages(pm.variant, &n); return n;
}
static const char* pie_theme(void) {
    if(pm.server) return pm.offered[pm.page]->theme;
    return current()->name;
}
static const char* pie_slice_label(int i) {
    if(!pm.server) return current()->label[i];
    const PieSlice* s = &pm.offered[pm.page]->slice[i];
    return s->label[0] ? s->label : s->text; /* an empty Label draws the Text */
}
/* A blank slice cannot be chosen: aiming at it selects nothing, as the centre
   does. Blank is a Text and a Label that are both empty, whatever the Tooltip. */
static bool pie_slice_enabled(int i) {
    if(!pm.server) return true;
    const PieSlice* s = &pm.offered[pm.page]->slice[i];
    return s->text[0] || s->label[0];
}
static const char* pie_slice_tooltip(int i) {
    if(!pm.server) return NULL;
    const char* tip = pm.offered[pm.page]->slice[i].tooltip;
    return tip[0] ? tip : NULL;
}

void pie_menu_move(float dx, float dy) {
    if(!pm.open) return;
    pm.dx += dx; pm.dy += dy;
    float len = hypotf(pm.dx, pm.dy);
    if(len > 196.f) { pm.dx *= 196.f / len; pm.dy *= 196.f / len; }
    if(len < PIEM_DEADZONE) { pm.selection = -1; return; }
    float a = atan2f(pm.dx, -pm.dy);
    if(a < 0) a += 2.f * PIEM_PI;
    int wedge = ((int)floorf((a + PIEM_PI / 6.f) / (PIEM_PI / 3.f))) % 6;
    pm.selection = pie_slice_enabled(wedge) ? wedge : -1;
}
void pie_menu_cycle(int direction) {
    if(!pm.open) return;
    int count = pie_count();
    if(count < 1) return;
    pm.page = (pm.page + count + direction) % count;
    if(pm.server) pm.remembered_server[pm.context] = pm.page;
    else pm.remembered[pm.variant] = pm.page;
    if(pm.selection >= 0 && !pie_slice_enabled(pm.selection)) pm.selection = -1;
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
/* Pie Menu v1 "Sending": a chosen slice sends its Text as the client normally
   sends chat, GLOBAL included, and the client interprets none of it: a Text
   that starts with '/' is a command for the server to read, on either channel.
   Marking a place is no action of its own -- a slice that wants one sends the
   Teamplay /pingmark command, as in "/pingmark %h Enemy". */
static void send_slice(const Pie* pie, int index, const PieAim* aim) {
    const PieSlice* slice = &pie->slice[index];
    char text[PIE_MENU_TEXT_MAX + 1];
    /* Expanded once, immediately before sending, and never re-scanned. */
    pie_menu_ext_expand(slice->text, aim, text, sizeof text);
    send_chat(text, (slice->flags & PIE_FLAG_GLOBAL) != 0);
}
void pie_menu_close(bool commit) {
    if(!pm.open) return;
    int selected = pm.selection, target = pm.target, page_index = pm.page, variant = pm.variant;
    const Pie* pie = pm.server && pm.page >= 0 && pm.page < pm.offered_count ? pm.offered[pm.page] : NULL;
    PieAim aim = pm.aim;
    float x = pm.px, y = pm.py, z = pm.pz;
    bool valid = pm.ping_valid, server = pm.server;
    pm.open = false; pm.target = -1; pm.ping_valid = false;
    pm.selection = -1; pm.dx = pm.dy = 0;
    pm.server = false;
    if(commit && selected >= 0 && selected < 6 && network_connected && network_logged_in) {
        if(pie) send_slice(pie, selected, &aim);
        else {
            /* The client's own menu. A server's may have left the page past the
               end of this variant's pages, so bound it before reading one. */
            int pages_in_variant;
            const PiePage* variant_pages = pages(variant, &pages_in_variant);
            const PiePage page = variant_pages[page_index >= 0 && page_index < pages_in_variant ? page_index : 0];
            const char* label = page.label[selected];
            if(page.pings) {
                if(!(valid && teamplay_send_ping(x, z, 63.f - y, label))) send_chat(label, false);
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
    }
    /* The held pies were read above; only now may the Menu that replaced them
       while this one was open be released. */
    if(server) pie_menu_ext_thaw();
}
/* Keep glyphs on integer pixels. Two displaced shadow passes at 11-12px
 * made the pie text look smeared; one crisp shadow retains contrast. The
 * selected wedge is nearly white, so its lettering must turn dark. */
/* Chat's inline colour codes 1-7, and the 0xff marker that prefixes a UTF-8
   string. A server's Theme, Label and Tooltip are drawn the way chat is, so
   these steer the ink rather than reaching the font, which has no glyph for
   them and would show a '?'. */
static bool pie_ink_code(unsigned char c) { return c >= 1 && c <= 7; }

static void pie_ink_color(unsigned char code, float ink, float* r, float* g, float* b) {
    switch(code) {
        case 1:
            *r = gamestate.team_1.red / 255.f;
            *g = gamestate.team_1.green / 255.f;
            *b = gamestate.team_1.blue / 255.f;
            break;
        case 2:
            *r = gamestate.team_2.red / 255.f;
            *g = gamestate.team_2.green / 255.f;
            *b = gamestate.team_2.blue / 255.f;
            break;
        case 4: *r = 1.f; *g = 0.f; *b = 0.f; break;
        case 5: *r = 0.f; *g = 1.f; *b = 0.f; break;
        case 7: *r = *g = *b = 120.f / 255.f; break;
        default: *r = *g = *b = 1.f; break; /* 3 and 6 are white */
    }
    /* A highlighted wedge is near white behind its label, where plain ink goes
       black; scaling a colour the same way keeps the run legible there. */
    *r *= ink; *g *= ink; *b *= ink;
}

/* What is drawn, with the codes that steer the ink left out. */
static size_t pie_plain(const char* str, char* dst, size_t cap) {
    size_t n = 0;
    for(const char* s = str; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if(pie_ink_code(c) || c == 0xff) continue;
        if(n + 1 >= cap) break;
        dst[n++] = *s;
    }
    dst[n] = 0;
    return n;
}

static float pie_text_length(float size, const char* str) {
    char plain[PIE_MENU_UTF8_MAX + 1];
    pie_plain(str, plain, sizeof plain);
    return font_length(size, plain);
}

static void text(float x, float y, float size, const char* str, float alpha, float selected) {
    if(!str || alpha <= 0) return;
    char plain[PIE_MENU_UTF8_MAX + 1];
    pie_plain(str, plain, sizeof plain);
    float px = roundf(x - font_length(size, plain) * .5f);
    float py = roundf(y + size * .5f);
    float ink = selected > .55f ? .06f : 1.f;
    /* One shadow for the whole line, then a run per colour. */
    glColor4f(0, 0, 0, .65f * alpha);
    font_render(px, py - 1.f, size, plain);
    char run[PIE_MENU_UTF8_MAX + 1];
    size_t n = 0;
    float rx = px, r = ink, g = ink, b = ink;
    for(const char* s = str;; s++) {
        unsigned char c = (unsigned char)*s;
        if(!c || pie_ink_code(c) || c == 0xff) {
            if(n) {
                run[n] = 0;
                glColor4f(r, g, b, alpha);
                font_render(roundf(rx), py, size, run);
                rx += font_length(size, run) - size * .125f; /* less the trailing bearing */
                n = 0;
            }
            if(!c) break;
            if(pie_ink_code(c)) pie_ink_color(c, ink, &r, &g, &b);
            continue;
        }
        if(n + 1 < sizeof run) run[n++] = *s;
    }
}
/* Horizontal labels can cross a 60-degree wedge even when they fit in
 * the circle. Measure the actual font and split the longer commands at a
 * word boundary; each line fits inside an 82px-wide region at the slice's
 * centre radius, including the entrance animation's smaller radius. */
static void slice_label(float x, float y, const char* label, float alpha, float fit, float selected) {
    const float max_width = 82.f * fit;
    size_t length = strlen(label);
    for(float size = 14.f * fit; size >= 8.f * fit; size -= fit) {
        if(pie_text_length(size, label) <= max_width) {
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
                float left = pie_text_length(size, first), right = pie_text_length(size, second);
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
    for(int i = 0; i < PIEM_SLICES; ++i) {
        float a = 2.f * PIEM_PI * i / 6.f;
        float from = a - PIEM_PI / 6.f + PIEM_PI / 360.f;
        float to = a + PIEM_PI / 6.f - PIEM_PI / 360.f;
        bool enabled = pie_slice_enabled(i);
        float h = enabled ? pm.highlight[i] : 0.f, ro = outer + h * 10.f;
        float v = (.08f + .77f * h) * eased;
        glColor4f(1, 1, 1, v);
        glx_draw_ring_segment_2d(cx, cy, inner, ro, from, to, 48);
        /* One 1px contour, not overlapping rings and a translucent halo. */
        glColor4f(1, 1, 1, .5f * eased);
        glx_draw_ring_segment_2d(cx, cy, ro - 1.f, ro, from, to, 48);
        font_select(FONT_FIXEDSYS);
        float tx = cx + sinf(a) * 126.5f * scale;
        float ty = cy + cosf(a) * 126.5f * scale;
        /* A slice that cannot be chosen still shows its Label, dimmed. */
        slice_label(tx, ty, pie_slice_label(i), eased * page_alpha * (enabled ? 1.f : .45f), fit, h);
        glEnable(GL_BLEND);
    }
    glColor4f(1, 1, 1, .5f * eased);
    glx_draw_ring_segment_2d(cx, cy, inner, inner + 1, 0, 2.f * PIEM_PI, 256);
    font_select(FONT_FIXEDSYS);
    if(pm.selection < 0) text(cx, cy, 18.f * fit, pie_theme(), .7f * eased * page_alpha, 0.f);
    else {
        /* The ring is captioned in the middle by its theme; the chosen slice
           and, while it is highlighted, its tooltip go below it. */
        text(cx, cy - 26.f * fit, 12.f * fit, pie_theme(), .5f * eased * page_alpha, 0.f);
        const char* chosen = pie_slice_label(pm.selection);
        float size = 18.f * fit;
        while(size > 9.f * fit && pie_text_length(size, chosen) > inner * 1.6f) size -= fit;
        text(cx, cy, size, chosen, pm.highlight[pm.selection] * eased * page_alpha, 0.f);
        const char* tip = pie_slice_tooltip(pm.selection);
        if(tip) {
            float tip_size = 11.f * fit;
            while(tip_size > 7.f * fit && pie_text_length(tip_size, tip) > inner * 1.9f) tip_size -= fit;
            text(cx, cy + 24.f * fit, tip_size, tip, .85f * pm.highlight[pm.selection] * eased * page_alpha, 0.f);
        }
    }
    glEnable(GL_BLEND);
    int count = pie_count();
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
