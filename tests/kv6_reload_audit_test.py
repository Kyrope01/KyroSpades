"""Test the production KV6 reload/validation code with instrumented GPU stubs."""
from pathlib import Path
import subprocess, tempfile, zipfile
root = Path(__file__).resolve().parents[1]
src = (root/'src/model.c').read_text()
code = src[src.index('void kv6_load(struct kv6_t*'):src.index('\nvoid kv6_rebuild(struct kv6_t*')]
stub = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#define CHECK_ALLOCATION_ERROR(p) assert(p);
#define log_error(...) ((void)0)
#define log_warn(...) ((void)0)
struct kv6_voxel {uint16_t x,y,z;uint8_t visfaces;uint32_t color;};
struct kv6_t {uint16_t xsiz,ysiz,zsiz;float xpiv,ypiv,zpiv;bool has_display_list,colorize,flip_yaw;struct kv6_voxel* voxels;int voxel_count;float scale;};
unsigned int buffer_read32(unsigned char*p,int i) {return (unsigned)p[i]|(unsigned)p[i+1]<<8|(unsigned)p[i+2]<<16|(unsigned)p[i+3]<<24;}
unsigned short buffer_read16(unsigned char*p,int i) {return p[i]|p[i+1]<<8;}
unsigned char buffer_read8(unsigned char*p,int i) {return p[i];}
float buffer_readf(unsigned char*p,int i) {uint32_t n=buffer_read32(p,i);float f;memcpy(&f,&n,4);return f;}
int file_size(const char*p) {FILE*f=fopen(p,"rb");if(!f)return 0;fseek(f,0,SEEK_END);int n=ftell(f);fclose(f);return n;}
int file_exists(const char*p) {FILE*f=fopen(p,"rb");if(!f)return 0;fclose(f);return 1;}
unsigned char* file_load(const char*p) {int n=file_size(p);FILE*f=fopen(p,"rb");unsigned char*b=malloc(n);assert(fread(b,1,n,f)==(size_t)n);fclose(f);return b;}
unsigned char* file_load_sized(const char*p,size_t*n) {*n=(size_t)file_size(p);return file_load(p);}
int destroyed;
void kv6_rebuild(struct kv6_t*k) {if(k->has_display_list){destroyed+=2;k->has_display_list=false;}}
'''
checks = r'''
static void put32(unsigned char*b,int i,uint32_t x) {for(int j=0;j<4;j++)b[i+j]=x>>(8*j);}
static void write_file(const char*p,unsigned char*b,int n) {FILE*f=fopen(p,"wb");assert(f);fwrite(b,1,n,f);fclose(f);}
int main(int argc,char**argv) {
 for(int i=1;i<argc;i++) {int n=file_size(argv[i]);unsigned char*b=file_load(argv[i]);assert(kv6_valid_data(b,n));free(b);}
 unsigned char huge[32]={0};put32(huge,0,0x6c78764b);put32(huge,4,65535);put32(huge,8,65535);put32(huge,12,1);put32(huge,28,1);
 assert(!kv6_valid_data(huge,SIZE_MAX)); /* reject before table access */
 unsigned char b[46]={0};put32(b,0,0x6c78764b);put32(b,4,1);put32(b,8,1);put32(b,12,1);put32(b,28,1);b[44]=1;
 assert(kv6_valid_data(b,sizeof b));
 for(int n=0;n<46;n++) assert(!kv6_valid_data(b,n));
 b[44]=2;assert(!kv6_valid_data(b,sizeof b));b[44]=1;
 b[36]=1;assert(!kv6_valid_data(b,sizeof b));b[36]=0;
 put32(b,16,0x7fc00000);assert(!kv6_valid_data(b,sizeof b));put32(b,16,0);
 write_file("valid.kv6",b,sizeof b);struct kv6_t k={0};
 assert(kv6_reload(&k,"valid.kv6",0.05f)==0);assert(k.voxels && k.voxel_count==1);
 k.has_display_list=true;struct kv6_voxel* old=k.voxels;
 write_file("invalid.kv6",b,10);assert(kv6_reload(&k,"invalid.kv6",1)==-1);assert(k.voxels==old && k.has_display_list && destroyed==0);
 assert(kv6_reload(&k,"valid.kv6",0.05f)==0);assert(destroyed==2 && !k.has_display_list);
 free(k.voxels);puts("KV6 validation and reload resource checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
    p=Path(d)/'test.c'; p.write_text(stub+code+checks)
    exe=Path(d)/'test'
    subprocess.run(['gcc','-std=c99','-Wall','-Wextra','-Werror',str(p),'-lm','-o',str(exe)],check=True)
    models=list((root/'resources/kv6').glob('*.kv6'))+list((root/'resources.override/kv6').glob('*.kv6'))
    with zipfile.ZipFile(root/'bsresources.zip') as archive:
        for i, name in enumerate(n for n in archive.namelist() if n.lower().endswith('.kv6')):
            path = Path(d)/f'archive-{i}.kv6'
            path.write_bytes(archive.read(name))
            models.append(path)
    subprocess.run([str(exe),*map(str,models)],cwd=d,check=True)
assert 'kv6_rebuild(&cat->entries[i].previews[m]);' in (root/'src/skins.c').read_text()
assert 'file_dir_list("kv6", skin_scan_file, &context)' in (root/'src/skins.c').read_text()
