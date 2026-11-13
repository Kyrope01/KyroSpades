"""Exercise production AABB queries against exhaustive voxel and ray cases."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include <assert.h>
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include "aabb.h"
int map_size_x=8,map_size_y=8,map_size_z=8,locked,locks,reads;
unsigned char solid[8][8][8];
void map_read_lock(void){assert(!locked);locked=1;locks++;}
void map_read_unlock(void){assert(locked);locked=0;}
_Bool map_isair_nolock(int x,int y,int z){assert(locked);assert(x>=0&&x<8&&y>=0&&y<8&&z>=0&&z<8);reads++;return !solid[x][y][z];}
static uint32_t state=100;static uint32_t rng(void){state^=state<<13;state^=state>>17;state^=state<<5;return state;}
static float coord(void){return ((int)(rng()%65)-16)*.25f;}
static int oracle(AABB*a,int miny){
 for(int x=0;x<8;x++)for(int z=0;z<8;z++)for(int y=0;y<8;y++){
  AABB cube={.min={x,y,z},.max={x+1,y+1,z+1}};
  if(y>=miny && solid[x][y][z] && aabb_intersection(a,&cube))return 1;
 }return 0;
}
int main(void){
 AABB a={.min={1,1,1},.max={2,2,2}},b={.min={2,2,2},.max={3,3,3}};
 assert(aabb_intersection(&a,&b));
 Ray r={.origin.coords={-1,1.5,1.5},.direction.coords={1,0,0}};float d=-1;
 assert(aabb_intersection_ray(&a,&r,&d)&&fabsf(d-2)<1e-6);
 r.direction.x=1e-30f;assert(aabb_intersection_ray(&a,&r,&d)&&fabsf(d-2)<1e-5);
 r.direction.x=0;assert(!aabb_intersection_ray(&a,&r,&d));
 r.origin.x=1.5f;assert(!aabb_intersection_ray(&a,&r,&d));
 r.direction.x=NAN;assert(!aabb_intersection_ray(&a,&r,&d));
 r.direction.x=1;r.origin.x=-1;r.origin.y=1;assert(aabb_intersection_ray(&a,&r,&d));
 r.origin.y=0;r.direction.y=1; /* ray crosses the box at a single corner */
 r.direction.x=3;assert(!aabb_intersection_ray(&a,&r,&d));
 for(int x=0;x<8;x++)for(int y=0;y<8;y++)for(int z=0;z<8;z++)solid[x][y][z]=(rng()%5)==0;
 for(int n=0;n<50000;n++){
  for(int i=0;i<3;i++){float x=coord(),y=coord();a.min[i]=fminf(x,y);a.max[i]=fmaxf(x,y);}
  int miny=(int)(rng()%15)-4;
  int expected=oracle(&a,miny<0?0:miny);int got=aabb_intersection_terrain(&a,miny);
  assert(!locked);if(got!=expected){fprintf(stderr,"mismatch at %d\n",n);return 1;}
 }
 a=(AABB){.min={0,-3,0},.max={1,-1,1}};assert(!aabb_intersection_terrain(&a,-5));assert(!locked);
 a=(AABB){.min={FLT_MAX,0,0},.max={FLT_MAX,1,1}};assert(!aabb_intersection_terrain(&a,0));
 a.min_x=NAN;assert(!aabb_intersection_terrain(&a,0));
 a=(AABB){.min={2,2,2},.max={1,3,3}};assert(!aabb_intersection_terrain(&a,0));
 for(int x=0;x<8;x++)for(int y=0;y<8;y++)for(int z=0;z<8;z++)solid[x][y][z]=0;
 a=(AABB){.min={2.1f,2.1f,2.1f},.max={2.9f,2.9f,2.9f}};reads=locks=0;
 assert(!aabb_intersection_terrain(&a,0));assert(reads==1 && locks==1 && !locked);
 a=(AABB){.min={2,2,2},.max={2,2,2}};reads=locks=0;
 assert(!aabb_intersection_terrain(&a,0));assert(reads==8 && locks==1 && !locked);
 puts("AABB 50,000 differential queries, boundary, lock and ray checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(code);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined,float-cast-overflow','-ffunction-sections','-fdata-sections','-I'+str(root/'src'),'-I'+str(root/'deps'),'-I'+str(root/'deps/libvxl'),'-I'+str(root/'tools/bench/stubs'),str(p),str(root/'src/aabb.c'),'-Wl,--gc-sections','-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
