"""Exercise file readers and demo record guards with compiled production code."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
demo = (root/'src/demo.c').read_text()
record = demo[demo.index('void register_demo_packet('):demo.index('\nstatic void demo_record_teamplay_packet')]
assert '!isfinite(ts)' in demo and '(count && ts < last_ts)' in demo
assert 'DemoPlaybackState.packets[pkt].timestamp' in demo
assert '!isfinite(time)' in demo and '!isfinite(speed)' in demo
with tempfile.TemporaryDirectory() as d:
    d = Path(d)
    c = d/'test.c'
    c.write_text(r'''
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include <limits.h>
#include "file.h"
typedef struct { unsigned char *data; size_t dataLength; } ENetPacket;
struct {FILE *fp; float start_time;} CurrentDemo;
static double window_time(void) { return 1; }
static int stopped;
static void demo_stop_record(void) { fclose(CurrentDemo.fp); CurrentDemo.fp=NULL; stopped++; }
#define log_error(...) ((void)0)
''' + record + r'''
int main(void) {
 size_t n=99; assert(!file_load_sized("missing", &n) && n==0);
 FILE*f=fopen("empty", "wb"); assert(f); fclose(f);
 unsigned char*b=file_load_sized("empty",&n); assert(b && !n && !b[0]); free(b);
 f=fopen("data","wb"); unsigned char x[]={0,0,128,63,255}; fwrite(x,1,5,f); fclose(f);
 b=file_load_sized("data",&n); assert(b && n==5 && b[5]==0); assert(buffer_readf(b,0)==1.0f); free(b);
 unsigned char unaligned[]={0,0xff,0xff,0xff,0xff}; assert(buffer_read32(unaligned,1)==0xffffffffU);
 CurrentDemo.fp=tmpfile(); unsigned char data[1]={1}; ENetPacket p={data,65536}; register_demo_packet(&p); assert(stopped==1 && !CurrentDemo.fp);
 CurrentDemo.fp=tmpfile(); p.dataLength=1; register_demo_packet(&p); assert(ftell(CurrentDemo.fp)==7); fclose(CurrentDemo.fp);
 return 0;
}
''')
    exe=d/'test'
    subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined','-ffunction-sections','-fdata-sections', '-I'+str(root/'src'),'-I'+str(root/'tools/bench/stubs'), '-I'+str(root/'deps'),str(c),str(root/'src/file.c'),'-Wl,--gc-sections','-o',str(exe)],check=True)
    subprocess.run([str(exe)],cwd=d,check=True)
print('File and demo audit checks passed')
