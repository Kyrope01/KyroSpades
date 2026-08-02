
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "common.h"
#include "main.h"
#include "hud.h"
#include "chatlog.h"
#include "chathistory.h"
#include "window.h"
#include "font.h"
#include "config.h"
#include "network.h"
#include "player.h"
#include "texture.h"

void hud_common_render_for_chatlog(mu_Context* ctx);
void hud_common_sidebar_for_chatlog(mu_Context* ctx, float scalex, float scaley);
static void resolve_mouse_target(mu_Context* ctx, int* out_line, int* out_char);

#define CHATLOG_MAX_LINES 2048
#define CHATLOG_TEXT_HEIGHT 16

static int chatlog_text_height(void) {
	return (int)(CHATLOG_TEXT_HEIGHT * hud_ui_scale());
}
/* Must match the second dimension of chat[][][] in main.c. Reading past
   this is undefined - the loop walked into adjacent memory before. */
#define CHATLOG_RING_SIZE 128
#define CHATLOG_MAX_URLS_PER_LINE 4

struct url_span {
	int start;
	int end;
};

struct visible_line {
	/* Raw text source (with color codes). For live entries this points
	   into chat[][]; for history into the chathistory buffer. NULL for
	   placeholder/separator lines. */
	const char* raw;
	/* Byte length to read from `raw`. -1 means read until '\0' (the
	   classic single-segment case). Set to a finite length for the
	   second-and-later segments produced when a single chat message
	   contains '\n' and we want each segment on its own visible row. */
	int raw_len;
	/* 1 when this line is a continuation of a previous \n-split message.
	   Suppresses the "[Global]" prefix and player-name extraction for
	   continuation rows (only the first row carries the name). */
	int is_continuation;
	unsigned int color;
	char plain[256];
	int plain_len;
	mu_Rect rect;
	struct url_span urls[CHATLOG_MAX_URLS_PER_LINE];
	int url_count;
	int name_start;
	int name_len;
	int is_placeholder;
	int placeholder_count;     /* "N hidden" if > 0; 0 = day separator */
};

static struct visible_line lines[CHATLOG_MAX_LINES];
static int line_count = 0;

/* Selection: positions are (line index into `lines`, byte offset into that
   line's plain text). sel_*_line < 0 means no selection. */
static int sel_anchor_line = -1;
static int sel_anchor_char = 0;
static int sel_active_line = -1;
static int sel_active_char = 0;

static int lmb_dragging = 0;
static int pending_anchor = 0;

static int needs_scroll_to_bottom = 1;
static intptr_t prev_top_chat_idx = -1;
static int prev_top_chat_len = 0;
static int prev_session_count = 0;
/* Last observed chathistory_count(); used to detect prepends so we can
   anchor the scroll position only when older lines were actually loaded. */
static int prev_history_count = 0;

#define CTXMENU_W 220
#define CTXMENU_H 26

static int ctxmenu_height(void) {
	int h = chatlog_text_height() + 12;
	return h < CTXMENU_H ? CTXMENU_H : h;
}
#define CTXMENU_PREVIEW 18

enum {
	CTXMENU_KIND_COPY,
	CTXMENU_KIND_FILTER,
	CTXMENU_KIND_FILTER_CLEAR,
	CTXMENU_KIND_SELECT_ALL,
};

static int   ctxmenu_visible = 0;
static int   ctxmenu_kind    = CTXMENU_KIND_COPY;
static int   ctxmenu_x = 0;
static int   ctxmenu_y = 0;
static char  ctxmenu_label[96] = {0};
static char  ctxmenu_payload[64] = {0};
static mu_Rect ctxmenu_rect;

/* Active filter: only show lines from this player when set. Empty = off.
   Comparison is exact, case-sensitive against the extracted player name. */
static char filter_active_name[64] = {0};
static int  filter_hide_server = 0;
static mu_Rect filter_clear_btn;
static mu_Rect filter_server_btn;


static int    linkmodal_visible = 0;
static char   linkmodal_url[1024] = {0};
static mu_Rect linkmodal_rect;
static mu_Rect linkmodal_visit_btn;
static mu_Rect linkmodal_cancel_btn;

/* The press that may turn into a link click. We resolve which URL (if any)
   is under the cursor at LMB-press time, then commit on release iff the
   user did not drag and the cursor still rests on the same URL. */
static int  pending_url_line = -1;
static int  pending_url_idx  = -1;
static int  pending_url_press_x = 0;
static int  pending_url_press_y = 0;

/* Tracked so we only flip the OS cursor when state actually changes. */
static int hand_cursor_active = 0;

/* Captured at the end of each render so the mouse handler can tell
   whether a press landed on the scrollbar / outside the line area, in
   which case we must NOT start a text selection. Without this, dragging
   the scroll thumb sweeps the cursor across many line rects and grows
   a runaway selection. */
static mu_Rect content_body_rect = {0, 0, 0, 0};

/* ---------------------------------------------------------------- */
/*  Find-in-chat (Notepad-style search bar)                         */
/* ---------------------------------------------------------------- */

#define CHATLOG_SEARCH_QUERY_MAX   96
#define CHATLOG_SEARCH_BAR_H       28
/* Cap on tracked matches per render. Worst case for the per-frame
   match build is line_count * matches_per_line; with line_count up to
   CHATLOG_MAX_LINES (2048) and a degenerate query like "a" against a
   chat full of "aaa...", match_count would otherwise grow without
   bound. 4096 covers ~2 hits per visible line, which is more than
   anyone navigates manually anyway - hits past the cap are silently
   dropped and the counter clamps. */
#define CHATLOG_SEARCH_MAX_MATCHES 4096

/* Raw key codes for letters we detect via the `internal` parameter on
   the keyboard callback - the configurable WINDOW_KEY_* mapping doesn't
   cover the alphabetic letters we need (F for Ctrl+F, A for Ctrl+A),
   so we look at the raw code instead. Same trick as the chat input's
   Home/End.
   SDL uses lowercase SDLK_* values; GLFW uses uppercase ASCII. */
#ifdef USE_SDL
#define CHATLOG_KEY_F  SDLK_f
#define CHATLOG_KEY_A  SDLK_a
#else
#define CHATLOG_KEY_F  70
#define CHATLOG_KEY_A  65
#endif

struct search_match {
	int line;       /* index into `lines` */
	int char_start; /* byte offset into lines[line].plain (inclusive) */
	int char_end;   /* exclusive */
};

enum {
	SEARCH_NAV_NONE = 0,
	SEARCH_NAV_LAST,    /* jump to the most recent match (post query edit) */
	SEARCH_NAV_NEXT,
	SEARCH_NAV_PREV,
};

static int   search_visible    = 0;
static char  search_query[CHATLOG_SEARCH_QUERY_MAX] = {0};
static int   search_query_len  = 0;
static struct search_match search_matches[CHATLOG_SEARCH_MAX_MATCHES];
static int   search_match_count = 0;
static int   search_current     = 0;       /* index into search_matches */
/* Per-line offset into search_matches: matches whose .line == i live
   in the half-open range [matches_line_start[i], matches_line_start[i+1]).
   Lets render_chat_line dispatch only that line's hits instead of
   scanning the full match array per row. The +1 slot holds the total
   match count as a sentinel. */
static int   matches_line_start[CHATLOG_MAX_LINES + 1] = {0};
/* Pending navigation request set by user input (key/click) and applied
   in render after recompute_matches has the fresh match array. We
   can't apply navigation eagerly because the matches don't exist yet -
   they're rebuilt every frame from the freshly-laid-out visible lines. */
static int   search_nav_request = SEARCH_NAV_NONE;
/* Match index whose line should be scrolled into view this frame; -1
   means no pending scroll. Set after navigation is applied; consumed
   AFTER mu_end_panel below so it overrides the bottom-stick logic
   (otherwise an incoming live message during navigation snaps the
   user back to "now" instead of leaving them on the hit). */
static int   search_pending_scroll = -1;

/* Hit rects for the search bar widgets, populated each render so the
   mouse handler can dispatch clicks. Zeroed out when the bar is
   hidden so stale rects don't accidentally swallow clicks. */
static mu_Rect search_bar_rect    = {0, 0, 0, 0};
static mu_Rect search_btn_prev    = {0, 0, 0, 0};
static mu_Rect search_btn_next    = {0, 0, 0, 0};
static mu_Rect search_btn_close   = {0, 0, 0, 0};

static int is_url_byte(unsigned char c) {
	if(c <= 0x20) return 0;
	switch(c) {
		case '"': case '\'': case '<': case '>': case '`':
		case '{': case '}': case '|': case '\\': case '^':
			return 0;
	}
	return 1;
}

/* Trim trailing punctuation that almost never belongs to a URL when the
   URL is the last token in a sentence ("see https://x.com/foo." etc). */
static int url_strip_trailing(const char* s, int start, int end) {
	while(end > start) {
		char c = s[end - 1];
		if(c == '.' || c == ',' || c == ';' || c == ':' || c == '!' ||
		   c == '?' || c == ')' || c == ']' || c == '}') {
			end--;
			continue;
		}
		break;
	}
	return end;
}

static void scan_urls(struct visible_line* vl) {
	vl->url_count = 0;
	const char* s = vl->plain;
	int len = vl->plain_len;
	for(int i = 0; i < len && vl->url_count < CHATLOG_MAX_URLS_PER_LINE; ) {
		int matched = 0;
		if(i + 7 <= len && strncmp(s + i, "http://", 7) == 0) matched = 7;
		else if(i + 8 <= len && strncmp(s + i, "https://", 8) == 0) matched = 8;
		if(!matched) { i++; continue; }

		int start = i;
		int j = i + matched;
		while(j < len && is_url_byte((unsigned char)s[j])) j++;
		j = url_strip_trailing(s, start, j);
		/* Need at least one char of host past the scheme. */
		if(j > start + matched) {
			vl->urls[vl->url_count].start = start;
			vl->urls[vl->url_count].end   = j;
			vl->url_count++;
		}
		i = (j > start) ? j : (start + 1);
	}
}

static int url_at_position(int line_index, int char_offset) {
	if(line_index < 0 || line_index >= line_count) return -1;
	struct visible_line* vl = &lines[line_index];
	for(int i = 0; i < vl->url_count; i++) {
		if(char_offset >= vl->urls[i].start && char_offset < vl->urls[i].end)
			return i;
	}
	return -1;
}

static void linkmodal_open(const char* url) {
	if(!url || !*url) return;
	strncpy(linkmodal_url, url, sizeof(linkmodal_url) - 1);
	linkmodal_url[sizeof(linkmodal_url) - 1] = 0;
	linkmodal_visible = 1;
	/* Selection highlight would distract from the modal. */
	sel_anchor_line = sel_active_line = -1;
	ctxmenu_visible = 0;
}

static void linkmodal_close(void) {
	linkmodal_visible = 0;
	linkmodal_url[0] = 0;
}

/* Strip inline color codes (1..7). When `max_bytes` >= 0 the read stops
   after that many source bytes; '\n' always acts as a terminator so
   newline-split segments don't bleed plain text into each other. */
static int strip_color_codes_n(const char* src, int max_bytes, char* dst) {
	int j = 0;
	for(int i = 0; (max_bytes < 0 || i < max_bytes) && src[i]; i++) {
		unsigned char c = (unsigned char)src[i];
		if(c == '\n') break;
		if(c >= 1 && c <= 7) continue;
		if(c == 0xFF) continue;
		dst[j++] = src[i];
	}
	dst[j] = 0;
	return j;
}

static int strip_color_codes(const char* src, char* dst) {
	return strip_color_codes_n(src, -1, dst);
}

static float plain_prefix_width(const char* s, int n) {
	if(n <= 0) return 0.f;
	char buf[256];
	if(n > 255) n = 255;
	memcpy(buf, s, n);
	buf[n] = 0;
	return font_length((float)chatlog_text_height(), buf);
}

