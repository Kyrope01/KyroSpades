"""Regression checks for the second cross-file audit."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
hud=(root/'src/hud.c').read_text(); main=(root/'src/main.c').read_text()
model=(root/'src/model.c').read_text(); map_c=(root/'src/map.c').read_text(); player=(root/'src/player.c').read_text(); glx=(root/'src/glx.c').read_text()
helper=hud[hud.index('static void hud_serverlist_copy_field('):hud.index('\n/* Two-step server selection')]
check=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "parson.h"
''' + helper + r'''
int main(void){
 JSON_Value* v=json_parse_string("[{\"name\":\"A very long name\"},null,{}]");
 assert(v);JSON_Array* a=json_value_get_array(v);char out[5];
 hud_serverlist_copy_field(out,sizeof out,json_array_get_object(a,0),"name");assert(!strcmp(out,"A ve"));
 hud_serverlist_copy_field(out,sizeof out,json_array_get_object(a,0),"map");assert(!strcmp(out,""));
 hud_serverlist_copy_field(out,sizeof out,json_array_get_object(a,1),"name");assert(!strcmp(out,""));
 hud_serverlist_copy_field(out,sizeof out,json_array_get_object(a,2),"name");assert(!strcmp(out,""));
 json_value_free(v);assert(!json_parse_string("not json"));
 v=json_parse_string("[]");assert(json_array_get_count(json_value_get_array(v))==0);json_value_free(v);
 puts("server-list missing/null/long field checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(check);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=c99','-I'+str(root/'src'),str(p),str(root/'src/parson.c'),'-lm','-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
section=hud[hud.index('if(request_serverlist) {',hud.index('static void hud_serverlist_render')):]
assert 'if(!servers || count > INT_MAX' in section and 'count ? calloc(count' in section
assert 'snprintf(latest_ver, sizeof(latest_ver)' in hud and 'minimap draw: glGetError()' not in hud
body=model[model.index('static void kv6_render_unflipped'):model.index('tesselator_glx(&tess_color')]
assert body.count('tesselator_set_color(tess, rgba(')==6 and body.count(', 255));')==6
assert 'return map_isair_locked(x, y, z);' in map_c and 'return map_get_locked(x, y, z);' in map_c
assert 'bool found = libvxl_map_gettop' in map_c and 'pthread_rwlock_rdlock(&map_lock)' in map_c[map_c.index('int map_height_at'):map_c.index('bool map_isair(int')]
assert 'if(!interp_active)' in player[player.index('void player_render_interpolate_end'):]
assert 'network_connect_string(connect_arg)' in main and 'network_connect_string(connect_arg + 1)' not in main
assert 'fixed_catchup_count < 4' in main and 'dt > 0.0 ? (int)fmin' in main
assert main.index('rpc_init();',main.index('void init()')) < main.index('hud_init();',main.index('void init()'))
flush=glx[glx.index('void glx_batch2d_flush(void)'):glx.index('\nvoid glx_batch2d_enable')]
assert 'previous_program = (GLuint)glx_current_program()' in flush and 'glx_use_program(previous_program);' in flush
print('model lighting, locking, timing, interpolation and program state checks passed')
