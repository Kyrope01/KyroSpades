"""Run real camera ADS, free-aim, and hit traversal against small fakes."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s=(root/'src/camera.c').read_text()
ads=s[s.index('#define CAMERA_ADS_ANIM_TIME'):s.index('\nvoid camera_apply()')]
hits=s[s.index('void camera_hit_fromplayer('):s.index('\nvoid camera_local_eye(')]
aabb=(root/'src/aabb.c').read_text(); aabb=aabb[aabb.index('bool aabb_intersection_ray('):aabb.index('\nvoid aabb_set_center(')]
stub=r'''
#include <math.h>
#include <stdbool.h>
#include <float.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#define PI 3.14159265358979323846F
#define DOUBLEPI (2*PI)
#define EPSILON 0.005F
#define CAMERA_DEFAULT_FOV 70.0F
#define PLAYERS_MAX 256
#define CAMERAMODE_FPS 0
#define CAMERAMODE_BODYVIEW 1
#define CAMERAMODE_SPECTATOR 2
#define TOOL_GUN 2
#define WEAPON_RIFLE 0
#define WEAPON_SMG 1
#define WEAPON_SHOTGUN 2
#define CAMERA_HITTYPE_NONE 0
#define CAMERA_HITTYPE_BLOCK 1
#define CAMERA_HITTYPE_PLAYER 2
struct V {float x,y,z;};
struct Player {int connected,alive,held_item,weapon; struct V pos,orientation; struct {struct V eye;} physics; struct {struct {int sprint,crouch;}keys;struct {int rmb;float rmb_start;}buttons;} input;};
struct Player players[PLAYERS_MAX]; unsigned char local_player_id; int camera_mode=CAMERAMODE_FPS,cameracontroller_bodyview_mode,cameracontroller_bodyview_player;
struct {int ads_zoom_animation,disable_dynamic_fov,free_aim;float camera_fov,rifle_ads_fov,smg_ads_fov,shotgun_ads_fov,free_aim_deadzone_h,free_aim_deadzone_v;} settings;
float camera_x,camera_y,camera_z,camera_rot_x,camera_rot_y,camera_crosshair_rot_x,camera_crosshair_rot_y,camera_muzzle_rot_x,camera_muzzle_rot_y;
float window_time(void){return 10;} void cameracontroller_spectator_rotate(float a,float b){(void)a;(void)b;}
float player_height(struct Player*p){(void)p;return 0;}
struct Camera_HitType {char type;float x,y,z,distance;int xb,yb,zb;unsigned char player_id,player_section;};
typedef struct {union {float coords[3];struct {float x,y,z;};} origin,direction;} Ray;
typedef struct {float min[3],max[3];} AABB;
float len3D(float x,float y,float z){return sqrtf(x*x+y*y+z*z);}
int map_size_x=300,map_size_y=8,map_size_z=8,block_x=-1;
bool map_isair(int x,int y,int z){(void)y;(void)z;return x!=block_x;}
struct player_intersection {int hit;float d;};
int collision_calls;
void player_collision(struct Player*p,Ray*r,struct player_intersection*i){(void)p;(void)r;collision_calls++;i->hit=1;i->d=5;}
int player_intersection_choose(struct player_intersection*i,float*d){*d=i->d;return 3;}
int player_intersection_exists(struct player_intersection*i){return i->hit;}
void camera_overflow_adjust(void);
int* camera_terrain_pickEx(unsigned char,float,float,float,float,float,float);
void camera_hit(struct Camera_HitType*,int,float,float,float,float,float,float,float);
void camera_hit_mask(struct Camera_HitType*,int,float,float,float,float,float,float,float);
'''
checks=r'''
int main(int argc,char**argv){
 if(argc>1){int n=atoi(argv[1]);settings.camera_fov=90;players[0].alive=1;players[0].input.keys.sprint=1;
 for(int i=0;i<n;i++)camera_fov_scaled(1.0f/n);printf("%.7f",camera_fov_scaled(0));return 0;}
 settings.camera_fov=90;settings.rifle_ads_fov=70;settings.smg_ads_fov=60;settings.shotgun_ads_fov=40;
 players[0].alive=1;players[0].held_item=TOOL_GUN;players[0].input.buttons.rmb=1;
 camera_ads_update(.016f);assert(camera_ads_progress==1 && camera_ads_scope_progress==1);
 assert(fabsf(camera_fov_scaled(0)-38.590f)<.02f);
 settings.smg_ads_fov=60;players[0].weapon=WEAPON_SMG;assert(fabsf(camera_fov_scaled(0)-32.204f)<.02f);
 players[0].input.keys.sprint=1;camera_ads_update(.016f);assert(!camera_ads_progress && !camera_ads_scope_progress);
 settings.free_aim=1;settings.free_aim_deadzone_v=30;camera_rot_y=2.5f;camera_crosshair_rot_y=2.5f;
 for(int i=0;i<30;i++)camera_look_delta(0,.05f);assert(fabsf(camera_rot_y-(PI-EPSILON))<.00001f);
 for(int i=0;i<30;i++)camera_look_delta(0,-.05f);assert(camera_rot_y<2.5f);
 float x,y,z;camera_vector_from_angles(0,0,&x,&y,&z);assert(x==0 && y==1 && z==0);
 struct Camera_HitType h;memset(&h,0xff,sizeof(h));camera_hit_fromplayer(&h,-1,4);assert(h.type==0 && h.distance==FLT_MAX && !h.player_id);
 players[0].alive=0;block_x=4;camera_hit_mask(&h,-1,1.5f,1.5f,1.5f,1,0,0,2.5f);assert(h.type==1 && fabsf(h.distance-2.5f)<.0001f);
 camera_hit_mask(&h,-1,1.5f,1.5f,1.5f,1,0,0,2.49f);assert(h.type==0);
 block_x=200;camera_hit_mask(&h,-1,1.5f,1.5f,1.5f,1,0,0,250);assert(h.type==1 && h.x==200);
 block_x=-1;players[1].connected=players[1].alive=1;players[1].pos.x=100;
 camera_hit_mask(&h,-1,1.5f,1.5f,1.5f,1,0,0,4.9f);assert(h.type==0);
 camera_hit_mask(&h,-1,1.5f,1.5f,1.5f,1,0,0,5.0f);assert(h.type==2 && h.player_id==1);
 int before=collision_calls;block_x=4;camera_hit_terrain(&h,1.5f,1.5f,1.5f,20,0,0,2.5f);
 assert(h.type==1 && fabsf(h.distance-2.5f)<.0001f && collision_calls==before);
 camera_hit_terrain(&h,1.5f,1.5f,1.5f,20,0,0,2.49f);assert(h.type==0 && collision_calls==before);
 puts("Camera ADS, pole, hit range and long traversal checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(stub+aabb+ads+hits+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined',str(p),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
 a=float(subprocess.check_output([str(exe),'30'],text=True));b=float(subprocess.check_output([str(exe),'240'],text=True))
 assert abs(a-b)<0.0001, (a,b)
assert 'glmc_mat4_mul(n, m, m)' in (root/'src/matrix.c').read_text()
assert '#define PLAYERS_MAX (PLAYER_ID_SERVER + 1)' in (root/'src/player.h').read_text()
assert 'extern unsigned char local_player_id;' in (root/'src/player.h').read_text()
print('Frustum multiplication and local player ID checked')

render=s[s.index('static struct tesselator camera_inside_tess;'):s.index('\nvoid camera_ExtractFrustum')]
stub=r'''
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
typedef int GLboolean;typedef int GLint;typedef unsigned GLenum;
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_CULL_FACE 1
#define GL_BLEND 2
#define GL_TEXTURE_2D 3
#define GL_DEPTH_WRITEMASK 4
#define GL_DEPTH_FUNC 5
#define GL_LEQUAL 6
#define VERTEX_FLOAT 1
#define CUBE_FACE_X_N 0
#define CUBE_FACE_X_P 1
#define CUBE_FACE_Y_N 2
#define CUBE_FACE_Y_P 3
#define CUBE_FACE_Z_N 4
#define CUBE_FACE_Z_P 5
#define rgba(a,b,c,d) 0
struct tesselator {void* vertices;int faces;};
int map_size_x=10,map_size_y=10,map_size_z=10,solid=1,gles_version=2,glx_version=1;
float camera_x=1.5,camera_y=1.5,camera_z=1.5;
int matrix_model=42,saved,uploads,draws,mask=0,depth=77,states[4]={0,1,1,1};
unsigned prog=22;
bool map_isair(int x,int y,int z){return !(solid && x==1 && y==1 && z==1);}
void tesselator_create(struct tesselator*t,int a,int b,int c){(void)a;(void)b;(void)c;t->vertices=t;}
void tesselator_clear(struct tesselator*t){t->faces=0;}void tesselator_set_color(struct tesselator*t,int c){(void)t;(void)c;}
void tesselator_addf_cube_face(struct tesselator*t,int f,float x,float y,float z,float size){(void)f;(void)x;(void)y;(void)z;(void)size;t->faces++;}
void tesselator_draw(struct tesselator*t,int c){assert(c==1 && t->faces==6 && matrix_model==0 && mask==1 && depth==GL_LEQUAL);draws++;}
int glIsEnabled(int n){return states[n];}void glDisable(int n){assert(n!=GL_TEXTURE_2D || gles_version<2);states[n]=0;}void glEnable(int n){states[n]=1;}
void glGetBooleanv(int n,GLboolean*p){assert(n==GL_DEPTH_WRITEMASK);*p=mask;}
void glGetIntegerv(int n,GLint*p){assert(n==GL_DEPTH_FUNC);*p=depth;}
void glDepthFunc(int n){depth=n;}void glDepthMask(int n){mask=n;}
void matrix_push(int m){saved=m;}void matrix_identity(int m){(void)m;matrix_model=0;}void matrix_pop(int m){(void)m;matrix_model=saved;}void matrix_upload(void){uploads++;}
unsigned glx_current_program(void){return prog;}void glx_use_default_shader(void){prog=33;}void glx_use_program(unsigned p){prog=p;}
'''
checks=r'''
int main(void){
#ifndef OPENGL_ES
 gles_version=1;
#endif
 camera_inside_block_render();assert(draws==1 && matrix_model==42 && uploads==2 && mask==0 && depth==77 && states[1] && states[2] && states[3]);
#ifdef GLX_PROGRAMMABLE
 assert(prog==22);
#endif
 solid=0;camera_inside_block_render();assert(draws==1 && uploads==2);puts("Camera inside-block state restoration passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'render.c';p.write_text(stub+render+checks)
 for flags in ([],['-DOPENGL_CORE','-DGLX_PROGRAMMABLE'],['-DOPENGL_ES','-DGLX_PROGRAMMABLE']):
  exe=Path(d)/('test'+str(len(flags))+('es' if '-DOPENGL_ES' in flags else ''))
  subprocess.run(['gcc','-std=c99',*flags,str(p),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)

# hitbox_intersection receives transformed ray coordinates: verify that a
# scaled model's local-space distance is converted back to world units.
player=(root/'src/player.c').read_text()
hitbox=player[player.index('static bool hitbox_intersection('):player.index('\nvoid player_collision',player.index('static bool hitbox_intersection('))]
stub=r'''
#include <math.h>
#include <stdbool.h>
#include <assert.h>
#include <float.h>
#include <stdio.h>
typedef float mat4[16];typedef float vec3[3];
typedef struct {union {float coords[3];struct {float x,y,z;};} origin,direction;} Ray;
typedef struct {float min[3],max[3];} AABB;
struct hitbox {float pivot[3],size[3],scale;};
float len3D(float x,float y,float z){return sqrtf(x*x+y*y+z*z);}
void glmc_mat4_inv(mat4 m,mat4 out){(void)m;(void)out;}
void glmc_mat4_mulv3(mat4 m,float*v,float w,float*out){(void)m;(void)w;for(int i=0;i<3;i++)out[i]=v[i]*.5f;}
'''
checks=r'''
int main(void){mat4 m={0};struct hitbox b={{0,0,0},{1,1,1},1};Ray r={.origin.coords={-4,.5,.5},.direction.coords={1,0,0}};float d;
 assert(hitbox_intersection(m,&b,&r,&d) && fabsf(d-4)<.0001f);puts("Scaled player hit distance passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'hitbox.c';p.write_text(stub+aabb+hitbox+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11',str(p),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
