/*

	KyroSpades HUD layout store.

	Persistent, resolution-independent geometry for every movable HUD
	element (positions, anchors, visibility, scale) plus the frame bounds
	registry the editor uses for hit-testing.  Rendering code reads this
	module through three cheap calls:

	    hud_layout_origin()    rebase an element's draw origin
	    hud_layout_scale()     uniform element scale (1.0 when natural)
	    hud_layout_visible()   user visibility toggle

	With the default (natural) layout all three are no-ops, so gameplay
	renders exactly as it did before the HUD editor existed.

	Persistence goes to hud_layout.json next to config.ini.  This module
	is UI-free on purpose (no GL, no microui) so its math can be unit
	tested in isolation.

        This file is part of KyroSpades.  GPL v3 or later (see hud.h).
*/

#ifndef HUD_LAYOUT_H
#define HUD_LAYOUT_H

#include <stdbool.h>

/* Element ids are serialized (json_key below) — append only, never reorder. */
enum hud_element {
	HUD_EL_HEALTH = 0,
	HUD_EL_AMMO,
	HUD_EL_PALETTE,
	HUD_EL_CHAT,
	HUD_EL_KILLFEED,
	HUD_EL_MINIMAP,
	HUD_EL_SCORES_TOP,
	HUD_EL_GMI,
	HUD_EL_SCOREBOARD,
	HUD_EL_FPSBOX,
	HUD_EL_STATS,
	HUD_EL_TECHSTATS,
	HUD_EL_SPECTATE,
	HUD_EL_TARGETINFO,
	HUD_EL_YCLAMP,
	HUD_EL_CENTERMSG,
	HUD_EL_TCBAR,
	HUD_EL_COUNT
};

/* Row-major 9-point screen anchors. NOTE: indices are serialized; the TOP
   row maps to GL y = window_height (GL space is y-up, origin bottom-left). */
enum {
	HUD_ANCHOR_TL = 0, HUD_ANCHOR_TC, HUD_ANCHOR_TR,
	HUD_ANCHOR_ML,     HUD_ANCHOR_MC, HUD_ANCHOR_MR,
	HUD_ANCHOR_BL,     HUD_ANCHOR_BC, HUD_ANCHOR_BR
};

void  hud_layout_init(void);                 /* load hud_layout.json (or defaults) */
bool  hud_layout_save(void);                 /* write file; true on success        */
void  hud_layout_reset_all(void);            /* every element back to natural      */
void  hud_layout_reset_element(int el);

bool  hud_layout_is_custom(int el);
bool  hud_layout_is_dirty(void);             /* live state != last saved state     */
void  hud_layout_snapshot_take(void);        /* remember saved state (editor open) */
void  hud_layout_snapshot_restore(void);     /* Revert                             */

bool  hud_layout_visible(int el);
float hud_layout_scale(int el);              /* 1.0 when natural / not scalable    */

/* Core primitive: in natural mode this is a NO-OP (caller keeps its inline
   natural coordinates). In custom mode it overwrites x/y (GL space, y up,
   top-edge convention like texture_draw) with anchor_point + offset. */
void  hud_layout_origin(int el, float* x, float* y);

/* Editor-side mutation. All coordinates GL space; fractions stored. */
void  hud_layout_set_px(int el, float x, float y);
void  hud_layout_set_anchor(int el, int anchor);  /* keeps on-screen position */
void  hud_layout_set_visible(int el, bool vis);
void  hud_layout_set_scale(int el, float s);
float hud_layout_get_scale(int el);
int   hud_layout_get_anchor(int el);               /* HUD_ANCHOR_* (editor UI) */

/* Frame bounds registry (editor hit-testing / selection chrome).
   Report the union box of everything the element drew this frame, GL space,
   y = TOP edge (same convention as texture_draw). */
void  hud_layout_report_bounds(int el, float x, float y, float w, float h);
void  hud_layout_frame_begin(void);          /* reset fresh-frame flags */
bool  hud_layout_bounds(int el, float* x, float* y, float* w, float* h);
int   hud_layout_pick(float mx_gl, float my_gl); /* topmost element at GL point, -1 if none */

/* Block-color palette geometry — single source of truth shared by hud.c
   (renderer + touch) and window.c (aim-zone exclusion). GL space. */
float hud_layout_palette_cell(void);
float hud_layout_palette_size(void);
float hud_layout_palette_left(void);
float hud_layout_palette_top(void);
float hud_layout_palette_bottom(void);
void  hud_layout_palette_cell_clamp(float sx, float sy, int* gx, int* gy); /* screen coords y-down */
bool  hud_layout_palette_contains(float sx, float sy);                      /* screen coords y-down */

/* Snap helper: returns px snapped to the given grid (grid <= 1: unchanged). */
float hud_layout_snap(float px, int grid_px);

/* Anchor point in GL pixels for the CURRENT window size (editor chrome). */
void  hud_layout_anchor_point(int anchor, float* x, float* y);

/* Nominal hitbox size in on-screen GL pixels (editor fallback box). */
void  hud_layout_nominal_bounds(int el, float* w, float* h);

/* Element metadata (panel display names, serialized keys, capabilities). */
const char* hud_layout_name(int el);
const char* hud_layout_key(int el);
bool        hud_layout_scalable(int el);
int         hud_layout_default_anchor(int el);

#endif
