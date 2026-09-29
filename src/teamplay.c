/* Teamplay v1 wire/state port of ZeroSpades (GPL-3.0-or-later).
 * Copyright (c) 2026 Fran6nd and ZeroSpades developers.
 * Original ZeroSpades Sources/Client/{Teamplay,NetClient}.cpp.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "network.h"
#include "player.h"
#include "teamplay.h"
#include "chatlog.h"
#include "sound.h"
#include "demo.h"

TeamplayPing teamplay_pings[TEAMPLAY_PLAYERS];
uint8_t teamplay_features;
float teamplay_north_x = 0.0f, teamplay_north_y = -1.0f;
static bool negotiated;

static uint32_t le32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float lefloat(const uint8_t* p) {
    uint32_t bits = le32(p);
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
/* CMake builds with -Ofast/-ffast-math; libc isnan/isfinite/isinf may
 * optimize away. Inspect IEEE754 wire bits before any arithmetic instead. */
static uint32_t floatbits(float f) { uint32_t v; memcpy(&v,&f,4); return v; }
static bool tp_nan(float f) { return (floatbits(f) & 0x7fffffffU) > 0x7f800000U; }
static bool tp_inf(float f) { return (floatbits(f) & 0x7fffffffU) == 0x7f800000U; }
static bool tp_finite(float f) { return (floatbits(f) & 0x7f800000U) != 0x7f800000U; }
static void putfloat(uint8_t* p, float f) {
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    for(int i = 0; i < 4; ++i) p[i] = (uint8_t)(bits >> (i * 8));
}

/* Drop malformed UTF-8, embedded NUL and control characters. Never split a
 * codepoint across the 64-byte cap (or trust a packet to be NUL-terminated). */
void teamplay_reason(char out[65], const uint8_t* s, size_t len) {
    size_t j = 0;
    for(size_t i = 0; i < len;) {
        uint32_t cp;
        size_t n;
        uint8_t b = s[i];
        if(b < 0x80) { n = 1; cp = b; }
        else if(b >= 0xc2 && b <= 0xdf) { n = 2; cp = b & 31; }
        else if(b >= 0xe0 && b <= 0xef) { n = 3; cp = b & 15; }
        else if(b >= 0xf0 && b <= 0xf4) { n = 4; cp = b & 7; }
        else { ++i; continue; }
        if(i + n > len) break;
        bool valid = true;
        for(size_t k = 1; k < n; k++) {
            if((s[i+k] & 0xc0) != 0x80) { valid = false; break; }
            cp = (cp << 6) | (s[i+k] & 63);
        }
        if(n == 2 && cp < 0x80) valid = false;
        if(n == 3 && cp < 0x800) valid = false;
        if(n == 4 && cp < 0x10000) valid = false;
        if(cp >= 0xd800 && cp <= 0xdfff) valid = false;
        if(cp > 0x10ffff) valid = false;
        if(!valid) { ++i; continue; }
        if(cp == 0 || cp == '\r' || cp == '\n' || cp < 32 || cp == 127) { i += n; continue; }
        if(j + n > 64) break;
        memcpy(out + j, s + i, n);
        j += n; i += n;
    }
    out[j] = 0;
    /* ZeroSpades trims padding after stripping newlines. */
    size_t a = 0;
    while(out[a] == ' ' || out[a] == '\t') ++a;
    if(a) memmove(out, out + a, j - a + 1);
    j = strlen(out);
    while(j && (out[j-1] == ' ' || out[j-1] == '\t')) out[--j] = 0;
}

void teamplay_reset_map(void) {
    memset(teamplay_pings, 0, sizeof(teamplay_pings));
}
void teamplay_reset_connection(void) {
    negotiated = false;
    teamplay_features = 0;
    teamplay_north_x = 0.0f;
    teamplay_north_y = -1.0f;
    teamplay_reset_map();
}
void teamplay_set_negotiated(bool enabled) { negotiated = enabled; }
bool teamplay_negotiated(void) { return negotiated; }
bool teamplay_can_ping(void) { return negotiated && (teamplay_features & TEAMPLAY_FEATURE_PING); }
void teamplay_player_left(int id) {
    if(id >= 0 && id < TEAMPLAY_PLAYERS)
        teamplay_pings[id].active = false;
}
void teamplay_tick(float dt) {
    if(!tp_finite(dt) || dt < 0) return;
    for(int i = 0; i < TEAMPLAY_PLAYERS; i++) {
        TeamplayPing* p = &teamplay_pings[i];
        if(p->active) {
            p->age += dt;
            if(!p->endless && (p->remaining -= dt) <= 0) p->active = false;
        }
    }
}
static uint8_t surfaces(uint8_t sent) { return sent ? (sent & 7) : (TEAMPLAY_WORLD | TEAMPLAY_MAP); }
static bool valid_duration(float d) { return !tp_nan(d) && d >= 0; }
static bool valid_position(float x, float y, float z) {
    return tp_finite(x) && tp_finite(y) && tp_finite(z)
        && x >= 0 && x <= 512 && y >= 0 && y <= 512 && z >= -64 && z <= 64;
}

