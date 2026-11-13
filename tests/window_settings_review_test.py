"""Exercise real window resize/apply code with SDL/GLFW fakes (including 2x DPI)."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'src/window.c').read_text()
def section(start,end,offset=0):
 a=s.index(start,offset);return s[a:s.index(end,a)]
common=section('static int window_pending_apply', '\n#ifdef USE_GLFW')
glfw=section('void window_fromsettings()', '\nvoid window_deinit()')
sdlstart=s.index('#ifdef USE_SDL\n\nvoid window_textinput')
sdlfrom=section('void window_fromsettings()', '\n#ifdef __ANDROID__',sdlstart)
sdlapply=section('void window_apply()', '\nvoid window_keyname(',sdlstart)
stub=r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define log_warn(...) ((void)0)
#define log_info(...) ((void)0)
struct options {int window_width,window_height,fullscreen,vsync,multisamples;} settings={800,600,0,1,0},settings_tmp={800,600,0,1,0};
int settings_windowed_width=800,settings_windowed_height=600;
struct window_instance {void* impl;} instance, *hud_window=&instance;
int actual_w=800,actual_h=600,full=0,size_calls,enter_calls,exit_calls,window_creating_context,fail_full;
void reshape(struct window_instance*w,int width,int height){(void)w;settings.window_width=settings_tmp.window_width=width;settings.window_height=settings_tmp.window_height=height;}
void window_swapping(int x){(void)x;}
''' + common + r'''
#ifdef TEST_GLFW
typedef struct {int x;} GLFWmonitor; typedef struct {int width,height,refreshRate;} GLFWvidmode;
GLFWmonitor monitor;GLFWvidmode mode={1920,1080,60};
#define GLFW_DONT_CARE -1
#define GLFW_NO_ERROR 0
GLFWmonitor* glfwGetPrimaryMonitor(void){return &monitor;}const GLFWvidmode*glfwGetVideoMode(GLFWmonitor*m){(void)m;return &mode;}
GLFWmonitor*glfwGetWindowMonitor(void*w){(void)w;return full?&monitor:NULL;}
int window_on_wayland(void){return 0;}int glfwGetError(const char**s){(void)s;return 0;}
void glfwGetWindowSize(void*w,int*x,int*y){(void)w;*x=actual_w;*y=actual_h;}
void glfwGetFramebufferSize(void*w,int*x,int*y){(void)w;*x=actual_w*2;*y=actual_h*2;}
void glfwSetWindowSize(void*w,int x,int y){(void)w;size_calls++;actual_w=x;actual_h=y;window_note_windowed_resize(x,y);}
void glfwSetWindowMonitor(void*w,GLFWmonitor*m,int x,int y,int width,int height,int refresh){(void)w;(void)x;(void)y;(void)refresh;if(m){enter_calls++;if(fail_full)return;full=1;}else{exit_calls++;full=0;}actual_w=width;actual_h=height;window_note_windowed_resize(width,height);}
void glfwShowWindow(void*w){(void)w;}
''' + glfw + r'''
#else
#define SDL_WINDOW_FULLSCREEN 1
#define SDL_WINDOW_FULLSCREEN_DESKTOP 3
int SDL_GetWindowFlags(void*w){(void)w;return full?3:0;}
int SDL_SetWindowFullscreen(void*w,int mode){(void)w;if(mode){enter_calls++;if(fail_full)return -1;full=1;actual_w=1920;actual_h=1080;}else{exit_calls++;full=0;actual_w=800;actual_h=600;}window_note_windowed_resize(actual_w,actual_h);return 0;}
void SDL_GetWindowSize(void*w,int*x,int*y){(void)w;*x=actual_w;*y=actual_h;}
void SDL_SetWindowSize(void*w,int x,int y){(void)w;size_calls++;actual_w=x;actual_h=y;window_note_windowed_resize(x,y);}
const char*SDL_GetError(void){return "fake";}
''' + sdlfrom + sdlapply + r'''
#endif
static void apply(void){window_fromsettings();window_apply();}
int main(void){
 window_set_windowed_size(800,600);actual_w=1000;actual_h=800;window_note_windowed_resize(1000,800);
 assert(windowed_width==1000 && settings_windowed_width==1000);settings.window_width=2000;settings.window_height=1600;settings_tmp=settings;
 apply();assert(!size_calls&&!enter_calls&&!exit_calls && actual_w==1000);
 /* A manual resize may arrive after a settings apply was queued. */
 window_fromsettings();actual_w=1050;actual_h=850;window_note_windowed_resize(actual_w,actual_h);window_apply();
 assert(!size_calls && actual_w==1050 && windowed_width==1050);
 settings.fullscreen=1;settings_tmp.fullscreen=1;apply();assert(full && enter_calls==1 && windowed_width==1050);
 settings_windowed_width=1200;settings_windowed_height=700;window_set_windowed_size(1200,700);
 apply();assert(enter_calls==1&&windowed_width==1200);
 window_note_windowed_resize(1920,1080);assert(windowed_width==1200 && settings_windowed_width==1200);
 settings.fullscreen=0;settings_tmp.fullscreen=0;apply();assert(!full && exit_calls==1 && actual_w==1200 && actual_h==700);
 window_note_windowed_resize(actual_w,actual_h);assert(settings_windowed_width==1200);
 settings_windowed_width=1300;actual_w=1250;actual_h=750;window_note_windowed_resize(actual_w,actual_h);
 assert(settings_windowed_width==1300 && settings_windowed_height==750); /* pending edit retained */
 fail_full=1;settings.fullscreen=settings_tmp.fullscreen=1;apply();
 assert(!full && !settings.fullscreen && !settings_tmp.fullscreen && actual_w==1250 && actual_h==750);
 puts("window backend transitions passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(stub)
 for flag in ([],['-DTEST_GLFW']):
  subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',*flag,str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
