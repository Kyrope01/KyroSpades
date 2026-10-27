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
#include "pie_menu_ext.h"
#define TEAM_SPECTATOR 255
#define PLAYERS_MAX 256
#define CAMERAMODE_FPS 1
#define CAMERAMODE_SPECTATOR 2
#define CAMERAMODE_BODYVIEW 3
#define CAMERAMODE_DEATH 4
#define CAMERA_HITTYPE_NONE 0
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
struct Player {int connected,team,alive; char name[17]; struct {float x,y,z;} orientation_smooth;};
struct Player players[256];
/* Colour code 1 and 2 draw the team colours, as they do in chat. */
struct GameState { struct Team { char name[11]; unsigned char red, green, blue; } team_1, team_2; } gamestate;
unsigned char local_player_id = 1;
int network_connected = 1, network_logged_in = 1, network_map_transfer = 0;
int camera_mode = CAMERAMODE_FPS, sound_chat;
struct {int free_aim,window_width,window_height;} settings;
float camera_rot_x,camera_rot_y,camera_crosshair_rot_x,camera_crosshair_rot_y;
/* The camera being drawn, which the extension's aim is taken from. */
float camera_x=100,camera_y=20,camera_z=300;
int cameracontroller_bodyview_mode, cameracontroller_bodyview_player;
struct Camera_HitType {char type; float distance; unsigned char player_id;};
struct PacketChatMessage {unsigned char player_id,chat_type; char message[255];};
void log_log(int level,const char* file,int line,const char* fmt,...) {
    (void)level;(void)file;(void)line;(void)fmt;
}
int map_isair(int x,int y,int z) {(void)x;(void)y;(void)z;return 1;}
void camera_local_eye(float* x,float* y,float* z) {*x=10;*y=20;*z=30;}
void camera_vector_from_angles(float x,float y,float* a,float* b,float* c) {
    (void)x;(void)y;*a=1;*b=0;*c=0;
}
static int hit_type=CAMERA_HITTYPE_BLOCK, target=2;
void camera_hit_fromplayer(struct Camera_HitType* h,int id,float range) {
    (void)id;(void)range; h->type=hit_type; h->distance=5;h->player_id=target;
}
/* The drawn camera's aim: a block at (257,21,298) in render coordinates, and
   the nearest thing the ray meets at 5 units along +x. */
static int spec_hit=CAMERA_HITTYPE_BLOCK, spec_player=2, has_cell=1;
static int cell[6]={257,21,298,256,21,298};
int* camera_terrain_pickEx(unsigned char mode,float x,float y,float z,float rx,float ry,float rz) {
    (void)mode;(void)x;(void)y;(void)z;(void)rx;(void)ry;(void)rz;
    return has_cell ? cell : NULL;
}
void camera_hit(struct Camera_HitType* h,int exclude,float x,float y,float z,
                float rx,float ry,float rz,float range) {
    (void)exclude;(void)x;(void)y;(void)z;(void)rx;(void)ry;(void)rz;(void)range;
    h->type=spec_hit; h->distance=5; h->player_id=(unsigned char)spec_player;
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
    puts("Pie menu packet routing for all 54 slices passed");
    server_menu_checks();
    return 0;
}
'''
server_checks = r'''
/* The same client against a server that owns the menu: Pie Menu v1, extension
   0x31. The real parser is linked in, so these packets are the real wire. */
