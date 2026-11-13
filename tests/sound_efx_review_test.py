from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/sound.c').read_text()
a=s.index('#define ALC_HRTF_SOFT'); b=s.index('/* ---- YSR room probe state',a); typedef=s[a:b]
a=s.index('static void sound_efx_init('); b=s.index('\n#endif',a); fn=s[a:b]
h=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define log_warn(...) ((void)0)
#define log_info(...) ((void)0)
#define AL_NO_ERROR 0
typedef struct {int x;} ALCdevice; typedef struct {int x;} ALCcontext;
''' + typedef + r'''
#define ROOM_RING 128
float room_dist[ROOM_RING]; int error, fail_at, step, deleted_effect,deleted_slot,missing,extension=1;
int alcIsExtensionPresent(ALCdevice*d,const char*n){(void)d;(void)n;return extension;}
int alGetError(void){int e=error;error=0;return e;}
void gen_effect(int n,unsigned int* id){(void)n;*id=11;if(++step==fail_at)error=1;}
void del_effect(int n,const unsigned int* id){(void)n;assert(*id==11);deleted_effect++;}
void effecti(unsigned int id,int v,int type){(void)id;(void)v;(void)type;if(++step==fail_at)error=1;}
void effectf(unsigned int id,int v,float x){(void)id;(void)v;(void)x;}
void gen_slot(int n,unsigned int* id){(void)n;*id=22;if(++step==fail_at)error=1;}
void del_slot(int n,const unsigned int* id){(void)n;assert(*id==22);deleted_slot++;}
void sloti(unsigned int id,int a,int b){(void)id;(void)a;(void)b;if(++step==fail_at)error=1;}
void gen_filter(int n,unsigned int* id){(void)n;(void)id;} void del_filter(int n,const unsigned int* id){(void)n;(void)id;}
void filteri(unsigned int a,int b,int c){(void)a;(void)b;(void)c;} void filterf(unsigned int a,int b,float c){(void)a;(void)b;(void)c;}
void* alGetProcAddress(const char* n){
 const char* names[]={"alGenEffects","alDeleteEffects","alEffecti","alEffectf","alGenAuxiliaryEffectSlots","alDeleteAuxiliaryEffectSlots","alAuxiliaryEffectSloti","alGenFilters","alDeleteFilters","alFilteri","alFilterf"};
 void* ptrs[]={(void*)gen_effect,(void*)del_effect,(void*)effecti,(void*)effectf,(void*)gen_slot,(void*)del_slot,(void*)sloti,(void*)gen_filter,(void*)del_filter,(void*)filteri,(void*)filterf};
 for(int i=0;i<11;i++)if(!strcmp(n,names[i]))return i==missing?NULL:ptrs[i];return NULL;
}
''' + fn + r'''
int main(void){ALCdevice d={0};
 for(int i=0;i<11;i++){missing=i;step=0;sound_efx_init(&d);assert(!efx_ok && !fx_reverb && !fx_slot && !step);}
 missing=-1;
 for(int i=1;i<=4;i++){fail_at=i;step=deleted_effect=deleted_slot=0;sound_efx_init(&d);assert(!efx_ok&&!fx_reverb&&!fx_slot);assert(deleted_effect==1);assert(deleted_slot==(i>=3));}
 fail_at=0;step=0;sound_efx_init(&d);assert(efx_ok && fx_reverb==11 && fx_slot==22 && room_dist[0]==-1);
 puts("EFX partial failures and proc validation passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(h);subprocess.run(['cc','-std=gnu11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'test.c'),'-o',str(p/'test')],check=True);subprocess.run([str(p/'test')],check=True)
