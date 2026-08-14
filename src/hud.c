
#include "gles_immediate_stubs.h"
/*
        Copyright (c) 2017-2020 ByteBit

        This file is part of KyroSpades.

        KyroSpades is free software: you can redistribute it and/or modify
        it under the terms of the GNU General Public License as published by
        the Free Software Foundation, either version 3 of the License, or
        (at your option) any later version.

        KyroSpades is distributed in the hope that it will be useful,
        but WITHOUT ANY WARRANTY; without even the implied warranty of
        MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
        GNU General Public License for more details.

        You should have received a copy of the GNU General Public License
        along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>
#include <limits.h>
#include <ctype.h>

#include "lodepng/lodepng.h"
#include "main.h"
#include "file.h"
#include "common.h"
#include "glx.h"
#include "list.h"
#include "matrix.h"
#include "texture.h"
#include "player.h"
extern float tactical_sprint_amount;
#include "hud.h"
#include "recorder.h"
#include "http.h"
#include "parson.h"
#include "config.h"
#include "network.h"
#include "demo.h"
#include "rpc.h"
#include "map.h"
#include "player.h"
#include "camera.h"
#include "cameracontroller.h"
#include "ping.h"
#include "chunk.h"
#include "utils.h"
#include "weapon.h"
#include "tracer.h"
#include "font.h"
#include "sound.h"
#include "chatlog.h"
#include "particle.h"
#include "model.h"
#include "skins.h"
#include "damagenumbers.h"

struct hud* hud_active;
struct window_instance* hud_window;

int player_stats_blocks_placed = 0;
int player_stats_kills = 0;
int player_stats_headshots = 0;
int player_stats_deaths = 0;
float player_stats_distance = 0.0F;
int player_stats_jumps = 0;

static float player_stats_last_x = 0.0F;
static float player_stats_last_y = 0.0F;
static float player_stats_last_z = 0.0F;
static float particle_stats_last_time = 0.0F;
static int particle_stats_last_total = 0;
static float particle_stats_created_per_second = 0.0F;
static double spec_color_palette_time = 0.0;

void player_stats_reset() {
        player_stats_blocks_placed = 0;
        player_stats_kills = 0;
        player_stats_headshots = 0;
        player_stats_deaths = 0;
        player_stats_distance = 0.0F;
        player_stats_jumps = 0;
}

#define LIGHTEN(c) (255.F * (settings.lighten_colors / 255.F) + c * (1.F - settings.lighten_colors / 255.F))

static int is_inside_centered(double mx, double my, int x, int y, int w, int h) {
        return mx >= x - w / 2 && mx < x + w / 2 && my >= y - h / 2 && my < y + h / 2;
}

static int is_inside(double mx, double my, int x, int y, int w, int h) {
        return mx >= x && mx < x + w && my >= y && my < y + h;
}

/* -- Block-colour palette geometry --------------------------------------
   The 8x8 swatch grid is 75% of its former size and centered along the bottom,
   keeping it clear of the ammo display on the right. These helpers drive the
   renderer, touch hit-test and aim-zone exclusion from the same geometry.
   Coordinates are HUD/GL space: origin bottom-left, y UP. Grid row gy=0 is the
   TOP row, matching texture_block_color(). NOTE: window.c mirrors these
   fractions -- keep both in sync. */
#define PALETTE_CELLS 8
static inline float palette_cell(void)   { return settings.window_height * 0.024F; }
static inline float palette_size(void)   { return palette_cell() * PALETTE_CELLS; }
static inline float palette_left(void)   { return (settings.window_width - palette_size()) * 0.5F; }
static inline float palette_bottom(void) { return settings.window_height * 0.045F; }   /* GL y, clears home indicator */
static inline float palette_top(void)    { return palette_bottom() + palette_size(); } /* GL y of grid top */

/* Map a touch point (screen coords, y DOWN) to a grid cell, clamping to
   [0,7] so a drag that strays slightly off-grid still scrubs the edge row. */
static void palette_cell_clamp(float sx, float sy, int* gx, int* gy) {
        float gl_y = settings.window_height - sy;
        int cx = (int)((sx - palette_left()) / palette_cell());
        int cy = (int)((palette_top() - gl_y) / palette_cell()); /* gy=0 at top */
        if(cx < 0) cx = 0;
        if(cx > 7) cx = 7;
        if(cy < 0) cy = 0;
        if(cy > 7) cy = 7;
        *gx = cx;
        *gy = cy;
}

/* Strict membership test (no clamp): did a touch START inside the palette? */
static int palette_contains(float sx, float sy) {
        float gl_y = settings.window_height - sy;
        return sx >= palette_left() && sx < palette_left() + palette_size()
                && gl_y >= palette_bottom() && gl_y < palette_top();
}

/* Draw only an outline BEHIND the selected swatch. Each palette swatch uses
   6 of its source texture's 8 pixels, so its visible size is 75% of a cell and
   is anchored at the cell's top-left. The border follows that exact geometry;
   the palette texture is drawn afterward and covers the center, leaving a
   correctly centered flashing outline rather than a black/white overlay cube. */
static void palette_draw_selection_border(unsigned int selected_color) {
        float cell = palette_cell();
        float swatch = cell * 0.75F;
        float border = cell * 0.08F;
        for(int y = 0; y < 8; y++) {
                for(int x = 0; x < 8; x++) {
                        if(texture_block_color(x, y) == selected_color) {
                                unsigned char flash = (((int)(window_time() * 4)) & 1) * 0xFF;
                                glColor3ub(flash, flash, flash);
                                texture_draw_empty(palette_left() + x * cell - border,
                                                   palette_top() - y * cell + border,
                                                   swatch + border * 2.0F,
                                                   swatch + border * 2.0F);
                                return;
                        }
                }
        }
}

static void format_comma(char* buffer, int value) {
        char tmp[32];
        sprintf(tmp, "%d", value);
        int len = strlen(tmp);
        int j = 0;
        for(int i = 0; i < len; i++) {
                if(i > 0 && (len - i) % 3 == 0) {
                        buffer[j++] = ',';
                }
                buffer[j++] = tmp[i];
        }
        buffer[j] = '\0';
}

void hud_init() {
        hud_serverlist.ctx = malloc(sizeof(mu_Context));
        hud_settings.ctx = malloc(sizeof(mu_Context));
        hud_controls.ctx = malloc(sizeof(mu_Context));
        hud_chatlog.ctx = malloc(sizeof(mu_Context));
        hud_demolist.ctx = malloc(sizeof(mu_Context));
        hud_skins.ctx = malloc(sizeof(mu_Context));
        hud_macros.ctx = malloc(sizeof(mu_Context));
        hud_recording.ctx = malloc(sizeof(mu_Context));
        hud_replay.ctx = malloc(sizeof(mu_Context));

        hud_change(&hud_serverlist);
}

void hud_accent_rgb(int* r, int* g, int* b) {
        if(settings.ui_rgb) {
                /* Three phase-shifted cosine waves produce a continuous rainbow
                   with no hard hue boundaries. Speed is radians per second, so
                   the default 1.0 completes a cycle in about 6.28 seconds. */
                float phase = (float)window_time() * settings.ui_rgb_speed;
                *r = (int)(127.5F * (cosf(phase) + 1.0F));
                *g = (int)(127.5F * (cosf(phase - 2.0943951F) + 1.0F));
                *b = (int)(127.5F * (cosf(phase - 4.1887902F) + 1.0F));
        } else {
                *r = settings.ui_accent_r;
                *g = settings.ui_accent_g;
                *b = settings.ui_accent_b;
        }
}

int hud_accent_red(void) {
        int r, g, b;
        hud_accent_rgb(&r, &g, &b);
        return r;
}

int hud_accent_green(void) {
        int r, g, b;
        hud_accent_rgb(&r, &g, &b);
        return g;
}

int hud_accent_blue(void) {
        int r, g, b;
        hud_accent_rgb(&r, &g, &b);
        return b;
}

inline int hud_accent_color() {
        int r, g, b;
        hud_accent_rgb(&r, &g, &b);
        return rgb(r, g, b);
}

float hud_ui_scale(void) {
#if defined(OS_IOS)
        /* Every iOS device is a high-DPI Retina display, and with HIGHDPI the
           drawable is full native resolution, so the microui menus (serverlist,
           settings, demos) render tiny at 1.0. Double them. */
        return 2.0F;
#elif defined(__ANDROID__)
        /* Enlarge touch targets on physically small, high-resolution screens.
           Android-only for now: the >=1920 heuristic would otherwise also double
           the UI on ordinary 1080p desktop monitors. Proper DPI-based scaling for
           desktop can be added later. Recompute every call (no caching): the
           surface rotates at runtime and the real resolution isn't known yet when
           hud_init runs, so a cached value would lock in the wrong scale. */
        int big = settings.window_width > settings.window_height
                ? settings.window_width : settings.window_height;
        return (big >= 1920) ? 2.0F : 1.0F;
#else
        return 1.0F;
#endif
}

static int mu_text_height(mu_Font font) {
        return (int)(16.F * hud_ui_scale());
}

static int mu_text_width(mu_Font font, const char* text, int len) {
        if(len <= 0) {
                return ceil(font_length(mu_text_height(font), (char*)text));
        } else {
                char tmp[len + 1];
                memcpy(tmp, text, len);
                tmp[len] = 0;
                return ceil(font_length(mu_text_height(font), tmp));
        }
}

static void mu_text_color(mu_Context* ctx, int red, int green, int blue) {
        ctx->style->colors[MU_COLOR_TEXT] = mu_color(red, green, blue, 255);
}

static void mu_text_color_default(mu_Context* ctx) {
        ctx->style->colors[MU_COLOR_TEXT] = mu_color(230, 230, 230, 255);
}

static mu_Color mu_accent_color(float m, int a) {
        int r, g, b;
        hud_accent_rgb(&r, &g, &b);
        return mu_color(
                max(0, min(255, r * m)),
                max(0, min(255, g * m)),
                max(0, min(255, b * m)),
                a
        );
}

static void mu_text_accent_color(mu_Context* ctx, float m) {
        mu_Color color = mu_accent_color(m, 255);
        mu_text_color(ctx, color.r, color.g, color.b);
}

/* --- Focus-driven soft-keyboard (IME) management -------------------------
   Calling window_textinput(1) unconditionally on screen init pops the
   Android soft keyboard whenever a menu merely opens, and starting/stopping
   the IME on every hud transition is expensive and crash-prone. Instead,
   textboxes report whether they hold microui focus each frame, and the IME
   is toggled only on actual focus transitions (i.e. when the user taps the
   field). The in-game chat keeps managing window_textinput() itself;
   hud_ime_update() backs off whenever the active hud has no microui
   context. */
static int hud_ime_focus_frame = 0;

static int hud_textbox(mu_Context* ctx, char* buf, int bufsz, int opt) {
        /* mu_textbox_ex derives its id from the buffer pointer value; computing
           it the same way here lets us know if this textbox holds focus */
        mu_Id id = mu_get_id(ctx, &buf, sizeof(buf));
        int res = mu_textbox_ex(ctx, buf, bufsz, opt);
        if(ctx->focus == id)
                hud_ime_focus_frame = 1;
        return res;
}

void hud_ime_update() {
        static int ime_active = -1;
        if(!hud_active->ctx) {
                ime_active = -1; /* ingame: chat code owns window_textinput() */
                return;
        }
        if(hud_ime_focus_frame != ime_active) {
                window_textinput(hud_ime_focus_frame);
                ime_active = hud_ime_focus_frame;
        }
        hud_ime_focus_frame = 0;
}

static void hud_refresh_accent_style(mu_Context* ctx) {
        if(!ctx) return;
        ctx->style->colors[MU_COLOR_BASE] = mu_accent_color(0.3F, 255);
        ctx->style->colors[MU_COLOR_BORDER] = mu_accent_color(0.8F, 255);
        ctx->style->colors[MU_COLOR_BUTTON] = mu_accent_color(0.3F, 255);
        ctx->style->colors[MU_COLOR_BUTTONHOVER] = mu_accent_color(0.8F, 255);
        ctx->style->colors[MU_COLOR_BUTTONFOCUS] = mu_accent_color(1.0F, 255);
        ctx->style->colors[MU_COLOR_BASEFOCUS] = mu_accent_color(0.5F, 255);
        ctx->style->colors[MU_COLOR_BASEHOVER] = mu_accent_color(0.5F, 255);
        ctx->style->colors[MU_COLOR_PANELBG] = mu_accent_color(0.1F, 192);
        ctx->style->colors[MU_COLOR_WINDOWBG] = mu_accent_color(0.1F, 192);
        ctx->style->colors[MU_COLOR_TITLEBG] = mu_accent_color(0.8F, 255);
        ctx->style->colors[MU_COLOR_SCROLLTHUMB] = mu_accent_color(0.5F, 255);
        ctx->style->colors[MU_COLOR_SCROLLBASE] = mu_accent_color(0.05F, 255);
}

void hud_change(struct hud* new) {
        config_key_reset_togglestates();
        hud_active = new;

        if(hud_active->ctx) {
                mu_init(hud_active->ctx);
                hud_active->ctx->text_width = mu_text_width;
                hud_active->ctx->text_height = mu_text_height;
                hud_active->ctx->get_clipboard = window_clipboard;
                hud_active->ctx->set_clipboard = window_setclipboard;
                hud_refresh_accent_style(hud_active->ctx);
        }

        if(hud_active->init)
                hud_active->init();
}

/*          HUD_INGAME START           */

static float hud_ingame_touch_x = 0.0F;
static float hud_ingame_touch_y = 0.0F;
static void* demo_scrub_finger = NULL;
static float demo_scrub_target = 0.0f;

int screen_current = SCREEN_NONE;
int show_exit = 0;
int show_update_popup = 0;
static char latest_ver[32];

/* Multi-line chat input: cursor offset into chat[0][0] in bytes, plus
   wrapping helpers. Buffer remains a single string (newlines never get
   stored), but the field can wrap onto up to CHAT_INPUT_MAX_ROWS rows. */
#define CHAT_INPUT_MAX_ROWS 3
#define CHAT_INPUT_ROW_H 16.0F
int chat_cursor = 0;
int chat_input_rows = 1;
/* Selection: -1 = none. When set and != chat_cursor, range
   [min(anchor, cursor), max(anchor, cursor)) is the active selection. */
int chat_sel_anchor = -1;
static int chat_drag_active = 0;

static void chat_cursor_clamp(void) {
        int len = (int)strlen(chat[0][0]);
        if(chat_cursor < 0) chat_cursor = 0;
        if(chat_cursor > len) chat_cursor = len;
        if(chat_sel_anchor > len) chat_sel_anchor = len;
}

static int chat_sel_active(void) {
        return chat_sel_anchor >= 0 && chat_sel_anchor != chat_cursor;
}

static void chat_sel_clear(void) {
        chat_sel_anchor = -1;
}

static void chat_sel_range(int* lo, int* hi) {
        int a = chat_sel_anchor, b = chat_cursor;
        if(a < 0) { *lo = *hi = b; return; }
        if(a <= b) { *lo = a; *hi = b; } else { *lo = b; *hi = a; }
}

static int chat_sel_delete(void) {
        if(!chat_sel_active()) return 0;
        int lo, hi;
        chat_sel_range(&lo, &hi);
        int len = (int)strlen(chat[0][0]);
        memmove(chat[0][0] + lo, chat[0][0] + hi, len - hi + 1);
        chat_cursor = lo;
        chat_sel_clear();
        return 1;
}

static void chat_sel_copy_to_clipboard(void) {
        if(!chat_sel_active()) return;
        int lo, hi;
        chat_sel_range(&lo, &hi);
        char buf[260];
        int n = hi - lo;
        if(n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
        memcpy(buf, chat[0][0] + lo, n);
        buf[n] = 0;
        window_setclipboard(buf);
}

/* Move cursor; with shift held, extend selection (seeding anchor at the
   pre-move position if there was none). Without shift, drop selection. */
static void chat_cursor_move_with_shift(int new_cursor, int shift_held) {
        if(shift_held) {
                if(chat_sel_anchor < 0) chat_sel_anchor = chat_cursor;
        } else {
                chat_sel_clear();
        }
        chat_cursor = new_cursor;
}

static int chat_wrap(float available_w, int* row_starts, int* row_lens, int max_rows) {
        char* s = chat[0][0];
        int len = (int)strlen(s);
        if(len == 0) { row_starts[0] = 0; row_lens[0] = 0; return 1; }
        int rows = 0;
        int i = 0;
        char tmp[260];
        while(rows < max_rows) {
                int start = i;
                int last_break = -1;
                int hard_break = -1;
                int end = i;
                while(end < len) {
                        if(s[end] == '\n') { hard_break = end; break; }
                        int take = end - start + 1;
                        if(take >= (int)sizeof(tmp)) break;
                        memcpy(tmp, s + start, take);
                        tmp[take] = 0;
                        if(font_length(CHAT_INPUT_ROW_H, tmp) > available_w) break;
                        if(s[end] == ' ') last_break = end;
                        end++;
                }
                if(hard_break >= 0) {
                        row_starts[rows] = start;
                        row_lens[rows] = hard_break - start;
                        rows++;
                        i = hard_break + 1;
                        if(i > len) break;
                        continue;
                }
                if(end >= len) {
                        row_starts[rows] = start;
                        row_lens[rows] = end - start;
                        rows++;
                        break;
                }
                int cut = (last_break > start) ? last_break + 1 : end;
                if(cut <= start) cut = start + 1;
                row_starts[rows] = start;
                row_lens[rows] = cut - start;
                rows++;
                i = cut;
        }
        return rows;
}

static void chat_cursor_to_rowcol(const int* row_starts, const int* row_lens, int rows,
                                                                  int* out_row, int* out_col) {
        for(int r = 0; r < rows; r++) {
                int s = row_starts[r];
                int e = s + row_lens[r];
                if((chat_cursor >= s && chat_cursor < e) || (r == rows - 1 && chat_cursor >= s)) {
                        *out_row = r; *out_col = chat_cursor - s; return;
                }
        }
        *out_row = rows - 1; *out_col = row_lens[rows - 1];
}

/* Map a screen-pixel mouse position (top-left origin) to a byte offset
   in chat[0][0]. Returns -1 if outside the chat input area. */
static int chat_input_offset_at(double sx_pixel, double sy_pixel) {
        float sx = (float)sx_pixel;
        float sy = (float)settings.window_height - (float)sy_pixel;
        float avail_w = (float)settings.window_width - 11.0F - 16.0F;
        int row_starts[CHAT_INPUT_MAX_ROWS], row_lens[CHAT_INPUT_MAX_ROWS];
        int rows = chat_wrap(avail_w, row_starts, row_lens, CHAT_INPUT_MAX_ROWS);

        float top_baseline = 69.0F + (rows - 1) * CHAT_INPUT_ROW_H;
        if(sy < 69.0F - CHAT_INPUT_ROW_H - 4.0F || sy > top_baseline + 4.0F) return -1;
        if(sx < 3.0F || sx > 11.0F + avail_w + 5.0F) return -1;

        int picked = 0;
        for(int r = 0; r < rows; r++) {
                float baseline = 69.0F + (rows - 1 - r) * CHAT_INPUT_ROW_H;
                if(sy <= baseline + 4.0F) picked = r;
        }

        int n = row_lens[picked];
        char tmp[260];
        if(n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
        memcpy(tmp, chat[0][0] + row_starts[picked], n);
        tmp[n] = 0;

        float rel_x = sx - 11.0F;
        if(rel_x <= 0.0F) return row_starts[picked];

        int best = 0;
        float best_diff = 1e9F;
        char acc[260];
        for(int i = 0; i <= n; i++) {
                memcpy(acc, tmp, i);
                acc[i] = 0;
                float w = font_length(CHAT_INPUT_ROW_H, acc);
                float d = fabsf(w - rel_x);
                if(d < best_diff) { best_diff = d; best = i; }
        }
        int off = row_starts[picked] + best;
        int len = (int)strlen(chat[0][0]);
        if(off > len) off = len;
        while(off > 0 && ((unsigned char)chat[0][0][off] & 0xC0) == 0x80) off--;
        return off;
}

static int mouse_seed_pending = 1;

static void hud_ingame_init() {
        window_textinput(0);
        chat_input_mode = CHAT_NO_INPUT;
        window_mousemode(WINDOW_CURSOR_DISABLED);
        mouse_seed_pending = 1;
}

struct player_table {
        unsigned char id;
        unsigned int score;
};

static int playertable_sort(const void* a, const void* b) {
        struct player_table* aa = (struct player_table*)a;
        struct player_table* bb = (struct player_table*)b;
        return bb->score - aa->score;
}

static void hud_ingame_render3D() {
        glDepthRange(0.0F, 0.05F);

        matrix_identity(matrix_projection);
        matrix_perspective(matrix_projection, CAMERA_DEFAULT_FOV,
                                           ((float)settings.window_width) / ((float)settings.window_height), 0.1F, 128.0F);
        matrix_identity(matrix_view);
        matrix_upload_p();

        if(!network_map_transfer) {
                if(camera_mode == CAMERAMODE_FPS && players[local_player_id].items_show) {
                        players[local_player_id].input.buttons.rmb = 0;

                        /* Compact tool-switch overlay: keep the same four-item
                           presentation and selected-item emphasis, but render the
                           entire row at half scale and half spacing. Its baseline
                           is placed close to the lower edge of the view instead of
                           floating below screen centre. */
                        const float tool_overlay_scale = 0.5F;
                        const float tool_overlay_x = -1.125F;
                        const float tool_overlay_y = -3.15F;
                        const float tool_overlay_spacing = 0.75F;

                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, tool_overlay_x, tool_overlay_y, -6.0F);
                        matrix_rotate(matrix_model, window_time() * 57.4F, 0.0F, 1.0F, 0.0F);
                        matrix_translate(matrix_model, (model_spade.xpiv - model_spade.xsiz / 2) * 0.05F,
                                                         (model_spade.zpiv - model_spade.zsiz / 2) * 0.05F,
                                                         (model_spade.ypiv - model_spade.ysiz / 2) * 0.05F);
                        matrix_scale3(matrix_model, tool_overlay_scale);
                        if(players[local_player_id].held_item == TOOL_SPADE) {
                                matrix_scale(matrix_model, 1.5F, 1.5F, 1.5F);
                        }
                        matrix_upload();
                        kv6_render(&model_spade, players[local_player_id].team);

                        if(local_player_blocks > 0) {
                                matrix_identity(matrix_model);
                                matrix_translate(matrix_model, tool_overlay_x, tool_overlay_y, -6.0F);
                                matrix_translate(matrix_model, tool_overlay_spacing, 0.0F, 0.0F);
                                matrix_rotate(matrix_model, window_time() * 57.4F, 0.0F, 1.0F, 0.0F);
                                matrix_translate(matrix_model, (model_block.xpiv - model_block.xsiz / 2) * 0.05F,
                                                                 (model_block.zpiv - model_block.zsiz / 2) * 0.05F,
                                                                 (model_block.ypiv - model_block.ysiz / 2) * 0.05F);
                                matrix_scale3(matrix_model, tool_overlay_scale);
                                if(players[local_player_id].held_item == TOOL_BLOCK) {
                                        matrix_scale(matrix_model, 1.5F, 1.5F, 1.5F);
                                }
                                model_block.red = players[local_player_id].block.red / 255.0F;
                                model_block.green = players[local_player_id].block.green / 255.0F;
                                model_block.blue = players[local_player_id].block.blue / 255.0F;
                                matrix_upload();
                                kv6_render(&model_block, players[local_player_id].team);
                        }

                        if(local_player_ammo + local_player_ammo_reserved > 0) {
                                struct kv6_t* gun;
                                switch(players[local_player_id].weapon) {
                                        default:
                                        case WEAPON_RIFLE: gun = &model_semi; break;
                                        case WEAPON_SMG: gun = &model_smg; break;
                                        case WEAPON_SHOTGUN: gun = &model_shotgun; break;
                                }
                                matrix_identity(matrix_model);
                                matrix_translate(matrix_model, tool_overlay_x, tool_overlay_y, -6.0F);
                                matrix_translate(matrix_model, tool_overlay_spacing * 2.0F, 0.0F, 0.0F);
                                matrix_rotate(matrix_model, window_time() * 57.4F, 0.0F, 1.0F, 0.0F);
                                matrix_translate(matrix_model, (gun->xpiv - gun->xsiz / 2) * 0.05F, (gun->zpiv - gun->zsiz / 2) * 0.05F,
                                                                 (gun->ypiv - gun->ysiz / 2) * 0.05F);
                                matrix_scale3(matrix_model, tool_overlay_scale);
                                if(players[local_player_id].held_item == TOOL_GUN) {
                                        matrix_scale(matrix_model, 1.5F, 1.5F, 1.5F);
                                }
                                matrix_upload();
                                kv6_render(gun, players[local_player_id].team);
                        }

                        if(local_player_grenades > 0) {
                                matrix_identity(matrix_model);
                                matrix_translate(matrix_model, tool_overlay_x, tool_overlay_y, -6.0F);
                                matrix_translate(matrix_model, tool_overlay_spacing * 3.0F, 0.0F, 0.0F);
                                matrix_rotate(matrix_model, window_time() * 57.4F, 0.0F, 1.0F, 0.0F);
                                matrix_translate(matrix_model, (model_grenade.xpiv - model_grenade.xsiz / 2) * 0.05F,
                                                                 (model_grenade.zpiv - model_grenade.zsiz / 2) * 0.05F,
                                                                 (model_grenade.ypiv - model_grenade.ysiz / 2) * 0.05F);
                                matrix_scale3(matrix_model, tool_overlay_scale);
                                if(players[local_player_id].held_item == TOOL_GRENADE) {
                                        matrix_scale(matrix_model, 1.5F, 1.5F, 1.5F);
                                }
                                matrix_upload();
                                kv6_render(&model_grenade, players[local_player_id].team);
                        }
                }

                if(screen_current == SCREEN_TEAM_SELECT) {
                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, -1.4F, -2.0F, -3.0F);
                        matrix_rotate(matrix_model, -90.0F + 22.5F, 0.0F, 1.0F, 0.0F);
                        matrix_upload();
                        struct Player p_hud;
                        memset(&p_hud, 0, sizeof(struct Player));
                        p_hud.spade_use_timer = FLT_MAX;
                        p_hud.input.keys.packed = 0;
                        p_hud.held_item = TOOL_SPADE;
                        p_hud.input.buttons.packed = 0;
                        p_hud.physics.eye.x = p_hud.pos.x = 0;
                        p_hud.physics.eye.y = p_hud.pos.y = 0;
                        p_hud.physics.eye.z = p_hud.pos.z = 0;
                        p_hud.physics.velocity.x = 0.0F;
                        p_hud.physics.velocity.y = 0.0F;
                        p_hud.physics.velocity.z = 0.0F;
                        p_hud.orientation.x = p_hud.orientation_smooth.x = 1.0F;
                        p_hud.orientation.y = p_hud.orientation_smooth.y = 0.0F;
                        p_hud.orientation.z = p_hud.orientation_smooth.z = 0.0F;
                        p_hud.alive = 1;

                        p_hud.team = TEAM_1;
                        player_render(&p_hud, PLAYERS_MAX);
                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, 1.4F, -2.0F, -3.0F);
                        matrix_rotate(matrix_model, -90.0F - 22.5F, 0.0F, 1.0F, 0.0F);
                        matrix_upload();
                        p_hud.team = TEAM_2;
                        player_render(&p_hud, PLAYERS_MAX);
                }

                if(screen_current == SCREEN_GUN_SELECT) {
                        int team = network_logged_in ? players[local_player_id].team : local_player_newteam;

                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, -1.5F, -1.25F, -3.25F);
                        matrix_rotate(matrix_model, window_time() * 90.0F, 0.0F, 1.0F, 0.0F);
                        matrix_translate(matrix_model, (model_semi.xpiv - model_semi.xsiz / 2.0F) * model_semi.scale,
                                                         (model_semi.zpiv - model_semi.zsiz / 2.0F) * model_semi.scale,
                                                         (model_semi.ypiv - model_semi.ysiz / 2.0F) * model_semi.scale);
                        matrix_upload();
                        kv6_render(&model_semi, team);

                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, 0.0F, -1.25F, -3.25F);
                        matrix_rotate(matrix_model, window_time() * 90.0F, 0.0F, 1.0F, 0.0F);
                        matrix_translate(matrix_model, (model_smg.xpiv - model_smg.xsiz / 2.0F) * model_smg.scale,
                                                         (model_smg.zpiv - model_smg.zsiz / 2.0F) * model_smg.scale,
                                                         (model_smg.ypiv - model_smg.ysiz / 2.0F) * model_smg.scale);
                        matrix_upload();
                        kv6_render(&model_smg, team);

                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, 1.5F, -1.25F, -3.25F);
                        matrix_rotate(matrix_model, window_time() * 90.0F, 0.0F, 1.0F, 0.0F);
                        matrix_translate(matrix_model, (model_shotgun.xpiv - model_shotgun.xsiz / 2.0F) * model_shotgun.scale,
                                                         (model_shotgun.zpiv - model_shotgun.zsiz / 2.0F) * model_shotgun.scale,
                                                         (model_shotgun.ypiv - model_shotgun.ysiz / 2.0F) * model_shotgun.scale);
                        matrix_upload();
                        kv6_render(&model_shotgun, team);
                }

                struct kv6_t* rotating_model = NULL;
                int rotating_model_team = TEAM_SPECTATOR;
                if(gamestate.gamemode_type == GAMEMODE_CTF) {
                        switch(players[local_player_id].team) {
                                case TEAM_1:
                                        if(gamestate.gamemode.ctf.team_2_intel
                                           && gamestate.gamemode.ctf.team_2_intel_location.held.player_id == local_player_id) {
                                                rotating_model = &model_intel;
                                                rotating_model_team = TEAM_2;
                                        }
                                        break;
                                case TEAM_2:
                                        if(gamestate.gamemode.ctf.team_1_intel
                                           && gamestate.gamemode.ctf.team_1_intel_location.held.player_id == local_player_id) {
                                                rotating_model = &model_intel;
                                                rotating_model_team = TEAM_1;
                                        }
                                        break;
                        }
                }
                if(gamestate.gamemode_type == GAMEMODE_TC) {
                        for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++) {
                                float dtx = gamestate.gamemode.tc.territory[k].x - players[local_player_id].pos.x;
                                float dty = (63.0F - gamestate.gamemode.tc.territory[k].z) - players[local_player_id].pos.y;
                                float dtz = gamestate.gamemode.tc.territory[k].y - players[local_player_id].pos.z;
                                float l = dtx * dtx + dty * dty + dtz * dtz;
                                if(l <= 20.0F * 20.0F) {
                                        rotating_model = &model_tent;
                                        rotating_model_team = gamestate.gamemode.tc.territory[k].team;
                                        break;
                                }
                        }
                }
                if(rotating_model) {
                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, 0.0F,
                                                         -(rotating_model->zsiz * 0.5F + rotating_model->zpiv) * rotating_model->scale, -10.0F);
                        matrix_rotate(matrix_model, window_time() * 90.0F, 0.0F, 1.0F, 0.0F);
                        /* Put the model's bounding-box centre on the rotation
                           axis so the spinning intel doesn't wobble around a
                           point that is off the pole.  The offset is applied
                           in the model's scaled space (after kv6_render's
                           pivot translate), so it is in world units. */
                        matrix_translate(matrix_model,
                                         -((rotating_model->xsiz * 0.5F - rotating_model->xpiv) * rotating_model->scale),
                                         0.0F,
                                         -((rotating_model->zsiz * 0.5F - rotating_model->zpiv) * rotating_model->scale));
                        matrix_upload();
                        glViewport(-settings.window_width * 0.4F, settings.window_height * 0.2F, settings.window_width,
                                           settings.window_height);
                        kv6_render(rotating_model, rotating_model_team);
                        glViewport(0.0F, 0.0F, settings.window_width, settings.window_height);
                }
        }
}

static void hud_ingame_keyboard(int key, int action, int mods, int internal);

