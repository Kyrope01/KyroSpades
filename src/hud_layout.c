/*

	KyroSpades HUD layout store — implementation.

        This file is part of KyroSpades.  GPL v3 or later (see hud.h).
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "config.h"
#include "log.h"
#include "file.h"
#include "parson.h"

#include "hud_layout.h"

#define LAYOUT_FILE      "hud_layout.json"
#define LAYOUT_VERSION   1
#define LAYOUT_OFFSET_LIMIT 1.25F   /* max |fraction| from the anchor */

/* ── State ─────────────────────────────────────────────────────────────── */

/* Persisted tuple (what JSON stores and what dirty-checks compare). */
struct layout_el {
	unsigned char mode;      /* 0 = natural (factory), 1 = custom          */
	unsigned char anchor;    /* HUD_ANCHOR_*, meaningful in custom mode    */
	unsigned char visible;
	float ox, oy;            /* offset from anchor, fraction of W / of H   */
	float scale;
};

/* Volatile per-frame bookkeeping (never compared / never serialized). */
struct layout_volatile_el {
	float bounds[4];         /* last reported [x, y_top, w, h], GL space   */
	unsigned char has_bounds;
	unsigned char drawn_this_frame;
	unsigned int  draw_seq;  /* monotonic stamp of the latest report       */
};

static struct layout_el         LS[HUD_EL_COUNT];
static struct layout_el         LS_saved[HUD_EL_COUNT];
static struct layout_volatile_el LV[HUD_EL_COUNT];
static unsigned int draw_seq_counter = 0;
static int layout_ready = 0;

#define MODE_NATURAL 0
#define MODE_CUSTOM  1

/* Element metadata. ids/json_keys are append-only (JSON stability). */
static const struct {
	const char* key;
	const char* name;
	unsigned char default_anchor;
	bool scalable;
	float nominal[4];        /* fallback hitbox @800x600, y = top edge */
} EL_META[HUD_EL_COUNT] = {
	/* key          name                   anchor  scalable  x     y     w     h  */
	{ "health",    "Health",               HUD_ANCHOR_BL, true,  {   6,  60, 164,  52 } },
	{ "ammo",      "Ammo / held item",     HUD_ANCHOR_BR, true,  { 682,  40, 110,  36 } },
	{ "palette",   "Block color palette",  HUD_ANCHOR_BC, false, { 342, 142, 116, 116 } },
	{ "chat",      "Chat",                 HUD_ANCHOR_BL, false, {   3, 284, 336, 208 } },
	{ "killfeed",  "Killfeed",             HUD_ANCHOR_TL, false, {   3, 578, 320,  54 } },
	{ "minimap",   "Minimap (small)",      HUD_ANCHOR_TR, true,  { 656, 586, 132, 148 } },
	{ "scores_top","Team scores (top)",    HUD_ANCHOR_TC, false, { 325, 576, 150,  24 } },
	{ "gmi",       "Player counter",       HUD_ANCHOR_BR, true,  { 722, 126,  70,  74 } },
	{ "scoreboard","Scoreboard (TAB)",     HUD_ANCHOR_MC, false, { 100, 596, 600, 338 } },
	{ "fpsbox",    "FPS + ping box",       HUD_ANCHOR_MR, true,  { 695, 366, 105,  40 } },
	{ "stats",     "Player stats",         HUD_ANCHOR_ML, true,  {   8, 320, 230,  96 } },
	{ "techstats", "Tech stats",           HUD_ANCHOR_MR, true,  { 560, 320, 232,  80 } },
	{ "spectate",  "Spectator labels",     HUD_ANCHOR_TC, false, { 250,  26, 300,  22 } },
	{ "targetinfo","Target info",          HUD_ANCHOR_MC, false, { 250, 120, 300,  16 } },
	{ "yclamp",    "Y-Clamp indicator",    HUD_ANCHOR_ML, false, {   8, 296, 140,  16 } },
	{ "centermsg", "Center notices",       HUD_ANCHOR_MC, false, { 250, 300, 300,  32 } },
	{ "tcbar",     "TC capture bar",       HUD_ANCHOR_MC, false, { 180, 150, 440,  20 } },
};

/* ── Small helpers ─────────────────────────────────────────────────────── */

static struct layout_el* el_ok(int el) {
	if(el < 0 || el >= HUD_EL_COUNT)
		return NULL;
	return &LS[el];
}

