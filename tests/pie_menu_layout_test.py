"""Check every pie-menu option fits its wedge after real layout's wrapping.
Run: python3 tests/pie_menu_layout_test.py
"""
from pathlib import Path
import json
import re
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/pie_menu.c').read_text()
labels = []
for match in re.findall(r'\{"[^"]+",\s*(?:true|false),\s*(?:true|false),\s*\{([^}]+)\}\}', source):
    row = re.findall(r'"([^"]+)"', match)
    assert len(row) == 6
    labels.extend(row)
assert len(labels) == (3 + 4 + 2) * 6
assert 'Move Mouse: Choose    G: Next Page' in source
assert 'LMB: Select    RMB: Cancel' in source
assert 'RMB More' not in source
assert '0, 2 * PIEM_PI, 256)' in source  # finer curve than the old 120 steps
assert 'inner, ro, from, to, 48)' in source  # finer slice edges
assert 'pie_text_length(size, chosen) > inner * 1.6f' in source
assert 'outer, outer + 1.5f' not in source  # no blurry black fringe
assert 'ro, ro + 1.2f' not in source     # no double contour
helper = source[source.index('static bool pie_ink_code('):source.index('void pie_menu_draw(void) {')]
strings = ', '.join(json.dumps(label) for label in labels)
test = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define FONT_FIXEDSYS 0
#define PIE_MENU_UTF8_MAX 765
static int lines, dark_ink, light_ink;
static float fit_check, red, green, blue;
/* Colour codes 1 and 2 draw the team colours, as they do in chat. */
static struct { struct { char name[11]; unsigned char red, green, blue; } team_1, team_2; } gamestate;
static void glColor4f(float r,float g,float b,float a) {red=r;green=g;blue=b;(void)a;}
static float font_length(float size,char* s) {return (float)strlen(s) * size * .62f;}
static void font_render(float x,float y,float size,char* s) {
    assert(x==roundf(x) && y==roundf(y)); /* pixel aligned, no subpixel haze */
    assert(font_length(size,s) <= 82.f * fit_check + .001f);
    assert(red==green && red==blue);
    if(red==.06f) dark_ink++;
    if(red==1.f) light_ink++;
    lines++;
}
''' + helper + r'''
int main(void) {
    const char* labels[] = {''' + strings + r'''};
    for(int f=0;f<2;f++) {
        fit_check = f ? .68f : 1.f;
        for(unsigned i=0;i<sizeof(labels)/sizeof(*labels);i++) {
            lines=0;
            slice_label(0,0,labels[i],1.f,fit_check,0.f);
            assert(lines >= 2 && lines <= 4 && !(lines%2));
            lines=0;
            slice_label(0,0,labels[i],1.f,fit_check,1.f);
            assert(lines >= 2 && lines <= 4 && !(lines%2));
        }
    }
    assert(dark_ink>0 && light_ink>0); /* white text on dark; dark on selected */
    puts("Pie menu wedge-label/contrast tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp) / 'layout.c'
    exe = Path(tmp) / 'layout'
    file.write_text(test)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(file), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# A server's Theme, Label and Tooltip are drawn the way chat is, colour codes
# included: codes 1-7 steer the ink and never reach the font, which has no
# glyph for them and would draw a '?'.
color_test = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#define PIE_MENU_UTF8_MAX 765
static float red, green, blue;
static char drawn[16][64];
static float drawn_r[16], drawn_g[16], drawn_b[16];
static int drawn_n;
/* Codes 1 and 2 draw the team colours, as they do in chat. */
static struct { struct { char name[11]; unsigned char red, green, blue; } team_1, team_2; } gamestate;
static void glColor4f(float r, float g, float b, float a) { red=r; green=g; blue=b; (void)a; }
static float font_length(float size, char* s) { return (float)strlen(s) * size * .62f; }
static void font_render(float x, float y, float size, char* s) {
    (void)x; (void)y; (void)size;
    for(const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        assert(!(c >= 1 && c <= 7) && c != 0xff); /* a code would draw as '?' */
    }
    assert(drawn_n < 16);
    snprintf(drawn[drawn_n], sizeof drawn[0], "%s", s);
    drawn_r[drawn_n] = red; drawn_g[drawn_n] = green; drawn_b[drawn_n] = blue;
    drawn_n++;
}
''' + helper + r'''
int main(void) {
    gamestate.team_1.red = 10; gamestate.team_1.green = 20; gamestate.team_1.blue = 30;
    /* One shadow for the whole line, then a run per colour. */
    drawn_n = 0;
    text(0, 0, 10.f, "\x04Red\x03White", 1.f, 0.f);
    assert(drawn_n == 3);
    assert(!strcmp(drawn[0], "RedWhite"));                 /* the shadow */
    assert(drawn_r[0] == 0.f && drawn_g[0] == 0.f);
    assert(!strcmp(drawn[1], "Red"));
    assert(drawn_r[1] == 1.f && drawn_g[1] == 0.f && drawn_b[1] == 0.f);
    assert(!strcmp(drawn[2], "White"));
    assert(drawn_r[2] == 1.f && drawn_g[2] == 1.f && drawn_b[2] == 1.f);

    /* Codes steer the ink, so they must not widen the line. */
    assert(pie_text_length(10.f, "\x04Red\x03White") == pie_text_length(10.f, "RedWhite"));

    /* On a highlighted wedge plain ink goes near black; a colour follows it. */
    drawn_n = 0;
    text(0, 0, 10.f, "\x05Green", 1.f, 1.f);
    assert(drawn_n == 2);
    assert(drawn_g[1] == .06f && drawn_r[1] == 0.f);

    /* 1 and 2 are the team colours. */
    drawn_n = 0;
    text(0, 0, 10.f, "\x01Us", 1.f, 0.f);
    assert(drawn_n == 2);
    assert(drawn_r[1] == 10.f / 255.f && drawn_g[1] == 20.f / 255.f && drawn_b[1] == 30.f / 255.f);

    /* The UTF-8 marker is not a glyph either. */
    drawn_n = 0;
    text(0, 0, 10.f, "\xff\xc3\xa9lan", 1.f, 0.f);
    assert(drawn_n == 2);
    assert(!strcmp(drawn[1], "\xc3\xa9lan"));

    /* Uncoloured text is unchanged: shadow, then one white run. */
    drawn_n = 0;
    text(0, 0, 10.f, "Plain", 1.f, 0.f);
    assert(drawn_n == 2);
    assert(!strcmp(drawn[1], "Plain"));
    assert(drawn_r[1] == 1.f && drawn_g[1] == 1.f);

    /* The wedge-label path strips codes before it measures, so a coloured
       label is laid out by its text alone and never wraps on a code. */
    drawn_n = 0;
    slice_label(0, 0, "\x04Nuke", 1.f, 1.f, 0.f);
    assert(drawn_n == 2);
    assert(!strcmp(drawn[1], "Nuke"));
    assert(drawn_r[1] == 1.f && drawn_g[1] == 0.f && drawn_b[1] == 0.f);
    assert(pie_text_length(10.f, "\x04Nuke") == pie_text_length(10.f, "Nuke"));
    puts("Pie menu colour-code tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp) / 'color.c'
    exe = Path(tmp) / 'color'
    file.write_text(color_test)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(file), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