static int hud_ingame_onscreencontrol(int index, char* str, int activate) {
        if(chat_input_mode == CHAT_NO_INPUT) {
                if(show_exit) {
                        switch(index) {
                                case 0:
                                        if(str)
                                                strcpy(str, "Yes");
                                        if(activate == 0)
                                                hud_ingame_keyboard(WINDOW_KEY_YES, WINDOW_RELEASE, 0, 0);
                                        if(activate == 1)
                                                hud_ingame_keyboard(WINDOW_KEY_YES, WINDOW_PRESS, 0, 0);
                                        return 1;
                                case 1:
                                        if(str)
                                                strcpy(str, "No");
                                        if(activate == 0)
                                                hud_ingame_keyboard(WINDOW_KEY_NO, WINDOW_RELEASE, 0, 0);
                                        if(activate == 1)
                                                hud_ingame_keyboard(WINDOW_KEY_NO, WINDOW_PRESS, 0, 0);
                                        return 1;
                        }
                } else {
                        if(!network_connected || network_logged_in || demo_is_playing()) {
                                /* Demo playback only needs the spectator controls
                                   (LMB/RMB/Cam + Jump/Crouch). The top row would
                                   just overlap the seek bar and do nothing. */
                                if(demo_is_playing() && index <= 6)
                                        return 0;
                                switch(index) {
                                        case 0:
                                                if(str)
                                                        strcpy(str, "G-Chat");
                                                if(activate == 0)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHAT, WINDOW_RELEASE, 0, 0);
                                                if(activate == 1)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHAT, WINDOW_PRESS, 0, 0);
                                                return 1;
                                        case 1:
                                                if(str)
                                                        strcpy(str, "T-Chat");
                                                if(activate == 0)
                                                        hud_ingame_keyboard(WINDOW_KEY_YES, WINDOW_RELEASE, 0, 0);
                                                if(activate == 1)
                                                        hud_ingame_keyboard(WINDOW_KEY_YES, WINDOW_PRESS, 0, 0);
                                                return 1;
                                        case 2:
                                                if(str)
                                                        strcpy(str, "Score");
                                                /* A touch tap is press+release in quick succession, so the
                                                   hold-to-view scoreboard would only flash for a frame.
                                                   Toggle it instead: each tap flips TAB held/released. */
                                                if(activate == 1) {
                                                        static int score_held = 0;
                                                        score_held = !score_held;
                                                        keys(hud_window, WINDOW_KEY_TAB, 0,
                                                                 score_held ? WINDOW_PRESS : WINDOW_RELEASE, 0);
                                                }
                                                return 1;
                                        case 3:
                                                if(str)
                                                        strcpy(str, "Team");
                                                if(activate == 0)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHANGETEAM, WINDOW_RELEASE, 0, 0);
                                                if(activate == 1)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHANGETEAM, WINDOW_PRESS, 0, 0);
                                                return 1;
                                        case 4:
                                                if(str)
                                                        strcpy(str, "Weapon");
                                                if(activate == 0)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHANGEWEAPON, WINDOW_RELEASE, 0, 0);
                                                if(activate == 1)
                                                        hud_ingame_keyboard(WINDOW_KEY_CHANGEWEAPON, WINDOW_PRESS, 0, 0);
                                                return 1;
                                        case 5:
                                                if(str)
                                                        strcpy(str, "Network");
                                                if(activate == 0)
                                                        keys(hud_window, WINDOW_KEY_NETWORKSTATS, 0, WINDOW_RELEASE, 0);
                                                if(activate == 1)
                                                        keys(hud_window, WINDOW_KEY_NETWORKSTATS, 0, WINDOW_PRESS, 0);
                                                return 1;
                                        case 6:
                                                if(str)
                                                        strcpy(str, "Tool");
                                                if(activate == 1)
                                                        mouse_scroll(hud_window, 0, -1);
                                                return 1;
                                        case 7:
#if defined(OS_IOS)
                                                /* iOS has no hardware back button the way Android does, so
                                                   surface Escape as an on-screen "Menu" button: it opens/closes
                                                   the in-game exit menu, exactly like pressing Esc on desktop.
                                                   Rightmost slot keeps indices 0-6 (and their touch zones)
                                                   untouched.
                                                   Route through hud_ingame_keyboard, NOT keys(): the exit menu
                                                   toggle (`show_exit ^= 1`) lives in the hud's keyboard
                                                   callback (~hud.c:3097). keys() only sets
                                                   window_pressed_keys[ESCAPE], which nothing polls — so the
                                                   v12 version of this case appeared to do nothing. The other
                                                   menu-opening buttons (Team case 3, Weapon case 4) use the
                                                   same hud_ingame_keyboard path for the same reason. */
                                                if(str)
                                                        strcpy(str, "Menu");
                                                if(activate == 0)
                                                        hud_ingame_keyboard(WINDOW_KEY_ESCAPE, WINDOW_RELEASE, 0, 0);
                                                if(activate == 1)
                                                        hud_ingame_keyboard(WINDOW_KEY_ESCAPE, WINDOW_PRESS, 0, 0);
                                                return 1;
#else
                                                return 0;
#endif
                                        case 64:
                                                if(str)
                                                        strcpy(str, "LMB");
                                                if(activate == 0)
                                                        mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_RELEASE, 0);
                                                if(activate == 1)
                                                        mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_PRESS, 0);
                                                return 1;
                                        case 65:
                                                if(str)
                                                        strcpy(str, "RMB");
                                                if(activate == 0)
                                                        mouse_click(hud_window, WINDOW_MOUSE_RMB, WINDOW_RELEASE, 0);
                                                if(activate == 1)
                                                        mouse_click(hud_window, WINDOW_MOUSE_RMB, WINDOW_PRESS, 0);
                                                return 1;
                                        /* Jump/Crouch buttons below the left joystick. These poke
                                           window_pressed_keys directly (like the joystick does for
                                           movement) instead of synthesizing key events: the camera
                                           controller polls the pressed state every frame, which
                                           gives correct hold semantics for swimming up and for
                                           staying crouched. */
                                        case 66:
                                                if(str)
                                                        strcpy(str, "Jump");
                                                if(activate == 0)
                                                        window_pressed_keys[WINDOW_KEY_SPACE] = 0;
                                                if(activate == 1)
                                                        window_pressed_keys[WINDOW_KEY_SPACE] = 1;
                                                return 1;
                                        case 67:
                                                if(str)
                                                        strcpy(str, "Crouch");
                                                if(activate == 0)
                                                        window_pressed_keys[WINDOW_KEY_CROUCH] = 0;
                                                if(activate == 1)
                                                        window_pressed_keys[WINDOW_KEY_CROUCH] = 1;
                                                return 1;
                                        /* Spectator free-camera toggle (touch has no keyboard key
                                           for WINDOW_KEY_SWITCH_CAMERA). PRESS only; the handler
                                           toggles on PRESS. */
                                        case 68:
                                                if(camera_mode != CAMERAMODE_SPECTATOR)
                                                        return 0;
                                                if(str)
                                                        strcpy(str, "Cam");
                                                if(activate == 1)
                                                        cameracontroller_bodyview_mode = !cameracontroller_bodyview_mode;
                                                return 1;
                                }
                        }
                }
        } else {
                switch(index) {
                        case 0:
                                if(str)
                                        strcpy(str, "Send");
                                if(activate == 0)
                                        hud_ingame_keyboard(WINDOW_KEY_ENTER, WINDOW_RELEASE, 0, 0);
                                if(activate == 1)
                                        hud_ingame_keyboard(WINDOW_KEY_ENTER, WINDOW_PRESS, 0, 0);
                                return 1;
                        case 1:
                                if(str)
                                        strcpy(str, "Close");
                                if(activate == 0)
                                        hud_ingame_keyboard(WINDOW_KEY_ESCAPE, WINDOW_RELEASE, 0, 0);
                                if(activate == 1)
                                        hud_ingame_keyboard(WINDOW_KEY_ESCAPE, WINDOW_PRESS, 0, 0);
                                return 1;
                        case 2:
#if defined(OS_IOS)
                                /* iOS's software keyboard overlays the bottom of the screen and
                                   hides the chat history, with no built-in dismiss button (unlike
                                   Android's keyboard down-arrow). This toggle hides the keyboard
                                   WITHOUT leaving chat mode, so the player can read back the last
                                   messages, then tap again to bring the keyboard back and type.
                                   Label tracks the current state. Fires on TOUCH_DOWN only. */
                                if(str)
                                        strcpy(str, window_textinput_active() ? "Hide KB" : "Show KB");
                                if(activate == 1)
                                        window_textinput(window_textinput_active() ? 0 : 1);
                                return 1;
#else
                                return 0;
#endif
                }
        }
        return 0;
}

