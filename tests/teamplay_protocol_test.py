"""Compile/test the actual Teamplay C wire/state code without graphics or ENet.

Run: python3 tests/teamplay_protocol_test.py
Production includes are substituted with small link stubs; the implementation
itself, including its byte parser and sender, is not rewritten for the test.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/teamplay.c").read_text()
for header in ("common.h", "network.h", "player.h", "chatlog.h", "sound.h", "demo.h"):
    src = src.replace(f'#include "{header}"', "")
stubs = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "teamplay.h"
#define TEAM_SPECTATOR 255
#define SOUND_LOCAL 0
struct player_test { int connected, alive, team; char name[17]; };
struct player_test players[256];
int local_player_id, network_connected, network_logged_in;
int sound_beep1;
static int sounds, chats, sends, last_packet, last_length;
static unsigned char last_data[128];
void sound_create(int s, void* wav, float x, float y, float z) {
    (void)s; (void)wav; (void)x; (void)y; (void)z; ++sounds;
}
int demo_is_playing(void) { return 0; }
int demo_mute_effects(void) { return 0; }
void chat_add(int c, unsigned int color, const char* s) {
    (void)c; (void)color; (void)s; ++chats;
}
void network_send(int id, void* data, int len) {
    ++sends; last_packet=id; last_length=len;
    assert(len < (int)sizeof(last_data));
    memcpy(last_data, data, len);
}
'''
checks = r'''
static void writef(unsigned char* out, float f) {
    uint32_t v; memcpy(&v,&f,4);
    for(int i=0;i<4;i++) out[i]=(unsigned char)(v>>(i*8));
}
static void writebits(unsigned char* out, uint32_t v) {
    for(int i=0;i<4;i++) out[i]=(unsigned char)(v>>(i*8));
}
static unsigned char snapshot[3][80];
static size_t snapshot_len[3];
static int snapshot_count;
static void save_snapshot(const uint8_t* data, size_t len) {
    assert(snapshot_count < 3 && len <= sizeof(snapshot[0]));
    memcpy(snapshot[snapshot_count], data, len);
    snapshot_len[snapshot_count++] = len;
}
int main(void) {
    teamplay_reset_connection();
    unsigned char ping[23+100] = {0};
    ping[0]=1; ping[1]=3;
    writef(ping+2,100.5f); writef(ping+6,200.25f); writef(ping+10,20.f);
    writef(ping+14,2.f); ping[18]=0; ping[19]=12; ping[20]=34; ping[21]=56;
    memcpy(ping+23,"Hello",5);
    assert(!teamplay_receive(ping,28,0,0));
    teamplay_set_negotiated(true);
    assert(!teamplay_receive(ping,22,0,0));
    unsigned char conf[10]={0, 0xff};
    writef(conf+2,0.f); writef(conf+6,-2.f);
    assert(teamplay_receive(conf,10,0,0));
    assert(teamplay_features==7 && teamplay_north_x==0 && teamplay_north_y==-1);
    assert(teamplay_can_ping());
    assert(teamplay_receive(ping,28,0,0));
    assert(teamplay_pings[3].active && teamplay_pings[3].red==56);
    assert(teamplay_pings[3].green==34 && teamplay_pings[3].blue==12);
    assert(teamplay_pings[3].surfaces==3 && !strcmp(teamplay_pings[3].reason,"Hello"));
    assert(sounds==1 && chats==0);
    teamplay_tick(1.f); assert(teamplay_pings[3].active);
    teamplay_tick(1.f); assert(!teamplay_pings[3].active);
    ping[18]=4; assert(teamplay_receive(ping,28,0,0));
    assert(teamplay_pings[3].surfaces==4 && chats==1);
    ping[18]=0x80; assert(teamplay_receive(ping,28,0,0));
    assert(teamplay_pings[3].surfaces==0); /* unknown-only isn't default */
    writebits(ping+14,0x7fc00000); assert(!teamplay_receive(ping,28,0,0));
    writebits(ping+14,0xff800000); assert(!teamplay_receive(ping,28,0,0));
    writebits(ping+14,0x7f800000);
    assert(teamplay_receive(ping,28,0,0));
    assert(teamplay_pings[3].endless);
    teamplay_tick(100000.f); assert(teamplay_pings[3].active);
    writebits(ping+2,0x7fc00000); assert(!teamplay_receive(ping,28,0,0));
    writef(ping+14,0.f); /* zero-duration removal needs no valid position */
    assert(teamplay_receive(ping,28,0,0) && !teamplay_pings[3].active);
    writef(ping+2,100.f); writef(ping+14,2.f);
    assert(teamplay_receive(ping,28,1,0) && !teamplay_pings[3].active);
    assert(teamplay_receive(ping,28,0,1) && !teamplay_pings[3].active);
    unsigned char mark[12+70]={2,4};
    writef(mark+2,10.f); mark[6]=3; mark[7]=3;
    mark[8]=11; mark[9]=22; mark[10]=33; mark[11]=91; /* reserved */
    memcpy(mark+12,"Marked",6);
    assert(!teamplay_receive(mark,11,0,0));
    assert(teamplay_receive(mark,18,0,0));
    assert(teamplay_marks[4].active && teamplay_marks[4].show_name);
    assert(teamplay_marks[4].clear_on_respawn && teamplay_marks[4].red==33);
    assert(!strcmp(teamplay_marks[4].reason,"Marked"));
    teamplay_overlay_hold(true);
    teamplay_tick(.1f);
    assert(teamplay_can_overlay() && teamplay_overlay_opacity()>0.f);
    teamplay_features=2; assert(!teamplay_can_overlay() && teamplay_overlay_opacity()==0.f);
    assert(teamplay_marks[4].active); /* mark is independent of TEAM_ESP bit */
    teamplay_features=7;
    teamplay_record_initial(save_snapshot);
    assert(snapshot_count==2 && snapshot_len[0]==11 && snapshot_len[1]==19);
    assert(snapshot[0][0]==112 && snapshot[0][1]==0 && snapshot[0][2]==7);
    assert(snapshot[1][0]==112 && snapshot[1][1]==2 && snapshot[1][2]==4);
    assert(snapshot[1][7]==3 && snapshot[1][8]==3);
    assert(!memcmp(snapshot[1]+13,"Marked",6));
    teamplay_player_spawned(4); assert(!teamplay_marks[4].active);
    mark[7]=TEAMPLAY_SHOW_NAME;
    assert(teamplay_receive(mark,18,0,0));
    teamplay_player_spawned(4); assert(teamplay_marks[4].active); /* persistent */
    teamplay_tick(10.f); assert(!teamplay_marks[4].active);
    writebits(mark+2,0x7f800000);
    assert(teamplay_receive(mark,18,0,0) && teamplay_marks[4].endless);
    teamplay_tick(999.f); assert(teamplay_marks[4].active);
    writebits(mark+2,0x7fc00000); assert(!teamplay_receive(mark,18,0,0));
    assert(teamplay_marks[4].active);
    writebits(mark+2,0xff800000); assert(!teamplay_receive(mark,18,0,0));
    writef(mark+2,0.f); assert(teamplay_receive(mark,12,0,0));
    assert(!teamplay_marks[4].active); /* zero-duration removal */
    writef(mark+2,5.f);
    assert(teamplay_receive(mark,18,0,1)); /* queued across map load */
    assert(!teamplay_marks[4].active);
    teamplay_reset_map(); teamplay_apply_pending();
    assert(teamplay_marks[4].active && teamplay_marks[4].remaining==5.f);
    mark[6]=0x80; mark[7]=0;
    assert(teamplay_receive(mark,18,0,0) && teamplay_marks[4].surfaces==0);
    mark[6]=0;
    assert(teamplay_receive(mark,18,0,0)
           && teamplay_marks[4].surfaces==(TEAMPLAY_WORLD|TEAMPLAY_MAP));
    writef(mark+2,0.f);
    assert(teamplay_receive(mark,12,0,1));
    teamplay_reset_map(); teamplay_apply_pending();
    assert(!teamplay_marks[4].active); /* queued removal cancels a mark */
    writef(mark+2,5.f);
    assert(teamplay_receive(mark,18,0,0));
    teamplay_pings[4].active=true;
    teamplay_player_left(4); assert(!teamplay_pings[4].active && !teamplay_marks[4].active);
    teamplay_reset_map();
    assert(!teamplay_pings[3].active && teamplay_features==7 && teamplay_can_ping());
    char cleaned[65];
    const unsigned char dirty[]={' ', 'A', '\n', 0xff, 'B', 0, ' ', 0xe2,0x98,0x83,' '};
    teamplay_reason(cleaned,dirty,sizeof(dirty));
    assert(!strcmp(cleaned,"AB \xe2\x98\x83"));
    players[1].alive=1; players[1].connected=1; players[1].team=0;
    local_player_id=1; network_connected=1; network_logged_in=1;
    teamplay_features=0; assert(!teamplay_send_ping(1,2,3,"Hi") && !sends);
    teamplay_features=2; assert(teamplay_send_ping(1,2,3,"Hi"));
    assert(last_packet==112 && last_length==25 && last_data[0]==1 && last_data[1]==255);
    assert(last_data[18]==0 && last_data[19]==0 && last_data[22]==0);
    assert(!memcmp(last_data+23,"Hi",2));
    assert(!teamplay_send_ping(NAN,2,3,"Hi"));
    teamplay_reset_connection(); assert(!teamplay_can_ping() && !teamplay_pings[4].active);
    puts("Teamplay protocol/state tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / "test.c"
    c.write_text(stubs + src + checks)
    for flags in (["-O2", "-fsanitize=address,undefined", "-fno-omit-frame-pointer"],
                  ["-Ofast"]):
        exe = Path(d) / ("asan" if "-O2" in flags else "release")
        subprocess.run(["gcc", "-std=c99", "-Wall", "-Wextra", "-Werror", *flags,
                        "-I", str(root / "src"), str(c), "-lm", "-o", str(exe)], check=True)
        subprocess.run([str(exe)], check=True)
