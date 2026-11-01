/* aosprotocol Pie Menu v1, as merged in zerospades/aosprotocol 7449ddd.
 * The server owns what the radial menu offers; this file only parses and
 * stores a Menu, and expands the substitutions of a slice's Text.
 * Copyright (c) 2026 Francois ND and the ZeroSpades developers.
 * KyroSpades is GPL-3.0-or-later.  See LICENSE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "charset.h"
#include "log.h"
#include "pie_menu_ext.h"

/* A string of a Menu: NUL terminated, and UTF-8 behind the 0xff prefix of
   UTF-8 Chat rather than Code Page 437. */
static bool read_string(const uint8_t* data, size_t len, size_t* pos, const uint8_t** bytes, size_t* count, bool* utf8) {
    size_t start = *pos;
    while(*pos < len && data[*pos] != 0) (*pos)++;
    if(*pos >= len) return false; /* no NUL: the string runs past the end */
    *bytes = data + start;
    *count = *pos - start;
    (*pos)++; /* the NUL itself is not part of the string */
    *utf8 = *count > 0 && data[start] == 0xff;
    if(*utf8) {
        (*bytes)++;
        (*count)--;
        if(!charset_utf8_well_formed(*bytes, *count)) return false;
    }
    return true;
}

/* Copy one parsed string into dst. Cutting never splits a codepoint, and a
   Code Page 437 string is decoded when it is only ever drawn. */
static void store(char* dst, size_t cap, const uint8_t* s, size_t n, bool utf8, bool decode) {
    size_t o = 0, i = 0;
    while(i < n) {
        char enc[4];
        const uint8_t* src;
        size_t len;
        if(utf8 || !decode) {
            len = utf8 ? charset_utf8_length(s[i]) : 1;
            if(i + len > n) break;
            src = s + i;
        } else {
            len = charset_cp437_glyph(s[i], enc);
            src = (const uint8_t*)enc;
        }
        if(o + len > cap - 1) break;
        memcpy(dst + o, src, len);
        o += len;
        i += utf8 ? len : 1;
    }
    dst[o] = 0;
}

/* The applied Menu, and the one a Menu replaced while a menu was open: a
   client takes the menu itself when the menu opens, so an open menu keeps
   reading the pies it was opened on. */
static PieMenu* menu;
static PieMenu* retiring;
static int menu_count;
static bool server_owned; /* a Menu has been applied, an empty one included */
static bool negotiated, frozen;

void pie_menu_ext_reset_connection(void) {
    negotiated = false;
    server_owned = false;
    menu_count = 0;
    /* The menu belongs to the connection, not to the world and not to a
       player: only a new connection clears it. */
    if(frozen) {
        free(retiring);
        retiring = menu;
    } else {
        free(menu);
        free(retiring);
        retiring = NULL;
    }
    menu = NULL;
}
void pie_menu_ext_set_negotiated(bool enabled) {
    pie_menu_ext_reset_connection();
    negotiated = enabled;
}
bool pie_menu_ext_negotiated(void) { return negotiated; }
bool pie_menu_ext_owner(void) { return server_owned; }
int pie_menu_ext_count(void) { return menu_count; }
const PieMenu* pie_menu_ext_pie(int index) { return index >= 0 && index < menu_count ? menu + index : NULL; }
void pie_menu_ext_freeze(void) { frozen = true; }
void pie_menu_ext_thaw(void) {
    frozen = false;
    free(retiring);
    retiring = NULL;
}