static inline void hud_common_render(mu_Context* ctx) {
        /* RGB accents animate continuously, so refresh retained microui colors
           every frame rather than only when switching menus. */
        hud_refresh_accent_style(ctx);

        // Ingame menu
        if(network_connected && !network_map_transfer) {
                mu_Color color = mu_accent_color(0.15F, 1.F);
                glColor3ub(color.r, color.g, color.b);
                texture_draw_empty(0, settings.window_height, settings.window_width, settings.window_height);
                return;
        }

        glColor3f(0.5F, 0.5F, 0.5F);
        texture_draw(&texture_ui_bg,
                0.0F,
                settings.window_height,
                settings.window_width,
                settings.window_height
        );

#ifdef JENKINS_BUILD
        if(show_update_popup && mu_begin_window_ex(ctx, "New KyroSpades version available", mu_rect(0, 0, 350, 155),
                                                         MU_OPT_HOLDFOCUS | MU_OPT_NORESIZE | MU_OPT_NOCLOSE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                mu_bring_to_front(ctx, cnt);
                cnt->rect = mu_rect((settings.window_width - 350) / 2, 200, 350, 155);
                mu_layout_row(ctx, 1, (int[]) {-1}, -ctx->text_height(ctx->style->font) * 1.75F);

                char msg[1024];
                int diff = atoi(latest_ver) - atoi(JENKINS_BUILD);
                snprintf(msg, 1023, "Your client version (%s) is %i version%s behind.\n"
                                                        "Visit butter.penguins.win/download to update to the latest version (%s).",
                                 JENKINS_BUILD, diff, diff != 1 ? "s": "", latest_ver);
                mu_text(ctx, msg);
                int A = ctx->text_width(ctx->style->font, "Later", 0) * 1.6F;
                mu_layout_row(ctx, 2, (int[]) {-A, -1}, 0);
                if(mu_button(ctx, "Go to website"))
                        file_url("https://butter.penguins.win/download");
                if(mu_button(ctx, "Later")) {
                        show_update_popup = 0;
                }

                mu_end_window(ctx);
        }
#endif
}

static inline void hud_texture_draw(struct texture* t, float x, float y, float w, float h) {
        if(settings.hud_shadows) {
                texture_draw_shadow(t, x, y, w, h);
        } else {
                texture_draw(t, x, y, w, h);
        }
}

static inline void hud_font_render(float x, float y, float h, char* text, float a) {
        if(settings.hud_shadows) {
                font_render_shadow(x, y, h, text, a);
        } else {
                font_render(x, y, h, text);
        }
}

static inline void hud_font_render_outlined(float x, float y, float h, char* text, float a) {
        float color[4];
        glx_get_current_color(color);
        glColor4f(0.F, 0.F, 0.F, a);
        font_render(x - 1.F, y, h, text);
        font_render(x + 1.F, y, h, text);
        font_render(x, y - 1.F, h, text);
        font_render(x, y + 1.F, h, text);
        glColor4f(color[0], color[1], color[2], color[3]);
        font_render(x, y, h, text);
}

static inline void hud_font_render_centered(float x, float y, float h, char* text, float a) {
        if(settings.hud_shadows) {
                font_centered_shadow(x, y, h, text, a);
        } else {
                font_centered(x, y, h, text);
        }
}

static void hud_healthbar_color(float health, float* r, float* g, float* b) {
        /* Full -> green; 85..75 -> yellow; 75..50 -> orange;
           50..30 -> red. Interpolate inside each band so color changes are
           smooth rather than jumping at the thresholds. */
        const float green[3]  = {0.20F, 0.86F, 0.29F};
        const float yellow[3] = {0.95F, 0.86F, 0.12F};
        const float orange[3] = {1.00F, 0.48F, 0.06F};
        const float redc[3]   = {0.95F, 0.08F, 0.06F};
        const float* from;
        const float* to;
        float t;

        if(health >= 85.0F) {
                from = to = green;
                t = 0.0F;
        } else if(health >= 75.0F) {
                from = green; to = yellow;
                t = (85.0F - health) / 10.0F;
        } else if(health >= 50.0F) {
                from = yellow; to = orange;
                t = (75.0F - health) / 25.0F;
        } else if(health >= 30.0F) {
                from = orange; to = redc;
                t = (50.0F - health) / 20.0F;
        } else {
                /* Below 30 HP, pulse from dark red to bright red. */
                float pulse = 0.55F + 0.45F * (sinf((float)window_time() * 12.0F) * 0.5F + 0.5F);
                *r = redc[0] * pulse;
                *g = redc[1] * pulse;
                *b = redc[2] * pulse;
                return;
        }

        *r = from[0] + (to[0] - from[0]) * t;
        *g = from[1] + (to[1] - from[1]) * t;
        *b = from[2] + (to[2] - from[2]) * t;
}

static void hud_healthbar_render(int health) {
        static float displayed_health = 100.0F;
        static double last_update = 0.0;
        static int initialized = 0;
        double now = window_time();
        float dt = last_update > 0.0 ? (float)(now - last_update) : 0.016F;
        last_update = now;
        if(dt < 0.0F) dt = 0.0F;
        if(dt > 0.1F) dt = 0.1F;

        float target = max(0, min(100, health));
        if(!initialized) {
                displayed_health = target;
                initialized = 1;
        } else {
                /* Exponential response is frame-rate independent. Damage drains
                   smoothly while healing fills a little faster. */
                float rate = target < displayed_health ? 9.0F : 12.0F;
                float blend = 1.0F - expf(-rate * dt);
                displayed_health += (target - displayed_health) * blend;
                if(fabsf(target - displayed_health) < 0.05F)
                        displayed_health = target;
        }

        const float bar_x = 8.0F;
        const float bar_top = 22.0F;
        const float bar_w = 160.0F;
        const float bar_h = 12.0F;
        float fill_w = bar_w * displayed_health / 100.0F;
        float r, g, b;
        hud_healthbar_color(displayed_health, &r, &g, &b);

        /* Border flashes too at critical health, making the warning visible
           even when only a very small portion of the bar remains. */
        if(displayed_health < 30.0F)
                glColor3f(r, 0.02F, 0.02F);
        else
                glColor3ub(12, 12, 12);
        texture_draw_empty(bar_x - 2.0F, bar_top + 2.0F, bar_w + 4.0F, bar_h + 4.0F);

        glColor3ub(35, 35, 40);
        texture_draw_empty(bar_x, bar_top, bar_w, bar_h);

        if(fill_w > 0.0F) {
                glColor3f(r, g, b);
                texture_draw_empty(bar_x, bar_top, fill_w, bar_h);
                /* A slim highlight gives the otherwise flat bar some depth. */
                glColor3f(min(1.0F, r + 0.20F), min(1.0F, g + 0.20F), min(1.0F, b + 0.20F));
                texture_draw_empty(bar_x, bar_top, fill_w, 2.0F);
        }

        /* Twenty 5-HP partitions retain a detailed segmented look while the
           fill edge itself remains continuous and smoothly animated. */
        glColor3ub(10, 10, 12);
        for(int i = 1; i < 20; i++) {
                float x = bar_x + bar_w * i / 20.0F;
                texture_draw_empty(x, bar_top, 1.0F, bar_h);
        }
        glColor3f(1.0F, 1.0F, 1.0F);
}

static void hud_ammo_crosshair_render(float scalef) {
        if(tactical_sprint_amount > 0.4f && players[local_player_id].input.keys.sprint) return;
        if(!settings.ammo_crosshair || camera_mode != CAMERAMODE_FPS
           || local_player_id < 0 || local_player_id >= PLAYERS_MAX
           || !players[local_player_id].alive
           || players[local_player_id].held_item != TOOL_GUN
           || players[local_player_id].items_show)
                return;

        int gun = players[local_player_id].weapon;
        int magazine = weapon_ammo(gun);
        if(magazine <= 0)
                return;

        float visible_ammo = (float)local_player_ammo;
        if(weapon_reloading()) {
                float duration = (gun == WEAPON_SHOTGUN) ? 0.5F : 2.5F;
                float progress = ((float)window_time() - weapon_reload_start) / duration;
                progress = max(0.0F, min(1.0F, progress));
                int loadable = weapon_can_reload();
                if(gun == WEAPON_SHOTGUN)
                        loadable = min(loadable, 1);
                visible_ammo += loadable * progress;
        }
        visible_ammo = max(0.0F, min((float)magazine, visible_ammo));

        /* Empty magazines have no ring. During a reload from empty it grows
           into view immediately as the first partial segment fills. */
        if(visible_ammo <= 0.001F)
                return;

        float percent = visible_ammo / magazine * 100.0F;
        float r, g, b;
        hud_healthbar_color(percent, &r, &g, &b);
        glColor3f(r, g, b);

        float cx = settings.window_width * 0.5F;
        float cy = settings.window_height * 0.5F;
        /* 25% smaller than the original 27px center radius. */
        float radius = 20.25F * scalef;
        float half_thickness = 1.75F * scalef;
        float segment_angle = DOUBLEPI / magazine;
        float gap = min(segment_angle * 0.22F, 0.045F);
        float empty_segments = magazine - visible_ammo;
        float usable = segment_angle - gap;
        /* Compute tessellation once so every full bullet sector has exactly the
           same vertex count and therefore perfectly uniform geometry. */
        int full_steps = max(2, (int)ceilf(usable / (DOUBLEPI / 60.0F)));

        /* Segment zero starts at 12 o'clock and angles increase clockwise.
           Empty space grows clockwise as bullets are spent. During reload the
           same boundary retreats anticlockwise, so the ring refills in the
           requested direction. Partial reload progress fills a segment from
           its clockwise end back toward its start. */
        for(int i = 0; i < magazine; i++) {
                float occupancy = min(1.0F, max(0.0F, (i + 1.0F) - empty_segments));
                if(occupancy <= 0.0F)
                        continue;

                float end = (i + 1.0F) * segment_angle - gap * 0.5F;
                float start = end - usable * occupancy;
                int steps = max(1, (int)ceilf(full_steps * occupancy));
                glx_draw_ring_segment_2d(cx, cy,
                                         radius - half_thickness,
                                         radius + half_thickness,
                                         start, end, steps);
        }
        glColor3f(1.0F, 1.0F, 1.0F);
}

static int chat_messages = 16;
static int chat_scroll_offset = 0;

static void hud_render_message(unsigned int channel, unsigned int k) {
char *c;
float x, y;

/* For the global chat channel, allow scrolling back through history
 * while the chat input is open. The offset is driven by the scroll
 * wheel (see hud_ingame_scroll). */
unsigned int idx = k + 1;
if(channel == 0)
        idx += chat_scroll_offset;
if(idx > 127)
        idx = 127;

if(channel == 0) {
x = 16.F;
if(chat_input_mode != CHAT_NO_INPUT && settings.chat_flip_on_open) {
y = 75.F + ((k + 2.F) * (16.F + settings.chat_spacing)) - settings.chat_spacing / 2.F;
} else {
y = 75.F + ((chat_messages - k + 1.F) * (16.F + settings.chat_spacing)) - settings.chat_spacing / 2.F;
}
/* Lift messages so a multi-row input prompt doesn't paint over them. */
if(chat_input_mode != CHAT_NO_INPUT && chat_input_rows > 1)
y += (chat_input_rows - 1) * 16.0F;
} else {
x = 16.F;
y = settings.window_height - 22.0F - 10.0F * k - k * 8.F;
}

// Check if this message contains any mention word
int is_mentioned = 0;
if(settings.chat_mention_words[0] != '\0' && channel == 0 && *chat[channel][idx] != '\0') {
char msg_copy[256];
strncpy(msg_copy, chat[channel][idx], 255);
msg_copy[255] = '\0';

// Convert message to lowercase for comparison
for(char* p = msg_copy; *p; p++) {
if(*p >= 'A' && *p <= 'Z') *p = *p + 32;
}

// Parse mention words (comma or space separated)
char mentions_copy[256];
strncpy(mentions_copy, settings.chat_mention_words, 255);
mentions_copy[255] = '\0';

char* token = strtok(mentions_copy, ", ");
while(token != NULL) {
// Convert token to lowercase
for(char* p = token; *p; p++) {
if(*p >= 'A' && *p <= 'Z') *p = *p + 32;
}

// Skip empty tokens
if(strlen(token) > 0) {
// Check if token exists in message
if(strstr(msg_copy, token) != NULL) {
is_mentioned = 1;
break;
}
}
token = strtok(NULL, ", ");
}
}


if(*chat[channel][idx] != '\0') {
if(channel == 0 && is_mentioned) {
glColor3ub(settings.chat_mention_r, settings.chat_mention_g, settings.chat_mention_b);
} else {
glColor3ub(red(chat_color[channel][idx]), green(chat_color[channel][idx]), blue(chat_color[channel][idx]));
}
glLineWidth(3);
if(channel == 0) {
glx_draw_line_2d(x - 11.F, y + settings.chat_spacing / 2.F + 1.F,
                  x - 11.F, floor(y - 16.F - settings.chat_spacing / 2 + 1.F));
} else {
glx_draw_line_2d(x - 11.F, y + 2.F,
                  x - 11.F, y - 16.F);
}
glLineWidth(1);
glColor3ub(255, 255, 255);
}


char buffer[512];
unsigned int i = 0;
for(c = chat[channel][idx]; *c != '\0'; c++) {
if((unsigned char)*c == 0xFF) {
buffer[i] = '\0';
float len = font_length(16.F, buffer) - 2.F;
        if(is_mentioned)
                glColor3ub(settings.chat_mention_r, settings.chat_mention_g, settings.chat_mention_b);
        font_render(x, y, 16.F, buffer);
        x += len;
        i = 0;
        continue;
}
// Chat color codes are 1..7; everything else (including UTF-8 high bytes) is text.
if((unsigned char)*c > 7) {buffer[i++] = *c;
if(*(c + 1) != '\0') {
continue;
}
}

buffer[i] = '\0';
float len = font_length(16.F, buffer) - 2.F;
if(is_mentioned) {
glColor3ub(settings.chat_mention_r, settings.chat_mention_g, settings.chat_mention_b);
}
font_render(x, y, 16.F, buffer);

switch(*c) {
case '\1': glColor3ub(LIGHTEN(gamestate.team_1.red), LIGHTEN(gamestate.team_1.green), LIGHTEN(gamestate.team_1.blue)); break; // Team1 color
case '\2': glColor3ub(LIGHTEN(gamestate.team_2.red), LIGHTEN(gamestate.team_2.green), LIGHTEN(gamestate.team_2.blue)); break; // Team2 color
case '\3': glColor3ub(255, 255, 255); break; // Team3 (spec) color
case '\4': glColor3ub(255, 0, 0); break; // Red
case '\5': glColor3ub(0, 255, 0); break; // Green
case '\6': glColor3ub(255, 255, 255); break; // Reset (white)
case '\7': glColor3ub(120, 120, 120); break; // Gray
}

x += len;
i = 0;
}
}

static void demo_playback_render_overlay(float scalef) {
        if(!demo_is_playing()) return;
        float bar_w = (float)settings.window_width  * 0.6f;
        float bar_h = 8.0f * scalef;
        float bar_x = ((float)settings.window_width  - bar_w) * 0.5f;
        float bar_y = (float)settings.window_height - 28.0f * scalef;
        float progress = (DemoPlaybackState.duration > 0.0f)
                ? (DemoPlaybackState.current_time / DemoPlaybackState.duration) : 0.0f;
        if(demo_scrub_finger && DemoPlaybackState.duration > 0.0f)
                progress = demo_scrub_target / DemoPlaybackState.duration;
        if(progress < 0.0f) progress = 0.0f;
        if(progress > 1.0f) progress = 1.0f;
#if !defined(OPENGL_ES)
        glDisable(GL_TEXTURE_2D);
#endif
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
        glx_draw_quad_2d(bar_x, bar_y, bar_w, bar_h);
        glColor4f(0.25f, 0.72f, 1.0f, 0.85f);
        glx_draw_quad_2d(bar_x, bar_y, bar_w * progress, bar_h);
        glDisable(GL_BLEND);
#if !defined(OPENGL_ES)
        glEnable(GL_TEXTURE_2D);
#endif
        float text_h = 13.0f * scalef;
        float text_y = bar_y - 18.0f * scalef;
        int cur_m = (int)(DemoPlaybackState.current_time)/60, cur_s = (int)(DemoPlaybackState.current_time)%60;
        int dur_m = (int)(DemoPlaybackState.duration)/60, dur_s = (int)(DemoPlaybackState.duration)%60;
        glColor3f(1.0f, 1.0f, 1.0f);
#ifdef USE_TOUCH
        char speed_buf[8];
        snprintf(speed_buf, sizeof(speed_buf), "%.4gx", (double)DemoPlaybackState.speed);
        hud_font_render(bar_x, text_y, text_h, speed_buf, 1.0f);

        char time_buf[32];
        snprintf(time_buf, sizeof(time_buf), "  %d:%02d / %d:%02d", cur_m, cur_s, dur_m, dur_s);
        hud_font_render(bar_x + font_length(text_h, speed_buf), text_y, text_h, time_buf, 1.0f);

        const char* btn = DemoPlaybackState.finished ? "|<" :
                          DemoPlaybackState.paused   ? "|>" : "||";
        hud_font_render(bar_x + bar_w - font_length(text_h, btn), text_y, text_h, btn, 1.0f);
#else
        char buf[80];
        snprintf(buf, sizeof(buf), "%d:%02d / %d:%02d  %.4gx%s",
                cur_m, cur_s, dur_m, dur_s, (double)DemoPlaybackState.speed,
                DemoPlaybackState.paused ? "  [PAUSED]" : (DemoPlaybackState.finished ? "  [END]" : ""));
        hud_font_render(bar_x, text_y, text_h, buf, 1.0f);
#endif
}


static int hud_project_aim(float yaw, float pitch, float* sx, float* sy) {
        float cx, cy, cz, ax, ay, az;
        camera_vector_from_angles(camera_rot_x, camera_rot_y, &cx, &cy, &cz);
        camera_vector_from_angles(yaw, pitch, &ax, &ay, &az);

        mat4 view, proj, mvp;
        matrix_identity(view);
        matrix_lookAt(view, 0.0, 0.0, 0.0, cx, cy, cz, 0.0, 1.0, 0.0);
        matrix_identity(proj);
        matrix_perspective(proj, settings.camera_fov, ((float)settings.window_width) / ((float)settings.window_height), 0.1F, 128.0F);
        glmc_mat4_mul(proj, view, mvp);

        vec4 clip = {ax, ay, az, 1.0F};
        vec4 out;
        glmc_mat4_mulv(mvp, clip, out);
        if(fabsf(out[3]) < 0.0001F)
                return 0;
        float ndc_x = out[0] / out[3];
        float ndc_y = out[1] / out[3];
        *sx = (ndc_x * 0.5F + 0.5F) * (float)settings.window_width;
        *sy = (ndc_y * 0.5F + 0.5F) * (float)settings.window_height;
        return *sx > -128.0F && *sx < settings.window_width + 128.0F
                && *sy > -128.0F && *sy < settings.window_height + 128.0F;
}

static void hud_draw_target_at(float x, float y, float size) {
        texture_draw(&texture_target, ceilf(x - size * 0.5F), ceilf(y + size * 0.5F), size, size);
}

static void hud_ingame_render(mu_Context* ctx, float scalex, float scalef) {
        // window_mousemode(camera_mode==CAMERAMODE_SELECTION?WINDOW_CURSOR_ENABLED:WINDOW_CURSOR_DISABLED);

        /* World/model rendering can leave GL_TEXTURE_ENV_MODE set to GL_BLEND
           (fog) or GL_COMBINE (team colorize), which makes HUD textures ignore
           glColor and sample incorrectly (e.g. the minimap rendered black).
           Force the standard modulate mode for all HUD drawing. */
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

        hud_active->render_localplayer = players[local_player_id].team != TEAM_SPECTATOR
                && (screen_current == SCREEN_NONE || camera_mode != CAMERAMODE_FPS);

        if(settings.player_stats) {
                if(!network_connected || !network_logged_in
                   || players[local_player_id].team == TEAM_SPECTATOR) {
                        player_stats_reset();
                        player_stats_last_x = players[local_player_id].pos.x;
                        player_stats_last_y = players[local_player_id].pos.y;
                        player_stats_last_z = players[local_player_id].pos.z;
                } else {
                        float dx = players[local_player_id].pos.x - player_stats_last_x;
                        float dy = players[local_player_id].pos.y - player_stats_last_y;
                        float dz = players[local_player_id].pos.z - player_stats_last_z;
                        float dist = sqrt(dx*dx + dy*dy + dz*dz);
                        if(dist < 10.0F) {
                                player_stats_distance += dist;
                        }
                        player_stats_last_x = players[local_player_id].pos.x;
                        player_stats_last_y = players[local_player_id].pos.y;
                        player_stats_last_z = players[local_player_id].pos.z;
                }
                float now = window_time();
                float dt = now - particle_stats_last_time;
                if(dt >= 0.5F) {
                        particle_stats_created_per_second = (particle_stats_total_created - particle_stats_last_total) / dt;
                        particle_stats_last_total = particle_stats_total_created;
                        particle_stats_last_time = now;
                }
        }

        if(cameracontroller_yclamp) {
                glColor3f(1.0F, 1.0F, 1.0F);
                hud_font_render(8.F, settings.window_height / 2 - 4.F, 16.0F, "Y-Clamp enabled", .5f);
        }

        if(window_key_down(WINDOW_KEY_NETWORKSTATS)) {
                if(network_map_transfer)
                        glColor3f(1.0F, 1.0F, 1.0F);
                else
                        glColor3f(0.0F, 0.0F, 0.0F);
                glEnable(GL_DEPTH_TEST);
                glColorMask(0, 0, 0, 0);
                texture_draw_empty(8.0F * scalex, 380.0F * scalef, 160.0F * scalef, 160.0F * scalef);
                glColorMask(1, 1, 1, 1);
                glDepthFunc(GL_NOTEQUAL);
                texture_draw_empty(7.0F * scalex, 381.0F * scalef, 162.0F * scalef, 162.0F * scalef);
                glDepthFunc(GL_LEQUAL);
                glDisable(GL_DEPTH_TEST);
                font_select(FONT_FIXEDSYS);
                char dbg_str[32];

                int max = 0;
                for(int k = 0; k < 40; k++) {
                        max = max(max, network_stats[k].ingoing + network_stats[k].outgoing);
                }
                for(int k = 0; k < 40; k++) {
                        float in_h = (float)(network_stats[39 - k].ingoing) / max * 160.0F;
                        float out_h = (float)(network_stats[39 - k].ingoing + network_stats[39 - k].outgoing) / max * 160.0F;
                        float ping_h = min(network_stats[39 - k].avg_ping / 25.0F, 160.0F);

                        glColor3f(0.0F, 0.0F, 1.0F);
                        texture_draw_empty(8.0F * scalex + 4 * k * scalef, (220.0F + out_h) * scalef, 4.0F * scalef,
                                                           out_h * scalef);
                        if(!k) {
                                sprintf(dbg_str, "out: %i b/s", network_stats[1].outgoing);
                                font_render(8.0F * scalex + 80 * scalef, 212.0F * scalef, 16.0F, dbg_str);
                        }

                        glColor3f(0.0F, 1.0F, 0.0F);
                        texture_draw_empty(8.0F * scalex + 4 * k * scalef, (220.0F + in_h) * scalef, 4.0F * scalef, in_h * scalef);
                        if(!k) {
                                sprintf(dbg_str, "in: %i b/s", network_stats[1].ingoing);
                                font_render(8.0F * scalex, 212.0F * scalef, 16.F, dbg_str);
                        }

                        glColor3f(1.0F, 0.0F, 0.0F);
                        texture_draw_empty(8.0F * scalex + 4 * k * scalef, (220.0F + ping_h) * scalef, 4.0F * scalef,
                                                           ping_h * scalef);
                        if(!k) {
                                sprintf(dbg_str, "ping: %i", network_stats[1].avg_ping);
                                font_render(8.0F * scalex, 202.0F * scalef, 16.F, dbg_str);
                        }
                }
                font_select(FONT_FIXEDSYS);
                glColor3f(1.0F, 1.0F, 1.0F);
        }

        if(network_map_transfer) {
                hud_common_render(ctx);
                glColor3ub(255, 255, 255);

                texture_draw(&texture_splash_icon,
                        settings.window_width - texture_splash_icon.width - 16.F,
                        texture_splash_icon.height + 16.F,
                        texture_splash_icon.width,
                        texture_splash_icon.height
                );

                float p = (compressed_chunk_data_estimate > 0) ?
                        ((float)compressed_chunk_data_offset / (float)compressed_chunk_data_estimate) :
                        0.0F;
                glColor3ub(68, 68, 68);
                texture_draw(&texture_loader, 0, texture_loader.height, settings.window_width, texture_loader.height);

                glColor3ub(255, 255, 255);
                texture_draw(&texture_splash,
                        (settings.window_width - settings.window_height * 4.0F / 3.0F * 0.7F) * 0.5F,
                         530 * scalef,
                         settings.window_height * 4.0F / 3.0F * 0.7F,
                         settings.window_height * 0.7F);

                mu_Color color = mu_accent_color(1.F, 255);
                glColor3ub(color.r, color.g, color.b);
                texture_draw(
                        &texture_loader,
                        0,
                        texture_loader.height,
                        fmin(1.F, p) * settings.window_width,
                        texture_white.height
                );

                glColor3f(1.0F, 1.0F, 1.0F);
                char str[128];
                
                sprintf(str, "Loading Map %iKB/%iKB", compressed_chunk_data_offset / 1024,
                                compressed_chunk_data_estimate / 1024);
                font_render(4.F, texture_loader.height + 16.F + 4.F, 16, str);

                font_select(FONT_FIXEDSYS);
        } else {
                if(window_key_down(WINDOW_KEY_HIDEHUD))
                        return;

                /* Floating damage numbers: drawn early in the 2D pass so chat/
                   scoreboard/other HUD elements still layer on top of them. */
                damagenumbers_render();

                if(screen_current == SCREEN_TEAM_SELECT) {
                        glColor3f(1.0F, 1.0F, 1.0F);
                        char join_str[48];
                        sprintf(join_str, "Press 1 to join %s", gamestate.team_1.name);
                        font_centered(settings.window_width / 4.0F, 61 * scalef, 16.F, join_str);
                        sprintf(join_str, "Press 2 to join %s", gamestate.team_2.name);
                        font_centered(settings.window_width / 4.0F * 3.0F, 61 * scalef, 16.F, join_str);
                        font_centered(settings.window_width / 2.0F, 61 * scalef, 16.F, "Press 3 to spectate");
                        if(!network_logged_in)
                                font_centered(settings.window_width / 2.0F, 41 * scalef, 16.F, "Press 4 to spectate anonymously");
                        glColor3f(1.0F, 1.0F, 1.0F);
                }

                if(screen_current == SCREEN_GUN_SELECT) {
                        glColor3f(1.0F, 0.0F, 0.0F);
                        font_centered(settings.window_width / 4.0F * 1.0F, 61 * scalef, 16.F, "Press 1 to select");
                        font_centered(settings.window_width / 4.0F * 2.0F, 61 * scalef, 16.F, "Press 2 to select");
                        font_centered(settings.window_width / 4.0F * 3.0F, 61 * scalef, 16.F, "Press 3 to select");
                        glColor3f(1.0F, 1.0F, 1.0F);
                }

                // Optional compact team scores at top of screen.  This was
                // always-on in KyroSpades and cost a couple font/quad draws
                // every single gameplay frame; keep the feature, but don't
                // tax the BetterSpades-style fast HUD unless requested.
                if(settings.show_live_player_count && network_connected && network_logged_in) {
                        for(int i = 0; i < 2; i++) {
                                struct Team team;
                                float x_offset;

                                float r, g, b;

                                switch(i) {
                                        case 0: team = gamestate.team_1; x_offset = settings.window_width / 2.F - 75.F; break;
                                        case 1: team = gamestate.team_2; x_offset = settings.window_width / 2.F; break;
                                }

                                r = team.red / 255.F;
                                g = team.green / 255.F;
                                b = team.blue / 255.F;

                                char score_str[8];
                                glColor3ub(team.red, team.green, team.blue);

                                switch(gamestate.gamemode_type) {
                                        case GAMEMODE_CTF:
                                                sprintf(score_str, "%i-%i",
                                                                i == 0 ? gamestate.gamemode.ctf.team_1_score:
                                                                                                                           gamestate.gamemode.ctf.team_2_score,
                                                                gamestate.gamemode.ctf.capture_limit);
                                                break;
                                        case GAMEMODE_TC: {
                                                int t = 0;
                                                for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++)
                                                        if(gamestate.gamemode.tc.territory[k].team == TEAM_1)
                                                                t++;
                                                sprintf(score_str, "%i-%i", t, gamestate.gamemode.tc.territory_count);
                                                break;
                                        }
                                }

                                float score_width = font_length(16.F, score_str);
                                float box_width = score_width + 20.F;

                                glColor4f(0, 0, 0, 0.5F);
                                glEnable(GL_BLEND);
                                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                glColor4f(r, g, b, 1.F);
                                texture_draw_empty(x_offset, settings.window_height - 24.F, box_width, 24.F);
                                glDisable(GL_BLEND);

                                glColor3ub(255, 255, 255);
                                font_render(x_offset + 10.F, settings.window_height - 27.F, 16.0F, score_str);
                        }
                }

                if(chat_input_mode == CHAT_NO_INPUT && window_key_down(WINDOW_KEY_TAB) || camera_mode == CAMERAMODE_SELECTION) {
                        if(network_connected && network_logged_in) {
                                char ping_str[16];
                                sprintf(ping_str, "PING: %ims", network_ping());
                                glColor3f(1.0F, 0.0F, 0.0F);
                                font_centered(settings.window_width / 2.0F, settings.window_height - 4.F, 16.F, ping_str);
                        }


                        int count_team1 = 1;
                        int count_team2 = 1;
                        int count_spec = 0;
                        for(int k = 0; k < PLAYERS_MAX; k++) {
                                if(players[k].connected) {
                                        if(players[k].team == TEAM_1) {
                                                count_team1++;
                                        } else if(players[k].team == TEAM_2) {
                                                count_team2++;
                                        } else {
                                                count_spec++;
                                        }
                                }
                        }

                        float height = 24.F * max(count_team1, count_team2);
                        for(int i = 0; i < 3; i++) {
                                if(i == 2 && count_spec == 0) {
                                        continue;
                                }

                                struct Team team;
                                float x_offset;
                                float y_offset = 0;

                                float r, g, b;

                                switch(i) {
                                        case 0: team = gamestate.team_1; x_offset = settings.window_width / 2.F - 300.F; break;
                                        case 1: team = gamestate.team_2; x_offset = settings.window_width / 2.F; break;
                                        case 2: x_offset = settings.window_width / 2.F - 150.F; y_offset = height + 32.F; break;
                                }

                                if(i != 2) {
                                        r = team.red / 255.F;
                                        g = team.green / 255.F;
                                        b = team.blue / 255.F;
                                } else {
                                        r = 0.1F;
                                        g = 0.1F;
                                        b = 0.1F;
                                }

                                char score_str[8];
                                glColor3ub(team.red, team.green, team.blue);

                                if(i != 2) {
                                        switch(gamestate.gamemode_type) {
                                                case GAMEMODE_CTF:
                                                        sprintf(score_str, "%i-%i",
                                                                        i == 0 ? gamestate.gamemode.ctf.team_1_score:
                                                                                                                           gamestate.gamemode.ctf.team_2_score,
                                                                        gamestate.gamemode.ctf.capture_limit);
                                                        break;
                                                case GAMEMODE_TC: {
                                                        int t = 0;
                                                        for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++)
                                                                if(gamestate.gamemode.tc.territory[k].team == TEAM_1)
                                                                        t++;
                                                        sprintf(score_str, "%i-%i", t, gamestate.gamemode.tc.territory_count);
                                                        break;
                                                }
                                        }
                                }

                                glColor4f(0, 0, 0, 0.5F);
                                glEnable(GL_BLEND);
                                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                glColor4f(r, g, b, 1.F);
                                texture_draw_empty(x_offset, 450 * scalef - y_offset, 300, 24.F);
                                glColor4f(r * 0.75F, g * 0.75F, b * 0.75F, 0.75F);
                                texture_draw_empty(x_offset, 450 * scalef - y_offset, 300, i == 2 ? (24.F * (count_spec + 1)): height);
                                glDisable(GL_BLEND);

                                glColor3ub(255, 255, 255);
                                if(i != 2) {
                                        font_render(x_offset + 300.F - font_length(16.F, score_str) - 8.F, 447 * scalef, 16.0F, score_str);
                                        font_render(x_offset + 8.F,
                                                        450 * scalef - 6.F, 16.0F, team.name);
                                } else {
                                        font_centered(x_offset + 150.F,
                                                        450 * scalef - y_offset - 4.F, 16.0F, "Spectator");
                                }
                        }

                        struct player_table pt[PLAYERS_MAX];
                        int connected = 0;
                        for(int k = 0; k < PLAYERS_MAX; k++) {
                                if(players[k].connected) {
                                        pt[connected].id = k;
                                        pt[connected++].score = players[k].score;
                                }
                        }
                        qsort(pt, connected, sizeof(struct player_table), playertable_sort);

                        int cntt[3] = {0};
                        for(int k = 0; k < connected; k++) {
                                int mul = 0;
                                float x_offset;
                                float y_offset = 0.F;

                                switch(players[pt[k].id].team) {
                                        case TEAM_1: mul = 1; x_offset = settings.window_width / 2.F - 300.F; break;
                                        case TEAM_2: mul = 3; x_offset = settings.window_width / 2.F; break;
                                        default:
                                        case TEAM_SPECTATOR: mul = 2; x_offset = settings.window_width / 2.F - 150.F; y_offset = height + 32.F; break;
                                }
                                if(pt[k].id == local_player_id)
                                        glColor3f(1.0F, 1.0F, 0.0F);
                                else if(!players[pt[k].id].alive)
                                        glColor3f(0.6F, 0.6F, 0.6F);
                                else
                                        glColor3f(1.0F, 1.0F, 1.0F);
                                char id_str[16];
                                sprintf(id_str, "#%i", pt[k].id);
                                glEnable(GL_BLEND);
                                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                glColor4f(1.F, 1.F, 1.F, 0.7F);
                                font_render(x_offset + 8.F, 450 * scalef - 8.F - (24 * (cntt[mul - 1] + 1)) - y_offset,
                                                        16.0F, id_str);

                                if(players[pt[k].id].alive) {
                                        glColor3f(1.F, 1.F, 1.F);
                                } else {
                                        glColor4f(1.F, 1.F, 1.F, 0.5F);
                                }

                                font_render(x_offset + 48.F,
                                                        450 * scalef - 8.F - (24 * (cntt[mul - 1] + 1)) - y_offset, 16.0F, players[pt[k].id].name);
                                glDisable(GL_BLEND);
                                if(mul != 2) {
                                        sprintf(id_str, "%i", pt[k].score);
                                        font_render(x_offset + 300.F - font_length(16.F, id_str) - 8.F,
                                                                450 * scalef - 8.F - (24 * (cntt[mul - 1] + 1)), 16, id_str);
                                }
                                if(gamestate.gamemode_type == GAMEMODE_CTF
                                   && ((gamestate.gamemode.ctf.team_1_intel
                                                && gamestate.gamemode.ctf.team_1_intel_location.held.player_id == pt[k].id)
                                           || (gamestate.gamemode.ctf.team_2_intel
                                                   && gamestate.gamemode.ctf.team_2_intel_location.held.player_id == pt[k].id))) {
                                        texture_draw(&texture_intel,
                                                                 x_offset + 300.F - font_length(16.F, id_str) - 8.F - 24.F,
                                                                 450 * scalef - 8.F - (24 * (cntt[mul - 1] + 1)),
                                                                 18.0F, 18.0F);
                                }
                                cntt[mul - 1]++;
                        }
                }

                int is_local = (camera_mode == CAMERAMODE_FPS) || (cameracontroller_bodyview_player == local_player_id);
                int local_id = (camera_mode == CAMERAMODE_FPS) ? local_player_id : cameracontroller_bodyview_player;

                if(camera_mode == CAMERAMODE_BODYVIEW
                   || (camera_mode == CAMERAMODE_SPECTATOR && cameracontroller_bodyview_mode)) {
                        if(cameracontroller_bodyview_player != local_player_id) {
                                font_select(FONT_FIXEDSYS);
                                char bv_buf[64];
                                snprintf(bv_buf, sizeof(bv_buf), "Spectating %s", players[cameracontroller_bodyview_player].name);
                                float bv_nh = 22.F;
                                float bv_nx = settings.window_width / 2.0F - font_length(bv_nh, bv_buf) / 2.0F;
                                float bv_ny = 4.F + bv_nh;
                                unsigned char bv_r = 255, bv_g = 255, bv_b = 255;
                                switch(players[cameracontroller_bodyview_player].team) {
                                        case TEAM_1: bv_r = gamestate.team_1.red; bv_g = gamestate.team_1.green; bv_b = gamestate.team_1.blue; break;
                                        case TEAM_2: bv_r = gamestate.team_2.red; bv_g = gamestate.team_2.green; bv_b = gamestate.team_2.blue; break;
                                }
                                glColor3ub(bv_r, bv_g, bv_b);
                                font_render(bv_nx - 1.F, bv_ny, bv_nh, bv_buf);
                                font_render(bv_nx + 1.F, bv_ny, bv_nh, bv_buf);
                                font_render(bv_nx, bv_ny - 1.F, bv_nh, bv_buf);
                                font_render(bv_nx, bv_ny + 1.F, bv_nh, bv_buf);
                                glColor3ub(255, 255, 255);
                                font_render(bv_nx, bv_ny, bv_nh, bv_buf);
                        }
                        font_select(FONT_FIXEDSYS);
                        mu_Color color = mu_accent_color(1.F, 255);
                        glColor3ub(color.r, color.g, color.b);
                        font_centered(settings.window_width / 2.0F, settings.window_height, 16.0F,
                                                  "Click to switch players");
                        if(window_time() - local_player_death_time <= local_player_respawn_time) {
                                glColor3f(1.0F, 0.0F, 0.0F);
                                int cnt = local_player_respawn_time - (int)(window_time() - local_player_death_time);
                                char coin[16];
                                sprintf(coin, "INSERT COIN:%i", cnt);
                                font_centered(settings.window_width / 2.0F,
                                                          53.0F * scalef * (cameracontroller_bodyview_mode ? 2.0F : 1.0F), 53.0F * scalef, coin);
                                if(local_player_respawn_cnt_last != cnt) {
                                        if(cnt < 4) {
                                                sound_create(SOUND_LOCAL, (cnt == 1) ? &sound_beep1 : &sound_beep2, 0.0F, 0.0F, 0.0F);
                                        }
                                        local_player_respawn_cnt_last = cnt;
                                }
                        }
                        glColor3f(1.0F, 1.0F, 1.0F);
                }

                if(camera_mode == CAMERAMODE_SPECTATOR && !cameracontroller_bodyview_mode
                   && player_intersection_type >= 0 && player_intersection_player >= 0
                   && player_intersection_player < PLAYERS_MAX
                   && players[player_intersection_player].team != TEAM_SPECTATOR) {
                        font_select(FONT_FIXEDSYS);
                        char hv_buf[64];
                        snprintf(hv_buf, sizeof(hv_buf), "Spectating %s", players[player_intersection_player].name);
                        float hv_nh = 22.F;
                        float hv_nx = settings.window_width / 2.0F - font_length(hv_nh, hv_buf) / 2.0F;
                        float hv_ny = 4.F + hv_nh;
                        unsigned char hv_r = 255, hv_g = 255, hv_b = 255;
                        switch(players[player_intersection_player].team) {
                                case TEAM_1: hv_r = gamestate.team_1.red; hv_g = gamestate.team_1.green; hv_b = gamestate.team_1.blue; break;
                                case TEAM_2: hv_r = gamestate.team_2.red; hv_g = gamestate.team_2.green; hv_b = gamestate.team_2.blue; break;
                        }
                        glColor3ub(hv_r, hv_g, hv_b);
                        font_render(hv_nx - 1.F, hv_ny, hv_nh, hv_buf);
                        font_render(hv_nx + 1.F, hv_ny, hv_nh, hv_buf);
                        font_render(hv_nx, hv_ny - 1.F, hv_nh, hv_buf);
                        font_render(hv_nx, hv_ny + 1.F, hv_nh, hv_buf);
                        glColor3ub(255, 255, 255);
                        font_render(hv_nx, hv_ny, hv_nh, hv_buf);
                }

                if(camera_mode == CAMERAMODE_FPS
                   || ((camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR)
                           && cameracontroller_bodyview_mode)) {
                        glColor3f(1.0F, 1.0F, 1.0F);

                        if(settings.iron_sight && players[local_id].held_item == TOOL_GUN && players[local_id].input.buttons.rmb
                           && players[local_id].alive) {
                                struct texture* zoom;
                                switch(players[local_id].weapon) {
                                        case WEAPON_RIFLE: zoom = &texture_zoom_semi; break;
                                        case WEAPON_SMG: zoom = &texture_zoom_smg; break;
                                        case WEAPON_SHOTGUN: zoom = &texture_zoom_shotgun; break;
                                }
                                float last_shot = is_local ? weapon_last_shot : players[local_id].gun_shoot_timer;
                                float zoom_factor = fmax(
                                        0.25F * (1.0F - ((window_time() - last_shot) / weapon_delay(players[local_id].weapon))) + 1.0F,
                                        1.0F);
                                float current_zoom_factor = zoom_factor;
                                float aspect_ratio = (float)zoom->width / (float)zoom->height;
                                float size_scale = 1.0F;

                                if(settings.ads_zoom_animation) {
                                        float ads_time = window_time() - players[local_id].input.buttons.rmb_start;
                                        float ads_scale = fmin(ads_time / 0.15F, 1.0F);
                                        // Use smoothstep for smoother zoom-in animation
                                        float ads_scale_smooth = ads_scale * ads_scale * (3.0F - 2.0F * ads_scale);
                                        current_zoom_factor = 1.0F + (zoom_factor - 1.0F) * ads_scale_smooth;
                                        // Scale the image size from 0.5x to 1.0x during ADS transition
                                        size_scale = 0.5F + 0.5F * ads_scale_smooth;
                                }

                                texture_draw(zoom, (settings.window_width - settings.window_height * aspect_ratio * current_zoom_factor * size_scale) / 2.0F,
                                                         settings.window_height * (current_zoom_factor * size_scale * 0.5F + 0.5F),
                                                         settings.window_height * aspect_ratio * current_zoom_factor * size_scale, settings.window_height * current_zoom_factor * size_scale);
                                texture_draw_sector(zoom, 0, settings.window_height * (current_zoom_factor * size_scale * 0.5F + 0.5F),
                                                                        (settings.window_width - settings.window_height * aspect_ratio * current_zoom_factor * size_scale)
                                                                                / 2.0F,
                                                                        settings.window_height * current_zoom_factor * size_scale, 0.0F, 0.0F, 1.0F / (float)zoom->width, 1.0F);
                                texture_draw_sector(
                                        zoom, (settings.window_width + settings.window_height * aspect_ratio * current_zoom_factor * size_scale) / 2.0F,
                                        settings.window_height * (current_zoom_factor * size_scale * 0.5F + 0.5F),
                                        (settings.window_width - settings.window_height * aspect_ratio * current_zoom_factor * size_scale) / 2.0F,
                                        settings.window_height * current_zoom_factor * size_scale, (float)(zoom->width - 1) / (float)zoom->width, 0.0F,
                                        1.0F / (float)zoom->width, 1.0F);
                        } else {
                                if(settings.free_aim && camera_mode == CAMERAMODE_FPS) {
                                        float sx, sy;
                                        if(hud_project_aim(camera_crosshair_rot_x, camera_crosshair_rot_y, &sx, &sy)) {
                                                /* Draw only the actual shot direction.  A second eased
                                                   muzzle marker looked cool, but made it unclear where
                                                   bullets would land. */
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                                hud_draw_target_at(sx, sy, 20.0F);
                                        }
                                } else {
                                        hud_draw_target_at(settings.window_width * 0.5F, settings.window_height * 0.5F,
                                                           (float)texture_target.width);
                                }
                        }

                        hud_ammo_crosshair_render(scalef);

                        if(window_time() - local_player_last_damage_timer <= 0.5F && is_local) {
                                float ang = atan2(players[local_player_id].orientation.z, players[local_player_id].orientation.x)
                                        - atan2(camera_z - local_player_last_damage_z, camera_x - local_player_last_damage_x) + PI;
                                texture_draw_rotated(&texture_indicator, settings.window_width / 2.0F, settings.window_height / 2.0F,
                                                                         200, 200, ang);
                        }

                        int health
                                = is_local ? (players[local_id].alive ? local_player_health : 0) : (players[local_id].alive ? 100 : 0);

                        if(health <= 30)
                                glColor3f(1, 0, 0);
                        else if(health <= 50)
                                glColor3f(1, 0.5F, 0);
                        else
                                glColor3f(1, 1, 1);

                        font_select(FONT_FANTASY);
                        char hp[4];
                        sprintf(hp, "%i", health);
                        float health_top = settings.healthbar ? 60.0F : 40.0F;
                        hud_texture_draw(&texture_health, 8.F, health_top, 36.0F, 32.F);
                        hud_font_render(48.F, health_top - 2.0F, 30.F, hp, 1.F);
                        if(settings.healthbar)
                                hud_healthbar_render(health);

                        char item_mini_str[32];
                        struct texture* item_mini;
                        int off = 0;

                        struct Team* team = players[local_id].team == 0 ? &gamestate.team_1: &gamestate.team_2;
                        glColor3ub(LIGHTEN(team->red), LIGHTEN(team->green), LIGHTEN(team->blue));

                        switch(players[local_id].held_item) {
                                default:
                                case TOOL_BLOCK: off = 64 * scalef;
                                case TOOL_SPADE:
                                        item_mini = &texture_block;
                                        glColor3ub(players[local_player_id].block.red, players[local_player_id].block.green, players[local_player_id].block.blue);
                                        sprintf(item_mini_str, "%i", is_local ? local_player_blocks : 50);
                                        break;
                                case TOOL_GRENADE:
                                        item_mini = &texture_grenade;
                                        sprintf(item_mini_str, "%i", is_local ? local_player_grenades : 3);
                                        break;
                                case TOOL_GUN: {
                                        int ammo = is_local ? local_player_ammo : players[local_id].ammo;
                                        int ammo_reserve = is_local ? local_player_ammo_reserved : players[local_id].ammo_reserved;
                                        sprintf(item_mini_str, "%i/%i", ammo, ammo_reserve);
                                        switch(players[local_id].weapon) {
                                                case WEAPON_RIFLE: item_mini = &texture_ammo_semi; break;
                                                case WEAPON_SMG: item_mini = &texture_ammo_smg; break;
                                                case WEAPON_SHOTGUN: item_mini = &texture_ammo_shotgun; break;
                                        }
                                        if(ammo == 0)
                                                glColor3f(1.0F, 0.0F, 0.0F);
                                        break;
                                }
                        }

                        hud_texture_draw(item_mini, settings.window_width - texture_health.width - 8.F, item_mini->height + 8.F, texture_health.width, texture_health.height);
                        /* The block icon stays tinted with the held block's
                           color, but the counter text must stay readable --
                           e.g. when you copy the ground's (dark) color, a
                           dark number would vanish against the HUD. */
                        if(players[local_id].held_item == TOOL_BLOCK)
                                glColor3f(1.0F, 1.0F, 1.0F);
                        hud_font_render(settings.window_width - texture_health.width - 12.F - font_length(30.F, item_mini_str), 37.F, 30.F, item_mini_str, 1.F);
                        font_select(FONT_FIXEDSYS);
                        glColor3f(1.0F, 1.0F, 1.0F);

                        float gmi_y = 54.F;

                        if(players[local_id].held_item == TOOL_BLOCK) {
                                /* Border first, palette second: transparent padding reveals
                                   the outline while the actual color remains unobscured. */
                                palette_draw_selection_border(players[local_id].block.packed);
                                glColor3f(1.0F, 1.0F, 1.0F);
                                texture_draw(&texture_color_selection, palette_left(), palette_top(),
                                                         palette_size(), palette_size());
                                glColor3f(1.0F, 1.0F, 1.0F);
                        }

                        if(settings.show_live_player_count) {
                                unsigned int team1_alive = 0;
                                unsigned int team2_alive = 0;
                                for(int k = 0; k < PLAYERS_MAX; k++) {
                                        if(players[k].connected && players[k].alive) {
                                                if(players[k].team == TEAM_1) {
                                                        team1_alive++;
                                                } else if(players[k].team == TEAM_2) {
                                                        team2_alive++;
                                                }
                                        }
                                }

                                char count[4];
                                sprintf(count, "%i", team1_alive);

                                font_select(FONT_FANTASY);
                                glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
                                // helmet and text
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y + 32.F, 32.F, 16.F);
                                glColor3ub(255, 255, 255);
                                hud_font_render(settings.window_width - 8.F - 32.F - 30.F, gmi_y + 28.F, 30.F, count, .4F);

                                // skin
                                glColor3ub(222, 200, 141);
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y + 16.F, 32.F, 16.F);

                                // eyes
                                glColor3ub(0, 0, 0);
                                texture_draw_empty(settings.window_width - 8.F - 26.F, gmi_y + 16.F, 6.F, 6.F);
                                texture_draw_empty(settings.window_width - 8.F - 11.F, gmi_y + 16.F, 6.F, 6.F);
                                // shadow
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y, 32.F, 2.F);

                                // team 2
                                gmi_y += 40.F;

                                sprintf(count, "%i", team2_alive);
                                font_select(FONT_FANTASY);
                                glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
                                // helmet and text
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y + 32.F, 32.F, 16.F);
                                glColor3ub(255, 255, 255);
                                hud_font_render(settings.window_width - 8.F - 32.F - 30.F, gmi_y + 28.F, 30.F, count, .4F);

                                // skin
                                glColor3ub(222, 200, 141);
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y + 16.F, 32.F, 16.F);

                                // eyes
                                glColor3ub(0, 0, 0);
                                texture_draw_empty(settings.window_width - 8.F - 26.F, gmi_y + 16.F, 6.F, 6.F);
                                texture_draw_empty(settings.window_width - 8.F - 11.F, gmi_y + 16.F, 6.F, 6.F);
                                // shadow
                                texture_draw_empty(settings.window_width - 8.F - 32.F, gmi_y, 32.F, 2.F);
                        }
                }

                if(camera_mode == CAMERAMODE_SPECTATOR && spec_color_palette_time > window_time()) {
                        unsigned int cur = rgb((int)(fog_color[0] * 255.0F + 0.5F), (int)(fog_color[1] * 255.0F + 0.5F),
                                                                   (int)(fog_color[2] * 255.0F + 0.5F));
                        palette_draw_selection_border(cur);
                        glColor3f(1.0F, 1.0F, 1.0F);
                        texture_draw(&texture_color_selection, palette_left(), palette_top(),
                                                 palette_size(), palette_size());
                        glColor3f(1.0F, 1.0F, 1.0F);
                }

                if(settings.player_stats && network_connected && network_logged_in
                   && players[local_player_id].team != TEAM_SPECTATOR) {
                        font_select(FONT_FIXEDSYS);
                        float x = 8.F;
                        float y = settings.window_height / 2.F - 60.F;
                        float h = 16.F;
                        char line[64];
                        char num_buf[32];

                        glColor3ub(255, 255, 255);
                        format_comma(num_buf, player_stats_blocks_placed);
                        sprintf(line, "Blocks Placed: %s", num_buf);
                        hud_font_render_outlined(x, y, h, line, 1.F);

                        format_comma(num_buf, player_stats_kills);
                        sprintf(line, "Kills: %s", num_buf);
                        hud_font_render_outlined(x, y + h, h, line, 1.F);

                        format_comma(num_buf, player_stats_headshots);
                        sprintf(line, "Headshot Kills: %s", num_buf);
                        hud_font_render_outlined(x, y + h * 2, h, line, 1.F);

                        format_comma(num_buf, player_stats_deaths);
                        sprintf(line, "Deaths: %s", num_buf);
                        hud_font_render_outlined(x, y + h * 3, h, line, 1.F);

                        format_comma(num_buf, (int)player_stats_distance);
                        sprintf(line, "Distance Traveled: %s blocks", num_buf);
                        hud_font_render_outlined(x, y + h * 4, h, line, 1.F);

                        format_comma(num_buf, player_stats_jumps);
                        sprintf(line, "Jumps: %s", num_buf);
                        hud_font_render_outlined(x, y + h * 5, h, line, 1.F);

                        font_select(FONT_FIXEDSYS);
                }

                if(settings.player_technical_stats && network_connected && network_logged_in
                   && players[local_player_id].team != TEAM_SPECTATOR) {
                        font_select(FONT_FIXEDSYS);
                        float right_edge = settings.window_width - 8.F;
                        float y = settings.window_height / 2.F - 44.F;
                        float h = 16.F;
                        char line[64];
                        char num_buf[32];

                        glColor3ub(255, 255, 255);
                        format_comma(num_buf, particle_stats_count);
                        sprintf(line, "Particles: %s", num_buf);
                        hud_font_render_outlined(right_edge - font_length(h, line), y, h, line, 1.F);

                        format_comma(num_buf, (int)particle_stats_created_per_second);
                        sprintf(line, "New Parts/s: %s", num_buf);
                        hud_font_render_outlined(right_edge - font_length(h, line), y + h, h, line, 1.F);

                        format_comma(num_buf, particle_stats_vertices);
                        sprintf(line, "Vertices: %s", num_buf);
                        hud_font_render_outlined(right_edge - font_length(h, line), y + h * 2, h, line, 1.F);

                        format_comma(num_buf, model_total_voxels() + map_total_blocks());
                        sprintf(line, "Voxels: %s", num_buf);
                        hud_font_render_outlined(right_edge - font_length(h, line), y + h * 3, h, line, 1.F);

                        int* pick_pos = camera_terrain_pick_local(1);
                        if(pick_pos) {
                                float ex, ey, ez;
                                camera_local_eye(&ex, &ey, &ez);
                                double dx = pick_pos[0] - ex;
                                double dy = pick_pos[1] - ey;
                                double dz = pick_pos[2] - ez;
                                double dist = sqrt(dx*dx + dy*dy + dz*dz);
                                sprintf(line, "Dist: %.0f", dist);
                        } else {
                                sprintf(line, "Dist: --");
                        }
                        hud_font_render_outlined(right_edge - font_length(h, line), y + h * 4, h, line, 1.F);

                        font_select(FONT_FIXEDSYS);
                }

                if(camera_mode != CAMERAMODE_SELECTION) {
                        font_select(FONT_FIXEDSYS);

                        if(settings.chat_shadow != 0.f) {
                                float chat_width = 0;
                                int chat_height = 0;
                                for(int k = 0; k < chat_messages; k++) {
                                        int idx = k + 1 + chat_scroll_offset;
                                        if(idx > 127) idx = 127;
                                        if((window_time() - chat_timer[0][idx] < 10.0F || chat_input_mode != CHAT_NO_INPUT)
                                           && strlen(chat[0][idx]) > 0) {
                                                chat_width = fmaxf(font_length(16.0F, chat[0][idx]), chat_width);
                                                chat_height = k + 1;
                                        }

                                }

                                if(chat_input_mode != CHAT_NO_INPUT) {
                                        chat_width = fmaxf(chat_width, font_length(16.0F, chat[0][0]));
                                }

                                if(chat_input_mode != CHAT_NO_INPUT) {
                                        chat_messages = min(127, floor((settings.window_height - 100.F) / (16.F + settings.chat_spacing)));
                                        chat_height = chat_messages;
                                } else {
                                        chat_messages = 12;
                                }

                                if(chat_height > 0) {
                                        float x = 3.F,
                                                  y = 76.F + ((chat_messages + 1.F) * (16.F + settings.chat_spacing)),
                                                  w = chat_width + 16.0F,
                                                  h = (16.F + settings.chat_spacing) * chat_height;

                                        mu_Color color = mu_accent_color(0.3F, settings.chat_shadow * 255);
                                        glColor4ub(color.r, color.g, color.b, color.a);
                                        glEnable(GL_BLEND);
                                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                                        texture_draw_empty(x, y, w, h);
                                        if(chat_input_mode != CHAT_NO_INPUT) {
                                                texture_draw_empty(3.0F, 90.F, chat_width + 16.0F, 42.F);

                                                color = mu_accent_color(1.F, 255);
                                                glColor4ub(color.r, color.g, color.b, color.a);
                                                glLineWidth(3);
                                                glx_draw_line_2d(3.0F, 90.F, chat_width + 19.F, 90.F);
                                        }


                                        glDisable(GL_BLEND);
                                }
                        }

                        glColor3f(1.0F, 1.0F, 1.0F);

                        if(chat_input_mode != CHAT_NO_INPUT) {
                                chat_cursor_clamp();
                                float avail_w = (float)settings.window_width - 11.0F - 16.0F;
                                int row_starts[CHAT_INPUT_MAX_ROWS], row_lens[CHAT_INPUT_MAX_ROWS];
                                int rows = chat_wrap(avail_w, row_starts, row_lens, CHAT_INPUT_MAX_ROWS);
                                chat_input_rows = rows;
                                int cur_row = 0, cur_col = 0;
                                chat_cursor_to_rowcol(row_starts, row_lens, rows, &cur_row, &cur_col);
                                /* Render rows bottom-up so the cursor's row sits on the original
                                   baseline (y=69) and earlier wrapped rows stack above. The
                                   prefix label rides on the topmost rendered row. */
                                float top_y = 69.F + (rows - 1) * CHAT_INPUT_ROW_H;
                                switch(chat_input_mode) {
                                        case CHAT_ALL_INPUT:
                                                font_render(11.0F, top_y + 15.F, 16.0F, "Global:");
                                                break;
                                        case CHAT_TEAM_INPUT:
                                                font_render(11.0F, top_y + 15.F, 16.0F, "Team:");
                                                break;
                                }
                                char tmp[260];
                                int sel_lo = -1, sel_hi = -1;
                                if(chat_sel_active()) chat_sel_range(&sel_lo, &sel_hi);
                                for(int r = 0; r < rows; r++) {
                                        float y = 69.F + (rows - 1 - r) * CHAT_INPUT_ROW_H;
                                        int n = row_lens[r];
                                        if(n >= (int)sizeof(tmp)) n = (int)sizeof(tmp) - 1;
                                        int row_start = row_starts[r];
                                        int row_end = row_start + n;

                                        if(sel_lo >= 0 && sel_hi > sel_lo
                                           && sel_hi > row_start && sel_lo < row_end) {
                                                int rl = sel_lo > row_start ? sel_lo - row_start : 0;
                                                int rh = sel_hi < row_end ? sel_hi - row_start : n;
                                                char pre[260], mid[260];
                                                memcpy(pre, chat[0][0] + row_start, rl);
                                                pre[rl] = 0;
                                                memcpy(mid, chat[0][0] + row_start + rl, rh - rl);
                                                mid[rh - rl] = 0;
                                                float x0 = 11.0F + font_length(16.0F, pre);
                                                float x1 = 11.0F + font_length(16.0F, pre) + font_length(16.0F, mid);
                                                mu_Color sc = mu_color(80, 130, 220, 200);
                                                glColor4ub(sc.r, sc.g, sc.b, sc.a);
                                                glEnable(GL_BLEND);
                                                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                                texture_draw_empty(x0, y, x1 - x0, CHAT_INPUT_ROW_H);
                                                glDisable(GL_BLEND);
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                        }

                                        memcpy(tmp, chat[0][0] + row_start, n);
                                        tmp[n] = 0;
                                        font_render(11.0F, y, 16.0F, tmp);
                                        if(r == cur_row && !chat_sel_active()) {
                                                int cc = chat_cursor - row_start;
                                                if(cc < 0) cc = 0;
                                                if(cc > n) cc = n;
                                                char before[260];
                                                memcpy(before, chat[0][0] + row_start, cc);
                                                before[cc] = 0;
                                                float cx = 11.0F + font_length(16.0F, before);
                                                font_render(cx, y, 16.0F, "_");
                                        }
                                }
                        }

                        for(int k = 0; k < chat_messages; k++) {
                                glColor3ub(255, 255, 255);
                                int idx0 = k + 1 + chat_scroll_offset;
                                if(idx0 > 127) idx0 = 127;
                                if(window_time() - chat_timer[0][idx0] < 10.0F || chat_input_mode != CHAT_NO_INPUT) {
                                        hud_render_message(0, k);
                                }

                                // Hide killfeed when chat is open
                                if(chat_input_mode == CHAT_NO_INPUT && window_time() - chat_timer[1][k + 1] < 10.0F) {
                                        hud_render_message(1, k);
                                }
                        }

                        font_select(FONT_FIXEDSYS);
                        glColor3ub(255, 255, 255);
                }

                if(gamestate.gamemode_type == GAMEMODE_TC && gamestate.progressbar.tent < gamestate.gamemode.tc.territory_count
                   && gamestate.gamemode.tc.territory[gamestate.progressbar.tent].team
                           != gamestate.progressbar.team_capturing) {
                        float p = max(min(gamestate.progressbar.progress
                                                                  + 0.05F * gamestate.progressbar.rate * (window_time() - gamestate.progressbar.update),
                                                          1.0F),
                                                  0.0F);
                        float pbx = gamestate.gamemode.tc.territory[gamestate.progressbar.tent].x - players[local_player_id].pos.x;
                        float pby = (63.0F - gamestate.gamemode.tc.territory[gamestate.progressbar.tent].z)
                                - players[local_player_id].pos.y;
                        float pbz = gamestate.gamemode.tc.territory[gamestate.progressbar.tent].y - players[local_player_id].pos.z;
                        float l = pbx * pbx + pby * pby + pbz * pbz;
                        if(p < 1.0F && l < 20.0F * 20.0F) {
                                switch(gamestate.gamemode.tc.territory[gamestate.progressbar.tent].team) {
                                        case TEAM_1: glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue); break;
                                        case TEAM_2: glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue); break;
                                        default: glColor3ub(0, 0, 0);
                                }
                                texture_draw(&texture_white, (settings.window_width - 440.0F * scalef) / 2.0F + 440.0F * scalef * p,
                                                         settings.window_height * 0.25F, 440.0F * scalef * (1.0F - p), 20.0F * scalef);
                                switch(gamestate.progressbar.team_capturing) {
                                        case TEAM_1: glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue); break;
                                        case TEAM_2: glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue); break;
                                        default: glColor3ub(0, 0, 0);
                                }
                                texture_draw(&texture_white, (settings.window_width - 440.0F * scalef) / 2.0F,
                                                         settings.window_height * 0.25F, 440.0F * scalef * p, 20.0F * scalef);
                        }
                }

                // draw the minimap
                if(camera_mode != CAMERAMODE_SELECTION) {
                        glColor3f(1.0F, 1.0F, 1.0F);
                        // large
                        /* Never overlay the big map while typing in chat (same
                           guard the scoreboard above has): a map state armed
                           through any path must not flash while typing. */
                        if(chat_input_mode == CHAT_NO_INPUT && window_key_down(WINDOW_KEY_MAP)) {
                                float minimap_x = (settings.window_width - (map_size_x + 1) * scalef) / 2.0F;
                                float minimap_y = ((600 - map_size_z - 1) / 2.0F + map_size_z + 1) * scalef;

                                texture_draw(&texture_minimap, minimap_x, minimap_y, 512 * scalef, 512 * scalef);

                                char c[2] = {0};
                                font_select(FONT_FANTASY);
                                for(int k = 0; k < 8; k++) {
                                        c[0] = 'A' + k;
                                        font_centered(minimap_x + (64 * k + 32) * scalef, minimap_y + 10.0F * scalef, 10.0F, c);
                                        c[0] = '1' + k;
                                        font_centered(minimap_x - 10, minimap_y - (64 * k + 32 - 4) * scalef, 10.0F, c);
                                }
                                font_select(FONT_FIXEDSYS);

                                tracer_minimap(1, scalef, minimap_x, minimap_y, 512.0F);

                                if(gamestate.gamemode_type == GAMEMODE_CTF) {
                                        if(!gamestate.gamemode.ctf.team_1_intel) {
                                                glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
                                                texture_draw_rotated(
                                                        &texture_intel, minimap_x + gamestate.gamemode.ctf.team_1_intel_location.dropped.x * scalef,
                                                        minimap_y - gamestate.gamemode.ctf.team_1_intel_location.dropped.y * scalef, 12 * scalef,
                                                        12 * scalef, 0.0F);
                                        }
                                        if(map_object_visible(gamestate.gamemode.ctf.team_1_base.x, 0.0F,
                                                                                  gamestate.gamemode.ctf.team_1_base.y)) {
                                                glColor3ub(gamestate.team_1.red * 0.94F, gamestate.team_1.green * 0.94F,
                                                                   gamestate.team_1.blue * 0.94F);
                                                texture_draw_empty_rotated(minimap_x + gamestate.gamemode.ctf.team_1_base.x * scalef,
                                                                                                   minimap_y - gamestate.gamemode.ctf.team_1_base.y * scalef,
                                                                                                   12 * scalef, 12 * scalef, 0.0F);
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                                texture_draw_rotated(
                                                        &texture_medical, minimap_x + gamestate.gamemode.ctf.team_1_base.x * scalef,
                                                        minimap_y - gamestate.gamemode.ctf.team_1_base.y * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }

                                        if(!gamestate.gamemode.ctf.team_2_intel) {
                                                glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
                                                texture_draw_rotated(
                                                        &texture_intel, minimap_x + gamestate.gamemode.ctf.team_2_intel_location.dropped.x * scalef,
                                                        minimap_y - gamestate.gamemode.ctf.team_2_intel_location.dropped.y * scalef, 12 * scalef,
                                                        12 * scalef, 0.0F);
                                        }
                                        if(map_object_visible(gamestate.gamemode.ctf.team_2_base.x, 0.0F,
                                                                                  gamestate.gamemode.ctf.team_2_base.y)) {
                                                glColor3ub(gamestate.team_2.red * 0.94F, gamestate.team_2.green * 0.94F,
                                                                   gamestate.team_2.blue * 0.94F);
                                                texture_draw_empty_rotated(minimap_x + gamestate.gamemode.ctf.team_2_base.x * scalef,
                                                                                                   minimap_y - gamestate.gamemode.ctf.team_2_base.y * scalef,
                                                                                                   12 * scalef, 12 * scalef, 0.0F);
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                                texture_draw_rotated(
                                                        &texture_medical, minimap_x + gamestate.gamemode.ctf.team_2_base.x * scalef,
                                                        minimap_y - gamestate.gamemode.ctf.team_2_base.y * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                }
                                if(gamestate.gamemode_type == GAMEMODE_TC) {
                                        for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++) {
                                                switch(gamestate.gamemode.tc.territory[k].team) {
                                                        case TEAM_1:
                                                                glColor3f(gamestate.team_1.red * 0.94F, gamestate.team_1.green * 0.94F,
                                                                                  gamestate.team_1.blue * 0.94F);
                                                                break;
                                                        case TEAM_2:
                                                                glColor3f(gamestate.team_2.red * 0.94F, gamestate.team_2.green * 0.94F,
                                                                                  gamestate.team_2.blue * 0.94F);
                                                                break;
                                                        default:
                                                        case TEAM_SPECTATOR: glColor3ub(0, 0, 0);
                                                }
                                                texture_draw_rotated(
                                                        &texture_command, minimap_x + gamestate.gamemode.tc.territory[k].x * scalef,
                                                        minimap_y - gamestate.gamemode.tc.territory[k].y * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                }

                                for(int k = 0; k < PLAYERS_MAX; k++) {
                                        if(players[k].connected && players[k].alive && k != local_player_id
                                           && players[k].team != TEAM_SPECTATOR
                                           && (players[k].team == players[local_player_id].team || camera_mode == CAMERAMODE_SPECTATOR)) {
                                                switch(players[k].team) {
                                                        case TEAM_1:
                                                                glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
                                                                break;
                                                        case TEAM_2:
                                                                glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
                                                                break;
                                                }
                                                float ang = -atan2(players[k].orientation.z, players[k].orientation.x) - HALFPI;
                                                texture_draw_rotated(&texture_player, minimap_x + players[k].pos.x * scalef,
                                                                                         minimap_y - players[k].pos.z * scalef, 12 * scalef, 12 * scalef, ang);
                                        }
                                }

                                glColor3f(0.0F, 1.0F, 1.0F);
                                texture_draw_rotated(&texture_player, minimap_x + camera_x * scalef, minimap_y - camera_z * scalef,
                                                                         12 * scalef, 12 * scalef, camera_rot_x + PI);
                                glColor3f(1.0F, 1.0F, 1.0F);
                        } else {
                                // minimized, top right
                                float zoom_sizes[] = {32.0F, 64.0F, 128.0F, 256.0F, 512.0F};
                                int zoom_idx = max(0, min(4, settings.minimap_zoom - 1));
                                float viewport = zoom_sizes[zoom_idx];
                                float half_vp = viewport / 2.0F;
                                float view_x = camera_x - half_vp;
                                float view_z = camera_z - half_vp;
                                float map_scale = 128.0F / viewport;
                                char sector_str[3] = {(int)(camera_x / 64.0F) + 'A', (int)(camera_z / 64.0F) + '1', 0};
                                glColor4f(0.F, 0.F, 0.F, 0.7F);

                                switch(players[local_player_id].team) {
                                        case TEAM_1: glColor3ub(LIGHTEN(gamestate.team_1.red), LIGHTEN(gamestate.team_1.green), LIGHTEN(gamestate.team_1.blue)); break;
                                        case TEAM_2: glColor3ub(LIGHTEN(gamestate.team_2.red), LIGHTEN(gamestate.team_2.green), LIGHTEN(gamestate.team_2.blue)); break;
                                        case TEAM_SPECTATOR:
                                        default: glColor3ub(150, 150, 150);
                                }
                                font_select(FONT_FANTASY);
                                hud_font_render_centered(settings.window_width - 77 * scalef, 454 * scalef, 30.F, sector_str, 1.F);
                                font_select(FONT_FIXEDSYS);

                                glColor3ub(0, 0, 0);
                                texture_draw_empty(settings.window_width - 144 * scalef, 586 * scalef, 130 * scalef, 130 * scalef);
                                glColor3f(1.0F, 1.0F, 1.0F);

                                {
                                        /* Previously this used texture_draw_sector() with
                                           fractional texcoords u=(camera_x-64)/512 .. u+0.25.
                                           Every other ingredient of that call (state guards,
                                           blending, the texture itself, the draw function) is
                                           proven good elsewhere in the same frame: the fullscreen
                                           map draws this exact texture fine, and the icon draws
                                           right after use the identical prologue/epilogue. Only
                                           this one combination (sub-rect texcoords on the
                                           subimage-updated minimap texture) came out black on the
                                           Android GLES1 driver. Sidestep it: draw the FULL map
                                           with plain 0..1 coords (the path that provably works)
                                           and clip the visible 128x128 window with scissoring,
                                           which is core GLES 1.1. Output is pixel-identical,
                                           except map edges now show the black backdrop instead of
                                           CLAMP_TO_EDGE smear, which arguably looks better. */
                                        float box_x = settings.window_width - 143 * scalef;
                                        float box_top = 585 * scalef;
                                        float box_size = 128 * scalef;

                                        glColor4f(1.0F, 1.0F, 1.0F, 1.0F);
                                        glEnable(GL_SCISSOR_TEST);
                                        glScissor((int)box_x, (int)(box_top - box_size), (int)ceil(box_size), (int)ceil(box_size));
                                        /* Scale the map texture by map_scale so the visible
                                           world area matches the icon/tracer math (which also
                                           use map_scale). Previously the texture was drawn at a
                                           fixed 512*scalef with a 64.0F half-offset, which only
                                           matched viewport=128 (map_scale=1) and caused icons
                                           to drift relative to the map at any other zoom. */
                                        texture_draw(&texture_minimap,
                                                                 box_x - (camera_x - half_vp) * map_scale * scalef,
                                                                 box_top + (camera_z - half_vp) * map_scale * scalef,
                                                                 512 * map_scale * scalef, 512 * map_scale * scalef);
                                        glDisable(GL_SCISSOR_TEST);

                                        int gl_err = glGetError();
                                        if(gl_err != 0)
                                                log_warn("minimap draw: glGetError() = 0x%04X", gl_err);
                                }

                                tracer_minimap(0, scalef, view_x, view_z, viewport);

                                if(gamestate.gamemode_type == GAMEMODE_CTF) {
                                        float tent1_x = min(max(gamestate.gamemode.ctf.team_1_base.x, view_x), view_x + viewport) - view_x;
                                        float tent1_y = min(max(gamestate.gamemode.ctf.team_1_base.y, view_z), view_z + viewport) - view_z;

                                        float tent2_x = min(max(gamestate.gamemode.ctf.team_2_base.x, view_x), view_x + viewport) - view_x;
                                        float tent2_y = min(max(gamestate.gamemode.ctf.team_2_base.y, view_z), view_z + viewport) - view_z;

                                        if(map_object_visible(gamestate.gamemode.ctf.team_1_base.x, 0.0F,
                                                                                  gamestate.gamemode.ctf.team_1_base.y)) {
                                                glColor3ub(gamestate.team_1.red * 0.94F, gamestate.team_1.green * 0.94F,
                                                                   gamestate.team_1.blue * 0.94F);
texture_draw_empty_rotated(settings.window_width - 143 * scalef + tent1_x * map_scale * scalef,
                           (585 - tent1_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                                texture_draw_rotated(&texture_medical, settings.window_width - 143 * scalef + tent1_x * map_scale * scalef,
                                                                                         (585 - tent1_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                        if(!gamestate.gamemode.ctf.team_1_intel) {
                                                float intel_x
                                                        = min(max(gamestate.gamemode.ctf.team_1_intel_location.dropped.x, view_x), view_x + viewport)
                                                        - view_x;
                                                float intel_y
                                                        = min(max(gamestate.gamemode.ctf.team_1_intel_location.dropped.y, view_z), view_z + viewport)
                                                        - view_z;
                                                glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
                                                texture_draw_rotated(&texture_intel, settings.window_width - 143 * scalef + intel_x * map_scale * scalef,
                                                                                         (585 - intel_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }

                                        if(map_object_visible(gamestate.gamemode.ctf.team_2_base.x, 0.0F,
                                                                                  gamestate.gamemode.ctf.team_2_base.y)) {
                                                glColor3ub(gamestate.team_2.red * 0.94F, gamestate.team_2.green * 0.94F,
                                                                   gamestate.team_2.blue * 0.94F);
texture_draw_empty_rotated(settings.window_width - 143 * scalef + tent2_x * map_scale * scalef,
                           (585 - tent2_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                                glColor3f(1.0F, 1.0F, 1.0F);
                                                texture_draw_rotated(&texture_medical, settings.window_width - 143 * scalef + tent2_x * map_scale * scalef,
                                                                                         (585 - tent2_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                        if(!gamestate.gamemode.ctf.team_2_intel) {
                                                float intel_x
                                                        = min(max(gamestate.gamemode.ctf.team_2_intel_location.dropped.x, view_x), view_x + viewport)
                                                        - view_x;
                                                float intel_y
                                                        = min(max(gamestate.gamemode.ctf.team_2_intel_location.dropped.y, view_z), view_z + viewport)
                                                        - view_z;
                                                glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
                                                texture_draw_rotated(&texture_intel, settings.window_width - 143 * scalef + intel_x * map_scale * scalef,
                                                                                         (585 - intel_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                }
                                if(gamestate.gamemode_type == GAMEMODE_TC) {
                                        for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++) {
                                                switch(gamestate.gamemode.tc.territory[k].team) {
                                                        case TEAM_1:
                                                                glColor3f(gamestate.team_1.red * 0.94F, gamestate.team_1.green * 0.94F,
                                                                                  gamestate.team_1.blue * 0.94F);
                                                                break;
                                                        case TEAM_2:
                                                                glColor3f(gamestate.team_2.red * 0.94F, gamestate.team_2.green * 0.94F,
                                                                                  gamestate.team_2.blue * 0.94F);
                                                                break;
                                                        default:
                                                        case TEAM_SPECTATOR: glColor3ub(0, 0, 0);
                                                }
                                                float t_x = min(max(gamestate.gamemode.tc.territory[k].x, view_x), view_x + viewport) - view_x;
                                                float t_y = min(max(gamestate.gamemode.tc.territory[k].y, view_z), view_z + viewport) - view_z;
                                                texture_draw_rotated(&texture_command, settings.window_width - 143 * scalef + t_x * map_scale * scalef,
                                                                                         (585 - t_y * map_scale) * scalef, 12 * scalef, 12 * scalef, 0.0F);
                                        }
                                }

                                for(int k = 0; k < PLAYERS_MAX; k++) {
                                        if(players[k].connected && players[k].alive
                                           && (players[k].team == players[local_player_id].team
                                                   || (camera_mode == CAMERAMODE_SPECTATOR
                                                           && (k == local_player_id || players[k].team != TEAM_SPECTATOR)))) {
                                                if(k == local_player_id) {
                                                        glColor3ub(0, 255, 255);
                                                } else {
                                                        switch(players[k].team) {
                                                                case TEAM_1:
                                                                        glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
                                                                        break;
                                                                case TEAM_2:
                                                                        glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
                                                                        break;
                                                        }
                                                }
                                                float player_x = ((k == local_player_id) ? camera_x : players[k].pos.x) - view_x;
                                                float player_y = ((k == local_player_id) ? camera_z : players[k].pos.z) - view_z;
                                                if(player_x >= 0.0F && player_x <= viewport && player_y >= 0.0F && player_y <= viewport) {
                                                        float ang = (k == local_player_id) ?
                                                                camera_rot_x + PI :
                                                                -atan2(players[k].orientation.z, players[k].orientation.x) - HALFPI;
                                                        texture_draw_rotated(&texture_player,
                                                                                                 settings.window_width - 143 * scalef + player_x * map_scale * scalef,
                                                                                                 (585 - player_y * map_scale) * scalef, 12 * scalef, 12 * scalef, ang);
                                                }
                                        }
                                }
                                glColor3f(1.0F, 1.0F, 1.0F);
                        }
                }

                struct Camera_HitType hit;
                camera_hit_fromplayer(&hit, local_player_id, 128.0F);

                if(hit.type == CAMERA_HITTYPE_PLAYER
                   && player_intersection_type >= 0
                   && (players[local_player_id].team == TEAM_SPECTATOR
                           || players[player_intersection_player].team == players[local_player_id].team)) {
                        font_select(FONT_FIXEDSYS);
                        char* th[4] = {"torso", "head", "arms", "legs"};
                        char str[32];
                        switch(players[player_intersection_player].team) {
                                case TEAM_1: glColor3ub(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue); break;
                                case TEAM_2: glColor3ub(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue); break;
                                default: glColor3f(1.0F, 1.0F, 1.0F);
                        }
                        sprintf(str, "%s's %s", players[player_intersection_player].name, th[player_intersection_type]);
                        font_centered(settings.window_width / 2.0F, settings.window_height * 0.2F, 16.0F, str);
                }

                if(window_time() - chat_popup_timer < chat_popup_duration) {
                        glColor3ub(red(chat_popup_color), green(chat_popup_color), blue(chat_popup_color));
                        font_centered(settings.window_width / 2.F, settings.window_height / 2.0F, 32.F, chat_popup);
                }
                glColor3f(1.0F, 1.0F, 1.0F);

                if(local_player_drag_active && local_player_drag_amount > 0) {
                        char drag_str[16];
                        font_select(FONT_FIXEDSYS);
                        sprintf(drag_str, "%i", local_player_drag_amount);
                        float cx = settings.window_width / 2.0F;
                        float cy = 50.F;
                        float tw = font_length(32.F, drag_str);
                        float sx = cx - tw / 2.0F;
                        glColor4f(0.F, 0.F, 0.F, 1.F);
                        font_render(sx - 1.F, cy, 32.F, drag_str);
                        font_render(sx + 1.F, cy, 32.F, drag_str);
                        font_render(sx, cy - 1.F, 32.F, drag_str);
                        font_render(sx, cy + 1.F, 32.F, drag_str);
                        glColor3ub(players[local_player_id].block.red,
                                           players[local_player_id].block.green,
                                           players[local_player_id].block.blue);
                        font_render(sx, cy, 32.F, drag_str);
                }
        }

        if(settings.show_fps) {
                mu_Color color = mu_accent_color(0.3F, settings.chat_shadow * 255);
                glColor4ub(color.r, color.g, color.b, color.a);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                texture_draw_empty(settings.window_width - 105.F, settings.window_height / 2.F - 18.F + 84.F, 100.F, 36.F);

                color = mu_accent_color(1.F, 255);
                glColor3ub(color.r, color.g, color.b);
                glLineWidth(3);
                glx_draw_line_2d(settings.window_width - 5.F, floor(settings.window_height / 2.F - 18.F + 84.F),
                                  settings.window_width - 5.F, floor(settings.window_height / 2.F - 18.F + 48.F));
                glLineWidth(1);
                glColor3ub(255, 255, 255);
                glDisable(GL_BLEND);

                char debug_str[16];
                font_select(FONT_FIXEDSYS);
                glColor3f(1.0F, 1.0F, 1.0F);
                sprintf(debug_str, "%ims", network_ping());
                font_render(settings.window_width - 17.0F - font_length(16.F, debug_str), settings.window_height / 2.F - 18.F + 82.F, 16.0F, debug_str);
                sprintf(debug_str, "%i fps", (int)fps);
                font_render(settings.window_width - 17.0F - font_length(16.F, debug_str), settings.window_height / 2.F - 18.F + 66.F, 16.0F, debug_str);
        }

#ifdef USE_TOUCH
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
        glColor3f(1.0F, 1.0F, 1.0F);
        if(camera_mode == CAMERAMODE_FPS || camera_mode == CAMERAMODE_SPECTATOR) {
                texture_draw_rotated(&texture_ui_joystick, settings.window_height * 0.3F, settings.window_height * 0.3F,
                                                         settings.window_height * 0.4F, settings.window_height * 0.4F, 0.0F);
                texture_draw_rotated(&texture_ui_knob, settings.window_height * 0.3F, settings.window_height * 0.3F,
                                                         settings.window_height * 0.075F, settings.window_height * 0.075F, 0.0F);
                texture_draw_rotated(&texture_ui_knob, hud_ingame_touch_x + settings.window_height * 0.3F,
                                                         hud_ingame_touch_y + settings.window_height * 0.3F, settings.window_height * 0.1F,
                                                         settings.window_height * 0.1F, 0.0F);
        }

        int k = 0;
        char str[128];
        while(hud_ingame_onscreencontrol(k, str, -1)) {
                texture_draw_rotated(&texture_ui_input, settings.window_height * (0.2F + 0.175F * k),
                                                         settings.window_height * 0.96F, settings.window_height * 0.15F,
                                                         settings.window_height * 0.1F, 0.0F);
                font_centered(settings.window_height * (0.2F + 0.175F * k), settings.window_height * 0.98F,
                                          settings.window_height * 0.04F, str);
                k++;
        }
        if(hud_ingame_onscreencontrol(64, str, -1)) {
                texture_draw_rotated(&texture_ui_input, settings.window_width - settings.window_height * 0.075F,
                                                         settings.window_height * 0.6F, settings.window_height * 0.15F,
                                                         settings.window_height * 0.1F, 0.0F);
                font_centered(settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.62F,
                                          settings.window_height * 0.04F, str);
        }
        if(hud_ingame_onscreencontrol(65, str, -1)) {
                texture_draw_rotated(&texture_ui_input, settings.window_width - settings.window_height * 0.075F,
                                                         settings.window_height * 0.45F, settings.window_height * 0.15F,
                                                         settings.window_height * 0.1F, 0.0F);
                font_centered(settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.47F,
                                          settings.window_height * 0.04F, str);
        }
        /* Spectator free-camera toggle, under the LMB/RMB plates. */
        if(hud_ingame_onscreencontrol(68, str, -1)) {
                texture_draw_rotated(&texture_ui_input, settings.window_width - settings.window_height * 0.075F,
                                                         settings.window_height * 0.3F, settings.window_height * 0.15F,
                                                         settings.window_height * 0.1F, 0.0F);
                font_centered(settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.32F,
                                          settings.window_height * 0.04F, str);
        }
        /* Jump + Crouch side by side directly below the left joystick (the
           joystick circle ends at 0.1 * window_height; these plates occupy the
           strip underneath it). Only shown alongside the joystick, i.e. when
           actually controlling a player or spectating in fly mode.
           Center moved from cy=0.05H up to cy=0.08H: after HIGHDPI the buttons
           sit in pixel space, and at cy=0.05H the bottom edge of the visible
           plate (y_gl=0) lands inside iOS's home-indicator edge-protection
           strip. Taps there are first-pass intercepted by the OS for the
           home gesture, so the bottom half of the button felt dead. The
           hit-test rect below is intentionally taller and wider than the
           visual rect for the same reason — see the matching is_inside_centered
           calls in hud_ingame_touch. */
        if(camera_mode == CAMERAMODE_FPS || camera_mode == CAMERAMODE_SPECTATOR) {
                /* Crouch left, Jump right -- mirroring the muscle memory of
                   CTRL (left) / Space (right) on a PC keyboard. */
                if(hud_ingame_onscreencontrol(67, str, -1)) {
                        texture_draw_rotated(&texture_ui_input, settings.window_height * 0.195F,
                                                                 settings.window_height * 0.08F, settings.window_height * 0.15F,
                                                                 settings.window_height * 0.1F, 0.0F);
                        font_centered(settings.window_height * 0.195F, settings.window_height * 0.10F,
                                                  settings.window_height * 0.04F, str);
                }
                if(hud_ingame_onscreencontrol(66, str, -1)) {
                        texture_draw_rotated(&texture_ui_input, settings.window_height * 0.405F,
                                                                 settings.window_height * 0.08F, settings.window_height * 0.15F,
                                                                 settings.window_height * 0.1F, 0.0F);
                        font_centered(settings.window_height * 0.405F, settings.window_height * 0.10F,
                                                  settings.window_height * 0.04F, str);
                }
        }
#endif
        demo_playback_render_overlay(scalef);
}

static void hud_ingame_scroll(double yoffset) {
        /* While the chat input is open, the scroll wheel pages through the
         * chat history instead of switching weapons. yoffset > 0 scrolls
         * toward older messages; yoffset < 0 scrolls back toward the newest. */
        if(chat_input_mode != CHAT_NO_INPUT && yoffset != 0.0F) {
                int max_offset = 127 - chat_messages;
                if(max_offset < 0) max_offset = 0;
                static float scroll_accum = 0.0F;
                scroll_accum += (yoffset > 0) ? 0.5F : -0.5F;
                int step = (int)scroll_accum;
                if(step != 0) {
                        scroll_accum -= step;
                        chat_scroll_offset += step;
                        if(chat_scroll_offset < 0) chat_scroll_offset = 0;
                        if(chat_scroll_offset > max_offset) chat_scroll_offset = max_offset;
                }
                return;
        }
        if(camera_mode == CAMERAMODE_FPS && yoffset != 0.0F) {
                int h = players[local_player_id].held_item;
                if(!players[local_player_id].items_show)
                        local_player_lasttool = h;
                h += (yoffset < 0) ? 1 : -1;
                if(h < 0)
                        h = 3;
                if(h == TOOL_BLOCK && local_player_blocks == 0)
                        h += (yoffset < 0) ? 1 : -1;
                if(h == TOOL_GUN && local_player_ammo + local_player_ammo_reserved == 0)
                        h += (yoffset < 0) ? 1 : -1;
                if(h == TOOL_GRENADE && local_player_grenades == 0)
                        h += (yoffset < 0) ? 1 : -1;
                if(h > 3)
                        h = 0;
                players[local_player_id].held_item = h;
                sound_create(SOUND_LOCAL, &sound_switch, 0.0F, 0.0F, 0.0F);
                player_on_held_item_change(players + local_player_id);
        }
}

static double last_x, last_y;
static void hud_ingame_mouselocation(double x, double y) {
        if(chat_input_mode != CHAT_NO_INPUT) {
                if(chat_drag_active) {
                        int off = chat_input_offset_at(x, y);
                        if(off >= 0) chat_cursor = off;
                }
                return;
        }
        if(show_exit) return;

        /* Skip first delta: cursor was free in another HUD. */
        if(mouse_seed_pending) {
                last_x = x;
                last_y = y;
                mouse_seed_pending = 0;
                return;
        }

        float dx = x - last_x;
        float dy = y - last_y;
        last_x = x;
        last_y = y;

        float s = 1.0F;
        if(camera_mode == CAMERAMODE_FPS && players[local_player_id].held_item == TOOL_GUN
           && players[local_player_id].input.buttons.rmb) {
                s = 0.5F;
        }

        if(settings.invert_y)
                dy *= -1.0F;

        // In spectator mode with roll, apply mouse movement relative to rolled camera orientation
        if(camera_mode == CAMERAMODE_SPECTATOR) {
                extern float cameracontroller_get_roll(void);
                float roll = cameracontroller_get_roll();
                
                // Rotate the mouse delta by the roll angle
                // When rolled 90°, mouse Y should affect yaw (left/right), not pitch
                float cos_roll = cos(roll);
                float sin_roll = sin(roll);
                
                // Transform mouse deltas: rotate by negative roll to get camera-relative movement
                float dx_rotated = dx * cos_roll + dy * sin_roll;
                float dy_rotated = dy * cos_roll - dx * sin_roll;
                
                camera_look_delta(dx_rotated * settings.mouse_sensitivity / 5.0F * (float)MOUSE_SENSITIVITY * s,
                                  dy_rotated * settings.mouse_sensitivity / 5.0F * (float)MOUSE_SENSITIVITY * s);
        } else {
                camera_look_delta(dx * settings.mouse_sensitivity / 5.0F * (float)MOUSE_SENSITIVITY * s,
                                  dy * settings.mouse_sensitivity / 5.0F * (float)MOUSE_SENSITIVITY * s);
        }
}

static void hud_switch_next_player() {
        float nearest_dist = FLT_MAX;
        int nearest_player = -1;
        for(int k = 0; k < PLAYERS_MAX; k++)
                if(player_can_spectate(&players[k]) && players[k].alive && k != cameracontroller_bodyview_player
                   && distance3D(camera_x, camera_y, camera_z, players[k].pos.x, players[k].pos.y, players[k].pos.z)
                           < nearest_dist) {
                        nearest_dist
                                = distance3D(camera_x, camera_y, camera_z, players[k].pos.x, players[k].pos.y, players[k].pos.z);
                        nearest_player = k;
                }
        if(nearest_player >= 0)
                cameracontroller_bodyview_player = nearest_player;
}

void hud_ingame_mouseclick(double x, double y, int button, int action, int mods) {
        if(chat_input_mode != CHAT_NO_INPUT) {
                if(button == WINDOW_MOUSE_LMB) {
                        if(action == WINDOW_PRESS) {
                                int off = chat_input_offset_at(x, y);
                                if(off >= 0) {
                                        chat_cursor = off;
                                        chat_sel_anchor = window_shift_down() ? chat_sel_anchor : off;
                                        if(chat_sel_anchor < 0) chat_sel_anchor = off;
                                        chat_drag_active = 1;
                                }
                        } else if(action == WINDOW_RELEASE) {
                                chat_drag_active = 0;
                                if(chat_sel_anchor == chat_cursor) chat_sel_clear();
                        }
                }
                return;
        }
        if(show_exit) return;

        if(button == WINDOW_MOUSE_LMB) {
                button_map[0] = (action == WINDOW_PRESS);
        }
        if(button == WINDOW_MOUSE_RMB) {
                if(action == WINDOW_PRESS && players[local_player_id].held_item == TOOL_GUN && !settings.hold_down_sights
                   && !players[local_player_id].items_show) {
                        int was_aiming = players[local_player_id].input.buttons.rmb;
                        players[local_player_id].input.buttons.rmb ^= 1;
                        if(players[local_player_id].input.buttons.rmb) {
                                players[local_player_id].input.buttons.rmb_start = window_time();
#ifdef USE_SOUND
                                sound_create(SOUND_LOCAL, &sound_zoomin, 0, 0, 0);
#endif
                        } else {
#ifdef USE_SOUND
                                sound_create(SOUND_LOCAL, &sound_zoomout, 0, 0, 0);
#endif
                        }
                }
                if(local_player_drag_active && action == WINDOW_RELEASE && players[local_player_id].held_item == TOOL_BLOCK) {
                        float ex, ey, ez;
                        camera_local_eye(&ex, &ey, &ez);
                        int* pos = camera_terrain_pick_local(0);
                        if(pos != NULL && pos[1] > 1
                           && chebyshev(pos[0] - ex, pos[1] - ey, pos[2] - ez) < 3.0F
                           && !overlaps_with_player(pos[0], pos[1], pos[2])) {
                                int amount = map_cube_line(local_player_drag_x, local_player_drag_z, 63 - local_player_drag_y, pos[0],
                                                                                   pos[2], 63 - pos[1], NULL);
                                if(amount > 0 && amount <= local_player_blocks) {
                                        struct PacketBlockLine line;
                                        line.player_id = local_player_id;
                                        line.sx = local_player_drag_x;
                                        line.sy = local_player_drag_z;
                                        line.sz = 63 - local_player_drag_y;
                                        line.ex = pos[0];
                                        line.ey = pos[2];
                                        line.ez = 63 - pos[1];
                                        network_send(PACKET_BLOCKLINE_ID, &line, sizeof(line));
                                        local_player_blocks -= amount;
                                }
                                players[local_player_id].item_showup = window_time();
                        }
                }
                local_player_drag_active = 0;
                if(action == WINDOW_PRESS && players[local_player_id].held_item == TOOL_BLOCK
                   && window_time() - players[local_player_id].item_showup >= 0.5F) {
                        float ex, ey, ez;
                        camera_local_eye(&ex, &ey, &ez);
                        int* pos = camera_terrain_pick_local(0);
                        if(pos != NULL && pos[1] > 1
                           && chebyshev(pos[0] - ex, pos[1] - ey, pos[2] - ez) < 3.0F
                           && !overlaps_with_player(pos[0], pos[1], pos[2])) {
                                local_player_drag_active = 1;
                                local_player_drag_x = pos[0];
                                local_player_drag_y = pos[1];
                                local_player_drag_z = pos[2];
                        }
                }
                button_map[1] = (action == WINDOW_PRESS);
        }
        if(button == WINDOW_MOUSE_MMB) {
                button_map[2] = (action == WINDOW_PRESS);
        }
        if(camera_mode == CAMERAMODE_BODYVIEW && button == WINDOW_MOUSE_MMB && action == WINDOW_PRESS) {
                hud_switch_next_player();
        }
        if(button == WINDOW_MOUSE_RMB && action == WINDOW_PRESS) {
                players[local_player_id].input.buttons.rmb_start = window_time();
                if(camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR) {
                        if(camera_mode == CAMERAMODE_SPECTATOR)
                                cameracontroller_bodyview_mode = 1;
                        int found = 0;
                        for(int k = 0; k < PLAYERS_MAX; k++) {
                                cameracontroller_bodyview_player = (cameracontroller_bodyview_player + 1) % PLAYERS_MAX;
                                // Validate cameracontroller_bodyview_player before accessing players array
                                if(cameracontroller_bodyview_player >= PLAYERS_MAX) {
                                        cameracontroller_bodyview_player = 0;
                                }
                                if(player_can_spectate(&players[cameracontroller_bodyview_player])) {
                                        found = 1;
                                        break;
                                }
                        }
                        // If no valid player found, disable bodyview mode
                        if(!found) {
                                cameracontroller_bodyview_mode = 0;
                        }
                        cameracontroller_bodyview_zoom = 0.0F;
                }
        }
        if(button == WINDOW_MOUSE_LMB) {
                if(camera_mode == CAMERAMODE_FPS && window_time() - players[local_player_id].item_showup >= 0.5F) {
                        if(players[local_player_id].held_item == TOOL_GRENADE && local_player_grenades > 0) {
                                if(action == WINDOW_RELEASE) {
                                        local_player_grenades = max(local_player_grenades - 1, 0);
                                        struct PacketGrenade g;
                                        g.player_id = local_player_id;
                                        g.fuse_length
                                                = max(3.0F - (window_time() - players[local_player_id].input.buttons.lmb_start), 0.0F);
                                        g.x = players[local_player_id].pos.x;
                                        g.y = players[local_player_id].pos.z;
                                        g.z = 63.0F - players[local_player_id].pos.y;
                                        g.vx = (g.fuse_length == 0.0F) ?
                                                0.0F :
                                                (players[local_player_id].orientation.x + players[local_player_id].physics.velocity.x);
                                        g.vy = (g.fuse_length == 0.0F) ?
                                                0.0F :
                                                (players[local_player_id].orientation.z + players[local_player_id].physics.velocity.z);
                                        g.vz = (g.fuse_length == 0.0F) ?
                                                0.0F :
                                                (-players[local_player_id].orientation.y - players[local_player_id].physics.velocity.y);
                                        network_send(PACKET_GRENADE_ID, &g, sizeof(g));
                                        read_PacketGrenade(&g, sizeof(g)); // server won't loop packet back
                                        players[local_player_id].item_showup = window_time();
                                }
                                if(action == WINDOW_PRESS) {
                                        sound_create(SOUND_LOCAL, &sound_grenade_pin, 0.0F, 0.0F, 0.0F);
                                }
                        }
                }
        }
        if(button == WINDOW_MOUSE_LMB && action == WINDOW_PRESS) {
                players[local_player_id].input.buttons.lmb_start = window_time();

                if(camera_mode == CAMERAMODE_FPS) {
                        if(players[local_player_id].held_item == TOOL_GUN) {
                                if(weapon_reloading()) {
                                        weapon_reload_abort();
                                }
                                if(local_player_ammo == 0 && window_time() - players[local_player_id].item_showup >= 0.5F) {
                                        sound_create(SOUND_LOCAL, &sound_empty, 0.0F, 0.0F, 0.0F);
                                        chat_showpopup("RELOAD", 0.4F, rgb(255, 0, 0));
                                }
                        }
                }

                if(camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR) {
                        if(camera_mode == CAMERAMODE_SPECTATOR)
                                cameracontroller_bodyview_mode = 1;
                        int found = 0;
                        for(int k = 0; k < PLAYERS_MAX; k++) {
                                cameracontroller_bodyview_player = (cameracontroller_bodyview_player - 1) % PLAYERS_MAX;
                                if(cameracontroller_bodyview_player < 0)
                                        cameracontroller_bodyview_player = PLAYERS_MAX - 1;
                                // Validate cameracontroller_bodyview_player before accessing players array
                                if(cameracontroller_bodyview_player >= PLAYERS_MAX) {
                                        cameracontroller_bodyview_player = 0;
                                }
                                if(player_can_spectate(&players[cameracontroller_bodyview_player])) {
                                        found = 1;
                                        break;
                                }
                        }
                        // If no valid player found, disable bodyview mode
                        if(!found) {
                                cameracontroller_bodyview_mode = 0;
                        }
                        cameracontroller_bodyview_zoom = 0.0F;
                }
        }
}

struct autocomplete_type {
        const char* str;
        int acceptance;
};

static int autocomplete_type_cmp(const void* a, const void* b) {
        struct autocomplete_type* aa = (struct autocomplete_type*)a;
        struct autocomplete_type* bb = (struct autocomplete_type*)b;
        return bb->acceptance - aa->acceptance;
}

static const char* hud_ingame_completeword(const char* s) {
        // find most likely player name or command

        struct autocomplete_type candidates[PLAYERS_MAX * 2 + 64] = {0};
        int candidates_cnt = 0;

        for(int k = 0; k < PLAYERS_MAX; k++) {
                if(players[k].connected)
                        candidates[candidates_cnt++] = (struct autocomplete_type) {
                                players[k].name,
                                0,
                        };
        }

        candidates[candidates_cnt++] = (struct autocomplete_type) {
                gamestate.team_1.name,
                0,
        };
        candidates[candidates_cnt++] = (struct autocomplete_type) {
                gamestate.team_2.name,
                0,
        };
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/help", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/medkit", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/squad", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/votekick", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/login", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/airstrike", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/streak", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/ratio", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/intel", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/time", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/admin", 0};
        candidates[candidates_cnt++] = (struct autocomplete_type) {"/ping", 0};

        // valuate all strings
        for(int k = 0; k < candidates_cnt; k++) {
                for(int i = 0; i < strlen(candidates[k].str) && i < strlen(s); i++) {
                        if(candidates[k].str[i] == s[i])
                                candidates[k].acceptance += 2;
                        else if(tolower(candidates[k].str[i]) == tolower(s[i]) || s[i] == '*') {
                                candidates[k].acceptance++;
                        } else {
                                candidates[k].acceptance = 0;
                                break;
                        }
                }
        }

        qsort(candidates, candidates_cnt, sizeof(struct autocomplete_type), autocomplete_type_cmp);
        return (strlen(candidates[0].str) > 0 && candidates[0].acceptance > 0) ? candidates[0].str : NULL;
}

static void hud_ingame_keyboard(int key, int action, int mods, int internal) {
        /* Key repeat: GLFW delivers GLFW_REPEAT events that the handler used
           to drop, so holding a key did nothing and actions felt like they
           landed on release.  Normalize repeat to press for the keys that
           are safe (and useful) to auto-repeat: palette cursor movement,
           color picking, and the chat text cursor. */
        if(action == WINDOW_REPEAT) {
                switch(key) {
                        case WINDOW_KEY_CURSOR_UP:
                        case WINDOW_KEY_CURSOR_DOWN:
                        case WINDOW_KEY_CURSOR_LEFT:
                        case WINDOW_KEY_CURSOR_RIGHT:
                        case WINDOW_KEY_PICKCOLOR:
                                action = WINDOW_PRESS;
                                break;
                        default:
                                break;
                }
        }
        if(action == WINDOW_PRESS && internal > 0) {
                for(int i = 0; i < list_size(&config_macros); i++) {
                        struct config_macro* m = list_get(&config_macros, i);
                        if(internal == m->key && m->text[0]) {
                                struct PacketChatMessage msg;
                                msg.player_id = local_player_id;
                                msg.chat_type = CHAT_ALL;
                                strncpy(msg.message, m->text, sizeof(msg.message) - 1);
                                msg.message[sizeof(msg.message) - 1] = '\0';
                                for(size_t i = 0; msg.message[i]; i++)
                                        if(msg.message[i] == '\n') msg.message[i] = ' ';
                                network_send(PACKET_CHATMESSAGE_ID, &msg,
                                        sizeof(msg) - sizeof(msg.message) + strlen(m->text) + 1);
                                sound_create(SOUND_LOCAL, &sound_chat, 0.0F, 0.0F, 0.0F);
                                chat_add(2, 0, m->text);
                                return;
                        }
                }
        }
        if(chat_input_mode != CHAT_NO_INPUT && action == WINDOW_PRESS && key == WINDOW_KEY_TAB && strlen(chat[0][0]) > 0) {
                // autocomplete word
                char* incomplete = strrchr(chat[0][0], ' ') + 1;
                if(incomplete == (char*)1)
                        incomplete = chat[0][0];
                const char* match = hud_ingame_completeword(incomplete);
                if(match && strlen(match) + strlen(chat[0][0]) < 128)
                        strcpy(incomplete, match);
        }

        if(chat_input_mode == CHAT_NO_INPUT) {
                if(action == WINDOW_PRESS) {
                        if(demo_is_playing()) {
                                if(key == WINDOW_KEY_DEMO_PAUSE) { demo_playback_toggle_pause(); return; }
                                if(key == WINDOW_KEY_DEMO_SEEK_BACK) { demo_playback_seek(DemoPlaybackState.current_time - 10.0f); return; }
                                if(key == WINDOW_KEY_DEMO_SEEK_FWD) { demo_playback_seek(DemoPlaybackState.current_time + 10.0f); return; }
                                if(key == WINDOW_KEY_DEMO_SPEED_DOWN) { demo_playback_set_speed(DemoPlaybackState.speed * 0.5f); return; }
                                if(key == WINDOW_KEY_DEMO_SPEED_UP) { demo_playback_set_speed(DemoPlaybackState.speed * 2.0f); return; }
                        }
                        if(!network_connected) {
                                if(key == WINDOW_KEY_F1) {
                                        camera_mode = CAMERAMODE_SELECTION;
                                }
                                if(key == WINDOW_KEY_F2) {
                                        camera_mode = CAMERAMODE_FPS;
                                }
                                if(key == WINDOW_KEY_F3) {
                                        camera_mode = CAMERAMODE_SPECTATOR;
                                        cameracontroller_reset_spectator_velocity();
                                }
                                if(key == WINDOW_KEY_F4) {
                                        camera_mode = CAMERAMODE_BODYVIEW;
                                }
                                if(key == WINDOW_KEY_SNEAK) {
                                        log_debug("%f,%f,%f,%f,%f", camera_x, camera_y, camera_z, camera_rot_x, camera_rot_y);
                                        players[local_player_id].pos.x = 256.0F;
                                        players[local_player_id].pos.y = 63.0F;
                                        players[local_player_id].pos.z = 256.0F;
                                        player_prev_snap(local_player_id);
                                }
                        }

                        if(key == WINDOW_KEY_NO || (show_exit && key == WINDOW_KEY_ESCAPE)) {
                                show_exit = 0;
                                window_mousemode(WINDOW_CURSOR_DISABLED);
                                return;
                        } else if(key == WINDOW_KEY_YES) {
                                if(show_exit) {
                                        hud_change(&hud_serverlist);
                                } else {
                                        window_textinput(1);
                                        chat_input_mode = CHAT_TEAM_INPUT;
                                        chat_scroll_offset = 0;
                                }
                        }

                        if(show_exit) {
                                return;
                        }

                        if(key == WINDOW_KEY_LASTTOOL) {
                                int tmp = players[local_player_id].held_item;
                                players[local_player_id].held_item = local_player_lasttool;
                                local_player_lasttool = tmp;
                                player_on_held_item_change(players + local_player_id);
                        }

                        if(key == WINDOW_KEY_VOLUME_UP) {
                                settings.volume = min(settings.volume + 1, 10);
                        }
                        if(key == WINDOW_KEY_VOLUME_DOWN) {
                                settings.volume = max(settings.volume - 1, 0);
                        }
                        if(key == WINDOW_KEY_VOLUME_UP || key == WINDOW_KEY_VOLUME_DOWN) {
                                sound_volume(settings.volume / 10.0F);
                                char volstr[64];
                                sprintf(volstr, "Volume: %i", settings.volume);
                                chat_add(0, 0x00FFFF, volstr);
                        }

                        if(key == WINDOW_KEY_MAP_ZOOM) {
                                settings.minimap_zoom++;
                                if(settings.minimap_zoom > 5)
                                        settings.minimap_zoom = 1;
                        }

                        if(key == WINDOW_KEY_COMMAND) {
                                window_textinput(1);
                                chat_input_mode = CHAT_ALL_INPUT;
                                chat_scroll_offset = 0;
                                strcpy(chat[0][0], "/");
                                /* Cursor must follow the prefilled "/", otherwise stale
                                   chat_cursor (typically 0) places typed chars before the
                                   slash, producing "kill/" instead of "/kill". */
                                chat_cursor = (int)strlen(chat[0][0]);
                        }

                        if(key == WINDOW_KEY_CHAT) {
                                window_textinput(1);
                                chat_input_mode = CHAT_ALL_INPUT;
                                chat_scroll_offset = 0;
                        }

                        if((key == WINDOW_KEY_CURSOR_UP || key == WINDOW_KEY_CURSOR_DOWN || key == WINDOW_KEY_CURSOR_LEFT
                                || key == WINDOW_KEY_CURSOR_RIGHT)
                           && camera_mode == CAMERAMODE_FPS && players[local_player_id].held_item == TOOL_BLOCK) {
                                int y;
                                for(y = 0; y < 8; y++) {
                                        for(int x = 0; x < 8; x++) {
                                                if(texture_block_color(x, y) == players[local_player_id].block.packed) {
                                                        switch(key) {
                                                                case WINDOW_KEY_CURSOR_LEFT:
                                                                        x--;
                                                                        if(x < 0)
                                                                                x = 7;
                                                                        break;
                                                                case WINDOW_KEY_CURSOR_RIGHT:
                                                                        x++;
                                                                        if(x > 7)
                                                                                x = 0;
                                                                        break;
                                                                case WINDOW_KEY_CURSOR_UP:
                                                                        y--;
                                                                        if(y < 0)
                                                                                y = 7;
                                                                        break;
                                                                case WINDOW_KEY_CURSOR_DOWN:
                                                                        y++;
                                                                        if(y > 7)
                                                                                y = 0;
                                                                        break;
                                                        }
                                                        players[local_player_id].block.packed = texture_block_color(x, y);
                                                        network_updateColor();
                                                        y = 10;
                                                        break;
                                                }
                                        }
                                }
                if(y < 10) {
                                players[local_player_id].block.packed = texture_block_color(3, 0);
                                network_updateColor();
                        }
                } else if((key == WINDOW_KEY_CURSOR_UP || key == WINDOW_KEY_CURSOR_DOWN || key == WINDOW_KEY_CURSOR_LEFT
                                   || key == WINDOW_KEY_CURSOR_RIGHT)
                                  && camera_mode == CAMERAMODE_SPECTATOR) {
                        int py;
                        unsigned int cur = rgb((int)(fog_color[0] * 255.0F + 0.5F), (int)(fog_color[1] * 255.0F + 0.5F), (int)(fog_color[2] * 255.0F + 0.5F));
                        for(py = 0; py < 8; py++) {
                                for(int px = 0; px < 8; px++) {
                                        if(texture_block_color(px, py) == cur) {
                                                switch(key) {
                                                        case WINDOW_KEY_CURSOR_LEFT:
                                                                px--;
                                                                if(px < 0) px = 7;
                                                                break;
                                                        case WINDOW_KEY_CURSOR_RIGHT:
                                                                px++;
                                                                if(px > 7) px = 0;
                                                                break;
                                                        case WINDOW_KEY_CURSOR_UP:
                                                                py--;
                                                                if(py < 0) py = 7;
                                                                break;
                                                        case WINDOW_KEY_CURSOR_DOWN:
                                                                py++;
                                                                if(py > 7) py = 0;
                                                                break;
                                                }
                                                cur = texture_block_color(px, py);
                                                fog_color[0] = (cur & 0xFF) / 255.0F;
                                                fog_color[1] = ((cur >> 8) & 0xFF) / 255.0F;
                                                fog_color[2] = ((cur >> 16) & 0xFF) / 255.0F;
                                                spec_color_palette_time = window_time() + 2.0;
                                                py = 10;
                                                break;
                                        }
                                }
                        }
                        if(py < 10) {
                                cur = texture_block_color(3, 0);
                                fog_color[0] = (cur & 0xFF) / 255.0F;
                                fog_color[1] = ((cur >> 8) & 0xFF) / 255.0F;
                                fog_color[2] = ((cur >> 16) & 0xFF) / 255.0F;
                                spec_color_palette_time = window_time() + 2.0;
                        }
                }

                        if(key == WINDOW_KEY_RELOAD && camera_mode == CAMERAMODE_FPS
                           && players[local_player_id].held_item == TOOL_GUN) {
                                weapon_reload();
                        }

                        if(key == WINDOW_KEY_SWITCH_CAMERA && (camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR)) {
                                cameracontroller_bodyview_mode = !cameracontroller_bodyview_mode;
                        }

                        if(key == WINDOW_KEY_NEXT_PLAYER && camera_mode == CAMERAMODE_BODYVIEW) {
                                hud_switch_next_player();
                        }

                        if(screen_current == SCREEN_NONE && camera_mode == CAMERAMODE_FPS) {
                                unsigned char tool_switch = 0;
                                switch(key) {
                                        case WINDOW_KEY_TOOL1:
                                                if(players[local_player_id].held_item != TOOL_SPADE) {
                                                        local_player_lasttool = players[local_player_id].held_item;
                                                        players[local_player_id].held_item = TOOL_SPADE;
                                                        tool_switch = 1;
                                                }
                                                break;
                                        case WINDOW_KEY_TOOL2:
                                                if(players[local_player_id].held_item != TOOL_BLOCK) {
                                                        local_player_lasttool = players[local_player_id].held_item;
                                                        players[local_player_id].held_item = TOOL_BLOCK;
                                                        tool_switch = 1;
                                                }
                                                break;
                                        case WINDOW_KEY_TOOL3:
                                                if(players[local_player_id].held_item != TOOL_GUN) {
                                                        local_player_lasttool = players[local_player_id].held_item;
                                                        players[local_player_id].held_item = TOOL_GUN;
                                                        tool_switch = 1;
                                                }
                                                break;
                                        case WINDOW_KEY_TOOL4:
                                                if(players[local_player_id].held_item != TOOL_GRENADE) {
                                                        local_player_lasttool = players[local_player_id].held_item;
                                                        players[local_player_id].held_item = TOOL_GRENADE;
                                                        tool_switch = 1;
                                                }
                                                break;
                                }

                                if(tool_switch) {
                                        sound_create(SOUND_LOCAL, &sound_switch, 0.0F, 0.0F, 0.0F);
                                        player_on_held_item_change(players + local_player_id);
                                        /* Direct number-key selection is deliberate:
                                           don't flash the rotating tool-switch
                                           overlay (it only appears when switching
                                           via the scroll wheel / Q, which is the
                                           discoverable path).  The item_showup
                                           cooldown set above is kept, so the new
                                           tool still can't fire instantly. */
                                        players[local_player_id].items_show = 0;
                                }
                        }

                        if(screen_current == SCREEN_NONE) {
                                if(key == WINDOW_KEY_CHANGETEAM) {
                                        screen_current = SCREEN_TEAM_SELECT;
                                        return;
                                }
                                if(key == WINDOW_KEY_CHANGEWEAPON) {
                                        screen_current = SCREEN_GUN_SELECT;
                                        return;
                                }
                        }

                        if(screen_current == SCREEN_TEAM_SELECT) {
                                int new_team = 256;
                                int anonymous_spectator = 0;
                                switch(key) {
                                        case WINDOW_KEY_SELECT1: new_team = TEAM_1; break;
                                        case WINDOW_KEY_SELECT2: new_team = TEAM_2; break;
                                        case WINDOW_KEY_SELECT3: new_team = TEAM_SPECTATOR; break;
                                        case WINDOW_KEY_SELECT4:
                                                new_team = TEAM_SPECTATOR;
                                                anonymous_spectator = !network_logged_in;
                                                break;
                                }
                                if(new_team <= 255) {
                                        if(network_logged_in) {
                                                struct PacketChangeTeam p;
                                                p.player_id = local_player_id;
                                                p.team = new_team;
                                                network_send(PACKET_CHANGETEAM_ID, &p, sizeof(p));
                                                // If switching from spectator to a team, show weapon select
                                                if(players[local_player_id].team == TEAM_SPECTATOR && new_team != TEAM_SPECTATOR) {
                                                        screen_current = SCREEN_GUN_SELECT;
                                                } else {
                                                        screen_current = SCREEN_NONE;
                                                }
                                                return;
                                        } else {
                                                local_player_newteam = new_team;
                                                if(new_team == TEAM_SPECTATOR) {
                                                        struct PacketExistingPlayer login;
                                                        login.player_id = local_player_id;
                                                        login.team = local_player_newteam;
                                                        login.weapon = WEAPON_RIFLE;
                                                        login.held_item = TOOL_GUN;
                                                        login.kills = 0;
                                                        login.blue = players[local_player_id].block.blue;
                                                        login.green = players[local_player_id].block.green;
                                                        login.red = players[local_player_id].block.red;
                                                        const char* join_name = anonymous_spectator ? "Anonymous" : settings.name;
                                                        strcpy(login.name, join_name);
                                                        network_send(PACKET_EXISTINGPLAYER_ID, &login,
                                                                                 sizeof(login) - sizeof(login.name) + strlen(join_name) + 1);
                                                        screen_current = SCREEN_NONE;
                                                } else {
                                                        screen_current = SCREEN_GUN_SELECT;
                                                }
                                                return;
                                        }
                                }
                                if((key == WINDOW_KEY_CHANGETEAM || key == WINDOW_KEY_ESCAPE)
                                   && (!network_connected || (network_connected && network_logged_in))) {
                                        screen_current = SCREEN_NONE;
                                        return;
                                }
                        }
                        if(screen_current == SCREEN_GUN_SELECT) {
                                int new_gun = 255;
                                switch(key) {
                                        case WINDOW_KEY_SELECT1: new_gun = WEAPON_RIFLE; break;
                                        case WINDOW_KEY_SELECT2: new_gun = WEAPON_SMG; break;
                                        case WINDOW_KEY_SELECT3: new_gun = WEAPON_SHOTGUN; break;
                                }
                                if(new_gun < 255) {
                                        if(network_logged_in) {
                                                struct PacketChangeWeapon p;
                                                p.player_id = local_player_id;
                                                p.weapon = new_gun;
                                                network_send(PACKET_CHANGEWEAPON_ID, &p, sizeof(p));
                                        } else {
                                                struct PacketExistingPlayer login;
                                                login.player_id = local_player_id;
                                                login.team = local_player_newteam;
                                                login.weapon = new_gun;
                                                login.held_item = TOOL_GUN;
                                                login.kills = 0;
                                                login.blue = players[local_player_id].block.blue;
                                                login.green = players[local_player_id].block.green;
                                                login.red = players[local_player_id].block.red;
                                                strcpy(login.name, settings.name);
                                                network_send(PACKET_EXISTINGPLAYER_ID, &login,
                                                                         sizeof(login) - sizeof(login.name) + strlen(settings.name) + 1);
                                        }
                                        screen_current = SCREEN_NONE;
                                        return;
                                }
                                if((key == WINDOW_KEY_CHANGEWEAPON || key == WINDOW_KEY_ESCAPE)
                                   && (!network_connected || (network_connected && network_logged_in))) {
                                        screen_current = SCREEN_NONE;
                                        return;
                                }
                        }

                        if(key == WINDOW_KEY_ESCAPE) {
                                if(network_map_transfer) {
                                        hud_change(&hud_serverlist);
                                        return;
                                }

                                show_exit ^= 1;
                                if(show_exit) {
                                        hud_change(&hud_settings);
                                }

                                window_mousemode(show_exit ? WINDOW_CURSOR_ENABLED : WINDOW_CURSOR_DISABLED);
                                return;
                        }

                        if(players[local_player_id].team == TEAM_SPECTATOR && key == WINDOW_KEY_YCLAMP) {
                                cameracontroller_yclamp ^= 1;
                        }

                        if(key == WINDOW_KEY_PICKCOLOR && players[local_player_id].held_item == TOOL_BLOCK) {
                                players[local_player_id].item_disabled = window_time();
                                /* No tool-switch overlay here: copying a color
                                   is not a tool change, and showing the overlay
                                   made it peek through the colour palette. */

                                struct Camera_HitType hit;
                                camera_hit_fromplayer(&hit, local_player_id, 128.0F);

                                switch(hit.type) {
                                        case CAMERA_HITTYPE_BLOCK:
                                                players[local_player_id].block.packed = map_get(hit.x, hit.y, hit.z);
                                                float dmg = (100.0F - map_damage_get(hit.x, hit.y, hit.z)) / 100.0F * 0.75F + 0.25F;
                                                players[local_player_id].block.red *= dmg;
                                                players[local_player_id].block.green *= dmg;
                                                players[local_player_id].block.blue *= dmg;
                                                break;
                                        case CAMERA_HITTYPE_PLAYER:
                                                players[local_player_id].block.packed = players[hit.player_id].block.packed;
                                                break;
                                        case CAMERA_HITTYPE_NONE:
                                        default:
                                                players[local_player_id].block.red = fog_color[0] * 255.0F + 0.5F;
                                                players[local_player_id].block.green = fog_color[1] * 255.0F + 0.5F;
                                                players[local_player_id].block.blue = fog_color[2] * 255.0F + 0.5F;
                                                break;
                                }
                                network_updateColor();
                        }
                }
        } else {
                if(action != WINDOW_RELEASE) {
                        int shift_held = window_shift_down();

                        if(key == WINDOW_KEY_V && mods) {
                                const char* clipboard = window_clipboard();
                                if(clipboard) {
                                        chat_sel_delete();
                                        chat_cursor_clamp();
                                        size_t len = strlen(chat[0][0]);
                                        size_t cap = sizeof(chat[0][0]);
                                        size_t paste_len = strlen(clipboard);
                                        size_t room = (len < cap - 1) ? (cap - 1 - len) : 0;
                                        if(paste_len > room) paste_len = room;
                                        memmove(chat[0][0] + chat_cursor + paste_len,
                                                        chat[0][0] + chat_cursor,
                                                        len - chat_cursor + 1);
                                        for(size_t i = 0; i < paste_len; i++) {
                                                char c = clipboard[i];
                                                if(c == '\r') c = '\n';
                                                else if(c == '\t') c = ' ';
                                                chat[0][0][chat_cursor + i] = c;
                                        }
                                        chat_cursor += (int)paste_len;
                                        chat_sel_clear();
                                }
                                return;
                        }

                        if(key == WINDOW_KEY_C && mods) {
                                chat_sel_copy_to_clipboard();
                                return;
                        }

#ifdef USE_SDL
                        if(internal == SDLK_x && mods) {
#else
                        if(internal == 88 /* X */ && mods) {
#endif
                                if(chat_sel_active()) {
                                        chat_sel_copy_to_clipboard();
                                        chat_sel_delete();
                                }
                                return;
                        }

#ifdef USE_SDL
                        if(internal == SDLK_a && mods) {
#else
                        if(internal == 65 /* A */ && mods) {
#endif
                                int len = (int)strlen(chat[0][0]);
                                chat_sel_anchor = 0;
                                chat_cursor = len;
                                return;
                        }

                        if(key == WINDOW_KEY_HISTORY_PREVIOUS) {
                                float avail_w = (float)settings.window_width - 11.0F - 16.0F;
                                int rs[CHAT_INPUT_MAX_ROWS], rl[CHAT_INPUT_MAX_ROWS];
                                int rows = chat_wrap(avail_w, rs, rl, CHAT_INPUT_MAX_ROWS);
                                int cr = 0, cc = 0;
                                chat_cursor_to_rowcol(rs, rl, rows, &cr, &cc);
                                if(cr == 0) {
                                        if(chat_history_pos < 127) {
                                                strcpy(chat[0][0], chat[2][++chat_history_pos]);
                                                chat_cursor = (int)strlen(chat[0][0]);
                                                chat_sel_clear();
                                        }
                                } else {
                                        int dst = cr - 1;
                                        int target = cc; if(target > rl[dst]) target = rl[dst];
                                        chat_cursor_move_with_shift(rs[dst] + target, shift_held);
                                }
                        }

                        if(key == WINDOW_KEY_HISTORY_NEXT) {
                                float avail_w = (float)settings.window_width - 11.0F - 16.0F;
                                int rs[CHAT_INPUT_MAX_ROWS], rl[CHAT_INPUT_MAX_ROWS];
                                int rows = chat_wrap(avail_w, rs, rl, CHAT_INPUT_MAX_ROWS);
                                int cr = 0, cc = 0;
                                chat_cursor_to_rowcol(rs, rl, rows, &cr, &cc);
                                if(cr >= rows - 1) {
                                        if(chat_history_pos > 0) {
                                                strcpy(chat[0][0], chat[2][--chat_history_pos]);
                                                chat_cursor = (int)strlen(chat[0][0]);
                                                chat_sel_clear();
                                        }
                                } else {
                                        int dst = cr + 1;
                                        int target = cc; if(target > rl[dst]) target = rl[dst];
                                        chat_cursor_move_with_shift(rs[dst] + target, shift_held);
                                }
                        }

                        if(key == WINDOW_KEY_CURSOR_LEFT) {
                                if(chat_sel_active() && !shift_held) {
                                        int lo, hi;
                                        chat_sel_range(&lo, &hi);
                                        chat_cursor = lo;
                                        chat_sel_clear();
                                } else {
                                        int new_cur = chat_cursor;
                                        if(new_cur > 0) {
                                                new_cur--;
                                                while(new_cur > 0
                                                          && ((unsigned char)chat[0][0][new_cur] & 0xC0) == 0x80)
                                                        new_cur--;
                                        }
                                        chat_cursor_move_with_shift(new_cur, shift_held);
                                }
                        }
                        if(key == WINDOW_KEY_CURSOR_RIGHT) {
                                if(chat_sel_active() && !shift_held) {
                                        int lo, hi;
                                        chat_sel_range(&lo, &hi);
                                        chat_cursor = hi;
                                        chat_sel_clear();
                                } else {
                                        int len = (int)strlen(chat[0][0]);
                                        int new_cur = chat_cursor;
                                        if(new_cur < len) {
                                                new_cur++;
                                                while(new_cur < len
                                                          && ((unsigned char)chat[0][0][new_cur] & 0xC0) == 0x80)
                                                        new_cur++;
                                        }
                                        chat_cursor_move_with_shift(new_cur, shift_held);
                                }
                        }
                        if(key == WINDOW_KEY_UNKNOWN) {
                                int len = (int)strlen(chat[0][0]);
                                switch(internal) {
                                        case 268: chat_cursor_move_with_shift(0,   shift_held); break; /* Home */
                                        case 269: chat_cursor_move_with_shift(len, shift_held); break; /* End  */
                                        case 261: { /* Delete */
                                                if(!chat_sel_delete() && chat_cursor < len) {
                                                        int del_end = chat_cursor + 1;
                                                        while(del_end < len
                                                                  && ((unsigned char)chat[0][0][del_end] & 0xC0) == 0x80)
                                                                del_end++;
                                                        memmove(chat[0][0] + chat_cursor,
                                                                        chat[0][0] + del_end,
                                                                        len - del_end + 1);
                                                }
                                                break;
                                        }
                                }
                        }

                        if(key == WINDOW_KEY_ESCAPE || key == WINDOW_KEY_ENTER) {
                                chat_history_pos = 0;
                                if(key == WINDOW_KEY_ENTER && strlen(chat[0][0]) > 0) {
                                        struct PacketChatMessage msg;
                                        msg.player_id = local_player_id;
                                        msg.chat_type = (chat_input_mode == CHAT_ALL_INPUT) ? CHAT_ALL : CHAT_TEAM;
                                        strncpy(msg.message, chat[0][0], sizeof(msg.message) - 1);
                                        msg.message[sizeof(msg.message) - 1] = '\0';
                                        for(size_t i = 0; msg.message[i]; i++)
                                                if(msg.message[i] == '\n') msg.message[i] = ' ';
                                        network_send(PACKET_CHATMESSAGE_ID, &msg,
                                                                 sizeof(msg) - sizeof(msg.message) + strlen(chat[0][0]) + 1);
                                        sound_create(SOUND_LOCAL, &sound_chat, 0.0F, 0.0F, 0.0F);
                                        chat_add(2, 0, chat[0][0]);
                                }
                                window_textinput(0);
                                chat_input_mode = CHAT_NO_INPUT;
                                chat_scroll_offset = 0;
                                chat[0][0][0] = 0;
                                chat_cursor = 0;
                                chat_input_rows = 1;
                                chat_sel_clear();
                        }
                        if(key == WINDOW_KEY_BACKSPACE) {
                                chat_cursor_clamp();
                                if(chat_sel_delete()) {
                                        /* selection consumed the keystroke */
                                } else if(chat_cursor > 0) {
                                        int del_start = chat_cursor - 1;
                                        while(del_start > 0
                                                  && ((unsigned char)chat[0][0][del_start] & 0xC0) == 0x80)
                                                del_start--;
                                        size_t len = strlen(chat[0][0]);
                                        memmove(chat[0][0] + del_start,
                                                        chat[0][0] + chat_cursor,
                                                        len - chat_cursor + 1);
                                        chat_cursor = del_start;
                                }
                        }
                }
        }
}

static void hud_ingame_touch(void* finger, int action, float x, float y, float dx, float dy) {
        window_setmouseloc(x, y);
        struct window_finger* f = (struct window_finger*)finger;

#ifdef USE_TOUCH
        if(demo_is_playing()) {
                float scalef = settings.window_height / 600.0f;
                float bar_w  = settings.window_width * 0.6f;
                float bar_h  = 8.0f * scalef;
                float bar_x  = (settings.window_width - bar_w) * 0.5f;
                float bar_y  = settings.window_height - 28.0f * scalef;
                float text_h = 13.0f * scalef;

                /* bar_y is the OpenGL top edge; in touch/screen coords (y=0 at top):
                   bar occupies [bar_top_sc, bar_bot_sc], text row sits below that. */
                float bar_top_sc  = settings.window_height - bar_y;
                float bar_bot_sc  = bar_top_sc + bar_h;
                float text_row_sc = bar_bot_sc + 18.0f * scalef;
                float hit_pad     = 14.0f * scalef;

                /* Update scrub target on MOVE/UP; only actually seek on UP so
                   backward seeks (which require a full world reset + replay) fire
                   once, and forward seeks don't trigger repeated chunk rebuilds. */
                if(demo_scrub_finger == finger && action != TOUCH_DOWN) {
                        float t = (x - bar_x) / bar_w * DemoPlaybackState.duration;
                        if(t < 0.0f) t = 0.0f;
                        if(t > DemoPlaybackState.duration) t = DemoPlaybackState.duration;
                        demo_scrub_target = t;
                        if(action == TOUCH_UP) {
                                demo_playback_seek(demo_scrub_target);
                                demo_scrub_finger = NULL;
                        }
                        return;
                }

                if(action == TOUCH_DOWN) {
                        /* Bar: start scrubbing. */
                        if(f->start.x >= bar_x && f->start.x <= bar_x + bar_w
                           && f->start.y >= bar_top_sc - hit_pad
                           && f->start.y <= bar_bot_sc + hit_pad) {
                                demo_scrub_finger = finger;
                                float t = (f->start.x - bar_x) / bar_w * DemoPlaybackState.duration;
                                if(t < 0.0f) t = 0.0f;
                                if(t > DemoPlaybackState.duration) t = DemoPlaybackState.duration;
                                demo_scrub_target = t;
                                return;
                        }

                        /* Text row: speed label (left) and pause/play/restart button (right). */
                        if(f->start.x >= bar_x && f->start.x <= bar_x + bar_w
                           && f->start.y >= text_row_sc - hit_pad
                           && f->start.y <= text_row_sc + hit_pad) {

                                char speed_buf[8];
                                snprintf(speed_buf, sizeof(speed_buf), "%.4gx",
                                         (double)DemoPlaybackState.speed);
                                float speed_w = font_length(text_h, speed_buf) + 8.0f * scalef;

                                const char* btn = DemoPlaybackState.finished ? "|<" :
                                                  DemoPlaybackState.paused   ? "|>" : "||";
                                float btn_w = font_length(text_h, btn) + 8.0f * scalef;

                                if(f->start.x <= bar_x + speed_w) {
                                        /* Cycle speed: 0.25 -> 0.5 -> 1 -> 2 -> 4 -> 0.25 */
                                        static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
                                        int n = (int)(sizeof(speeds) / sizeof(speeds[0]));
                                        int idx = 0;
                                        for(int i = 0; i < n; i++) {
                                                if(fabsf(DemoPlaybackState.speed - speeds[i]) < 0.01f) {
                                                        idx = i;
                                                        break;
                                                }
                                        }
                                        demo_playback_set_speed(speeds[(idx + 1) % n]);
                                        return;
                                }

                                if(f->start.x >= bar_x + bar_w - btn_w) {
                                        if(DemoPlaybackState.finished)
                                                demo_playback_seek(0.0f);
                                        else
                                                demo_playback_toggle_pause();
                                        return;
                                }
                        }
                }
        }
#endif

        /* Selection overlays take priority over the on-screen controls below:
           while one is open the screen is divided into thirds (left / middle /
           right tap targets), and the control hit tests must not intercept and
           consume those taps first.

           A selection only fires on a TOUCH_UP whose TOUCH_DOWN this overlay saw
           itself. The top-bar buttons act on TOUCH_DOWN, so the very tap that
           opens the overlay would otherwise deliver its TOUCH_UP here and
           instantly "select" whatever third the button happens to sit in. */
        static void* overlay_down_finger = NULL;
        if(screen_current == SCREEN_TEAM_SELECT || screen_current == SCREEN_GUN_SELECT) {
                int gun = (screen_current == SCREEN_GUN_SELECT);

                /* The top button row stays live while the overlay is open so that
                   tapping Team/Weapon again toggles the popup closed (those buttons
                   send WINDOW_KEY_CHANGETEAM/-WEAPON, which the keyboard handler
                   treats as "close"). Such a tap is a button press, not a thirds
                   selection. */
                if(action != TOUCH_MOVE) {
                        int k = 0;
                        while(hud_ingame_onscreencontrol(k, NULL, -1)) {
                                if(is_inside_centered(f->start.x, settings.window_height - f->start.y,
                                                                          settings.window_height * (0.2F + 0.175F * k),
                                                                          settings.window_height * 0.96F,
                                                                          settings.window_height * 0.15F, settings.window_height * 0.1F)) {
                                        overlay_down_finger = NULL;
                                        hud_ingame_onscreencontrol(k, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                                        return;
                                }
                                k++;
                        }
                }

                if(action == TOUCH_DOWN)
                        overlay_down_finger = finger;
                if(action == TOUCH_UP && finger == overlay_down_finger) {
                        overlay_down_finger = NULL;
                        /* The three visible anchors (labels + 3D models) sit in columns
                           centered at 1/4, 1/2 and 3/4 of the window width, but the model
                           offsets are a *perspective* projection: they scale with window
                           HEIGHT and the FOV, not with width. On wide dev screens the
                           right-hand model happened to fall inside the right third
                           (x > 2/3 w); on narrower aspect ratios / larger FOVs it drifts
                           left of that line, so tapping it selected the MIDDLE zone —
                           i.e. "tap team 2, spawn as spectator". Splitting at the
                           midpoints between the anchor columns (3/8 and 5/8) keeps every
                           anchor safely inside its own zone across aspect ratios. */
                        if(x < settings.window_width * 0.375F)
                                hud_ingame_keyboard(WINDOW_KEY_SELECT1, WINDOW_PRESS, 0, 0);
                        else if(x > settings.window_width * 0.625F)
                                hud_ingame_keyboard(gun ? WINDOW_KEY_SELECT3 : WINDOW_KEY_SELECT2, WINDOW_PRESS, 0, 0);
                        else
                                hud_ingame_keyboard(gun ? WINDOW_KEY_SELECT2 : WINDOW_KEY_SELECT3, WINDOW_PRESS, 0, 0);
                }
                return;
        }
        overlay_down_finger = NULL;

        if(action != TOUCH_MOVE) {
                int k = 0;
                while(hud_ingame_onscreencontrol(k, NULL, -1)) {
                        if(is_inside_centered(f->start.x, settings.window_height - f->start.y,
                                                                  settings.window_height * (0.2F + 0.175F * k), settings.window_height * 0.96F,
                                                                  settings.window_height * 0.15F, settings.window_height * 0.1F)) {
                                hud_ingame_onscreencontrol(k, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                                return;
                        }
                        k++;
                }
                if(is_inside_centered(f->start.x, settings.window_height - f->start.y,
                                                          settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.6F,
                                                          settings.window_height * 0.15F, settings.window_height * 0.1F)) {
                        hud_ingame_onscreencontrol(64, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                        return;
                }
                if(is_inside_centered(f->start.x, settings.window_height - f->start.y,
                                                          settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.45F,
                                                          settings.window_height * 0.15F, settings.window_height * 0.1F)) {
                        hud_ingame_onscreencontrol(65, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                        return;
                }
                /* Spectator free-camera toggle plate. */
                if(hud_ingame_onscreencontrol(68, NULL, -1)
                   && is_inside_centered(f->start.x, settings.window_height - f->start.y,
                                         settings.window_width - settings.window_height * 0.075F, settings.window_height * 0.3F,
                                         settings.window_height * 0.15F, settings.window_height * 0.1F)) {
                        if(action == TOUCH_DOWN)
                                hud_ingame_onscreencontrol(68, NULL, 1);
                        return;
                }
                /* Jump/Crouch plates below the joystick. Hit-testing keys off the
                   finger's START position (like every other control), so a press that
                   begins on a button stays a button even if it wanders, and a stick
                   grab can't slide into a button.
                   VIRTUAL hit rect is much larger than the visual plate
                   (0.24 x 0.22H vs 0.15 x 0.1H) and is the invisible touch zone
                   only — the drawn button is unchanged. Sizing is bounded by two
                   neighbours:
                     - Horizontally: the two plate centers are 0.21H apart
                       (0.195H and 0.405H), so a half-width of 0.12H makes them
                       just meet at x=0.30H. Crouch is tested first, so the seam
                       resolves to crouch — harmless.
                     - Vertically/up: the joystick ring occupies flipped-y
                       ~[0.15H, 0.45H]. The rect top at ~0.20H overlaps the ring's
                       bottom sliver; again buttons-first means a low stick grab
                       that strays down reads as the button, which is the
                       forgiving behaviour anyway.
                     - Vertically/down: extends to the screen bottom. iOS may
                       first-pass eat a touch that STARTS in the home-indicator
                       strip, but presses land near the plate center (cy=0.08H),
                       well clear of it, so the extra downward area is usable. */
                if(camera_mode == CAMERAMODE_FPS || camera_mode == CAMERAMODE_SPECTATOR) {
                        if(is_inside_centered(f->start.x, settings.window_height - f->start.y, settings.window_height * 0.195F,
                                                                  settings.window_height * 0.09F, settings.window_height * 0.24F,
                                                                  settings.window_height * 0.22F)) {
                                hud_ingame_onscreencontrol(67, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                                return;
                        }
                        if(is_inside_centered(f->start.x, settings.window_height - f->start.y, settings.window_height * 0.405F,
                                                                  settings.window_height * 0.09F, settings.window_height * 0.24F,
                                                                  settings.window_height * 0.22F)) {
                                hud_ingame_onscreencontrol(66, NULL, (action == TOUCH_DOWN) ? 1 : 0);
                                return;
                        }
                }
        }

        if(screen_current == SCREEN_NONE) {
                /* Block-colour palette (bottom-right). Only while actually holding
                   the block tool in FPS — that's the only time the grid is drawn,
                   and the only time we steal this corner from camera look. A tap
                   selects the swatch under the finger; that's all that's needed
                   (no drag/scrub). The whole gesture is consumed (return) so a
                   tap that wobbles into a tiny drag can't swing the camera.
                   window.c excludes this same rect from the aim zone under the
                   same condition, so the finger is delivered here, not to look. */
                if(camera_mode == CAMERAMODE_FPS && players[local_player_id].held_item == TOOL_BLOCK
                   && palette_contains(f->start.x, f->start.y)) {
                        if(action == TOUCH_DOWN) {
                                int gx, gy;
                                palette_cell_clamp(x, y, &gx, &gy);
                                players[local_player_id].block.packed = texture_block_color(gx, gy);
                                network_updateColor();
                        }
                        return;
                }
                if(action == TOUCH_DOWN && x > settings.window_width - settings.window_height * 0.25F
                   && y < settings.window_height * 0.25F) {
                        window_pressed_keys[WINDOW_KEY_MAP] = !window_pressed_keys[WINDOW_KEY_MAP];
                        return;
                }
                if((camera_mode == CAMERAMODE_FPS || camera_mode == CAMERAMODE_SPECTATOR)
                   && distance2D(f->start.x, f->start.y, settings.window_height * 0.3F, settings.window_height * 0.7F)
                           < (settings.window_height * 0.15F) * (settings.window_height * 0.15F)) {
                        float mx = max(min(x - settings.window_height * 0.3F, settings.window_height * 0.2F),
                                                   -settings.window_height * 0.2F);
                        float my = max(min(y - settings.window_height * 0.7F, settings.window_height * 0.2F),
                                                   -settings.window_height * 0.2F);
                        hud_ingame_touch_x = mx;
                        hud_ingame_touch_y = -my;
                        if(absf(mx) > settings.window_height * 0.045F) {
                                window_pressed_keys[WINDOW_KEY_LEFT] = mx < 0;
                                window_pressed_keys[WINDOW_KEY_RIGHT] = mx > 0;
                        } else {
                                window_pressed_keys[WINDOW_KEY_LEFT] = 0;
                                window_pressed_keys[WINDOW_KEY_RIGHT] = 0;
                        }
                        if(absf(my) > settings.window_height * 0.045F) {
                                window_pressed_keys[WINDOW_KEY_UP] = my < 0;
                                window_pressed_keys[WINDOW_KEY_DOWN] = my > 0;
                        } else {
                                window_pressed_keys[WINDOW_KEY_UP] = 0;
                                window_pressed_keys[WINDOW_KEY_DOWN] = 0;
                        }
                        // window_pressed_keys[WINDOW_KEY_CROUCH] = (window_time()-f->down_time)>0.25F &&
                        // absf(mx)<settings.window_height*0.06F && absf(my)<settings.window_height*0.06F;
                        window_pressed_keys[WINDOW_KEY_SPRINT]
                                = absf(mx) > settings.window_height * 0.19F || absf(my) > settings.window_height * 0.19F;
                        if(action == TOUCH_UP) {
                                window_pressed_keys[WINDOW_KEY_LEFT] = 0;
                                window_pressed_keys[WINDOW_KEY_RIGHT] = 0;
                                window_pressed_keys[WINDOW_KEY_UP] = 0;
                                window_pressed_keys[WINDOW_KEY_DOWN] = 0;
                                window_pressed_keys[WINDOW_KEY_SPRINT] = 0;
                                // window_pressed_keys[WINDOW_KEY_CROUCH] = 0;
                                hud_ingame_touch_x = 0;
                                hud_ingame_touch_y = 0;
                        }
                        return;
                }
                if(camera_mode == CAMERAMODE_BODYVIEW && action == TOUCH_UP) {
                        /* A tap is a click, not a hold: button_map[] in
                           hud_ingame_mouseclick() latches on PRESS and only clears on
                           RELEASE, so an unpaired PRESS here left the trigger held
                           forever — the player would respawn firing nonstop. */
                        if(x < settings.window_width / 2) {
                                hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_LMB, WINDOW_PRESS, 0);
                                hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_LMB, WINDOW_RELEASE, 0);
                        }
                        if(x > settings.window_width / 2) {
                                hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_RMB, WINDOW_PRESS, 0);
                                hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_RMB, WINDOW_RELEASE, 0);
                        }
                        return;
                }
                if(chat_input_mode == CHAT_NO_INPUT && f->start.x < settings.window_width * 0.4F
                   && f->start.y > settings.window_height * 0.55F && absf(dy) > absf(dx)) {
                        static float chat_swipe_accum = 0.0F;
                        int max_offset = 127 - chat_messages;
                        if(max_offset < 0) max_offset = 0;
                        chat_swipe_accum += dy / (settings.window_height * 0.04F);
                        int step = (int)chat_swipe_accum;
                        if(step != 0) {
                                chat_swipe_accum -= step;
                                chat_scroll_offset += step;
                                if(chat_scroll_offset < 0) chat_scroll_offset = 0;
                                if(chat_scroll_offset > max_offset) chat_scroll_offset = max_offset;
                        }
                        return;
                }
                if(chat_input_mode != CHAT_NO_INPUT && f->start.x < settings.window_width * 0.4F 
                   && absf(dy) > absf(dx) && action == TOUCH_MOVE) {
                        static float chat_history_input_accum = 0.0F;
                        int max_offset = 127 - chat_messages;
                        if(max_offset < 0) max_offset = 0;
                        chat_history_input_accum += dy / (settings.window_height * 0.04F);
                        int step = (int)chat_history_input_accum;
                        if(step != 0) {
                                chat_history_input_accum -= step;
                                chat_scroll_offset += step;
                                if(chat_scroll_offset < 0) chat_scroll_offset = 0;
                                if(chat_scroll_offset > max_offset) chat_scroll_offset = max_offset;
                        }
                        return;
                }
                if(0) {
                        camera_rot_x -= dx * 0.002F;
                        camera_rot_y += dy * 0.002F;
                        camera_overflow_adjust();
                        return;
                }
        }
}

struct hud hud_ingame = {
        hud_ingame_init,
        hud_ingame_render3D,
        hud_ingame_render,
        hud_ingame_keyboard,
        hud_ingame_mouselocation,
        hud_ingame_mouseclick,
        hud_ingame_scroll,
        hud_ingame_touch,
        NULL,
        1,
        0,
        NULL,
};

/*         HUD_SERVERLIST START        */

static http_t* request_serverlist = NULL;
static http_t* request_version = NULL;
static http_t* request_news = NULL;
static int server_count = 0;
static int player_count = 0;
static struct serverlist_entry* serverlist;
static int serverlist_source = 0;
static int serverlist_is_outdated;
static int serverlist_checked_for_updates = 0;
static int serverlist_con_established;
static pthread_mutex_t serverlist_lock;

/* Two-step server selection for touch: a first tap on a row only highlights it
   (records its identifier here); a second tap on the same, already-highlighted
   row actually joins. Tapping a different row moves the highlight there instead
   of joining. Empty string means nothing is selected. */
static char serverlist_selected[64] = "";

/* Joining is deferred to the end of the render pass: server_c() blocks until
   the connection is up and switches huds, so calling it from inside the row
   loop both truncates the frame at the tapped row (everything below it never
   renders, and that broken frame stays visible for the whole connect) and
   runs hud_change() while microui windows are still open. */
static bool serverlist_join_pending = false;
static char serverlist_join_addr[256];
static char serverlist_join_name[32];
static bool serverlist_join_has_name = false;
static int serverlist_join_max = 0;

static struct serverlist_news_entry {
        struct texture image;
        char caption[65];
        char url[129];
        float tile_size;
        int color;
        struct serverlist_news_entry* next;
} serverlist_news;

static int serverlist_news_exists = 0;
static char serverlist_input[128];

static void pinned_load();
static void pinned_save();
static int pinned_contains(const char* identifier);
static void pinned_toggle(const char* identifier);

static void hud_serverlist_init() {
        ping_stop();
        network_disconnect();
        window_title(NULL);
        rpc_seti(RPC_VALUE_SLOTS, 0);
        show_exit = 0;

        window_mousemode(WINDOW_CURSOR_ENABLED);

        player_count = 0;
        server_count = 0;
        serverlist_selected[0] = '\0';
        serverlist_join_pending = false;
        const char* url = serverlist_source == 0 ? "http://services.buildandshoot.com/serverlist.json" : "http://checkpoint.aos.coffee/serverlist.json";
        request_serverlist = http_get(url, NULL);
#ifdef JENKINS_BUILD
        if (!serverlist_checked_for_updates) {
                serverlist_is_outdated = 0;
#if defined(__amd64__) || defined(__x86_64__)
                request_version = http_get("http://butter.penguins.win/api/version/", NULL);
#elif defined(__i386__)
                request_version = http_get("http://butter.penguins.win/api/version32/", NULL);
#endif
                serverlist_checked_for_updates = 1;
        }
#endif
#ifndef OS_IOS
        /* The aos.party news endpoint is defunct; skip it on iOS. Other
           platforms keep the request (it fails gracefully if the host is gone). */
        if(!serverlist_news_exists)
                request_news = http_get("http://aos.party/bs/news/", NULL);
#endif

        serverlist_con_established = request_serverlist != NULL;
        memcpy(serverlist_input, settings.last_address, sizeof settings.last_address);

        /* hud_serverlist_init doubles as the Refresh handler and runs on every
           visit to this screen. Re-running pthread_mutex_init() on a mutex that
           the async ping-update path may be holding is undefined behavior --
           initialize it exactly once. (The unconditional window_textinput(1)
           that used to live here is gone: the keyboard now opens only when the
           address field is focused, see hud_textbox/hud_ime_update.) */
        static int serverlist_lock_ready = 0;
        if(!serverlist_lock_ready) {
                pthread_mutex_init(&serverlist_lock, NULL);
                serverlist_lock_ready = 1;
        }
        pinned_load();
}

static int compare_pinned(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;
        if(aa->pinned && !bb->pinned) return -1;
        if(!aa->pinned && bb->pinned) return 1;
        return 0;
}

static int hud_serverlist_sort(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        if(strcmp(aa->country, "LAN") == 0) {
                return -1;
        }
        if(strcmp(bb->country, "LAN") == 0) {
                return 1;
        }

        if(abs(aa->current - bb->current) == 0)
                if(abs(aa->ping - bb->ping) == 0)
                        return strcmp(aa->name, bb->name);
                else
                        return aa->ping - bb->ping;

        return bb->current - aa->current;
}

static int hud_serverlist_sort_players(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        return bb->current - aa->current;
}

static int hud_serverlist_sort_name(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        return strcmp(aa->name, bb->name);
}

static int hud_serverlist_sort_map(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        return strcmp(aa->map, bb->map);
}

static int hud_serverlist_sort_mode(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        return strcmp(aa->gamemode, bb->gamemode);
}

static int hud_serverlist_sort_ping(const void* a, const void* b) {
        struct serverlist_entry* aa = (struct serverlist_entry*)a;
        struct serverlist_entry* bb = (struct serverlist_entry*)b;

        int cp = compare_pinned(a, b);
        if(cp) return cp;

        return aa->ping - bb->ping;
}

static void hud_serverlist_pingupdate(void* e, float time_delta, char* aos) {
        pthread_mutex_lock(&serverlist_lock);
        if(!e) {
                for(int k = 0; k < server_count; k++)
                        if(!strcmp(serverlist[k].identifier, aos)) {
                                serverlist[k].ping = ceil(time_delta * 1000.0F);
                                break;
                        }
        } else {
                serverlist = realloc(serverlist, (++server_count) * sizeof(struct serverlist_entry));
                memcpy(serverlist + server_count - 1, e, sizeof(struct serverlist_entry));
        }

        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort);
        pthread_mutex_unlock(&serverlist_lock);
}

#define MAX_PINNED 128
static char pinned_identifiers[MAX_PINNED][32];
static int pinned_count = 0;

static void pinned_load() {
        if(!file_exists("pinned_servers.txt")) return;
        int sz = file_size("pinned_servers.txt");
        if(sz <= 0) return;
        char* data = file_load("pinned_servers.txt");
        if(!data) return;
        pinned_count = 0;
        char* p = data;
        char* end = data + sz;
        while(p < end && pinned_count < MAX_PINNED) {
                char* nl = strchr(p, '\n');
                int len = nl ? (nl - p) : (end - p);
                if(len > 0 && len < (int)sizeof(pinned_identifiers[0])) {
                        memcpy(pinned_identifiers[pinned_count], p, len);
                        pinned_identifiers[pinned_count][len] = 0;
                        pinned_count++;
                }
                p = nl ? nl + 1 : end;
        }
        free(data);
}

static void pinned_save() {
        void* f = file_open("pinned_servers.txt", "w");
        if(!f) return;
        for(int k = 0; k < pinned_count; k++)
                file_printf(f, "%s\n", pinned_identifiers[k]);
        file_close(f);
}

static int pinned_contains(const char* identifier) {
        for(int k = 0; k < pinned_count; k++)
                if(!strcmp(pinned_identifiers[k], identifier)) return 1;
        return 0;
}

static void pinned_toggle(const char* identifier) {
        int idx = -1;
        for(int k = 0; k < pinned_count; k++)
                if(!strcmp(pinned_identifiers[k], identifier)) { idx = k; break; }
        if(idx >= 0) {
                memmove(&pinned_identifiers[idx], &pinned_identifiers[idx + 1],
                                (pinned_count - idx - 1) * sizeof(pinned_identifiers[0]));
                pinned_count--;
        } else if(pinned_count < MAX_PINNED) {
                strncpy(pinned_identifiers[pinned_count], identifier, sizeof(pinned_identifiers[0]) - 1);
                pinned_count++;
        }
        pinned_save();
}

static void server_c(char* address, char* name, int slots) {
        chat_clear(0);

        if(file_exists(address)) {
                void* data = file_load(address);
                map_vxl_load(data, file_size(address));
                free(data);
                chunk_rebuild_all();
                camera_mode = CAMERAMODE_FPS;
                players[local_player_id].pos.x = map_size_x / 2.0F;
                players[local_player_id].pos.y = map_size_y - 1.0F;
                players[local_player_id].pos.z = map_size_z / 2.0F;
                player_prev_snap(local_player_id);
                window_title(address);
                hud_change(&hud_ingame);
        } else {
                window_title(name);
                if(name && address) {
                        rpc_setv(RPC_VALUE_SERVERNAME, name);
                        rpc_setv(RPC_VALUE_SERVERURL, address);
                        /* Real slot count from the server list when joining
                           from it; 32 (classic AoS max) as fallback. */
                        rpc_seti(RPC_VALUE_SLOTS, slots > 0 ? slots : 32);
                } else {
                        rpc_seti(RPC_VALUE_SLOTS, 0);
                }
                if(network_connect_string(address))
                        hud_change(&hud_ingame);
        }

        memcpy(&settings.last_address, address, 128);
        config_save();
}

static struct texture* hud_serverlist_ui_images(int icon_id, bool* resize) {
        if(icon_id >= 32) {
                struct serverlist_news_entry* current = &serverlist_news;
                int index = 32;
                while(current) {
                        if(index == icon_id)
                                return &current->image;

                        index++;
                        current = current->next;
                }
        }

        switch(icon_id) {
                case MU_ICON_CHECK: return &texture_ui_box_check;
                case MU_ICON_EXPANDED: return &texture_ui_expanded;
                case MU_ICON_COLLAPSED: return &texture_ui_collapsed;
                case 16: *resize = true; return &texture_ui_join;
                case 17: *resize = true; return &texture_ui_wait;
                default: return NULL;
        }
}

#define IF_SELECTED(x) x

static void hud_nav_button(mu_Context* ctx, struct hud* hud_struct, const char* name) {
        if(hud_active == hud_struct) {
                mu_Color old_button_bg = ctx->style->colors[MU_COLOR_BUTTON];

                ctx->style->colors[MU_COLOR_BUTTON] = ctx->style->colors[MU_COLOR_BORDER];
                mu_button_ex(ctx, name, 0, MU_OPT_NOINTERACT | MU_OPT_ALIGNCENTER);
                ctx->style->colors[MU_COLOR_BUTTON] = old_button_bg;
        } else {
                if(mu_button(ctx, name))
                        hud_change(hud_struct);
        }
}

void hud_common_sidebar(mu_Context* ctx, float scalex, float scaley) {
        int sidebar_w = (int)(160 * scalex);

        mu_layout_row(ctx, 2, (int[]) {sidebar_w, -1}, -1);

        mu_begin_panel(ctx, "Navigation");
        mu_layout_row(ctx, 1, (int[]) {-1}, 0);

#if defined(OS_IOS)
        /* iOS has no hardware back button and no Escape key, and while a menu
           hud is active the in-game HUD (with its on-screen Menu button) isn't
           drawn — so without this there is NO way back into a running game once
           any menu is opened mid-match. Mirror hud_settings_keyboard's Escape
           handler: return to the in-game hud, clear the exit-menu flag,
           recapture the pointer. Connected-only (nothing to resume from the
           disconnected main menu). NOTE: the sidebar superseded the old top
           nav bar (hud_common_nav), which is where this button used to live —
           if that function is ever revived or this sidebar is replaced again,
           the Resume entry must move with it. */
        if(network_connected) {
                mu_text_color(ctx, 120, 230, 120);
                if(mu_button(ctx, "Resume")) {
                        hud_change(&hud_ingame);
                        show_exit = 0;
                        window_mousemode(WINDOW_CURSOR_DISABLED);
                }
                mu_text_color_default(ctx);
        }
#endif

        if(!network_connected)
                hud_nav_button(ctx, &hud_serverlist, "Servers");

        hud_nav_button(ctx, &hud_settings, "Settings");
        hud_nav_button(ctx, &hud_controls, "Controls");
        hud_nav_button(ctx, &hud_skins, "Skins");
        hud_nav_button(ctx, &hud_macros, "Macros");
        hud_nav_button(ctx, &hud_recording, "Video Recording");
        hud_nav_button(ctx, &hud_replay, "Replay Settings");


        if(!network_connected)
                hud_nav_button(ctx, &hud_demolist, "Demos");

        if(network_connected)
                hud_nav_button(ctx, &hud_chatlog, "Chat Log");

        if(serverlist_is_outdated) {
                mu_text_color(ctx, 255, 255, 60);
                if(mu_button(ctx, "New updates"))
                        show_update_popup = 1;
                mu_text_color_default(ctx);
        }

        mu_text_color(ctx, 255, 60, 60);
        if(mu_button(ctx, network_connected ? "Disconnect" : "Exit"))
                if(network_connected)
                        hud_change(&hud_serverlist);
                else
                        exit(0);
        mu_text_color_default(ctx);

        mu_end_panel(ctx);
}

static void hud_common_nav(mu_Context* ctx, mu_Rect* frame, float scalex, float scaley) {
        mu_Container* cnt = mu_get_current_container(ctx);
        cnt->rect = *frame;

        /* Build the right-hand status text FIRST: its width has to take part in
           the layout computation. Previously the status cell just received
           whatever space was left over after the fixed-width buttons, so on
           narrow windows it clipped ("...aying for 2m31s", truncated server
           counts). */
        char total_str[128];
        if(hud_active == &hud_serverlist) {
                sprintf(total_str, (server_count > 0) ? "%i players on %i servers" : "No servers", player_count, server_count);
        } else {
                if(network_connected) {
                        sprintf(total_str, "Playing for %im%is", (int)window_time() / 60, (int)window_time() % 60);
                } else {
                        sprintf(total_str, "KyroSpades %s %s", KYROSPADES_VERSION, BS_VER_INFO);
                }
        }

        /* Nav entries in DRAW order. The old code kept the width array and the
           button calls in two separate, manually synced lists -- and they had
           drifted apart: when connected, the widths row still contained the
           width for the hidden "Demos" tab, so "Chat Log" was sized for the
           label "Demos" and "Disconnect" for the label "Chat Log" (hence both
           rendering cramped). Build labels and widths from ONE list instead so
           they cannot disagree again. */
        const char* labels[8];
        float mults[8];
        int n = 0;
#if defined(OS_IOS)
        /* iOS has no Escape key, and while a menu is open the in-game HUD (with
           its Menu button) isn't drawn — so without this there is NO on-screen
           way back to the game once a menu is opened mid-match. Surface a
           Resume button as the first nav entry, connected-only (no game to
           resume from the disconnected main menu). The matching mu_button is
           drawn first below; keep label list and draw order in sync. */
        if(network_connected) { labels[n] = "Resume"; mults[n++] = 1.5F; }
#endif
        if(!network_connected) { labels[n] = "Servers"; mults[n++] = 1.5F; }
        labels[n] = "Settings"; mults[n++] = 1.5F;
        labels[n] = "Controls"; mults[n++] = 1.5F;
        labels[n] = "Skins"; mults[n++] = 1.5F;
        if(!network_connected) { labels[n] = "Demos"; mults[n++] = 1.5F; }
        if(network_connected) { labels[n] = "Chat Log"; mults[n++] = 1.5F; }
        if(serverlist_is_outdated) { labels[n] = "New updates"; mults[n++] = 1.2F; }
        labels[n] = network_connected ? "Disconnect" : "Exit"; mults[n++] = 1.5F;

        int raw[8], widths[9];
        int sum = 0;
        for(int k = 0; k < n; k++) {
                raw[k] = ctx->text_width(ctx->style->font, (char*)labels[k], 0);
                widths[k] = raw[k] * mults[k];
                sum += widths[k];
        }

        /* Reserve room for the status text, then check what's actually
           available in this row. If buttons + status don't fit, compress the
           buttons' padding (each carries 20-50% slack) down to a 1.05x floor
           before letting anything clip. Only if even that is not enough does
           the status text lose width -- i.e. on absurdly small windows. */
        int status_w = ctx->text_width(ctx->style->font, total_str, 0) + ctx->style->padding * 2;
        int inner = frame->w - ctx->style->padding * 2 - ctx->style->spacing * (n + 2);
        if(sum + status_w > inner && sum > 0) {
                float f = (float)(inner - status_w) / (float)sum;
                for(int k = 0; k < n; k++) {
                        int minw = raw[k] * 1.05F;
                        int scaled = (int)(widths[k] * f);
                        widths[k] = max(scaled, minw);
                }
        }

        widths[n] = -1;
        mu_layout_row(ctx, n + 1, widths, 0);

#if defined(OS_IOS)
        /* Resume — mirrors hud_settings_keyboard's Escape handler: drop back to
           the in-game HUD, clear the exit-menu flag, recapture the pointer.
           Connected-only so it lines up with the label added above. */
        if(network_connected) {
                mu_text_color(ctx, 120, 230, 120);
                if(mu_button(ctx, "Resume")) {
                        hud_change(&hud_ingame);
                        show_exit = 0;
                        window_mousemode(WINDOW_CURSOR_DISABLED);
                }
                mu_text_color_default(ctx);
        }
#endif

        if(!network_connected) {
                hud_nav_button(ctx, &hud_serverlist, "Servers");
        }

        hud_nav_button(ctx, &hud_settings, "Settings");
        hud_nav_button(ctx, &hud_controls, "Controls");
        hud_nav_button(ctx, &hud_skins, "Skins");
        if(!network_connected)
                hud_nav_button(ctx, &hud_demolist, "Demos");

        /* Chat Log only makes sense while connected to a server (it's where the
           messages come from). When disconnected we hide the tab entirely so
           the layout collapses cleanly. */
        if(network_connected) {
                hud_nav_button(ctx, &hud_chatlog, "Chat Log");
        }

        if(serverlist_is_outdated) {
                mu_text_color(ctx, 255, 255, 60);
                if(mu_button(ctx, "New updates")) {
                        show_update_popup = 1;
                }

                mu_text_color_default(ctx);
        }

        mu_text_color(ctx, 255, 60, 60);
        if(mu_button(ctx, network_connected ? "Disconnect": "Exit"))
                if(network_connected)
                        hud_change(&hud_serverlist);
                else
                        exit(0);
        mu_text_color_default(ctx);

        mu_button_ex(ctx, total_str, 0, MU_OPT_ALIGNRIGHT | MU_OPT_NOINTERACT);
}

static void hud_serverlist_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);

        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);

        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;

                hud_common_sidebar(ctx, scalex, scaley);

                mu_begin_panel(ctx, "Content");

                mu_layout_row(ctx, 1, (int[]) {-1}, settings.window_height * 0.3F);

                if(serverlist_news_exists && settings.show_news) {
                        mu_begin_panel(ctx, "News");
                        mu_layout_row(ctx, 0, NULL, 0);

                        struct serverlist_news_entry* current = &serverlist_news;
                        int index = 0;
                        while(current) {
                                mu_layout_begin_column(ctx);
                                float size = settings.window_height * 0.3F - ctx->text_height(ctx->style->font) * 4.125F;
                                mu_layout_row(ctx, 1, (int[]) {size * current->tile_size}, size);
                                if(mu_button_ex(ctx, NULL, 32 + index, MU_OPT_NOFRAME)) {
                                        if(!strncmp("aos://", current->url, 6)) {
                                                strncpy(serverlist_join_addr, current->url,
                                                                sizeof(serverlist_join_addr) - 1);
                                                serverlist_join_addr[sizeof(serverlist_join_addr) - 1] = '\0';
                                                strncpy(serverlist_join_name, current->caption,
                                                                sizeof(serverlist_join_name) - 1);
                                                serverlist_join_name[sizeof(serverlist_join_name) - 1] = '\0';
                                                serverlist_join_has_name = true;
                                                serverlist_join_max = 0; /* news tiles carry no player counts */
                                                serverlist_join_pending = true;
                                        } else {
                                                file_url(current->url);
                                        }
                                }
                                mu_layout_height(ctx, 0);
                                mu_text_color(ctx, red(current->color), green(current->color), blue(current->color));
                                mu_text(ctx, current->caption);
                                mu_text_color_default(ctx);
                                mu_layout_end_column(ctx);
                                index++;
                                current = current->next;
                        }

                        mu_end_panel(ctx);
                }

                int a = ctx->text_width(ctx->style->font, "Refresh", 0) * 1.6F;
                int b = ctx->text_width(ctx->style->font, "Local", 0) * 4.0F;
                int c = ctx->text_width(ctx->style->font, "Join", 0) * 2.0F;
                mu_layout_row(ctx, 4, (int[]) {-c - b, -b, -a, -1}, 0);

                if(hud_textbox(ctx, serverlist_input, sizeof(serverlist_input), 0) & MU_RES_SUBMIT)
                        server_c(serverlist_input, NULL, 0);
                if(mu_button_ex(ctx, "Join", 16, MU_OPT_ALIGNRIGHT))
                        server_c(serverlist_input, NULL, 0);

                if(mu_button_ex(ctx, "Local", 16, MU_OPT_ALIGNRIGHT))
                        server_c("aos://16777343:32887", NULL, 0);

                if(mu_button_ex(ctx, "Refresh", 17, MU_OPT_ALIGNRIGHT) && !request_serverlist)
                        hud_serverlist_init();

                mu_layout_row(ctx, 1, (int[]) {-1}, -1);

                mu_begin_panel(ctx, "Servers");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                char source_label[64];
                sprintf(source_label, "Serverlist: %s", serverlist_source == 0 ? "Master" : "checkpoint.aos.coffee");
                if(mu_button(ctx, source_label)) {
                        serverlist_source = !serverlist_source;
                        hud_serverlist_init();
                }

                int width = mu_get_current_container(ctx)->body.w;

                int flag_width = ctx->style->size.y + ctx->style->padding * 2;
                int players_w = ctx->text_width(ctx->style->font, "Players", 0) + ctx->style->padding * 2;
                int col0_w = fminf(82.F * hud_ui_scale(), 0.12F * width);
                if(col0_w < players_w)
                        col0_w = players_w;
                mu_layout_row(ctx, 5, (int[]) {col0_w, 0.418F * width, 0.22F * width, 0.117F * width, -1}, 0);

                if(mu_button(ctx, "Players")) {
                        pthread_mutex_lock(&serverlist_lock);
                        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort_players);
                        pthread_mutex_unlock(&serverlist_lock);
                }
                if(mu_button(ctx, "Name")) {
                        pthread_mutex_lock(&serverlist_lock);
                        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort_name);
                        pthread_mutex_unlock(&serverlist_lock);
                }
                if(mu_button(ctx, "Map")) {
                        pthread_mutex_lock(&serverlist_lock);
                        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort_map);
                        pthread_mutex_unlock(&serverlist_lock);
                }
                if(mu_button(ctx, "Mode")) {
                        pthread_mutex_lock(&serverlist_lock);
                        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort_mode);
                        pthread_mutex_unlock(&serverlist_lock);
                }
                if(mu_button(ctx, "Ping")) {
                        pthread_mutex_lock(&serverlist_lock);
                        qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort_ping);
                        pthread_mutex_unlock(&serverlist_lock);
                }

                mu_layout_row(ctx, 6,
                                          (int[]) {col0_w, flag_width, 0.418F * width - flag_width - ctx->style->spacing * 2,
                                                           0.22F * width, 0.117F * width, -1},
                                          0);

                pthread_mutex_lock(&serverlist_lock);
                bool any_row_tapped = false;
                if(server_count > 0) {
                        char total_str[128];
                        int serverlist_need_sort = 0;
                        for(int k = 0; k < server_count; k++) {
                                if(serverlist[k].current >= 0)
                                        sprintf(total_str, "%i/%i", serverlist[k].current, serverlist[k].max);
                                else
                                        strcpy(total_str, "-");

                                int f = ((serverlist[k].current && serverlist[k].current < serverlist[k].max)
                                                 || serverlist[k].current < 0) ?
                                        1 :
                                        2;

                                mu_push_id(ctx, &serverlist[k].identifier, strlen(serverlist[k].identifier));

                                bool is_selected = strcmp(serverlist_selected, serverlist[k].identifier) == 0;

                                if(is_selected)
                                        f = 1;

                                if(is_selected) {
                                        mu_Container* panel = mu_get_current_container(ctx);
                                        mu_Layout* lay = &ctx->layout_stack.items[ctx->layout_stack.idx - 1];
                                        mu_Layout saved = *lay;
                                        mu_Rect cell = mu_layout_next(ctx);
                                        *lay = saved;
                                        mu_Rect band = mu_rect(panel->body.x, cell.y, panel->body.w, cell.h);
                                        mu_draw_rect(ctx, band,
                                                mu_color(hud_accent_red() * 2 / 5, hud_accent_green() * 2 / 5,
                                                                 hud_accent_blue() * 2 / 5, 255));
                                        mu_draw_box(ctx, band,
                                                mu_color(hud_accent_red(), hud_accent_green(),
                                                                 hud_accent_blue(), 255));
                                }


                                mu_text_color(ctx, 230 / f, 230 / f, 230 / f);
                                bool tapped = false;
                                if(mu_button_ex(ctx, total_str, 0, MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER))
                                        tapped = true;
                                if(mu_button_ex(ctx, "", texture_flag_index(serverlist[k].country) + HUD_FLAG_INDEX_START,
                                                                MU_OPT_NOFRAME))
                                        tapped = true;
                                if(mu_button_ex(ctx, serverlist[k].name, 0, MU_OPT_NOFRAME))
                                        tapped = true;
                                if(mu_button_ex(ctx, serverlist[k].map, 0, MU_OPT_NOFRAME))
                                        tapped = true;
                                if(mu_button_ex(ctx, serverlist[k].gamemode, 0, MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER))
                                        tapped = true;

                                if(serverlist[k].ping >= 0) {
                                        if(serverlist[k].ping < 110)
                                                mu_text_color(ctx, 0, 255 / f, 0);
                                        else if(serverlist[k].ping < 200)
                                                mu_text_color(ctx, 255 / f, 255 / f, 0);
                                        else
                                                mu_text_color(ctx, 255 / f, 0, 0);
                                }

                                sprintf(total_str, "%ims", serverlist[k].ping);
                                if(mu_button_ex(ctx, (serverlist[k].ping >= 0) ? total_str : "?", 0,
                                                                MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER))
                                        tapped = true;

                                if(serverlist[k].pinned) {
                                        mu_Container* cnt = mu_get_current_container(ctx);
                                        mu_draw_rect(ctx, mu_rect(cnt->body.x, ctx->last_rect.y, cnt->body.w, ctx->last_rect.h),
                                                                 mu_color(255, 220, 0, 20));
                                }

                                if(ctx->mouse_pressed & MU_MOUSE_RIGHT) {
                                        mu_Container* cnt = mu_get_current_container(ctx);
                                        mu_Rect row_rect = mu_rect(cnt->body.x, ctx->last_rect.y, cnt->body.w, ctx->last_rect.h);
                                        if(mu_mouse_over(ctx, row_rect)) {
                                                pinned_toggle(serverlist[k].identifier);
                                                serverlist[k].pinned = !serverlist[k].pinned;
                                                serverlist_need_sort = 1;
                                        }
                                }

                                mu_pop_id(ctx);

                                if(tapped) {
                                        any_row_tapped = true;
                                        if(is_selected) {
                                                serverlist_selected[0] = '\0';
                                                strncpy(serverlist_join_addr, serverlist[k].identifier,
                                                                sizeof(serverlist_join_addr) - 1);
                                                serverlist_join_addr[sizeof(serverlist_join_addr) - 1] = '\0';
                                                strncpy(serverlist_join_name, serverlist[k].name,
                                                                sizeof(serverlist_join_name) - 1);
                                                serverlist_join_name[sizeof(serverlist_join_name) - 1] = '\0';
                                                serverlist_join_has_name = true;
                                                serverlist_join_max = serverlist[k].max;
                                                serverlist_join_pending = true;
                                        } else {
                                                strncpy(serverlist_selected, serverlist[k].identifier,
                                                                sizeof(serverlist_selected) - 1);
                                                serverlist_selected[sizeof(serverlist_selected) - 1] = '\0';
                                        }
                                }
                        }
                        if(serverlist_need_sort)
                                qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort);
                } else {
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                        mu_button_ex(ctx, "Fetching servers...", 0, MU_OPT_NOFRAME | MU_OPT_ALIGNCENTER);
                }
                pthread_mutex_unlock(&serverlist_lock);

                if(ctx->mouse_pressed == MU_MOUSE_LEFT && !any_row_tapped
                   && mu_mouse_over(ctx, mu_get_current_container(ctx)->body))
                        serverlist_selected[0] = '\0';

                mu_text_color_default(ctx);
                mu_end_panel(ctx);

                mu_end_panel(ctx);

                mu_end_window(ctx);
        }

        if(window_time() - chat_popup_timer < chat_popup_duration
           && mu_begin_window_ex(ctx, "Disconnected from server", mu_rect(200, 250, 300, 100),
                                                         MU_OPT_HOLDFOCUS | MU_OPT_NORESIZE | MU_OPT_NOCLOSE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                mu_bring_to_front(ctx, cnt);
                /* Size everything off the font, like the chat-log link modal: the
                   old fixed 300x100 px box truncated both the title and the reason
                   text once the UI font is scaled up on high-DPI / mobile screens. */
                int th = ctx->text_height(ctx->style->font);
                int pad = th;
                int reason_w = ctx->text_width(ctx->style->font, "Reason:", 0) * 1.5F;
                int w = ctx->text_width(ctx->style->font, "Disconnected from server", 0) + 4 * th;
                int t = reason_w + ctx->text_width(ctx->style->font, chat_popup, 0) + 2 * pad;
                if(t > w) w = t;
                int max_w = settings.window_width * 92 / 100;
                if(w > max_w) w = max_w;
                int h = ctx->style->title_height + th + 3 * pad;
                cnt->rect = mu_rect((settings.window_width - w) / 2, (settings.window_height - h) / 3, w, h);
                mu_layout_row(ctx, 2, (int[]) {reason_w, -1}, 0);
                mu_text(ctx, "Reason:");
                mu_text(ctx, chat_popup);
                mu_end_window(ctx);
        }

        if(request_news) {
                switch(http_process(request_news)) {
                        case HTTP_STATUS_COMPLETED: {
                                JSON_Value* js = json_parse_string(request_news->response_data);
                                JSON_Array* news = json_value_get_array(js);
                                int news_entries = json_array_get_count(news);

                                struct serverlist_news_entry* current = &serverlist_news;
                                memset(current, 0, sizeof(struct serverlist_news_entry));

                                for(int k = 0; k < news_entries; k++) {
                                        JSON_Object* s = json_array_get_object(news, k);
                                        if(json_object_get_string(s, "caption"))
                                                strncpy(current->caption, json_object_get_string(s, "caption"), sizeof(current->caption) - 1);
                                        if(json_object_get_string(s, "url"))
                                                strncpy(current->url, json_object_get_string(s, "url"), sizeof(current->url) - 1);
                                        current->tile_size = json_object_get_number(s, "tilesize");
                                        current->color = json_object_get_number(s, "color");
                                        if(json_object_get_string(s, "image")) {
                                                char* img = (char*)json_object_get_string(s, "image");
                                                int size = base64_decode(img, strlen(img));
                                                unsigned char* buffer;
                                                int width, height;
                                                lodepng_decode32(&buffer, &width, &height, img, size);
                                                texture_create_buffer(&current->image, width, height, buffer, 1);
                                                texture_filter(&current->image, TEXTURE_FILTER_LINEAR);
                                        }
                                        current->next = (k < news_entries - 1) ? malloc(sizeof(struct serverlist_news_entry)) : NULL;
                                        current = current->next;
                                }

                                json_value_free(js);
                                http_release(request_news);
                                serverlist_news_exists = 1;
                                request_news = NULL;
                                break;
                        }
                        case HTTP_STATUS_FAILED:
                                http_release(request_news);
                                request_news = NULL;
                                break;
                }
        }

#ifdef JENKINS_BUILD
        if(request_version) {
                switch(http_process(request_version)) {
                        case HTTP_STATUS_COMPLETED:
                                serverlist_is_outdated = 1;
                                strcpy(latest_ver, request_version->response_data);
                                log_info("newest game version: %s", latest_ver);
                                log_info("current game version: %s", JENKINS_BUILD);
                                serverlist_is_outdated = strcmp(request_version->response_data, JENKINS_BUILD) != 0;
                                http_release(request_version);
                                request_version = NULL;
                                break;
                        case HTTP_STATUS_FAILED:
                                http_release(request_version);
                                request_version = NULL;
                                break;
                }
        }
#endif

        int render_status_icon = !serverlist_con_established;
        if(request_serverlist) {
                switch(http_process(request_serverlist)) {
                        case HTTP_STATUS_PENDING: render_status_icon = 1; break;
                        case HTTP_STATUS_COMPLETED: {
                                if(serverlist_source != 0) {
                                        void* f = file_open("serverlist_debug.txt", "w");
                                        if(f) {
                                                file_printf(f, "%s", request_serverlist->response_data);
                                                file_close(f);
                                        }
                                }
                                JSON_Value* js = json_parse_string(request_serverlist->response_data);
                                JSON_Array* servers = json_value_get_array(js);
                                server_count = json_array_get_count(servers);

                                pthread_mutex_lock(&serverlist_lock);
                                serverlist = realloc(serverlist, server_count * sizeof(struct serverlist_entry));
                                CHECK_ALLOCATION_ERROR(serverlist)

                                ping_start(hud_serverlist_pingupdate);

                                player_count = 0;
                                for(int k = 0; k < server_count; k++) {
                                        JSON_Object* s = json_array_get_object(servers, k);
                                        memset(&serverlist[k], 0, sizeof(struct serverlist_entry));

                                        serverlist[k].current = (int)json_object_get_number(s, "players_current");
                                        serverlist[k].max = (int)json_object_get_number(s, "players_max");
                                        serverlist[k].ping = -1;

                                        strncpy(serverlist[k].name, json_object_get_string(s, "name"), sizeof(serverlist[k].name) - 1);
                                        strncpy(serverlist[k].map, json_object_get_string(s, "map"), sizeof(serverlist[k].map) - 1);
                                        strncpy(serverlist[k].gamemode, json_object_get_string(s, "game_mode"),
                                                        sizeof(serverlist[k].gamemode) - 1);
                                        strncpy(serverlist[k].identifier, json_object_get_string(s, "identifier"),
                                                        sizeof(serverlist[k].identifier) - 1);
                                        strncpy(serverlist[k].country, json_object_get_string(s, "country"),
                                                        sizeof(serverlist[k].country) - 1);

                                        int port;
                                        char ip[32];
                                        if(network_identifier_split(serverlist[k].identifier, ip, &port))
                                                ping_check(ip, port, serverlist[k].identifier);

                                        player_count += serverlist[k].current;
                                }

                                for(int k = 0; k < server_count; k++)
                                        serverlist[k].pinned = pinned_contains(serverlist[k].identifier);

                                qsort(serverlist, server_count, sizeof(struct serverlist_entry), hud_serverlist_sort);
                                pthread_mutex_unlock(&serverlist_lock);

                                http_release(request_serverlist);
                                json_value_free(js);
                                request_serverlist = NULL;
                                break;
                        }
                        case HTTP_STATUS_FAILED:
                                http_release(request_serverlist);
                                hud_serverlist_init();
                                break;
                }
        }

        /* Deferred join: every microui window of this frame is closed by now, so
           the frame the user sees during the (blocking) connect is complete. */
        if(serverlist_join_pending) {
                serverlist_join_pending = false;
                server_c(serverlist_join_addr,
                                 serverlist_join_has_name ? serverlist_join_name : NULL,
                                 serverlist_join_max);
                serverlist_join_max = 0;
        }
}

static void hud_serverlist_touch(void* finger, int action, float x, float y, float dx, float dy) {
        (void)finger;
        window_setmouseloc(x, y);
        /* Drag pans the list; taps fall through to the generic synthetic-mouse path
           which single-clicks like every other menu. */
        if(action == TOUCH_MOVE && hud_serverlist.ctx)
                mu_input_scroll(hud_serverlist.ctx, 0, (int)(-dy));
}

struct hud hud_serverlist = {
        hud_serverlist_init,
        NULL,
        hud_serverlist_render,
        NULL,
        NULL,
        NULL,
        NULL,
        hud_serverlist_touch,
        hud_serverlist_ui_images,
        0,
        0,
        NULL,
};

/*         HUD_SETTINGS START        */

static int selected_category = 0;
static char settings_search[128] = "";

/* Case-insensitive settings search. Every whitespace-separated word in the
   query must occur somewhere in the setting name, category or subcategory.
   This makes searches such as "weapon fov" useful without requiring the
   words to be adjacent. */
static int settings_search_matches(struct config_setting* setting) {
        const unsigned char* q = (const unsigned char*)settings_search;
        char haystack[256];
        snprintf(haystack, sizeof(haystack), "%s %s %s",
                 setting->name, setting->category, setting->subcategory);
        for(char* p = haystack; *p; p++)
                *p = (char)tolower((unsigned char)*p);

        while(*q) {
                while(*q && isspace(*q)) q++;
                if(!*q) break;

                char word[64];
                int n = 0;
                while(*q && !isspace(*q)) {
                        if(n < (int)sizeof(word) - 1)
                                word[n++] = (char)tolower(*q);
                        q++;
                }
                word[n] = 0;
                if(n > 0 && !strstr(haystack, word))
                        return 0;
        }
        return 1;
}

static int settings_search_active(void) {
        const unsigned char* q = (const unsigned char*)settings_search;
        while(*q && isspace(*q)) q++;
        return *q != 0;
}

static void hud_settings_init() {
        memcpy(&settings_tmp, &settings, sizeof(struct RENDER_OPTIONS));
        selected_category = 0;
        settings_search[0] = '\0';
}

static int int_slider_defaults(mu_Context* ctx, struct config_setting* setting) {
        int k = setting->defaults_length - 1;
        while(k > 0 && setting->defaults[k] > *(int*)setting->value)
                k--;

        float tmp = k;

        mu_push_id(ctx, setting, sizeof(setting));
        int res = mu_slider_ex(ctx, &tmp, 0, setting->defaults_length - 1, 0, "", MU_OPT_ALIGNCENTER);

        if(res & MU_RES_CHANGE)
                *(int*)setting->value = setting->defaults[(int)round(tmp)];

        if(setting->label_callback) {
                char buf[64];
                setting->label_callback(buf, sizeof(buf), setting->defaults[(int)round(tmp)], (int)round(tmp));
                mu_draw_control_text(ctx, buf, ctx->last_rect, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
        }

        mu_pop_id(ctx);
        return res;
}

static int int_slider(mu_Context* ctx, int* value, int low, int high) {
        float tmp = *value;
        mu_push_id(ctx, &value, sizeof(value));
        int res = mu_slider_ex(ctx, &tmp, low, high, 0, "%.0f", MU_OPT_ALIGNCENTER);
        mu_pop_id(ctx);
        *value = round(tmp);
        return res;
}

static int int_number(mu_Context* ctx, int* value) {
        float tmp = *value;
        mu_push_id(ctx, &value, sizeof(value));
        int res = mu_number_ex(ctx, &tmp, 1, "%.0f", MU_OPT_ALIGNCENTER);
        mu_pop_id(ctx);
        *value = max(round(tmp), 0);
        return res;
}

static struct texture* hud_settings_ui_images(int icon_id, bool* resize) {
        switch(icon_id) {
                case MU_ICON_CHECK: return &texture_ui_box_check;
                case MU_ICON_EXPANDED: return &texture_ui_expanded;
                case MU_ICON_COLLAPSED: return &texture_ui_collapsed;
                default: return NULL;
        }
}

static void render_setting_row(mu_Context* ctx, struct config_setting* a, int width) {
        mu_layout_row(ctx, 2, (int[]) {0.65F * width, -1}, 0);

        /* Hover highlight: when the mouse is over this row, draw a tinted
           band behind the entire row (spanning the panel width) so the user
           can see which setting they're about to interact with. Mirrors the
           selected-row highlight in the server list, but driven by mouse
           hover instead of selection. Peek the next layout cell to learn
           the row's Y/height, then rewind the layout cursor so the real
           row content lays out exactly where it would have. */
        {
                mu_Layout* lay = &ctx->layout_stack.items[ctx->layout_stack.idx - 1];
                mu_Layout saved = *lay;
                mu_Rect cell = mu_layout_next(ctx);
                *lay = saved; /* undo the slot the peek consumed */

                mu_Container* panel = mu_get_current_container(ctx);
                mu_Rect row_rect = mu_rect(panel->body.x, cell.y, panel->body.w, cell.h);

                if(mu_mouse_over(ctx, row_rect)) {
                        /* Dark accent tint (40% of accent color) for the fill,
                           full accent for the outline — matches the server
                           list selection style. */
                        mu_draw_rect(ctx, row_rect,
                                mu_color(hud_accent_red() * 2 / 5,
                                                 hud_accent_green() * 2 / 5,
                                                 hud_accent_blue() * 2 / 5, 255));
                        mu_draw_box(ctx, row_rect,
                                mu_color(hud_accent_red(), hud_accent_green(),
                                                 hud_accent_blue(), 255));
                }
        }

        switch(a->type) {
                case CONFIG_TYPE_STRING:
                        mu_text(ctx, a->name);
                        hud_textbox(ctx, a->value, a->max + 1, 0);
                        break;
                case CONFIG_TYPE_INT:
                        if(a->max == 1 && a->min == 0) {
                                mu_text(ctx, a->name);
                                mu_checkbox(ctx, "", a->value);
                        } else if(a->defaults_length > 0) {
                                mu_text(ctx, a->name);
                                int_slider_defaults(ctx, a);
                        } else if(a->max == INT_MAX) {
                                mu_text(ctx, a->name);
                                int_number(ctx, a->value);
                        } else {
                                mu_text(ctx, a->name);
                                int_slider(ctx, a->value, a->min, a->max);
                        }
                        break;
                case CONFIG_TYPE_FLOAT:
                        mu_text(ctx, a->name);
                        if(a->max == INT_MAX) {
                                mu_number(ctx, a->value, 0.1F);
                                *(float*)a->value = max(a->min, *(float*)a->value);
                        } else {
                                mu_slider(ctx, a->value, a->min, a->max);
                        }
                        break;
        }

}

static int setting_in_category(struct config_setting* a, int cat) {
        switch(cat) {
                case 0:
                        return strcmp(a->category, "Weapon Settings") != 0
                                && strcmp(a->category, "Weather") != 0
                                && strcmp(a->category, "Spectator Mode Settings") != 0
                                && strcmp(a->category, "Graphic Settings") != 0
                                && strcmp(a->category, "HUD/UI Settings") != 0
                                && strcmp(a->category, "Chat Settings") != 0
                                && strcmp(a->category, "Recording & Replay") != 0
                                && strcmp(a->category, "Visual Effects") != 0;
                case 1: return strcmp(a->category, "Weapon Settings") == 0;
                case 2: return strcmp(a->category, "Weather") == 0;
                case 3: return strcmp(a->category, "Spectator Mode Settings") == 0;
                case 4: return strcmp(a->category, "Graphic Settings") == 0;
                case 5: return strcmp(a->category, "HUD/UI Settings") == 0;
                case 6: return strcmp(a->category, "Chat Settings") == 0;
                case 7: return strcmp(a->category, "Visual Effects") == 0;
                default: return 0;
        }
}

static void hud_settings_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);

        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);

        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;

                hud_common_sidebar(ctx, scalex, scaley);

                mu_begin_panel(ctx, "Content");

                /* Keep search outside the scrolling settings panel so it remains
                   visible while browsing results. A search spans every category;
                   clearing it immediately returns to the selected category. */
                int search_label_w = ctx->text_width(ctx->style->font, "Search settings:", 0)
                        + ctx->style->padding * 2;
                int clear_w = ctx->text_width(ctx->style->font, "Clear", 0)
                        + ctx->style->padding * 2;
                mu_layout_row(ctx, 3, (int[]) {search_label_w, -clear_w, -1}, 0);
                mu_label(ctx, "Search settings:");
                hud_textbox(ctx, settings_search, sizeof(settings_search), 0);
                if(mu_button(ctx, "Clear")) {
                        settings_search[0] = '\0';
                        ctx->focus = 0;
                }

                mu_layout_row(ctx, 2, (int[]) {150, -1}, -1);

                mu_begin_panel(ctx, "Categories");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                static const char* cat_names[] = {
                        "General",
                        "Weapons",
                        "Weather",
                        "Spectator",
                        "Graphics",
                        "HUD/UI",
                        "Chat",
                        "Visual Effects",
                };
                int cat_count = sizeof(cat_names) / sizeof(cat_names[0]);

                for(int i = 0; i < cat_count; i++) {
                        if(selected_category == i) {
                                mu_Color old_border = ctx->style->colors[MU_COLOR_BORDER];
                                mu_Color old_text = ctx->style->colors[MU_COLOR_TEXT];
                                mu_Color accent = {hud_accent_red(), hud_accent_green(), hud_accent_blue(), 255};
                                mu_Color black = {0, 0, 0, 255};
                                ctx->style->colors[MU_COLOR_BORDER] = accent;
                                ctx->style->colors[MU_COLOR_TEXT] = black;
                                mu_button_ex(ctx, cat_names[i], 0, MU_OPT_NOINTERACT);
                                ctx->style->colors[MU_COLOR_TEXT] = old_text;
                                ctx->style->colors[MU_COLOR_BORDER] = old_border;
                        } else {
                                if(mu_button(ctx, cat_names[i]))
                                        selected_category = i;
                        }
                }

                mu_end_panel(ctx);

                mu_begin_panel(ctx, "Settings");

                int width = mu_get_current_container(ctx)->body.w;

                /* Two-pass rendering:
                   Pass 1: render all settings with NO subcategory (directly, no dropdown)
                   Pass 2: for each unique subcategory, render one collapsible dropdown
                           containing ALL settings in that subcategory (grouped together
                           regardless of their order in the config_settings list). */

                /* Helper: check if a setting should be skipped (hidden recording settings) */
                #define SHOULD_SKIP(a) ( \
                        strcmp((a)->name, "Recording FPS") == 0 || \
                        strcmp((a)->name, "Recording bitrate (kbps)") == 0 || \
                        strcmp((a)->name, "Replay Enabled") == 0 || \
                        strcmp((a)->name, "Replay Duration (s)") == 0 || \
                        strcmp((a)->name, "Replay Save Hotkey") == 0)

                if(settings_search_active()) {
                        int matches = 0;
                        for(int k = 0; k < list_size(&config_settings); k++) {
                                struct config_setting* a = list_get(&config_settings, k);
                                if(!SHOULD_SKIP(a) && settings_search_matches(a))
                                        matches++;
                        }

                        char result_text[64];
                        if(matches == 0)
                                strcpy(result_text, "No matching settings");
                        else
                                snprintf(result_text, sizeof(result_text), "%d matching setting%s",
                                         matches, matches == 1 ? "" : "s");
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                        mu_button_ex(ctx, result_text, 0,
                                     MU_OPT_NOINTERACT | MU_OPT_ALIGNCENTER);

                        /* Search results are expanded directly, regardless of
                           category or subcategory, for one-tap access. */
                        for(int k = 0; k < list_size(&config_settings); k++) {
                                struct config_setting* a = list_get(&config_settings, k);
                                if(!SHOULD_SKIP(a) && settings_search_matches(a))
                                        render_setting_row(ctx, a, width);
                        }
                } else {
                /* Pass 1: settings with no subcategory */
                for(int k = 0; k < list_size(&config_settings); k++) {
                        struct config_setting* a = list_get(&config_settings, k);
                        if(!setting_in_category(a, selected_category) || SHOULD_SKIP(a))
                                continue;
                        if(a->subcategory[0] != 0)
                                continue; /* skip subcategorized settings — handled in pass 2 */
                        render_setting_row(ctx, a, width);
                }

                /* Pass 2: for each unique subcategory, render a dropdown */
                char seen_subcats[32][32]; /* track which subcategories we've already rendered */
                int num_seen = 0;

                for(int k = 0; k < list_size(&config_settings); k++) {
                        struct config_setting* a = list_get(&config_settings, k);
                        if(!setting_in_category(a, selected_category) || SHOULD_SKIP(a))
                                continue;
                        if(a->subcategory[0] == 0)
                                continue; /* already rendered in pass 1 */

                        /* Check if we already rendered this subcategory */
                        int already_seen = 0;
                        for(int s = 0; s < num_seen; s++) {
                                if(strcmp(seen_subcats[s], a->subcategory) == 0) {
                                        already_seen = 1;
                                        break;
                                }
                        }
                        if(already_seen)
                                continue;

                        /* Record this subcategory as seen */
                        strncpy(seen_subcats[num_seen], a->subcategory, 31);
                        seen_subcats[num_seen][31] = 0;
                        num_seen++;
                        if(num_seen >= 32) break;

                        /* Render the dropdown header (highlighted) */
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                        /* Highlight the treenode header with accent color */
                        mu_Color old_border = ctx->style->colors[MU_COLOR_BORDER];
                        mu_Color old_text = ctx->style->colors[MU_COLOR_TEXT];
                        mu_Color accent = {hud_accent_red(), hud_accent_green(), hud_accent_blue(), 255};
                        mu_Color white = {255, 255, 255, 255};
                        ctx->style->colors[MU_COLOR_BORDER] = accent;
                        ctx->style->colors[MU_COLOR_TEXT] = white;

                        int expanded = mu_begin_treenode(ctx, a->subcategory);

                        ctx->style->colors[MU_COLOR_TEXT] = old_text;
                        ctx->style->colors[MU_COLOR_BORDER] = old_border;

                        if(expanded) {
                                /* Render all settings in this subcategory */
                                for(int j = 0; j < list_size(&config_settings); j++) {
                                        struct config_setting* b = list_get(&config_settings, j);
                                        if(!setting_in_category(b, selected_category) || SHOULD_SKIP(b))
                                                continue;
                                        if(strcmp(b->subcategory, a->subcategory) != 0)
                                                continue;
                                        render_setting_row(ctx, b, width);
                                }
                                mu_end_treenode(ctx);
                        }
                }
                } /* !settings_search_active() */

                #undef SHOULD_SKIP

                mu_end_panel(ctx);

                mu_end_panel(ctx);

                mu_end_window(ctx);
        }

        if(memcmp(&settings, &settings_tmp, sizeof(struct RENDER_OPTIONS)) != 0) {
                int remesh = settings.textured_blocks != settings_tmp.textured_blocks
                        || settings.ambient_occlusion != settings_tmp.ambient_occlusion
                        || settings.greedy_meshing != settings_tmp.greedy_meshing
                        || settings.ao_multiplier != settings_tmp.ao_multiplier
                        || settings.shadow_quality != settings_tmp.shadow_quality
                        || settings.shadow_intensity != settings_tmp.shadow_intensity;
                memcpy(&settings, &settings_tmp, sizeof(struct RENDER_OPTIONS));
                window_fromsettings();
                sound_volume(settings.volume / 10.0F);
                config_save();
                if(remesh)
                        chunk_rebuild_all();
        }
}

static void hud_settings_keyboard(int key, int action, int mods, int internal) {
        if(action == WINDOW_PRESS && show_exit && key == WINDOW_KEY_ESCAPE) {
                hud_change(&hud_ingame);
                show_exit = 0;
                window_mousemode(WINDOW_CURSOR_DISABLED);
        }
}

static void hud_settings_touch(void* finger, int action, float x, float y, float dx, float dy) {
        (void)finger;
        window_setmouseloc(x, y);
        if(action == TOUCH_MOVE && hud_settings.ctx)
                mu_input_scroll(hud_settings.ctx, 0, (int)(-dy));
}

struct hud hud_settings = {
        hud_settings_init,
        NULL,
        hud_settings_render,
        hud_settings_keyboard,
        NULL,
        NULL,
        NULL,
        hud_settings_touch,
        hud_settings_ui_images,
        0,
        0,
        NULL,
};


/*         HUD_SKINS START        */

static int skins_selected_category = 0;
static int skins_selected_entry[SKIN_CATEGORIES] = {0, 0, 0, 0, 0, 0, 0, 0};
static int skins_preview_cells_x[256];
static int skins_preview_cells_y[256];
static int skins_preview_cat[256];
static int skins_preview_ent[256];
static int skins_preview_cell_count = 0;

static void mu_draw_control_frame_inner(mu_Context* ctx, mu_Rect rect, mu_Color color) {
        mu_draw_rect(ctx, rect, color);
}

static void hud_skins_init() {
        skins_selected_category = 0;
        skins_selected_entry[0] = settings.skin_spade;
        skins_selected_entry[1] = settings.skin_grenade;
        skins_selected_entry[2] = settings.skin_rifle;
        skins_selected_entry[3] = settings.skin_smg;
        skins_selected_entry[4] = settings.skin_shotgun;
        skins_selected_entry[6] = settings.skin_intel;
        skins_selected_entry[7] = settings.skin_tent;
        skins_scan();
}

static void hud_skins_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);

        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);

        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;

                hud_common_sidebar(ctx, scalex, scaley);

                mu_begin_panel(ctx, "Content");

                mu_layout_row(ctx, 2, (int[]) {150, -1}, -1);

                mu_begin_panel(ctx, "Categories");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                for(int i = 0; i < SKIN_CATEGORIES; i++) {
                        if(i == SKIN_PLAYER) continue;
                        mu_Color old_border = ctx->style->colors[MU_COLOR_BORDER];
                        mu_Color old_text = ctx->style->colors[MU_COLOR_TEXT];
                        mu_Color accent = {hud_accent_red(), hud_accent_green(), hud_accent_blue(), 255};
                        ctx->style->colors[MU_COLOR_BORDER] = accent;
                        if(skins_selected_category == i) {
                                ctx->style->colors[MU_COLOR_TEXT] = (mu_Color){0, 0, 0, 255};
                                mu_button_ex(ctx, skin_categories[i].label, 0, MU_OPT_NOINTERACT);
                        } else {
                                ctx->style->colors[MU_COLOR_TEXT] = (mu_Color){255, 255, 255, 255};
                                if(mu_button(ctx, skin_categories[i].label))
                                        skins_selected_category = i;
                        }
                        ctx->style->colors[MU_COLOR_TEXT] = old_text;
                        ctx->style->colors[MU_COLOR_BORDER] = old_border;
                }

                mu_end_panel(ctx);

                mu_begin_panel(ctx, "Skins");
                struct skin_category* cat = &skin_categories[skins_selected_category];

                if(cat->count > 0) {
                        int cell_w = 140 * scalex;
                        int cell_h = 160 * scaley;
                        int panel_w = mu_get_current_container(ctx)->body.w;
                        int cols = panel_w / cell_w;
                        if(cols < 1) cols = 1;

                        int widths[cols];
                        for(int c = 0; c < cols; c++)
                                widths[c] = panel_w / cols;

                        int font_h = ctx->text_height(ctx->style->font);

                        mu_layout_row(ctx, cols, widths, cell_h);

                        skins_preview_cell_count = 0;

                        for(int i = 0; i < cat->count; i++) {
                                if(skins_preview_cell_count >= 256)
                                        break;

                                mu_push_id(ctx, &i, sizeof(i));

                                mu_layout_begin_column(ctx);

                                mu_layout_row(ctx, 1, (int[]) {-1}, cell_h);

                                int is_selected = (skins_selected_entry[skins_selected_category] == i);

                                mu_Color old_button = ctx->style->colors[MU_COLOR_BUTTON];
                                mu_Color old_border = ctx->style->colors[MU_COLOR_BORDER];
                                mu_Color old_text = ctx->style->colors[MU_COLOR_TEXT];

                                if(is_selected) {
                                        ctx->style->colors[MU_COLOR_BORDER] = (mu_Color){255, 255, 0, 255};
                                        ctx->style->colors[MU_COLOR_BUTTON] = (mu_Color){0, 0, 0, 60};
                                } else {
                                        ctx->style->colors[MU_COLOR_BORDER] = (mu_Color){hud_accent_red(), hud_accent_green(), hud_accent_blue(), 255};
                                        ctx->style->colors[MU_COLOR_BUTTON] = (mu_Color){0, 0, 0, 30};
                                }

                                if(mu_button_ex(ctx, "", 0, MU_OPT_ALIGNCENTER)) {
                                        if(skins_apply(skins_selected_category, i, 1) == 0) {
                                                skins_selected_entry[skins_selected_category] = i;
                                                switch(skins_selected_category) {
                                                        case SKIN_SPADE: settings.skin_spade = i; break;
                                                        case SKIN_GRENADE: settings.skin_grenade = i; break;
                                                        case SKIN_RIFLE: settings.skin_rifle = i; break;
                                                        case SKIN_SMG: settings.skin_smg = i; break;
                                                        case SKIN_SHOTGUN: settings.skin_shotgun = i; break;
                                                        case SKIN_INTEL: settings.skin_intel = i; break;
                                                        case SKIN_TENT: settings.skin_tent = i; break;
                                                }
                                                config_save();
                                        }
                                }

                                ctx->style->colors[MU_COLOR_BUTTON] = old_button;
                                ctx->style->colors[MU_COLOR_BORDER] = old_border;

                                mu_Rect r = ctx->last_rect;

                                if(r.w > 0 && r.h > 0) {
                                        int name_overlay_h = font_h + ctx->style->padding * 2;
                                        mu_Rect name_rect = {r.x, r.y + r.h - name_overlay_h, r.w, name_overlay_h};
                                        mu_draw_rect(ctx, name_rect, (mu_Color){0, 0, 0, 140});
                                        ctx->style->colors[MU_COLOR_TEXT] = (mu_Color){255, 255, 255, 255};
                                        mu_draw_control_text(ctx, cat->entries[i].name, name_rect, MU_COLOR_TEXT, MU_OPT_ALIGNCENTER);
                                        ctx->style->colors[MU_COLOR_TEXT] = old_text;

                                        skins_preview_cells_x[skins_preview_cell_count] = r.x + r.w / 2;
                                        int model_area_h = r.h - name_overlay_h;
                                        skins_preview_cells_y[skins_preview_cell_count] = settings.window_height - (r.y + model_area_h * 0.4F);
                                        skins_preview_cat[skins_preview_cell_count] = skins_selected_category;
                                        skins_preview_ent[skins_preview_cell_count] = i;
                                        skins_preview_cell_count++;
                                }

                                mu_layout_end_column(ctx);

                                mu_pop_id(ctx);
                        }
                } else {
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                        mu_text(ctx, "No skins found for this weapon");
                }

                mu_end_panel(ctx);

                mu_end_panel(ctx);

                mu_end_window(ctx);
        }

        for(int k = 0; k < skins_preview_cell_count; k++) {
                skins_render_preview(skins_preview_cat[k], skins_preview_ent[k],
                        skins_preview_cells_x[k], skins_preview_cells_y[k],
                        100.0F * scalex);
        }
}

static void hud_skins_keyboard(int key, int action, int mods, int internal) {
        if(action == WINDOW_PRESS && show_exit && key == WINDOW_KEY_ESCAPE) {
                hud_change(&hud_ingame);
                show_exit = 0;
                window_mousemode(WINDOW_CURSOR_DISABLED);
        }
}

static void hud_skins_touch(void* finger, int action, float x, float y, float dx, float dy) {
        window_setmouseloc(x, y);
}

static struct texture* hud_skins_ui_images(int icon_id, bool* resize) {
        switch(icon_id) {
                case MU_ICON_CHECK: return &texture_ui_box_check;
                case MU_ICON_EXPANDED: return &texture_ui_expanded;
                case MU_ICON_COLLAPSED: return &texture_ui_collapsed;
                default: return NULL;
        }
}

struct hud hud_skins = {
        hud_skins_init,
        NULL,
        hud_skins_render,
        hud_skins_keyboard,
        NULL,
        NULL,
        NULL,
        hud_skins_touch,
        hud_skins_ui_images,
        0,
        0,
        NULL,
};


/*         HUD_CONTROLS START        */

static struct config_key_pair* hud_controls_edit;
static int macro_edit_index = -1;

static void hud_controls_init() {
        hud_controls_edit = NULL;
}

int demo_list_files(char*** out);
static char** demo_files = NULL;
static int    demo_file_count = 0;

static int demo_delete_index = -1;
static int demo_rename_index = -1;
static char demo_rename_buf[256];

static void hud_demolist_init(void) {
        if(!hud_demolist.ctx) hud_demolist.ctx = malloc(sizeof(mu_Context));
        hud_skins.ctx = malloc(sizeof(mu_Context));
        if(demo_files) { for(int i = 0; i < demo_file_count; i++) free(demo_files[i]); free(demo_files); demo_files = NULL; }
        demo_file_count = demo_list_files(&demo_files);
}

static void hud_demolist_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);
        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);
        if(mu_begin_window_ex(ctx, "Demos", frame, MU_OPT_NOFRAME|MU_OPT_NOTITLE|MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;
                hud_common_sidebar(ctx, scalex, scaley);
                mu_begin_panel(ctx, "Content");
                int rb = ctx->text_width(ctx->style->font, "Refresh", 0) * 1.6F;
                mu_layout_row(ctx, 2, (int[]) {-rb, -1}, 0);
                char count_str[64];
                snprintf(count_str, sizeof(count_str), demo_file_count ? "%d demo%s" : "No .demo files found in demos/",
                        demo_file_count, demo_file_count == 1 ? "" : "s");
                mu_button_ex(ctx, count_str, 0, MU_OPT_NOINTERACT);
                if(mu_button_ex(ctx, "Refresh", 17, MU_OPT_ALIGNRIGHT)) hud_demolist_init();
                mu_layout_row(ctx, 1, (int[]) {-1}, -1);
                mu_begin_panel(ctx, "DemoFiles");
                for(int i = 0; i < demo_file_count; i++) {
                        const char* slash = strrchr(demo_files[i], '/');
                        const char* name = slash ? slash+1 : demo_files[i];
                        mu_push_id(ctx, &i, sizeof(i));
                        int bw = ctx->text_width(ctx->style->font, "Delete", 0) * 1.6F;
                        int sp = ctx->style->spacing;
#ifdef __ANDROID__
                        /* Share exports the demo through the system share sheet,
                           the only way off the app's sandboxed storage on Android. */
                        mu_layout_row(ctx, 4, (int[]) {-(bw * 3 + sp * 4 + 1), bw, bw, bw}, 0);
#else
                        mu_layout_row(ctx, 3, (int[]) {-(bw * 2 + sp * 3 + 1), bw, bw}, 0);
#endif
                        if(mu_button_ex(ctx, name, 0, MU_OPT_NOFRAME)) {
                                if(demo_playback_open(demo_files[i])) {
                                        /* Initialize game state for demo playback */
                                        camera_mode = CAMERAMODE_SPECTATOR;
                                        cameracontroller_reset_spectator_velocity();
                                        /* Process initial packets to load map and set up world state */
                                        network_update();
                                        hud_change(&hud_ingame);
                                }
                        }
#ifdef __ANDROID__
                        if(mu_button_ex(ctx, "Share", 0, MU_OPT_NOFRAME)) {
                                window_share_file(demo_files[i]);
                        }
#endif
                        if(mu_button_ex(ctx, "Delete", 0, MU_OPT_NOFRAME)) {
                                demo_delete_index = i;
                        }
                        if(mu_button_ex(ctx, "Rename", 0, MU_OPT_NOFRAME)) {
                                demo_rename_index = i;
                                const char* slash2 = strrchr(demo_files[i], '/');
                                const char* base = slash2 ? slash2+1 : demo_files[i];
                                strncpy(demo_rename_buf, base, sizeof(demo_rename_buf) - 1);
                                demo_rename_buf[sizeof(demo_rename_buf) - 1] = 0;
                        }
                        mu_pop_id(ctx);
                }
                if(!demo_file_count) {
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                        mu_button_ex(ctx, "Use auto-record in Settings, or drop .demo files into demos/",
                                0, MU_OPT_NOFRAME|MU_OPT_ALIGNCENTER|MU_OPT_NOINTERACT);
                }
                mu_end_panel(ctx);
                mu_end_panel(ctx); mu_end_window(ctx);
        }

        /* --- Delete confirm dialog --- */
        if(demo_delete_index >= 0 && demo_delete_index < demo_file_count) {
                if(mu_begin_window_ex(ctx, "Confirm Delete", mu_rect(0, 0, 320, 130),
                         MU_OPT_HOLDFOCUS | MU_OPT_NORESIZE | MU_OPT_NOCLOSE)) {
                        mu_Container* cnt = mu_get_current_container(ctx);
                        mu_bring_to_front(ctx, cnt);
                        cnt->rect = mu_rect((settings.window_width - 320) / 2, 280, 320, 130);
                        mu_layout_row(ctx, 1, (int[]) {-1}, -ctx->text_height(ctx->style->font) * 1.5F);
                        mu_text(ctx, "Are you sure you want to delete this demo?");
                        int bw = ctx->text_width(ctx->style->font, "Yes", 0) * 1.6F;
                        mu_layout_row(ctx, 2, (int[]) {-bw, -1}, 0);
                        if(mu_button(ctx, "Yes")) {
                                remove(demo_files[demo_delete_index]);
                                demo_delete_index = -1;
                                hud_demolist_init();
                        }
                        if(mu_button(ctx, "No")) {
                                demo_delete_index = -1;
                        }
                        mu_end_window(ctx);
                }
        }

        /* --- Rename dialog --- */
        if(demo_rename_index >= 0 && demo_rename_index < demo_file_count) {
                if(mu_begin_window_ex(ctx, "Rename Demo", mu_rect(0, 0, 400, 130),
                         MU_OPT_HOLDFOCUS | MU_OPT_NORESIZE | MU_OPT_NOCLOSE)) {
                        mu_Container* cnt = mu_get_current_container(ctx);
                        mu_bring_to_front(ctx, cnt);
                        cnt->rect = mu_rect((settings.window_width - 400) / 2, 280, 400, 130);
                        mu_layout_row(ctx, 1, (int[]) {-1}, -ctx->text_height(ctx->style->font) * 1.5F);
                        mu_text(ctx, "Enter new name for the demo file:");
                        mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                        int res = hud_textbox(ctx, demo_rename_buf, sizeof(demo_rename_buf), 0);
                        int bw = ctx->text_width(ctx->style->font, "Rename", 0) * 1.6F;
                        mu_layout_row(ctx, 2, (int[]) {-bw, -1}, 0);
                        if(mu_button(ctx, "Rename") || (res & MU_RES_SUBMIT)) {
                                const char* slash = strrchr(demo_files[demo_rename_index], '/');
                                char new_path[512];
                                if(slash) {
                                        int dir_len = slash - demo_files[demo_rename_index];
                                        memcpy(new_path, demo_files[demo_rename_index], dir_len);
                                        new_path[dir_len] = '/';
                                        new_path[dir_len + 1] = 0;
                                } else {
                                        new_path[0] = 0;
                                }
                                char* ext = strrchr(demo_rename_buf, '.');
                                if(!ext || strcmp(ext, ".demo") != 0) {
                                        strncat(demo_rename_buf, ".demo", sizeof(demo_rename_buf) - strlen(demo_rename_buf) - 1);
                                }
                                strncat(new_path, demo_rename_buf, sizeof(new_path) - strlen(new_path) - 1);
                                rename(demo_files[demo_rename_index], new_path);
                                demo_rename_index = -1;
                                hud_demolist_init();
                        }
                        if(mu_button(ctx, "Cancel")) {
                                demo_rename_index = -1;
                        }
                        mu_end_window(ctx);
                }
        }
}
static void hud_demolist_keyboard(int key, int action, int mods, int internal) {
        (void)key; (void)action; (void)mods; (void)internal;
}
static void hud_demolist_touch(void* finger, int action, float x, float y, float dx, float dy) {
        (void)finger; (void)action; (void)dx; (void)dy;
        window_setmouseloc(x, y);
}
struct hud hud_demolist = { hud_demolist_init, NULL, hud_demolist_render, hud_demolist_keyboard,
        NULL, NULL, NULL, hud_demolist_touch, NULL, 0, 0, NULL, };

