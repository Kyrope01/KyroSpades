"""Exercise the production per-model cache policy and greedy scratch algorithm."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/model.c').read_text()
helper=s[s.index('static void kv6_sync_render_settings('):s.index('\nstatic void kv6_render_unflipped(')]
greedy=s[s.index('static int kv6_voxel_cmp('):s.index('\n/* Keep point-light equations')]
header=r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#define KV6_VIS_NEG_X 1
#define KV6_VIS_POS_X 2
#define KV6_VIS_NEG_Z 4
#define KV6_VIS_POS_Z 8
#define KV6_VIS_POS_Y 16
#define KV6_VIS_NEG_Y 32
struct kv6_voxel {uint16_t x,y,z;uint8_t visfaces;uint32_t color;};
struct kv6_t {uint16_t xsiz,ysiz,zsiz;bool has_display_list,display_list_point,display_list_greedy;struct kv6_voxel* voxels;int voxel_count;};
struct {int greedy_meshing;} settings;
int destroyed;
void kv6_rebuild(struct kv6_t* p) {if(p->has_display_list){p->has_display_list=false;destroyed++;}}
'''
checks=r'''
int main(void) {
 struct kv6_t a={0}, preview={0};
 kv6_sync_render_settings(&a,0,0);a.has_display_list=1;
 kv6_sync_render_settings(&preview,0,0);preview.has_display_list=1;
 kv6_sync_render_settings(&a,1,0);assert(destroyed==1&&!a.has_display_list&&a.display_list_point);
 a.has_display_list=1;kv6_sync_render_settings(&a,1,1);assert(destroyed==1&&a.has_display_list);
 kv6_sync_render_settings(&preview,1,1);assert(destroyed==2&&!preview.has_display_list);
 kv6_sync_render_settings(&a,0,1);assert(destroyed==3&&!a.has_display_list&&a.display_list_greedy);
 a.has_display_list=1;kv6_sync_render_settings(&a,0,0);assert(destroyed==4&&!a.has_display_list);
 struct kv6_voxel vox[3]={{.x=0,.y=0,.z=0,.visfaces=16,.color=1},
  {.x=0,.y=1,.z=0,.visfaces=16,.color=1}, {.x=1,.y=0,.z=0,.visfaces=16,.color=1}};
 uint8_t marked[3]={0};struct kv6_voxel** scratch=malloc(65535*sizeof(*scratch));assert(scratch);
 struct kv6_t k={.xsiz=65535,.ysiz=65535,.zsiz=65535,.voxels=vox,.voxel_count=3};size_t x,y;
 settings.greedy_meshing=1;greedy_mesh(&k,vox,marked,&x,&y,16,scratch);
 assert(x==1&&y==2&&marked[0]==16&&marked[1]==16&&marked[2]==0); /* rollback partial row */
 settings.greedy_meshing=0;greedy_mesh(&k,vox,marked,&x,&y,16,NULL);assert(x==1&&y==1);
 free(scratch);puts("KV6 cache and greedy workspace passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(header+helper+greedy+checks)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
assert 'kv6_sync_render_settings(kv6, settings.voxlap_models, settings.greedy_meshing)' in s
assert 'struct kv6_voxel* recent[*max_b]' not in s
assert s.count('KV6_VIS_POS_Y, recent)')==1
lighting=(root/'src/lighting.c').read_text()
assert 'fill = 0.30 * fill * fill * range;' in lighting
assert '(range + fill * range)' in s and '(range+fill*range)' in s
# Execute the real dimension rejection with a previously uploaded list.
dim=s[s.index('static void kv6_check_dimensions('):s.index('\nvoid kv6_init()',s.index('static void kv6_check_dimensions('))]
dim_stub=r'''
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#define max(a,b) ((a)>(b)?(a):(b))
#define log_error(...) ((void)0)
struct kv6_t {int xsiz,ysiz,zsiz,voxel_count;float scale;void* voxels;bool has_display_list;};
static int destroyed;
static void kv6_rebuild(struct kv6_t* k) {if(k->has_display_list){destroyed++;k->has_display_list=false;}}
'''
dim_check=r'''
int main(void) {
 struct kv6_t k={.xsiz=10,.ysiz=2,.zsiz=2,.scale=1,.voxel_count=1,.voxels=malloc(1),.has_display_list=true};
 kv6_check_dimensions(&k,20);assert(k.voxels&&k.voxel_count==1&&destroyed==0);
 kv6_check_dimensions(&k,5);assert(!k.voxels&&!k.voxel_count&&destroyed==1&&!k.has_display_list);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(dim_stub+dim+dim_check)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