static int plain_char_at_offset(const char* plain, int plain_len, float rel_x) {
	if(rel_x <= 0.f) return 0;
	float prev_w = 0.f;
	for(int i = 1; i <= plain_len; i++) {
		float w = plain_prefix_width(plain, i);
		if(w >= rel_x) {
			return (rel_x - prev_w < w - rel_x) ? (i - 1) : i;
		}
		prev_w = w;
	}
	return plain_len;
}

static void selection_ordered(int* start_line, int* start_char,
							  int* end_line, int* end_char) {
	int al = sel_anchor_line, ac = sel_anchor_char;
	int bl = sel_active_line, bc = sel_active_char;
	if(al < bl || (al == bl && ac <= bc)) {
		*start_line = al; *start_char = ac;
		*end_line   = bl; *end_char   = bc;
	} else {
		*start_line = bl; *start_char = bc;
		*end_line   = al; *end_char   = ac;
	}
}

static int has_selection(void) {
	return sel_anchor_line >= 0 && sel_active_line >= 0
		&& !(sel_anchor_line == sel_active_line && sel_anchor_char == sel_active_char);
}

static char* build_selection_text(void) {
	if(!has_selection()) return NULL;

	int s_line, s_char, e_line, e_char;
	selection_ordered(&s_line, &s_char, &e_line, &e_char);
	if(s_line < 0 || e_line >= line_count) return NULL;

	size_t cap = 1;
	for(int i = s_line; i <= e_line; i++) cap += lines[i].plain_len + 1;
	char* buf = (char*)malloc(cap);
	if(!buf) return NULL;
	size_t pos = 0;

	for(int i = s_line; i <= e_line; i++) {
		int from = (i == s_line) ? s_char : 0;
		int to   = (i == e_line) ? e_char : lines[i].plain_len;
		if(from < 0) from = 0;
		if(to > lines[i].plain_len) to = lines[i].plain_len;
		if(to > from) {
			memcpy(buf + pos, lines[i].plain + from, to - from);
			pos += (to - from);
		}
		if(i < e_line) buf[pos++] = '\n';
	}
	buf[pos] = 0;
	return buf;
}

static void copy_selection_to_clipboard(void) {
	char* sel = build_selection_text();
	if(!sel) return;
	if(*sel) window_setclipboard(sel);
	free(sel);
}

static void ctxmenu_hide(void) {
	ctxmenu_visible = 0;
}

static void ctxmenu_show_copy(int x, int y) {
	char* sel = build_selection_text();
	if(!sel || !*sel) {
		if(sel) free(sel);
		ctxmenu_visible = 0;
		return;
	}

	char preview[CTXMENU_PREVIEW + 1];
	int n = 0;
	for(int i = 0; sel[i] && n < CTXMENU_PREVIEW; i++) {
		char c = sel[i];
		preview[n++] = (c == '\n' || c == '\r' || c == '\t') ? ' ' : c;
	}
	preview[n] = 0;
	int truncated = (sel[n] != 0);
	free(sel);

	snprintf(ctxmenu_label, sizeof(ctxmenu_label),
			 truncated ? "Copy \"%s\u2026\"" : "Copy \"%s\"", preview);
	ctxmenu_kind = CTXMENU_KIND_COPY;
	ctxmenu_payload[0] = 0;

	int max_x = settings.window_width - CTXMENU_W - 4;
	int max_y = settings.window_height - ctxmenu_height() - 4;
	if(x > max_x) x = max_x;
	if(y > max_y) y = max_y;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	ctxmenu_x = x;
	ctxmenu_y = y;
	ctxmenu_rect = mu_rect(x, y, CTXMENU_W, ctxmenu_height());
	ctxmenu_visible = 1;
}

/* Width sized to the label (with horizontal padding) so longer payloads
   like "Show only messages from <long_name>" don't get clipped. */
static int ctxmenu_label_width(const char* label) {
	int w = (int)font_length((float)chatlog_text_height(), (char*)label) + 24;
	if(w < CTXMENU_W) w = CTXMENU_W;
	return w;
}

static void ctxmenu_show_filter(int x, int y, const char* name) {
	if(!name || !*name) { ctxmenu_visible = 0; return; }

	strncpy(ctxmenu_payload, name, sizeof(ctxmenu_payload) - 1);
	ctxmenu_payload[sizeof(ctxmenu_payload) - 1] = 0;
	snprintf(ctxmenu_label, sizeof(ctxmenu_label),
			 "Show only messages from %s", ctxmenu_payload);
	ctxmenu_kind = CTXMENU_KIND_FILTER;

	int w = ctxmenu_label_width(ctxmenu_label);
	int max_x = settings.window_width - w - 4;
	int max_y = settings.window_height - ctxmenu_height() - 4;
	if(x > max_x) x = max_x;
	if(y > max_y) y = max_y;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	ctxmenu_x = x;
	ctxmenu_y = y;
	ctxmenu_rect = mu_rect(x, y, w, ctxmenu_height());
	ctxmenu_visible = 1;
}

static void ctxmenu_show_filter_clear(int x, int y) {
	ctxmenu_payload[0] = 0;
	snprintf(ctxmenu_label, sizeof(ctxmenu_label), "Clear filter");
	ctxmenu_kind = CTXMENU_KIND_FILTER_CLEAR;

	int max_x = settings.window_width - CTXMENU_W - 4;
	int max_y = settings.window_height - ctxmenu_height() - 4;
	if(x > max_x) x = max_x;
	if(y > max_y) y = max_y;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	ctxmenu_x = x;
	ctxmenu_y = y;
	ctxmenu_rect = mu_rect(x, y, CTXMENU_W, ctxmenu_height());
	ctxmenu_visible = 1;
}

/* Right-click on empty chat-log space: companion to Ctrl+A. */
static void ctxmenu_show_select_all(int x, int y) {
	ctxmenu_payload[0] = 0;
	snprintf(ctxmenu_label, sizeof(ctxmenu_label), "Select all");
	ctxmenu_kind = CTXMENU_KIND_SELECT_ALL;

	int max_x = settings.window_width - CTXMENU_W - 4;
	int max_y = settings.window_height - ctxmenu_height() - 4;
	if(x > max_x) x = max_x;
	if(y > max_y) y = max_y;
	if(x < 0) x = 0;
	if(y < 0) y = 0;

	ctxmenu_x = x;
	ctxmenu_y = y;
	ctxmenu_rect = mu_rect(x, y, CTXMENU_W, ctxmenu_height());
	ctxmenu_visible = 1;
}

static int ctxmenu_hit(int mx, int my) {
	return ctxmenu_visible
		&& mx >= ctxmenu_rect.x && mx < ctxmenu_rect.x + ctxmenu_rect.w
		&& my >= ctxmenu_rect.y && my < ctxmenu_rect.y + ctxmenu_rect.h;
}

static void filter_set(const char* name) {
	if(!name || !*name) {
		filter_active_name[0] = 0;
		return;
	}
	strncpy(filter_active_name, name, sizeof(filter_active_name) - 1);
	filter_active_name[sizeof(filter_active_name) - 1] = 0;
	needs_scroll_to_bottom = 1;
}

static void filter_clear(void) {
	filter_active_name[0] = 0;
	filter_hide_server = 0;
	needs_scroll_to_bottom = 1;
}

static int filter_active(void) {
	return filter_active_name[0] != 0;
}

/* Anchor at the first non-placeholder line, extend to the last. Used by
   Ctrl+A and the "Select all" right-click menu. */
static void select_all_lines(void) {
	int first = -1, last = -1;
	for(int i = 0; i < line_count; i++) {
		if(!lines[i].is_placeholder) {
			if(first < 0) first = i;
			last = i;
		}
	}
	if(first < 0) {
		sel_anchor_line = sel_active_line = -1;
		return;
	}
	sel_anchor_line = first;
	sel_anchor_char = 0;
	sel_active_line = last;
	sel_active_char = lines[last].plain_len;
	lmb_dragging = 0;
	pending_anchor = 0;
}

static void ctxmenu_commit(void) {
	if(ctxmenu_kind == CTXMENU_KIND_COPY) {
		copy_selection_to_clipboard();
	} else if(ctxmenu_kind == CTXMENU_KIND_FILTER) {
		filter_set(ctxmenu_payload);
	} else if(ctxmenu_kind == CTXMENU_KIND_FILTER_CLEAR) {
		filter_clear();
	} else if(ctxmenu_kind == CTXMENU_KIND_SELECT_ALL) {
		select_all_lines();
	}
}

static void ctxmenu_render(mu_Context* ctx) {
	if(!ctxmenu_visible) return;

	int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
	int hover = ctxmenu_hit(mx, my);

	mu_Color bg     = mu_color(40, 40, 40, 240);
	mu_Color border = mu_color(120, 120, 120, 255);
	mu_Color hov    = mu_color(80, 130, 200, 220);
	mu_Color fg     = mu_color(230, 230, 230, 255);

	mu_draw_rect(ctx, ctxmenu_rect, bg);
	mu_draw_box(ctx, ctxmenu_rect, border);
	if(hover)
		mu_draw_rect(ctx, mu_rect(ctxmenu_rect.x + 1, ctxmenu_rect.y + 1,
								  ctxmenu_rect.w - 2, ctxmenu_rect.h - 2), hov);
	int th = ctx->text_height(ctx->style->font);
	mu_draw_text(ctx, ctx->style->font, ctxmenu_label, -1,
				 mu_vec2(ctxmenu_rect.x + 8, ctxmenu_rect.y + (ctxmenu_rect.h - th) / 2), fg);
}

