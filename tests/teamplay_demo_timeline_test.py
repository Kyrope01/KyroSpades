"""Test the actual demo bootstrap/timeline functions with isolated dependencies.

A full client build and live replay still need to be checked on a machine with
CMake, OpenGL/ENet headers and the ZeroSpades server.
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "src/demo.c").read_text()
bootstrap = source[source.index("static void detect_bootstrap_end(void) {"):source.index("/* ── Open ──")]
update = source[source.index("void demo_playback_update(void) {"):source.index("/* ── Initial-map snapshot")]
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define PACKET_MAPSTART_ID 18
#define PACKET_MAPCHUNK_ID 19
#define PACKET_STATEDATA_ID 15
#define PACKET_EXISTINGPLAYER_ID 9
#define TEAMPLAY_PACKET_ID 112
#define log_info(...) ((void)0)
struct DemoPacketEntry {
    float timestamp;
    unsigned short length;
    unsigned char* data;
};
static struct {
    struct DemoPacketEntry* packets;
    int packet_count, current_packet_index, active, paused, finished;
    float bootstrap_end_time, duration, current_time, speed;
    double last_real_time;
} DemoPlaybackState;
static double clock_now;
static double window_time(void) { return clock_now; }
static void game_clock_sync(void) {}
static float ping_remaining;
static int dispatched;
static void teamplay_tick(float dt) {
    assert(dt >= 0);
    if(ping_remaining > 0) ping_remaining -= dt;
}
static void dispatch_packet(const struct DemoPacketEntry* e) {
    dispatched++;
    if(e->data[0] == TEAMPLAY_PACKET_ID && e->data[1] == 1)
        ping_remaining = 10;
}
'''
checks = r'''
int main(void) {
    unsigned char config[] = {112, 0};
    unsigned char mark[] = {112, 2};
    unsigned char map[] = {18};
    unsigned char chunk[] = {19};
    unsigned char state[] = {15};
    unsigned char player[] = {9};
    unsigned char ping[] = {112, 1};
    struct DemoPacketEntry packets[] = {
        {0.01f,2,config}, {0.02f,2,mark}, {0.03f,1,map},
        {0.04f,1,chunk}, {0.05f,1,state}, {0.06f,1,player},
        {0.1f,2,ping}, {20.f,2,ping}
    };
    DemoPlaybackState.packets=packets;
    DemoPlaybackState.packet_count=8;
    detect_bootstrap_end();
    assert(DemoPlaybackState.bootstrap_end_time > .059f &&
           DemoPlaybackState.bootstrap_end_time < .061f);
    /* Playback at 2x: a ping at t=.1 ages to the current demo time. */
    DemoPlaybackState.active=1;
    DemoPlaybackState.speed=2;
    DemoPlaybackState.duration=100;
    clock_now=2;
    demo_playback_update();
    assert(dispatched==7 && DemoPlaybackState.current_time==4);
    assert(ping_remaining > 6.09f && ping_remaining < 6.11f);
    DemoPlaybackState.paused=1; clock_now=20;
    demo_playback_update();
    assert(ping_remaining > 6.09f && ping_remaining < 6.11f); /* pause doesn't age */
    DemoPlaybackState.paused=0; DemoPlaybackState.last_real_time=20;
    clock_now=23;
    demo_playback_update();
    assert(ping_remaining < .11f && ping_remaining > .09f); /* age to t=10 */
    puts("Teamplay demo timeline/bootstrap tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "demo.c"
    exe = Path(d) / "demo"
    c.write_text(stubs + bootstrap + update + checks)
    subprocess.run(["gcc", "-std=c99", "-O2", "-Wall", "-Wextra", "-Werror",
                    str(c), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
