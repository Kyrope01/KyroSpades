from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'src/sound.c').read_text();a=s.index('void sound_deinit(void)');b=s.index('\nvoid sound_update()',a)
h=r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define USE_SOUND
typedef struct {int x;} ALCdevice;typedef struct {int x;} ALCcontext;
struct Sound_source {unsigned openal_handle,filter;};struct entity_system {void*buffer;size_t count;} sound_sources;
int sound_enabled=1,rain_playing=1,rain_fade_state=1,efx_ok=1,room_head=4,room_filled=5;float rain_gain=1;unsigned rain_source=9,fx_slot=3,fx_reverb=4;
ALCdevice device,*sound_device=&device;ALCcontext context,*sound_context=&context;int sources,slots,effects,ctx,dev,entities;
void alSourceStop(unsigned x){assert(x==9);}void alDeleteSources(int n,const unsigned*x){(void)n;assert(*x==9);}
void sound_source_free(struct Sound_source*s){sources++;s->openal_handle=s->filter=0;}
void delslot(int n,const unsigned*x){(void)n;assert(*x==3);slots++;} void deleffect(int n,const unsigned*x){(void)n;assert(*x==4);effects++;}
void (*p_alDeleteAuxiliaryEffectSlots)(int,const unsigned*)=delslot;void (*p_alDeleteEffects)(int,const unsigned*)=deleffect;
void alcMakeContextCurrent(ALCcontext*p){assert(!p);}void alcDestroyContext(ALCcontext*p){assert(p==&context);ctx++;}void alcCloseDevice(ALCdevice*p){assert(p==&device);dev++;}
void entitysys_destroy(struct entity_system*p){(void)p;entities++;}
''' + s[a:b] + r'''
int main(void){struct Sound_source ss[]={{1,4},{0,0},{0,5}};sound_sources.buffer=ss;sound_sources.count=3;
 sound_deinit();assert(sources==2&&slots==1&&effects==1&&ctx==1&&dev==1&&entities==1);
 assert(!rain_source&&!fx_slot&&!fx_reverb&&!efx_ok&&!rain_playing&&!sound_context&&!sound_device&&!room_filled);
 sound_sources.count=0;sound_deinit();assert(slots==1&&effects==1&&ctx==1&&dev==1);puts("sound shutdown cleanup passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
