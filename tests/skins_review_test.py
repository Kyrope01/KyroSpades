"""Test real flip parser, scope replacement and preview matrix isolation with fakes."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/skins.c').read_text()
parser=s[s.index('struct skin_flip_entry {'):s.index('\nstatic void flip_save(')]
scope=s[s.index('static void skin_load_scope('):s.index('\nint skins_apply(')]
stub=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#define SKIN_NAME_MAX 16
#define log_warn(...) ((void)0)
enum skin_category_type {SKIN_RIFLE=2,SKIN_SMG=3,SKIN_SHOTGUN=4};
static char* input;
int file_exists(const char* p){(void)p;return 1;}
unsigned char* file_load(const char* p){(void)p;size_t n=strlen(input)+1;char* b=malloc(n);memcpy(b,input,n);return (unsigned char*)b;}
'''
check=r'''
int main(void) {
 char buf[4096];memset(buf,'a',sizeof(buf));
 memcpy(buf,"2 ",2);buf[2000]='\n';strcpy(buf+2001,"2 4142\n3 -\n999999999999999999999999 4142\n2 0001\n2 414\n2 4142434445464748494a4b4c4d4e4f50\n4 4344\n");input=buf;
 flip_load();assert(skin_flip_count==3);assert(flip_find(2,"AB")>=0);assert(flip_find(3,"")>=0);assert(flip_find(4,"CD")>=0);free(skin_flips);
 puts("flip parser bounds passed");
}
'''
scope_stub=r'''
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#define log_warn(...) ((void)0)
enum skin_category_type {SKIN_RIFLE,SKIN_SMG,SKIN_SHOTGUN};
static int fail_custom,fail_default,calls,deletes;
struct texture {int id;};
int texture_create(struct texture* t,char* path) {calls++;if((!strcmp(path,"custom")&&fail_custom)||(!strcmp(path,"default")&&fail_default))return 0;t->id=!strcmp(path,"custom")?2:3;return 1;}
void texture_delete(struct texture* t){assert(t->id);deletes++;}
int legacy_mod_reload_suffix(const char* s){return !strcmp(s,"legacy");}
'''
scope_check=r'''
int main(void){struct texture t={1};char custom[]="custom";char def[]="default";
 skin_load_scope(&t,custom,def);assert(t.id==2&&calls==1&&deletes==1);
 fail_custom=1;skin_load_scope(&t,custom,def);assert(t.id==3&&calls==3&&deletes==2);
 fail_default=1;skin_load_scope(&t,custom,def);assert(t.id==3&&calls==5&&deletes==2);
 skin_load_scope(&t,def,def);assert(t.id==3&&calls==6&&deletes==2);
 assert(!sound_uses_skin_suffix(SKIN_RIFLE,0,""));assert(sound_uses_skin_suffix(SKIN_RIFLE,0,"other"));
 assert(!sound_uses_skin_suffix(SKIN_RIFLE,1,"other"));assert(sound_uses_skin_suffix(SKIN_RIFLE,1,"legacy"));
 assert(sound_uses_skin_suffix(SKIN_SMG,0,"other"));puts("scope fallback and sound policy passed");}
'''
with tempfile.TemporaryDirectory() as d:
 for name,code in [('flip',stub+parser+check),('scope',scope_stub+scope+scope_check)]:
  p=Path(d);(p/(name+'.c')).write_text(code)
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/(name+'.c')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
assert 'memcpy(saved_view, matrix_view' in s and 'matrix_identity(matrix_view)' in s and 'memcpy(matrix_view, saved_view' in s
scan=s[s.index('void skins_scan(void) {'):s.index('\n/* A failed decode')]
scan_stub=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define SKIN_CATEGORIES 8
#define SKIN_NAME_MAX 48
#define log_warn(...) ((void)0)
enum skin_category_type {SKIN_SPADE,SKIN_GRENADE,SKIN_RIFLE,SKIN_SMG,SKIN_SHOTGUN,SKIN_PLAYER,SKIN_INTEL,SKIN_TENT};
enum skin_player_model {SKIN_PLAYER_HEAD,SKIN_PLAYER_TORSO,SKIN_PLAYER_TORSO_CROUCH,SKIN_PLAYER_ARMS,SKIN_PLAYER_LEG,SKIN_PLAYER_LEG_CROUCH,SKIN_PLAYER_DEAD,SKIN_PLAYER_MODEL_COUNT};
struct kv6_t {void* voxels;bool flip_yaw;};struct sound{int x;};
struct skin_model_def {struct kv6_t* ptr;const char* basename;float scale;};
struct skin_sound_def {struct sound* ptr;const char* basename;};
struct skin_entry {char name[48],suffix[48];struct kv6_t* previews;};
struct skin_category {char label[32];struct skin_model_def models[8];struct skin_sound_def sounds[4];int model_count,sound_count,count;struct skin_entry* entries;} skin_categories[8];
struct kv6_t model_spade,model_grenade,model_semi,model_smg,model_shotgun,model_playerhead,model_playertorso,model_playertorsoc,model_playerarms,model_playerleg,model_playerlegc,model_playerdead,model_intel,model_tent;
struct sound sound_rifle_shoot,sound_rifle_reload,sound_smg_shoot,sound_shotgun_shoot;
static int fail_malloc,fail_calloc,enumerations;
static void* test_malloc(size_t n){if(fail_malloc){fail_malloc=0;return NULL;}return malloc(n);}
static void* test_calloc(size_t n,size_t z){if(fail_calloc){fail_calloc=0;return NULL;}return calloc(n,z);}
#define malloc test_malloc
#define calloc test_calloc
void kv6_rebuild(struct kv6_t* p){(void)p;}
int skin_reload_model(enum skin_category_type c,struct skin_category*cat,int m,struct kv6_t*k,const char*p){(void)c;(void)cat;(void)m;(void)k;(void)p;return -1;}
bool skins_weapon_category(enum skin_category_type c){return c==SKIN_RIFLE||c==SKIN_SMG||c==SKIN_SHOTGUN;}
int flip_find(int c,const char* s){(void)c;(void)s;return -1;}
struct skin_scan_context {struct skin_category* cat;enum skin_category_type category;int capacity;};
void skin_scan_file(const char* s,void*p){(void)s;(void)p;}
void file_dir_list(const char* p,void(*cb)(const char*,void*),void* ctx){(void)p;(void)cb;(void)ctx;enumerations++;}
int entry_compare(const void*a,const void*b){(void)a;(void)b;return 0;}
'''
scan_check=r'''
int main(void){fail_malloc=1;skins_scan();assert(!skin_categories[0].entries&&!skin_categories[0].count&&skin_categories[1].count==1&&enumerations==7);
 fail_calloc=1;enumerations=0;skins_scan();assert(!skin_categories[0].entries&&!skin_categories[0].count&&skin_categories[1].count==1&&enumerations==7);
 skins_scan();for(int i=0;i<8;i++)assert(skin_categories[i].count==1&&skin_categories[i].entries);
 for(int i=0;i<8;i++){free(skin_categories[i].entries[0].previews);free(skin_categories[i].entries);}
 puts("skin allocation failure handling passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'scan.c').write_text(scan_stub+scan+scan_check)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'scan.c'),'-o',str(p/'scan')],check=True)
 subprocess.run([str(p/'scan')],check=True)
preview=s[s.index('void skins_render_preview('):s.index('\nvoid skins_init(')]
preview_stub=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef float mat4[16];mat4 matrix_model,matrix_view,matrix_projection;
enum skin_category_type {SKIN_PLAYER,SKIN_INTEL,SKIN_TENT,SKIN_RIFLE};
enum {SKIN_PLAYER_HEAD,SKIN_PLAYER_TORSO,SKIN_PLAYER_TORSO_CROUCH,SKIN_PLAYER_ARMS,SKIN_PLAYER_LEG,SKIN_PLAYER_LEG_CROUCH,SKIN_PLAYER_DEAD,SKIN_PLAYER_MODEL_COUNT};
#define SKIN_CATEGORIES 4
struct kv6_t {float xpiv,ypiv,zpiv,scale;};struct skin_entry {struct kv6_t* previews;};
struct skin_category {int count,model_count;struct skin_entry* entries;} skin_categories[4];
struct {int window_width,window_height;} settings={800,600};
float window_time(void){return 1;}
void matrix_identity(float* p){memset(p,0,sizeof(mat4));p[0]=1;}
void matrix_ortho(float*p,float a,float b,float c,float d,float e,float f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;p[0]=2;}
void matrix_upload_p(void){} void matrix_upload(void){}
void matrix_translate(float*p,float x,float y,float z){(void)p;(void)x;(void)y;(void)z;}
void matrix_scale(float*p,float x,float y,float z){(void)p;(void)x;(void)y;(void)z;}
void matrix_rotate(float*p,float a,float x,float y,float z){(void)p;(void)a;(void)x;(void)y;(void)z;}
void matrix_push(float*p){(void)p;}void matrix_pop(float*p){(void)p;}
int drawn;void kv6_render(struct kv6_t*p,int team){(void)p;(void)team;assert(matrix_view[0]==1);drawn++;}
'''
preview_check=r'''
int main(void){struct kv6_t models[7]={0};struct skin_entry e={models};skin_categories[0]=(struct skin_category){1,7,&e};
 matrix_model[0]=11;matrix_projection[0]=22;matrix_view[0]=33;skins_render_preview(SKIN_PLAYER,0,0,0,1);
 assert(drawn==4&&matrix_model[0]==11&&matrix_projection[0]==22&&matrix_view[0]==33);
 skins_render_preview(SKIN_PLAYER,7,0,0,1);assert(drawn==4&&matrix_view[0]==33);puts("preview view isolation passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'preview.c').write_text(preview_stub+preview+preview_check)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'preview.c'),'-o',str(p/'preview')],check=True)
 subprocess.run([str(p/'preview')],check=True)
