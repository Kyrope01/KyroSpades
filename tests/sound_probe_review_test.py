from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'src/sound.c').read_text();a=s.index('static float sound_occlusion_probe(');b=s.index('\nstatic void sound_apply_occlusion',a)
h=r'''
#include <math.h>
#include <assert.h>
#include <stdio.h>
#define CAMERA_HITTYPE_BLOCK 1
struct Camera_HitType {int type;float distance;};
float camera_x=10,camera_y=5,camera_z=5;int calls,mode;
void camera_hit_terrain(struct Camera_HitType* h,float x,float y,float z,float rx,float ry,float rz,float range){
 (void)x;(void)y;(void)z;calls++;assert(fabsf(range-sqrtf(rx*rx+ry*ry+rz*rz))<0.0001f);
 h->type=mode;h->distance=mode==1?range*0.25f:0;
}
''' + s[a:b] + r'''
int main(void){assert(sound_occlusion_probe(9.5f,5,5)==0 && calls==0);
 mode=1;assert(sound_occlusion_probe(1,5,5)==1 && calls==27);
 mode=0;assert(sound_occlusion_probe(1,5,5)==0 && calls==54);
 assert(sound_occlusion_probe(NAN,5,5)==0 && calls==54);puts("terrain-only occlusion probes passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-lm','-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
assert 'camera_hit_mask(' not in s[s.index('static float sound_occlusion_probe'):s.index('#endif /* USE_SOUND */',s.index('static float sound_occlusion_probe'))]
