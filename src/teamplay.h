/*
 * Teamplay ping wire format (ZeroSpades extension 48, packet 112).
 * Protocol semantics adapted from ZeroSpades, GPL-3.0-or-later,
 * Copyright (c) 2026 Fran6nd and the ZeroSpades developers.
 * KyroSpades is GPL-3.0-or-later.  Wire positions are AoS x/y/z-down;
 * rendering converts to Kyro's x/y-up/z coordinate system.
 */
#ifndef TEAMPLAY_H
#define TEAMPLAY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TEAMPLAY_PACKET_ID 112
#define TEAMPLAY_EXTENSION_ID 48
#define TEAMPLAY_FEATURE_ESP 1
#define TEAMPLAY_FEATURE_PING 2
#define TEAMPLAY_WORLD 1
#define TEAMPLAY_MAP 2
#define TEAMPLAY_PLAYERS 256
#define TEAMPLAY_CLEAR_ON_RESPAWN 1
#define TEAMPLAY_SHOW_NAME 2

typedef struct TeamplayMark {
    bool active, endless, clear_on_respawn, show_name;
    float remaining;
    uint8_t surfaces, sent_surfaces, red, green, blue;
    char reason[65];
} TeamplayMark;
extern TeamplayMark teamplay_marks[TEAMPLAY_PLAYERS];
void teamplay_player_spawned(int id);
void teamplay_apply_pending(void);
bool teamplay_can_overlay(void);
void teamplay_overlay_hold(bool held);
float teamplay_overlay_opacity(void);

typedef struct TeamplayPing {
    bool active, endless;
    float x, y, z; /* wire coordinates */
    float remaining, age;
    uint8_t surfaces, red, green, blue;
    char reason[65];
} TeamplayPing;
extern TeamplayPing teamplay_pings[TEAMPLAY_PLAYERS];
extern uint8_t teamplay_features;
extern float teamplay_north_x, teamplay_north_y;
void teamplay_reset_connection(void);
void teamplay_reset_map(void);
void teamplay_player_left(int id);
void teamplay_tick(float dt);
void teamplay_set_negotiated(bool enabled);
bool teamplay_negotiated(void);
bool teamplay_can_ping(void);
/* Payload begins with subpacket byte; returns false for a malformed packet. */
bool teamplay_receive(const uint8_t* data, size_t len, bool seeking, bool map_loading);
/* Takes wire coordinates and a UTF-8 reason. No optimistic local marker. */
bool teamplay_send_ping(float x, float y, float z, const char* reason);
/* Sanitizes a bounded, untrusted string to the v1 64-byte UTF-8 reason. */
void teamplay_reason(char out[65], const uint8_t* bytes, size_t len);
/* Bootstrap a mid-game demo with ping permission (Config), not old pings. */
void teamplay_record_initial(void (*record)(const uint8_t*, size_t));
#endif
