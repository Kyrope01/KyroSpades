"""Headless coverage for menu surface geometry and button press feedback."""
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
static unsigned char pixels[32][48];
static int width(mu_Font f, const char *s, int n) {
    (void)f;
    return (n < 0 ? (int)strlen(s) : n) * 6;
}
static int height(mu_Font f) { (void)f; return 10; }

static void strip(void *data, float x, float y, float w, float h, mu_Color color) {
    mu_Rect *clip = data;
    assert(x >= clip->x && x + w <= clip->x + clip->w);
    assert(y >= clip->y && y + h <= clip->y + clip->h);
    assert(w > 0 && h > 0);
    for (int row = y; row < y + h; row++)
        for (int col = clip->x; col < clip->x + clip->w; col++)
            if (col + 0.5f >= x && col + 0.5f < x + w) {
                assert(!pixels[row][col]); /* no double blending */
                pixels[row][col] = color.r;
            }
}

static void shape(int w, int h, int top_only, mu_Rect clip) {
    memset(pixels, 0, sizeof(pixels));
    mu_begin(&ui);
    assert(mu_begin_window_ex(&ui, "surface", mu_rect(0, 0, 48, 32),
        MU_OPT_NOTITLE | MU_OPT_NOFRAME | MU_OPT_NORESIZE | MU_OPT_NOSCROLL));
    mu_push_clip_rect(&ui, clip);
    mu_draw_surface(&ui, mu_rect(0, 0, w, h), mu_color(1, 0, 0, 128),
        mu_color(2, 0, 0, 128), 6, top_only);
    mu_pop_clip_rect(&ui);
    mu_end_window(&ui);
    mu_end(&ui);
    mu_Command *cmd = NULL;
    assert(mu_next_command(&ui, &cmd) && cmd->type == MU_COMMAND_SURFACE);
    assert(cmd->surface.rect.h == h);
    assert(cmd->surface.radius <= w / 2 && cmd->surface.radius <= h / 2);
    mu_render_surface(&cmd->surface, strip, &clip);
    assert(!mu_next_command(&ui, &cmd));
}

static float button_offset;
static mu_Rect button_frame(float time, int options, int submit) {
    ui.time = time;
    mu_begin(&ui);
    assert(mu_begin_window_ex(&ui, "test", mu_rect(0, 0, 120, 80),
        MU_OPT_NOTITLE | MU_OPT_NOFRAME | MU_OPT_NORESIZE | MU_OPT_NOSCROLL));
    mu_layout_row(&ui, 1, (int[]){80}, 22);
    assert(!!(mu_button_ex(&ui, "Apply", 0, options) & MU_RES_SUBMIT) == submit);
    mu_end_window(&ui);
    mu_end(&ui);
    mu_Command *cmd = NULL;
    mu_Rect result = mu_rect(0, 0, 0, 0);
    while (mu_next_command(&ui, &cmd))
        if (cmd->type == MU_COMMAND_SURFACE) {
            result = cmd->surface.rect;
            button_offset = cmd->surface.offset_y;
        }
    return result;
}

int main(void) {
    mu_init(&ui);
    ui.text_width = width;
    ui.text_height = height;
    shape(40, 20, 0, mu_rect(0, 0, 40, 20));
    assert(pixels[0][0] == 0 && pixels[19][0] == 0);
    assert(pixels[10][0] == 2 && pixels[10][20] == 1);
    shape(40, 20, 1, mu_rect(0, 0, 40, 20));
    assert(pixels[0][0] == 0 && pixels[19][0] == 2);
    shape(40, 20, 0, mu_rect(0, 5, 20, 10));
    assert(pixels[5][0] == 2 && pixels[5][19] == 1 && pixels[4][10] == 0);
    shape(1, 2, 0, mu_rect(0, 0, 1, 2));
    assert(pixels[0][0] == 2 && pixels[1][0] == 2);

    mu_input_mousemove(&ui, 5, 5);
    button_frame(0, 0, 0); /* prime the root for hover */
    mu_Rect rest = button_frame(0.01f, 0, 0);
    assert(rest.w == 80 && rest.h == 22);
    /* Click at the top-left pixel, which the pressed paint rect won't cover. */
    mu_input_mousedown(&ui, rest.x, rest.y, MU_MOUSE_LEFT);
    mu_Rect pressed = button_frame(0.02f, 0, 1);
    assert(pressed.y == rest.y && pressed.w == rest.w && button_offset == 2);
    assert(button_frame(0.5f, 0, 0).y == pressed.y && button_offset == 2);
    mu_input_mouseup(&ui, rest.x, rest.y, MU_MOUSE_LEFT);
    assert(button_frame(0.52f, 0, 0).y == pressed.y && button_offset == 2);
    button_frame(0.62f, 0, 0);
    assert(button_offset > 0 && button_offset < 1 && button_offset != (int)button_offset);
    button_frame(0.75f, 0, 0);
    assert(button_offset == 0);
    mu_input_mousedown(&ui, rest.x, rest.y, MU_MOUSE_LEFT);
    assert(button_frame(0.76f, MU_OPT_NOFRAME, 1).w == 0);
    assert(ui.press_time == 0.52f); /* flat cells do not restart the animation */
}
'''

with TemporaryDirectory() as tmp:
    source = Path(tmp) / "ui.c"
    executable = Path(tmp) / "ui"
    source.write_text(harness)
    run(["cc", "-std=c99", "-I", str(root / "src"), str(source),
         str(root / "src/microui.c"), "-lm", "-o", str(executable)], check=True)
    run([str(executable)], check=True)
print("Menu surface geometry and press feedback passed")
