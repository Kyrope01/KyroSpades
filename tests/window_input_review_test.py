from pathlib import Path
import subprocess,tempfile
s=(Path(__file__).resolve().parents[1]/'src/window.c').read_text()
def section(a,b): i=s.index(a);return s[i:s.index(b,i)]
mouse=section('static void window_impl_mouseclick(', '\nstatic void window_impl_mouse(')
start=s.index('void window_present()',s.index('#ifdef USE_GLFW'));update=s[start:s.index('\nint window_closed()',start)]
clip=section('static char* window_clipboard_cache', '\nvoid window_setclipboard(',s.index('#ifdef USE_SDL\n\nvoid window_textinput')) if False else None
sd=s.index('#ifdef USE_SDL\n\nvoid window_textinput');a=s.index('static char* window_clipboard_cache',sd);clip=s[a:s.index('\nvoid window_setclipboard(',a)]
h=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef struct {int x;} GLFWwindow;
typedef struct {unsigned char buttons[16];float axes[8];} GLFWgamepadstate;
#define GLFW_MOUSE_BUTTON_LEFT 0
#define GLFW_MOUSE_BUTTON_RIGHT 1
#define GLFW_MOUSE_BUTTON_MIDDLE 2
#define WINDOW_MOUSE_LMB 0
#define WINDOW_MOUSE_RMB 1
#define WINDOW_MOUSE_MMB 2
#define GLFW_RELEASE 0
#define GLFW_PRESS 1
#define WINDOW_RELEASE 0
#define WINDOW_PRESS 1
#define GLFW_MOD_CONTROL 1
#define GLFW_MOD_SUPER 2
#define GLFW_GAMEPAD_BUTTON_DPAD_UP 0
#define GLFW_GAMEPAD_BUTTON_DPAD_DOWN 1
#define GLFW_GAMEPAD_BUTTON_DPAD_LEFT 2
#define GLFW_GAMEPAD_BUTTON_DPAD_RIGHT 3
#define GLFW_GAMEPAD_BUTTON_START 4
#define GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER 5
#define GLFW_GAMEPAD_BUTTON_LEFT_THUMB 6
#define GLFW_GAMEPAD_BUTTON_LEFT_BUMPER 7
#define GLFW_GAMEPAD_BUTTON_X 8
#define GLFW_GAMEPAD_BUTTON_A 9
#define GLFW_GAMEPAD_BUTTON_B 10
#define GLFW_GAMEPAD_AXIS_LEFT_Y 0
#define GLFW_GAMEPAD_AXIS_LEFT_X 1
#define GLFW_GAMEPAD_AXIS_RIGHT_X 2
#define GLFW_GAMEPAD_AXIS_RIGHT_Y 3
#define WINDOW_KEY_TOOL1 0
#define WINDOW_KEY_TOOL3 1
#define WINDOW_KEY_TOOL4 2
#define WINDOW_KEY_TOOL2 3
#define WINDOW_KEY_ESCAPE 4
#define WINDOW_KEY_SPACE 5
#define WINDOW_KEY_CROUCH 6
#define WINDOW_KEY_SPRINT 7
#define WINDOW_KEY_RELOAD 8
#define WINDOW_KEY_UP 9
#define WINDOW_KEY_DOWN 10
#define WINDOW_KEY_LEFT 11
#define WINDOW_KEY_RIGHT 12
struct window_instance {void*impl;} instance,*hud_window=&instance;
int joystick_id,joystick_available=1,window_pressed_keys[32],clicks,old_first,fail_state=1,freed;
float joystick_mouse[2];GLFWgamepadstate joystick_state;
void mouse_click(void*w,int b,int a,int m){(void)w;(void)b;(void)a;(void)m;clicks++;}
void mouse(void*w,float x,float y){(void)w;(void)x;(void)y;}
void window_reapply_mousemode(void){}
void glfwSwapBuffers(void*w){(void)w;}void glfwPollEvents(void){}int glfwJoystickIsGamepad(int id){(void)id;return 1;}
int glfwGetGamepadState(int id,GLFWgamepadstate*s){(void)id;if(fail_state)return 0;memset(s,0,sizeof(*s));return 1;}
void gamepad_translate_key(GLFWgamepadstate*a,GLFWgamepadstate*b,int c,int d){(void)a;(void)d;if(c==0)old_first=b->buttons[0];}
void gamepad_translate_button(GLFWgamepadstate*a,GLFWgamepadstate*b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
char* SDL_GetClipboardText(void){char*p=malloc(3);memcpy(p,"ok",3);return p;}
int SDL_HasClipboardText(void){return 1;}void SDL_free(void*p){freed++;free(p);}
''' + mouse + '\n' + update + '\n' + clip + r'''
int main(void){window_impl_mouseclick(0,4,GLFW_PRESS,0);assert(!clicks);window_impl_mouseclick(0,0,GLFW_PRESS,0);assert(clicks==1);
 joystick_state.buttons[0]=1;window_update();assert(joystick_state.buttons[0]==1);fail_state=0;window_update();assert(old_first==1&&!joystick_state.buttons[0]);
 assert(!strcmp(window_clipboard(),"ok"));assert(!strcmp(window_clipboard(),"ok")&&freed==1);SDL_free(window_clipboard_cache);window_clipboard_cache=0;
 puts("window input and clipboard behavior passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'x.c').write_text(h);subprocess.run(['cc','-std=c11','-fsanitize=undefined','-fno-sanitize-recover=all',str(p/'x.c'),'-o',str(p/'x')],check=True);subprocess.run([str(p/'x')],check=True)
assert s.count('if(!output || length == 0) return;') == 2
assert 'name ? name : "(unknown)"' in s
