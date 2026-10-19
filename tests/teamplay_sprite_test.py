"""Compile the actual waypoint sprite/frame/tint logic without GL headers.
Run: python3 tests/teamplay_sprite_test.py
"""
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/teamplay_draw.c').read_text()
sprite = source[source.index('static void ping_color('):source.index('static void begin(void)')]
assert 'ping_sprite(id,p,px,py,size,alpha)' in source
assert 'ping_sprite(id,p,x,y,size,fade(p))' in source
assert 'p->remaining' not in sprite  # the server, not animation, determines expiry
image = (root / 'resources/png/particle_block_anim.png').read_bytes()
assert image[:8] == b'\x89PNG\r\n\x1a\n'
assert struct.unpack('>II', image[16:24]) == (512, 512)
check = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#define GL_BLEND 1
#define GL_TEXTURE0 2
#define GL_TEXTURE_2D 3
#define TEAM_1 0
#define TEAM_2 1
#define TEAM_SPECTATOR 255
typedef struct { float age; unsigned char red,green,blue; } TeamplayPing;
struct texture {int width,height,texture_id;};
static struct texture texture_particle_anim={512,512,42};
static struct {int connected,team;} players[256];
static int local_player_id=1;
static struct {struct {unsigned char red,green,blue;} team_1,team_2;} gamestate;
static int draws, fallback;
static float uv[4], color[4], tint[4], width, height;
static void diamond(float x,float y,float r,uint8_t red,uint8_t green,uint8_t blue,float alpha,float age) {
    (void)x;(void)y;(void)r;(void)alpha;(void)age;
    assert(red==gamestate.team_1.red && green==gamestate.team_1.green && blue==gamestate.team_1.blue);
    fallback++;
}
static void glColor4f(float r,float g,float b,float a) {color[0]=r;color[1]=g;color[2]=b;color[3]=a;}
static void glColor3f(float r,float g,float b) {(void)r;(void)g;(void)b;}
static void glEnable(int what) {assert(what==GL_BLEND);}
static void glActiveTexture(int what) {assert(what==GL_TEXTURE0);}
static void glBindTexture(int what,int id) {assert(what==GL_TEXTURE_2D && id==0);}
static void texture_draw_sector(struct texture* t,float x,float y,float w,float h,float u,float v,float us,float vs) {
    assert(t==&texture_particle_anim && x==50 && y==150);
    width=w; height=h; uv[0]=u;uv[1]=v;uv[2]=us;uv[3]=vs;
    for(int i=0;i<4;i++) tint[i]=color[i];
    draws++;
}
''' + sprite + r'''
int main(void) {
    gamestate.team_1.red=9;gamestate.team_1.green=25;gamestate.team_1.blue=255;
    gamestate.team_2.red=255;gamestate.team_2.green=145;gamestate.team_2.blue=25;
    players[1].team=TEAM_1; players[2].team=TEAM_2; players[2].connected=1;
    TeamplayPing p={.age=0,.red=200,.green=100,.blue=30};
    ping_sprite(2,&p,100,100,100,.8f);
    assert(draws==1 && width==100 && height==100);
    assert(fabsf(tint[0]-1)<.0001f && fabsf(tint[1]-145/255.f)<.0001f && tint[3]==.8f);
    assert(fabsf(uv[0]-.5f/512.f)<.0001f && fabsf(uv[1]-.5f/512.f)<.0001f);
    assert(fabsf(uv[2]-63.f/512.f)<.0001f && fabsf(uv[3]-63.f/512.f)<.0001f);
    p.age=1.f/24.f; ping_sprite(2,&p,100,100,100,.8f);
    assert(fabsf(uv[0]-64.5f/512.f)<.0001f && fabsf(uv[1]-.5f/512.f)<.0001f);
    p.age=8.f/24.f; ping_sprite(2,&p,100,100,100,.8f);
    assert(fabsf(uv[0]-.5f/512.f)<.0001f && fabsf(uv[1]-64.5f/512.f)<.0001f);
    p.age=64.f/24.f; ping_sprite(255,&p,100,100,100,.8f);
    assert(tint[0]==9/255.f && tint[2]==1.f); /* server-owned ping gets local team */
    assert(fabsf(uv[0]-.5f/512.f)<.0001f);
    texture_particle_anim.texture_id=0;
    ping_sprite(255,&p,100,100,100,.8f);
    assert(draws==4 && fallback==1); /* missing optional image: still show waypoint */
    puts("Teamplay sprite frame/tint tests passed");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    file = Path(tmp) / 'sprite.c'
    exe = Path(tmp) / 'sprite'
    file.write_text(check)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(file), '-lm', '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
