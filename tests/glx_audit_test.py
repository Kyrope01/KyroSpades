"""Execute real GLX display-list/cache/lifecycle code against a fake GL driver."""
from pathlib import Path
import subprocess, tempfile
root = Path(__file__).resolve().parents[1]
s = (root/'src/glx.c').read_text()
h = (root/'src/glx.h').read_text()
struct = h[h.index('struct glx_displaylist {'):h.index('\nvoid glx_init(')]
cache = s[s.index('#define GLX_UNIFORM_CACHE_CAPACITY'):s.index('/* ── Programmable default shader')]
create_update = s[s.index('void glx_displaylist_create('):s.index('/* ── Client-array shadow')]
draw = s[s.index('void glx_displaylist_draw('):s.index('/* ── Programmable helper draw functions')]
stub = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
typedef unsigned GLuint;typedef int GLint;typedef unsigned GLenum;typedef short GLshort;typedef float GLfloat;typedef unsigned char GLubyte;typedef signed char GLbyte;
#define GL_ARRAY_BUFFER 1
#define GL_VERTEX_ARRAY 2
#define GL_COLOR_ARRAY 3
#define GL_NORMAL_ARRAY 4
#define GL_TEXTURE_COORD_ARRAY 5
#define GL_COMPILE 6
#define GL_SHORT 7
#define GL_FLOAT 8
#define GL_UNSIGNED_BYTE 9
#define GL_BYTE 10
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_POINTS 11
#define GL_QUADS 12
#define GL_TRIANGLES 13
#define GL_DYNAMIC_DRAW 14
#define log_warn(...) ((void)0)
struct {int force_displaylist,dynamic_lights;} settings={0,1};
int glx_version=1,explicit_vertex_attributes=1,cs_trusted=0;
struct {unsigned draw_calls,attrib_calls,buffer_uploads;unsigned long buffer_bytes;} glx_stats;
void glx_stats_draw(void){glx_stats.draw_calls++;} void glx_stats_attrib(void){glx_stats.attrib_calls++;} void glx_stats_upload(unsigned long n){glx_stats.buffer_uploads++;glx_stats.buffer_bytes+=n;}
int list_created,buffer_created,list_deleted,buffer_deleted,list_draw,draws,lookups,glprogram,bindzero;
int default_shader=7,loc_u_HasVertexColor=1,loc_u_TextureEnabled=2,loc_u_TexCoordScale=3,loc_u_LightingEnabled=4;
void glx_batch2d_flush(void){} void glUseProgram(GLuint p){glprogram=p;} void glDeleteProgram(GLuint p){(void)p;}
int glGetUniformLocation(GLuint p,const char*n){(void)p;(void)n;return ++lookups;}
bool glx_programmable_active(void){return true;}
void glx_use_default_shader(void); void matrix_upload(void){}
unsigned glGenLists(int n){(void)n;list_created++;return 55;}
void glGenBuffers(int n,GLuint*p){(void)n;buffer_created++;*p=66;}
void glDeleteLists(GLuint x,int n){assert(x==55&&n==1);list_deleted++;}
void glDeleteBuffers(int n,const GLuint*x){assert(n==1&&*x==66);buffer_deleted++;}
void glBindBuffer(GLenum a,GLuint n){(void)a;if(n==0)bindzero++;}
void glBufferData(GLenum a,size_t n,const void*p,GLenum t){(void)a;(void)n;(void)p;(void)t;}
void glBufferSubData(GLenum a,size_t o,size_t n,const void*p){(void)a;(void)o;(void)n;(void)p;}
void glEnableClientState(int x){(void)x;}void glDisableClientState(int x){(void)x;}
void glNewList(GLuint x,int y){(void)x;(void)y;}void glEndList(void){}void glCallList(GLuint x){assert(x==55);list_draw++;}
void glVertexPointer(int n,int t,int stride,const void*p){(void)n;(void)t;(void)stride;(void)p;}
void glColorPointer(int n,int t,int stride,const void*p){(void)n;(void)t;(void)stride;(void)p;}
void glNormalPointer(int t,int stride,const void*p){(void)t;(void)stride;(void)p;}
void glTexCoordPointer(int n,int t,int stride,const void*p){(void)n;(void)t;(void)stride;(void)p;}
void glDrawArrays(int mode,int first,int n){(void)mode;(void)first;(void)n;draws++;}
void glVertexAttribPointer(int i,int n,int t,int norm,int stride,const void*p){(void)i;(void)n;(void)t;(void)norm;(void)stride;(void)p;}
void glEnableVertexAttribArray(int x){(void)x;}void glDisableVertexAttribArray(int x){(void)x;}
void glVertexAttrib4f(int i,float a,float b,float c,float d){(void)i;(void)a;(void)b;(void)c;(void)d;}
void glVertexAttrib3f(int i,float a,float b,float c){(void)i;(void)a;(void)b;(void)c;}
void glVertexAttrib2f(int i,float a,float b){(void)i;(void)a;(void)b;}
void glUniform1f(int i,float n){(void)i;(void)n;}
void glx_client_state_invalidate(void){}
'''
mid = r'''
void glx_use_default_shader(void){glx_use_program(default_shader);}
#ifndef OPENGL_CORE
void cs_bind(struct glx_displaylist*x,int type,size_t a,size_t b,size_t c){(void)x;(void)type;(void)a;(void)b;(void)c;}
void cs_release(void){}
#endif
'''
checks = r'''
int main(void) {
 glx_program_state_reset();
 assert(glx_uniform_location(1,"u_Model")==1);assert(glx_uniform_location(1,"u_Model")==1);assert(lookups==1);
 glx_delete_program(1);assert(glx_uniform_location(1,"u_Model")==2);
 char names[513][24];int collision=-1;size_t n;
 for(int i=0;i<513;i++){snprintf(names[i],sizeof(names[i]),"u_%d",i);for(int j=0;j<i;j++)if(glx_uniform_hash(3,names[i],&n)%512==glx_uniform_hash(3,names[j],&n)%512){collision=j;break;}if(collision>=0){int a=glx_uniform_location(3,names[collision]);glx_uniform_location(3,names[i]);int b=glx_uniform_location(3,names[collision]);assert(b!=a);break;}}
 assert(collision>=0);
 struct glx_displaylist d; float verts[9]={0};
#ifndef GLX_PROGRAMMABLE
 settings.force_displaylist=1;
 glx_displaylist_create(&d,false,false);assert(d.use_legacy && d.modern==0);
 settings.force_displaylist=0;glx_displaylist_update(&d,3,GLX_DISPLAYLIST_ENHANCED,NULL,verts,NULL,NULL);
 assert(bindzero);glx_displaylist_draw(&d,GLX_DISPLAYLIST_ENHANCED);assert(list_draw==1);
 glx_displaylist_draw(&d,GLX_DISPLAYLIST_NORMAL);assert(list_draw==1);
 glx_displaylist_destroy(&d);assert(list_deleted==1 && buffer_deleted==0);
 settings.force_displaylist=0;glx_displaylist_create(&d,false,false);settings.force_displaylist=1;
 glx_displaylist_update(&d,3,GLX_DISPLAYLIST_ENHANCED,NULL,verts,NULL,NULL);
 glx_displaylist_draw(&d,GLX_DISPLAYLIST_ENHANCED);assert(draws>=1);glx_displaylist_destroy(&d);assert(buffer_deleted==1);
#else
 glx_displaylist_create(&d,false,false);glx_displaylist_update(&d,3,GLX_DISPLAYLIST_ENHANCED,NULL,verts,NULL,NULL);
 glx_use_program(0);glx_stats.draw_calls=0;glx_stats.attrib_calls=0;glx_displaylist_draw(&d,GLX_DISPLAYLIST_ENHANCED);
 assert(draws==1 && glx_stats.draw_calls==1 && glx_stats.attrib_calls==6 && glprogram==default_shader);
 glx_displaylist_draw(&d,GLX_DISPLAYLIST_NORMAL);assert(draws==1);
 glx_displaylist_destroy(&d);assert(buffer_deleted==1);
#endif
 puts("GLX cache, backend, draw and stats checks passed");return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(stub+struct+cache+mid+create_update+draw+checks)
 for defines in ([],['-DGLX_PROGRAMMABLE','-DOPENGL_CORE']):
  exe=Path(d)/('core' if defines else 'legacy')
  subprocess.run(['gcc','-std=c99',*defines,str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
assert 'batch2d_verts = 0;' in s[s.index('void glx_deinit'):s.index('/* ── Shader compilation')]
assert 'glDeleteBuffers(1, &batch2d_vbo)' in s
assert 'compile_failed = true;' in s and 'if(compile_failed)' in s
assert 'float verts[(1024 + 1) * 6];' in s
assert 'glx_color_byte(gles_current_color[0])' in s
assert 'glUniform4f(loc_u_TeamColor, default_team_color[0]' in s
print('GLX teardown, shader, ring and batch integration checks passed')

# Exercise the production batch flush and teardown, including non-white tint
# and a queued batch present during context destruction.
batch_state = s[s.index('#define GLX_BATCH2D_MAX_VERTS'):s.index('/* ── Shader compilation')]
batch_code = s[s.index('bool glx_batch2d_enabled(void)'):s.index('\nvoid glx_batch2d_enable(bool on)')]
batch_stub = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <stdio.h>
#define GLX_PROGRAMMABLE
#define OPENGL_CORE
#define GL_ARRAY_BUFFER 1
#define GL_STREAM_DRAW 2
#define GL_FLOAT 3
#define GL_FALSE 0
#define GL_UNSIGNED_BYTE 4
#define GL_TRUE 1
#define GL_TRIANGLES 5
typedef unsigned GLuint;typedef int GLboolean;
struct {unsigned draw_calls,attrib_calls,buffer_uploads;unsigned long buffer_bytes;} glx_stats;
float default_team_color[3]={0.2f,0.4f,0.6f},gles_current_color[4]={1,1,1,1};
GLuint default_shader=7,line_quad_vbo=0,quad_vbo=0,default_vao=0,quad_vao=0;int loc_u_TeamColor=3;
int draw_count,delete_count,draw_tint_white,restored,used=88,fail_buffer;
float requested_line_width=4;void glx_set_explicit_attributes(bool b){assert(!b);}
unsigned glx_current_program(void){return (unsigned)used;}
void glx_stats_draw(void){glx_stats.draw_calls++;}
bool glx_programmable_active(void){return true;}
void glx_use_program(unsigned p){used=p;}
void glx_use_default_shader(void){glx_use_program(default_shader);}
void glx_delete_program(unsigned p){(void)p;}
void glx_program_state_reset(void){}
void glx_default_shader_set_draw_state(int a,int b,int c){assert(a==1&&b==0&&c==0);}
void glDeleteBuffers(int n,const GLuint*p){assert(n==1&&*p==44);delete_count++;}
void glBindVertexArray(unsigned n){(void)n;}void glDeleteVertexArrays(int n,const GLuint*p){(void)n;(void)p;}
void glGenBuffers(int n,GLuint*p){(void)n;*p=fail_buffer ? 0 : 44;}
void glBindBuffer(int a,unsigned p){(void)a;(void)p;}
void glBufferData(int a,size_t n,const void*p,int usage){(void)a;(void)n;(void)p;(void)usage;}
void glBufferSubData(int a,size_t o,size_t n,const void*p){(void)a;(void)o;(void)n;(void)p;}
void glUniform4f(int loc,float r,float g,float b,float a){assert(loc==3&&a==1);if(r==1&&g==1&&b==1)draw_tint_white=1;else{assert(r==.2f&&g==.4f&&b==.6f);restored=1;}}
void glVertexAttribPointer(int a,int b,int c,int d,int e,const void*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;}
void glEnableVertexAttribArray(int a){(void)a;}void glDisableVertexAttribArray(int a){(void)a;}
void glDrawArrays(int a,int b,int c){(void)a;(void)b;assert(c==6);assert(draw_tint_white);draw_count++;}
void glx_get_current_color(float*p){for(int i=0;i<4;i++)p[i]=1;}
void glColor4f(float r,float g,float b,float a){(void)r;(void)g;(void)b;(void)a;}
void glx_client_state_invalidate(void){}
'''
batch_checks = r'''
int main(void){
 batch2d_enabled=true;batch2d_verts=6;fail_buffer=1;glx_batch2d_flush();
 assert(!draw_count && !batch2d_vbo && !batch2d_flushing && used==88);
 batch2d_verts=6;fail_buffer=0;glx_batch2d_flush();
 assert(draw_count==1 && restored && batch2d_verts==0 && batch2d_vbo==44 && used==88);
 assert(glx_stats.draw_calls==1 && glx_stats.attrib_calls==6 && glx_stats.buffer_uploads==3 && glx_stats.buffer_bytes==96);
 batch2d_verts=6;glx_deinit();assert(draw_count==1 && delete_count==1 && batch2d_vbo==0 && !batch2d_enabled && !batch2d_verts);
 assert(default_shader==0 && used==0 && requested_line_width==1);puts("GLX batch tint, flush and teardown checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'batch.c';p.write_text(batch_stub+batch_state+batch_code+batch_checks);exe=Path(d)/'batch'
 subprocess.run(['gcc','-std=c99',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)

shader = s[s.index('int glx_shader('):s.index('/* ── Display list abstraction')]
shader_stub = r'''
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#define GL_VERTEX_SHADER 1
#define GL_FRAGMENT_SHADER 2
#define GL_COMPILE_STATUS 3
#define GL_LINK_STATUS 4
#define log_error(...) ((void)0)
typedef char GLchar;typedef int GLint;
int glx_version=1, fail_shader=0, fail_program=0, fail_link=0, creates=0, links=0, deletes=0, program_deleted=0;
int glCreateShader(int type){return type;}
void glShaderSource(int s,int n,const GLchar*const* src,void*p){(void)s;(void)n;(void)src;(void)p;}
void glCompileShader(int s){(void)s;}
void glGetShaderiv(int s,int what,GLint*ok){(void)what;*ok=s!=fail_shader;}
void glGetShaderInfoLog(int s,int n,void*p,char*log){(void)s;(void)n;(void)p;log[0]=0;}
void glDeleteShader(int s){(void)s;deletes++;}
int glCreateProgram(void){creates++;return fail_program ? 0 : 10;}
void glAttachShader(int p,int s){(void)p;(void)s;}
void glBindAttribLocation(int p,int a,const char*n){(void)p;(void)a;(void)n;}
void glLinkProgram(int p){(void)p;links++;}
void glGetProgramiv(int p,int what,int*ok){(void)p;(void)what;*ok=!fail_link;}
void glGetProgramInfoLog(int p,int n,void*x,char*l){(void)p;(void)n;(void)x;l[0]=0;}
void glx_delete_program(int p){assert(p==10);program_deleted++;}
'''
shader_checks = r'''
int main(void){
 fail_shader=1;assert(glx_shader("v","f")==0);assert(creates==0&&links==0&&deletes==2);
 fail_shader=0;fail_program=1;assert(glx_shader("v","f")==0);assert(links==0&&deletes==4);
 fail_program=0;fail_link=1;assert(glx_shader("v","f")==0);assert(links==1&&program_deleted==1&&deletes==6);
 fail_link=0;assert(glx_shader("v","f")==10);assert(links==2&&deletes==8);
 puts("GLX shader compile/link failure checks passed");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'shader.c';p.write_text(shader_stub+shader+shader_checks);exe=Path(d)/'shader'
 subprocess.run(['gcc','-std=c99',str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