static int linkmodal_hit(mu_Rect r, int mx, int my) {
	return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

static const char* LINKMODAL_TITLE = "Open external link?";
static const char* LINKMODAL_WARN1 = "This may be unsafe.";
static const char* LINKMODAL_WARN2 = "Make sure you trust the destination.";
static char linkmodal_preview[160];

/* All metrics are derived from the font so the dialog scales with
   hud_ui_scale() and wide fonts can't overflow the box or the buttons. */
static void linkmodal_layout(mu_Context* ctx) {
	int th  = ctx->text_height(ctx->style->font);
	int pad = th;
	int gap = th / 2;

	int btn_h   = th + gap;
	int btn_w_v = ctx->text_width(ctx->style->font, "Visit website", 0) + 2 * gap;
	int btn_w_c = ctx->text_width(ctx->style->font, "Cancel", 0) + 2 * gap;

	int w = ctx->text_width(ctx->style->font, LINKMODAL_TITLE, 0);
	int t = ctx->text_width(ctx->style->font, LINKMODAL_WARN1, 0);
	if(t > w) w = t;
	t = ctx->text_width(ctx->style->font, LINKMODAL_WARN2, 0);
	if(t > w) w = t;
	if(btn_w_v + gap + btn_w_c > w) w = btn_w_v + gap + btn_w_c;
	w += 2 * pad;
	int max_w = settings.window_width * 92 / 100;
	if(w > max_w) w = max_w;

	/* URL preview: copy as much as fits the inner width, else truncate
	   with a trailing "..." */
	int inner = w - 2 * pad;
	int n = 0, full = strlen(linkmodal_url);
	while(n < full && n < (int)sizeof(linkmodal_preview) - 4) {
		linkmodal_preview[n] = linkmodal_url[n];
		linkmodal_preview[n + 1] = 0;
		if(ctx->text_width(ctx->style->font, linkmodal_preview, 0) > inner)
			break;
		n++;
	}
	linkmodal_preview[n] = 0;
	if(n < full) {
		while(n > 0
			  && ctx->text_width(ctx->style->font, linkmodal_preview, 0)
				  + ctx->text_width(ctx->style->font, "...", 0) > inner) {
			linkmodal_preview[--n] = 0;
		}
		strcat(linkmodal_preview, "...");
	}

	/* title + 2 warning lines + url line + button row, gap-separated */
	int h = pad + th + gap + th + th + gap + th + pad + btn_h + pad;

	int x = settings.window_width / 2 - w / 2;
	int y = settings.window_height / 2 - h / 2;
	linkmodal_rect = mu_rect(x, y, w, h);

	int by = y + h - btn_h - pad;
	int bx_visit  = x + w / 2 - (btn_w_v + gap + btn_w_c) / 2;
	int bx_cancel = bx_visit + btn_w_v + gap;
	linkmodal_visit_btn  = mu_rect(bx_visit,  by, btn_w_v, btn_h);
	linkmodal_cancel_btn = mu_rect(bx_cancel, by, btn_w_c, btn_h);
}

static void linkmodal_render(mu_Context* ctx) {
	if(!linkmodal_visible) return;
	linkmodal_layout(ctx);

	int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
	int th  = ctx->text_height(ctx->style->font);
	int pad = th;
	int gap = th / 2;

	mu_draw_rect(ctx, mu_rect(0, 0, settings.window_width, settings.window_height),
				 mu_color(0, 0, 0, 140));

	mu_draw_rect(ctx, linkmodal_rect, mu_color(28, 28, 28, 250));
	mu_draw_box(ctx, linkmodal_rect, mu_color(120, 120, 120, 255));

	mu_Color fg   = mu_color(230, 230, 230, 255);
	mu_Color warn = mu_color(220, 190, 80, 255);

	int ty = linkmodal_rect.y + pad;
	mu_draw_text(ctx, ctx->style->font, LINKMODAL_TITLE, -1,
				 mu_vec2(linkmodal_rect.x + pad, ty), fg);
	ty += th + gap;
	mu_draw_text(ctx, ctx->style->font, LINKMODAL_WARN1, -1,
				 mu_vec2(linkmodal_rect.x + pad, ty), warn);
	ty += th;
	mu_draw_text(ctx, ctx->style->font, LINKMODAL_WARN2, -1,
				 mu_vec2(linkmodal_rect.x + pad, ty), warn);
	ty += th + gap;
	mu_draw_text(ctx, ctx->style->font, linkmodal_preview, -1,
				 mu_vec2(linkmodal_rect.x + pad, ty), fg);

	int hov_visit  = linkmodal_hit(linkmodal_visit_btn,  mx, my);
	int hov_cancel = linkmodal_hit(linkmodal_cancel_btn, mx, my);

	int ar = hud_accent_red(), ag = hud_accent_green(), ab = hud_accent_blue();
	mu_Color visit_bg     = mu_color(hov_visit ? ar : (ar * 7) / 10,
									 hov_visit ? ag : (ag * 7) / 10,
									 hov_visit ? ab : (ab * 7) / 10, 255);
	mu_Color visit_border = mu_color(ar, ag, ab, 255);
	mu_Color cancel_bg    = mu_color(hov_cancel ? 90 : 60, hov_cancel ? 90 : 60, hov_cancel ? 90 : 60, 255);
	mu_Color cancel_border= mu_color(120, 120, 120, 255);

	mu_draw_rect(ctx, linkmodal_visit_btn, visit_bg);
	mu_draw_box(ctx, linkmodal_visit_btn, visit_border);
	mu_draw_rect(ctx, linkmodal_cancel_btn, cancel_bg);
	mu_draw_box(ctx, linkmodal_cancel_btn, cancel_border);

	int tw_v = ctx->text_width(ctx->style->font, "Visit website", 0);
	int tw_c = ctx->text_width(ctx->style->font, "Cancel", 0);
	mu_draw_text(ctx, ctx->style->font, "Visit website", -1,
				 mu_vec2(linkmodal_visit_btn.x + (linkmodal_visit_btn.w - tw_v) / 2,
						 linkmodal_visit_btn.y + (linkmodal_visit_btn.h - th) / 2), fg);
	mu_draw_text(ctx, ctx->style->font, "Cancel", -1,
				 mu_vec2(linkmodal_cancel_btn.x + (linkmodal_cancel_btn.w - tw_c) / 2,
						 linkmodal_cancel_btn.y + (linkmodal_cancel_btn.h - th) / 2), fg);
}

static int name_at_position(int line_index, int char_offset) {
	if(line_index < 0 || line_index >= line_count) return 0;
	struct visible_line* vl = &lines[line_index];
	if(vl->is_placeholder || vl->name_len <= 0) return 0;
	return char_offset >= vl->name_start && char_offset < vl->name_start + vl->name_len;
}

static void update_hover_cursor(mu_Context* ctx) {
	int line, ch;
	resolve_mouse_target(ctx, &line, &ch);
	int want_hand = 0;
	if(!lmb_dragging && !linkmodal_visible) {
		want_hand = (url_at_position(line, ch) >= 0)
					|| name_at_position(line, ch);
	}
	if(want_hand != hand_cursor_active) {
		window_cursor_hand(want_hand);
		hand_cursor_active = want_hand;
	}
}

/* Lift dark team colors toward a minimum perceived brightness so a black
   or pure-blue team name doesn't disappear against the panel background.

   This is a two-phase lift:

     Phase 1 - Saturate to ceiling.
       Scale all channels so the brightest one hits 255. This preserves
       hue exactly and brightens "diluted" hues like dark navy (0,0,80)
       into pure blue (0,0,255) at zero hue cost. Pure black has no hue
       to preserve and falls back to a flat readable grey.

     Phase 2 - Mix toward white if still too dark.
       Some saturated hues are perceptually dark even at maximum
       saturation: pure blue's luminance is ~18/255, pure red ~54/255.
       For these, no hue-preserving trick can make them readable on a
       dark panel - we must add R+G (or G+B etc) channels. A hue-cost
       lerp toward white covers exactly the residual brightness deficit
       and stops there. Pure green (~71/255) needs only a small touch.

   TARGET is chosen low enough that lifted colors stay clearly tied to
   their team identity - pure blue ends up around (200,200,255), which
   reads as light blue, not white. Adjust TARGET upward if it's still
   hard to read on your panel background, downward if the lift looks too
   pastel for your taste. */
static mu_Color readable_color(int r, int g, int b) {
	int y = (2126 * r + 7152 * g + 722 * b) / 10000;
	const int TARGET = 85;
	if(y >= TARGET) return mu_color(r, g, b, 255);

	/* Phase 1: hue-preserving saturation to ceiling. */
	int mx = r; if(g > mx) mx = g; if(b > mx) mx = b;
	if(mx <= 0) return mu_color(200, 200, 200, 255); /* pure black */
	int sr = (r * 255) / mx;
	int sg = (g * 255) / mx;
	int sb = (b * 255) / mx;
	int sy = (2126 * sr + 7152 * sg + 722 * sb) / 10000;

	/* Phase 2: only if the saturated ceiling is itself too dark
	   (typical for pure blue, deep red), lerp toward white. */
	if(sy < TARGET) {
		int f = ((TARGET - sy) * 256) / TARGET;
		if(f > 256) f = 256;
		sr = (sr * (256 - f) + 255 * f) >> 8;
		sg = (sg * (256 - f) + 255 * f) >> 8;
		sb = (sb * (256 - f) + 255 * f) >> 8;
	}

	if(sr > 255) sr = 255; if(sg > 255) sg = 255; if(sb > 255) sb = 255;
	if(sr < 0) sr = 0;     if(sg < 0) sg = 0;     if(sb < 0) sb = 0;
	return mu_color(sr, sg, sb, 255);
}

static mu_Color color_for_inline_code(unsigned char code) {
	switch(code) {
		case 1: return readable_color(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue);
		case 2: return readable_color(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue);
		case 3: return mu_color(255, 255, 255, 255);
		case 4: return mu_color(255,   0,   0, 255);
		case 5: return mu_color(  0, 255,   0, 255);
		case 6: return mu_color(255, 255, 255, 255);
		case 7: return mu_color(120, 120, 120, 255);
	}
	return mu_color(255, 255, 255, 255);
}

static mu_Color color_from_packed(unsigned int packed) {
	if(packed == 0) return mu_color(230, 230, 230, 255);
	/* Apply the same readability lift as inline color codes. The [Global]
	   prefix and any other text rendered before the first \x01/\x02 code
	   uses this color directly; without the lift it stayed pitch-dark
	   for blue teams while the rest of the line read fine. */
	return readable_color(red(packed), green(packed), blue(packed));
}

/* chat[0][1] is newest, chat[0][127] is never written by chat_add's shift.
   Walk down from the highest filled index so output is oldest -> newest. */
/* Detect a player name span at the start of a chat line. Player chat from
   network.c arrives wrapped in team color codes: "\x01Name\x06: text" or
   "\x01Name\x06 joined the \x01Team\x06 team". Server messages have no
   leading team color code, so they fall through. Returns the byte length
   of the name in plain text (which equals its length in raw because team
   color codes only delimit). */
static int extract_player_name(const char* raw, char* out, int out_max, int* plain_start) {
	if(plain_start) *plain_start = 0;
	if(!raw) return 0;
	int i = 0;
	int prefix_plain = 0;
	if(strncmp(raw, "[Global] ", 9) == 0) { i = 9; prefix_plain = 9; }
	unsigned char first = (unsigned char)raw[i];
	if(first != 0x01 && first != 0x02 && first != 0x03) return 0;
	int n = 0;
	for(int j = i + 1; raw[j]; j++) {
		unsigned char c = (unsigned char)raw[j];
		if(c == 0x06) {
			out[n] = 0;
			if(plain_start) *plain_start = prefix_plain;
			return n;
		}
		if(c >= 1 && c <= 7) return 0;
		if(n >= out_max - 1) return 0;
		out[n++] = (char)c;
	}
	return 0;
}

/* Day index (days since the unix epoch, in local time) for grouping. */
static int day_index(time_t t) {
	struct tm* tm = localtime(&t);
	if(!tm) return 0;
	struct tm copy = *tm;
	copy.tm_hour = 0;
	copy.tm_min  = 0;
	copy.tm_sec  = 0;
	copy.tm_isdst = -1;
	time_t midnight = mktime(&copy);
	return (int)(midnight / 86400);
}

static void format_day_label(time_t when, int today_day, char* out, size_t out_size) {
	int dd = day_index(when);
	int diff = today_day - dd;
	struct tm tm = *localtime(&when);
	char date[32];
	strftime(date, sizeof(date), "%b %d, %Y", &tm);
	if(diff <= 0)        snprintf(out, out_size, "Today \u2022 %s", date);
	else if(diff == 1)   snprintf(out, out_size, "Yesterday \u2022 %s", date);
	else if(diff < 7)    snprintf(out, out_size, "This week \u2022 %s", date);
	else if(diff < 30)   snprintf(out, out_size, "This month \u2022 %s", date);
	else                 snprintf(out, out_size, "%s", date);
}

static void emit_hidden_placeholder(int* hidden_run) {
	if(*hidden_run > 0 && line_count < CHATLOG_MAX_LINES) {
		struct visible_line* ph = &lines[line_count++];
		memset(ph, 0, sizeof(*ph));
		ph->is_placeholder = 1;
		ph->placeholder_count = *hidden_run;
		snprintf(ph->plain, sizeof(ph->plain),
				 *hidden_run == 1 ? "%d message hidden" : "%d messages hidden",
				 *hidden_run);
		ph->plain_len = (int)strlen(ph->plain);
		*hidden_run = 0;
	}
}

static void emit_day_separator(time_t when, int today_day) {
	if(line_count >= CHATLOG_MAX_LINES) return;
	struct visible_line* ph = &lines[line_count++];
	memset(ph, 0, sizeof(*ph));
	ph->is_placeholder = 1;
	ph->placeholder_count = 0;
	format_day_label(when, today_day, ph->plain, sizeof(ph->plain));
	ph->plain_len = (int)strlen(ph->plain);
}

/* Marks a (re)connection-session boundary. is_current = 1 labels the
   live ring's session ("Current session") so the user can tell at a
   glance which lines belong to the current match vs ones replayed from
   older log entries; is_current = 0 stamps each older session with its
   start time. */
static void emit_session_separator(time_t when, int is_current) {
	if(line_count >= CHATLOG_MAX_LINES) return;
	struct visible_line* ph = &lines[line_count++];
	memset(ph, 0, sizeof(*ph));
	ph->is_placeholder = 1;
	ph->placeholder_count = 0;
	if(is_current) {
		snprintf(ph->plain, sizeof(ph->plain), "Current session");
	} else {
		struct tm tm = *localtime(&when);
		char buf[16];
		strftime(buf, sizeof(buf), "%H:%M", &tm);
		snprintf(ph->plain, sizeof(ph->plain), "Session \u2022 %s", buf);
	}
	ph->plain_len = (int)strlen(ph->plain);
}

/* Emit a single visible row spanning [raw + start, raw + start + len).
   `is_continuation` is set on segments after the first when a chat line
   contains '\n' - the player-name extraction is skipped on those rows. */
static void emit_chat_segment(const char* raw, int start, int len,
							  unsigned int color, int is_continuation) {
	if(line_count >= CHATLOG_MAX_LINES) return;
	struct visible_line* vl = &lines[line_count++];
	memset(vl, 0, sizeof(*vl));
	vl->raw = raw + start;
	vl->raw_len = len;
	vl->is_continuation = is_continuation;
	vl->plain_len = strip_color_codes_n(vl->raw, vl->raw_len, vl->plain);
	if(!is_continuation) {
		char name[64];
		int name_start = 0;
		int name_len = extract_player_name(vl->raw, name, sizeof(name), &name_start);
		vl->name_start = name_start;
		vl->name_len = name_len;
		/* Server-origin lines arrive tagged with hud_accent_color (your team
		   color), which paints neutral announcements like "Blue captured the
		   intel" in unreadable navy. Force light grey for lines without a
		   leading name tag; chat from real players keeps its team color. */
		vl->color = (name_len == 0) ? 0xC8C8C8 : color;
	} else {
		vl->color = color;
	}
	scan_urls(vl);
}

/* Split a raw chat string on '\n' and emit one visible row per segment.
   Single-line messages take the fast path with raw_len = -1. */
static void emit_chat_line(const char* raw, unsigned int color) {
	if(!raw) return;
	int start = 0, seg = 0;
	const int MAX_SEGMENTS = 16;
	for(int i = 0; raw[i] && seg < MAX_SEGMENTS - 1; i++) {
		if(raw[i] == '\n') {
			emit_chat_segment(raw, start, i - start, color, seg > 0);
			start = i + 1;
			seg++;
			if(line_count >= CHATLOG_MAX_LINES) return;
		}
	}
	if(seg == 0) {
		emit_chat_segment(raw, 0, -1, color, 0);
	} else {
		emit_chat_segment(raw, start, (int)strlen(raw + start), color, seg > 0);
	}
}

static void build_visible_lines(void) {
	line_count = 0;

	int today_day = day_index(time(NULL));
	int last_day  = -1;
	int hidden_run = 0;

	/* History: oldest -> newest, with day separators on transitions. */
	int hc = chathistory_count();
	for(int i = 0; i < hc && line_count < CHATLOG_MAX_LINES; i++) {
		const struct chathistory_line* h = chathistory_get(i);
		if(!h) break;

		char name[64];
		int name_start = 0;
		int name_len = extract_player_name(h->raw, name, sizeof(name), &name_start);
		if(filter_active()) {
			if(name_len > 0 && strcmp(name, filter_active_name) != 0) { hidden_run++; continue; }
			if(name_len == 0 && filter_hide_server) { hidden_run++; continue; }
		}

		emit_hidden_placeholder(&hidden_run);

		int dd = day_index(h->when);
		if(dd != last_day) {
			emit_day_separator(h->when, today_day);
			last_day = dd;
		}
		/* Each replayed (re)connection gets its own session marker.
		   When the session also crosses a day boundary the day
		   separator above renders first; both are kept because they
		   answer different questions ("which day" vs "which join"). */
		if(h->is_session_start)
			emit_session_separator(h->when, 0);

		/* Historical lines render in a slightly muted default color; inline
		   color codes will repaint name/team segments as usual. */
		emit_chat_line(h->raw, 0xAAAAAA);
	}

	/* Live session: iterate session_log (not the 128-slot live ring,
	   which would silently drop old messages on overflow). */
	if(session_log_count > 0) {
		if(last_day >= 0 && last_day != today_day) {
			emit_hidden_placeholder(&hidden_run);
			emit_day_separator(time(NULL), today_day);
		}

		if(line_count > 0)
			emit_session_separator(time(NULL), 1);

		for(int k = 0; k < session_log_count && line_count < CHATLOG_MAX_LINES; k++) {
			const char* raw = session_log_raw[k];
			char name[64];
			int name_start = 0;
			int name_len = extract_player_name(raw, name, sizeof(name), &name_start);

			if(filter_active()) {
				if(name_len > 0 && strcmp(name, filter_active_name) != 0) { hidden_run++; continue; }
				if(name_len == 0 && filter_hide_server) { hidden_run++; continue; }
			}
			emit_hidden_placeholder(&hidden_run);
			emit_chat_line(raw, session_log_color[k]);
		}
	}

	emit_hidden_placeholder(&hidden_run);
}

static void resolve_mouse_target(mu_Context* ctx, int* out_line, int* out_char) {
	int mx = ctx->mouse_pos.x;
	int my = ctx->mouse_pos.y;

	if(line_count <= 0) {
		*out_line = -1;
		*out_char = 0;
		return;
	}

	if(my < lines[0].rect.y) {
		*out_line = 0;
		*out_char = 0;
		return;
	}

	mu_Rect last = lines[line_count - 1].rect;
	if(my >= last.y + last.h) {
		*out_line = line_count - 1;
		*out_char = lines[line_count - 1].plain_len;
		return;
	}

	/* Walk top-down; the largest i with rect.y <= my wins. This snaps
	   clicks that land in the row spacing or in horizontal margins to the
	   nearest line, matching how every text widget on the planet behaves. */
	int best = 0;
	for(int i = 0; i < line_count; i++) {
		if(lines[i].rect.y <= my) best = i;
		else break;
	}
	mu_Rect r = lines[best].rect;
	float rel_x = (float)(mx - r.x);
	if(rel_x < 0.f) { *out_line = best; *out_char = 0; return; }
	*out_line = best;
	*out_char = plain_char_at_offset(lines[best].plain, lines[best].plain_len, rel_x);
}

/* ---- Find-in-chat helpers ------------------------------------- */

/* ASCII-only fold to lowercase. Chat content is overwhelmingly ASCII
   in practice, and a UTF-8-aware case fold would balloon the search
   path to no real benefit (you'd have to ICU-link the whole game).
   Bytes 0x80+ are passed through unchanged - they'll match
   byte-exactly across queries, which is the right behavior for the
   small amount of non-ASCII that does show up (player names, emoji). */
static int search_ascii_tolower(int c) {
	return (c >= 'A' && c <= 'Z') ? c + ('a' - 'A') : c;
}

/* Find first occurrence of `needle` (length needle_len) in `hay`
   (length hay_len) starting at byte offset `from`, ASCII-case-
   insensitive. Returns the start offset, or -1 if not found.

   Plain O(n*m) - chat lines max out at 256 bytes and queries at ~96,
   so the asymptotic win from KMP/Boyer-Moore would be drowned by the
   setup cost. */
static int search_find_ci(const char* hay, int hay_len,
						  const char* needle, int needle_len, int from) {
	if(needle_len <= 0 || hay_len < needle_len) return -1;
	if(from < 0) from = 0;
	int last = hay_len - needle_len;
	for(int i = from; i <= last; i++) {
		int ok = 1;
		for(int j = 0; j < needle_len; j++) {
			if(search_ascii_tolower((unsigned char)hay[i + j])
			!= search_ascii_tolower((unsigned char)needle[j])) {
				ok = 0;
				break;
			}
		}
		if(ok) return i;
	}
	return -1;
}

/* Rebuild the match array from the current `lines[]` snapshot. Cheap
   enough to call every render frame - line_count is bounded and most
   lines yield 0 matches against a typical query.

   Also fills matches_line_start[] so render_chat_line can dispatch
   per-line hits in O(matches_for_this_line) instead of O(total).
   Placeholder lines (day separators, "N hidden" notes) are skipped:
   they don't carry user-authored text and matching them would
   highlight UI chrome. */
static void search_recompute_matches(void) {
	search_match_count = 0;
	if(!search_visible || search_query_len == 0) {
		/* Empty query: zero out the per-line index up through the
		   current line count so the renderer's lookup is consistent. */
		for(int i = 0; i <= line_count && i <= CHATLOG_MAX_LINES; i++)
			matches_line_start[i] = 0;
		search_current = 0;
		return;
	}

	int last_line = -1;
	for(int i = 0; i < line_count
		  && search_match_count < CHATLOG_SEARCH_MAX_MATCHES; i++) {
		/* Lines from last_line+1..i with no matches still need their
		   start index recorded so the renderer's [start[i], start[i+1])
		   range is well-defined. */
		while(last_line < i) {
			last_line++;
			if(last_line <= CHATLOG_MAX_LINES)
				matches_line_start[last_line] = search_match_count;
		}
		struct visible_line* vl = &lines[i];
		if(vl->is_placeholder) continue;

		int start = 0;
		while(start <= vl->plain_len - search_query_len
			  && search_match_count < CHATLOG_SEARCH_MAX_MATCHES) {
			int hit = search_find_ci(vl->plain, vl->plain_len,
									 search_query, search_query_len, start);
			if(hit < 0) break;
			search_matches[search_match_count].line = i;
			search_matches[search_match_count].char_start = hit;
			search_matches[search_match_count].char_end   = hit + search_query_len;
			search_match_count++;
			/* Advance past this match. We deliberately don't allow
			   overlapping hits: searching "aa" in "aaaa" yields 2 hits
			   ("aa", "aa"), not 3. Notepad behaves the same way. */
			start = hit + search_query_len;
		}
	}
	/* Trailing sentinel covers the rest of the index up through the
	   one-past-end slot used by the renderer. */
	while(last_line < line_count && last_line + 1 <= CHATLOG_MAX_LINES) {
		last_line++;
		matches_line_start[last_line] = search_match_count;
	}

	/* Clamp current after a rebuild - new lines may have shifted the
	   match list around. The "stay on the same logical match" problem
	   doesn't have a clean answer without per-match identity tracking;
	   a clamp is the least-surprising fallback. */
	if(search_match_count == 0) {
		search_current = 0;
	} else {
		if(search_current < 0) search_current = 0;
		if(search_current >= search_match_count)
			search_current = search_match_count - 1;
	}
}

/* Apply any pending nav request now that recompute_matches has run.
   Sets search_pending_scroll so the post-panel scroll override can
   bring the new current match into view. */
static void search_apply_nav_request(void) {
	if(search_nav_request == SEARCH_NAV_NONE) return;
	if(search_match_count <= 0) {
		search_nav_request = SEARCH_NAV_NONE;
		return;
	}
	int n = search_match_count;
	switch(search_nav_request) {
		case SEARCH_NAV_LAST:
			/* Newest message first: chat is bottom-anchored and the
			   typical search ("did Bob just say X") wants the most
			   recent occurrence, not the oldest. */
			search_current = n - 1;
			break;
		case SEARCH_NAV_NEXT:
			search_current = (search_current + 1) % n;
			break;
		case SEARCH_NAV_PREV:
			/* Force a positive remainder - C's % preserves sign for
			   negative dividends. */
			search_current = ((search_current - 1) % n + n) % n;
			break;
		default: break;
	}
	search_pending_scroll = search_current;
	search_nav_request = SEARCH_NAV_NONE;
}

static void search_open(void) {
	if(search_visible) {
		/* Already open: clear the query so the user can start a fresh
		   search by typing immediately. Matches Notepad's "Ctrl+F
		   focuses and selects the box" behavior closely enough for
		   our purposes - we don't have selection in the box. */
		search_query[0] = 0;
		search_query_len = 0;
		search_match_count = 0;
		search_current = 0;
		search_nav_request = SEARCH_NAV_NONE;
		search_pending_scroll = -1;
		return;
	}
	search_visible = 1;
	search_query[0] = 0;
	search_query_len = 0;
	search_match_count = 0;
	search_current = 0;
	search_nav_request = SEARCH_NAV_NONE;
	search_pending_scroll = -1;
	/* Clearing this avoids the bar opening at the bottom of the log
	   then immediately scrolling further - the very next render would
	   see "user is at bottom + a frame just changed" and re-snap. */
	needs_scroll_to_bottom = 0;
}

static void search_close(void) {
	search_visible = 0;
	search_query[0] = 0;
	search_query_len = 0;
	search_match_count = 0;
	search_current = 0;
	search_nav_request = SEARCH_NAV_NONE;
	search_pending_scroll = -1;
	search_bar_rect  = mu_rect(0, 0, 0, 0);
	search_btn_prev  = mu_rect(0, 0, 0, 0);
	search_btn_next  = mu_rect(0, 0, 0, 0);
	search_btn_close = mu_rect(0, 0, 0, 0);
}

static void search_backspace(void) {
	if(!search_visible || search_query_len <= 0) return;
	/* Walk back over UTF-8 continuation bytes (0x80..0xBF) to land on
	   the start of the previous codepoint. Mirrors the chat input's
	   backspace behavior. */
	int i = search_query_len - 1;
	while(i > 0 && ((unsigned char)search_query[i] & 0xC0) == 0x80) i--;
	search_query_len = i;
	search_query[search_query_len] = 0;
	/* On further query edits, jump to the newest hit rather than
	   sticking to whatever index we were on - the user is steering
	   the query, not navigating yet. */
	search_nav_request = SEARCH_NAV_LAST;
}

int chatlog_search_active(void) {
	return search_visible;
}

void chatlog_search_text_input(const char* utf8) {
	if(!search_visible || !utf8 || !*utf8) return;
	int n = (int)strlen(utf8);
	/* Reject control bytes - the inline color-code range (1..7) and
	   anything below 0x20. Plain text strips these out at line build
	   time so they could never match the chat anyway, and accepting
	   them would let a stray escape sequence sneak into the query
	   and corrupt the rendered bar. */
	for(int i = 0; i < n; i++) {
		unsigned char c = (unsigned char)utf8[i];
		if(c < 0x20) return;
	}
	int cap = (int)sizeof(search_query) - 1;
	if(search_query_len + n > cap) n = cap - search_query_len;
	if(n <= 0) return;
	memcpy(search_query + search_query_len, utf8, n);
	search_query_len += n;
	search_query[search_query_len] = 0;
	/* See search_backspace: typing also resets focus to the most
	   recent hit. */
	search_nav_request = SEARCH_NAV_LAST;
}

/* Pull text from the system clipboard into the search query. Hooked
   up by the keyboard handler on Ctrl/Cmd+V while the bar has focus. */
static void search_paste_clipboard(void) {
	const char* clip = window_clipboard();
	if(!clip || !*clip) return;
	chatlog_search_text_input(clip);
}

/* Pixel layout for the search bar buttons. Called both from the
   renderer and from the click handler so the rects stay in sync.
   `bar` is the full-width row reserved for the bar in the parent
   window; layout is right-aligned: [×] [↓] [↑] [counter] | query. */
static void search_layout_bar(mu_Rect bar) {
	int btn_h = bar.h - 8;
	int btn_w = btn_h;       /* square icon-style buttons */
	int gap   = 4;
	int right = bar.x + bar.w - 6;

	search_btn_close = mu_rect(right - btn_w, bar.y + 4, btn_w, btn_h);
	right = search_btn_close.x - gap;

	search_btn_next  = mu_rect(right - btn_w, bar.y + 4, btn_w, btn_h);
	right = search_btn_next.x - gap;

	search_btn_prev  = mu_rect(right - btn_w, bar.y + 4, btn_w, btn_h);
}

/* Draw the bar. Caller has already laid out the row via
   mu_layout_row + mu_layout_next, so `bar` is in screen coords. */
static void search_render_bar(mu_Context* ctx, mu_Rect bar) {
	search_bar_rect = bar;
	search_layout_bar(bar);

	int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
	int hov_prev  = mx >= search_btn_prev.x  && mx < search_btn_prev.x  + search_btn_prev.w
				 && my >= search_btn_prev.y  && my < search_btn_prev.y  + search_btn_prev.h;
	int hov_next  = mx >= search_btn_next.x  && mx < search_btn_next.x  + search_btn_next.w
				 && my >= search_btn_next.y  && my < search_btn_next.y  + search_btn_next.h;
	int hov_close = mx >= search_btn_close.x && mx < search_btn_close.x + search_btn_close.w
				 && my >= search_btn_close.y && my < search_btn_close.y + search_btn_close.h;

	/* Bar background - a touch lighter than the filter banner so the
	   two stack visibly when both are active. */
	mu_draw_rect(ctx, bar, mu_color(50, 55, 70, 220));
	mu_draw_box(ctx, bar,  mu_color(120, 130, 160, 255));

	mu_Color fg     = mu_color(230, 230, 230, 255);
	mu_Color dim    = mu_color(160, 160, 170, 255);
	mu_Color label  = mu_color(180, 200, 230, 255);

	int th = ctx->text_height(ctx->style->font);
	int text_y = bar.y + (bar.h - th) / 2;

	/* "Find:" label, query text, then a thin caret to indicate focus.
	   We don't blink it - blinking requires owning a clock here, and
	   the bar is short-lived enough that a static caret reads fine. */
	int x = bar.x + 8;
	const char* prefix = "Find:";
	int pw = ctx->text_width(ctx->style->font, prefix, 0);
	mu_draw_text(ctx, ctx->style->font, prefix, -1,
				 mu_vec2(x, text_y), label);
	x += pw + 8;

	/* Reserve room on the right for the buttons + counter so the query
	   text gets clipped instead of overlapping. */
	int right_reserved = (bar.x + bar.w) - search_btn_prev.x + 8;
	int counter_reserved = 70;     /* "9999/9999" worst case fits */
	int query_max_x = bar.x + bar.w - right_reserved - counter_reserved;
	if(query_max_x < x + 40) query_max_x = x + 40; /* sanity floor */

	if(search_query_len > 0) {
		/* Color the typed query red when there are no matches - same
		   convention as Firefox/Chrome's find bar. */
		mu_Color qc = (search_match_count > 0) ? fg : mu_color(240, 120, 120, 255);
		/* Draw inside a clip rect so an over-long query doesn't bleed
		   into the buttons. */
		mu_push_clip_rect(ctx, mu_rect(x, bar.y, query_max_x - x, bar.h));
		mu_draw_text(ctx, ctx->style->font, search_query, search_query_len,
					 mu_vec2(x, text_y), qc);
		mu_pop_clip_rect(ctx);
		float qw = font_length((float)chatlog_text_height(), search_query);
		int caret_x = x + (int)qw + 1;
		if(caret_x > query_max_x - 2) caret_x = query_max_x - 2;
		mu_draw_rect(ctx, mu_rect(caret_x, bar.y + 5, 1, bar.h - 10), fg);
	} else {
		/* Empty-state hint, dimmed. The hint and caret share a slot. */
		const char* hint = "type to search messages";
		mu_push_clip_rect(ctx, mu_rect(x, bar.y, query_max_x - x, bar.h));
		mu_draw_text(ctx, ctx->style->font, hint, -1,
					 mu_vec2(x, text_y), dim);
		mu_pop_clip_rect(ctx);
		mu_draw_rect(ctx, mu_rect(x, bar.y + 5, 1, bar.h - 10), fg);
	}

	/* Match counter, right-aligned just before the prev button. */
	char counter[32];
	if(search_query_len == 0) counter[0] = 0;
	else if(search_match_count == 0) snprintf(counter, sizeof(counter), "no results");
	else snprintf(counter, sizeof(counter), "%d / %d",
				  search_current + 1, search_match_count);
	if(counter[0]) {
		int cw = ctx->text_width(ctx->style->font, counter, 0);
		mu_draw_text(ctx, ctx->style->font, counter, -1,
					 mu_vec2(search_btn_prev.x - cw - 8, text_y),
					 (search_match_count == 0 && search_query_len > 0)
						? mu_color(240, 120, 120, 255)
						: fg);
	}

	/* Buttons. Drawn as boxes with a small glyph - microui doesn't
	   give us proper icon assets to reuse for prev/next here, so we
	   render the arrow as a text glyph (Unicode-safe in this font). */
	int can_nav = (search_match_count > 0);
	mu_Color btn_bg_idle = mu_color(60, 65, 80, 255);
	mu_Color btn_bg_hov  = mu_color(90, 100, 130, 255);
	mu_Color btn_bg_off  = mu_color(45, 48, 58, 255);
	mu_Color btn_border  = mu_color(140, 150, 180, 255);

	mu_draw_rect(ctx, search_btn_prev,
				 !can_nav ? btn_bg_off : (hov_prev ? btn_bg_hov : btn_bg_idle));
	mu_draw_box(ctx, search_btn_prev, btn_border);
	mu_draw_rect(ctx, search_btn_next,
				 !can_nav ? btn_bg_off : (hov_next ? btn_bg_hov : btn_bg_idle));
	mu_draw_box(ctx, search_btn_next, btn_border);
	mu_draw_rect(ctx, search_btn_close,
				 hov_close ? mu_color(140, 70, 70, 255) : mu_color(80, 50, 50, 255));
	mu_draw_box(ctx, search_btn_close, mu_color(180, 120, 120, 255));

	const char* up_glyph    = "\xE2\x86\x91"; /* U+2191 */
	const char* down_glyph  = "\xE2\x86\x93"; /* U+2193 */
	const char* close_glyph = "\xC3\x97";     /* U+00D7 */
	mu_Color glyph_col = can_nav ? fg : dim;

	int gw, gh = th;
	gw = ctx->text_width(ctx->style->font, up_glyph, 0);
	mu_draw_text(ctx, ctx->style->font, up_glyph, -1,
				 mu_vec2(search_btn_prev.x + (search_btn_prev.w - gw) / 2,
						 search_btn_prev.y + (search_btn_prev.h - gh) / 2),
				 glyph_col);
	gw = ctx->text_width(ctx->style->font, down_glyph, 0);
	mu_draw_text(ctx, ctx->style->font, down_glyph, -1,
				 mu_vec2(search_btn_next.x + (search_btn_next.w - gw) / 2,
						 search_btn_next.y + (search_btn_next.h - gh) / 2),
				 glyph_col);
	gw = ctx->text_width(ctx->style->font, close_glyph, 0);
	mu_draw_text(ctx, ctx->style->font, close_glyph, -1,
				 mu_vec2(search_btn_close.x + (search_btn_close.w - gw) / 2,
						 search_btn_close.y + (search_btn_close.h - gh) / 2),
				 fg);
}

/* Hit-test the search bar widgets. Returns 1 if the click was handled
   (and the caller should stop further dispatch), 0 otherwise. */
static int search_handle_click(int mx, int my) {
	if(!search_visible) return 0;
	if(search_btn_prev.w > 0
	   && mx >= search_btn_prev.x && mx < search_btn_prev.x + search_btn_prev.w
	   && my >= search_btn_prev.y && my < search_btn_prev.y + search_btn_prev.h) {
		search_nav_request = SEARCH_NAV_PREV;
		return 1;
	}
	if(search_btn_next.w > 0
	   && mx >= search_btn_next.x && mx < search_btn_next.x + search_btn_next.w
	   && my >= search_btn_next.y && my < search_btn_next.y + search_btn_next.h) {
		search_nav_request = SEARCH_NAV_NEXT;
		return 1;
	}
	if(search_btn_close.w > 0
	   && mx >= search_btn_close.x && mx < search_btn_close.x + search_btn_close.w
	   && my >= search_btn_close.y && my < search_btn_close.y + search_btn_close.h) {
		search_close();
		return 1;
	}
	/* A click anywhere ELSE within the bar still shouldn't fall through
	   into the chat panel below (it'd start a text selection on a
	   chat line that happened to share the y range, which does not
	   happen here because the bar is laid out outside the panel, but
	   defending against future layout shuffles is cheap). */
	if(search_bar_rect.w > 0
	   && mx >= search_bar_rect.x && mx < search_bar_rect.x + search_bar_rect.w
	   && my >= search_bar_rect.y && my < search_bar_rect.y + search_bar_rect.h) {
		return 1;
	}
	return 0;
}

static void render_chat_line(mu_Context* ctx, int line_index) {
	struct visible_line* vl = &lines[line_index];

	mu_layout_row(ctx, 1, (int[]) { -1 }, chatlog_text_height());
	mu_Rect r = mu_layout_next(ctx);
	vl->rect = r;

	mu_Rect clip = mu_get_clip_rect(ctx);
	if(r.y + r.h < clip.y || r.y >= clip.y + clip.h) return;

	if(vl->is_placeholder) {
		/* Discord-style separator: thin line + dim centered text. */
		mu_Color line_col = mu_color(110, 110, 110, 180);
		mu_Color text_col = mu_color(150, 150, 150, 220);
		int mid_y = r.y + r.h / 2;
		int tw = ctx->text_width(ctx->style->font, vl->plain, vl->plain_len);
		int tx = r.x + (r.w - tw) / 2;
		int pad = 8;
		if(tx - pad > r.x)
			mu_draw_rect(ctx, mu_rect(r.x + 4, mid_y, tx - pad - (r.x + 4), 1), line_col);
		if(tx + tw + pad < r.x + r.w)
			mu_draw_rect(ctx, mu_rect(tx + tw + pad, mid_y,
									  (r.x + r.w) - (tx + tw + pad) - 4, 1), line_col);
		mu_draw_text(ctx, ctx->style->font, vl->plain, vl->plain_len,
					 mu_vec2(tx, r.y + (r.h - ctx->text_height(ctx->style->font)) / 2),
					 text_col);
		return;
	}

	/* Search highlight pass. Drawn first so the selection highlight
	   (drawn next, below) and the eventual text glyphs both stack on
	   top of it. We dispatch via matches_line_start so this is
	   O(matches_for_this_line), not O(total_matches).

	   Lines beyond CHATLOG_MAX_LINES would index out of bounds on
	   matches_line_start; defend explicitly even though line_count is
	   already bounded by the same constant. */
	if(search_visible && search_match_count > 0
	   && line_index >= 0 && line_index < CHATLOG_MAX_LINES) {
		int from_m = matches_line_start[line_index];
		int to_m   = (line_index + 1 <= CHATLOG_MAX_LINES)
					? matches_line_start[line_index + 1]
					: search_match_count;
		for(int m = from_m; m < to_m && m < search_match_count; m++) {
			int from_c = search_matches[m].char_start;
			int to_c   = search_matches[m].char_end;
			if(from_c < 0) from_c = 0;
			if(to_c > vl->plain_len) to_c = vl->plain_len;
			if(to_c <= from_c) continue;
			float x0 = plain_prefix_width(vl->plain, from_c);
			float x1 = plain_prefix_width(vl->plain, to_c);
			int rect_x = r.x + (int)x0;
			int rect_w = (int)(x1 - x0);
			if(rect_w <= 0) continue;
			/* Current match: brighter, more opaque so it stands out
			   even when adjacent matches sit nearby on the same line. */
			mu_Color hl = (m == search_current)
				? mu_color(255, 150,  30, 220)    /* current: orange */
				: mu_color(240, 220,  60, 130);   /* others:  yellow */
			mu_draw_rect(ctx, mu_rect(rect_x, r.y, rect_w, r.h), hl);
		}
	}

	if(has_selection()) {
		int s_line, s_char, e_line, e_char;
		selection_ordered(&s_line, &s_char, &e_line, &e_char);
		if(line_index >= s_line && line_index <= e_line) {
			int from = (line_index == s_line) ? s_char : 0;
			int to   = (line_index == e_line) ? e_char : vl->plain_len;
			if(from < 0) from = 0;
			if(to > vl->plain_len) to = vl->plain_len;
			mu_Color hl = mu_color(80, 130, 200, 140);
			float x0 = plain_prefix_width(vl->plain, from);
			float x1 = plain_prefix_width(vl->plain, to);
			int rect_x = r.x + (int)x0;
			int rect_w;
			/* Selection continues onto the next line: extend the highlight
			   to the right edge so the implicit newline reads as included
			   instead of orphaning a sliver at column 0 of the next line. */
			if(line_index < e_line)
				rect_w = (r.x + r.w) - rect_x;
			else
				rect_w = (int)(x1 - x0);
			if(rect_w > 0)
				mu_draw_rect(ctx, mu_rect(rect_x, r.y, rect_w, r.h), hl);
		}
	}

	const char* raw = vl->raw;
	if(!raw) return;
	mu_Color cur = color_from_packed(vl->color);
	char run[256];
	int run_len = 0;
	int run_start_offset = 0;     /* plain-text offset where current run began */
	int run_in_url = 0;           /* current run lies inside a URL range */
	int plain_offset = 0;         /* count of non-color-code bytes seen so far */

	for(int i = 0; ; i++) {
		/* Stop at the segment boundary (raw_len >= 0) or at '\0' / '\n'.
		   '\n' acts as a hard end-of-line so the second half of a
		   newline-split message renders on its own row instead of
		   bleeding through. */
		unsigned char c = (unsigned char)raw[i];
		int is_segment_end = (vl->raw_len >= 0 && i >= vl->raw_len);
		int is_code = (!is_segment_end) && (c >= 1 && c <= 7);
		int is_marker = (!is_segment_end) && (c == 0xFF);
		int is_end  = is_segment_end || (c == 0) || (c == '\n');
		int in_url  = (!is_code && !is_marker && !is_end)
					  ? (url_at_position(line_index, plain_offset) >= 0)
					  : run_in_url;
		int boundary = (run_len > 0) && (in_url != run_in_url);

		if(is_code || is_marker || is_end || boundary) {
			if(run_len > 0 && !run_in_url) {
				run[run_len] = 0;
				float x = plain_prefix_width(vl->plain, run_start_offset);
				mu_draw_text(ctx, ctx->style->font, run, run_len,
							 mu_vec2(r.x + (int)x, r.y), cur);
			}
			run_len = 0;
			if(is_end) break;
			if(is_code) { cur = color_for_inline_code(c); continue; }
			if(is_marker) continue;
		}

		if(run_len == 0) {
			run_start_offset = plain_offset;
			run_in_url = in_url;
		}
		if(run_len < (int)sizeof(run) - 1) run[run_len++] = (char)c;
		plain_offset++;
	}

	/* URL pass: draw each URL once in the accent color and add a thin
	   underline. The main pass above deliberately skipped these bytes so
	   we are not over-painting. */
	for(int u = 0; u < vl->url_count; u++) {
		int us = vl->urls[u].start;
		int ue = vl->urls[u].end;
		float x0 = plain_prefix_width(vl->plain, us);
		float x1 = plain_prefix_width(vl->plain, ue);
		mu_Color link = mu_color(hud_accent_red(), hud_accent_green(),
								 hud_accent_blue(), 255);

		char tmp[256];
		int n = ue - us;
		if(n > 255) n = 255;
		memcpy(tmp, vl->plain + us, n);
		tmp[n] = 0;
		mu_draw_text(ctx, ctx->style->font, tmp, n,
					 mu_vec2(r.x + (int)x0, r.y), link);
		/* Underline at the bottom row of the line cell. r.h - 1 keeps it
		   strictly below the descender area so periods at the baseline
		   no longer visually fuse with it into a fake underscore. */
		mu_draw_rect(ctx, mu_rect(r.x + (int)x0, r.y + r.h - 1,
								  (int)(x1 - x0), 1), link);
	}
}

static void hud_chatlog_init(void) {
	sel_anchor_line = sel_active_line = -1;
	sel_anchor_char = sel_active_char = 0;
	lmb_dragging = 0;
	pending_anchor = 0;
	needs_scroll_to_bottom = 1;
	prev_top_chat_idx = -1;
	prev_top_chat_len = 0;
	prev_session_count = 0;
	/* Reset visible-line state too. Without these, re-entering the
	   chatlog with leftover line_count and prev_history_count from a
	   previous session can mis-classify the first frame's growth as a
	   prepend and shift the scroll incorrectly. */
	line_count = 0;
	prev_history_count = 0;
	content_body_rect = mu_rect(0, 0, 0, 0);
	ctxmenu_visible = 0;
	ctxmenu_kind = CTXMENU_KIND_COPY;
	ctxmenu_payload[0] = 0;
	linkmodal_visible = 0;
	linkmodal_url[0] = 0;
	pending_url_line = pending_url_idx = -1;
	filter_active_name[0] = 0;
	filter_hide_server = 0;
	filter_clear_btn = mu_rect(0, 0, 0, 0);
	filter_server_btn = mu_rect(0, 0, 0, 0);
	/* Search state. Closing on every (re-)entry into the HUD is
	   intentional - dropping back into the chatlog with a stale query
	   from a previous session would highlight matches against text
	   the user can no longer see in context. */
	search_close();
	chathistory_reset();
	if(hand_cursor_active) {
		window_cursor_hand(0);
		hand_cursor_active = 0;
	}
}

static struct texture* hud_chatlog_ui_images(int icon_id, bool* resize) {
	switch(icon_id) {
		case MU_ICON_CHECK:     return &texture_ui_box_check;
		case MU_ICON_EXPANDED:  return &texture_ui_expanded;
		case MU_ICON_COLLAPSED: return &texture_ui_collapsed;
		default: return NULL;
	}
}

static void hud_chatlog_render(mu_Context* ctx, float scalex, float scaley) {
	chathistory_poll();
	hud_common_render_for_chatlog(ctx);

	mu_Rect frame = mu_rect(0, 0, settings.window_width, settings.window_height);

	if(mu_begin_window_ex(ctx, "Main", frame, MU_OPT_NOFRAME | MU_OPT_NOTITLE | MU_OPT_NORESIZE)) {
		mu_Container* cnt = mu_get_current_container(ctx);
		cnt->rect = frame;

		hud_common_sidebar_for_chatlog(ctx, scalex, scaley);

		mu_begin_panel(ctx, "Content");

		/* Search bar lives above the filter banner so both can stack
		   visibly when the user is filtering AND searching at once -
		   a common combination ("show only Bob's messages, find 'gg'"). */
		if(search_visible) {
			mu_layout_row(ctx, 1, (int[]) { -1 }, CHATLOG_SEARCH_BAR_H);
			mu_Rect sbar = mu_layout_next(ctx);
			search_render_bar(ctx, sbar);
		} else {
			search_bar_rect  = mu_rect(0, 0, 0, 0);
			search_btn_prev  = mu_rect(0, 0, 0, 0);
			search_btn_next  = mu_rect(0, 0, 0, 0);
			search_btn_close = mu_rect(0, 0, 0, 0);
		}

		if(filter_active()) {
			mu_layout_row(ctx, 1, (int[]) { -1 }, 24);
			mu_Rect bar = mu_layout_next(ctx);
			mu_draw_rect(ctx, bar, mu_color(60, 60, 80, 200));
			mu_draw_box(ctx, bar, mu_color(120, 120, 160, 255));
			char label[128];
			snprintf(label, sizeof(label), "Filtering: %s", filter_active_name);
			mu_draw_text(ctx, ctx->style->font, label, -1,
						 mu_vec2(bar.x + 8, bar.y + 4),
						 mu_color(230, 230, 230, 255));
			int btn_w = 72, btn_h = bar.h - 6;
			/* Pick the wider of the two possible server-button labels so the
			   button width doesn't change when the toggle flips. */
			int sw_a = ctx->text_width(ctx->style->font, "Server: shown",  0);
			int sw_b = ctx->text_width(ctx->style->font, "Server: hidden", 0);
			int srv_w = (sw_a > sw_b ? sw_a : sw_b) + 18;
			filter_clear_btn  = mu_rect(bar.x + bar.w - btn_w - 4,                bar.y + 3, btn_w, btn_h);
			filter_server_btn = mu_rect(bar.x + bar.w - btn_w - srv_w - 12,       bar.y + 3, srv_w, btn_h);
			int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;

			int hov_c = mx >= filter_clear_btn.x  && mx < filter_clear_btn.x  + filter_clear_btn.w
					&& my >= filter_clear_btn.y  && my < filter_clear_btn.y  + filter_clear_btn.h;
			int hov_s = mx >= filter_server_btn.x && mx < filter_server_btn.x + filter_server_btn.w
					&& my >= filter_server_btn.y && my < filter_server_btn.y + filter_server_btn.h;

			mu_Color srv_bg = filter_hide_server
				? mu_color(hov_s ? 110 : 80, hov_s ? 80 : 60, hov_s ? 80 : 60, 255)
				: mu_color(hov_s ? 90 : 60,  hov_s ? 100 : 70, hov_s ? 110 : 80, 255);
			mu_draw_rect(ctx, filter_server_btn, srv_bg);
			mu_draw_box(ctx, filter_server_btn,  mu_color(140, 140, 160, 255));
			const char* slabel = filter_hide_server ? "Server: hidden" : "Server: shown";
			int tw_s = ctx->text_width(ctx->style->font, slabel, 0);
			int th   = ctx->text_height(ctx->style->font);
			mu_draw_text(ctx, ctx->style->font, slabel, -1,
						 mu_vec2(filter_server_btn.x + (filter_server_btn.w - tw_s) / 2,
								 filter_server_btn.y + (filter_server_btn.h - th) / 2),
						 mu_color(230, 230, 230, 255));

			mu_draw_rect(ctx, filter_clear_btn, mu_color(hov_c ? 110 : 80, hov_c ? 80 : 60, hov_c ? 80 : 60, 255));
			mu_draw_box(ctx, filter_clear_btn,  mu_color(160, 120, 120, 255));
			int tw_c = ctx->text_width(ctx->style->font, "Clear", 0);
			mu_draw_text(ctx, ctx->style->font, "Clear", -1,
						 mu_vec2(filter_clear_btn.x + (filter_clear_btn.w - tw_c) / 2,
								 filter_clear_btn.y + (filter_clear_btn.h - th) / 2),
						 mu_color(230, 230, 230, 255));
		} else {
			filter_clear_btn  = mu_rect(0, 0, 0, 0);
			filter_server_btn = mu_rect(0, 0, 0, 0);
		}

		mu_layout_row(ctx, 1, (int[]) { -1 }, -1);
		mu_begin_panel(ctx, "ChatLogContent");
		mu_Container* panel = mu_get_current_container(ctx);

		/* Cache the panel body (already shrunk by microui to exclude the
		   scrollbar) so the mouse handler can reject clicks that land on
		   the scrollbar. */
		content_body_rect = panel->body;

		int prev_content_h = panel->content_size.y;
		int prev_count = line_count;
		/* Track chathistory size separately so we can distinguish a
		   genuine prepend (older messages loaded) from an append (new
		   live message arriving). The previous code treated any growth
		   in line_count as a prepend, which scrolled the user's view
		   downward by one line per incoming live message while they were
		   trying to read older history. */
		int prev_hc = prev_history_count;
		int cur_hc = chathistory_count();

		build_visible_lines();

		/* Search index lives in line-space, so it has to be rebuilt
		   the moment the visible_lines array is. We do it here -
		   before the prepend-anchor and bottom-stick logic - so any
		   pending nav request can resolve into a concrete pending
		   scroll target that the post-panel scroll override picks up. */
		search_recompute_matches();
		search_apply_nav_request();

		/* Anchor the user's reading position only when the growth came
		   from chathistory (lines prepended at the top). Live additions
		   land at the bottom and require no scroll adjustment. */
		if(cur_hc > prev_hc && prev_count > 0) {
			int added_h = (line_count - prev_count) * chatlog_text_height();
			(void)prev_content_h;
			if(added_h > 0) panel->scroll.y += added_h;
			/* Selection indices are positional; prepending invalidates them. */
			sel_anchor_line = sel_active_line = -1;
			lmb_dragging = 0;
			pending_anchor = 0;
		}
		prev_history_count = cur_hc;

		const char* top_raw = (line_count > 0) ? lines[line_count - 1].raw : NULL;
		int top_len = (line_count > 0) ? lines[line_count - 1].plain_len : 0;
		/* Detect appends to session_log so auto-scroll-to-bottom can fire. */
		int live_changed = (session_log_count != prev_session_count);
		int messages_changed = live_changed
			|| ((intptr_t)top_raw != prev_top_chat_idx)
			|| (top_len != prev_top_chat_len);
		prev_session_count = session_log_count;
		prev_top_chat_idx = (intptr_t)top_raw;
		prev_top_chat_len = top_len;

		int max_scroll_before = panel->content_size.y - panel->body.h;
		if(max_scroll_before < 0) max_scroll_before = 0;
		/* +/- padding*2 slack: microui's scrollbar clamps cs+=padding*2,
		   so being within that of the visible max still counts as bottom. */
		int was_at_bottom = (panel->scroll.y >= max_scroll_before
							 - ctx->style->padding * 2 - 4);

		/* "Stick to bottom" needs the post-build content_size to clamp
		   against, but content_size is only updated in mu_end_panel.
		   Setting scroll.y = INT_MAX here gets clamped this frame to the
		   stale max (so this frame draws short of true bottom), but
		   stays at INT_MAX after mu_end_panel updates content_size. The
		   real fix happens after mu_end_panel below: we set scroll.y to
		   the freshly-computed max so next begin_panel's clamp is a
		   no-op AND this same value is used by the *next* frame's
		   layout. */
		int want_scroll_to_bottom = needs_scroll_to_bottom
									|| (messages_changed && was_at_bottom);
		if(want_scroll_to_bottom) {
			panel->scroll.y = 0x7FFFFFFF;
			needs_scroll_to_bottom = 0;
		}

		if(line_count == 0 && chathistory_count() == 0) {
			mu_layout_row(ctx, 1, (int[]) { -1 }, chatlog_text_height() * 2);
			mu_Rect empty = mu_layout_next(ctx);
			mu_draw_text(ctx, ctx->style->font,
						 "No messages yet.", -1,
						 mu_vec2(empty.x + 8, empty.y + 4),
						 mu_color(160, 160, 160, 255));
		} else {
			/* Top of scroll area: button to load older history (or status text). */
			int can_load = !chathistory_no_more() && network_connected
						&& network_current_ip[0] && network_current_port != 0;
			if(can_load || chathistory_loading()) {
				mu_layout_row(ctx, 1, (int[]) { -1 }, 28);
				mu_Rect btn = mu_layout_next(ctx);
				int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
				int hov = mx >= btn.x && mx < btn.x + btn.w && my >= btn.y && my < btn.y + btn.h;
				int loading = chathistory_loading();
				const char* label = loading ? "Loading previous chat..." : "Load previous chat";
				mu_Color bg = mu_color(loading ? 50 : (hov ? 90 : 60),
									   loading ? 50 : (hov ? 90 : 60),
									   loading ? 60 : (hov ? 110 : 80), 255);
				mu_draw_rect(ctx, mu_rect(btn.x + 4, btn.y + 4, btn.w - 8, btn.h - 8), bg);
				mu_draw_box(ctx, mu_rect(btn.x + 4, btn.y + 4, btn.w - 8, btn.h - 8),
							mu_color(120, 120, 140, 255));
				int tw = ctx->text_width(ctx->style->font, label, 0);
				int th = ctx->text_height(ctx->style->font);
				mu_draw_text(ctx, ctx->style->font, label, -1,
							 mu_vec2(btn.x + (btn.w - tw) / 2, btn.y + (btn.h - th) / 2),
							 mu_color(230, 230, 230, 255));
				/* Trigger on click (only when not currently loading). LMB
				   handler already consumed the press for selection; we
				   detect via mouse_down here to avoid coupling. */
				if(!loading && hov && (ctx->mouse_pressed & MU_MOUSE_LEFT)) {
					chathistory_request_load();
				}
			} else if(chathistory_no_more() && chathistory_count() > 0) {
				mu_layout_row(ctx, 1, (int[]) { -1 }, 22);
				mu_Rect r = mu_layout_next(ctx);
				const char* msg = "Beginning of available history";
				int tw = ctx->text_width(ctx->style->font, msg, 0);
				int th = ctx->text_height(ctx->style->font);
				mu_draw_text(ctx, ctx->style->font, msg, -1,
							 mu_vec2(r.x + (r.w - tw) / 2, r.y + (r.h - th) / 2),
							 mu_color(140, 140, 140, 255));
			}

			for(int i = 0; i < line_count; i++) {
				render_chat_line(ctx, i);
			}
		}

		if(pending_anchor) {
			int line, ch;
			resolve_mouse_target(ctx, &line, &ch);
			if(line >= 0) {
				sel_anchor_line = line;
				sel_anchor_char = ch;
				sel_active_line = line;
				sel_active_char = ch;
			} else {
				sel_anchor_line = sel_active_line = -1;
			}
			pending_anchor = 0;
		} else if(lmb_dragging && sel_anchor_line >= 0) {
			int line, ch;
			resolve_mouse_target(ctx, &line, &ch);
			if(line >= 0) {
				sel_active_line = line;
				sel_active_char = ch;
			}
		}

		ctxmenu_render(ctx);
		linkmodal_render(ctx);

		mu_end_panel(ctx);
		/* content_size is now updated. Pin scroll.y to the true new max
		   so that (a) next frame's begin_panel clamp is a no-op, and
		   (b) layout->body.y - scroll.y in the next frame's row layout
		   produces the bottom-aligned positions we want. Without this,
		   a stuck INT_MAX gets clamped to whatever stale max microui
		   sees on the next begin_panel (which uses last-frame content_size
		   if anything else messes with it - belt and braces). */
		if(want_scroll_to_bottom) {
			int true_max = panel->content_size.y + ctx->style->padding * 2
						 - panel->body.h;
			if(true_max < 0) true_max = 0;
			panel->scroll.y = true_max;
		}
		/* Search-navigation scroll override. Runs AFTER the bottom-
		   stick block above so that pressing "next match" while a new
		   live message lands on the same frame keeps the user on the
		   match instead of yanking them back to "now". The line rect
		   used here was just populated by render_chat_line above, so
		   coordinates are valid for this frame; setting scroll.y now
		   takes effect on the NEXT frame's layout - same one-frame
		   delay as the bottom-stick path. */
		if(search_pending_scroll >= 0
		   && search_pending_scroll < search_match_count) {
			int line_idx = search_matches[search_pending_scroll].line;
			if(line_idx >= 0 && line_idx < line_count
			   && lines[line_idx].rect.h > 0) {
				int line_screen_y = lines[line_idx].rect.y;
				/* rect.y is in screen coords; convert to content-y by
				   undoing the panel offset and adding back the current
				   scroll. */
				int content_y = line_screen_y - panel->body.y + panel->scroll.y;
				/* Park the match about a third of the way down the
				   visible area: centering hides it behind a too-active
				   "is at bottom?" check, and pinning to the top means
				   no context above the hit. A third reads naturally. */
				int target = content_y - panel->body.h / 3;
				int cs_h = panel->content_size.y + ctx->style->padding * 2;
				int max_scroll = cs_h - panel->body.h;
				if(max_scroll < 0) max_scroll = 0;
				if(target < 0) target = 0;
				if(target > max_scroll) target = max_scroll;
				panel->scroll.y = target;
			}
			search_pending_scroll = -1;
		}
		mu_end_panel(ctx);
		mu_end_window(ctx);
	}

	update_hover_cursor(ctx);
}

static void hud_chatlog_keyboard(int key, int action, int mods, int internal) {
	if(action == WINDOW_RELEASE) return;

	if(key == WINDOW_KEY_ESCAPE) {
		if(linkmodal_visible) {
			linkmodal_close();
			return;
		}
		if(ctxmenu_visible) {
			ctxmenu_hide();
			return;
		}
		/* Esc closes the search bar before bailing on the whole HUD -
		   matches user expectation set by every find-in-page UI on
		   the planet, and keeps the second Esc available for "back
		   to game". */
		if(search_visible) {
			search_close();
			return;
		}
		/* Single ESC returns to gameplay cleanly. The previous handler kept
		   show_exit set, which left hud_ingame in menu-mode and froze input
		   until a second ESC press. */
		show_exit = 0;
		hud_change(&hud_ingame);
		return;
	}

	if(linkmodal_visible) return;

	/* Ctrl+F / Cmd+F toggles the search bar. We look at `internal`
	   (raw GLFW key code) rather than a translated WINDOW_KEY_*
	   because the letter F isn't reliably bound through the
	   keybinding config - same trick the chat input uses for
	   Home/End. */
	if(internal == CHATLOG_KEY_F && (mods || window_super_down())) {
		search_open();
		return;
	}

	/* When the search bar is open, it captures navigation and editing
	   keys so the user can drive the search without their keystrokes
	   being interpreted as game-style shortcuts. Other keys still
	   fall through to the existing handlers below (Ctrl+C still
	   copies the chat selection, etc.) - this is intentional, the
	   search bar isn't a modal. */
	if(search_visible) {
		/* Ctrl/Cmd+V pastes from clipboard into the query. Mirrors
		   the chat input's paste path. */
		if(key == WINDOW_KEY_V && (mods || window_super_down())) {
			search_paste_clipboard();
			return;
		}
		if(key == WINDOW_KEY_BACKSPACE) {
			search_backspace();
			return;
		}
		/* Enter / F3 / arrow-down advance; Shift with any of these
		   walks backward. window_shift_down() reads live key state
		   because mods doesn't carry the shift bit in this
		   keyboard pipeline. */
		if(key == WINDOW_KEY_ENTER || key == WINDOW_KEY_F3
		   || key == WINDOW_KEY_CURSOR_DOWN) {
			search_nav_request = window_shift_down() ? SEARCH_NAV_PREV : SEARCH_NAV_NEXT;
			return;
		}
		if(key == WINDOW_KEY_CURSOR_UP) {
			search_nav_request = SEARCH_NAV_PREV;
			return;
		}
	}

	/* F3 outside the search bar still acts as "find next" if there's
	   an active search - convenient when the user closed the bar but
	   wants to keep stepping through hits. (Closed bar = no hits, so
	   this is a no-op in that case; the branch is here for symmetry
	   with editor conventions.) */
	if(!search_visible && key == WINDOW_KEY_F3 && search_match_count > 0) {
		search_nav_request = window_shift_down() ? SEARCH_NAV_PREV : SEARCH_NAV_NEXT;
		return;
	}

	if(key == WINDOW_KEY_C && (mods || window_super_down())) {
		copy_selection_to_clipboard();
	}

	if(internal == CHATLOG_KEY_A && (mods || window_super_down())) {
		select_all_lines();
	}
}

static void hud_chatlog_mouseclick(double x, double y, int button, int action, int mods) {
	int mx = (int)x, my = (int)y;

	if(linkmodal_visible) {
		if(button == WINDOW_MOUSE_LMB && action == WINDOW_PRESS) {
			if(linkmodal_hit(linkmodal_visit_btn, mx, my)) {
				window_open_url(linkmodal_url);
				linkmodal_close();
			} else if(linkmodal_hit(linkmodal_cancel_btn, mx, my)) {
				linkmodal_close();
			}
		}
		return;
	}

	if(button == WINDOW_MOUSE_LMB) {
		if(action == WINDOW_PRESS) {
			if(ctxmenu_visible) {
				if(ctxmenu_hit(mx, my))
					ctxmenu_commit();
				ctxmenu_hide();
				return;
			}
			/* Search bar widgets take priority - they sit above the
			   filter banner and the content panel, and dispatching
			   them here (before the content_body_rect rejection)
			   keeps the prev/next/close buttons working without
			   accidentally starting a text selection. */
			if(search_handle_click(mx, my)) return;
			/* Filter clear button takes priority over selection drag. */
			if(filter_active() && filter_clear_btn.w > 0
			   && mx >= filter_clear_btn.x && mx < filter_clear_btn.x + filter_clear_btn.w
			   && my >= filter_clear_btn.y && my < filter_clear_btn.y + filter_clear_btn.h) {
				filter_clear();
				return;
			}
			if(filter_active() && filter_server_btn.w > 0
			   && mx >= filter_server_btn.x && mx < filter_server_btn.x + filter_server_btn.w
			   && my >= filter_server_btn.y && my < filter_server_btn.y + filter_server_btn.h) {
				filter_hide_server = !filter_hide_server;
				return;
			}
			/* Reject presses that land outside the rendered content body
			   (e.g. on the vertical scrollbar). microui handles scrollbar
			   drag itself; if we ALSO start a text selection here, the
			   user's scroll-thumb drag sweeps a runaway selection across
			   every line the cursor passes over. */
			if(content_body_rect.w > 0 && content_body_rect.h > 0) {
				if(mx <  content_body_rect.x ||
				   mx >= content_body_rect.x + content_body_rect.w ||
				   my <  content_body_rect.y ||
				   my >= content_body_rect.y + content_body_rect.h) {
					/* Clear any half-open selection and let microui own
					   this press for scrollbar interaction. */
					sel_anchor_line = sel_active_line = -1;
					pending_anchor = 0;
					lmb_dragging = 0;
					return;
				}
			}
			/* Ctrl/Cmd + LMB on an existing selection copies without
			   clobbering it. Provides a working "click to copy" gesture on
			   macOS setups where two-finger right-click isn't enabled. */
			if((mods || window_super_down()) && has_selection()) {
				copy_selection_to_clipboard();
				return;
			}
			/* Shift + LMB extends the existing selection to the click
			   position. mods bitmask doesn't carry shift, so check the
			   key state directly. */
			if(window_shift_down() && has_selection()) {
				int ln = -1, ch = 0;
				for(int i = 0; i < line_count; i++) {
					mu_Rect rr = lines[i].rect;
					if(my >= rr.y && my < rr.y + rr.h) { ln = i; break; }
				}
				if(ln >= 0 && !lines[ln].is_placeholder) {
					mu_Rect rr = lines[ln].rect;
					ch = plain_char_at_offset(lines[ln].plain, lines[ln].plain_len,
											  (float)(mx - rr.x));
					sel_active_line = ln;
					sel_active_char = ch;
					lmb_dragging = 1;
				}
				return;
			}
			/* Resolve the URL under the press now; we'll commit on release
			   only if the user did not drag to make a real selection. */
			pending_url_line = pending_url_idx = -1;
			if(line_count > 0) {
				int ln = -1, ch;
				for(int i = 0; i < line_count; i++) {
					mu_Rect rr = lines[i].rect;
					if(my >= rr.y && my < rr.y + rr.h) { ln = i; break; }
				}
				if(ln >= 0 && !lines[ln].is_placeholder) {
					mu_Rect rr = lines[ln].rect;
					float rel_x = (float)(mx - rr.x);
					ch = plain_char_at_offset(lines[ln].plain, lines[ln].plain_len, rel_x);
					int u = url_at_position(ln, ch);
					if(u >= 0) {
						pending_url_line = ln;
						pending_url_idx  = u;
						pending_url_press_x = mx;
						pending_url_press_y = my;
					}
				}
			}
			pending_anchor = 1;
			lmb_dragging = 1;
		} else if(action == WINDOW_RELEASE) {
			lmb_dragging = 0;
			int dragged = !(sel_anchor_line == sel_active_line && sel_anchor_char == sel_active_char);
			if(!dragged && pending_url_line >= 0 && pending_url_idx >= 0) {
				if(pending_url_line < line_count) {
					mu_Rect rr = lines[pending_url_line].rect;
					if(mx >= rr.x && mx < rr.x + rr.w && my >= rr.y && my < rr.y + rr.h) {
						float rel_x = (float)(mx - rr.x);
						int ch = plain_char_at_offset(lines[pending_url_line].plain,
													  lines[pending_url_line].plain_len, rel_x);
						int u = url_at_position(pending_url_line, ch);
						if(u == pending_url_idx) {
							struct url_span sp = lines[pending_url_line].urls[u];
							int n = sp.end - sp.start;
							if(n > 0 && n < (int)sizeof(linkmodal_url) - 1) {
								char tmp[1024];
								memcpy(tmp, lines[pending_url_line].plain + sp.start, n);
								tmp[n] = 0;
								linkmodal_open(tmp);
							}
						}
					}
				}
			}
			pending_url_line = pending_url_idx = -1;
			if(!dragged) {
				sel_anchor_line = sel_active_line = -1;
			}
		}
	} else if(button == WINDOW_MOUSE_RMB && action == WINDOW_PRESS) {
		if(ctxmenu_visible) {
			ctxmenu_hide();
			return;
		}
		/* Priority: name -> filter menu, selection -> copy menu, else select-all. */
		int ln = -1;
		for(int i = 0; i < line_count; i++) {
			mu_Rect rr = lines[i].rect;
			if(my >= rr.y && my < rr.y + rr.h) { ln = i; break; }
		}
		if(ln >= 0 && !lines[ln].is_placeholder && lines[ln].name_len > 0) {
			mu_Rect rr = lines[ln].rect;
			float rel_x = (float)(mx - rr.x);
			int ch = plain_char_at_offset(lines[ln].plain, lines[ln].plain_len, rel_x);
			int ns = lines[ln].name_start;
			int ne = ns + lines[ln].name_len;
			if(ch >= ns && ch < ne) {
				char name[64];
				int nl = lines[ln].name_len;
				if(nl > (int)sizeof(name) - 1) nl = sizeof(name) - 1;
				memcpy(name, lines[ln].plain + ns, nl);
				name[nl] = 0;
				if(filter_active() && strcmp(name, filter_active_name) == 0)
					ctxmenu_show_filter_clear(mx, my);
				else
					ctxmenu_show_filter(mx, my, name);
				return;
			}
		}
		if(has_selection()) {
			ctxmenu_show_copy(mx, my);
			return;
		}
		if(content_body_rect.w > 0 && content_body_rect.h > 0) {
			if(mx <  content_body_rect.x ||
			   mx >= content_body_rect.x + content_body_rect.w ||
			   my <  content_body_rect.y ||
			   my >= content_body_rect.y + content_body_rect.h) {
				return;
			}
		}
		if(line_count > 0)
			ctxmenu_show_select_all(mx, my);
	}
}

static void hud_chatlog_mouselocation(double x, double y) {
	(void)x; (void)y;
}

static void hud_chatlog_touch(void* finger, int action, float x, float y, float dx, float dy) {
	window_setmouseloc(x, y);
}

struct hud hud_chatlog = {
	hud_chatlog_init,
	NULL,
	hud_chatlog_render,
	hud_chatlog_keyboard,
	hud_chatlog_mouselocation,
	hud_chatlog_mouseclick,
	NULL,
	hud_chatlog_touch,
	hud_chatlog_ui_images,
	0,
	0,
	NULL,
};