static void hud_controls_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);

        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);

        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;

                hud_common_sidebar(ctx, scalex, scaley);

                mu_begin_panel(ctx, "ControlsContent");

                mu_layout_row(ctx, 1, (int[]) {-1}, -1);

                mu_begin_panel(ctx, "Content");

                char* category = NULL;
                int open = 0;
                for(int k = 0; k < list_size(&config_keys); k++) {
                        struct config_key_pair* a = list_get(&config_keys, k);

                        if(*a->display) {
                                if(!category || strcmp(category, a->category)) {
                                        category = a->category;

                                        open = mu_header_ex(ctx, a->category, MU_OPT_EXPANDED);
                                }

                                if(open) {
                                        int width = mu_get_current_container(ctx)->body.w;
                                        if(a->def != a->original) {
                                                mu_layout_row(ctx, 4,
                                                                          (int[]) {0.65F * width, ctx->text_width(ctx->style->font, "Reset", 0) * 1.5F,
                                                                                           -0.05F * width, -1},
                                                                          0);
                                        } else {
                                                mu_layout_row(ctx, 3, (int[]) {0.65F * width, -0.05F * width, -1}, 0);
                                        }

                                        mu_push_id(ctx, a->display, sizeof(a->display));
                                        mu_text(ctx, a->display);

                                        if(a->def != a->original && mu_button(ctx, "Reset")) {
                                                a->def = a->original;
                                                config_save();
                                        }

                                        char name[32];
                                        window_keyname(a->def, name, sizeof(name));

                                        if(hud_controls_edit == a)
                                                mu_text_color(ctx, 255, 0, 0);
                                        if(mu_button(ctx, name))
                                                hud_controls_edit = (hud_controls_edit == a) ? NULL : a;
                                        mu_text_color_default(ctx);
                                        mu_pop_id(ctx);

                                        mu_push_id(ctx, a->name, sizeof(a->name));
                                        int pw = ctx->text_width(ctx->style->font, a->name, 0) + ctx->style->padding * 4;
                                        int ph = ctx->style->size.y + ctx->style->padding * 4 + ctx->style->title_height;
                                        if(mu_begin_window_ex(ctx, "Help",
                                                                                  mu_rect((settings.window_width - pw) / 2,
                                                                                                  (settings.window_height - ph) / 2, pw, ph),
                                                                                  MU_OPT_AUTOSIZE | MU_OPT_NORESIZE | MU_OPT_NOSCROLL | MU_OPT_POPUP | MU_OPT_CLOSED)) {
                                mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                                                mu_text(ctx, a->name);
                                                mu_end_window(ctx);
                                        }
                                        if(mu_button(ctx, "?")) {
                                                mu_open_popup(ctx, "Help");
                                                mu_Container* cnt = mu_get_container(ctx, "Help");
                                                if(cnt)
                                                        cnt->rect = mu_rect((settings.window_width - pw) / 2,
                                                                                                (settings.window_height - ph) / 2, pw, ph);
                                        }
                                        mu_pop_id(ctx);
                                }
                        }
                }

                mu_end_panel(ctx);

                mu_end_panel(ctx);

                mu_end_window(ctx);
        }
}

