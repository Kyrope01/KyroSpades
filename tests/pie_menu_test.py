"""Isolated behavioral test of the actual pie menu state and chat routing.

Runs without GL/ENet by linking only reached C functions (-ffunction-sections).
Run: python3 tests/pie_menu_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
src = (root / "src/pie_menu.c").read_text()
import re
src = re.sub(r'#include "[^"]+"', '', src)
stubs = r'''
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#define TEAM_SPECTATOR 255
#define CAMERAMODE_FPS 1
#define CAMERAMODE_DEATH 4
#define CAMERA_HITTYPE_BLOCK 1
#define CAMERA_HITTYPE_PLAYER 2
#define PACKET_CHATMESSAGE_ID 17
#define CHAT_ALL 0
#define CHAT_TEAM 1
#define SOUND_LOCAL 0
#define FONT_FIXEDSYS 0
#define GL_BLEND 1
#define GL_SRC_ALPHA 2
#define GL_ONE_MINUS_SRC_ALPHA 3
#define GL_SRC_ALPHA_SATURATE 4
void glColor4f(float,float,float,float);
void glEnable(int); void glDisable(int); void glBlendFunc(int,int);
void glColor3f(float,float,float);
void glx_draw_ring_segment_2d(float,float,float,float,float,float,int);
void font_select(int); float font_length(float,char*);
void font_render(float,float,float,char*);
struct Player {int connected,team,alive; char name[17];};
struct Player players[256];
unsigned char local_player_id = 1;
int network_connected = 1, network_logged_in = 1, network_map_transfer = 0;
int camera_mode = CAMERAMODE_FPS, sound_chat;
struct {int free_aim,window_width,window_height;} settings;
float camera_rot_x,camera_rot_y,camera_crosshair_rot_x,camera_crosshair_rot_y;
struct Camera_HitType {char type; float distance; unsigned char player_id;};
struct PacketChatMessage {unsigned char player_id,chat_type; char message[255];};
int map_isair(int x,int y,int z) {(void)x;(void)y;(void)z;return 1;}
void camera_local_eye(float* x,float* y,float* z) {*x=10;*y=20;*z=30;}
void camera_vector_from_angles(float x,float y,float* a,float* b,float* c) {
    (void)x;(void)y;*a=1;*b=0;*c=0;
}
static int hit_type=CAMERA_HITTYPE_BLOCK, target=2;
void camera_hit_fromplayer(struct Camera_HitType* h,int id,float range) {
    (void)id;(void)range; h->type=hit_type; h->distance=5;h->player_id=target;
}
static int sends, pings, sound, chats, packet_len;
static unsigned char packet[257];
static char last_text[255], last_reason[128];
static unsigned char chat_kind;
static float wire[3];
static bool can_ping;
int teamplay_can_ping(void) {return can_ping;}
bool teamplay_send_ping(float x,float y,float z,const char* reason) {
    if(!can_ping || !players[local_player_id].alive) return false;
    pings++; wire[0]=x; wire[1]=y; wire[2]=z;
    snprintf(last_reason,sizeof(last_reason),"%s",reason); return true;
}
void sound_create(int a, void* b,float c,float d,float e) {
    (void)a;(void)b;(void)c;(void)d;(void)e;sound++;
}
void chat_add(int a,unsigned int b,const char* c) {(void)a;(void)b;(void)c; chats++;}
void chat_showpopup(const char* a,float b,int c) {(void)a;(void)b;(void)c;}
void network_send(int id,void* data,int len) {
    assert(id==PACKET_CHATMESSAGE_ID && len>=3 && len<=257);
    memcpy(packet,data,len); packet_len=len;
    struct PacketChatMessage* m=data;
    assert(m->player_id==local_player_id);
    assert(len==2+(int)strlen(m->message)+1); /* NUL on wire, as in ZeroSpades */
    assert(packet[len-1]==0);
    sends++;chat_kind=m->chat_type;
    snprintf(last_text,sizeof(last_text),"%s",m->message);
}
'''
checks = r'''
int main(void) {
    players[1].alive=1;players[1].team=0;
    players[2].alive=1;players[2].team=0;players[2].connected=1;
    snprintf(players[2].name,sizeof(players[2].name),"Ally");
    assert(pie_menu_open());
    assert(!pie_menu_has_selection());
    pie_menu_close(true);
    assert(!sends && !pings); /* dead zone */
    assert(pie_menu_open());
    pie_menu_move(0,-100); assert(pie_menu_has_selection()); pie_menu_close(true);
    assert(pings==0 && sends==1 && chat_kind==CHAT_TEAM && !strcmp(last_text,"Enemy Here!"));
    can_ping=true;
    assert(pie_menu_open());pie_menu_move(0,-100);pie_menu_close(true);
    assert(pings==1 && sends==1 && !strcmp(last_reason,"Enemy Here!"));
    assert(wire[0]==15 && wire[1]==30 && wire[2]==43); /* x,z,63-y */
    hit_type=CAMERA_HITTYPE_PLAYER;
    assert(pie_menu_open());pie_menu_move(0,-100);pie_menu_close(true);
    assert(pings==1 && sends==2 && !strcmp(last_text,"/pm #2 Above You!"));
    assert(pie_menu_open());pie_menu_cycle(-1);pie_menu_move(0,-100);pie_menu_close(true);
    assert(pings==2 && sends==2); /* last teammate page is Point */
    /* New remembered teammate page is Point (last), cycle one forward to Warn. */
    assert(pie_menu_open());pie_menu_cycle(1);pie_menu_move(0,-100);pie_menu_close(true);
    assert(sends==3 && !strcmp(last_text,"/pm #2 Above You!"));
    players[2].team=1;
    assert(pie_menu_open());pie_menu_move(0,-100);pie_menu_close(true);
    assert(chat_kind==CHAT_ALL && !strcmp(last_text,"Ally, I See You"));
    assert(pie_menu_open());pie_menu_cycle(1);pie_menu_move(0,-100);pie_menu_close(true);
    assert(pings==3 && chat_kind==CHAT_ALL); /* no extra global taunt */
    hit_type=CAMERA_HITTYPE_BLOCK; players[1].alive=0;
    assert(pie_menu_open());pie_menu_move(0,-100);pie_menu_close(true);
    assert(!strcmp(last_text,"Enemy Here!")); /* dead: chat fallback */
    assert(sound==0 && chats==0); /* a local echo is not server acceptance */

    /* Exercise every slice on every page, not merely the top wedge. Assert
       actual packet headers, text, NUL and length; on Point pages a server
       with no Teamplay extension must receive team chat, not a silent drop. */
    static const char* labels[3][4][6] = {
        {
            {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"},
            {"Affirmative", "Thank You", "Hi!", "Negative", "Sorry!", "Help Me"},
            {"Attack!", "Get the Intel!", "Enemy Has the Intel!", "Fall Back!", "Regroup on Me", "Defend the Intel!"}
        },
        {
            {"Above You!", "On Your Right!", "Behind You!", "Below You!", "Sniper on You!", "On Your Left!"},
            {"Affirmative", "Thank You", "Hi!", "Negative", "Sorry!", "Help Me"},
            {"Follow Me", "Cover Me", "Let Me Through", "Stay Here", "Boost Me Up", "Help Me Build"},
            {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"}
        },
        {
            {"I See You", "Nice Try", "Miss Me?", "Too Easy", "Behind You...", "Say Goodbye"},
            {"Enemy Here!", "Tear It Down!", "Watch This Spot", "Go Here!", "Let's Dig Here", "Help Me Build"}
        }
    };
    players[1].alive=1; can_ping=false;
    for(int variant=0;variant<3;variant++) {
        hit_type=variant==0 ? CAMERA_HITTYPE_BLOCK : CAMERA_HITTYPE_PLAYER;
        players[2].team=variant==2 ? 1 : 0;
        int pages_count=variant==0 ? 3 : variant==1 ? 4 : 2;
        for(int page=0;page<pages_count;page++) {
            for(int slice=0;slice<6;slice++) {
                pm.remembered[variant]=0;
                assert(pie_menu_open());
                assert(pm.variant==variant);
                pie_menu_cycle(page);
                float angle=slice*3.14159265358979323846f/3.f;
                pie_menu_move(sinf(angle)*100.f,-cosf(angle)*100.f);
                assert(pm.selection==slice);
                int before=sends;
                pie_menu_close(true);
                assert(sends==before+1 && packet[0]==local_player_id);
                assert(chat_kind==(variant==2 && page==0 ? CHAT_ALL : CHAT_TEAM));
                char expected[255];
                const char* label=labels[variant][page][slice];
                if(variant==1 && page!=3) snprintf(expected,sizeof(expected),"/pm #2 %s",label);
                else if(variant==2 && page==0) snprintf(expected,sizeof(expected),"Ally, %s",label);
                else snprintf(expected,sizeof(expected),"%s",label);
                assert(!strcmp(last_text,expected));
                assert(packet[1]==chat_kind && packet_len==(int)strlen(expected)+3);
                assert(!memcmp(packet+2,expected,strlen(expected)+1));
                if((variant==0 && page==0) || (variant==1 && page==3)
                   || (variant==2 && page==1)) {
                    /* The same Point slices switch to pings only when the
                       server has negotiated the extension. */
                    can_ping=true;
                    assert(pie_menu_open());
                    pie_menu_move(sinf(angle)*100.f,-cosf(angle)*100.f);
                    int before_pings=pings;
                    pie_menu_close(true);
                    assert(pings==before_pings+1 && sends==before+1);
                    assert(!strcmp(last_reason,label));
                    assert(wire[0]==15 && wire[1]==30 && wire[2]==43);
                    can_ping=false;
                }
            }
        }
    }
    assert(sound==0 && chats==0); /* wait for server echo/reply */
    puts("Pie menu packet routing for all 54 slices passed"); return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / "pie.c"
    exe = Path(d) / "test"
    path.write_text(stubs + src + checks)
    subprocess.run(["gcc", "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                    "-Wl,--gc-sections", "-I", str(root / "src"), str(path), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
