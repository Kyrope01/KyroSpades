"""Drive real microui frames without a graphics context."""
from pathlib import Path
from subprocess import run
from tempfile import TemporaryDirectory

root = Path(__file__).resolve().parents[1]
harness = r'''
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include "microui.h"

static mu_Context ui;
static mu_Container *panel, *other;
static int submissions, last_submitted;
#define PLAIN (MU_OPT_NOTITLE | MU_OPT_NOFRAME | MU_OPT_NORESIZE | MU_OPT_NOSCROLL)

static int width(mu_Font font, const char *text, int len) {
    (void)font;
    return 6 * (len < 0 ? (int)strlen(text) : len);
}
static int height(mu_Font font) { (void)font; return 10; }
static void init(void) {
    mu_init(&ui);
    ui.text_width = width;
    ui.text_height = height;
    ui.smooth_scroll = 1;
    submissions = 0;
}

static int scroll_frame(float t, int count, int view_height) {
    ui.time = t;
    mu_begin(&ui);
    assert(mu_begin_window_ex(&ui, "screen", mu_rect(0, 0, 400, 390), PLAIN));
    mu_layout_row(&ui, 2, (int[]){140, 140}, view_height);
    mu_begin_panel_ex(&ui, "items", MU_OPT_NOFRAME);
    panel = mu_get_current_container(&ui);
    mu_layout_row(&ui, 1, (int[]){-1}, 20);
    for (int i = 0; i < count; i++) {
        mu_push_id(&ui, &i, sizeof(i));
        if (mu_button_ex(&ui, "Row", 0, MU_OPT_NOFRAME) & MU_RES_SUBMIT) {
            submissions++;
            last_submitted = i;
        }
        mu_pop_id(&ui);
    }
    mu_end_panel(&ui);
    mu_begin_panel_ex(&ui, "other", MU_OPT_NOFRAME);
    other = mu_get_current_container(&ui);
    mu_layout_row(&ui, 1, (int[]){-1}, 20);
    for (int i = 0; i < 50; i++) mu_label(&ui, "Independent pane");
    mu_end_panel(&ui);
    mu_end_window(&ui);
    mu_end(&ui);
    return panel->scroll.y;
}

static int start_scroll(int hz) {
    init();
    mu_input_mousemove(&ui, 20, 55);
    scroll_frame(0.0f, 50, 230);
    mu_input_scroll(&ui, 0, 50);
    assert(scroll_frame(0.1f, 50, 230) == 0);
    int result = 0;
    for (int i = 1; i <= hz / 6; i++) result = scroll_frame(0.1f + (float)i / hz, 50, 230);
    assert(other->scroll.y == 0);
    return result;
}

static void scrolling(void) {
    int a = start_scroll(30), b = start_scroll(60), c = start_scroll(144);
    assert(a >= 48 && a <= 50 && a == b && b == c);
    assert(scroll_frame(0.5f, 50, 230) == 50);

    mu_input_scroll(&ui, 0, 150);
    scroll_frame(0.51f, 50, 230);
    int coasting = scroll_frame(0.53f, 50, 230);
    assert(coasting > 50 && coasting < 200);
    mu_input_scroll(&ui, 0, -35);
    int before_reverse = scroll_frame(0.55f, 50, 230);
    int reversed = scroll_frame(0.57f, 50, 230);
    assert(reversed < before_reverse && reversed > before_reverse - 35);
    assert(scroll_frame(1.0f, 50, 230) == before_reverse - 35); /* long frame settles */

    mu_input_scroll(&ui, 0, 50);
    scroll_frame(1.01f, 50, 230);
    int shown = scroll_frame(1.03f, 50, 230);
    /* Pick a visible row without priming its hover ID: the offset must not
       advance and input must use this row, not a stale hover or wheel target. */
    int row = (shown + 19) / 24 + 1;
    int y = 10 + row * 24 - shown + 5;
    mu_input_mousedown(&ui, 20, y, MU_MOUSE_LEFT);
    assert(scroll_frame(1.07f, 50, 230) == shown);
    assert(submissions == 1 && last_submitted == row);
    mu_input_mouseup(&ui, 20, y, MU_MOUSE_LEFT);
    assert(scroll_frame(1.09f, 50, 230) == shown);

    mu_input_scroll_direct(&ui, 0, 16.25f);
    mu_input_scroll_direct(&ui, 0, 1.75f);
    assert(scroll_frame(1.10f, 50, 230) == shown);
    assert(scroll_frame(1.11f, 50, 230) == shown + 18);
    assert(scroll_frame(1.12f, 50, 230) == shown + 18);

    /* Wheel coast can't fight a scrollbar drag. */
    mu_input_scroll(&ui, 0, 50);
    scroll_frame(1.13f, 50, 230);
    int drag_start = scroll_frame(1.14f, 50, 230);
    mu_input_mousedown(&ui, panel->body.x + panel->body.w + 2, 40, MU_MOUSE_LEFT);
    scroll_frame(1.15f, 50, 230);
    int grabbed = panel->scroll.y;
    mu_input_mousemove(&ui, panel->body.x + panel->body.w + 2, 52);
    int dragged = scroll_frame(1.16f, 50, 230);
    assert(dragged > grabbed && grabbed != drag_start + 50);
    mu_input_mouseup(&ui, panel->body.x + panel->body.w + 2, 52, MU_MOUSE_LEFT);
    assert(scroll_frame(1.17f, 50, 230) == dragged);

    /* Reduced motion snaps; shorter content clamps both displayed and target. */
    mu_input_mousemove(&ui, 20, 55);
    ui.reduced_motion = 1;
    mu_input_scroll(&ui, 0, 70);
    scroll_frame(1.18f, 50, 230);
    assert(scroll_frame(1.19f, 50, 230) == dragged + 70);
    scroll_frame(1.20f, 1, 230);
    assert(scroll_frame(1.21f, 1, 230) == 0 && panel->scroll_y.target == 0);
    mu_reset_scroll(panel);
    assert(panel->scroll.y == 0 && !panel->scroll_y.direct);
}

static void paint(void) {
    init();
    ui.entrance_active = 1;
    ui.time = 0.01f;
    mu_begin(&ui);
    assert(mu_begin_window_ex(&ui, "motion", mu_rect(0, 0, 100, 80), PLAIN));
    mu_push_clip_rect(&ui, mu_rect(10, 10, 80, 21));
    mu_paint_motion(&ui, 0.01f, 0.02f, 0.16f, 7.0f, 0.60f);
    mu_draw_surface(&ui, mu_rect(10, 20, 65, 25), mu_color(255, 0, 0, 200),
                    mu_color(0, 0, 0, 0), 4, 0);
    mu_draw_control_text(&ui, "label", mu_rect(10, 15, 55, 10), MU_COLOR_TEXT, 0);
    mu_paint_reset(&ui);
    mu_pop_clip_rect(&ui);
    mu_end_window(&ui);
    mu_end(&ui);
    mu_Command *cmd = NULL;
    assert(mu_next_command(&ui, &cmd) && cmd->type == MU_COMMAND_SURFACE);
    assert(cmd->surface.rect.y == 20 && cmd->surface.offset_y == 7 && cmd->surface.clip.y == 10);
    assert(cmd->surface.clip.h == 21 && cmd->surface.fill.a == 120);
    assert(mu_next_command(&ui, &cmd) && cmd->type == MU_COMMAND_CLIP);
    assert(cmd->clip.rect.y == 22 && cmd->clip.rect.h == 9);
    assert(mu_next_command(&ui, &cmd) && cmd->type == MU_COMMAND_TEXT);
    assert(cmd->text.pos.y == 15 && cmd->text.offset_y == 7 && cmd->text.color.a == 153);
    mu_paint_motion(&ui, 0.10f / 0.55f, 0.02f, 0.16f, 8, 0.6f);
    assert(ui.paint_offset_y > 0.99f && ui.paint_offset_y < 1.01f && ui.paint_alpha > 0.9f);
    mu_paint_motion(&ui, 0.20f, 0.02f, 0.16f, 8, 0.6f);
    assert(ui.paint_offset_y > 0 && ui.paint_offset_y < 1);
    ui.time = 0.30f;
    mu_begin(&ui);
    assert(ui.entrance_active); /* The normal 250 ms cutoff scales too. */
    mu_end(&ui);
    mu_input_mousedown(&ui, 15, 20, MU_MOUSE_LEFT);
    assert(!ui.entrance_active && !ui.paint_offset_y && ui.paint_alpha == 1.0f);
    assert(ui.mouse_pressed == MU_MOUSE_LEFT);
    ui.reduced_motion = 1;
    mu_paint_motion(&ui, 0.0f, 0.02f, 0.16f, 8, 0.6f);
    assert(ui.paint_offset_y == 0 && ui.paint_alpha == 1.0f);
}

static int sections(float time, int click_y) {
    ui.time = time;
    if (click_y >= 0) {
        mu_input_mousedown(&ui, 10, click_y, MU_MOUSE_LEFT);
        mu_input_mouseup(&ui, 10, click_y, MU_MOUSE_LEFT);
    }
    mu_begin(&ui);
    assert(mu_begin_window_ex(&ui, "Settings", mu_rect(0, 0, 300, 200), PLAIN));
    int mask = 0;
    for (int i = 0; i < 3; i++) {
        mu_push_id(&ui, &i, sizeof(i));
        if (mu_begin_treenode(&ui, "Same named section")) {
            mask |= 1 << i;
            mu_end_treenode(&ui);
        }
        mu_pop_id(&ui);
    }
    mu_end_window(&ui);
    mu_end(&ui);
    return mask;
}

static void restore_sections(void) {
    init();
    assert(sections(0.0f, -1) == 0);
    sections(0.1f, 10);
    sections(0.2f, 34);
    assert(sections(0.3f, -1) == 3);
    sections(0.4f, 10); /* Explicitly close the first section. */
    assert(sections(0.5f, -1) == 2);
    mu_init_keep_treenodes(&ui);
    assert(!ui.focus && !ui.mouse_down);
    ui.text_width = width;
    ui.text_height = height;
    assert(sections(1.0f, -1) == 2);
    sections(1.1f, 58);
    assert(sections(1.2f, -1) == 6); /* A new section must not evict restored ones. */
}

int main(void) { paint(); scrolling(); restore_sections(); }
'''

with TemporaryDirectory() as tmp:
    source = Path(tmp) / "motion.c"
    executable = Path(tmp) / "motion"
    source.write_text(harness)
    run(["cc", "-std=c99", "-I", str(root / "src"), str(source),
         str(root / "src/microui.c"), "-lm", "-o", str(executable)], check=True)
    run([str(executable)], check=True)
print("Menu entrance geometry, interaction and scroll dynamics passed")