static void hud_controls_touch(void* finger, int action, float x, float y, float dx, float dy) {
        window_setmouseloc(x, y);
}

static void hud_controls_keyboard(int key, int action, int mods, int internal) {
        if(hud_controls_edit) {
                hud_controls_edit->def = internal;
                hud_controls_edit = NULL;
                config_save();
        }

        if(action == WINDOW_PRESS && show_exit && key == WINDOW_KEY_ESCAPE) {
                hud_change(&hud_ingame);
                show_exit = 0;
                window_mousemode(WINDOW_CURSOR_DISABLED);
        }
}

struct hud hud_controls = {
        hud_controls_init,
        NULL,
        hud_controls_render,
        hud_controls_keyboard,
        NULL,
        NULL,
        NULL,
        hud_controls_touch,
        hud_settings_ui_images,
        0,
        0,
        NULL,
};

/*         HUD_MACROS START        */

static void hud_macros_init(void) {
        macro_edit_index = -1;
}

static void hud_macros_touch(void* finger, int action, float x, float y, float dx, float dy) {
        (void)finger;
        window_setmouseloc(x, y);
        if(action == TOUCH_MOVE && hud_macros.ctx)
                mu_input_scroll(hud_macros.ctx, 0, (int)(-dy));
}

static void hud_macros_keyboard(int key, int action, int mods, int internal) {
        if(macro_edit_index >= 0 && action == WINDOW_PRESS
           && hud_macros.ctx && hud_macros.ctx->focus == 0) {
                struct config_macro* m = list_get(&config_macros, macro_edit_index);
                m->key = internal;
                macro_edit_index = -1;
                config_save();
                return;
        }
        if(key == WINDOW_KEY_ESCAPE && action == WINDOW_PRESS) {
                hud_change(&hud_serverlist);
        }
}

