"""Compile the real Pie Menu v1 parser and check it against the wire spec.

Spec: zerospades/aosprotocol extensions/pie-menu.md, as merged in 7449ddd.
Run: python3 tests/pie_menu_ext_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pie_menu_ext.h"

static int warnings, notices;
void log_log(int level, const char* file, int line, const char* fmt, ...) {
    (void)file; (void)line; (void)fmt;
    if(level >= 3) warnings++;   /* LOG_WARN and up: a refused menu */
    else notices++;
}

/* A string on the wire is NUL terminated. */
static size_t put(uint8_t* p, size_t n, const char* s) {
    size_t i = 0;
    while(s[i]) { p[n + i] = (uint8_t)s[i]; i++; }
    p[n + i] = 0;
    return i + 1;
}
typedef struct { uint8_t flags; const char *text, *label, *tooltip; } SliceSpec;
static size_t put_slice(uint8_t* p, size_t n, SliceSpec s) {
    p[n++] = 0; p[n++] = s.flags;                      /* Message ID is reserved */
    n += put(p, n, s.text);
    n += put(p, n, s.label);
    n += put(p, n, s.tooltip);
    return n;
}
static size_t put_pie(uint8_t* p, size_t n, uint8_t contexts, const char* theme, SliceSpec s[6]) {
    p[n++] = contexts; p[n++] = 0;                     /* Theme Message ID reserved */
    n += put(p, n, theme);
    for(int i = 0; i < PIE_MENU_SLICES; i++) n = put_slice(p, n, s[i]);
    return n;
}
static SliceSpec chat(const char* text, const char* label, const char* tooltip) {
    SliceSpec s = {0, text, label, tooltip};
    return s;
}
/* A blank slice: Text and Label both empty, so it draws nothing. */
static SliceSpec blank(void) { SliceSpec s = {0, "", "", ""}; return s; }
/* One pie of six identical slices, so a case only names what it tests. */
static size_t menu_one(uint8_t* p, uint8_t contexts, const char* theme, SliceSpec s) {
    SliceSpec six[6];
    for(int i = 0; i < 6; i++) six[i] = s;
    p[0] = 0; p[1] = 1;                                /* Sub ID 0, one pie */
    return put_pie(p, 2, contexts, theme, six);
}
/* One pie whose first slice carries the given strings and the rest are blank. */
static size_t menu_raw(uint8_t* p, const char* text, const char* label, const char* tooltip) {
    SliceSpec six[6];
    for(int i = 0; i < 6; i++) six[i] = blank();
    six[0] = (SliceSpec){0, text, label, tooltip};
    p[0] = 0; p[1] = 1;
    return put_pie(p, 2, PIE_CONTEXT_BLOCK, "", six);
}

int main(void) {
    uint8_t pkt[4096];
    assert(PIE_MENU_EXT_ID == 0x31 && PIE_MENU_PACKET_ID == 0x71);

    /* Before negotiation a server's Menu is not read at all. */
    size_t n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Social", chat("Hi!", "Hi", ""));
    assert(!pie_menu_ext_receive(pkt, n) && !pie_menu_ext_owner());
    pie_menu_ext_set_negotiated(true);
    assert(pie_menu_ext_negotiated() && !pie_menu_ext_owner());

    /* A Menu of one pie: contexts, theme, and every field of a slice. */
    SliceSpec six[6] = {
        chat("/apoc", "Nuke", "Calls in an airstrike"),
        {PIE_FLAG_GLOBAL, "/pingmark %h Enemy", "", ""},
        {0, "/place %r", "Place", ""},
        {0, "", "Airstrikes: 2", ""},                  /* no Text, so the Label draws */
        {0, "", "", "a tooltip alone"},                /* blank whatever its Tooltip */
        {0xff, "flagged", "", ""},                     /* GLOBAL kept, bits 1-7 ignored */
    };
    pkt[0] = 0; pkt[1] = 2;
    size_t len = put_pie(pkt, 2, PIE_CONTEXT_BLOCK | PIE_CONTEXT_TEAMMATE, "Build", six);
    len = put_pie(pkt, len, PIE_CONTEXT_ENEMY | PIE_CONTEXT_SKY | 0xc0, "Taunt", six);
    assert(pie_menu_ext_receive(pkt, len));             /* bits 6-7 ignored */
    assert(pie_menu_ext_owner() && pie_menu_ext_count() == 2);
    const Pie* world = pie_menu_ext_pie(0);
    assert(world && world->contexts == (PIE_CONTEXT_BLOCK | PIE_CONTEXT_TEAMMATE));
    assert(!strcmp(world->theme, "Build"));
    assert(!world->slice[0].flags);
    assert(!strcmp(world->slice[0].text, "/apoc") && !world->slice[0].text_utf8);
    assert(!strcmp(world->slice[0].label, "Nuke"));
    assert(!strcmp(world->slice[0].tooltip, "Calls in an airstrike"));
    assert(world->slice[1].flags & PIE_FLAG_GLOBAL);
    assert(!strcmp(world->slice[1].label, "/pingmark %h Enemy")); /* an empty Label draws the Text */
    assert(!strcmp(world->slice[2].text, "/place %r"));
    assert(!strcmp(world->slice[3].label, "Airstrikes: 2"));
    /* Blank is Text and Label both empty, whatever the Tooltip says. */
    assert(!world->slice[4].text[0] && !world->slice[4].label[0]);
    assert(!strcmp(world->slice[4].tooltip, "a tooltip alone"));
    assert(world->slice[5].flags == PIE_FLAG_GLOBAL);
    assert(pie_menu_ext_pie(1)->contexts == (PIE_CONTEXT_ENEMY | PIE_CONTEXT_SKY));
    assert(!pie_menu_ext_pie(2) && !pie_menu_ext_pie(-1));

    /* Menus are never merged: a new one replaces the old one entirely. */
    n = menu_one(pkt, PIE_CONTEXT_DEAD, "Spawn", chat("/spawn base", "Base", ""));
    assert(pie_menu_ext_receive(pkt, n) && pie_menu_ext_count() == 1);
    assert(!strcmp(pie_menu_ext_pie(0)->theme, "Spawn"));
    assert(pie_menu_ext_pie(0)->contexts == PIE_CONTEXT_DEAD);

    /* A Pie Count of 0 leaves no menu, and the client's own does not come back. */
    pkt[0] = 0; pkt[1] = 0;
    assert(pie_menu_ext_receive(pkt, 2) && pie_menu_ext_owner() && pie_menu_ext_count() == 0);
    assert(!pie_menu_ext_pie(0));

    /* Refusals leave the previous menu standing and are logged, not fatal. */
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Kept", chat("kept", "Kept", ""));
    assert(pie_menu_ext_receive(pkt, n) && pie_menu_ext_count() == 1);
    int before = warnings;
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Bad", chat("bad", "Bad", ""));
    pkt[n] = 'x';                                       /* bytes remain after the last pie */
    assert(!pie_menu_ext_receive(pkt, n + 1) && warnings == before + 1);
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Bad", chat("bad", "Bad", "")) - 1;
    assert(!pie_menu_ext_receive(pkt, n));              /* the last string runs past the end */
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Bad", chat("bad", "Bad", ""));
    pkt[n - 1] = 0;                                     /* drop the slice's tooltip NUL */
    assert(!pie_menu_ext_receive(pkt, n - 1));
    assert(warnings == before + 3);
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Bad", chat("bad", "Bad", ""));
    pkt[2] = PIE_CONTEXT_BLOCK; pkt[3] = 0;
    size_t at = 4;
    pkt[at++] = 0xff; pkt[at++] = 0xc3; pkt[at] = 0;    /* 0xc3 alone is not UTF-8 */
    assert(!pie_menu_ext_receive(pkt, n));
    assert(pie_menu_ext_count() == 1 && !strcmp(pie_menu_ext_pie(0)->theme, "Kept"));
    assert(warnings == before + 4);
    /* An unknown sub-packet is ignored, not an error. */
    pkt[0] = 9;
    int noticed = notices;
    assert(pie_menu_ext_receive(pkt, 4) && warnings == before + 4 && notices == noticed + 1);
    assert(pie_menu_ext_count() == 1);

    /* A 0xff string is UTF-8 and is stored without the prefix. */
    n = menu_raw(pkt, "\xff" "\xc3\xa9t\xc3\xa9", "", "");
    assert(pie_menu_ext_receive(pkt, n));
    assert(pie_menu_ext_pie(0)->slice[0].text_utf8);
    assert(!strcmp(pie_menu_ext_pie(0)->slice[0].text, "\xc3\xa9t\xc3\xa9"));
    assert(!strcmp(pie_menu_ext_pie(0)->theme, ""));    /* an empty Theme is none */

    /* A Code Page 437 Label is decoded for drawing, its Text is kept as sent. */
    n = menu_raw(pkt, "\x82t\x82", "\x82t\x82", "");
    assert(pie_menu_ext_receive(pkt, n));
    const PieSlice* cp = &pie_menu_ext_pie(0)->slice[0];
    assert(!cp->text_utf8 && !strcmp(cp->text, "\x82t\x82"));
    assert(!strcmp(cp->label, "\xc3\xa9t\xc3\xa9"));

    /* An empty Label draws the Text, and the Text is kept as the server sent
       it -- so the drawn copy must be decoded too, or the same bytes come out
       as mojibake here and as "été" one slice over. Note the receive above
       freed the menu `cp` points into, so nothing here may read it. */
    n = menu_raw(pkt, "\x82t\x82", "", "");
    assert(pie_menu_ext_receive(pkt, n));
    const PieSlice* fb = &pie_menu_ext_pie(0)->slice[0];
    assert(!fb->text_utf8 && !strcmp(fb->text, "\x82t\x82")); /* still sent as sent */
    assert(!strcmp(fb->label, "\xc3\xa9t\xc3\xa9"));          /* but drawn decoded */

    /* A Text longer than the client's cap is cut, never mid-codepoint. */
    static char long_text[PIE_MENU_TEXT_MAX + 40];
    memset(long_text, 'a', sizeof long_text - 1);
    long_text[sizeof long_text - 1] = 0;
    n = 2;
    pkt[n++] = PIE_CONTEXT_BLOCK; pkt[n++] = 0;
    pkt[n++] = 0;
    SliceSpec wide = chat(long_text, "", "");
    n = put_slice(pkt, n, wide);
    for(int i = 1; i < 6; i++) n = put_slice(pkt, n, blank());
    assert(pie_menu_ext_receive(pkt, n));
    assert(strlen(pie_menu_ext_pie(0)->slice[0].text) == PIE_MENU_TEXT_MAX);

    /* Substitutions: %p, %b, %r, %% and a reserved token left as it is. */
    PieAim aim = {.player = 7, .has_block = true, .block_x = 257, .block_y = 298, .block_z = 40,
                  .ray_x = 255.5f, .ray_y = 300.125f, .ray_z = 40.25f,
                  .dir_x = 0.7071f, .dir_y = -0.7071f, .dir_z = 0.f,
                  .hit = true, .hit_x = 256.37f, .hit_y = 298.02f, .hit_z = 40.f};
    char out[512];
    pie_menu_ext_expand("/votekick %p at %b ray %r 100%%", &aim, out, sizeof out);
    assert(!strcmp(out, "/votekick #7 at 257 298 40 ray 255.50 300.12 40.25 0.7071 -0.7071 0.0000 100%"));
    /* %h is the point the aim met, and is what /pingmark takes. */
    pie_menu_ext_expand("/pingmark %h Enemy", &aim, out, sizeof out);
    assert(!strcmp(out, "/pingmark 256.37 298.02 40.00 Enemy"));
    /* An empty token leaves its argument missing. */
    PieAim bare = {.player = -1};
    pie_menu_ext_expand("/votekick %p", &bare, out, sizeof out);
    assert(!strcmp(out, "/votekick "));
    pie_menu_ext_expand("/dig %b", &bare, out, sizeof out);
    assert(!strcmp(out, "/dig "));
    /* Nothing aimed at, so %h is empty and /pingmark gets no position. */
    pie_menu_ext_expand("/pingmark %h Enemy", &bare, out, sizeof out);
    assert(!strcmp(out, "/pingmark  Enemy"));
    /* %r is never empty, and a % followed by anything else stays as it is. */
    pie_menu_ext_expand("%q%z%", &bare, out, sizeof out);
    assert(!strcmp(out, "%q%z%"));
    pie_menu_ext_expand("%r", &bare, out, sizeof out);
    assert(!strcmp(out, "0.00 0.00 0.00 0.0000 0.0000 0.0000"));
    /* The result is expanded once and never re-scanned. */
    pie_menu_ext_expand("%%p %p", &aim, out, sizeof out);
    assert(!strcmp(out, "%p #7"));
    /* A cut lands on a codepoint boundary. */
    pie_menu_ext_expand("\xc3\xa9\xc3\xa9\xc3\xa9\xc3\xa9", &bare, out, 5);
    assert(!strcmp(out, "\xc3\xa9\xc3\xa9"));
    pie_menu_ext_expand(NULL, &bare, out, sizeof out);
    assert(!strcmp(out, ""));

    /* A menu replaced while one is open keeps the pies it was opened on. */
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "First", chat("first", "First", ""));
    assert(pie_menu_ext_receive(pkt, n));
    pie_menu_ext_freeze();
    const Pie* held = pie_menu_ext_pie(0);
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Second", chat("second", "Second", ""));
    assert(pie_menu_ext_receive(pkt, n));
    assert(!strcmp(pie_menu_ext_pie(0)->theme, "Second"));
    assert(!strcmp(held->theme, "First"));
    pie_menu_ext_thaw();
    n = menu_one(pkt, PIE_CONTEXT_BLOCK, "Third", chat("third", "Third", ""));
    assert(pie_menu_ext_receive(pkt, n));
    assert(!strcmp(pie_menu_ext_pie(0)->theme, "Third"));

    /* The menu belongs to the connection: only a new connection clears it. */
    pie_menu_ext_reset_connection();
    assert(!pie_menu_ext_negotiated() && !pie_menu_ext_owner() && !pie_menu_ext_count());
    pie_menu_ext_set_negotiated(true);
    assert(!pie_menu_ext_owner());
    puts("Pie Menu v1 packet, validation, substitution and lifecycle tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'pie_menu_ext_test.c'
    exe = Path(d) / 'pie_menu_ext_test'
    c.write_text(harness)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src'), str(c),
                    str(root / 'src/pie_menu_ext.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The dispatch wiring in network.c cannot be compiled here without SDL/GL/ENet,
# so extract the handler and check it forwards the payload untouched.
network = (root / 'src/network.c').read_text()
handler = network[network.index('static void read_PacketPieMenu('):
                  network.index('\n}', network.index('static void read_PacketPieMenu(')) + 2]
assert 'packets[PIE_MENU_PACKET_ID] = read_PacketPieMenu;' in network
# The rest of the wiring cannot be compiled here (network.c needs SDL/GL/ENet),
# so check it structurally: the header is included, and every place that drops
# one extension's connection state drops the pie menu's too.
assert '#include "pie_menu_ext.h"' in network
assert network.count('pie_menu_ext_reset_connection();') == network.count('flashlight_ext_reset_connection();')
assert 'pie_menu_ext_set_negotiated(supports_pie_menu);' in network
assert 'PIE_MENU_EXT_ID, 1 };' in network
dispatch = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static const uint8_t* got;
static size_t got_len;
static bool pie_menu_ext_receive(const uint8_t* data, size_t len) { got = data; got_len = len; return true; }
''' + handler + r'''
int main(void) {
    uint8_t payload[] = {0, 1, 1, 0, 0};
    read_PacketPieMenu(payload, (int)sizeof payload);
    assert(got == payload && got_len == sizeof payload);
    got = NULL;
    read_PacketPieMenu(NULL, 5);
    read_PacketPieMenu(payload, 0);
    assert(!got);
    puts("Pie Menu packet dispatch tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'dispatch.c'
    exe = Path(d) / 'dispatch'
    c.write_text(dispatch)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# The example packets in the docs must actually parse, and mean what their
# captions say, or the documentation rots the moment the parser changes.
import re

doc = (root / 'docs/pie-menu-v1.md').read_text()
packets = []
for block in re.findall(r'((?:^[ \t]+(?:[0-9a-f]{2} )*[0-9a-f]{2}[ \t]*$\n?)+)', doc, re.MULTILINE):
    hexes = block.split()
    if hexes and hexes[0] == '71':
        packets.append(bytes(int(x, 16) for x in hexes))
assert len(packets) == 4, f'expected 4 documented packets, found {len(packets)}'
assert [len(p) for p in packets] == [3, 47, 49, 111]

arrays = '\n'.join(
    '    static const uint8_t p%d[] = {%s};' % (i, ','.join(str(b) for b in p))
    for i, p in enumerate(packets))
docs = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "pie_menu_ext.h"
void log_log(int level, const char* file, int line, const char* fmt, ...) {
    (void)level; (void)file; (void)line; (void)fmt;
}
int main(void) {
''' + arrays + r'''
    /* network.c hands the handler the payload after the top-level id. */
    pie_menu_ext_set_negotiated(true);
    assert(pie_menu_ext_receive(p0 + 1, sizeof p0 - 1));
    assert(pie_menu_ext_owner() && pie_menu_ext_count() == 0);

    assert(pie_menu_ext_receive(p1 + 1, sizeof p1 - 1) && pie_menu_ext_count() == 1);
    const Pie* social = pie_menu_ext_pie(0);
    assert(social->contexts == PIE_CONTEXT_BLOCK && !strcmp(social->theme, "Social"));
    assert(!social->slice[0].flags);
    assert(!strcmp(social->slice[0].text, "Hi!") && !strcmp(social->slice[0].label, "Hi"));
    assert(!strcmp(social->slice[0].tooltip, ""));
    for(int i = 1; i < PIE_MENU_SLICES; i++)
        assert(!social->slice[i].text[0] && !social->slice[i].label[0]);

    assert(pie_menu_ext_receive(p2 + 1, sizeof p2 - 1) && pie_menu_ext_count() == 1);
    const Pie* dead = pie_menu_ext_pie(0);
    assert(dead->contexts == PIE_CONTEXT_DEAD && !strcmp(dead->theme, "Dead"));
    assert((dead->slice[0].flags & PIE_FLAG_GLOBAL) && !strcmp(dead->slice[0].text, "hello all"));
    assert(!strcmp(dead->slice[0].label, "hello all"));  /* an empty Label draws the Text */

    assert(pie_menu_ext_receive(p3 + 1, sizeof p3 - 1) && pie_menu_ext_count() == 1);
    const Pie* build = pie_menu_ext_pie(0);
    assert(build->contexts == PIE_CONTEXT_BLOCK && !strcmp(build->theme, "Build"));
    assert(!strcmp(build->slice[0].text, "/place %r"));
    assert(!strcmp(build->slice[0].label, "Place"));
    assert(!strcmp(build->slice[0].tooltip, "Builds where you look"));
    /* No Text, so the Label draws and the slice sends nothing but itself. */
    assert(!build->slice[1].text[0] && !strcmp(build->slice[1].label, "Airstrikes: 2"));
    assert(!strcmp(build->slice[2].text, "/pingmark %h Enemy"));
    assert(!strcmp(build->slice[2].label, "Mark"));
    for(int i = 3; i < PIE_MENU_SLICES; i++)
        assert(!build->slice[i].text[0] && !build->slice[i].label[0]);
    puts("Documented example packets parse as documented");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'docs.c'
    exe = Path(d) / 'docs'
    c.write_text(docs)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', '-I' + str(root / 'src'), str(c),
                    str(root / 'src/pie_menu_ext.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
