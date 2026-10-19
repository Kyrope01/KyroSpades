"""Compile the actual world/minimap/self mark passes against fake GL and player state.

Run: python3 tests/teamplay_mark_draw_test.py
"""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'src/teamplay_draw.c').read_text()
world = source[source.index('    /* Marks follow players, not a frozen packet position.'):
               source.index('    if(teamplay_overlay_opacity()', source.index('    /* Marks follow players, not a frozen packet position.'))]
self_notice = source[source.index('    /* Receiving a mark for ourselves is informative'):
                     source.index('    end();', source.index('    /* Receiving a mark for ourselves is informative'))]
mini = source[source.index('    for(int id=0; id<TEAMPLAY_PLAYERS-1; id++) {', source.index('void teamplay_draw_map(')):
              source.index('    end();', source.index('void teamplay_draw_map('))]
c = r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define TEAMPLAY_PLAYERS 256
#define TEAMPLAY_WORLD 1
#define TEAMPLAY_MAP 2
#define TEAM_SPECTATOR 255
#define GL_BLEND 1
#define TP_PI 3.14159265358979323846f
struct Player {int connected,alive,team;char name[17];struct {float x,y,z;} pos;
               struct {struct {float x,y,z;} eye;} physics;};
typedef struct {bool active, show_name;unsigned char surfaces,red,green,blue;char reason[65];} TeamplayMark;
static struct Player players[TEAMPLAY_PLAYERS];
static TeamplayMark teamplay_marks[TEAMPLAY_PLAYERS];
static int local_player_id=1, labels, rings;
static float label_x,label_y,ring_x,ring_y, last_red, last_green, last_blue;
static char label_text[128];
static float player_height(const struct Player* p) {(void)p;return 1.5f;}
static bool project(float x,float y,float z,float* px,float* py,float* f) {
    *px=x;*py=y;*f=z;return true;
}
static void label_color(float x,float y,const char* text,float a,uint8_t r,uint8_t g,uint8_t b) {
    assert(a==1.f);labels++;label_x=x;label_y=y;
    last_red=r;last_green=g;last_blue=b;
    snprintf(label_text,sizeof(label_text),"%s",text);
}
static void glEnable(int which) {assert(which==GL_BLEND);}
static void glColor4f(float r,float g,float b,float a) {
    (void)a;last_red=r;last_green=g;last_blue=b;
}
static void glx_draw_ring_segment_2d(float x,float y,float inner,float outer,
                                     float start,float stop,int steps) {
    (void)inner;(void)outer;(void)start;(void)stop;assert(steps==24);
    rings++;ring_x=x;ring_y=y;
}
static void world_marks(void) {
    float sw=960,sh=540;
''' + world + self_notice + r'''
}
static void map_marks(void) {
    float left=10,top=500,width=512,height=512,origin_x=0,origin_y=0,extent=512;
''' + mini + r'''
}
int main(void) {
    players[1].connected=1;
    players[3].connected=1;players[3].alive=1;players[3].team=0;
    strcpy(players[3].name,"Opponent");players[3].pos.x=100;players[3].pos.z=200;
    players[3].physics.eye.x=100;players[3].physics.eye.y=7;
    teamplay_marks[3]=(TeamplayMark){.active=1,.surfaces=TEAMPLAY_WORLD|TEAMPLAY_MAP,
        .red=204,.green=44,.blue=90};
    strcpy(teamplay_marks[3].reason,"Target");
    world_marks();assert(labels==1 && !strcmp(label_text,"Target") && last_red==204);
    labels=0;teamplay_marks[3].show_name=1;
    world_marks();assert(labels==1 && !strcmp(label_text,"Opponent: Target"));
    labels=0;players[3].team=TEAM_SPECTATOR;
    world_marks();assert(labels==0); /* spectator never gets a world label */
    players[3].team=0;
    labels=0;teamplay_marks[3].surfaces=TEAMPLAY_MAP;
    world_marks();assert(labels==0); /* no world surface */
    teamplay_marks[1]=(TeamplayMark){.active=1,.surfaces=TEAMPLAY_MAP,.red=7,.green=8,.blue=9};
    strcpy(teamplay_marks[1].reason,"Beware");
    world_marks();assert(labels==1 && !strcmp(label_text,"YOU ARE MARKED: Beware"));
    assert(label_x==480 && label_y==540*.84f && last_red==7 && last_green==8);
    rings=0;map_marks();
    assert(rings==2 && ring_x==110 && ring_y==300);
    assert(fabsf(last_red-204/255.f)<.0001f && fabsf(last_green-44/255.f)<.0001f);
    teamplay_marks[3].surfaces=0x80;teamplay_marks[1].active=0;rings=0;
    map_marks();assert(rings==0); /* unknown-only doesn't paint minimap */
    puts("Teamplay mark world/minimap/self rendering gates passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    path=Path(d)/'marks.c'
    exe=Path(d)/'marks'
    path.write_text(c)
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(path),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