static void hud_macros_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);
        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);
        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;
                hud_common_sidebar(ctx, scalex, scaley);
                mu_begin_panel(ctx, "Content");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);
                if(mu_button(ctx, "+ Add macro")) {
                        struct config_macro new_macro = {0};
                        list_add(&config_macros, &new_macro);
                        macro_edit_index = list_size(&config_macros) - 1;
                        config_save();
                }
                mu_layout_row(ctx, 1, (int[]) {-1}, -1);
                mu_begin_panel(ctx, "MacrosList");
                int width = mu_get_current_container(ctx)->body.w;
                int spacing = ctx->style->spacing;
                int del_w = (int)(ctx->text_width(ctx->style->font, "x", 0) * 2.0F);
                int key_w = (int)(ctx->text_width(ctx->style->font, "Unbound", 0) + ctx->style->padding * 2);
                int col2_total = key_w + del_w;
                if(col2_total > width * 0.35F) {
                        col2_total = (int)(width * 0.35F);
                        key_w = col2_total - del_w;
                }
                int col1_w = width - col2_total - spacing * 2;
                if(col1_w < 1) col1_w = 1;
                mu_layout_row(ctx, 2, (int[]) {col1_w, col2_total}, 0);
                mu_button_ex(ctx, "String", 0, MU_OPT_ALIGNCENTER);
                mu_button_ex(ctx, "Keybind", 0, MU_OPT_ALIGNCENTER);
                for(int k = 0; k < list_size(&config_macros); k++) {
                        struct config_macro* m = list_get(&config_macros, k);
                        mu_push_id(ctx, &k, sizeof(k));
                        char key_label[64];
                        if(m->key > 0) {
                                window_keyname(m->key, key_label, sizeof(key_label));
                        } else {
                                strcpy(key_label, "Unbound");
                        }
                        mu_layout_row(ctx, 3, (int[]) {col1_w, key_w, del_w}, 0);
                        hud_textbox(ctx, m->text, sizeof(m->text), 0);
                        if(macro_edit_index == k) {
                                mu_text_color(ctx, 255, 0, 0);
                                mu_button_ex(ctx, "...", 0, MU_OPT_NOINTERACT);
                                mu_text_color_default(ctx);
                        } else {
                                if(mu_button(ctx, key_label))
                                        macro_edit_index = k;
                        }
                        mu_text_color(ctx, 255, 60, 60);
                        if(mu_button(ctx, "x")) {
                                list_remove(&config_macros, k);
                                macro_edit_index = -1;
                                config_save();
                        }
                        mu_text_color_default(ctx);
                        mu_pop_id(ctx);
                }
                mu_end_panel(ctx);
                mu_end_panel(ctx);
                mu_end_window(ctx);
        }
}

