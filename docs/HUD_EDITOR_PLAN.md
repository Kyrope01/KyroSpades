# KyroSpades — Graphical HUD Editor

**Status:** Plan (pre-implementation) — v1.1, full self-audit pass completed (coordinate conventions re-derived
from the code, worked examples re-checked, input/persistence flows traced end-to-end; corrections folded in) ·
**Target:** KyroSpades (ButterSpades fork), C99 / OpenGL(ES) / SDL2+GLFW / microui
**Scope:** A WYSIWYG in-game editor that can move, hide/show and (where supported) scale every regular HUD element
(chat, killfeed, health, ammo, minimap, scoreboard, stats, …), with **Apply**, **Apply & Resume** and **Revert** buttons,
a rebindable hotkey, a pause-menu entry button, full persistence, and touch support.

> The purpose of this document is that an implementer can complete the feature **without making a single
> structural decision themselves**. Every coordinate, guard, state transition, file change and pitfall that
> matters is written down here, derived from the actual code (commit `11649de`, branch base of this work).
> Line numbers refer to the current tree and may drift by a few lines; function names are authoritative.

---

## Table of contents

1. [Goals and non-goals](#1-goals-and-non-goals)
2. [User experience specification](#2-user-experience-specification)
3. [Codebase facts the implementation depends on](#3-codebase-facts)
4. [Architecture](#4-architecture)
5. [Coordinate-system contract (read twice)](#5-coordinate-system-contract)
6. [Layout data model](#6-layout-data-model)
7. [Complete element catalogue](#7-complete-element-catalogue)
8. [New module `hud_layout.c/h` — full API](#8-new-module-hud_layoutch)
9. [hud.c integration — per-element conversion spec](#9-hudc-integration)
10. [Editor UI specification](#10-editor-ui-specification)
11. [Input handling changes](#11-input-handling-changes)
12. [Mode transitions and buttons (open / apply / resume / revert)](#12-mode-transitions-and-buttons)
13. [Persistence format](#13-persistence-format)
14. [Edge cases and pitfall checklist](#14-edge-cases-and-pitfall-checklist)
15. [Implementation order with definition-of-done](#15-implementation-order)
16. [Test plan](#16-test-plan)
17. [Risks and mitigations](#17-risks-and-mitigations)
18. [Future extensions](#18-future-extensions)
19. [Appendix A — element enum & ids](#appendix-a)
20. [Appendix B — complete file-change manifest](#appendix-b)

---

## 1. Goals and non-goals

### 1.1 Goals

* **G1 — Move every regular HUD element.** A single on-screen drag repositions it. Elements and their
  full extent are listed in §7; "every element" means all 17 movable groups there, not the
  gameplay-locked or world-anchored overlays (those are enumerated and explicitly excluded with reasons).
* **G2 — WYSIWYG.** The editor is an *overlay on the real in-game HUD*: elements are drawn by the exact
  same code path the player sees. No duplicated rendering, no drift between editor and game.
* **G3 — Live preview of context elements.** Chat, killfeed, health/ammo, palette, scoreboard, stats
  etc. can be force-shown with sample data so they can be edited without a live game situation.
* **G4 — Resolution independence.** A layout made at 1080p must look the same at 720p/4K/windowed.
  Offsets are stored as fractions of the window, tied to a user-chosen screen anchor (9-point).
* **G5 — Safe persistence.** Layout saved to its own `hud_layout.json` (never into `config.ini` /
  `settings` struct), with schema versioning, corrupt-file recovery and one-click reset to default.
* **G6 — Buttons & flow as requested.** A button in the pause (ESC) menu opens the editor;
  **Apply** saves while staying in the editor; **Apply & Resume** saves and returns to gameplay;
  the editor can be re-opened at any time; unsaved changes are never lost silently.
* **G7 — Zero regression at default.** With no `hud_layout.json` (or after Reset), rendering must be
  **pixel-identical** to the pre-feature build. This is verified by screenshot diff (§16).
* **G8 — Touch support.** Select/drag by finger; the tool panel is usable via the existing
  finger→mouse emulation; nudge buttons give touch precision.
* **G9 — No server impact.** Purely client-side rendering + local file IO. No packets, no protocol changes.

### 1.2 Non-goals (v1)

* N1 — No per-element *coloring* or font changes (future, §18).
* N2 — No import/export of layout presets as shareable files (future, §18; the JSON schema already supports it).
* N3 — No layout *profiles* (per-weapon etc.).
* N4 — No editing of touch-control plates (joystick/buttons), demo-player overlay, iron-sight zoom
  overlay, map-loading screen or the big map (B) — all enumerated in §7 as locked/excluded with reasons.
* N5 — No undo history beyond **Revert session** / **Reset element** / **Reset all** (future, §18).
* N6 — No editing while the demo replayer overlay is up (the editor may be open, the demo bar is just
  not editable and hidden while editing; §14.24).

---

## 2. User experience specification

### 2.1 Opening the editor

| Entry point | Where | Behavior |
|---|---|---|
| **Pause-menu button** | ESC menu sidebar (`hud_common_sidebar`), between "Skins" and "Macros": **`HUD Editor`** | Switches the active HUD to `hud_ingame` with the editor overlay active. Works connected (live world behind the HUD) and disconnected (menu background behind a sample HUD). |
| **Menu nav bar** | `hud_common_nav` (legacy top nav, same position ordering) | Same. |
| **Rebindable hotkey** | `WINDOW_KEY_HUD_EDITOR`, default **F10**, category *"Game"*, display *"HUD editor"*, registered in `config_register_key` (both SDL and GLFW blocks) | Toggles the editor over live gameplay. If unsaved changes exist, closing goes through the save popup (§12.4). |
| **Mobile** | On-screen **Menu** button → pause menu → `HUD Editor` | Same as pause-menu button; touch interaction per §11.4. |

The pause-menu button must be added to **both** `hud_common_sidebar` and `hud_common_nav` (they are
independent code paths — see §14.2), and it must be visible whether or not the client is connected,
because editing from the main menu is an explicit use case (sample-data preview).

### 2.2 The editor screen

When active, the game view keeps rendering (world if connected, menu background if not), all HUD
elements draw at their configured positions with preview data where enabled, and on top of that:

* **Selection chrome** — hovered elements get a thin accent outline; the selected element gets a bright
  outline, 4 corner handles, a name tag, and a live position readout (pixels and fractions).
* **Tool panel** — a draggable microui window (right side by default), detailed in §10.
* **Optional aids** — grid overlay, center/cross guides, safe-area rectangle (toggled in the panel).

The mouse cursor is freed (`window_mousemode(WINDOW_CURSOR_ENABLED)`); game input (look, shoot, tools,
chat) is fully suppressed while editing (§11). The world itself is **not paused** (the game has no
pause concept) — the player is briefly invulnerable to their own inputs, not to the server; this is the
same situation as having the ESC menu open today, and editing from the main menu avoids it entirely.

### 2.3 Panel buttons (exact behavior)

| Button | Behavior |
|---|---|
| **Apply** | Writes `hud_layout.json`. Stays in the editor. Status line shows `Saved ✓`. Clears the unsaved flag. |
| **Apply & Resume** | Applies (as above) then closes the editor: cursor re-captured, input restored, straight back to gameplay (or main menu if disconnected). |
| **Revert** | Restores the layout snapshot taken when the editor was opened (last *saved* state). Live view updates immediately. Does **not** write the file. Clears unsaved flag. |
| **Reset All** | microui popup confirm → every element back to natural position/visibility/scale; unsaved flag set; file **not** written until Apply. |
| **Reset Element** | In the selected-element section; resets only the selected element. |

*Unsaved-changes protection:* if the live layout differs from the saved snapshot and the editor is
closed via ESC / hotkey / Resume-without-apply, a microui popup appears:
**"Save HUD changes?"** `[Save & Resume]` `[Discard]` `[Cancel]` (§12.4).

### 2.4 Status and feedback

* A one-line status label at the panel bottom: `Saved ✓ HH:MM:SS`, `Unsaved changes`, `Saved to hud_layout.json`,
  `Load failed — defaults (see log)`, `Layout reset`.
* While dragging: a floating readout near the element: `x −24 px  (−3.0% w)   y +18 px  (+3.0% h)`
  relative to the element's natural position, plus the anchor name.

### 2.5 The "shape" of a session (acceptance walkthrough)

1. Player in game presses **ESC** → pause menu → clicks **HUD Editor**.
2. Cursor appears; HUD is shown with all preview groups enabled by default (chat, killfeed, health,
   ammo, minimap, palette, GMI counter, FPS box). Scoreboard preview is off by default (toggle in panel).
3. Player drags the killfeed from top-left to top-right; drags chat up a bit; hides the technical stats
   checkbox; selects minimap and sets scale 1.25 in the panel.
4. Clicks **Apply** → status `Saved ✓`. Clicks **Apply & Resume** → back in the game, HUD in the new
   arrangement, cursor locked again.
5. Next session: layout loads from `hud_layout.json` automatically at startup. ESC → HUD Editor shows
   the same arrangement; **Reset All** → **Apply** returns everything to vanilla.

---

## 3. Codebase facts

Everything below was verified against the current tree. These are the facts the design leans on;
**do not re-derive them during implementation — trust this section** (and re-verify only the line numbers).

### 3.1 Rendering pipeline & coordinate spaces

| Fact | Reference |
|---|---|
| HUD 2D projection is `matrix_ortho(0, W, 0, H)` → **GL space: origin bottom-left, y grows upward**. | `main.c` ~1681 (`matrix_ortho`), `settings.window_width/height` are the space. |
| Scale factors: `scalex = W/800`, `scalef = H/600`. | `main.c` ~1686-1687. |
| `texture_draw*` / `texture_draw_empty(x, y, w, h)`: vertices span `y-h .. y` → **the `y` argument is the TOP edge** in GL space; `x` is the left edge. | `texture.c` `texture_draw_empty()` (vertex array `{x,y, x,y-h, x+w,y-h, …}`). |
| `font_render(x, y, h, text)`: glyph quads are emitted at GL `y − q.y0 … y − q.y1` where `q.y0 ≈ 0.15·h, q.y1 ≈ 0.95·h` → **the visual text box hangs BELOW the passed y**: it occupies roughly `[y − h, y]` (GL), with `x` the left edge. The passed `y` behaves like the top edge of the text box (the glyphs start a fraction of `h` below it). Cross-checked against: the chat input hit-test band (`chat_input_offset_at` accepts clicks from `y − ROW − 4` to `y + 4`), the team-score text drawn at `H−27` inside the bar `[H−48, H−24]`, the minimap sector letter, and the spectator name at the screen top. **Treat font boxes exactly like texture boxes: `(x, y, len, h)` with `y` = top edge.** | `font.c` `font_render()` (vertex `-q.y1 + y`), `stbtt_GetPackedQuad` baseline semantics (cursor starts at `y2 = h*0.75`). |
| microui runs in its own **y-down** space; `main.c` flips every mu rect/text with `settings.window_height - y` when emitting GL commands. mu textboxes/menus are unaffected by HUD coordinates. | `main.c` ~1704-1745 (mu command flush). |
| The single mu flush point renders **after** `hud_active->render_2D` returns → microui content always draws on top of custom GL HUD in the same frame. | `main.c` ~1689-1750. |
| `hud_ingame.ctx` is currently `NULL`; only menu HUDs get a `mu_Context` (allocated in `hud_init`). `render_2D` receives that (possibly NULL) ctx and main.c skips mu entirely when NULL. | `hud.c` `hud_init()` ~175, `struct hud hud_ingame` ~4104 (last field `NULL`). |
| FPS-box, scoreboard, chat etc. are all inside the giant `hud_ingame_render()` (static), split by guard conditions on `camera_mode`, `screen_current`, `chat_input_mode`, settings flags. | `hud.c` ~1439-2786. |

### 3.2 Input flow

| Fact | Reference |
|---|---|
| Mouse coordinates arrive **y-down from top-left** (SDL/OS convention). | `window.c` (SDL mouse motion → `mouse(hud_window, x, y)`). |
| `mouse_click()` calls `hud_active->input_mouseclick` **first**, then feeds `mu_input_mousedown/up`. Editor element-picking therefore runs *before* microui sees the click, and must itself ignore clicks that land on the tool panel (§11.2). | `main.c` `mouse_click()` ~2072. |
| In-game the cursor is captured (`WINDOW_CURSOR_DISABLED`, relative mode) in `hud_ingame_init`; menus enable it. | `hud.c` ~540, `window.c` `window_mousemode()`. |
| Keyboard events reach `hud_ingame_keyboard(key, action, mods, internal)`; ESC toggles the pause menu via `show_exit ^= 1; hud_change(&hud_settings);` | `hud.c` ~3141, ESC at ~3524. |
| `keys()` drops all key presses except NO/YES/ESCAPE while `show_exit` is set. | `main.c` `keys()` ~1949. |
| Key bindings: `config_register_key(internal, def, name, toggle, display, category)`; the `WINDOW_KEY_*` enum ends with `WINDOW_KEY_A`, `WINDOW_KEY_X`, `WINDOW_KEY_COUNT` — **new keys must be inserted before `WINDOW_KEY_COUNT`** (comment there warns about the old fixed size of 64). | `window.h` ~110-125, `config.c` `config_reload()` ~736. |
| Touch: `hud_ingame_touch()` plus special-cased routing in `window.c` (`aim_finger`, joystick zone, `window_aim_zone()`). Menu HUDs get finger→mouse emulation for microui. Several branches test `hud_active == &hud_ingame` — all need an `&& !hud_edit_active()` guard. | `window.c` ~1140-1240, `hud.c` ~3771. |
| **Palette geometry is mirrored between `hud.c` (`palette_left/top/size/bottom`) and `window.c` (`window_aim_zone`)** — a documented sync hazard. | `hud.c` ~113-160 (comment: "window.c mirrors these fractions"), `window.c` ~900. |
| Chat text hit-testing (click-to-place-cursor) converts mouse y with `settings.window_height - y` — proof of the y-down→GL conversion pattern to reuse. | `hud.c` `chat_input_offset_at()` ~495. |

### 3.3 Menus / screens

| Fact | Reference |
|---|---|
| Screens are `struct hud` vtables (`init`, `render_3D`, `render_2D`, `input_keyboard`, `input_mouselocation`, `input_mouseclick`, `input_mousescroll`, `input_touch`, `ui_images`, `render_world`, `render_localplayer`, `ctx`); switching via `hud_change()` (re-inits the mu ctx, calls `init`). | `hud.h` ~32-47, `hud.c` `hud_change()` ~339. |
| Pause menu = `hud_settings` screen with `hud_common_sidebar()` (Settings/Controls/Skins/Macros/Recording/Replay/Chat Log/Disconnect) + full-screen microui window. | `hud.c` `hud_common_sidebar()` ~4442, ESC handler ~3524. |
| Legacy top nav: `hud_common_nav()` — independent button list (kept in sync manually). | `hud.c` ~4507. |
| A good template for a full-screen microui tool screen (search box, category list, widgets): `hud_settings_render()`. A template for textbox use: `hud_textbox()` wrapper (inherits IME handling via `hud_ime_update`). | `hud.c` ~5252, ~300. |
| `hud_common_render()` draws the menu background (`texture_ui_bg`) when disconnected, or a translucent tint when connected. | `hud.c` ~976. |

### 3.4 Config & persistence

| Fact | Reference |
|---|---|
| `config.ini` is written via `file_open("config.ini","w")` + `file_printf` (there is **no** `file_save`; the generic writer pair is `file_open`/`file_printf`/`file_close`). | `config.c` ~420, `file.c` `file_open()` ~236. |
| `config_sets/seti/setf` upsert rows in a `[section]` INI list; `name`/`value` are capped at 32 chars. | `config.c` ~259. |
| The settings UI is generated from the `config_settings` registry into `settings`/`settings_tmp`; the HUD editor must **not** route its data through this machinery (§4, decision D4). | `config.h` ~90-125. |
| JSON: **parson is already vendored and used** (`hud.c` news parsing). parson's own `json_serialize_to_file` uses raw `fopen` and would bypass the project's writable-dir logic → **always serialize to string and write via `file_open/file_printf`**, and load via `file_load()`. | `hud.c` ~43, ~4906; `file.h` `file_load()` ~32. |
| Build: sources appended in `src/CMakeLists.txt` (`list(APPEND CLIENT_SOURCES …)` block ~175-209). | — |

### 3.5 Miscellaneous load-bearing facts

* Killfeed in this fork draws **top-left** (`x=16`, `y = H−22 − 10k − 8k`), not top-right
  (`hud_render_message`, channel 1). The editor making it movable is a user-visible improvement; the
  plan's default coordinates describe reality, not classic AoS.
* Chat messages fade after 10 s (`window_time() - chat_timer[...] < 10.0F` guards); the editor's
  preview refreshes timers of seeded lines (§10.6).
* `chat_messages` is a global set to 12 (or a computed value while typing) **inside the
  `settings.chat_shadow != 0` branch** — a pre-existing quirk; the editor must not change it.
* F-keys are heavily allocated: F1-F4 cameras, F5 screenshot, F6 hide-HUD, F7 recording, F8 replay-save,
  F9 save-map, F11 fullscreen, F12 net stats. **F10 is the only free F-key** → default editor hotkey.
* `WINDOW_KEY_HIDEHUD` (F6) makes `hud_ingame_render` return early — the editor must override this
  while active (§14.4).
* On iOS the palette's bottom offset (0.045·H) exists to clear the home-indicator edge-protection
  strip; bottom-anchored drags need the same consideration (§14.17).
* HUD textures/modulate state: `hud_ingame_render` force-sets `glTexEnvi(..., GL_MODULATE)` at entry
  because world rendering leaves GL_TEXTURE_ENV dirty. Editor chrome drawing must not disturb GL state
  (use the existing helpers `texture_draw_empty`, `glx_draw_line_2d`, and restore `glLineWidth(1)` /
  `glColor3f(1,1,1)` afterwards — same discipline as the existing code).
* `glx_draw_line_2d` and `glx_draw_ring_segment_2d` exist and are already used for 2D HUD drawing
  (chat bullets, ammo ring) — safe to reuse for chrome.

---

## 4. Architecture

### 4.1 Decision D1 — Overlay editor inside `hud_ingame` (not a separate `struct hud` screen)

**Chosen:** the editor is a mode *of the in-game HUD* (`hud_edit_active` flag consulted inside
`hud_ingame_render` and its input handlers). The alternative — a new `hud_hudedit` screen — was rejected
because `hud_ingame_render` is the only place the elements exist; a separate screen would have to
re-implement every element to display it (guaranteed drift, exactly the bug class this plan must avoid).

Consequences (all handled in this plan):
* Entry from a menu screen performs `hud_change(&hud_ingame)` **plus** activating the editor flag
  (§12). No new screen vtable is registered; `hud_init()` gains one `malloc` for `hud_ingame.ctx`.
* `main.c` gates the microui path: the ingame mu ctx is only "live" while editing, so the normal game
  path stays bit-identical (§11.1).
* World rendering while disconnected-from-menu must be suppressed (`render_3D` with no map) — one
  guard in `main.c` (§12.3).

### 4.2 Decision D2 — Layout indirection layer, elements keep their natural math

Each element's draw block keeps computing its coordinates exactly as today into a local *base point*
`(bx, by)` (GL space). One call

```c
hud_layout_origin(HUD_EL_X, &bx, &by);   /* no-op unless the element is in custom mode */
```

then **replaces the base point**, and every coordinate inside the block that referenced the old inline
expression (e.g. `settings.window_width - 143*scalef`) references the base variable instead. With the
default layout (nothing custom) `hud_layout_origin` does nothing → **pixel-identical rendering (G7)**.
Custom mode overwrites `(bx, by)` with `anchor_point + offset` (§6) → resolution-independent (G4).

This is the least invasive transformation that satisfies G1+G4+G7 simultaneously, and it localizes the
diff: per element it is "introduce base point, thread 1-3 variables through the block".

### 4.3 Decision D3 — Own persistence file `hud_layout.json` (not `config.ini`)

Rejected: ~17 elements × 4 fields ≈ 70 new `config_seti/config_setf` lines plus 70 read cases in
`config_read_key`, with a 32-char value cap and no schema versioning. Chosen: one parson JSON file,
serialized via `file_open`-based writer (§13), loaded in `hud_layout_init()` at startup, written only
by explicit Apply. It also keeps the editor orthogonal to the `settings`/`settings_tmp` apply flow of
the settings screen — zero interaction, zero surprises.

### 4.4 Decision D4 — Editor code lives in `hud.c`; geometry/persistence lives in new `hud_layout.c`

The repo's convention is feature-in-`hud.c` (skins, macros, chatlog, recording screens are all there),
and the editor needs intimate access to element internals. A separate `hud_editor.c` would need dozens
of new exports from `hud.c`. So:
* **`hud_layout.c/h`** (new): element table, geometry math, anchors, snapping helpers, bounds
  registry, JSON load/save. Self-contained; also included by `window.c` (palette sync, §11.4).
* **`hud.c`** (edited): per-element conversion (§9), editor state machine, chrome rendering, tool
  panel, preview system, input interception — all inside a clearly marked
  `/* ════════ HUD EDITOR ════════ */` section at the end of the ingame area, plus small guard edits
  at the element sites.
* hud.c is ~6300 lines; the editor adds ~900-1100. Acceptable and consistent with the project's style;
  a split can happen later without data-model changes.

### 4.5 Decision D5 — Per-element visibility, offset; selective scale

Every movable element supports **move** and **show/hide**. **Scale** is enabled only for elements whose
size math is trivially local (uniform px constants): health group, ammo group, minimap, GMI counter,
player-stats, tech-stats, FPS box (§7 marks which). Everything else is position+visibility only —
scaling chat or the scoreboard changes wrapping/row math and is deliberately out of v1.

### 4.6 Module map (resulting)

```
hud_layout.h/.c   NEW    element enum+metadata, layout state, origin/scale/visibility API,
                         bounds reporting, snapping, JSON load/save (via file_open writer)
hud.c             EDIT   element conversions (§9), editor state+UI (§10), input hooks (§11),
                         entry buttons, preview system, hud_init ctx alloc
hud.h             EDIT   extern int hud_edit_active; + hud_editing_active() decl (used by window.c/main.c)
window.h          EDIT   WINDOW_KEY_HUD_EDITOR enum entry
window.c          EDIT   touch routing guards, aim-zone skip, palette geometry via hud_layout
main.c            EDIT   mu-ctx gating, render_3D gating, nothing else
config.c          EDIT   register F10 key binding (SDL+GLFW)
CMakeLists.txt    EDIT   + hud_layout.c
```

---

## 5. Coordinate-system contract

There are **three** coordinate spaces in play. Every bug this feature could have is one of these five
rules being broken; the implementer should paste this section into the PR description.

```
┌────────────────────────────────────────────────────────────────────────┐
│ SPACE A — OS / SDL / microui   origin TOP-LEFT, y DOWN                  │
│   • mouse events (x, y from window.c)                                   │
│   • all microui windows/panels/widgets (tool panel lives here)          │
│                                                                         │
│ SPACE B — GL HUD               origin BOTTOM-LEFT, y UP                 │
│   • every element draw call (texture_draw*, font_render, glx_draw_*)    │
│   • texture_draw(x, y, w, h) → rect = [x, x+w] × [y−h, y]  (y = TOP)    │
│   • font_render(x, y, h, t) → text box ≈ [x, x+len] × [y−h, y]          │
│     (same top-edge convention — glyphs hang just below the passed y)    │
│   • all layout state (offsets, bounds) is stored in SPACE B             │
│                                                                         │
│ SPACE C — layout storage       fractions of window, SPACE B orientation │
│   • ox ∈ [−1..+1] fraction of W, oy fraction of H, measured from the    │
│     chosen 9-point anchor; converted to px at draw time                 │
└────────────────────────────────────────────────────────────────────────┘
```

**Rules (exhaustive):**

1. Mouse → GL: `gl_y = settings.window_height − my`. Used by every editor hit test.
2. Mouse drag delta → GL delta: `d_gl = (dx, −dy)`. A downward mouse drag *lowers* GL y.
3. GL → mouse (only needed when positioning microui popups near elements, not in v1): `my = H − gl_y`.
4. microui coordinates are Space A natively — the tool panel uses them as-is; never convert inside
   panel code, and never store element geometry from panel code without conversion.
5. Element bounds reported via `hud_layout_report_bounds` are **Space B**, with the texture convention
   `y = top edge` (§3.1). Selection outlines draw exactly these rects (it is legal for them to be
   1-2 px off for text-only elements; see padding note in §8.5).

Sanity check for the implementer (already proven in existing code): the chat click-to-position code
converts with `settings.window_height - y_pixel` (`chat_input_offset_at`), and the team score boxes at
the top of the screen are drawn at `y = settings.window_height − 24`. Both confirm Space B orientation.
Numeric proof of the top-edge convention (score bar, 800×600): the bar is
`texture_draw_empty(x, H−24, w, 24)` → spans GL `[H−48, H−24]`; its label is
`font_render(x+10, H−27, 16, …)` → glyph box `[H−43, H−27]` — inside the bar, as seen in game.

---

## 6. Layout data model

### 6.1 Stored state (in `hud_layout.c`, one record per element)

```c
typedef enum { HUD_ANCHOR_TL, HUD_ANCHOR_TC, HUD_ANCHOR_TR,
               HUD_ANCHOR_ML, HUD_ANCHOR_MC, HUD_ANCHOR_MR,
               HUD_ANCHOR_BL, HUD_ANCHOR_BC, HUD_ANCHOR_BR } hud_anchor;

typedef struct {
    /* runtime */
    unsigned char mode;      /* 0 = natural (factory), 1 = custom            */
    unsigned char anchor;    /* hud_anchor, only meaningful in custom mode   */
    float         ox, oy;    /* offset from anchor, fraction of W / of H     */
    float         scale;     /* 1.0 = natural; only used if el supports it   */
    unsigned char visible;   /* 0/1                                          */
    /* bookkeeping */
    float         bounds[4]; /* last reported [x, y, w, h], Space B          */
    unsigned char has_bounds;
} hud_layout_el;
```

* **Natural mode** is the factory state: `hud_layout_origin()` is a no-op, `scale()` returns 1,
  `visible()` returns 1 → code behaves exactly like today (G7).
* **Custom mode** is entered the first time the user moves an element (or changes its anchor/scale in
  the panel). At that moment the element's *current natural base point* `(nx, ny)` is needed once:

### 6.2 Anchor bake (the one subtle piece — implement exactly as written)

When the user starts dragging element `e` whose current base point is `(nx, ny)` (GL px) and the drag
has produced live position `(px, py)`:

```
a   = hud_layout_default_anchor(e)          /* table §7 — the element's natural corner    */
apx = (a col==LEFT?0 : a col==CENTER?W/2 : W) * 1.0
apy = (a row==TOP ? H : a row==CENTER?H/2 : 0)     /* anchor point in Space B            */
ox  = (px - apx) / W        oy = (py - apy) / H
mode = custom; anchor = a; store ox, oy
```

From then on, every frame:

```
apx/apy from CURRENT window size (so it follows resizes)
bx = apx + ox * W        by = apy + oy * H
```

If the user changes the anchor in the panel to `a'`: recompute `ox,oy` from the *current on-screen
position* and the new anchor point (position must not jump):

```
ox' = (bx_now − apx(a')) / W        oy' = (by_now − apy(a')) / H
```

Resetting an element sets `mode = natural` — it snaps back to the code constants (and this is why
element blocks must keep computing their natural `(bx, by)` even in custom mode; the variables are
just overwritten afterwards).

**Y orientation of anchors:** `TR` = top-right *visually*. In Space B the top edge is `y = H`, hence
`apy = H` for TOP-row anchors and `apy = 0` for BOTTOM-row anchors. Double-check this when writing
`hud_layout_anchor_point()` — it is the single easiest place to introduce an inverted-Y bug.

### 6.3 Units, precision, clamping

* Fractions stored as `float`; JSON stores 6 decimal places (`%.6f` ≈ 0.001 px at 1000 px window — far
  below visual threshold).
* Clamp stored values to `ox, oy ∈ [−1.25, +1.25]` (an element may be parked fully off-screen on
  purpose, but not "lost at infinity"). Visibility checkbox is always available as recovery, plus
  Reset Element.
* Bottom-edge note (iOS): clamping must not force elements into the bottom 4.5 % home-indicator strip;
  do not add such a clamp programmatically (users may want it there on desktop) — instead the **Reset
  Element** button is the recovery path. Documented, deliberate.

### 6.4 Scale

`hud_layout_scale(el)` returns 1.0 unless the element is scale-capable and in custom mode. Element
blocks multiply their *size* constants by `s = hud_layout_scale(...)`; the base point stays the
anchor-adjusted top-left. For right-anchored scaled elements the base point must be computed as
`bx = apx + ox*W − natural_width*s` (i.e. keep the anchor edge pinned) — this applies to AMMO, GMI,
TECHSTATS, FPSBOX, MINIMAP (all right-anchored). The catalogue in §7 states per element which point
the scale pins (top-left or top-right).

### 6.5 Visibility

`hud_layout_visible(HUD_EL_X)` is `&&`-combined into each element's existing guard (never `||`).
A hidden element still reports its nominal bounds in the editor (§8.5) so it can be re-selected.

---

## 7. Complete element catalogue

This is the authoritative list. Coordinates are the **current natural geometry** (Space B, taken from
the code — `W`=`settings.window_width`, `H`=`settings.window_height`, `sx`=`scalex`, `sf`=`scalef`).
All listed `y` values are the values **passed to the draw calls**, i.e. top edges per the §5
convention (texture quads span `[y−h, y]`; font glyph boxes hang just below the passed `y`).
"Guard today" is the condition that currently makes the element visible; the conversion adds
`&& hud_layout_visible(el)` to it, and (where listed) a preview bypass.

### 7.1 Movable elements (17)

| # | Enum id | UI name (group) | Contents (one dragging unit) | Natural base point (GL) | Scalable | Visible today when | Preview strategy |
|---|---------|-----------------|------------------------------|--------------------------|----------|--------------------|------------------|
| 1 | `HUD_EL_HEALTH` | Health | heart icon + number + optional segmented bar | icon `(8, health_top)` `health_top = 60 (bar on) / 40 (off)`; number `(48, health_top−2)` h30; bar `(8, 22)` 160×12 (+2 border) | ✔ (pin top-left) | inside FPS/body-view branch; `is_local` health | `pv_weapons` bypasses the camera branch guard; fake health 74 |
| 2 | `HUD_EL_AMMO` | Ammo / held item | item icon + counter | icon `(W−tex_health.width−8, item->height+8)` 36×32; counter right-aligned to `W−36−12`, `y=37` h30 | ✔ (pin top-right) | same branch as health | `pv_weapons`; fake `24/90` rifle, or `50` blocks |
| 3 | `HUD_EL_PALETTE` | Block color palette | 8×8 swatch grid + selection border | `palette_left()=(W−size)/2`, `palette_bottom()=0.045H`, `cell=0.024H` | — | held item == TOOL_BLOCK (draw + border); also spectator fog-palette variant | `pv_palette` bypasses held-item guard |
| 4 | `HUD_EL_CHAT` | Chat | log lines + bullet bars + shadow panel + input row + selection highlight | messages `x=16` (bullet `x−11`), newest `y=75+(chat_messages−k+1)*(16+chat_spacing)−chat_spacing/2`; input `x=11`, baseline `69` (+rows); shadow panel `(3, 76+(msgs+1)*(16+spacing), maxw+16, rows*(16+spacing))` | — | `camera_mode != SELECTION`; per-message 10 s fade (lifted while typing) | seeded sample lines + per-frame timer refresh (§10.6) |
| 5 | `HUD_EL_KILLFEED` | Killfeed | feed lines + bullet bars | `x=16`, `y = H−22 − k*(10+8)` (top-left in this fork!) | — | same loop as chat, hidden while chat input open | seeded 3 sample kill lines |
| 6 | `HUD_EL_MINIMAP` | Minimap (small) | backdrop + scissored map + sector letter + tent/intel/territory/player icons | backdrop `(W−144*sf, 586*sf)` 130×130; content box `(W−143*sf, top=585*sf)` 128×128; sector text `(W−77*sf, 454*sf)` h30 | ✔ (pin top-right) | `camera_mode != SELECTION` | always shown while editing |
| 7 | `HUD_EL_SCORES_TOP` | Team scores (top) | two colored score boxes + text | team1 `(W/2−75, H−24, len+20, 24)`, team2 `(W/2, H−24, …)` | — | `settings.show_live_player_count && connected && logged_in` | `pv_scores` bypasses connection guard, fake `2-3` / `1-3` |
| 8 | `HUD_EL_GMI` | Player counter (right) | two helmet boxes + alive counts | boxes `x=W−8−32` w32; team1 `gmi_y=54`, team2 `gmi_y=94`; count text `(W−8−62, gmi_y+28)` h30 | ✔ (pin top-right) | `settings.show_live_player_count`, inside FPS/body-view branch | `pv_gmi` + camera bypass, fake 7 / 6 |
| 9 | `HUD_EL_SCOREBOARD` | Scoreboard (TAB) | ping text + 2 team panels + spec panel + all player rows + intel icons | ping `(W/2, H−4)`; panels `x∈{W/2−300, W/2, W/2−150}`, header top `450*sf`, w300, row pitch 24; spec panel `y_offset = height+32` | — | `(no chat input && TAB held) || camera==SELECTION` | `pv_scoreboard` (fake roster built in code) |
| 10 | `HUD_EL_FPSBOX` | FPS + ping box | backdrop + accent line + 2 texts | box `(W−105, H/2+84)` 100×36; line `x=W−5` from `H/2+66` to `H/2+102`; ping text `y=H/2+64`, fps `y=H/2+48`, right-aligned to `W−17` | ✔ (pin top-right) | `settings.show_fps` | shown while editing regardless of setting (editor-only) |
| 11 | `HUD_EL_STATS` | Player stats | 6 outlined rows | `(8, H/2−60)`, row h16 | ✔ (pin top-left) | `settings.player_stats && connected && logged_in && !spec` | `pv_stats` bypasses connection guard (real counters, mostly 0) |
| 12 | `HUD_EL_TECHSTATS` | Tech stats | 5 outlined rows, right-aligned | right edge `W−8`, `y=H/2−44`, row h16 | ✔ (pin top-right) | `settings.player_technical_stats && connected && …` | `pv_stats` |
| 13 | `HUD_EL_SPECTATE` | Spectator labels | "Spectating X" name labels (body-view + hover), "Click to switch players", "INSERT COIN:n" | name `(W/2−len/2, 26)` h22 outlined; switch hint `(W/2, H)` h16; coin `(W/2, 53*sf*(1|2))` h53*sf | — | respective spectator camera states | editable but **not** force-previewed (context-dependent; user can enter spectator to see it) |
| 14 | `HUD_EL_TARGETINFO` | Target info | "name's torso" aim label | `(W/2, 0.2H)` h16 centered | — | player under crosshair (team rule) | not force-previewed (needs world) |
| 15 | `HUD_EL_YCLAMP` | Y-Clamp indicator | text | `(8, H/2−4)` h16 | — | `cameracontroller_yclamp` | toggleable in spectator; not force-previewed |
| 16 | `HUD_EL_CENTERMSG` | Center notices | chat popup (`RELOAD` etc.) + block-drag counter | popup `(W/2, H/2)` h32; drag counter `(W/2−len/2, 50)` h32 | — | transient timers | not force-previewed (transient) |
| 17 | `HUD_EL_TCBAR` | TC capture bar | two-tone progress bar | `((W−440*sf)/2, 0.25H)` 440*sf × 20*sf | — | TC mode, capturing near territory | not force-previewed |

Default anchors (for §6.2 bake) — HEALTH `BL`, AMMO `BR`, PALETTE `BC`, CHAT `BL`, KILLFEED `TL`,
MINIMAP `TR`, SCORES_TOP `TC`, GMI `BR`, SCOREBOARD `MC`, FPSBOX `MR`, STATS `ML`, TECHSTATS `MR`,
SPECTATE `TC`, TARGETINFO `MC`, YCLAMP `ML`, CENTERMSG `MC`, TCBAR `MC`.

### 7.2 Locked elements (drawn, selectable in the list, but **not movable** — with reasons)

| Element | Why locked |
|---|---|
| Crosshair (`hud_draw_target_at` at `W/2, H/2`, incl. free-aim variant) | Bullets land at screen center; an offset crosshair would lie about aim. |
| Ammo ring (`settings.ammo_crosshair`, radius `20.25*sf` around center) | Tied to crosshair by design. |
| Damage direction indicator (rotated around `W/2,H/2`, 200×200) | Its meaning *is* "direction relative to screen center". |
| Big map (hold B) incl. grid labels | Full-screen centered reference overlay; moving it has no use case; also world-scaled. |
| Team/gun selection prompts ("Press 1 to join …") | Position mirrors the two spawn zones on screen (`W/4, W/2, 3W/4`) — layout is semantic. |
| Network stats graph (F12) | Debug tool with its own fixed panel. |
| Demo playback overlay | Demo-player control surface, excluded (N6). |

Locked elements appear in the panel list under a "Locked (gameplay)" group, greyed, with the reason as
help text; they are never hit-test targets.

### 7.3 Excluded (not listed in the editor at all)

Loading/map-transfer screen, iron-sight zoom overlay, on-screen touch controls (joystick, action
plates — their layout is a separate mobile-UX problem, §18), world-anchored overlays (damage numbers,
ESP, tracers, hit markers, spectator ESP), rain/snow, wallpapers. These are not HUD layout elements.

### 7.4 Grouping in the panel list

```
Gameplay   : Health, Ammo / held item, Block color palette
Chat       : Chat, Killfeed
Information: Minimap, Team scores (top), Player counter, Scoreboard (TAB),
             Target info, Y-Clamp indicator
Diagnostics: FPS + ping box, Player stats, Tech stats
Special    : Spectator labels, Center notices, TC capture bar
Locked     : (greyed, §7.2)
```

---

## 8. New module `hud_layout.c/h`

### 8.1 `hud_layout.h` (complete public surface — nothing else may be added without a plan update)

```c
#ifndef HUD_LAYOUT_H
#define HUD_LAYOUT_H

#include <stdbool.h>

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
    HUD_EL_COUNT   /* 17 — ids are append-only forever (JSON stability, §13.4) */
};

enum { HUD_ANCHOR_TL, HUD_ANCHOR_TC, HUD_ANCHOR_TR,
       HUD_ANCHOR_ML, HUD_ANCHOR_MC, HUD_ANCHOR_MR,
       HUD_ANCHOR_BL, HUD_ANCHOR_BC, HUD_ANCHOR_BR };

void  hud_layout_init(void);                 /* load hud_layout.json (or defaults)        */
bool  hud_layout_save(void);                 /* write file; returns success               */
void  hud_layout_reset_all(void);            /* every element -> natural                  */
void  hud_layout_reset_element(int el);
bool  hud_layout_is_custom(int el);
bool  hud_layout_is_dirty(void);             /* live state != saved snapshot              */
void  hud_layout_snapshot_take(void);        /* editor open: remember saved state         */
void  hud_layout_snapshot_restore(void);     /* Revert button                             */

bool  hud_layout_visible(int el);
float hud_layout_scale(int el);              /* 1.0 when natural/unsupported              */

/* Core primitive (§4.2): no-op in natural mode; overwrites with
   anchor_point + offset in custom mode. x/y are Space B (GL, y-up).      */
void  hud_layout_origin(int el, float* x, float* y);

/* Editor-side mutation (Space B px in, fractions stored). Both read
   settings.window_width/height internally, like hud_layout_origin.        */
void  hud_layout_set_px(int el, float x, float y);
void  hud_layout_set_anchor(int el, int anchor);                            /* jump-free */
void  hud_layout_set_visible(int el, bool vis);
void  hud_layout_set_scale(int el, float s);

/* Bounds registry for hit-testing + chrome (§8.5).                        */
void  hud_layout_report_bounds(int el, float x, float y, float w, float h);
bool  hud_layout_bounds(int el, float* x, float* y, float* w, float* h); /* false if never reported */
int   hud_layout_pick(float mx_gl, float my_gl);  /* topmost element at GL point or -1     */

/* Palette geometry — single source of truth shared with window.c (§11.4). */
void  hud_layout_palette_rect(float* left, float* top, float* size);

/* Snap helper: returns px snapped to current editor grid (0 = off).       */
float hud_layout_snap(float px, int grid_px);

#endif
```

### 8.2 Element metadata table (static, in `hud_layout.c`)

```c
static const struct {
    const char* json_key;   /* "health", "ammo", … — stable, see Appendix A */
    const char* name;       /* panel display name */
    unsigned char default_anchor;
    bool          scalable;
    float         nominal[4]; /* fallback hitbox (x, y, w, h) @800×600, Space B,
                               used until the element reports real bounds this frame */
} EL_META[HUD_EL_COUNT] = { /* filled per §7.1 — nominal rects listed in Appendix A */ };
```

### 8.3 Geometry math (normative)

```c
void hud_layout_origin(int el, float* x, float* y) {
    hud_layout_el* e = &state[el];
    if(e->mode != CUSTOM) return;                    /* G7 fast path */
    float ax, ay;
    hud_layout_anchor_point(e->anchor, &ax, &ay);    /* uses CURRENT settings.window_* */
    *x = ax + e->ox * settings.window_width;
    *y = ay + e->oy * settings.window_height;
}
```

`hud_layout_anchor_point` (Space B): `TL=(0,H)` `TC=(W/2,H)` `TR=(W,H)` `ML=(0,H/2)` `MC=(W/2,H/2)`
`MR=(W,H/2)` `BL=(0,0)` `BC=(W/2,0)` `BR=(W,0)`. **Note again: TOP row maps to `y = H`.**

`hud_layout_set_px(el, x, y)`:

```
if mode==NATURAL: mode=CUSTOM; anchor=EL_META[el].default_anchor;
compute (ax,ay) for anchor; ox=(x−ax)/W; oy=(y−ay)/H; clamp to ±1.25
```

`hud_layout_set_anchor(el, anchor)` recomputes `ox,oy` from the *current* on-screen base (§6.2,
jump-free). The editor obtains the current base as `bounds` top-left (reported this frame).

### 8.4 Snapping

`hud_layout_snap(px, grid)`: `grid<=1 → px`; else `floorf(px/grid + 0.5f)*grid`. Applied **only**
while dragging, only to the stored px before converting to fractions. Additionally, during drag, if
the proposed base point is within 6 px (GL) of an anchor guide — screen edges for the element's anchor
row/column, or the screen center lines for CENTER anchors — it is pulled flush and the corresponding
guide line is drawn bright cyan for that frame. Guides are computed in the editor chrome, not in
`hud_layout.c` (keeps the module UI-free).

### 8.5 Bounds registry & picking

* Element code calls `hud_layout_report_bounds(EL, x, y, w, h)` **once per frame it draws** (Space B,
  `y` = top edge). Bounds are consumed only by the editor; cost is 4 float stores + 1 int store.
  Each report also stamps a module-global monotonically increasing `draw_seq` counter into the
  element's record.
* `hud_layout_bounds` falls back to `EL_META.nominal` (scaled by `W/800, H/600`) when the element
  didn't draw this frame (hidden/context elements). This makes hidden elements selectable through the
  panel with a *ghost* outline (drawn 40 % alpha, dashes via 8 px on / 5 px off segments). Note: a
  hidden element's anchor/X/Y edits operate on its stale/nominal position until it is shown again —
  acceptable; Reset Element always recovers.
* `hud_layout_pick` iterates all elements, keeps those whose bounds contain the GL point (3 px
  tolerance inflation), and returns the one with the **highest `draw_seq`** — i.e. the element drawn
  *last* at that pixel, matching what the eye sees on overlaps. This is deterministic and needs no
  hand-maintained z-order list. Locked elements are never returned; elements with neither fresh
  bounds nor a drawn stamp this frame are skipped by picking (they are still selectable via the panel
  list).
* Text elements: bounds = `(x, y, font_length(h,text), h)` at the *passed* coordinates — with the
  top-edge convention this box spans `[y−h, y]` and always brackets the glyphs (§5). Where a block
  draws multiple lines (stats, chat, killfeed) report the **union**.

### 8.6 JSON IO (normative)

* Load (in `hud_layout_init`, called from `config_reload`'s caller chain — see §15 P0): read with
  `file_load("hud_layout.json")`; `json_parse_string`; walk `elements.<key>`; unknown keys →
  `log_warn` and ignore (forward compatibility); malformed/missing file → defaults, `log_info`,
  **never** auto-write the file (first Apply creates it).
* Save: build parson value from live state; `json_serialize_to_string_pretty`; write via
  `file_open("hud_layout.json","w")` + `file_printf` + `file_close`; **never**
  `json_serialize_to_file` (bypasses writable-dir logic, §3.4). On write failure: status line shows
  the error, file untouched.
* After save/load, update the snapshot used by dirty-tracking/Revert.

Schema (§13.2) stores `anchor`, `ox`, `oy`, `visible`, `scale` per element plus a top-level
`"version": 1`.

---

## 9. hud.c integration

All changes below are in `hud.c` unless stated. The order of work within §9 is exactly the phase order
of §15 (convert first, editor second) — never interleave an element conversion with editor work in one
commit; conversions are individually verifiable as no-ops.

### 9.0 Shared prelude (top of `hud_ingame_render`, right after the `glTexEnvi` fix)

```c
/* ═══ HUD EDITOR: per-frame layout deltas + preview flags ═══ */
hud_editor_frame_begin(ctx, scalex, scalef);
```

`hud_editor_frame_begin` does **exactly** this, in order:

1. `if(!hud_edit_active) return;` (no-op during normal play).
2. **Idempotency guard:** if a force-close condition holds, close without writing and return:
   `network_map_transfer`, `!network_connected && opened-while-connected` (server died),
   `demo_is_playing()`, or `chat_input_mode != CHAT_NO_INPUT` (cannot happen, belt-and-braces).
3. **Input neutralization:** `memset(window_pressed_keys, 0, sizeof(window_pressed_keys));`
   (`extern int window_pressed_keys[WINDOW_KEY_COUNT];` — window.h:157). **This is required**, not
   cosmetic: `keys()` in main.c sets `window_pressed_keys[key] = 1` *before* the hud handler runs,
   so a key held when the editor opened (or pressed while editing) would otherwise keep driving
   movement/jump/crouch via the `window_key_down()` polls every physics tick. Clearing per frame
   guarantees zero game reaction and zero stuck keys on close (also covers toggle-keys like F6).
4. Disconnected editing: draw the menu backdrop directly (`glColor3f(0.5,0.5,0.5)` +
   `texture_draw(&texture_ui_bg, 0, H, W, H)` — same as the disconnected path of
   `hud_common_render`, but **do not call `hud_common_render`**, to avoid its update-popup side
   effect).
5. Refresh preview chat/killfeed timers for the seeded index range (§10.6).
6. Re-clamp the panel window rect into the screen.

It must **not** draw any chrome yet.

The **early returns** in this function need guards:

| Early return | Change |
|---|---|
| `if(window_key_down(WINDOW_KEY_HIDEHUD)) return;` | `if(window_key_down(WINDOW_KEY_HIDEHUD) && !hud_edit_active) return;` |
| `if(network_map_transfer) { … }` branch | untouched, but `hud_editor_frame_begin` (or the keyboard hook) force-closes the editor if a map transfer starts (§14.14) |

The **tail** of the function gets:

```c
if(hud_edit_active) hud_editor_frame_end(ctx, scalex, scalef);
   /* selection chrome, guides, panel widgets (mu calls are fine here — they are
      queued as commands and flushed after render_2D returns, drawing on top) */
```

placed **before** the `#ifdef USE_TOUCH` control-plate block and before
`demo_playback_render_overlay(scalef)`; both of those get `&& !hud_edit_active` /
early-return-while-editing guards (§14.24/§14.25).

### 9.1 Conversion pattern (worked example 1 — KILLFEED, simple)

**Before** (inside `hud_render_message`, channel 1 branch):

```c
} else {
    x = 16.F;
    y = settings.window_height - 22.0F - 10.0F * k - k * 8.F;
}
```

**After:**

```c
} else {
    x = 16.F;
    y = settings.window_height - 22.0F - 10.0F * k - k * 8.F;
}
…
/* (deltas computed once per frame by the caller into file-statics — see below) */
x += kf_dx;  y += kf_dy;
```

Concretely: `hud_render_message` gains two `float` parameters `(dx, dy)` (call sites pass the
element deltas), or — less invasive — two file-static floats `kf_dx, kf_dy` set each frame right
before the message loop. **⚠ The base point MUST be initialized to the element's natural base before
calling `hud_layout_origin`** — in natural mode the call is a no-op that leaves the values untouched,
and the delta is the difference to the natural base. Getting this wrong shifts the element even with
default layout (the G7 killer):

```c
/* natural base of the feed is (16, H−22) — init to it, NOT to (0,0): */
float bx = 16.F, by = settings.window_height - 22.F;
hud_layout_origin(HUD_EL_KILLFEED, &bx, &by);
kf_dx = bx - 16.F;
kf_dy = by - (settings.window_height - 22.F);   /* both are 0.0F in natural mode */
```

and `hud_render_message` adds `(kf_dx, kf_dy)` to its computed `x, y`. The `glx_draw_line_2d` bullet
bar inside it uses `x−11` already relative to `x` → no further change. **Report bounds** once per
frame after the loop: union of drawn lines (or nominal when nothing drawn).

> **Rule for every delta-style conversion** (killfeed, chat, scoreboard): initialize the base
> variables to the element's *natural* base first; only then call `hud_layout_origin`. The only
> element whose natural base is genuinely `(0, 0)` is SCOREBOARD.

### 9.2 Conversion pattern (worked example 2 — HEALTH, with scale)

```c
int health = …;
float s = hud_layout_scale(HUD_EL_HEALTH);
float bx = 8.F, by = health_top;             /* natural base (top-left of icon)   */
hud_layout_origin(HUD_EL_HEALTH, &bx, &by);
hud_texture_draw(&texture_health, bx, by, 36.0F * s, 32.F * s);
hud_font_render(bx + 40.F * s, by - 2.0F * s, 30.F * s, hp, 1.F);
if(settings.healthbar)
    hud_healthbar_render_at(bx, by - (health_top - 22.0F) * s, s, health);
        /* hud_healthbar_render gains (x, y, s) params; every internal constant
           (bar_x=8→x, bar_top=22→y, 160/12/2/1, 20 partitions) multiplies by s.
           Keep its smoothing statics untouched. */
hud_layout_report_bounds(HUD_EL_HEALTH, bx - 2.F*s, by, 164.F*s, 52.F*s);
        /* union, top-edge convention:
           bar border  y = by−36s, h = 16s           → spans [by−52s, by−36s]
           icon        y = by,    h = 32s            → spans [by−32s, by]
           number box  y = by−2s,  h = 30s           → spans [by−32s, by−2s]
           x: bar border spans [bx−2s, bx+162s] (w = bar_w+4 = 164s)
           → union: x [bx−2s, bx+162s], y [by−52s, by] */
```

The guard above this block changes from

```c
if(camera_mode == CAMERAMODE_FPS || ((…BODYVIEW…||…SPECTATOR…) && cameracontroller_bodyview_mode)) {
```

to

```c
if(camera_mode == CAMERAMODE_FPS || ((…) && cameracontroller_bodyview_mode)
   || (hud_edit_active && pv_weapons)) {
```

**and nothing else in that big branch changes** — the iron-sight/crosshair sub-block keeps its own
conditions (crosshair will show during editing: intended, it is a locked element the user should see).
PALETTE (inside this branch, `held_item == TOOL_BLOCK`) and GMI (`settings.show_live_player_count`)
get their own `|| (hud_edit_active && pv_palette / pv_gmi)` guards.

### 9.3 Conversion directives per element (checklist)

For each row: element, block location, exact variables to introduce, hazards.

1. **AMMO** — same branch as HEALTH. Introduce `bx = W−tex_health.width−8, by = item_mini->height+8`
   (natural base, top edge of the icon), then `hud_layout_origin`. Icon w/h ×s. Counter (keeps its
   natural relationship to the icon: natural counter y is 37 while natural icon top is
   `item_mini->height+8`, i.e. `37 = by_nat − (item_mini->height − 29)`):
   `counter_x = bx − 4*s − font_length(30*s, str)` (4 = 12 − 8, preserves right-alignment to the
   icon) and `counter_y = by − (item_mini->height − 29)*s` (equals 37 with the stock 32-px icons).
   Pin top-right: when scaling, `bx` must be recomputed as `(ax + ox*W) − (natural_w*(s−1))` —
   implement once as helper `hud_layout_pin_right(el, natural_w)` returning adjusted bx (also used
   by GMI, TECHSTATS, FPSBOX, MINIMAP).
2. **PALETTE** — the four helpers (`palette_cell/size/left/top/bottom`) move to `hud_layout.c` as
   `hud_layout_palette_*` and gain the layout offset: `left/top` add the element delta (computed from
   `hud_layout_origin(HUD_EL_PALETTE, …)` against the natural rect). `window.c` switches to the shared
   functions (§11.4). The spectator fog-palette draw site uses the same helpers → moves together.
3. **CHAT** — deltas applied at: `hud_render_message` channel 0 (`x`, `y`); the shadow-panel block
   (`x=3, y=76+…, w, h` and the input underlay `3, 90` + accent line); the input row renderer
   (prefix `11, top_y+15`; per-row `font_render(11, y …)`; selection rect `x0/x1, y`); and
   `chat_input_offset_at` (add deltas before its math — it is the click hit test). The wrap width
   `avail_w` stays measured from the *natural* right edge minus delta-x so long lines still wrap
   against the screen: `avail_w = W − 11 − 16 − dx` (clamped ≥ 100 px).
4. **KILLFEED** — §9.1. Hidden-while-typing rule stays.
5. **MINIMAP** — inside the minimized branch introduce
   `float mm_x = W − 143*sf, mm_top = 585*sf;` immediately, then `hud_layout_origin(HUD_EL_MINIMAP, &mm_x, &mm_top)`
   and **replace every** occurrence in the branch: backdrop `mm_x−1*sf`, `mm_top+1*sf`; scissor
   `glScissor(mm_x, mm_top−128*sf, …)`; map draw offset `mm_x − …`, `mm_top + …`; sector text base
   `(mm_x + 66*sf, mm_top − 131*sf)`… **derive from `mm_x/mm_top`, do not keep independent
   `settings.window_width − 77*sf` expressions**; tent/intel/territory/player icon calls (≈10 sites
   in the branch) all use `mm_x + …*map_scale*sf` / `(mm_top − …)*sf`. Search pattern to verify
   completeness afterwards: `grep -n "143 \* scalef\|585 \* scalef\|586 \* scalef\|77 \* scalef\|454 \* scalef" src/hud.c`
   must return **zero** hits inside this branch when done.
6. **SCORES_TOP** — `bx = W/2−75` (team1) / `W/2` (team2), `by = H−24`; add delta to box + text.
7. **GMI** — natural base = the team-1 figure's TOP edge: `bx = W−8−32, by = 86` (helmet drawn at
   `gmi_y+32 = 86`, top-edge convention). Deltas apply to `bx`, and `gmi_y := by − 32 + dy` (team 1)
   / `+ 40*s` (team 2). Scaled relations (natural → custom): helmet y `by` (team 2: `by + 40*s`),
   helmet h `16*s`; skin y `by − 16*s`, h `16*s`; eyes `6*s` squares at `by − 16*s`; shadow y
   `by − 32*s`, h `2*s`; count text y `by − 4*s` (natural 82), h `30*s`; count x `bx − 30*s −
   font_length(30*s, count)`. Pin right per §9.3.1.
8. **SCOREBOARD** — at branch top introduce `float sb_x = 0, sb_y = 0;` from
   `hud_layout_origin(HUD_EL_SCOREBOARD, &sb_x, &sb_y)` (natural 0,0) and add to: ping text
   `(W/2+sb_x, H−4+sb_y)`; both team `x_offset`s `+= sb_x`; every `450*sf` → `450*sf + sb_y`; spec
   `x_offset`/`y_offset` block likewise; per-row `font_render`/intel-icon coords inherit because they
   are already expressed via `x_offset`/`450*sf` — verify each of the ~12 coordinate expressions in
   the branch picks up the delta. Guard: add `|| (hud_edit_active && pv_scoreboard)` to the branch
   condition.
9. **FPSBOX** — `bx = W−105, by = H/2−18+84` (note: author's expression; keep it as the natural
   base expression) + delta; scale ×s on 100/36/line-offsets/text heights/right-align inset 17.
   Editor shows it regardless of `settings.show_fps` while active (chrome-visibility only — do not
   modify the guard; draw call wrapped in `if(settings.show_fps || (hud_edit_active && …))`).
10. **STATS / TECHSTATS** — base `(8, H/2−60)` / `(W−8, H/2−44)`; row pitch 16 → `16*s`; guard gets
    `|| (hud_edit_active && pv_stats)` (keep the `players[local_player_id].team != TEAM_SPECTATOR`
    check out of the preview path — while editing disconnected, team is 0/TEAM_1, fine; while editing
    connected as spectator, `pv_stats` should show the panel: the added bypass covers it).
11. **SPECTATE** — three sub-sites (body-view name, hover name, switch hint + coin): all + delta.
    Name labels are centered via `font_length` — delta applies to final `nx`.
12. **TARGETINFO / YCLAMP / CENTERMSG / TCBAR** — one base point each + delta; TCBAR keeps its two
    `texture_draw` halves relative to the shared base.
13. **Crosshair / ammo ring / damage indicator / big map / selection prompts / net stats** —
    **no changes at all** (locked, §7.2).

### 9.4 Registering `hud_ingame.ctx`

* `hud_init()`: add `hud_ingame.ctx = malloc(sizeof(mu_Context));` (matching the existing style).
* `main.c` render loop: change `mu_Context* ctx = hud_active->ctx;` to
  `mu_Context* ctx = (hud_active->ctx && !(hud_active == &hud_ingame && !hud_editing_active())) ? hud_active->ctx : NULL;`
  → mu runs **only** while editing; normal gameplay path unchanged (perf + behavior).
* **Gate every mu-input entry point in `main.c`** (`mouse_click`, `mouse`, `mouse_scroll`, `keys`):
  change their `if(hud_active->ctx)` checks to
  `if(hud_active->ctx && hud_editing_active())`. Do **not** rely on "harmless no-ops": with the
  ingame ctx permanently allocated, an ungated `mouse_click` would set `ctx->mouse_down` while the
  player is shooting, and `keys()` would feed ESC/arrows into a context that never runs `mu_begin`
  during normal play — stale state that would leak into the next editor session (and make
  window.c's `holding_widget` check misfire). Gating keeps the normal-play path byte-identical.
* Movement/jump keys cannot leak regardless of the above: §9.0 step 3 clears
  `window_pressed_keys` every frame while editing.

### 9.5 Entry button (sidebar + nav)

In `hud_common_sidebar`, directly after the Skins button:

```c
hud_nav_button(ctx, &hud_ingame, hud_edit_active ? "HUD Editor ●" : "HUD Editor");
```

…plus a wrapper: `hud_nav_button` switches to `hud_struct` on click; clicking "HUD Editor" must
instead call `hud_editor_open()` (which does `hud_change(&hud_ingame)` + editor activation + snapshot).
Implement via a tiny special case in `hud_nav_button` (compare pointer to `&hud_ingame`) or a
dedicated button call — **choose the dedicated call** to keep `hud_nav_button` semantics clean:

```c
if(mu_button(ctx, "HUD Editor")) hud_editor_open();
```

(active state highlight like `hud_nav_button`'s current-screen branch, using `hud_edit_active`).
Same addition in `hud_common_nav` (label list gets "HUD Editor" between "Skins" and "Demos"/"Chat
Log"; add the width entry in the **same** list — §3.3 documents how they drifted apart once).
**⚠ Array capacity:** `labels[8]`/`mults[8]`/`raw[8]`/`widths[9]` — the worst case is already 7
entries today (desktop disconnected + update banner: Servers, Settings, Controls, Skins, Demos, New
updates, Exit); adding HUD Editor makes it exactly 8 — legal but zero headroom. **Bump all four
arrays to size 10** in the same commit.

---

## 10. Editor UI specification

### 10.1 Layout (Space A, microui native)

```
┌─────────────────────────────── HUD Editor ────────────────────────┐
│ Elements ──────────────────────────────────────────────────────── │
│ [x] Health                ← row = checkbox + click-to-select      │
│ [x] Ammo / held item                                              │
│ [x] Block color palette                                           │
│ [x] Chat                                                          │
│ [x] Killfeed              ← selected row drawn with accent bg     │
│ [x] Minimap (small)                                               │
│ [ ] Scoreboard (TAB)      ← unchecked = hidden (or preview off)   │
│ … (grouped headers: Gameplay / Chat / Information / Diagnostics /  │
│    Special / Locked(greyed))  [scrollable panel]                  │
│ ───────────────────────────────────────────────────────────────── │
│ Selected: Killfeed                                                │
│  Anchor:   ┌───────┐   X: [   0.000 ]  Y: [   0.000 ]  (fractions)│
│            │ ▪ ▪ ▪ │   (3×3 anchor picker, current = accent)      │
│            │ ▪ ▪ ▪ │   Scale: [────●──────] 1.00  (if scalable)   │
│            └───────┘                                              │
│  [ Reset element ]        (nudge pad for touch: ◄ ▲ ▼ ► ±1 ±10)   │
│ ───────────────────────────────────────────────────────────────── │
│ Aids:  Snap [Off|2|4|8|16|32]   [x] Grid   [x] Guides   [x] Safe  │
│ Preview: [x] Chat [x] Killfeed [x] Health/Ammo [x] Palette        │
│          [x] Minimap data [x] Scores [ ] Scoreboard [x] Stats     │
│          [x] FPS box [x] Player counter                           │
│ ───────────────────────────────────────────────────────────────── │
│ [   Apply   ] [ Apply & Resume ]                                  │
│ [  Revert   ] [   Reset All   ] [ Close ]   Saved ✓ 19:42:07      │
└───────────────────────────────────────────────────────────────────┘
```

* Window: `mu_begin_window_ex(ctx, "HUD Editor", mu_rect(W−336, 16, 320, H−32), MU_OPT_NORESIZE | MU_OPT_NOCLOSE)`
  — movable by title bar (microui built-in), not resizable, no X button (closing is **explicit**
  via the panel's Close / Apply & Resume buttons, so there is exactly one close code path).
  Default rect re-clamped to the window each frame (§9.0 step 6). The panel rect is stored each
  frame in `hud_edit_panel_rect` (from `mu_get_current_container(ctx)->rect`, Space A) for
  click-exclusion (§11.2).
* **Close button** (last row, next to Revert): runs the §12.3/§12.4 close flow — mandatory for
  touch builds (no ESC key on iOS/Android; the on-screen Menu button is suppressed while editing).
* Widgets used: `mu_checkbox`, `mu_button`, `mu_slider_ex` (scale, 0.5–2.0 step 0.05 — bind via a
  `mu_Real` temp then write back, the same pattern `hud_settings_render` uses at ~5122/~5140),
  `hud_textbox` (X/Y numeric fields, parse with `strtof`, commit on submit or focus loss),
  `mu_begin_treenode_ex` for groups, `mu_header_ex` for section titles, `mu_begin_popup` for
  confirmations. No new microui primitives required (verified against `microui.h`).
* Row construction: `mu_layout_row(ctx, 2, (int[]){ 26, -1 }, 0)` — checkbox first (fixed ~26 px
  column), name button fills the rest. **Do not use negative widths here**: in microui a negative
  width anchors the item to the container's right edge — `{ -24, -1 }` would put the checkbox on
  the right. Name button: `mu_button_ex(ctx, name, 0, 0)` (flags 0 = microui's default LEFT text
  align; there is no MU_OPT_ALIGNLEFT constant). Selected row swaps `MU_COLOR_BUTTON` to accent
  (pattern already used by `hud_nav_button` and the settings categories).

### 10.2 Selection model

* One selected element (`hud_edit_selected`, −1 = none).
* Select by: clicking the element on screen (pick, §8.5), or clicking its row.
* Deselect by: clicking empty screen space, or ESC-with-no-popup.
* Selecting an element opens its section (anchor grid reflects live state; X/Y textboxes show
  fractions with 4 decimals; Reset button enabled only when custom).

### 10.3 Chrome rendering (Space B, drawn in `hud_editor_frame_end` before the mu panel)

| Item | Style |
|---|---|
| Hover outline | accent color, 1 px, alpha 160 |
| Selection outline | accent color, 2 px, alpha 255 + 4 corner handles (6×6 px filled squares at bounds corners) |
| Ghost (hidden/context element outline) | white, alpha 90, dashes (8 px on / 5 px off via `glx_draw_line_2d` segments) |
| Name tag | 16 px Fixedsys outlined, `hud_font_render_outlined(bounds_x + 4, bounds_y + 20, 16, tag)` — glyph box spans `[top+4, top+20]`, i.e. floats just **above** the element's top edge (top-edge convention, §5) |
| Live readout | 16 px, below name tag: `Δx −24 px (−3.0%w)  Δy +18 px (+3.0%h)` vs natural |
| Grid (when on) | lines every 32 px (GL), alpha 40; center cross alpha 70 |
| Guides (during snap) | cyan `0,255,255`, 2 px, alpha 220, full width/height at the snapped edge/center |
| Safe area (when on) | rectangle inset 4 % of W / 4.5 % of H, white alpha 60, 1 px — reflects the iOS strip caveat |
| Dimming | **none** by default (HUD must stay readable); optional "Dim" checkbox draws black alpha 90 before chrome but after elements |

All chrome drawing must leave GL state as found: `glColor3f(1,1,1)` after color changes,
`glLineWidth(1)` after wide lines, blend enabled/disabled exactly around the calls that need it
(follow `hud_healthbar_render`'s discipline). No new GL features (works on GLES1).

### 10.4 Drag interaction state machine (desktop)

```
state IDLE:
  LMB press over panel rect      → ignore (microui owns it)
  LMB press over element         → select AND begin DRAG immediately
                                    (grab = mouse_gl − bounds.top_left; drag starts
                                    on first movement — a click without movement
                                    just selects)
  LMB press over empty space     → deselect
  ESC                            → close flow (§12.4)
state DRAG:
  mouse move → proposed = mouse_gl − grab → snap → hud_layout_set_px(selected,…)
               (bake on first move per §6.2; set unsaved flag)
  guides drawn when snap-pulled
  LMB release → IDLE; final position re-clamped; unsaved flag stays
state NUDGE (keyboard): arrows move selected by 1 px (Shift: 10 px) —
  only while editor active; Tab cycles selectable elements (panel order)
```

Wheel: over panel → microui scroll (default path); over canvas → nudge selected element
vertically by one snap step (or 8 px when snap off).

RMB while editing: reserved for future context menu; currently **deselect** (and do not reach the
game handler — §11 interception already guarantees this).

### 10.5 Touch adaptation (state machine deltas)

* Finger down on canvas = same as LMB (select+drag from touch start) — fingers map through the
  menu-style routing (§11.4); drag threshold 6 px to distinguish tap-select from drag.
* The panel is scrollable via the existing finger→`mu_input_scroll` emulation (drag on panel).
* Nudge pad buttons in the selected-element section (◄▲▼►, ±1/±10 px steps) give precision.

### 10.6 Preview system

Panel checkboxes map to file-static flags in hud.c:

```c
static int pv_weapons, pv_palette, pv_chat, pv_killfeed, pv_scores,
           pv_gmi, pv_scoreboard, pv_stats, pv_fpsbox;   /* defaults: all 1 except pv_scoreboard */
```

* **Chat / killfeed sample data**: on editor open, if the respective channel is empty
  (`chat_messages == 0` or no live lines), seed via the real `chat_add` path:
  chat → `"[preview] This is how chat will look"` plus 4 more lines using the color-code bytes
  `\1`..`\7` (`chat_add(0, …)`); killfeed → 3 lines in the EXACT network format
  (`network.c` ~877): `sprintf(m, "%c%s%c killed %c%s%c (%s)", '\1', "Alpha", '\4', '\2',
  "Bravo", '\4', "Rifle")` → `chat_add(1, rgb(…team1…), m)`, and one grenade/melee variant each
  ("(Grenade)", "(Spade)"). Using the real bytes guarantees the editor preview renders the same
  multi-color segmentation as live killfeed.
  While `hud_edit_active`, every frame refresh `chat_timer[0][idx]/[1][idx] = window_time()` **only
  for the seeded index range** (record `pv_seed_lo/hi` at seed time) so sample lines never fade.
  On editor close, stop refreshing — they age out within 10 s naturally. **Never seed when the
  channel already has live traffic** (connected in-game): the real content is the best preview.
* **Health/ammo/palette/GMI**: guard bypasses per §9.2 with fixed sample values
  (health 74, ammo `24/90` rifle, blocks 50). While disconnected, team colors come from a zeroed
  `gamestate` (black boxes) — accepted cosmetic limitation, documented in the panel help text.
* **Scoreboard**: `pv_scoreboard` renders the TAB branch with a synthesized roster: 3 TEAM_1 /
  3 TEAM_2 / 2 spectators with fake names/scores written into the render path only (the branch
  reads `players[]` — add a small `hud_editor_scoreboard_row(i)` accessor that returns synthetic
  rows when `hud_edit_active && pv_scoreboard && !network_logged_in`, else real data; keep the
  branch's real-data path byte-identical when the flag is off).
* **Stats**: bypass connection guard; real (mostly-zero) counters are fine as a shape preview.
* **FPS box**: drawn while editing regardless of the setting (wraps its guard, not the setting).
* Preview flags are **not persisted**; they reset to defaults on each editor open.

### 10.7 Editor-local settings

`hud_edit_snap` (0/off or 2|4|8|16|32 px), grid/guides/safe checkboxes, dim checkbox — file-statics,
not persisted. Default: snap 8, grid off, guides on, safe off, dim off.

---

## 11. Input handling changes

### 11.1 Keyboard — `hud_ingame_keyboard`

First statement of the function:

```c
if(hud_edit_active) { hud_editor_keyboard(key, action, mods, internal); return; }
```

`hud_editor_keyboard` handles (and **only** these):

| Input | Action |
|---|---|
| any key while the save/confirm **popup is open** | ESC/Cancel-key cancels the popup; nothing else — editor close flow is suppressed |
| any key while a mu **textbox has focus** (`ctx->focus != 0`) | ignored here (microui edits the field via `mu_input_text`); only ESC passes through to the close flow |
| `WINDOW_KEY_ESCAPE` press | close flow (§12.4) |
| `WINDOW_KEY_HUD_EDITOR` press | close flow (same as ESC) |
| arrows / Shift+arrows | nudge selected (§10.4) |
| `WINDOW_KEY_TAB` | cycle selection |
| text keys | *not handled here* — microui textboxes receive them via `mu_key_translate`/`mu_input_text` in `keys()` (already generic) |

Everything else: swallowed (no tool switch, no chat, no Y-Clamp, no volume — the game must not react
while editing). Note the `keys()` show_exit guard (§3.2): the editor is never opened while
`show_exit` is set without going through `hud_editor_open()` which clears `show_exit`.

### 11.2 Mouse — `hud_ingame_mouseclick` / `hud_ingame_mouselocation` / `hud_ingame_scroll`

```c
/* mouseclick, first statement */
if(hud_edit_active) { hud_editor_mouseclick(x, y, button, action, mods); return; }
```

* `hud_editor_mouseclick` converts to GL (`gl_y = H − y`), applies the §10.4 machine,
  **ignoring events whose point lies inside `hud_edit_panel_rect`** (panel is in Space A; the test is
  `x >= r.x && x < r.x+r.w && y >= r.y && y < r.y+r.h` directly against the mouse coords — both are
  Space A here, no conversion for this specific test).
* Rationale: `mouse_click()` already forwarded the same click to microui *after* us (§3.2); by the
  time our handler runs we cannot know whether microui will claim it, so we exclude the panel
  rectangle geometrically. This is the single most important interaction rule in the editor —
  without it, clicking an element "under" the panel selects it while the panel also reacts.
* `hud_ingame_mouselocation`: editor branch returns immediately (no camera look). Microui receives
  moves through the generic `mu_input_mousemove` path in `mouse()` — panel hover/drag works.
* `hud_ingame_scroll`: editor branch: if over panel → return (microui scrolls via its own
  `mu_input_scroll`); else nudge selected (§10.4) and return. Weapon switching must not happen.

### 11.3 Input-state hygiene on open/close

`hud_editor_open()`:

```c
show_exit = 0;
hud_layout_snapshot_take();
/* clear held input so nothing stays "pressed" from before the editor opened
   (same idiom as player.c's existing `p->input.buttons.packed = 0;` path): */
players[local_player_id].input.keys.packed = 0;
players[local_player_id].input.buttons.packed = 0;
button_map[0] = button_map[1] = button_map[2] = 0;
chat_sel_clear();
window_textinput(0);          /* hud_ime_update() re-enables per-frame while a panel
                                 textbox is focused (main.c calls it after mu_end) */
window_mousemode(WINDOW_CURSOR_ENABLED);
hud_edit_active = 1; hud_edit_selected = -1;
pv flags = defaults;
```

Guard: if `hud_edit_active` is already set, `hud_editor_open()` returns immediately (idempotent).
Also record `hud_edit_opened_connected = network_connected;` — its **only** purpose is the
"server died mid-session" force-close test in §9.0 step 2 (`!network_connected &&
hud_edit_opened_connected`); close routing never uses it (§12.3 routes on live connection state).

`hud_editor_close(bool save)`:

```c
if(save) hud_layout_save();
hud_edit_active = 0;              /* also resets panel/selection/popup state */
if(network_connected) {
    window_mousemode(WINDOW_CURSOR_DISABLED);   /* stay in gameplay */
} else {
    hud_change(&hud_serverlist);  /* menu re-enables the cursor in its init
                                     (hud_serverlist_init does this explicitly) */
}
```

`hud_ingame_render` without the editor flag over a disconnected client is never left on screen —
the routing rule (§12.3) always lands on a valid screen.

### 11.4 window.c / touch routing

Every branch in the SDL finger handlers that currently tests `hud_active == &hud_ingame` gains
`&& !hud_editing_active()` so that, while editing, fingers are treated exactly like on menu screens
(menu-style mouse emulation for the mu panel, tap = click, drag = move):

* `SDL_FINGERDOWN/MOTION/UP` aim-finger selection & joystick handling,
* the long-press → RMB synthesis (menu branch already handles it),
* `window_aim_zone()` is naturally bypassed by the same routing (it is only consulted on the
  ingame path).

**Palette sync:** replace the private fraction copies in `window_aim_zone` (and anywhere else in
window.c that mirrors palette geometry) with the shared `hud_layout_palette_rect()`. Because the
helper applies the layout offset, aiming exclusion automatically follows a moved palette — this
closes the pre-existing drift hazard between the two files. Add `#include "hud_layout.h"` to
window.c.

### 11.5 Hotkey registration (`config.c`, both SDL and GLFW blocks)

```c
config_register_key(WINDOW_KEY_HUD_EDITOR, SDLK_F10,    "hud_editor", 0, "HUD editor", "Game");
config_register_key(WINDOW_KEY_HUD_EDITOR, GLFW_KEY_F10,"hud_editor", 0, "HUD editor", "Game");
```

Plus the enum entry in `window.h` immediately before `WINDOW_KEY_COUNT` and the display-name case in
window.c's key-name table (`"F10"` exists already; add `"HUD editor"` mapping is automatic via the
config display field). F10 was chosen because every other F-key is taken (§3.5); it is rebindable in
Controls like any key.

---

## 12. Mode transitions and buttons

### 12.1 Opening from a menu (pause/main)

```
hud_editor_open()  [called from sidebar/nav button]
  1  show_exit = 0
  2  hud_change(&hud_ingame)          /* re-inits ingame mu ctx — editor builds UI fresh */
  3  hud_ingame_init() side effects happen via hud_change → init:
       cursor disabled + textinput off — immediately overridden:
  4  window_mousemode(WINDOW_CURSOR_ENABLED); window_textinput(0);
  5  input hygiene (§11.3) — no-op when disconnected
  6  snapshot; editor state reset (selection, previews, panel); hud_edit_active = 1
```

Opening from live gameplay (hotkey): identical minus steps 2-3 (already `hud_ingame`); do **not**
call `hud_change` (it would reset nothing important, but skipping keeps drag state simple).

### 12.2 Rendering while editing

* Connected: world renders (existing `render_world = 1`), elements draw with layout + preview flags,
  chrome + panel on top.
* Disconnected (opened from main menu): skip both the 3D pass **and** the world simulation —
  * `main.c` ~1672: `if(hud_active->render_3D && !(hud_editing_active() && !network_connected)) hud_active->render_3D();`
    (the call site is gated on `render_3D` presence, not `render_world`).
  * `main.c` ~2454: `if(hud_active->render_world && !(hud_editing_active() && !network_connected)) {`
    — without this, `player_update`/`grenade_update` keep simulating while editing disconnected
    (the local player would fall/physics-run with no map, and could "die" mid-edit).
  `hud_editor_frame_begin` draws the menu background instead (§9.0 step 4); the framebuffer is fully
  overwritten by it, so skipping the 3D pass leaves no artifacts. All preview-guarded elements draw
  on top (§10.6); elements without preview support simply don't show (fine).

### 12.3 Closing

Routing rule (single source of truth, no stored "came from menu" flag — a flag can go stale when the
server dies mid-edit): **after closing, `if(network_connected) stay in hud_ingame; else
hud_change(&hud_serverlist);`** — the serverlist is the only valid disconnected screen, and when the
user never left the menu, it *is* the screen they came from.

| Trigger | Flow |
|---|---|
| **Apply & Resume** | save → editor close (cursor captured) → routing rule above. |
| ESC / hotkey / panel **Close** with **no unsaved changes** | close (no write). Same routing. |
| With **unsaved changes** | popup (§12.4) first. |
| **Forced close** (map transfer starts, server disconnects, demo playback starts — detected in §9.0 step 2) | close **without** writing; `log_info` + `chat_showpopup("HUD editor closed", …)`; routing rule decides the landing screen. |

**Choke point:** `hud_change()` itself ends the editing state whenever the destination is not
`hud_ingame`: clear `hud_edit_active`, reset panel/selection/popup state. This guarantees the flag
can never leak into another screen — even if a close path is missed (e.g. the Disconnect button
being unreachable is an assumption; a network drop during Apply is not). Place the check at the top
of `hud_change()`:

```c
if(hud_edit_active && new != &hud_ingame) hud_editor_force_close();
```

### 12.4 Unsaved-changes popup (microui, modal via `MU_OPT_HOLDFOCUS`)

```
┌ Save HUD changes? ────────────────┐
│ You changed the HUD layout.       │
│ [Save & Resume] [Discard] [Cancel]│
└───────────────────────────────────┘
```
Save & Resume → `hud_layout_save()` + §12.3 close. Discard → `hud_layout_snapshot_restore()` then
close **without** writing (live state returns to saved). Cancel → stay.

### 12.5 Button wiring summary (the user's requested flow, explicit)

* **"a button to open the editor"** → sidebar/nav "HUD Editor" + F10 (§2.1, §9.5, §11.5).
* **"a button to apply changes and go back in editor"** → **Apply** (§2.3): writes file, editor stays open.
* **"and go back in game"** → **Apply & Resume** (§2.3): writes file, closes editor, un-captures nothing
  (re-captures cursor), back to gameplay/menu.

---

## 13. Persistence format

### 13.1 File

`hud_layout.json` next to `config.ini` (same writable-dir resolution via `file_open`/`file_load`).

### 13.2 Schema v1 (normative example)

```json
{
    "version": 1,
    "elements": {
        "health":    { "anchor": 6, "ox":  0.012500, "oy":  0.033333, "visible": true,  "scale": 1.0 },
        "killfeed":  { "anchor": 2, "ox": -0.040000, "oy":  0.000000, "visible": true,  "scale": 1.0 },
        "techstats": { "anchor": 5, "ox":  0.000000, "oy":  0.000000, "visible": false, "scale": 1.0 }
    }
}
```

Reading the example (at any window size): health is anchored **BL** (6) and nudged right 1.25 % of W
and **up** 3.33 % of H (positive `oy` = up, GL convention); killfeed is anchored **TR** (2) and
pulled left 4 % of W — i.e. slightly inside from the top-right corner; techstats sits at its natural
position (anchor 5, zero offsets) but is hidden.

* Only **non-natural** (custom or non-default visibility/scale) elements need to appear; a missing
  element key = natural defaults. (Writing all 17 is also valid; the loader accepts both.)
* `anchor` indices per the enum in `hud_layout.h` (0=TL … 8=BR, row-major from top-left).
* `ox`/`oy` are Space-B fractions (§6.3): positive `oy` = up on screen (document this **in the file
  header comment is impossible in JSON — document in README/§6**).
* Unknown element keys: warn + ignore. Unknown top-level keys: warn + ignore.

### 13.3 Save trigger points (exhaustive)

Apply button, Apply & Resume, popup "Save & Resume". Nothing else ever writes the file (closing,
quit, disconnect: no writes). `config_save()` is **not** involved.

### 13.4 Versioning & migration

* Loader: if `version > 1` → load what parses, warn loudly; if `version` missing → treat as 1.
* Element ids/`json_key`s are append-only (Appendix A). Renames forbidden; a future rename ships as
  new key + read-old-key migration in the loader.

### 13.5 Corruption behavior

Parse failure → defaults for everything, `log_warn("hud_layout: parse error, defaults in use")`,
editor still fully usable; first Apply overwrites the bad file (freshman-sailor proof).

---

## 14. Edge cases and pitfall checklist

Each item is a **must-verify** box. Numbered for test-plan cross-reference.

1. **Y-flip trio** — mouse (A, y-down), GL (B, y-up), mu panel (A). Verify: drag up ⇒ element moves up
   on screen; anchor TL selection keeps element near top-left after resize.
2. **Anchor point table** — TOP anchors have GL `y = H` (§6.2/§8.3). Verify: set anchor TR on minimap,
   resize window taller → minimap stays pinned to top-right.
3. **Click-through into panel** — clicks inside `hud_edit_panel_rect` must never select elements
   (§11.2). Verify: click panel buttons while an element lies under the panel.
4. **F6 hide-HUD** — must not blank the screen while editing (guard §9.0). Verify: hold F6 in editor.
5. **Cursor capture** — open editor mid-gameplay via F10 ⇒ cursor visible & movable; close ⇒ captured
   again. Also verify `disable_raw_input` setting doesn't leak relative deltas (editor branch returns
   before look math, so it cannot).
6. **Held input on open** — if W/mouse was held when opening (hotkey), the player must stop moving and
   shooting; verify no stuck fire after close (input zeroing §11.3 **plus the per-frame
   `window_pressed_keys` memset**, §9.0 step 3 — the zeroing alone is NOT sufficient because
   `keys()` re-fills the array before the hud handler runs).
7. **Chat input interplay** — editor cannot be opened while typing (T is swallowed); if chat input is
   somehow open, `hud_editor_open` force-closes it (`chat_input_mode = CHAT_NO_INPUT` +
   `window_textinput(0)`).
8. **Killfeed-while-typing rule** — unchanged; editor's killfeed preview path doesn't touch it.
9. **Preview timer leak** — seeded chat/killfeed timers must only be refreshed while editing;
   after close they age out; verify no immortal preview lines after a match restart
   (`chat_add` shift clears eventually — the seed indices are < 128 and get reused).
10. **Default identity** — with no json file: byte-identical rendering (screenshot diff §16 T1);
    `hud_layout_origin` no-op path is the guarantee; also verify `hud_editing_active()` gating keeps
    the mu ctx NULL path identical (no mu commands emitted).
11. **Resolution change while editing** — drag window between 1280×720 ⇄ 1920×1080 (and
    fullscreen toggle): offsets track anchors; no element may fly off; panel re-clamps.
12. **HIGHDPI / retina** — mouse coords are pre-scaled by window.c (`mouse_scale_*`); editor math
    uses the same window-pixel space as rendering — verify on a 2× display that picking is not
    offset (this is exactly the class of bug the chat hit-test in `chat_input_offset_at` already
    solved; reuse its assumptions).
13. **Palette move sync** — move palette; in-game (after resume) block-tool aim zone and tap-to-pick
    must follow (shared helper §11.4). Verify on desktop *and* touch build.
14. **Forced close triggers** — map transfer starting (server sends new map), disconnect, demo
    playback start (§9.0 step 2) ⇒ editor closes without writing; no crash when world assets
    appear/disappear mid-edit. **Also verify the `hud_change()` choke point** (§12.3): any screen
    switch while editing clears the editor state — the flag must never be observed on another HUD.
    Note: a forced close keeps the *unsaved* edits in the in-memory layout (file untouched); the
    next editor session shows them with the unsaved flag set — intended.
15. **Minimap scissor** — moved minimap must move its `glScissor` rect with it (it derives from
    `mm_x/mm_top` per §9.3.5); verify map content stays inside the frame at odd positions; verify on
    **GLES1/Android** specifically (scissor path was hand-tuned there — see the long comment in the
    minimap block).
16. **Scale + right-pin** — scale minimap/ammo/GMI/techstats/fpsbox; their right edge must stay at the
    anchor; verify no drift when scaling (§9.3.1 helper).
17. **iOS home-indicator strip** — bottom-anchored elements dragged to `oy` such that they'd sit
    inside the bottom 4.5 %: allowed, but Reset Element must recover; note in help text (§6.3).
18. **Elements hidden by checkbox then editor closed** — hidden stays hidden in gameplay; verify the
    element is still pickable next editor session via panel list (bounds fallback §8.5).
19. **Scoreboard preview vs real TAB** — pressing TAB is swallowed while editing; preview flag is the
    only way to see it; on close, TAB behaves normally (verify no stuck `window_pressed_keys[TAB]`).
20. **Editor + spectator camera** — opening the editor as spectator: elements that need FPS-branch
    guards appear via preview bypasses; the live spectator HUD (labels) moves too if positioned;
    verify no crash from `cameracontroller_bodyview_player` paths (they keep their own guards).
21. **Editor + demo playback** — demo overlay hidden while editing (§14.24/§9.0); demo time keeps
    advancing; seek hotkeys swallowed; on close the overlay returns.
22. **Multiple monitors / window drag** — nothing special: window pixel space is self-consistent.
23. **Very small windows** (min SDL size) — panel default rect clamps to fit; elements clamp ±1.25;
    X/Y textboxes still work; verify no division by zero (`W`, `H` ≥ 1 always true in practice).
24. **Demo overlay & touch plates while editing** — both suppressed (guards in §9.0 tail).
25. **Chrome GL state leaks** — after editor frame: `glLineWidth(1)`, `glColor3f(1,1,1)`, blend state
    as found; verify microui panel colors are unaffected by chrome (draw order chrome → mu flush).
26. **JSON from a newer version** — warn + best-effort load (§13.4); never crash.
27. **Two editors?** — impossible: single flag, single player. Ignore.
28. **`settings_tmp`** — editor never touches `settings`/`settings_tmp`; verify opening settings
    screen after an edit session shows no layout-related entries and Apply/Cancel there doesn't
    resurrect or wipe the layout (they are independent files).
29. **Undo-less safety** — Revert/Reset paths verified to restore *exactly* the snapshot bytes→state.
30. **Hotkey while typing in panel textbox** — while a mu textbox has focus (`ctx->focus != 0`)
    `hud_editor_keyboard` ignores everything except ESC, and while a popup is open ESC only cancels
    the popup (§11.1 table). Verify: F10/arrows typed into the X/Y fields do not nudge or close; ESC
    with the save popup open does not close the editor.
31. **Element dragged fully off-screen then editor closed** — recovery = panel list → select row →
    Reset element; verify this works for an off-screen element (bounds fallback keeps the row usable).
32. **Killfeed/chat overlap after both moved to the same spot** — picking resolves topmost via the
    highest `draw_seq` stamp (§8.5); the killfeed draws after chat inside the message loop, so it
    must win. Verify clicking the overlap selects the killfeed, then select chat via its panel row.

33. **Editor open is idempotent** — clicking the sidebar "HUD Editor" button twice (or hotkey spam)
    must not re-snapshot (which would make Revert a no-op) and must not double-toggle (§11.3 guard).

34. **No mu state leak into editor sessions** — after a long shooting session (mouse_down churned
    during normal play), open the editor: the panel must behave normally on first click (mu input
    gates, §9.4) — no phantom drag, no stuck hover.

35. **Anchor-edit of a hidden element** — changing anchor/X/Y of an element that isn't drawn (hidden
    checkbox) operates on stale/nominal bounds (§8.5); verify no crash and that Reset Element
    recovers.

---

## 15. Implementation order

Each phase is one reviewable commit. **Do not** merge a phase with any checklist item failing.

### P0 — Groundwork (no behavior change)
* `hud_layout.h/.c` skeleton (enum, metadata table, state array, natural-mode API, init/save stubs).
* CMake source list + `window.h` enum entry + `config.c` F10 registrations.
* `hud_ingame.ctx` alloc + `main.c` mu gating + `hud_editing_active()` (returns 0 always, for now).
* **DoD:** builds on Linux/SDL + Android; game identical; F10 does nothing yet.

### P1 — Element conversions (identity only)
* Implement §9.1-§9.3 for all 17 movable elements (mode natural ⇒ pure no-op).
* Add `hud_layout_report_bounds` calls.
* Palette helper migration incl. window.c consumer.
* **DoD:** screenshot diff (§16 T1) passes at 800×600, 1280×720, 1920×1080; grep checks (§9.3.5) clean.

### P2 — Persistence
* JSON load/save (§8.6, §13), `hud_layout_init` wiring at startup, dirty/snapshot API.
* **DoD:** hand-write a json moving killfeed; observe it applied at boot; corrupt file → defaults+warn.

### P3 — Editor core
* Editor state machine, chrome, panel (list, select, drag, Apply/Apply & Resume/Revert/Reset),
  open/close flows, hotkey toggle, sidebar/nav buttons, input interception (§10, §11, §12).
* **DoD:** full §2.5 walkthrough works on desktop.

### P4 — Preview system + panel completeness
* §10.6 previews, anchor picker, X/Y fields, scale sliders, snapping/guides/grid/safe, popups,
  status line, nudge keys, wheel nudge.
* **DoD:** every §10 item demonstrable; §14.1-§14.13 verified.

### P5 — Touch + platform
* window.c finger routing, nudge pad, long-press behavior, iOS strip verification.
* **DoD:** §16 touch matrix passes on Android; iOS build boots to editor.

### P6 — Hardening & docs
* §14.14-§14.32 edge cases, README entry, this document updated to "implemented" with any deviations.

---

## 16. Test plan

### 16.1 Automated-ish checks

* **T1 Pixel-identity:** build pre-feature binary, capture screenshots at 800×600 / 1280×720 /
  1920×1080 (main menu, in-game FPS, spectator, TAB scoreboard, chat open — 5 shots × 3 sizes).
  Repeat with post-feature build, no `hud_layout.json`. Byte-compare (or PSNR ≥ 60 dB given the
  animated RGB accent — freeze `ui_rgb` + use a static accent for the test).
* **T2 Grep gates:** `grep -n "143 \* scalef" src/hud.c` (minimap) and the other §9.3.5 patterns
  return nothing inside their blocks; `grep -n "settings_tmp\|config_set" src/hud_layout.c` returns
  nothing (module isolation).
* **T3 Round-trip:** save → reload → save; second file byte-equal to first (modulo key order —
  use parson's deterministic object order; if unstable, compare parsed objects instead).

### 16.2 Manual matrix

| # | Scenario | Platforms |
|---|----------|-----------|
| M1 | §2.5 walkthrough end-to-end | Win/Linux/macOS |
| M2 | Each of 17 elements: drag to each screen corner + center, at 2 sizes | Win/Linux |
| M3 | Anchor changes on minimap + window resize ×3 | Linux |
| M4 | Scale each scalable element 0.5 / 1.5 / 2.0; check pin edge | Linux |
| M5 | Visibility toggles: hide all → Apply → in-game shows none; restore | Linux |
| M6 | Popup flows: ESC-with-changes ×3 buttons; Revert; Reset All | all |
| M7 | Hotkey open/close mid-combat; shoot/move blocked during edit; no stuck keys after | Linux |
| M8 | Connected editing during live match; death while editing; respawn | Linux |
| M9 | Disconnected editing from main menu; Apply & Resume returns to serverlist | Linux |
| M10 | Touch: select/drag/nudge/panel scroll; palette move + pick correctness | Android |
| M11 | GLES2 renderer path (Android) + GL desktop path — visuals equal | Android/Linux |
| M12 | Resize/fullscreen spam while dragging (stress, no crash, no stuck drag) | Linux |
| M13 | Corrupt/truncated/newer-version json at boot | Linux |
| M14 | Demos: open editor during replay; close; replay overlay intact | Linux |
| M15 | Classic regression sweep: chat text cursor placement after moving chat; scoreboard readable; minimap icons align at all zooms (`minimap_zoom` 1..5) | Linux |

---

## 17. Risks and mitigations

| Risk | L | Impact | Mitigation |
|---|---|---|---|
| Coordinate flip bug (mouse vs GL) | H | Broken drag/pick | §5 rules + M2 first-run; hit-test reuses proven `chat_input_offset_at` pattern |
| Element conversion misses a coordinate (stray inline constant) | M | Element tears apart when moved | §9.3 per-element grep gates; M2 per-element |
| microui panel steals game clicks or vice versa | M | Frustrating UX | Panel-rect exclusion (§11.2) + mu ctx gating (§9.4) |
| Preview seeds leak into real chat | M | Fake messages persist | Seed-only-when-empty + timer refresh only while editing (§14.9) |
| Palette desync hud.c/window.c | M | Touch aim dead zone wrong | Shared helper replaces both copies (§11.4) |
| GLES1 scissor/minimap regressions | M | Broken minimap on Android | mm_x/mm_top refactor keeps scissor derived from same base; M11 |
| Performance | L | — | 17 array lookups/frame; bounds stores; zero allocations in frame path |
| Scope creep (colors, undo, presets) | M | Delayed v1 | §1.2 non-goals; §18 backlog |

---

## 18. Future extensions (backlog, not v1)

1. Layout import/export + sharing (files under `hud_layouts/`, picker screen — schema already supports).
2. Per-element color/opacity overrides; font size for chat.
3. Undo/redo stack (command pattern around `hud_layout_set_*`).
4. Mobile touch-control editing (separate interaction model, pinch-scale).
5. Streaming presets (one-key swap between "compact"/"caster" layouts).
6. Alignment-to-other-elements smart guides (beyond screen guides).

---

## Appendix A

### Element ids ↔ json keys (append-only)

| id | enum | json key | panel name | scalable | default anchor |
|----|------|----------|------------|----------|----------------|
| 0 | HUD_EL_HEALTH | `health` | Health | yes | 6 (BL) |
| 1 | HUD_EL_AMMO | `ammo` | Ammo / held item | yes | 8 (BR) |
| 2 | HUD_EL_PALETTE | `palette` | Block color palette | no | 7 (BC) |
| 3 | HUD_EL_CHAT | `chat` | Chat | no | 6 (BL) |
| 4 | HUD_EL_KILLFEED | `killfeed` | Killfeed | no | 0 (TL) |
| 5 | HUD_EL_MINIMAP | `minimap` | Minimap (small) | yes | 2 (TR) |
| 6 | HUD_EL_SCORES_TOP | `scores_top` | Team scores (top) | no | 1 (TC) |
| 7 | HUD_EL_GMI | `gmi` | Player counter | yes | 8 (BR) |
| 8 | HUD_EL_SCOREBOARD | `scoreboard` | Scoreboard (TAB) | no | 4 (MC) |
| 9 | HUD_EL_FPSBOX | `fpsbox` | FPS + ping box | yes | 5 (MR) |
| 10 | HUD_EL_STATS | `stats` | Player stats | yes | 3 (ML) |
| 11 | HUD_EL_TECHSTATS | `techstats` | Tech stats | yes | 5 (MR) |
| 12 | HUD_EL_SPECTATE | `spectate` | Spectator labels | no | 1 (TC) |
| 13 | HUD_EL_TARGETINFO | `targetinfo` | Target info | no | 4 (MC) |
| 14 | HUD_EL_YCLAMP | `yclamp` | Y-Clamp indicator | no | 3 (ML) |
| 15 | HUD_EL_CENTERMSG | `centermsg` | Center notices | no | 4 (MC) |
| 16 | HUD_EL_TCBAR | `tcbar` | TC capture bar | no | 4 (MC) |

### Nominal hitboxes at 800×600 (Space B, `y` = TOP edge — the rect spans `[y−h, y]`; scaled by `W/800, H/600` as fallback)

Values below are computed from the natural draw calls (bar/healthbar default ON, 12 chat lines,
3 killfeed lines, default `chat_spacing`, 32 px ammo icons). They only serve until an element
reports real bounds; live-reported bounds always win.

| element | x | y (top) | w | h | derivation notes |
|---------|---|---------|---|---|------------------|
| health | 6 | 60 | 164 | 52 | bar border `[20,36]` + icon `[28,60]` + number box `[28,58]`; bar-off variant: y=40 h=32 |
| ammo | 682 | 40 | 110 | 36 | icon `[756..792]×[8,40]`; counter box right-aligned into `[682..752]×[7,37]` |
| palette | 342 | 142 | 116 | 116 | cell = 0.024·600 = 14.4 → grid 115.2 at left 342.4, top 142.2 (spans down to 27) |
| chat | 3 | 284 | 336 | 208 | shadow panel `[76,284]`; input row region below when typing (add 30) |
| killfeed | 3 | 578 | 320 | 54 | lines at y 578/560/542, glyph boxes hang below each |
| minimap | 656 | 586 | 132 | 148 | backdrop `[456,586]`; sector-letter box `[438,454]` below it |
| scores_top | 325 | 576 | 150 | 24 | boxes `[552,576]`; team2 box starts at 400 |
| gmi | 722 | 126 | 70 | 74 | figures `[52,86]` and `[92,126]`, x `[722,792]` incl. count text |
| scoreboard | 100 | 596 | 600 | 338 | panels from 450 down to ~258; ping text box `[580,596]` |
| fpsbox | 695 | 366 | 105 | 40 | box `[330,366]` (y = H/2−18+84), accent line at x=795 |
| stats | 8 | 320 | 230 | 96 | 6 rows, first box `[224,240]`, last `[304,320]` |
| techstats | 560 | 320 | 232 | 80 | 5 rows right-aligned to 792 |
| spectate | 250 | 26 | 300 | 22 | fallback = the body-view name site only; **multi-site group** — real bounds = union of the sites drawn this frame (name at top / hint at bottom / coin) |
| targetinfo | 250 | 120 | 300 | 16 | box `[104,120]` at 0.2·H |
| yclamp | 8 | 296 | 140 | 16 | box `[280,296]` at H/2−4 |
| centermsg | 250 | 300 | 300 | 32 | popup box `[268,300]`; drag-counter site near y=50 excluded from fallback |
| tcbar | 180 | 150 | 440 | 20 | box `[130,150]` at 0.25·H |

(These are *fallbacks* for pick/ghost only; live-reported bounds always win.)

## Appendix B — complete file-change manifest

| File | Change |
|------|--------|
| `src/hud_layout.h` | **new** — §8.1 API, enum, anchors |
| `src/hud_layout.c` | **new** — state, metadata, geometry, snap, bounds, pick, palette rect, JSON IO |
| `src/hud.c` | §9.0 prelude/tail + early-return guards; §9.1-§9.3 element conversions (17 blocks + `hud_render_message` deltas + `chat_input_offset_at` + `hud_healthbar_render` params); §9.5 sidebar/nav buttons; editor section (state, open/close, chrome, panel, previews, keyboard/mouse/scroll interception, scoreboard row accessor) |
| `src/hud.h` | `hud_editing_active()` declaration (+ `hud_edit_active` extern if window.c prefers the accessor — use the accessor) |
| `src/window.h` | `WINDOW_KEY_HUD_EDITOR` before `WINDOW_KEY_COUNT` |
| `src/window.c` | key-name table entry if required; finger-routing guards `&& !hud_editing_active()`; palette geometry via `hud_layout_palette_rect`; include hud_layout.h |
| `src/main.c` | mu-ctx gating (§9.4); `render_3D` gate while editing disconnected (§12.2) |
| `src/config.c` | 2× `config_register_key` F10 (SDL + GLFW blocks) |
| `src/CMakeLists.txt` | `list(APPEND CLIENT_SOURCES hud_layout.c)` |
| `README.md` | feature blurb + `hud_layout.json` note |

---

*End of plan. Implementation must follow §15 phase order; any deviation from this document
(new element, changed schema, extra API) updates this file in the same commit.*
