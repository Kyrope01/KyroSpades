/* aosprotocol Pie Menu v1, as merged in zerospades/aosprotocol 7449ddd.
 * The server owns what the radial menu offers; this file only parses and
 * stores a Menu, and expands the substitutions of a slice's Text.
 * Copyright (c) 2026 Francois ND and the ZeroSpades developers.
 * KyroSpades is GPL-3.0-or-later.  See LICENSE. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "log.h"
#include "pie_menu_ext.h"

/* Code Page 437 for bytes 0x80-0xFF, as Unicode.  Theme, Label and Tooltip are
   decoded on the way in because they are only ever drawn and the font takes
   UTF-8.  A Text is not: it is sent as the client sends chat. */
static const unsigned short cp437[128] = {
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7, /* 80 */
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5, /* 88 */
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9, /* 90 */
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192, /* 98 */
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba, /* a0 */
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb, /* a8 */
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556, /* b0 */
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510, /* b8 */
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f, /* c0 */
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567, /* c8 */
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b, /* d0 */
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580, /* d8 */
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4, /* e0 */
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229, /* e8 */
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248, /* f0 */
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0  /* f8 */
};

static size_t utf8_encode(unsigned cp, char out[4]) {
    if(cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if(cp < 0x800) {
        out[0] = (char)(0xc0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3f));
        return 2;
    }
    if(cp < 0x10000) {
        out[0] = (char)(0xe0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3f));
        out[2] = (char)(0x80 | (cp & 0x3f));
        return 3;
    }
    out[0] = (char)(0xf0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3f));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3f));
    out[3] = (char)(0x80 | (cp & 0x3f));
    return 4;
}

static size_t utf8_length(unsigned char lead) {
    if(lead < 0x80) return 1;
    if((lead & 0xe0) == 0xc0) return 2;
    if((lead & 0xf0) == 0xe0) return 3;
    return 4;
}

/* Rejects overlong forms, surrogates, everything above U+10FFFF and any
   truncated or bad continuation, so a Menu whose 0xff string is not
   well-formed UTF-8 is refused rather than drawn as garbage. */
static bool utf8_well_formed(const uint8_t* s, size_t n) {
    static const unsigned lowest[4] = {0, 0x80, 0x800, 0x10000};
    size_t i = 0;
    while(i < n) {
        uint8_t b = s[i];
        if(b < 0x80) {
            i++;
            continue;
        }
        unsigned cp;
        size_t extra;
        if(b >= 0xc2 && b <= 0xdf) { cp = b & 0x1f; extra = 1; }
        else if(b >= 0xe0 && b <= 0xef) { cp = b & 0x0f; extra = 2; }
        else if(b >= 0xf0 && b <= 0xf4) { cp = b & 0x07; extra = 3; }
        else return false; /* 0x80-0xc1 and 0xf5-0xff never start a sequence */
        if(i + extra >= n) return false;
        for(size_t k = 1; k <= extra; k++) {
            if((s[i + k] & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (s[i + k] & 0x3f);
        }
        if(cp < lowest[extra] || cp > 0x10ffff) return false;
        if(cp >= 0xd800 && cp <= 0xdfff) return false;
        i += extra + 1;
    }
    return true;
}

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
        if(!utf8_well_formed(*bytes, *count)) return false;
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
            len = utf8 ? utf8_length(s[i]) : 1;
            if(i + len > n) break;
            src = s + i;
        } else {
            len = utf8_encode(s[i] < 0x80 ? s[i] : cp437[s[i] - 0x80], enc);
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
    size_t want = utf8_length((unsigned char)s[i - 1]);
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