static size_t s_put(uint8_t* p, size_t n, const char* s) {
    size_t i = 0;
    while(s[i]) { p[n + i] = (uint8_t)s[i]; i++; }
    p[n + i] = 0;
    return i + 1;
}
typedef struct { uint8_t flags; const char *text, *label, *tooltip; } SSpec;
static size_t s_slice(uint8_t* p, size_t n, SSpec s) {
    p[n++] = 0; p[n++] = s.flags;                      /* the reserved Message ID */
    n += s_put(p, n, s.text);
    n += s_put(p, n, s.label);
    n += s_put(p, n, s.tooltip);
    return n;
}
static size_t s_pie(uint8_t* p, size_t n, uint8_t contexts, const char* theme, SSpec s[6]) {
    p[n++] = contexts; p[n++] = 0;
    n += s_put(p, n, theme);
    for(int i = 0; i < 6; i++) n = s_slice(p, n, s[i]);
    return n;
}
static void wedge(int i) {
    float a = i * 3.14159265358979323846f / 3.f;
    pie_menu_move(sinf(a) * 100.f, -cosf(a) * 100.f);
}
static void server_menu_checks(void) {
    static uint8_t pkt[4096];
    /* Alive, in a team, looking at a block: the BLOCK context. */
    players[1].alive = 1; players[1].team = 0;
    players[2].team = 0; players[2].connected = 1; players[2].alive = 1;
    hit_type = CAMERA_HITTYPE_BLOCK; spec_hit = CAMERA_HITTYPE_BLOCK;
    can_ping = false; sends = pings = 0;

    pie_menu_ext_set_negotiated(true);
    /* One pie for a block: a line, a GLOBAL line, a command, a blank slice
       that carries only a Tooltip, and two substitutions. */
    SSpec world[6] = {
        {0, "Hi!", "Hi", ""},
        {PIE_FLAG_GLOBAL, "hello all", "", ""},
        {0, "/place %r", "Place", "Builds where you look"},
        {0, "", "", "Airstrikes: 2"},
        {0, "/votekick %p at %b", "Kick", ""},
        {0, "100%% sure %q", "", ""},
    };
    SSpec dead[6];
    for(int i = 0; i < 6; i++) dead[i] = (SSpec){0, "", "", ""};
    dead[0] = (SSpec){0, "/spawn base", "Base", ""};
    dead[1] = (SSpec){0, "", "", ""};
    /* Offered to three contexts at once: a server sends such a pie once. */
    SSpec both[6];
    for(int i = 0; i < 6; i++) both[i] = (SSpec){0, "", "", ""};
    both[0] = (SSpec){0, "kick %p", "Kick", ""};
    pkt[0] = 0; pkt[1] = 3;
    size_t len = s_pie(pkt, 2, PIE_CONTEXT_BLOCK, "World", world);
    len = s_pie(pkt, len, PIE_CONTEXT_BLOCK | PIE_CONTEXT_TEAMMATE | PIE_CONTEXT_ENEMY, "Shared", both);
    len = s_pie(pkt, len, PIE_CONTEXT_DEAD, "Spawn", dead);
    assert(pie_menu_ext_receive(pkt, len));

    /* The server's menu replaces the client's own from the next open. */
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); assert(pie_menu_has_selection()); pie_menu_close(true);
    assert(sends == 1 && chat_kind == CHAT_TEAM && !strcmp(last_text, "Hi!"));
    /* A CHAT slice with GLOBAL goes to the whole server. */
    assert(pie_menu_open()); wedge(1); pie_menu_close(true);
    assert(sends == 2 && chat_kind == CHAT_ALL && !strcmp(last_text, "hello all"));
    /* A command is sent exactly as any other Text is, GLOBAL included. */
    assert(pie_menu_open()); wedge(2); pie_menu_close(true);
    assert(sends == 3 && chat_kind == CHAT_TEAM);
    assert(!strcmp(last_text, "/place 100.00 20.00 300.00 1.0000 0.0000 0.0000"));
    /* A blank slice cannot be chosen, as the centre cannot -- and a Tooltip
       alone does not make one choosable. */
    assert(pie_menu_open()); wedge(3);
    assert(!pie_menu_has_selection());
    pie_menu_close(true);
    assert(sends == 3 && pings == 0);
    /* %p is empty when no player is aimed at, and %b names the block. */
    assert(pie_menu_open()); wedge(4); pie_menu_close(true);
    assert(!strcmp(last_text, "/votekick  at 257 298 42"));
    /* A reserved token stays as it is, and %% is a percent sign. */
    assert(pie_menu_open()); wedge(5); pie_menu_close(true);
    assert(!strcmp(last_text, "100% sure %q"));

    /* Cycling reaches the next pie offered to this context, in the order the
       server sent them, and the pie last used in a context is the one that
       reopens. */
    assert(pie_menu_open() && pie_menu_server_menu());
    pie_menu_cycle(1);
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "kick ") && chat_kind == CHAT_TEAM);
    assert(pie_menu_open());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "kick "));
    assert(pie_menu_open()); pie_menu_cycle(1); pie_menu_close(false); /* back to World */

    /* A player the aim meets before any block is %p, and it decides the
       context: the same pie is offered to a teammate and to an enemy. */
    spec_hit = CAMERA_HITTYPE_PLAYER; spec_player = 2;
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "kick #2"));
    players[2].team = 1;
    assert(pie_menu_open());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "kick #2"));
    players[2].team = 0;
    spec_hit = CAMERA_HITTYPE_BLOCK;

    /* More pies for one context than the client's own menu has pages: the page
       index must never read past the built-in tables. */
    static uint8_t many_pkt[4096];
    SSpec many[6];
    for(int i = 0; i < 6; i++) many[i] = (SSpec){0, "", "", ""};
    many[0] = (SSpec){0, "many", "Many", ""};
    many_pkt[0] = 0; many_pkt[1] = 5;
    size_t many_len = 2;
    for(int i = 0; i < 5; i++) many_len = s_pie(many_pkt, many_len, PIE_CONTEXT_BLOCK, "Many", many);
    assert(pie_menu_ext_receive(many_pkt, many_len));
    for(int i = 0; i < 5; i++) {
        assert(pie_menu_open() && pie_menu_server_menu());
        if(i) pie_menu_cycle(1);
        wedge(0); pie_menu_close(true);
        assert(!strcmp(last_text, "many"));
    }
    pie_menu_close(false);

    /* Marking a place is a chat command now. The client sends Teamplay's
       /pingmark with %h -- the point the drawn camera's aim met, in wire
       coordinates -- and interprets none of it, so it never pings itself. */
    static uint8_t ping_pkt[512];
    SSpec pslice[6];
    for(int i = 0; i < 6; i++) pslice[i] = (SSpec){0, "", "", ""};
    pslice[0] = (SSpec){PIE_FLAG_GLOBAL, "/pingmark %h \x82t\x82", "Mark", ""};
    pslice[1] = (SSpec){0, "/pingmark %h no hit", "", ""};
    ping_pkt[0] = 0; ping_pkt[1] = 1;
    assert(pie_menu_ext_receive(ping_pkt, s_pie(ping_pkt, 2, PIE_CONTEXT_BLOCK, "Mark", pslice)));
    can_ping = true; /* negotiated, and still no ping: the line is the request */
    int sent = sends, pinged = pings;
    assert(pie_menu_open()); wedge(0); pie_menu_close(true);
    assert(pings == pinged && sends == sent + 1 && chat_kind == CHAT_ALL);
    assert(!strcmp(last_text, "/pingmark 105.00 300.00 43.00 \x82t\x82"));
    /* An aim that met nothing leaves %h empty and the server to answer. */
    spec_hit = CAMERA_HITTYPE_NONE;
    assert(pie_menu_open()); wedge(1); pie_menu_close(true);
    assert(pings == pinged && sends == sent + 2 && chat_kind == CHAT_TEAM);
    assert(!strcmp(last_text, "/pingmark  no hit"));
    spec_hit = CAMERA_HITTYPE_BLOCK;
    can_ping = false;

    /* Everything is taken when the menu opens: a new Menu arriving while it is
       held changes nothing until it is opened again. */
    assert(pie_menu_open());
    SSpec other[6];
    for(int i = 0; i < 6; i++) other[i] = (SSpec){0, "", "", ""};
    other[0] = (SSpec){0, "replaced", "Replaced", ""};
    static uint8_t swap[512];
    swap[0] = 0; swap[1] = 1;
    assert(pie_menu_ext_receive(swap, s_pie(swap, 2, PIE_CONTEXT_BLOCK, "New", other)));
    int before = sends;
    wedge(0); assert(pie_menu_has_selection()); pie_menu_close(true);
    assert(sends == before + 1);
    /* the held menu stood, its Text sent exactly as the server encoded it */
    assert(!strcmp(last_text, "/pingmark 105.00 300.00 43.00 \x82t\x82"));
    assert(pie_menu_open()); wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "replaced"));

    /* A dead player is offered the DEAD pies alone, and the menu opens. */
    players[1].alive = 0;
    assert(pie_menu_ext_receive(pkt, len));
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "/spawn base"));
    wedge(1);
    assert(pie_menu_open()); wedge(1);
    assert(!pie_menu_has_selection()); pie_menu_close(false);
    players[1].alive = 1;

    /* A spectator is offered SPECTATOR pies alone. */
    static uint8_t spec[512];
    SSpec sslice[6];
    for(int i = 0; i < 6; i++) sslice[i] = (SSpec){0, "", "", ""};
    sslice[0] = (SSpec){0, "/spec hello", "Greet", ""};
    spec[0] = 0; spec[1] = 1;
    assert(pie_menu_ext_receive(spec, s_pie(spec, 2, PIE_CONTEXT_SPECTATOR, "Staff", sslice)));
    players[1].team = TEAM_SPECTATOR;
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "/spec hello"));
    players[1].team = 0;
    /* With no pie for a block, a block has no menu: no other context's
       pies stand in, and the client's own does not come back. */
    assert(pie_menu_ext_receive(spec, s_pie(spec, 2, PIE_CONTEXT_SPECTATOR, "Staff", sslice)));
    assert(!pie_menu_open() && !pie_menu_opened());

    /* An aim that meets no block and no player is SKY, and a block aimed at
       is BLOCK: what the aim meets first decides which pies are offered. */
    static uint8_t sky_pkt[512];
    SSpec skyslice[6];
    for(int i = 0; i < 6; i++) skyslice[i] = (SSpec){0, "", "", ""};
    skyslice[0] = (SSpec){0, "sky", "Sky", ""};
    sky_pkt[0] = 0; sky_pkt[1] = 2;
    size_t sky_len = s_pie(sky_pkt, 2, PIE_CONTEXT_SKY, "Sky", skyslice);
    sky_len = s_pie(sky_pkt, sky_len, PIE_CONTEXT_BLOCK, "Block", world);
    assert(pie_menu_ext_receive(sky_pkt, sky_len));
    has_cell = 0; spec_hit = CAMERA_HITTYPE_NONE;      /* the aim meets nothing */
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "sky"));
    has_cell = 1; spec_hit = CAMERA_HITTYPE_BLOCK;     /* now it meets a block */
    assert(pie_menu_open() && pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "Hi!"));

    /* A Menu with a Pie Count of 0 leaves no pie menu at all. */
    pkt[0] = 0; pkt[1] = 0;
    assert(pie_menu_ext_receive(pkt, 2));
    players[1].team = 0;
    assert(!pie_menu_open() && !pie_menu_opened());

    /* Disconnecting brings the client's own menu back. */
    pie_menu_ext_reset_connection();
    pm.remembered[0] = 0; /* the world's first page, as a fresh client has it */
    assert(pie_menu_open() && !pie_menu_server_menu());
    wedge(0); pie_menu_close(true);
    assert(!strcmp(last_text, "Enemy Here!"));
    puts("Server-defined pie menu (Pie Menu v1) routing tests passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    path = Path(d) / "pie.c"
    exe = Path(d) / "test"
    ext = (root / "src/pie_menu_ext.c").read_text()
    path.write_text(stubs + src + ext + server_checks + checks)
    subprocess.run(["gcc", "-std=c99", "-O2", "-ffunction-sections", "-fdata-sections",
                    "-Wl,--gc-sections", "-I", str(root / "src"), str(path), "-lm", "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
