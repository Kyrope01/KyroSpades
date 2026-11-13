from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'src/sound.c').read_text()
def block(a,b): i=s.index(a);return s[i:s.index(b,i)]
rain=block('static int rain_playing = 0;', '\nvoid sound_rain_stop(')
free=block('static void sound_source_free(', '\n/* ---- occlusion')
update=block('static bool sound_update_single(', '\n#endif')
h=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define USE_SOUND
#define PLAYERS_MAX 256
#define AL_NO_ERROR 0
#define AL_LOOPING 1
#define AL_TRUE 1
#define AL_GAIN 2
#define AL_POSITION 3
#define AL_SOURCE_RELATIVE 4
#define AL_BUFFER 5
#define AL_SOURCE_STATE 6
#define AL_STOPPED 7
#define SOUND_OCCL_INTERVAL 4
#define log_warn(...) ((void)0)
typedef unsigned ALuint;
int sound_enabled=1,err,fail_gen,fail_play,deleted,filters_deleted;struct {int openal_buffer;} sound_rain={1};
int alGetError(void){int n=err;err=0;return n;}
void alGenSources(int n,ALuint*x){(void)n;if(fail_gen){*x=0;err=1;}else *x=19;}
void alSourcei(unsigned x,int y,int z){(void)x;(void)y;(void)z;}void alSourcef(unsigned x,int y,float z){(void)x;(void)y;(void)z;}
void alSource3f(unsigned x,int y,float a,float b,float c){(void)x;(void)y;(void)a;(void)b;(void)c;}
void alSourcePlay(unsigned x){(void)x;if(fail_play)err=1;}void alDeleteSources(int n,const unsigned*x){(void)n;(void)x;deleted++;}
void alGetSourcei(unsigned x,int y,int*z){(void)x;(void)y;*z=1;}
struct V {float x,y,z;};struct {int connected;struct V pos;struct {struct V velocity;}physics;} players[256];
struct Sound_source {unsigned openal_handle;char local;int stick_to_player;float base_gain,x,y,z,occl;unsigned filter;unsigned char probe_phase;};
void delete_filter(int n,const unsigned* id){(void)n;(void)id;filters_deleted++;} void (*p_alDeleteFilters)(int,const unsigned*)=delete_filter;
int efx_ok=1;unsigned sound_frame=0;float probed_x;
void sound_position(struct Sound_source*s,float x,float y,float z){s->x=x;s->y=y;s->z=z;}
void sound_velocity(struct Sound_source*s,float x,float y,float z){(void)s;(void)x;(void)y;(void)z;}
float sound_occlusion_probe(float x,float y,float z){(void)y;(void)z;probed_x=x;return 1;}
void sound_apply_occlusion(struct Sound_source*s){(void)s;}
''' + rain + '\n' + free + '\n' + update + r'''
int main(void){
 fail_gen=1;sound_rain_start();assert(!rain_source&&!rain_playing&&!rain_fade_state);
 fail_gen=0;fail_play=1;sound_rain_start();assert(!rain_source&&!rain_playing&&deleted==1);
 fail_play=0;sound_rain_start();assert(rain_source==19&&rain_playing&&rain_fade_state==1);
 struct Sound_source s={.openal_handle=1,.stick_to_player=999,.filter=5};assert(!sound_update_single(&s,0));assert(s.stick_to_player==-1);
 s.stick_to_player=3;players[3].connected=1;players[3].pos.x=12;assert(!sound_update_single(&s,0));assert(probed_x==12);
 players[3].connected=0;assert(sound_update_single(&s,0));assert(!s.openal_handle&&!s.filter&&filters_deleted==1);
 puts("rain, sticky bounds and source cleanup passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
assert 'if(!player || player_id < 0 || player_id >= PLAYERS_MAX) return;' in s
