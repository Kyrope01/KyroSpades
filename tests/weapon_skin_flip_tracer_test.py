"""Exercise actual skin flip persistence and tracer selection with small C stubs."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
skins = (root/'src/skins.c').read_text()
model = (root/'src/model.c').read_text()
hud = (root/'src/hud.c').read_text()
flips = skins[skins.index('struct skin_flip_entry'):skins.index('\nstatic int entry_compare')]
tracer = skins[skins.index('\t/* Tracers follow'):skins.index('\n\tfor(int s = 0;', skins.index('\t/* Tracers follow'))]
wrapper = model[model.index('void kv6_render(struct kv6_t* kv6, unsigned char team) {'):]
stub = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <assert.h>
#include <math.h>
#define SKIN_NAME_MAX 48
#define SKIN_CATEGORIES 8
enum skin_category_type {SKIN_SPADE,SKIN_GRENADE,SKIN_RIFLE,SKIN_SMG,SKIN_SHOTGUN,SKIN_PLAYER,SKIN_INTEL,SKIN_TENT};
struct kv6_t {bool flip_yaw; int xsiz,ysiz; float xpiv,ypiv,scale;};
struct skin_entry {char suffix[48]; struct kv6_t* previews;};
struct skin_model_def {struct kv6_t* ptr; const char* basename;};
struct skin_category {struct skin_model_def models[1]; struct skin_entry* entries; int count;};
struct skin_category skin_categories[8];
struct {int skin_rifle,skin_smg,skin_shotgun;} settings;
#define log_warn(...) ((void)0)
#define file_open fopen
#define file_close fclose
#define file_printf fprintf
int file_exists(const char* p) {FILE*f=fopen(p,"rb");if(!f)return 0;fclose(f);return 1;}
unsigned char* file_load(const char*p) {FILE*f=fopen(p,"rb");fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);unsigned char*d=calloc(n+1,1);fread(d,1,n,f);fclose(f);return d;}
struct kv6_t model_semi_tracer,model_smg_tracer,model_shotgun_tracer;
char last[256]; int fail_custom;
int kv6_reload(struct kv6_t* t, const char*p,float s) {(void)t;assert(s==0.05f);strcpy(last,p);return fail_custom && strchr(p,'(') ? -1:0;}
'''
checks = r'''
void load_tracer(enum skin_category_type category, struct skin_category* cat, struct skin_entry* e, int models) {
''' + tracer + r'''
}
static float tx,tz,angle,savedx,savedz,saveda; static int pushes,pops,uploads,renders;
int matrix_model;
void matrix_push(int m) {(void)m;savedx=tx;savedz=tz;saveda=angle;pushes++;}
void matrix_pop(int m) {(void)m;tx=savedx;tz=savedz;angle=saveda;pops++;}
void matrix_translate(int m,float x,float y,float z) {(void)m;(void)y;if(angle==180){tx-=x;tz-=z;}else{tx+=x;tz+=z;}}
void matrix_rotate(int m,float a,float x,float y,float z) {(void)m;assert(x==0 && y==1 && z==0);angle+=a;}
void matrix_upload(void) {uploads++;}
void kv6_render_unflipped(struct kv6_t*k,unsigned char team) {(void)team;renders++;if(k->flip_yaw){assert(fabsf(tx-0.6f)<0.001f);assert(fabsf(tz-1.6f)<0.001f);assert(angle==180);}}
''' + wrapper + r'''
int main(void) {
 struct kv6_t active={0},p0={0},p1={0},p2={0};
 struct skin_entry rifle[2]={{"",&p0},{"RPG 7!",&p1}}, smg[1]={{"RPG 7!",&p2}};
 struct skin_category *r=&skin_categories[SKIN_RIFLE], *s=&skin_categories[SKIN_SMG];
 r->entries=rifle;r->count=2;r->models[0]=(struct skin_model_def){&active,"semi"};
 s->entries=smg;s->count=1;s->models[0]=(struct skin_model_def){&model_smg_tracer,"smg"};
 settings.skin_rifle=1;
 flip_load(); assert(!skins_flip_get(SKIN_RIFLE,1));
 skins_flip_toggle(SKIN_RIFLE,1); assert(p1.flip_yaw && active.flip_yaw && skins_flip_get(SKIN_RIFLE,1));
 skins_flip_toggle(SKIN_SMG,0); skins_flip_toggle(SKIN_RIFLE,0);
 flip_load(); assert(skins_flip_get(SKIN_RIFLE,0) && skins_flip_get(SKIN_RIFLE,1) && skins_flip_get(SKIN_SMG,0));
 struct skin_entry tmp=rifle[0];rifle[0]=rifle[1];rifle[1]=tmp;
 assert(skins_flip_get(SKIN_RIFLE,0)); skins_flip_toggle(SKIN_RIFLE,0); flip_load();
 assert(!skins_flip_get(SKIN_RIFLE,0) && skins_flip_get(SKIN_RIFLE,1) && skins_flip_get(SKIN_SMG,0));
 strcpy(rifle[0].suffix,"RPG7"); system("mkdir -p kv6; touch 'kv6/semitracer(RPG7).kv6'");
 load_tracer(SKIN_RIFLE,r,&rifle[0],1);assert(!strcmp(last,"kv6/semitracer(RPG7).kv6"));
 fail_custom=1;load_tracer(SKIN_RIFLE,r,&rifle[0],1);assert(!strcmp(last,"kv6/semitracer.kv6"));fail_custom=0;
 load_tracer(SKIN_RIFLE,r,&rifle[1],1);assert(!strcmp(last,"kv6/semitracer.kv6"));
 load_tracer(SKIN_SMG,s,&smg[0],1);assert(!strcmp(last,"kv6/smgtracer.kv6"));
 strcpy(last,"untouched");load_tracer(SKIN_RIFLE,r,&rifle[0],0);assert(!strcmp(last,"untouched"));
 struct kv6_t k={true,10,20,2,2,0.1f};kv6_render(&k,0);assert(pushes==1 && pops==1 && uploads==2 && renders==1 && tx==0 && tz==0 && angle==0);
 k.flip_yaw=false;kv6_render(&k,0);assert(pushes==1 && renders==2);kv6_render(NULL,0);
 puts("weapon flip and tracer checks passed");
}
'''
assert 'skin_reload_model(category, cat, m, cat->models[m].ptr, path)' in skins
assert 'cat->models[m].ptr->flip_yaw = ' in skins
assert 'flip_load();\n\tskins_scan();' in skins
assert 'Flip selected weapon:' in hud and 'skins_flip_toggle' in hud
assert 'static void kv6_render_unflipped' in model and 'settings.voxlap_models' in model
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c'; p.write_text(stub+flips+checks)
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror',str(p),'-lm','-o',str(Path(d)/'test')],check=True)
    subprocess.run([str(Path(d)/'test')],cwd=d,check=True)
