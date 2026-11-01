"""Daytime and Weather (aosprotocol extension 0x33, packet 0x73) regression checks.

The wire parser and the connection state machine are exercised by compiling
`src/daytime_weather.c` against small stubs, so a change to the packet layout or
to the day/night rules fails here instead of on a server. The wiring into the
packet table, the render paths and the demo recorder is checked against the
sources.
"""
import subprocess
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
network_c = (root / 'src/network.c').read_text()
network_h = (root / 'src/network.h').read_text()
lighting = (root / 'src/lighting.c').read_text()
map_c = (root / 'src/map.c').read_text()
main_c = (root / 'src/main.c').read_text()
hud_c = (root / 'src/hud.c').read_text()
demo_c = (root / 'src/demo.c').read_text()
header = (root / 'src/daytime_weather.h').read_text()

HARNESS = r'''
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "common.h"
#include "daytime_weather.h"

static float fake_now;
static int world_supported;
static int demo_playing;
static int rebuilds;
static int warns;

float window_time(void) { return fake_now; }
void chunk_rebuild_all(void) { ++rebuilds; }
bool lighting_world_supported(void) { return world_supported != 0; }
bool demo_is_playing(void) { return demo_playing != 0; }
void log_log(int level, const char *file, int line, const char *fmt, ...) {
        (void)file; (void)line; (void)fmt;
        if(level == LOG_WARN) ++warns;
}

static int failures;
#define CHECK(cond) do { \
        if(!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } \
} while(0)

int main(void) {
        uint8_t night[1 + DAYTIME_WEATHER_SKY_BYTES] = { DAYTIME_WEATHER_SUB_SKY, 0, 0, 0, 0, 0, 0 };
        uint8_t noon[1 + DAYTIME_WEATHER_SKY_BYTES] = { DAYTIME_WEATHER_SUB_SKY, 0xD0, 0x02, 0, 0, 0, 0 };
        uint8_t big[1 + DAYTIME_WEATHER_SKY_BYTES] = { DAYTIME_WEATHER_SUB_SKY, 0xFF, 0xFF, 0, 0, 0, 0 };
        uint8_t sub1[1 + DAYTIME_WEATHER_SKY_BYTES] = { 1, 0, 0, 0, 0, 0, 0 };
        uint8_t encoded[2 + DAYTIME_WEATHER_SKY_BYTES];
        int time = -12345;

        /* --- wire layout ------------------------------------------------- */
        CHECK(DAYTIME_WEATHER_PACKET_ID == 0x73);
        CHECK(daytime_weather_parse_sky(night, sizeof night, &time) == 1 && time == 0);
        CHECK(daytime_weather_parse_sky(noon, sizeof noon, &time) == 1 && time == 720);
        CHECK(daytime_weather_parse_sky(big, sizeof big, &time) == 1 && time == 0xFFFF);
        CHECK(daytime_weather_parse_sky(sub1, sizeof sub1, &time) == 0);
        CHECK(daytime_weather_parse_sky(night, 1, &time) == 0);
        CHECK(daytime_weather_parse_sky(night, 6, &time) == 0);   /* one byte short */
        CHECK(daytime_weather_parse_sky(NULL, 7, &time) == 0);

        /* --- encoding round-trips into what the parser reads ------------- */
        CHECK(daytime_weather_encode_sky(encoded, sizeof encoded, 1) == sizeof encoded);
        CHECK(encoded[0] == DAYTIME_WEATHER_PACKET_ID);
        CHECK(daytime_weather_parse_sky(encoded + 1, (int)sizeof encoded - 1, &time) == 1 && time == 0);
        CHECK(daytime_weather_encode_sky(encoded, sizeof encoded, 0) == sizeof encoded);
        CHECK(daytime_weather_parse_sky(encoded + 1, (int)sizeof encoded - 1, &time) == 1 && time == 720);
        CHECK(daytime_weather_encode_sky(encoded, 4, 1) == 0);

        /* --- nothing negotiated: nothing changes ------------------------ */
        daytime_weather_reset_connection();
        fake_now = 10.0F;
        CHECK(daytime_weather_negotiated() == 0);
        CHECK(daytime_weather_sunlight() == 1.0F);
        CHECK(daytime_weather_awaiting_sky() == 0);
        daytime_weather_receive(night, sizeof night);
        CHECK(daytime_weather_is_night() == 0);

        /* --- negotiated: the world waits for the first Sky -------------- */
        daytime_weather_set_negotiated(1);
        CHECK(daytime_weather_negotiated() == 1);
        daytime_weather_world_entered();
        CHECK(daytime_weather_awaiting_sky() == 1);
        CHECK(daytime_weather_sunlight() == 1.0F);   /* unknown reads as day */

        fake_now += 3.5F;                            /* server never sends one */
        CHECK(daytime_weather_awaiting_sky() == 0);
        CHECK(warns == 1);

        /* --- the Sky itself --------------------------------------------- */
        daytime_weather_reset_world();
        daytime_weather_world_entered();
        daytime_weather_receive(night, sizeof night);
        CHECK(daytime_weather_awaiting_sky() == 0);
        CHECK(daytime_weather_is_night() == 1);
        CHECK(daytime_weather_sunlight() == 0.0F);
        CHECK(rebuilds == 1);                        /* baked paths re-light */

        daytime_weather_receive(noon, sizeof noon);
        CHECK(daytime_weather_is_night() == 0);
        CHECK(daytime_weather_sunlight() == 1.0F);
        CHECK(rebuilds == 2);

        /* A shader-lighting world does not need a remesh. */
        world_supported = 1;
        daytime_weather_receive(night, sizeof night);
        CHECK(rebuilds == 2);

        /* Truncated and unknown sub-packets leave the time alone. */
        daytime_weather_receive(night, 3);
        CHECK(daytime_weather_is_night() == 1);
        daytime_weather_receive(sub1, sizeof sub1);
        CHECK(daytime_weather_is_night() == 1);

        /* --- the Time belongs to the connection, not the world ---------- */
        daytime_weather_reset_world();
        CHECK(daytime_weather_negotiated() == 1);
        CHECK(daytime_weather_is_night() == 0);
        CHECK(daytime_weather_awaiting_sky() == 0);  /* no clock started */

        /* --- a demo never holds the world ------------------------------- */
        daytime_weather_set_negotiated(1);
        demo_playing = 1;
        daytime_weather_world_entered();
        CHECK(daytime_weather_awaiting_sky() == 0);
        demo_playing = 0;
        daytime_weather_world_entered();
        CHECK(daytime_weather_awaiting_sky() == 1);

        /* --- disconnect forgets everything ------------------------------ */
        daytime_weather_receive(night, sizeof night);
        daytime_weather_reset_connection();
        CHECK(daytime_weather_negotiated() == 0);
        CHECK(daytime_weather_sunlight() == 1.0F);

        if(failures) { printf("%d failure(s)\n", failures); return 1; }
        printf("daytime and weather harness passed\n");
        return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    work = Path(directory)
    source = work / 'harness.c'
    source.write_text(HARNESS)
    exe = work / 'harness'
    subprocess.run(['cc', '-std=gnu99', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-ffunction-sections', '-fdata-sections',
                    '-I', str(root / 'src'), '-I', str(root / 'deps'),
                    '-I', str(root / 'deps/libvxl'), '-I', str(root / 'tools/bench/stubs'),
                    str(root / 'src/daytime_weather.c'), str(source),
                    '-Wl,--gc-sections', '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print('Daytime and Weather state machine verified')

# ── wiring: negotiation, dispatch, rendering, demos, build files ────────────
assert 'EXT_DAYTIME_WEATHER = 0x33' in network_h
assert 'bool supports_daytime_weather = false;' in network_c
assert 'p->entries[k].version >= DAYTIME_WEATHER_VERSION' in network_c
assert 'daytime_weather_set_negotiated(supports_daytime_weather);' in network_c
assert 'reply.length = 4 + (supports_teamplay ? 1 : 0)' in network_c
assert '+ (supports_daytime_weather ? 1 : 0);' in network_c
assert 'daytime_weather_set_negotiated(supports_daytime_weather);' in network_c
assert '(struct PacketExtInfoEntry){ EXT_DAYTIME_WEATHER, DAYTIME_WEATHER_VERSION }' in network_c
assert 'packets[DAYTIME_WEATHER_PACKET_ID] = read_PacketDaytimeWeather;' in network_c
assert network_c.count('daytime_weather_reset_connection();') == 4  # init, connect, disconnect, kick

# The sun and the fog both follow the time of day; dynamic lights do not.
assert '* daytime_weather_sunlight();' in lighting
assert lighting.count('daytime_weather_sunlight()') == 1
assert '* daytime_weather_sunlight();' in map_c

# The world is held for the first Sky, and no shadow pass runs at night.
assert 'bool daytime_weather_holds_world = daytime_weather_awaiting_sky() != 0;' in main_c
assert 'if(!network_map_transfer && !daytime_weather_holds_world) {' in main_c
assert 'if(!daytime_weather_is_night())\n                                shadow_render();' in main_c
assert main_c.index('daytime_weather_world_entered();') > main_c.index('network_map_transfer = 0;')
assert 'daytime_weather_awaiting_sky()' in hud_c
assert "Waiting for the server's time of day" in hud_c

# Demos replay the time instead of inheriting whatever the session had.
assert 'daytime_weather_record_initial(demo_record_teamplay_packet);' in demo_c
assert 'daytime_weather_reset_world();' in demo_c

# The module is in both build descriptions and the docs know about it.
assert 'daytime_weather.c' in (root / 'src/CMakeLists.txt').read_text()
assert '$(ENGINE)/daytime_weather.c' in (root / 'tools/bench/Makefile').read_text()
networking = (root / 'docs/networking.md').read_text()
assert '0x33' in networking and 'Daytime and Weather' in networking
assert '**Daytime and Weather (`0x33`).**' not in networking  # no longer "Not implemented"
assert 'DAYTIME_WEATHER_PACKET_ID' in header
# network.h owns the extension id; a duplicate macro of the same name
# would rewrite that enum declaration in a file that includes this first.
assert '#define EXT_DAYTIME_WEATHER' not in header

print('Daytime and Weather wiring checks passed')
