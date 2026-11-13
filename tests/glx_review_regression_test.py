"""Compile production GLX helpers with a fake driver for review regressions."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'src/glx.c').read_text()
common = (root/'src/common.h').read_text()
assert '#if defined(OPENGL_ES) || defined(OPENGL_CORE)\nvoid glx_set_color4f' not in common
color = s[s.index('void glx_set_color4f('):s.index('\nvoid glx_get_current_color(')]
quad = s[s.index('static void glx_ensure_quad_vbo('):s.index('\n#endif /* GLX_PROGRAMMABLE */')]
pre = r'''
#include <assert.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
typedef unsigned GLuint; typedef int GLint; typedef int GLsizei;
#define GLX_PROGRAMMABLE
#define GL_ARRAY_BUFFER 1
#define GL_ARRAY_BUFFER_BINDING 2
#define GL_VERTEX_ARRAY_BINDING 3
#define GL_STATIC_DRAW 4
#define GL_FLOAT 5
#define GL_FALSE 0
#define GL_TRIANGLES 6
float gles_current_color[4]; float real[4];
void glColor4f(float r,float g,float b,float a){real[0]=r;real[1]=g;real[2]=b;real[3]=a;}
#define glColor4f(r,g,b,a) glx_set_color4f(r,g,b,a)
GLuint quad_vbo, line_quad_vbo;
#ifdef OPENGL_CORE
GLuint quad_vao;
#endif
int fail_buffer=1,deleted_vao,drawn,program=99,deleted_buffer;
void glGetIntegerv(int t,GLint*p){*p=t==GL_VERTEX_ARRAY_BINDING?7:8;}
void glDeleteBuffers(int n,GLuint*p){(void)n;(void)p;deleted_buffer++;}
void glDeleteVertexArrays(int n,GLuint*p){(void)n;assert(*p==10);deleted_vao++;}
void glGenVertexArrays(int n,GLuint*p){(void)n;*p=10;}
void glBindVertexArray(GLuint n){(void)n;}
void glGenBuffers(int n,GLuint*p){(void)n;*p=fail_buffer?0:11;}
void glBindBuffer(int t,GLuint n){(void)t;(void)n;}
void glBufferData(int t,size_t n,const void*p,int u){(void)t;(void)n;(void)p;(void)u;}
void glVertexAttribPointer(int a,int b,int c,int d,int e,const void*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void glEnableVertexAttribArray(int n){(void)n;}void glDisableVertexAttribArray(int n){(void)n;}
void glDrawArrays(int a,int b,int c){(void)a;(void)b;(void)c;drawn++;}
unsigned glx_current_program(void){return program;}
void glx_client_state_invalidate(void){} void glx_batch2d_flush(void){} void glx_stats_draw(void){}
'''
checks = r'''
int main(void) {
#ifndef OPENGL_CORE
 glx_set_color4f(.2f,.3f,.4f,.5f);assert(real[0]==.2f && real[3]==.5f && gles_current_color[2]==.4f);
#endif
 glx_draw_screen_quad();assert(!drawn && !quad_vbo);
#ifdef OPENGL_CORE
 assert(!quad_vao && deleted_vao==1);
#endif
 fail_buffer=0;glx_draw_screen_quad();assert(drawn==1 && quad_vbo==11);
 program=0;glx_draw_screen_quad();assert(drawn==1);
 puts("GLX colour and quad allocation checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(pre+color+quad+checks)
 for flags in ([], ['-DOPENGL_CORE']):
  exe=Path(d)/('core' if flags else 'desktop')
  subprocess.run(['gcc','-std=c99',*flags,str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
for f in ('glx_enable_sphericalfog()', 'glx_disable_sphericalfog()'):
 part=s[s.index('void '+f):]; part=part[:part.index('\n}')]
 desktop=part[part.index('#elif !defined(OPENGL_ES)'):part.index('#elif defined(OPENGL_ES)')]
 assert 'if(glx_programmable_active() && default_shader)' in desktop
 assert 'glUniform1f(loc_u_FogDist,' in desktop
assert s.count('if(!line_quad_vbo)\n                        return;') == 5
assert 'if(!batch2d_vbo) {' in s
print('GLX fog and streaming failure guards verified')

# Fixed-function batching must restore the full client-array state (including
# pointers/VBO bindings), not merely re-enable a texture-coordinate array.
state=s[s.index('#define GLX_BATCH2D_MAX_VERTS'):s.index('\nvoid glx_deinit')]
batch=s[s.index('bool glx_batch2d_enabled(void)'):s.index('\nvoid glx_batch2d_enable(bool on)')]
stub=r'''
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
typedef unsigned GLuint;typedef int GLboolean;
#define GL_CLIENT_VERTEX_ARRAY_BIT 1
#define GL_TEXTURE_2D 2
#define GL_TEXTURE_COORD_ARRAY 3
#define GL_NORMAL_ARRAY 4
#define GL_VERTEX_ARRAY 5
#define GL_COLOR_ARRAY 6
#define GL_ARRAY_BUFFER 7
#define GL_FLOAT 8
#define GL_UNSIGNED_BYTE 9
#define GL_TRIANGLES 10
int glx_version=1,client=123,saved_client,pushes,pops,draws,texture=1;
float color[4]={.2f,.3f,.4f,.5f};
void glx_get_current_color(float*p){for(int i=0;i<4;i++)p[i]=color[i];}
void glColor4f(float a,float b,float c,float d){color[0]=a;color[1]=b;color[2]=c;color[3]=d;}
int glIsEnabled(int n){assert(n==GL_TEXTURE_2D);return texture;}
void glPushClientAttrib(int n){assert(n==GL_CLIENT_VERTEX_ARRAY_BIT);saved_client=client;pushes++;}
void glPopClientAttrib(void){client=saved_client;pops++;}
void glEnable(int n){(void)n;texture=1;}void glDisable(int n){(void)n;texture=0;}
void glDisableClientState(int n){client-=n;}void glEnableClientState(int n){client+=n;}
void glBindBuffer(int n,int v){(void)n;(void)v;client=456;}
void glVertexPointer(int a,int b,int c,const void*d){(void)a;(void)b;(void)c;(void)d;client=789;}
void glColorPointer(int a,int b,int c,const void*d){(void)a;(void)b;(void)c;(void)d;client=999;}
void glDrawArrays(int a,int b,int c){(void)a;(void)b;(void)c;draws++;}
void glx_stats_draw(void){}void glx_client_state_invalidate(void){}
'''
checks=r'''
int main(void){batch2d_verts=6;glx_batch2d_flush();assert(pushes==1&&pops==1&&client==123&&texture==1&&draws==1);assert(color[0]==.2f&&color[3]==.5f);puts("GLX fixed batch state restore checks passed");}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(stub+state+batch+checks);exe=Path(d)/'test'
 subprocess.run(['gcc','-std=c99',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
