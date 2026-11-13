"""Test actual pthread channel behavior and chunk result generation accounting."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/chunk.c').read_text()
update=s[s.index('void chunk_update_all()'):s.index('\nvoid chunk_rebuild_all()')]
channel_test=r'''
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/wait.h>
#include <unistd.h>
#include "channel.h"
void log_log(int n,const char*f,int l,const char*fmt,...){(void)n;(void)f;(void)l;(void)fmt;}
void* __real_realloc(void*,size_t);int fail_realloc;
void* __wrap_realloc(void*p,size_t n){return fail_realloc?NULL:__real_realloc(p,n);}
struct channel c;int seen[4000];pthread_mutex_t seenlock=PTHREAD_MUTEX_INITIALIZER;
void* producer(void*a){int id=(int)(intptr_t)a;for(int i=0;i<1000;i++){int v=id*1000+i;channel_put(&c,&v);}return NULL;}
void* consumer(void*a){(void)a;for(;;){int v;channel_await(&c,&v);if(v<0)break;assert(v<4000);pthread_mutex_lock(&seenlock);seen[v]++;pthread_mutex_unlock(&seenlock);}return NULL;}
int main(void){
 struct channel over;assert(!channel_create(&over,SIZE_MAX,2));
 assert(channel_create(&c,sizeof(int),2));int v;assert(!channel_try_await(&c,&v));
 pthread_t p[4],q[2];for(int i=0;i<2;i++)assert(!pthread_create(q+i,0,consumer,0));
 for(int i=0;i<4;i++)assert(!pthread_create(p+i,0,producer,(void*)(intptr_t)i));
 for(int i=0;i<4;i++)pthread_join(p[i],0);int stop=-1;for(int i=0;i<2;i++)channel_put(&c,&stop);
 for(int i=0;i<2;i++)pthread_join(q[i],0);assert(!channel_size(&c));assert(!channel_try_await(&c,&v));
 for(int i=0;i<4000;i++)assert(seen[i]==1);channel_destroy(&c);
 assert(channel_create(&c,sizeof(int),1));v=9;channel_put(&c,&v);
 pid_t child=fork();assert(child>=0);if(!child){fail_realloc=1;channel_put(&c,&v);_exit(0);}
 int status;waitpid(child,&status,0);assert(WIFEXITED(status)&&WEXITSTATUS(status)==1);
 assert(channel_try_await(&c,&v)&&v==9);channel_destroy(&c);
 puts("Channel concurrency, empty try, overflow and allocation failure passed");
}
'''
stub=r'''
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#define CHUNK_SIZE 16
#define GL_TEXTURE_2D 1
#define GL_RGBA 2
#define GL_UNSIGNED_BYTE 3
struct channel {int dummy;} chunk_result_queue;
struct tesselator {int has_normal;};struct dl {int x;};
struct chunk {uint64_t gen;int x,y,max_height;bool created;struct dl display_list;};
struct chunk_result_packet {struct chunk* chunk;int max_height;struct tesselator tesselator;uint32_t* minimap_data;uint64_t gen;bool bulk;};
struct chunk chunks[2];struct chunk_result_packet entries[3];int next,chunk_bulk_pending=2;uint64_t chunk_gen=8;
struct {int texture_id;} texture_minimap;
float window_time(void){return 0;}
bool channel_try_await(struct channel*c,void*p){(void)c;if(next>=3)return false;*(struct chunk_result_packet*)p=entries[next++];return true;}
void glx_displaylist_create(struct dl*d,int a,int b){(void)d;(void)a;(void)b;}
void tesselator_glx(struct tesselator*t,struct dl*d){(void)t;(void)d;}
void shadow_invalidate(void){}void glBindTexture(int a,int b){(void)a;(void)b;}
void glTexSubImage2D(int a,int b,int c,int d,int e,int f,int g,int h,const void*i){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i;}
void tesselator_free(struct tesselator*t){(void)t;}
'''
checks=r'''
int main(void){
 chunks[0].gen=chunks[1].gen=8;
 entries[0]=(struct chunk_result_packet){.chunk=chunks,.gen=7,.bulk=true};
 entries[1]=(struct chunk_result_packet){.chunk=chunks,.gen=8,.bulk=false};
 entries[2]=(struct chunk_result_packet){.chunk=chunks+1,.gen=8,.bulk=true};
 chunk_update_all();assert(chunk_bulk_pending==1);assert(chunks[0].created&&chunks[1].created);
 puts("Chunk generation and bulk accounting passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);p=d/'channel.c';p.write_text(channel_test);exe=d/'channel'
 subprocess.run(['gcc','-std=gnu11','-pthread','-fsanitize=undefined','-I'+str(root/'src'),'-I'+str(root/'tools/bench/stubs'),'-I'+str(root/'deps'),str(p),str(root/'src/channel.c'),'-Wl,--wrap=realloc','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=20)
 p=d/'chunk.c';p.write_text(stub+update+checks);exe=d/'chunk'
 subprocess.run(['gcc','-std=gnu11',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
assert 'result.gen = work.gen;' in s and 'result.gen = work.chunk->gen' not in s
assert 'for(unsigned char b = 0; b < len_z; b++)' not in s
print('Chunk worker generation capture and loop checks passed')

block=s[s.index('void chunk_block_update('):s.index('\nstatic bool iterate_chunk_updates')]
stub=r'''
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define CHUNK_SIZE 16
#define CHUNKS_PER_DIM 2
int map_size_x=32,map_size_y=64,map_size_z=32,chunk_block_queue,chunk_block_queue_lock,inserts;
struct chunk{int x,y;}chunks[4];
struct chunk_work_packet {size_t chunk_x,chunk_y;struct chunk* chunk;uint64_t gen;bool bulk;};
void chunk_lock_lock(int*p){(void)p;}void chunk_lock_unlock(int*p){(void)p;}
void ht_insert(int*a,void*b,void*c){(void)a;(void)b;struct chunk_work_packet*w=c;assert(w->chunk>=chunks&&w->chunk<chunks+4);inserts++;}
'''
checks=r'''
int main(void){chunk_block_update(-1,0,0);chunk_block_update(0,-1,0);chunk_block_update(32,0,0);chunk_block_update(0,64,0);chunk_block_update(0,0,32);assert(!inserts);chunk_block_update(31,63,31);assert(inserts==1);puts("Chunk block update bounds passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'block.c';p.write_text(stub+block+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
