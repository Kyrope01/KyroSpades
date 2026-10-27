/* aosprotocol Pie Menu v1 (extensions/pie-menu.md) as merged in
 * zerospades/aosprotocol 7449ddd: extension 0x31, packet 0x71, version 1,
 * server to client only.  This module parses, validates and stores a Menu;
 * pie_menu.c offers it, draws it and sends what a chosen slice says.
 * Copyright (c) 2026 Francois ND and the ZeroSpades developers;
 * KyroSpades is GPL-3.0-or-later.  See LICENSE. */
#ifndef PIE_MENU_EXT_H
#define PIE_MENU_EXT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PIE_MENU_EXT_ID 0x31
#define PIE_MENU_PACKET_ID 0x71
#define PIE_MENU_SLICES 6
/* No string has a length limit of its own; a client may cut what it draws.
   A Text is cut to the chat payload it is sent in. */
#define PIE_MENU_TEXT_MAX 255
#define PIE_MENU_LABEL_MAX 64
#define PIE_MENU_TOOLTIP_MAX 128
#define PIE_MENU_THEME_MAX 64
/* A CP437 Text decoded to UTF-8 needs at most three bytes per character. */
#define PIE_MENU_UTF8_MAX (PIE_MENU_TEXT_MAX * 3)

/* Slice flags. Bit 0 is the team/global bit of the base Chat Message; there is
   no enemy channel. Bits 1-7 are reserved and ignored. */
#define PIE_FLAG_GLOBAL 1

/* Contexts: where a pie is offered. What the aim meets first, a block, a
   player or nothing, decides which pies are offered. Bits 6-7 are reserved. */
#define PIE_CONTEXT_BLOCK 1u
#define PIE_CONTEXT_TEAMMATE 2u
#define PIE_CONTEXT_ENEMY 4u
#define PIE_CONTEXT_SPECTATOR 8u
#define PIE_CONTEXT_DEAD 16u
#define PIE_CONTEXT_SKY 32u /* the aim meets no block and no player */
#define PIE_CONTEXT_KNOWN 0x3fu

/* A slice whose Text and Label are both empty is blank: it draws nothing and
   cannot be chosen, whatever its Tooltip. */
typedef struct PieSlice {
    uint8_t flags;
    bool text_utf8; /* the Text arrived behind the 0xff prefix of UTF-8 Chat */
    /* Sent exactly as received: whatever a slice sends goes out as the client
       normally sends chat. */
    char text[PIE_MENU_TEXT_MAX + 1];
    /* Drawn only, so decoded from Code Page 437 to the UTF-8 the font takes. */
    char label[PIE_MENU_LABEL_MAX + 1];
    char tooltip[PIE_MENU_TOOLTIP_MAX + 1];
} PieSlice;

typedef struct Pie {
    uint8_t contexts;
    char theme[PIE_MENU_THEME_MAX + 1];
    PieSlice slice[PIE_MENU_SLICES];
} Pie;

/* What the menu was opened on, taken when it opened and never re-read. The
   world values are AoS wire coordinates: z down, 63 at water level. */
typedef struct PieAim {
    int player;   /* %p: the player the aim meets before any block, or -1 */
    bool has_block; /* %b: the first solid block the aim meets */
    int block_x, block_y, block_z;
    float ray_x, ray_y, ray_z; /* %r: the camera position */
    float dir_x, dir_y, dir_z; /* %r: the view direction, of length 1 */
    bool hit;                  /* %h: the aim met a block or a player */
    float hit_x, hit_y, hit_z; /* %h: the point the aim met */
} PieAim;

void pie_menu_ext_reset_connection(void);
void pie_menu_ext_set_negotiated(bool enabled);
bool pie_menu_ext_negotiated(void);
/* Parses a whole packet before applying any of it: a menu is applied whole or
   not at all, and a refused one leaves the previous menu standing. */
bool pie_menu_ext_receive(const uint8_t* data, size_t len);
/* True once any Menu has been applied, an empty one included: the server owns
   the menu from then on and the client's own does not come back. */
bool pie_menu_ext_owner(void);
int pie_menu_ext_count(void);
const Pie* pie_menu_ext_pie(int index);
/* Hold the applied Menu alive across a replacement: a client takes the menu
   itself when the menu opens, and none of it changes while it is held. */
void pie_menu_ext_freeze(void);
void pie_menu_ext_thaw(void);
/* Expands one Text into out. The result is never re-scanned, so a name that
   looks like a token is not one. */
void pie_menu_ext_expand(const char* text, const PieAim* aim, char* out, size_t out_size);
#endif
