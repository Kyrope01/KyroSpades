from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/hud.c').read_text();a=s.index('static void hud_settings_commit(void) {');b=s.index('\nstatic int int_slider_defaults(',a)
h=r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
struct RENDER_OPTIONS {int window_width,window_height,fullscreen,volume,dynamic_lights,shadow_quality,water_shader,water_waves,textured_blocks,ambient_occlusion,greedy_meshing,sunlight_intensity,force_displaylist;float shadow_intensity,ao_multiplier;};
struct RENDER_OPTIONS settings,settings_tmp;
int settings_windowed_width=800,settings_windowed_height=600,w=800,h=600,saves,remeshes,applies;
void window_windowed_size(int*x,int*y){*x=w;*y=h;}void window_set_windowed_size(int x,int y){w=x;h=y;}
int lighting_world_supported(void){return 1;}int shadow_baked_enabled(void){return 1;}void lighting_set_enabled(int v){(void)v;}
void texture_blocks_prepare_normal_map(void){}void texture_blocks_release_normal_map(void){}
void window_fromsettings(void){applies++;}void sound_volume(float v){(void)v;}void config_save(void){saves++;}void chunk_rebuild_all(void){remeshes++;}
''' + s[a:b] + r'''
int main(void){settings.window_width=1920;settings.window_height=1080;settings.fullscreen=1;settings.sunlight_intensity=100;settings_tmp=settings;
 hud_settings_commit();assert(!saves);
 settings_windowed_width=1200;settings_windowed_height=700;hud_settings_commit();assert(w==1200&&h==700&&saves==1&&settings.window_width==1920&&!remeshes);
 for(int i=0;i<100;i++)hud_settings_commit();assert(saves==1);
 settings_tmp.ambient_occlusion=1;hud_settings_commit();assert(saves==2&&remeshes==1);
 settings_tmp.volume=3;hud_settings_commit();assert(saves==3&&remeshes==1);
 settings_windowed_width=0;hud_settings_commit();assert(w==1&&settings_windowed_width==1);
 puts("settings commit behavior passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
assert 'if(!(ctx->mouse_down & MU_MOUSE_LEFT))\n                hud_settings_commit();' in s
assert 'if(hud_active == &hud_settings && new != &hud_settings)\n                hud_settings_commit();' in s
cfg=(root/'src/config.c').read_text();assert '.value = &settings_windowed_width' in cfg and '.value = &settings_windowed_height' in cfg
assert 'window_windowed_size(&xres, &yres);' in cfg