static bool hud_recording_waiting_for_key = false;
static bool hud_replay_waiting_for_key = false;

static void hud_recording_init(void) {
        recorder_set_fps(settings.recording_fps);
        recorder_set_bitrate(settings.recording_bitrate_kbps);
}

static void hud_recording_keyboard(int key, int action, int mods, int internal) {
        (void)mods;
        (void)internal;
        if(key == WINDOW_KEY_ESCAPE && action == WINDOW_PRESS) {
                hud_change(&hud_serverlist);
        }
        if(hud_recording_waiting_for_key && action == WINDOW_PRESS) {
                struct config_key_pair* pk = config_key(WINDOW_KEY_RECORDING);
                if(pk) {
                        pk->def = internal;
                        config_save();
                }
                hud_recording_waiting_for_key = false;
        }
}

static void hud_recording_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);
        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);
        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;
                hud_common_sidebar(ctx, scalex, scaley);
                mu_begin_panel(ctx, "Content");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                mu_text(ctx, "Video Recording");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                int label_w = 120;
                mu_layout_row(ctx, 2, (int[]) {label_w, -1}, 0);

                mu_label(ctx, "Recording FPS:");
                char fps_label[16];
                snprintf(fps_label, sizeof(fps_label), "%d", settings.recording_fps);
                if(mu_button(ctx, fps_label)) {
                        settings.recording_fps = (settings.recording_fps == 60) ? 30 : 60;
                        recorder_set_fps(settings.recording_fps);
                        config_save();
                }

                mu_label(ctx, "Bitrate (kbps):");
                int old_bitrate = settings.recording_bitrate_kbps;
                int_slider(ctx, &settings.recording_bitrate_kbps, 500, 50000);
                if(settings.recording_bitrate_kbps != old_bitrate) {
                        recorder_set_bitrate(settings.recording_bitrate_kbps);
                        config_save();
                }

                mu_layout_row(ctx, 2, (int[]) {label_w, -1}, 0);
                mu_label(ctx, "Toggle Key:");
                char key_label_rec[32];
                struct config_key_pair* pk_rec = config_key(WINDOW_KEY_RECORDING);
                if (pk_rec) {
                        window_keyname(pk_rec->def, key_label_rec, sizeof(key_label_rec));
                } else {
                        strcpy(key_label_rec, "Unknown");
                }
                if(hud_recording_waiting_for_key)
                        mu_text_color(ctx, 255, 0, 0);
                if(mu_button(ctx, key_label_rec))
                        hud_recording_waiting_for_key = !hud_recording_waiting_for_key;
                mu_text_color_default(ctx);

                mu_layout_row(ctx, 2, (int[]) {label_w, -1}, 0);
                mu_label(ctx, "Status:");
                if(recorder_is_recording_active()) {
                        mu_text_color(ctx, 255, 60, 60);
                        mu_text(ctx, "Recording");
                        mu_text_color_default(ctx);
                } else {
                        mu_text(ctx, "Not recording");
                }

                mu_layout_row(ctx, 1, (int[]) {-1}, -1);

                mu_end_panel(ctx);
                mu_end_window(ctx);
        }
}

