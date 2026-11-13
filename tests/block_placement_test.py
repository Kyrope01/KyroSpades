"""Compile and exercise actual KS placement logic with a fake world/network."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
player=(root/'src/player.c').read_text()
geometry=player[player.index('int overlaps_with_player('):player.index('\nint button_map[3];')]
logic=player[player.index('static bool pending_block;'):player.index('\n#define FALL_DAMAGE_VELOCITY')]
stub=r'''
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include "block_placement.h"
#define PLAYERS_MAX 256
#define PLAYER_ID_SERVER 255
#define CAMERAMODE_FPS 1
#define TEAM_SPECTATOR 255
#define TOOL_BLOCK 1
#define SCREEN_NONE 0
#define CHAT_NO_INPUT 0
#define ACTION_BUILD 0
#define PACKET_BLOCKACTION_ID 13
struct Pos {float x,y,z;};
struct Player {int connected,alive,team,held_item; float item_showup; struct {struct Pos eye; int airborne;} physics; struct {struct {int crouch,sprint;} keys;} input;};
struct HUD {int render_world;} hud={1}, *hud_active=&hud;
struct Player players[PLAYERS_MAX];
int local_player_id=0, camera_mode=1, network_connected=1, network_logged_in=1, network_map_transfer=0;
int local_player_blocks=50, screen_current=0,chat_input_mode=0;
int button_map[3]={0};
int map_size_x=512,map_size_y=64,map_size_z=512;
int solid_x=10,solid_y=8,solid_z=10,occupied=0, neighbor=1;
int pick_valid=1, pos[3]={10,9,10};
float now=1; int sent=0,synced=0;
struct PacketBlockAction {int player_id,action_type,x,y,z;};
struct PacketBlockAction last;
int demo_is_playing(void) {return 0;}
float window_time(void) {return now;}
bool map_isair(int x,int y,int z) {return !((neighbor && x==solid_x && y==solid_y && z==solid_z) || (occupied && x==pos[0] && y==pos[1] && z==pos[2]));}
int* camera_terrain_pick_local(int mode) {(void)mode;return pick_valid ? pos : 0;}
void network_sync_build_state(void) {synced++;}
void network_send(int id, void* data, int n) {(void)n;assert(id==13);last=*(struct PacketBlockAction*)data;sent++;}
'''
checks=r'''
int main(void) {
 /* Compare to ZeroSpades' protocol-space eye box and block center. */
 for(int crouch=0;crouch<=1;crouch++) for(int h=0;h<40;h++) {
  float ey=8.03f+h*0.125f, net_eye=63.f-ey;
  for(int y=3;y<16;y++) {
   float net_block=63.f-y;
   bool ref_overlap=(net_block < net_eye+(crouch?1.35f:2.25f) && net_block+1.f > net_eye-0.45f);
   bool ref_range=fabsf(net_block+0.5f-net_eye)<3.f;
   assert(block_placement_overlaps(10,y,10,10.5f,ey,10.5f,crouch)==ref_overlap);
   assert(block_placement_in_range(10,y,10,10.5f,ey,10.5f)==ref_range);
  }
 }
 struct Player* p=&players[0]; p->alive=1;p->connected=1;p->team=0;p->held_item=TOOL_BLOCK;
 p->physics.eye=(struct Pos){10.5f,10.0f,10.5f}; p->physics.airborne=1;
 assert(block_placement_in_range(10,9,10,10.5f,10.f,10.5f));
 assert(!block_placement_in_range(13,9,10,10.5f,10.f,10.5f));
 assert(block_placement_overlaps(10,9,10,10.5f,10.f,10.5f,0));
 assert(!block_placement_overlaps(11,9,10,10.55f,10.f,10.5f,0)); /* touching edge */
 assert(block_placement_overlaps(10,9,10,10.5f,10.0f,10.5f,1));
 assert(!player_try_place_block() && pending_block && sent==0);
 p->physics.eye.y=11.3f; player_update_pending_block();
 assert(!pending_block && sent==1 && synced==1 && last.x==10 && last.y==10 && last.z==54);
 assert(local_player_blocks==50); /* no prediction */
 player_update_pending_block();assert(sent==1);
 now=2; p->physics.eye.y=10.f; assert(!player_try_place_block() && pending_block);
 p->physics.airborne=0;player_update_pending_block();assert(!pending_block && sent==1);
 p->physics.airborne=1;assert(!player_try_place_block() && pending_block);
 neighbor=0; player_update_pending_block();assert(!pending_block && sent==1);neighbor=1;
 assert(!player_try_place_block() && pending_block);occupied=1;player_update_pending_block();assert(!pending_block && sent==1);occupied=0;
 assert(!player_try_place_block() && pending_block);screen_current=1;player_update_pending_block();assert(!pending_block && sent==1);screen_current=0;
 assert(!player_try_place_block() && pending_block);chat_input_mode=1;player_update_pending_block();assert(!pending_block && sent==1);chat_input_mode=0;
 assert(!player_try_place_block() && pending_block);button_map[1]=1;player_update_pending_block();assert(!pending_block && sent==1);button_map[1]=0;
 assert(!player_try_place_block() && pending_block);p->held_item=0;player_update_pending_block();assert(!pending_block && sent==1);p->held_item=1;
 p->physics.eye.y=11.3f; assert(player_try_place_block() && sent==2);assert(!player_try_place_block() && sent==2);
 puts("block placement geometry and pending jump checks passed"); return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c'; p.write_text(stub+geometry+logic+checks)
    exe=Path(d)/'test'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-I',str(root/'src'),str(p),'-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
network=(root/'src/network.c').read_text(); hud=(root/'src/hud.c').read_text(); main=(root/'src/main.c').read_text()
assert 'network_sync_build_state();' in logic and logic.index('network_sync_build_state();') < logic.index('network_send(PACKET_BLOCKACTION_ID')
assert 'local_player_blocks--' not in logic
assert 'local_player_blocks -= amount' not in hud
assert 'player_update_pending_block();' in main
assert network.count('local_player_blocks--;') >= 3
assert 'player_cancel_pending_block();' in network
print('Placement network, confirmed stock, and simulation wiring checks passed')

# Exercise the real packet handlers, including duplicates, foreign builds,
# overlapping line cells and demo playback. No map mutation on send is assumed.
packet_handlers=network[network.index('void read_PacketBlockAction('):network.index('void read_PacketStateData(')]
packet_stub=r'''
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define PLAYERS_MAX 32
#define ACTION_BUILD 0
#define ACTION_DESTROY 1
#define ACTION_SPADE 2
#define ACTION_GRENADE 3
#define SOUND_WORLD 0
#define log_warn(...) ((void)0)
struct PacketBlockAction {unsigned char player_id,action_type; int x,y,z;};
struct PacketBlockLine {unsigned char player_id; int sx,sy,sz,ex,ey,ez;};
struct Point {int x,y,z;};
struct {int player_stats;} settings;
struct {struct {int red,green,blue;} block;} players[32];
unsigned char local_player_id=0,local_player_blocks=5;
int player_stats_blocks_placed=0, sound_build=0, is_demo=0;
int map_size_x=32, map_size_y=64, map_size_z=32;
bool world[32][32][64];
bool demo_is_playing(void){return is_demo;} bool demo_mute_effects(void){return true;}
bool map_isair(int x,int y,int z){return !world[x][z][y];}
void map_set(int x,int y,int z,unsigned int c){world[x][z][y]=(c!=0xFFFFFFFFu);}
unsigned int map_get(int x,int y,int z){(void)x;(void)y;(void)z;return 0;}
void map_update_physics(int x,int y,int z){(void)x;(void)y;(void)z;}
void particle_create_block(int c,float x,float y,float z,float a,float b,int n,float d,float e){(void)c;(void)x;(void)y;(void)z;(void)a;(void)b;(void)n;(void)d;(void)e;}
void glowing_blocks_confirm_local_build(int x,int y,int z){(void)x;(void)y;(void)z;}
void sound_create(int t,void* s,float x,float y,float z){(void)t;(void)s;(void)x;(void)y;(void)z;}
int map_cube_line(int x,int y,int z,int x2,int y2,int z2,struct Point* out){(void)y2;(void)z2;int n=x2-x+1;for(int i=0;i<n;i++) if(out) out[i]=(struct Point){x+i,y,z};return n;}
'''
packet_checks=r'''
int main(void){
 struct PacketBlockAction invalid={0,ACTION_GRENADE,2147483647,2,50};
 read_PacketBlockAction(&invalid,sizeof(invalid));
 struct PacketBlockLine bad={0,-2147483647-1,2,50,4,2,50};
 read_PacketBlockLine(&bad,sizeof(bad));
 struct PacketBlockAction a={0,ACTION_BUILD,2,2,50};
 read_PacketBlockAction(&a,sizeof(a)); assert(local_player_blocks==4 && world[2][2][13]);
 read_PacketBlockAction(&a,sizeof(a)); assert(local_player_blocks==4);
 a.player_id=1;a.x=3;read_PacketBlockAction(&a,sizeof(a));assert(local_player_blocks==4);
 is_demo=1;a.player_id=0;a.x=4;read_PacketBlockAction(&a,sizeof(a));assert(local_player_blocks==4);is_demo=0;
 struct PacketBlockLine l={0,1,2,50,4,2,50};
 read_PacketBlockLine(&l,sizeof(l));assert(local_player_blocks==3); /* 2,3,4 already occupied */
 read_PacketBlockLine(&l,sizeof(l));assert(local_player_blocks==3);
 struct PacketBlockLine single={0,5,2,50,5,2,50};read_PacketBlockLine(&single,sizeof(single));assert(local_player_blocks==2);
 is_demo=1;single.sx=single.ex=6;read_PacketBlockLine(&single,sizeof(single));assert(local_player_blocks==2);
 puts("confirmed single and line stock accounting checks passed");return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'packet.c';p.write_text(packet_stub+packet_handlers+packet_checks)
    exe=Path(d)/'packet'
    subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror',str(p),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