bool teamplay_receive(const uint8_t* data, size_t len, bool seeking, bool map_loading) {
    if(!data || !len || (!negotiated && !demo_is_playing())) return false;
    uint8_t sub = data[0];
    if(sub == 0) {
        if(len < 10) return false;
        float nx = lefloat(data + 2), ny = lefloat(data + 6);
        teamplay_features = data[1] & 7;
        double magnitude = hypot((double)nx, (double)ny);
        if(tp_finite(nx) && tp_finite(ny) && magnitude > 0) {
            teamplay_north_x = (float)(nx / magnitude);
            teamplay_north_y = (float)(ny / magnitude);
        } else { teamplay_north_x = 0; teamplay_north_y = -1; }
        return true;
    }
    if(sub == 1) {
        if(len < 23) return false; /* sub-ID + 22 fixed bytes */
        int id = data[1];
        float x = lefloat(data + 2), y = lefloat(data + 6), z = lefloat(data + 10);
        float duration = lefloat(data + 14);
        if(!valid_duration(duration) || (duration != 0 && !valid_position(x,y,z))) return false;
        if(map_loading || (seeking && duration != 0)) return true;
        TeamplayPing* p = &teamplay_pings[id];
        if(duration == 0) { p->active = false; return true; }
        p->active = true; p->endless = tp_inf(duration);
        p->x = x; p->y = y; p->z = z;
        p->remaining = duration; p->age = 0;
        p->surfaces = surfaces(data[18]);
        p->blue = data[19]; p->green = data[20]; p->red = data[21];
        teamplay_reason(p->reason, data + 23, len - 23); /* data[22]: reserved message ID */
        if(id != 255 && !(p->surfaces & TEAMPLAY_WORLD)) {
            char line[160];
            const char* who = players[id].connected ? players[id].name : "";
            if(*p->reason || *who) {
                snprintf(line, sizeof(line), "%s%s%s", who, *who && *p->reason ? ": " : "", p->reason);
                chat_add(0, 0xffffff, line);
            }
        }
        if(!demo_mute_effects()) sound_create(SOUND_LOCAL, &sound_beep1, 0, 0, 0);
        return true;
    }
    /* Player-mark/ESP packets are a separate Teamplay feature, not part of
       the pie menu or location pings. Ignore them without affecting pings. */
    return false;
}

bool teamplay_send_ping(float x, float y, float z, const char* reason) {
    if(!teamplay_can_ping() || !network_connected || !network_logged_in || demo_is_playing()
       || !valid_position(x, y, z) || !players[local_player_id].alive
       || players[local_player_id].team == TEAM_SPECTATOR) return false;
    uint8_t buf[23 + 64] = {1, 255};
    putfloat(buf + 2, x); putfloat(buf + 6, y); putfloat(buf + 10, z);
    /* duration, surfaces, BGR, message ID are zero: assigned by server. */
    char cleaned[65];
    if(!reason) reason = "";
    teamplay_reason(cleaned, (const uint8_t*)reason, strlen(reason));
    size_t n = strlen(cleaned);
    memcpy(buf + 23, cleaned, n);
    network_send(TEAMPLAY_PACKET_ID, buf, (int)(23 + n));
    return true;
}

void teamplay_record_initial(void (*record)(const uint8_t*, size_t)) {
    if(!record || !negotiated) return;
    uint8_t packet[11] = {0};
    packet[0] = TEAMPLAY_PACKET_ID;
    packet[1] = 0;
    packet[2] = teamplay_features;
    putfloat(packet + 3, teamplay_north_x);
    putfloat(packet + 7, teamplay_north_y);
    record(packet, 11);
}