static void hud_replay_init(void) {
}

static void hud_replay_keyboard(int key, int action, int mods, int internal) {
        if(key == WINDOW_KEY_ESCAPE && action == WINDOW_PRESS) {
                hud_change(&hud_serverlist);
        }
        if(hud_replay_waiting_for_key && action == WINDOW_PRESS) {
                struct config_key_pair* pk = config_key(WINDOW_KEY_REPLAY_SAVE);
                if(pk) {
                        pk->def = internal;
                        config_save();
                }
                hud_replay_waiting_for_key = false;
        }
}

static void hud_replay_render(mu_Context* ctx, float scalex, float scaley) {
        hud_common_render(ctx);
        mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);
        if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
                mu_Container* cnt = mu_get_current_container(ctx);
                cnt->rect = frame;
                hud_common_sidebar(ctx, scalex, scaley);
                mu_begin_panel(ctx, "Content");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                mu_text(ctx, "Replay Settings");
                mu_layout_row(ctx, 1, (int[]) {-1}, 0);

                int label_w = 120;
                mu_layout_row(ctx, 2, (int[]) {label_w, -1}, 0);

                mu_label(ctx, "Replay Buffer:");
                char* replay_toggle = recorder_is_buffer_active() ? "Stop" : "Start";
                if(mu_button(ctx, replay_toggle)) {
                        settings.replay_enabled = !recorder_is_buffer_active();
                        recorder_toggle_buffer();
                        config_save();
                }

                mu_label(ctx, "Status:");
                if(recorder_is_buffer_active()) {
                        mu_text_color(ctx, 255, 60, 60);
                        mu_text(ctx, "Buffer Active");
                        mu_text_color_default(ctx);
                } else {
                        mu_text(ctx, "Buffer Off");
                }

                mu_label(ctx, "Duration (s):");
                if(!recorder_is_buffer_active()) {
                        int_slider(ctx, &settings.replay_duration, 10, 120);
                } else {
                        char dur_str[16];
                        snprintf(dur_str, sizeof(dur_str), "%d", settings.replay_duration);
                        mu_text(ctx, dur_str);
                }

                mu_label(ctx, "Save Hotkey:");
                char key_label_rep[32];
                struct config_key_pair* pk_rep = config_key(WINDOW_KEY_REPLAY_SAVE);
                if (pk_rep) {
                        window_keyname(pk_rep->def, key_label_rep, sizeof(key_label_rep));
                } else {
                        strcpy(key_label_rep, "Unknown");
                }
                if(hud_replay_waiting_for_key)
                        mu_text_color(ctx, 255, 0, 0);
                if(mu_button(ctx, key_label_rep))
                        hud_replay_waiting_for_key = !hud_replay_waiting_for_key;
                mu_text_color_default(ctx);

                mu_layout_row(ctx, 1, (int[]) {-1}, -1);
                mu_end_panel(ctx);
                mu_end_window(ctx);
        }
}

struct hud hud_recording = {
        hud_recording_init,
        NULL,
        hud_recording_render,
        hud_recording_keyboard,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        0,
        NULL,
};

struct hud hud_replay = {
        hud_replay_init,
        NULL,
        hud_replay_render,
        hud_replay_keyboard,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        0,
        0,
        NULL,
};



struct hud hud_macros = {
        hud_macros_init,
        NULL,
        hud_macros_render,
        hud_macros_keyboard,
        NULL,
        NULL,
        NULL,
        hud_macros_touch,
        NULL,
        0,
        0,
        NULL,
};

void hud_common_render_for_chatlog(mu_Context* ctx) {
        hud_common_render(ctx);
}

void hud_common_sidebar_for_chatlog(mu_Context* ctx, float scalex, float scaley) {
        hud_common_sidebar(ctx, scalex, scaley);
}
