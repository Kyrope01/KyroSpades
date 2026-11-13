from pathlib import Path
import subprocess, tempfile
root=Path(__file__).resolve().parents[1]; s=(root/'src/network.c').read_text(); a=s.index('void read_PacketMapStart('); b=s.index('\n/* Measured interval',a)
h=r'''
#include <assert.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#define CHECK_ALLOCATION_ERROR(x) do { assert(x); } while(0);
#define log_warn(...) ((void)0)
#define log_info(...) ((void)0)
#define log_debug(...) ((void)0)
#define red(x) ((x)&255)
#define green(x) (((x)>>8)&255)
#define blue(x) (((x)>>16)&255)
#define alpha(x) (((x)>>24)&255)
#define PACKET_MAPCACHED_ID 31
#define CAMERAMODE_SELECTION 1
struct PacketMapStart075 {unsigned int map_size;};
struct PacketMapStart076 {unsigned int map_size,crc32;char map_name[64];};
struct PacketMapCached {unsigned char cached;};
void* compressed_chunk_data; int compressed_chunk_data_size,compressed_chunk_data_offset,compressed_chunk_data_estimate;
int network_logged_in,network_map_transfer,network_map_transfer_end,network_map_cached,camera_mode,resets,sends,seen_cached, seeking,cache_file;
void flashlight_ext_reset_map(void){resets++;} void player_outfit_ext_reset_map(void){} int demo_is_seeking(void){return seeking;}
void teamplay_reset_map(void){} void lighting_flashlight_reset(void){} void player_clear_corpses(void){} void bloodmarks_clear(void){} void damagenumbers_clear(void){}
int file_exists(const char* f){(void)f;return cache_file;} void* file_load_sized(const char* f,size_t* n){(void)f;*n=0;return NULL;}
void map_vxl_load(void* p,size_t n){(void)p;(void)n;assert(0);} void chunk_rebuild_all(void){assert(0);}
void network_send(int id,void* p,int n){assert(id==31&&n==1);sends++;seen_cached=((struct PacketMapCached*)p)->cached;}
void player_init(void){}
''' + s[a:b] + r'''
int main(void){
 unsigned char bytes[80];memset(bytes,'X',sizeof bytes);
 read_PacketMapStart(bytes,0);read_PacketMapStart(bytes,3);read_PacketMapStart(bytes,5);read_PacketMapStart(bytes,71); assert(resets==0);
 struct PacketMapStart075 p={123};read_PacketMapStart(&p,sizeof p);assert(resets==1&&compressed_chunk_data_estimate==123&&sends==0);
 struct PacketMapStart076 q;memset(&q,'A',sizeof q);q.map_size=456;q.crc32=123;cache_file=1;
 read_PacketMapStart(&q,sizeof q);assert(resets==2&&sends==1&&!seen_cached&&compressed_chunk_data_estimate==456);
 free(compressed_chunk_data);puts("network map start validation passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(h)
 subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