static float clampf(float v, float lo, float hi) {
	return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* Fallback bounds when the element didn't report this frame: the nominal
   rect scaled from the 800x600 reference to the current window. */
static void nominal_bounds(int el, float* x, float* y, float* w, float* h) {
	float sw = settings.window_width / 800.0F;
	float sh = settings.window_height / 600.0F;
	*x = EL_META[el].nominal[0] * sw;
	*y = EL_META[el].nominal[1] * sh;
	*w = EL_META[el].nominal[2] * sw;
	*h = EL_META[el].nominal[3] * sh;
}

void hud_layout_anchor_point(int anchor, float* x, float* y) {
	if(!x || !y)
		return;
	switch(anchor % 3) {
		case 0: *x = 0.0F; break;
		case 1: *x = settings.window_width * 0.5F; break;
		default: *x = (float)settings.window_width; break;
	}
	/* GL space is y-up: TOP row sits at y = H, BOTTOM row at y = 0. */
	switch(anchor / 3) {
		case 0: *y = (float)settings.window_height; break;
		case 1: *y = settings.window_height * 0.5F; break;
		default: *y = 0.0F; break;
	}
}

/* Nominal hitbox size in on-screen GL pixels: table value (at the 800x600
   reference size) scaled to the window and by the element scale. Used by the
   editor as the fallback selection box for elements that drew nothing this
   frame (hidden or not shown). */
void hud_layout_nominal_bounds(int el, float* w, float* h) {
	if(w) *w = 40.0F;
	if(h) *h = 16.0F;
	if(el < 0 || el >= HUD_EL_COUNT)
		return;
	float sx = (float)settings.window_width / 800.0F;
	float sy = (float)settings.window_height / 600.0F;
	float s = hud_layout_scale(el);
	if(w) *w = EL_META[el].nominal[2] * sx * s;
	if(h) *h = EL_META[el].nominal[3] * sy * s;
}

/* ── Read side (used by rendering every frame) ─────────────────────────── */

bool hud_layout_visible(int el) {
	struct layout_el* e = el_ok(el);
	return e ? (e->visible != 0) : true;
}

float hud_layout_scale(int el) {
	struct layout_el* e = el_ok(el);
	if(!e || e->mode != MODE_CUSTOM || !EL_META[el].scalable)
		return 1.0F;
	if(e->scale <= 0.05F)
		return 1.0F;
	return e->scale;
}

float hud_layout_get_scale(int el) {
	struct layout_el* e = el_ok(el);
	return (e && e->scale > 0.05F) ? e->scale : 1.0F;
}

void hud_layout_origin(int el, float* x, float* y) {
	struct layout_el* e = el_ok(el);
	if(!e || e->mode != MODE_CUSTOM || !x || !y)
		return;
	float ax, ay;
	hud_layout_anchor_point(e->anchor, &ax, &ay);
	*x = ax + e->ox * settings.window_width;
	*y = ay + e->oy * settings.window_height;
}

/* ── Bounds registry & picking ─────────────────────────────────────────── */

void hud_layout_frame_begin(void) {
	draw_seq_counter++;
}

void hud_layout_report_bounds(int el, float x, float y, float w, float h) {
	if(el < 0 || el >= HUD_EL_COUNT)
		return;
	LV[el].bounds[0] = x;
	LV[el].bounds[1] = y;
	LV[el].bounds[2] = w;
	LV[el].bounds[3] = h;
	LV[el].has_bounds = 1;
	LV[el].drawn_this_frame = 1;
	LV[el].draw_seq = draw_seq_counter;
}

bool hud_layout_bounds(int el, float* x, float* y, float* w, float* h) {
	if(el < 0 || el >= HUD_EL_COUNT || !x || !y || !w || !h)
		return false;
	if(LV[el].has_bounds && LV[el].bounds[2] > 0.0F && LV[el].bounds[3] > 0.0F) {
		*x = LV[el].bounds[0];
		*y = LV[el].bounds[1];
		*w = LV[el].bounds[2];
		*h = LV[el].bounds[3];
		return true;
	}
	nominal_bounds(el, x, y, w, h);
	return false; /* false = fallback geometry (ghost outline) */
}

int hud_layout_pick(float mx_gl, float my_gl) {
	int best = -1;
	unsigned int best_seq = 0;
	for(int k = 0; k < HUD_EL_COUNT; k++) {
		/* Only elements that actually drew this frame participate: that
		   makes z-order resolution (topmost = last drawn = highest seq)
		   match what the eye sees. Never-drawn/context elements remain
		   reachable through the panel list instead. */
		if(!LV[k].drawn_this_frame || LV[k].draw_seq != draw_seq_counter)
			continue;
		float bx, by, bw, bh;
		hud_layout_bounds(k, &bx, &by, &bw, &bh);
		if(mx_gl >= bx - 3.0F && mx_gl <= bx + bw + 3.0F
		   && my_gl >= by - bh - 3.0F && my_gl <= by + 3.0F) {
			if(best < 0 || LV[k].draw_seq >= best_seq) {
				best = k;
				best_seq = LV[k].draw_seq;
			}
		}
	}
	return best;
}

/* ── Editor-side mutation ──────────────────────────────────────────────── */

void hud_layout_set_px(int el, float x, float y) {
	struct layout_el* e = el_ok(el);
	if(!e)
		return;
	float W = (float)settings.window_width;
	float H = (float)settings.window_height;
	if(W < 1.0F || H < 1.0F)
		return;
	if(e->mode != MODE_CUSTOM) {
		e->mode = MODE_CUSTOM;
		e->anchor = EL_META[el].default_anchor;
	}
	float ax, ay;
	hud_layout_anchor_point(e->anchor, &ax, &ay);
	e->ox = clampf((x - ax) / W, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
	e->oy = clampf((y - ay) / H, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
}

int hud_layout_get_anchor(int el) {
	struct layout_el* e = el_ok(el);
	if(!e)
		return HUD_ANCHOR_MC;
	/* In natural mode the effective anchor is the element's default. */
	return e->mode == MODE_CUSTOM ? (int)e->anchor : (int)EL_META[el].default_anchor;
}

void hud_layout_set_anchor(int el, int anchor) {
	struct layout_el* e = el_ok(el);
	if(!e || anchor < HUD_ANCHOR_TL || anchor > HUD_ANCHOR_BR)
		return;
	float W = (float)settings.window_width;
	float H = (float)settings.window_height;
	if(W < 1.0F || H < 1.0F)
		return;
	/* Current on-screen base (from the freshest reported bounds) must not
	   jump when the anchor changes: recompute ox/oy relative to the new
	   anchor point. Natural elements implicitly sit at their default
	   anchor with zero offsets, so switching anchor enters custom mode. */
	float bx, by, bw, bh;
	bool have = hud_layout_bounds(el, &bx, &by, &bw, &bh);
	if(!have) {
		float ax, ay;
		hud_layout_anchor_point(EL_META[el].default_anchor, &ax, &ay);
		bx = ax + (e->mode == MODE_CUSTOM ? e->ox * W : 0.0F);
		by = ay + (e->mode == MODE_CUSTOM ? e->oy * H : 0.0F);
	}
	if(e->mode != MODE_CUSTOM) {
		e->mode = MODE_CUSTOM;
		e->anchor = EL_META[el].default_anchor;
		e->ox = 0.0F;
		e->oy = 0.0F;
	}
	e->anchor = (unsigned char)anchor;
	float ax, ay;
	hud_layout_anchor_point(anchor, &ax, &ay);
	e->ox = clampf((bx - ax) / W, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
	e->oy = clampf((by - ay) / H, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
}

void hud_layout_set_visible(int el, bool vis) {
	struct layout_el* e = el_ok(el);
	if(e)
		e->visible = vis ? 1 : 0;
}

void hud_layout_set_scale(int el, float s) {
	struct layout_el* e = el_ok(el);
	if(!e || !EL_META[el].scalable)
		return;
	if(e->mode != MODE_CUSTOM) {
		e->mode = MODE_CUSTOM;
		e->anchor = EL_META[el].default_anchor;
		e->ox = 0.0F;
		e->oy = 0.0F;
	}
	e->scale = clampf(s, 0.5F, 2.0F);
}

bool hud_layout_is_custom(int el) {
	struct layout_el* e = el_ok(el);
	return e ? (e->mode == MODE_CUSTOM) : false;
}

void hud_layout_reset_element(int el) {
	struct layout_el* e = el_ok(el);
	if(!e)
		return;
	e->mode = MODE_NATURAL;
	e->anchor = EL_META[el].default_anchor;
	e->ox = 0.0F;
	e->oy = 0.0F;
	e->scale = 1.0F;
	e->visible = 1;
}

void hud_layout_reset_all(void) {
	for(int k = 0; k < HUD_EL_COUNT; k++)
		hud_layout_reset_element(k);
}

/* ── Dirty tracking / snapshot ─────────────────────────────────────────── */

static bool tuple_equal(const struct layout_el* a, const struct layout_el* b) {
	return a->mode == b->mode && a->anchor == b->anchor && a->visible == b->visible
		&& a->ox == b->ox && a->oy == b->oy && a->scale == b->scale;
}

bool hud_layout_is_dirty(void) {
	for(int k = 0; k < HUD_EL_COUNT; k++)
		if(!tuple_equal(&LS[k], &LS_saved[k]))
			return true;
	return false;
}

void hud_layout_snapshot_take(void) {
	memcpy(LS_saved, LS, sizeof(LS));
}

void hud_layout_snapshot_restore(void) {
	memcpy(LS, LS_saved, sizeof(LS));
}

/* ── Persistence ───────────────────────────────────────────────────────── */

static void load_element(int el, JSON_Object* obj) {
	int anchor = (int)json_object_get_number(obj, "anchor");
	double ox = json_object_get_number(obj, "ox");
	double oy = json_object_get_number(obj, "oy");
	int vis = json_object_get_boolean(obj, "visible");
	double scale = json_object_get_number(obj, "scale");

	if(anchor < HUD_ANCHOR_TL || anchor > HUD_ANCHOR_BR)
		anchor = EL_META[el].default_anchor;
	LS[el].anchor = (unsigned char)anchor;
	LS[el].ox = clampf((float)ox, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
	LS[el].oy = clampf((float)oy, -LAYOUT_OFFSET_LIMIT, LAYOUT_OFFSET_LIMIT);
	if(vis >= 0)
		LS[el].visible = (unsigned char)(vis ? 1 : 0);
	LS[el].scale = (scale > 0.05) ? clampf((float)scale, 0.5F, 2.0F) : 1.0F;
	LS[el].mode = MODE_CUSTOM;
}

void hud_layout_init(void) {
	for(int k = 0; k < HUD_EL_COUNT; k++)
		hud_layout_reset_element(k);
	memset(LV, 0, sizeof(LV));
	memcpy(LS_saved, LS, sizeof(LS));   /* baseline for dirty tracking */

	char* text = (char*)file_load(LAYOUT_FILE);
	if(!text) {
		log_info("hud_layout: no %s, defaults in use", LAYOUT_FILE);
		layout_ready = 1;
		return;
	}

	JSON_Value* root_v = json_parse_string(text);
	free(text);
	if(!root_v || json_value_get_type(root_v) != JSONObject) {
		log_warn("hud_layout: %s failed to parse, defaults in use", LAYOUT_FILE);
		if(root_v)
			json_value_free(root_v);
		layout_ready = 1;
		return;
	}
	/* LS_saved already matches the reset state from above */

	JSON_Object* root = json_value_get_object(root_v);
	double version = json_object_get_number(root, "version");
	if(version > (double)LAYOUT_VERSION)
		log_warn("hud_layout: file version %.0f newer than supported %d, best-effort load",
				 version, LAYOUT_VERSION);

	JSON_Object* els = json_object_get_object(root, "elements");
	if(els) {
		size_t count = json_object_get_count(els);
		for(size_t i = 0; i < count; i++) {
			const char* name = json_object_get_name(els, i);
			JSON_Object* obj = json_object_get_object(els, name);
			if(!obj)
				continue;
			int match = -1;
			for(int k = 0; k < HUD_EL_COUNT; k++) {
				if(strcmp(EL_META[k].key, name) == 0) {
					match = k;
					break;
				}
			}
			if(match < 0) {
				log_warn("hud_layout: unknown element '%s' ignored", name ? name : "?");
				continue;
			}
			load_element(match, obj);
		}
	}
	json_value_free(root_v);

	memcpy(LS_saved, LS, sizeof(LS));
	layout_ready = 1;
	log_info("hud_layout: loaded %s", LAYOUT_FILE);
}

bool hud_layout_save(void) {
	JSON_Value* root_v = json_value_init_object();
	if(!root_v)
		return false;
	JSON_Object* root = json_value_get_object(root_v);
	json_object_set_number(root, "version", LAYOUT_VERSION);

	JSON_Value* els_v = json_value_init_object();
	if(!els_v) {
		json_value_free(root_v);
		return false;
	}
	JSON_Object* els = json_value_get_object(els_v);

	int written = 0;
	for(int k = 0; k < HUD_EL_COUNT; k++) {
		/* Only non-default elements need to be stored; missing = natural. */
		if(LS[k].mode != MODE_CUSTOM && LS[k].visible)
			continue;
		JSON_Value* e_v = json_value_init_object();
		if(!e_v)
			continue;
		JSON_Object* e = json_value_get_object(e_v);
		json_object_set_number(e, "anchor", LS[k].anchor);
		json_object_set_number(e, "ox", LS[k].ox);
		json_object_set_number(e, "oy", LS[k].oy);
		json_object_set_boolean(e, "visible", LS[k].visible ? 1 : 0);
		json_object_set_number(e, "scale", LS[k].scale);
		json_object_set_value(els, EL_META[k].key, e_v);
		written++;
	}
	json_object_set_value(root, "elements", els_v);

	char* text = json_serialize_to_string_pretty(root_v);
	json_value_free(root_v);
	if(!text) {
		log_warn("hud_layout: serialize failed");
		return false;
	}

	void* f = file_open(LAYOUT_FILE, "w");
	if(!f) {
		json_free_serialized_string(text);
		log_warn("hud_layout: cannot open %s for writing", LAYOUT_FILE);
		return false;
	}
	file_printf(f, "%s\n", text);
	file_close(f);
	json_free_serialized_string(text);

	memcpy(LS_saved, LS, sizeof(LS));
	log_info("hud_layout: saved %s (%d element%s)", LAYOUT_FILE, written,
			 written == 1 ? "" : "s");
	return true;
}

/* ── Palette geometry (shared hud.c <-> window.c) ──────────────────────── */

#define PALETTE_CELLS 8

float hud_layout_palette_cell(void) {
	return settings.window_height * 0.024F;
}

float hud_layout_palette_size(void) {
	return hud_layout_palette_cell() * PALETTE_CELLS;
}

float hud_layout_palette_left(void) {
	float base = (settings.window_width - hud_layout_palette_size()) * 0.5F;
	if(!layout_ready)
		return base;
	float x = base, y = settings.window_height * 0.045F + hud_layout_palette_size();
	hud_layout_origin(HUD_EL_PALETTE, &x, &y);
	return x;
}

float hud_layout_palette_top(void) {
	float base = settings.window_height * 0.045F + hud_layout_palette_size();
	if(!layout_ready)
		return base;
	float x = (settings.window_width - hud_layout_palette_size()) * 0.5F, y = base;
	hud_layout_origin(HUD_EL_PALETTE, &x, &y);
	return y;
}

float hud_layout_palette_bottom(void) {
	return hud_layout_palette_top() - hud_layout_palette_size();
}

void hud_layout_palette_cell_clamp(float sx, float sy, int* gx, int* gy) {
	float gl_y = settings.window_height - sy;
	float left = hud_layout_palette_left();
	float top = hud_layout_palette_top();
	float cell = hud_layout_palette_cell();
	int cx = (int)((sx - left) / cell);
	int cy = (int)((top - gl_y) / cell); /* gy=0 at top */
	if(cx < 0) cx = 0;
	if(cx > 7) cx = 7;
	if(cy < 0) cy = 0;
	if(cy > 7) cy = 7;
	*gx = cx;
	*gy = cy;
}

bool hud_layout_palette_contains(float sx, float sy) {
	/* A hidden palette must not leave an aim dead-zone behind. */
	if(!hud_layout_visible(HUD_EL_PALETTE))
		return false;
	float gl_y = settings.window_height - sy;
	float left = hud_layout_palette_left();
	float top = hud_layout_palette_top();
	float size = hud_layout_palette_size();
	return sx >= left && sx < left + size && gl_y >= top - size && gl_y < top;
}

/* ── Misc ──────────────────────────────────────────────────────────────── */

float hud_layout_snap(float px, int grid_px) {
	if(grid_px <= 1)
		return px;
	return floorf(px / grid_px + 0.5F) * grid_px;
}

const char* hud_layout_name(int el) {
	return (el >= 0 && el < HUD_EL_COUNT) ? EL_META[el].name : "?";
}

const char* hud_layout_key(int el) {
	return (el >= 0 && el < HUD_EL_COUNT) ? EL_META[el].key : "?";
}

bool hud_layout_scalable(int el) {
	return (el >= 0 && el < HUD_EL_COUNT) ? EL_META[el].scalable : false;
}

int hud_layout_default_anchor(int el) {
	return (el >= 0 && el < HUD_EL_COUNT) ? (int)EL_META[el].default_anchor : HUD_ANCHOR_MC;
}
