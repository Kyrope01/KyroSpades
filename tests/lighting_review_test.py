"""Exercise production lighting uniform cache and shader consistency."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'src/lighting.c').read_text()
structs=s[s.index('struct lighting_flash {'):s.index('/* Keep an emissive lamp tip')]
toggles=s[s.index('bool lighting_flashlight_toggle('):s.index('\nvoid lighting_add_flash(')]
builder=s[s.index('static void lighting_build_uniform_arrays('):s.index('\nvoid lighting_apply_program(')]
pre=r'''
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define PI 3.14159265358979323846F
#define LIGHTING_SHADER_LIGHTS 4
#define LIGHTING_FLASH_CAPACITY 32
#define LIGHTING_SUBMISSION_CAPACITY 64
struct {float dynamic_light_intensity;int hdr_rendering;} settings;
unsigned char local_player_id;
float sun_dir[3]={1,2,2};
double current_time=10;int time_calls,dark_calls,negotiated=1,supported=1;
double window_time(void){time_calls++;return current_time;}
struct flashlight_beam {unsigned char reach,cone,red,green,blue;};
struct flashlight_beam beam={50,128,255,64,32};
bool lighting_supported(void){return supported;}bool flashlight_ext_negotiated(void){return negotiated;}
struct flashlight_beam flashlight_ext_beam(unsigned int id){(void)id;return beam;}
bool flashlight_ext_dark(unsigned int id,double now){(void)id;(void)now;dark_calls++;return false;}
'''
checks=r'''
int main(void){
 settings.dynamic_light_intensity=2;
 const float*v=lighting_normalized_sun();assert(fabsf(v[0]-1.0f/3)<1e-6 && fabsf(v[2]-2.0f/3)<1e-6);
 sun_dir[0]=0;sun_dir[1]=0;sun_dir[2]=2;v=lighting_normalized_sun();assert(v[0]==0 && v[2]==1);
 lighting_flashlight_set_server_state(true);lighting_flashlight_update(.016f,true,1,2,3,0,0,1);
 assert(flashlight.frame_active && !frame_uniforms_valid);
 current_time=11;time_calls=dark_calls=0;
 const struct lighting_uniform_frame* u=lighting_frame_uniforms();
 assert(frame_uniforms_valid && time_calls==1 && dark_calls==1 && u->position_radius[15]==50);
 assert(u->spot_color_cone[12]==1 && fabsf(u->spot_color_cone[13]-64.0f/255)<1e-6);
 assert(u->color_intensity[15]>0.99f && u->flashlight_direction[3]>=0);
 for(int i=0;i<50;i++) assert(lighting_frame_uniforms()==u);
 assert(time_calls==1 && dark_calls==1);
 lighting_flashlight_set_server_state(false);assert(!frame_uniforms_valid);
 u=lighting_frame_uniforms();assert(u->flashlight_direction[3]<0 && u->color_intensity[15]==0);
 lighting_flashlight_set_server_state(true);lighting_flashlight_update(.016f,true,1,2,3,0,0,1);
 frame_uniforms_valid=false;frame_light_count=1;frame_lights[0].radius=5;frame_lights[0].intensity=2;frame_lights[0].color[0]=.5f;
 u=lighting_frame_uniforms();assert(u->position_radius[3]==5 && u->color_intensity[3]==4);
 supported=0;lighting_flashlight_update(.016f,true,1,2,3,0,0,1);u=lighting_frame_uniforms();assert(u->color_intensity[3]==0 && u->flashlight_direction[3]<0);
 puts("Lighting cache, invalidation, flashlight and normalized sun checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(pre+structs+toggles+builder+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=gnu11','-fsanitize=undefined',str(p),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
shader120=s[s.index('static const char* world_fragment_shader ='):s.index('static const char* world_fragment_shader_330')]
shader330=s[s.index('static const char* world_fragment_shader_330'):s.index('static void lighting_cache_uniforms')]
assert '(0.86 + 0.14 * sun_diffuse) * u_SunColor.r + added' in shader120
assert '(0.86 + 0.14 * sun_diffuse) * shadow_visibility * u_SunColor.r + added' in shader330
assert 'step(0.9995, v_Color.a)' in shader120 and 'step(0.9995, v_Color.a)' in shader330
assert s.count('directional normal-map details may rotate')==2
assert 'frame_uniforms_valid = false;' in s[s.index('void lighting_prepare_frame('):s.index('\n        int pending_count',s.index('void lighting_prepare_frame('))]
assert 'frame_uniforms_valid = false;' in s[s.index('bool lighting_set_enabled('):s.index('\n        settings.dynamic_lights',s.index('bool lighting_set_enabled('))]
print('GLSL path, emissive marker and frame invalidation checks passed')
