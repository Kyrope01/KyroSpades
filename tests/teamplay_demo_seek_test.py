"""Replay real seek dispatch against isolated mark lifecycle stubs."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/demo.c').read_text()
seek = source[source.index('static void demo_fast_replay_to(int from_index, float target_time) {'):
              source.index('/* ── Seek', source.index('static void demo_fast_replay_to(int from_index, float target_time) {'))]
stub = r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define PACKET_MAPSTART_ID 18
#define PACKET_MAPCHUNK_ID 19
#define PACKET_STATEDATA_ID 15
#define TEAMPLAY_PACKET_ID 112
struct DemoPacketEntry {float timestamp;unsigned short length;unsigned char* data;};
static struct {struct DemoPacketEntry* packets;int packet_count;} DemoPlaybackState;
static int demo_seeking, demo_muting;
static int active_mark, pending_mark, config_seen, applies;
static void teamplay_reset_map(void) { active_mark=0; }
static void teamplay_apply_pending(void) {if(pending_mark) {active_mark=pending_mark;pending_mark=0;} applies++;}
static bool teamplay_receive(const uint8_t* p,size_t len,bool seeking,bool map_loading) {
    assert(seeking && map_loading && len>=1 && p[0]==2);
    pending_mark=(int)len;return true;
}
static void teamplay_tick(float dt) {(void)dt;}
static void on_config(void* data,int len) {(void)data;(void)len;config_seen++;}
static void on_state(void* data,int len) {(void)data;(void)len;}
static void (*packets[256])(void*,int);
'''
checks = r'''
int main(void) {
    unsigned char conf[]={112,0}, mark[]={112,2,1,2,3}, start[]={18}, chunk[]={19}, state[]={15};
    struct DemoPacketEntry entries[]={
        {.timestamp=.01f,.length=2,.data=conf},
        {.timestamp=.02f,.length=5,.data=mark},
        {.timestamp=.03f,.length=1,.data=start},
        {.timestamp=.04f,.length=1,.data=chunk},
        {.timestamp=.05f,.length=1,.data=state},
        {.timestamp=.06f,.length=5,.data=mark},
    };
    packets[112]=on_config;packets[15]=on_state;
    DemoPlaybackState.packets=entries;DemoPlaybackState.packet_count=6;
    demo_fast_replay_to(0,.05f);
    assert(active_mark==4 && applies==1 && config_seen==1);
    assert(!demo_seeking && !demo_muting);
    active_mark=0;pending_mark=0;config_seen=0;applies=0;
    demo_fast_replay_to(0,.06f);
    assert(active_mark==4 && applies==1 && config_seen==2);
    puts("Teamplay demo seek mark bootstrap passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    c=Path(d)/'seek.c'
    exe=Path(d)/'seek'
    c.write_text(stub+seek+checks)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
