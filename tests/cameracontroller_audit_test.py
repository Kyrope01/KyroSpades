"""Execute real controller paths with fake physics and map collisions."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/cameracontroller.c').read_text()
death=s[s.index('void cameracontroller_death_init('):s.index('\nvoid cameracontroller_death_render')]
crouch=s[s.index('float last_cy;'):s.index('\nvoid cameracontroller_fps(')]
body=s[s.index('void cameracontroller_bodyview(float dt)'):s.index('\nvoid cameracontroller_bodyview_render')]
stub=r'''
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define CAMERAMODE_DEATH 1
#define CAMERAMODE_BODYVIEW 2
#define PLAYERS_MAX 256
#define len3D(x,y,z) sqrt((x)*(x)+(y)*(y)+(z)*(z))
struct Vec {float x,y,z;};
struct Player {int alive; struct Vec pos;struct {struct Vec eye,velocity; int airborne;}physics;struct {struct {int crouch;}keys;}input;};
struct Player players[PLAYERS_MAX];unsigned char local_player_id;
struct {int crouch_instant;} settings;
struct Vec view_offset;
int camera_mode,cameracontroller_bodyview_player,cameracontroller_bodyview_mode;
float camera_x,camera_y,camera_z,camera_vx,camera_vy,camera_vz,camera_rot_x,camera_rot_y,camera_size=.8,camera_height=.8,cameracontroller_bodyview_zoom;
float cam_shake_value,cam_bob_strength,crouch_cam_offset,gunlag_vel_smooth;
float cameracontroller_death_velocity_x,cameracontroller_death_velocity_y,cameracontroller_death_velocity_z;
int snaps,uncrouch_ok=1;
void player_prev_snap(int n){(void)n;snaps++;}
int player_uncrouch(struct Player*p){if(!uncrouch_ok)return 0;if(!p->physics.airborne){p->pos.y+=.9f;p->physics.eye.y+=.9f;}return 1;}
int player_can_spectate(const struct Player*p){return p==&players[0];}
float player_height2(const struct Player*p){return p->alive?0:1;}float player_height(const struct Player*p){(void)p;return 1;}
typedef struct {float x,y,z;} AABB;
void aabb_set_size(AABB*a,float x,float y,float z){(void)a;(void)x;(void)y;(void)z;}
void aabb_set_center(AABB*a,float x,float y,float z){a->x=x;a->y=y;a->z=z;}
int tests,kind;float wall;
bool aabb_intersection_terrain(AABB*a,int miny){(void)miny;tests++;
 if(kind==0)return false;if(kind==1)return true;
 return a->x>=wall || (a->y<0.3f && a->z<wall);}
'''
reference=r'''
static void old_scan(float*back,float*front){
 AABB a; aabb_set_size(&a,.4,.4,.4);*back=*front=-1;
 for(float k=0;k<5;k+=.05f){
  if(*back>=0 && *front>=0)break;
  aabb_set_center(&a,players[0].pos.x-sin(camera_rot_x)*sin(camera_rot_y)*k,
   players[0].pos.y-cos(camera_rot_y)*k+player_height2(&players[0]),
   players[0].pos.z-cos(camera_rot_x)*sin(camera_rot_y)*k);
  if(aabb_intersection_terrain(&a,0) && *back<0)*back=fmax(k-.1f,0);
  aabb_set_center(&a,players[0].pos.x+sin(camera_rot_x)*sin(camera_rot_y)*k,
   players[0].pos.y+cos(camera_rot_y)*k+player_height2(&players[0]),
   players[0].pos.z+cos(camera_rot_x)*sin(camera_rot_y)*k);
  if(!aabb_intersection_terrain(&a,0) && *front<0)*front=fmax(k-.1f,0);
 }
 if(*back<0)*back=5;if(*front<0)*front=5;
}
static uint32_t randstate=42;static uint32_t rnd(void){randstate^=randstate<<13;randstate^=randstate>>17;randstate^=randstate<<5;return randstate;}
'''
checks=r'''
int main(void){
 camera_x=2;camera_y=3;camera_z=4;cameracontroller_death_init(0,2,3,4);
 assert(camera_mode==CAMERAMODE_DEATH && cameracontroller_death_velocity_x==0 && cameracontroller_death_velocity_y==0 && cameracontroller_death_velocity_z==0);
 kind=0;cameracontroller_death(.1f);assert(isfinite(camera_x)&&isfinite(camera_y)&&isfinite(camera_z) && camera_vy==cameracontroller_death_velocity_y);
 cameracontroller_death_init(0,camera_x-1,camera_y,camera_z);assert(fabsf(cameracontroller_death_velocity_x-3)<1e-5f);
 struct Player*p=&players[0];p->pos.y=p->physics.eye.y=10;p->physics.airborne=0;
 for(int setting=0;setting<2;setting++){
  settings.crouch_instant=setting;crouch_cam_offset=0;snaps=0;
  cameracontroller_update_crouch(p,1);assert(p->input.keys.crouch&&fabsf(p->pos.y-9.1f)<1e-5);
  for(int i=0;i<240;i++)cameracontroller_update_crouch(p,1);assert(fabsf(p->pos.y-9.1f)<1e-5);
  uncrouch_ok=0;cameracontroller_update_crouch(p,0);assert(p->input.keys.crouch && fabsf(p->pos.y-9.1f)<1e-5);
  uncrouch_ok=1;cameracontroller_update_crouch(p,0);assert(!p->input.keys.crouch && fabsf(p->pos.y-10)<1e-5);
  assert(!setting || fabsf(crouch_cam_offset)<1e-5);
 }
 p->physics.airborne=1;crouch_cam_offset=0;cameracontroller_update_crouch(p,1);assert(p->input.keys.crouch && p->pos.y==10);
 cameracontroller_update_crouch(p,0);assert(!p->input.keys.crouch && p->pos.y==10 && crouch_cam_offset==0);
 p->physics.airborne=0;p->alive=0;players[0].pos.x=1;players[0].pos.y=1;players[0].pos.z=1;
 for(int i=0;i<2000;i++){
  camera_rot_x=(rnd()%6283)/1000.0f;camera_rot_y=(rnd()%3130)/1000.0f;
  kind=(int)(rnd()%3);wall=(rnd()%500)/100.0f;float back,front;
  tests=0;old_scan(&back,&front);int old_calls=tests;
  float expected=back<=0?-front:back;
  tests=0;cameracontroller_bodyview_zoom=0;cameracontroller_bodyview_mode=0;cameracontroller_bodyview_player=0;
  cameracontroller_bodyview(1);assert(fabsf(cameracontroller_bodyview_zoom-expected)<1e-5f);assert(tests<=old_calls);
  if(kind==0)assert(tests<=101 && old_calls>=198);
 }
 puts("Controller death, crouch, and 2000 bodyview scan comparisons passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'controller.c';p.write_text(stub+death+crouch+body+reference+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined',str(p),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
assert 'unsigned char crouch : 1;' in (root/'src/player.h').read_text()
selection=s[s.index('void cameracontroller_selection(float dt)'):s.index('void cameracontroller_selection_render()')]
assert 'matrix_rotate' not in selection and 'matrix_translate' not in selection
print('Controller input and selection integration checks passed')
