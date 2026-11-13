"""Exercise main.c history, RNG, and dropped objective geometry under UBSan."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root / 'src/main.c').read_text()
def extract(a,b): return s[s.index(a):s.index(b,s.index(a))]
a=s.index('uint32_t ms_seed = 1;'); rng=s[a:s.index('\n}',s.index('int ms_rand(void)',a))+2]
# Limit extraction to the single function regardless of the next API name.
a=s.index('static void chat_push_one('); chat=s[a:s.index('\n}',a)+2]
intel=extract('static bool intel_drop_render_y(', '\nstatic void render_objective_models')
harness=r'''
#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
char chat[3][128][256]; unsigned int chat_color[3][128]; float chat_timer[3][128];
float window_time(void) { return 99; }
int map_size_x=512,map_size_y=64,map_size_z=512, solid=20, locks=0;
struct { float zpiv, scale; } model_intel={-5.5f,0.1f};
void map_read_lock(void){locks++;} void map_read_unlock(void){locks--;}
bool map_isair_nolock(int x,int y,int z){ assert(locks==1 && x>=0 && x<512 && z>=0 && z<512 && y>=0 && y<64); return y!=solid; }
''' + rng + '\n' + chat + '\n' + intel + r'''
int main(void) {
 uint32_t ref=1; for(int i=0;i<100000;i++){ ref=ref*0x343fdu+0x269ec3u; assert(ms_rand()==(int)((ref>>16)&0x7fff)); }
 for(int i=1;i<128;i++){snprintf(chat[0][i],256,"%d",i);chat_color[0][i]=i;chat_timer[0][i]=i;}
 chat_push_one(0,42,"new"); assert(!strcmp(chat[0][127],"126") && chat_color[0][127]==126 && chat_timer[0][127]==126);
 float y; assert(intel_drop_render_y(1,2,10,&y)); assert(fabsf(y-(21-0.55f))<0.001f); assert(!locks);
 assert(!intel_drop_render_y(NAN,2,10,&y)); assert(!intel_drop_render_y(1,INFINITY,10,&y)); assert(!intel_drop_render_y(1,2,NAN,&y)); assert(!intel_drop_render_y(1e30f,2,10,&y));
 solid=-1; assert(intel_drop_render_y(1,2,1e30f,&y)); assert(y==0); assert(!locks);
 puts("main review behavior verified");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d); (p/'test.c').write_text(harness)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined,float-cast-overflow','-fno-sanitize-recover=all',str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
assert 'chat_cursor_clamp();\n        if(!chat_sel_active())' in (root/'src/hud.c').read_text()
assert 'GLuint texture;' in s and '(GLuint*)&screenshot_anim.texture' not in s