bool pie_menu_ext_receive(const uint8_t* data, size_t len) {
    if(!negotiated || !data || len < 2) return false;
    if(data[0] != 0) { /* Sub ID 0 is the only one; a newer one is not an error */
        log_debug("Ignoring unknown Pie Menu sub-packet %u", data[0]);
        return true;
    }
    unsigned count = data[1];
    size_t pos = 2;
    PieMenu* fresh = count ? calloc(count, sizeof *fresh) : NULL;
    if(count && !fresh) {
        log_warn("Pie Menu refused: no memory for %u pies", count);
        return false;
    }
    for(unsigned i = 0; i < count; i++) {
        PieMenu* pie = &fresh[i];
        const uint8_t* bytes;
        size_t n;
        bool utf8;
        if(pos + 2 > len) {
            log_warn("Pie Menu refused: pie %u runs past the end of the packet", i);
            free(fresh);
            return false;
        }
        /* Bits 6-7 of Contexts are reserved; unknown bits are ignored, and so
           is the reserved Theme Message ID that follows. */
        pie->contexts = (uint8_t)(data[pos] & PIE_CONTEXT_KNOWN);
        pos += 2;
        if(!read_string(data, len, &pos, &bytes, &n, &utf8)) {
            log_warn("Pie Menu refused: the theme of pie %u is not a string", i);
            free(fresh);
            return false;
        }
        store(pie->theme, sizeof pie->theme, bytes, n, utf8, true);
        for(int s = 0; s < PIE_MENU_SLICES; s++) {
            PieSlice* slice = &pie->slice[s];
            if(pos + 2 > len) {
                log_warn("Pie Menu refused: slice %d of pie %u runs past the end of the packet", s, i);
                free(fresh);
                return false;
            }
            /* The reserved Message ID is ignored. Bit 0 of Flags is GLOBAL;
               the rest are reserved. */
            slice->flags = (uint8_t)(data[pos + 1] & PIE_FLAG_GLOBAL);
            pos += 2;
            if(!read_string(data, len, &pos, &bytes, &n, &utf8)) {
                log_warn("Pie Menu refused: the text of slice %d of pie %u is not a string", s, i);
                free(fresh);
                return false;
            }
            /* A Text is sent as the client sends chat, so it is kept exactly
               as the server encoded it; Label and Tooltip are drawn only. */
            const uint8_t* text_bytes = bytes;
            size_t text_n = n;
            slice->text_utf8 = utf8;
            store(slice->text, sizeof slice->text, bytes, n, utf8, false);
            if(!read_string(data, len, &pos, &bytes, &n, &utf8)) {
                log_warn("Pie Menu refused: the label of slice %d of pie %u is not a string", s, i);
                free(fresh);
                return false;
            }
            store(slice->label, sizeof slice->label, bytes, n, utf8, true);
            /* An empty Label draws the Text, and the Text is kept as the server
               encoded it, so the drawn copy is decoded here -- a Code Page 437
               Text would otherwise reach the font raw and come out as
               mojibake. Cutting it to a Label's width is what a client may do
               with anything it draws. */
            if(!slice->label[0])
                store(slice->label, sizeof slice->label, text_bytes, text_n, slice->text_utf8, true);
            if(!read_string(data, len, &pos, &bytes, &n, &utf8)) {
                log_warn("Pie Menu refused: the tooltip of slice %d of pie %u is not a string", s, i);
                free(fresh);
                return false;
            }
            store(slice->tooltip, sizeof slice->tooltip, bytes, n, utf8, true);
        }
    }
    if(pos != len) {
        log_warn("Pie Menu refused: %u bytes remain after the last pie", (unsigned)(len - pos));
        free(fresh);
        return false;
    }
    /* Parsed whole, so from here it is applied whole. */
    if(frozen) {
        free(retiring);
        retiring = menu;
    } else {
        free(menu);
    }
    menu = fresh;
    menu_count = (int)count;
    server_owned = true;
    log_info("Pie Menu: the server offers %u pies", count);
    return true;
}

/* Cut a truncated tail back to the last whole codepoint. */
static size_t trim_incomplete(char* s, size_t n) {
    size_t i = n;
    while(i > 0 && ((unsigned char)s[i - 1] & 0xc0) == 0x80) i--;
    if(!i) return n;
    size_t want = charset_utf8_length((unsigned char)s[i - 1]);
    return i - 1 + want > n ? i - 1 : n;
}

void pie_menu_ext_expand(const char* text, const PieAim* aim, char* out, size_t out_size) {
    if(!out || !out_size) return;
    out[0] = 0;
    if(!text) return;
    static const PieAim none = {.player = -1};
    if(!aim) aim = &none;
    size_t o = 0;
    bool cut = false;
    /* One pass: the result is never re-scanned, so a name that looks like a
       token is not one. */
    for(const char* p = text; *p;) {
        char piece[96];
        size_t n;
        if(*p != '%') {
            piece[0] = *p;
            n = 1;
            p++;
        } else if(p[1] == '%') {
            piece[0] = '%';
            n = 1;
            p += 2;
        } else if(p[1] == 'p') {
            /* An empty token leaves its argument missing. */
            n = aim->player >= 0 ? (size_t)snprintf(piece, sizeof piece, "#%d", aim->player) : 0;
            p += 2;
        } else if(p[1] == 'b') {
            n = aim->has_block ? (size_t)snprintf(piece, sizeof piece, "%d %d %d", aim->block_x, aim->block_y,
                                                                          aim->block_z)
                               : 0;
            p += 2;
        } else if(p[1] == 'h') {
            /* The point the aim met, where it first met a block or a player. */
            n = aim->hit ? (size_t)snprintf(piece, sizeof piece, "%.2f %.2f %.2f", aim->hit_x, aim->hit_y, aim->hit_z)
                         : 0;
            p += 2;
        } else if(p[1] == 'r') {
            /* Decimals keep '.' whatever the locale: this client never calls
               setlocale, so snprintf is in the "C" locale. */
            n = (size_t)snprintf(piece, sizeof piece, "%.2f %.2f %.2f %.4f %.4f %.4f", aim->ray_x, aim->ray_y,
                                 aim->ray_z, aim->dir_x, aim->dir_y, aim->dir_z);
            p += 2;
        } else {
            /* A '%' followed by anything else, or by nothing, is reserved and
               version 1 leaves it as it is. */
            piece[0] = '%';
            n = 1;
            p++;
        }
        if(n > sizeof piece - 1) n = sizeof piece - 1;
        if(o + n > out_size - 1) {
            n = out_size - 1 - o;
            cut = true;
        }
        memcpy(out + o, piece, n);
        o += n;
        out[o] = 0;
        if(cut) break;
    }
    if(cut) {
        o = trim_incomplete(out, o);
        out[o] = 0;
    }
}
