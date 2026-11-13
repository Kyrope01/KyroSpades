"""Real bundled dr_wav + production upload: duration, mixdown and failures."""
from pathlib import Path
import subprocess,tempfile,wave,struct
root=Path(__file__).resolve().parents[1];s=(root/'src/sound.c').read_text()
a=s.index('extern short* drwav_open_and_read_file_s16'); b=s.index('\n#ifdef USE_SOUND\nstatic void sound_efx_init',a)
h=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#define USE_SOUND
#define log_warn(...) ((void)0)
#define AL_NO_ERROR 0
#define AL_FORMAT_MONO16 1
typedef unsigned int ALuint;
struct Sound_wav {unsigned int openal_buffer;float min,max;};
int sound_enabled=1, err, fail_gen,fail_upload, deleted, size_captured, rate_captured;
short captured[16000]; unsigned next_id=1;
int alGetError(void){int e=err;err=0;return e;}
void alGenBuffers(int n,ALuint*b){(void)n;if(fail_gen){err=1;*b=0;}else *b=next_id++;}
void alDeleteBuffers(int n,const ALuint*b){(void)n;(void)b;deleted++;}
void alBufferData(ALuint b,int format,const void* data,int size,int rate){(void)b;assert(format==1);if(fail_upload){err=1;return;}size_captured=size;rate_captured=rate;memcpy(captured,data,size);}
int file_exists(const char* s){FILE* f=fopen(s,"rb");if(!f)return 0;fclose(f);return 1;}
''' + s[a:b] + r'''
int main(int argc,char**argv){
 assert(argc==4); unsigned ch,rate;uint64_t count;short* raw=drwav_open_and_read_file_s16(argv[2],&ch,&rate,&count);
 assert(raw && ch==2 && rate==8000 && count==16000);free(raw);
 struct Sound_wav w={0};
 sound_load(&w,argv[1],1,2);assert(w.openal_buffer && size_captured==16000 && rate_captured==8000 && captured[0]==1000 && captured[7999]==1000);
 assert(sound_reload(&w,argv[2],1,2)==0);assert(size_captured==16000 && captured[0]==2000 && captured[7999]==2000);
 assert(sound_reload(&w,argv[3],1,2)==0);assert(size_captured==16000 && captured[0]==2000 && captured[7999]==2000);
 unsigned old=w.openal_buffer; fail_gen=1; assert(sound_reload(&w,argv[2],1,2)==-1 && w.openal_buffer==old);fail_gen=0;
 fail_upload=1;assert(sound_reload(&w,argv[2],1,2)==-1 && w.openal_buffer==old);fail_upload=0;
 assert(sound_reload(&w,"not-present.wav",1,2)==-1 && w.openal_buffer==old);
 struct Sound_wav missing={0};sound_load(&missing,"not-present.wav",1,2);assert(!missing.openal_buffer && missing.min==1 && missing.max==2);
 puts("WAV decode/mix/upload behavior passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d); paths=[]
 for n,vals in [(1,(1000,)),(2,(1000,3000)),(3,(1000,2000,3000))]:
  f=p/f'{n}.wav'; paths.append(str(f));
  with wave.open(str(f),'wb') as w:
   w.setnchannels(n);w.setsampwidth(2);w.setframerate(8000);w.writeframes(struct.pack('<'+'h'*n,*vals)*8000)
 (p/'test.c').write_text(h)
 subprocess.run(['cc','-std=c99','-DDR_WAV_IMPLEMENTATION','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(p/'test.c'),str(root/'src/dr_wav.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test'),*paths],check=True)
