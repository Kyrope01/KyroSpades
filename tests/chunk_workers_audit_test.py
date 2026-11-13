"""Run actual chunk queue scheduling and shutdown against real pthread channels."""
from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/chunk.c').read_text()
init=s[s.index('void chunk_init()'):s.index('\nstatic int chunk_sort')]
shutdown=s[s.index('void chunk_deinit(void)'):s.index('\n/* Keep the camera')]
rebuild=s[s.index('void chunk_rebuild_all()'):s.index('\nvoid chunk_block_update')]
stub=r'''
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include "channel.h"
#define CHUNKS_PER_DIM 8
#define CHUNK_WORKERS_MAX 16
#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))
#define log_info(...) ((void)0)
#define log_warn(...) ((void)0)
#define log_fatal(...) ((void)0)
#define chunk_lock_init(x) pthread_mutex_init(x,NULL)
#define chunk_lock_destroy(x) pthread_mutex_destroy(x)
struct dl {int x;};
struct chunk {bool created;int max_height;uint64_t gen;size_t x,y;struct dl display_list;};
struct chunk chunks[64];
struct chunk_work_packet {size_t chunk_x,chunk_y;struct chunk* chunk;uint64_t gen;bool bulk;};
struct tesselator {int unused;};
struct chunk_result_packet {struct chunk* chunk;int max_height;struct tesselator tesselator;uint32_t* minimap_data;uint64_t gen;bool bulk;};
struct chunk_render_call {int dummy;};
struct channel chunk_work_queue,chunk_result_queue;
pthread_mutex_t chunk_block_queue_lock;int chunk_block_queue,chunk_bulk_pending;uint64_t chunk_gen;
pthread_t chunk_threads[16];int chunk_thread_count;
struct chunk_render_call* chunk_draw_buf;int chunk_draw_buf_cap;
void log_log(int level,const char*file,int line,const char*fmt,...){(void)level;(void)file;(void)line;(void)fmt;}
int freed;
void tesselator_free(struct tesselator*t){(void)t;__sync_fetch_and_add(&freed,1);}
void glx_displaylist_destroy(struct dl*d){(void)d;}
int ht_setup(void*a,size_t b,size_t c,int d){(void)a;(void)b;(void)c;(void)d;return 0;}
int ht_destroy(void*a){(void)a;return 0;}
int window_cpucores(void){return 5;}
void* chunk_generate(void*arg) {
 (void)arg;for(;;){struct chunk_work_packet w;channel_await(&chunk_work_queue,&w);if(!w.chunk)break;
 struct chunk_result_packet r={.chunk=w.chunk,.minimap_data=malloc(4)};channel_put(&chunk_result_queue,&r);}
 return NULL;
}
'''
checks=r'''
int main(void){
 assert(channel_create(&chunk_work_queue,sizeof(struct chunk_work_packet),64));
 for(int y=0;y<8;y++)for(int x=0;x<8;x++){chunks[x+y*8].x=x;chunks[x+y*8].y=y;}
 chunk_rebuild_all();assert(channel_size(&chunk_work_queue)==64);
 int seen[64]={0};for(int k=0;k<64;k++){struct chunk_work_packet w;channel_await(&chunk_work_queue,&w);assert(w.chunk_x<8&&w.chunk_y<8&&w.gen==chunk_gen&&w.bulk);seen[w.chunk_x+8*w.chunk_y]++;}
 for(int k=0;k<64;k++)assert(seen[k]==1);channel_destroy(&chunk_work_queue);
 for(int n=0;n<40;n++){chunk_init();chunk_rebuild_all();chunk_draw_buf=malloc(64);chunk_draw_buf_cap=1;chunk_deinit();assert(chunk_thread_count==0&&chunk_draw_buf==NULL&&chunk_draw_buf_cap==0);}
 puts("chunk rebuild uniqueness and worker shutdown checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(stub+init+shutdown+rebuild+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-pthread','-I'+str(root/'src'),'-I'+str(root/'tools/bench/stubs'),'-I'+str(root/'deps'),str(p),str(root/'src/channel.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=20)
main=(root/'src/main.c').read_text();deinit=main[main.index('void deinit()'):main.index('void on_error',main.index('void deinit()'))]
assert deinit.index('map_stop_worker();')<deinit.index('chunk_deinit();')<deinit.index('water_deinit();')<deinit.index('map_deinit();')
assert 'pthread_detach(pthread_self())' not in s[s.index('void* chunk_generate('):s.index('/* ── Glowing blocks')]
assert 'calloc(CHUNK_SIZE * CHUNK_SIZE' in s
