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
assert 'font_length(size, (char*)chosen) > inner * 1.6f' in source
assert 'outer, outer + 1.5f' not in source  # no blurry black fringe
assert 'ro, ro + 1.2f' not in source     # no double contour
helper = source[source.index('static void text('):source.index('void pie_menu_draw(void) {')]
strings = ', '.join(json.dumps(label) for label in labels)
test = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#define FONT_FIXEDSYS 0
static int lines, dark_ink, light_ink;
static float fit_check, red, green, blue;
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
