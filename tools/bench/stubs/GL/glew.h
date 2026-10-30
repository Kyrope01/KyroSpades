/* Minimal GL stub for the headless CPU benchmarks.
 *
 * The benchmarks compile real engine translation units (water.c, chunk.c,
 * tesselator.c) on machines and CI runners that have no GL headers, no GPU
 * and no window system. Every entry point below is a no-op that records
 * nothing; only the CPU-side algorithms are being measured.
 *
 * Keep this in sync with what the benchmarked files actually call — it is
 * deliberately NOT a full GL header.
 */
#ifndef BENCH_GL_STUB_H
#define BENCH_GL_STUB_H

#include <stddef.h>
#include <stdint.h>

typedef unsigned int  GLenum;
typedef unsigned char GLboolean;
typedef unsigned int  GLbitfield;
typedef void          GLvoid;
typedef signed char   GLbyte;
typedef short         GLshort;
typedef int           GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int  GLuint;
typedef int           GLsizei;
typedef float         GLfloat;
typedef double        GLdouble;
typedef char          GLchar;
typedef ptrdiff_t     GLintptr;
typedef ptrdiff_t     GLsizeiptr;
typedef unsigned short GLhalf;

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_NONE 0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_RGBA 0x1908
#define GL_ALPHA 0x1906
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_BYTE 0x1400
#define GL_SHORT 0x1402
#define GL_INT 0x1404
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_QUADS 0x0007
#define GL_LINES 0x0001
#define GL_POINTS 0x0000
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_BLEND 0x0BE2
#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_SCISSOR_TEST 0x0C11
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_COMBINE 0x8570
#define GL_POLYGON_OFFSET_FILL 0x8037
#define GL_POLYGON_OFFSET_FACTOR 0x8038
#define GL_POLYGON_OFFSET_UNITS 0x2A00
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_PROGRAM_POINT_SIZE 0x8642
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_RENDERBUFFER_BINDING 0x8CA7
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_DEPTH_COMPONENT 0x1902
#define GL_FOG 0x0B60
#define GL_NO_ERROR 0

static inline void glActiveTexture(GLenum t) { (void)t; }
static inline void glBindTexture(GLenum t, GLuint x) { (void)t; (void)x; }
static inline void glGenTextures(GLsizei n, GLuint* x) { (void)n; (void)x; }
static inline void glDeleteTextures(GLsizei n, const GLuint* x) { (void)n; (void)x; }
static inline void glTexImage2D(GLenum a, GLint b, GLint c, GLsizei d, GLsizei e, GLint f, GLenum g, GLenum h,
                                const void* i) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; }
static inline void glTexSubImage2D(GLenum a, GLint b, GLint c, GLint d, GLsizei e, GLsizei f, GLenum g, GLenum h,
                                   const void* i) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;(void)i; }
static inline void glTexParameteri(GLenum a, GLenum b, GLint c) { (void)a;(void)b;(void)c; }
/* Legacy fixed-function helpers used by font.c/glx.c. */
static inline void glMatrixMode(GLenum a) { (void)a; }
static inline void glLoadIdentity(void) {}
static inline void glMultMatrixf(const float* m) { (void)m; }
static inline void glTexEnvi(GLenum a, GLenum b, GLint c) { (void)a;(void)b;(void)c; }
static inline void glTexEnvfv(GLenum a, GLenum b, const float* c) { (void)a;(void)b;(void)c; }
static inline void glLoadMatrixf(const float* m) { (void)m; }
static inline void glLightfv(GLenum a, GLenum b, const float* c) { (void)a;(void)b;(void)c; }
static inline void glLightModelfv(GLenum a, const float* b) { (void)a;(void)b; }
static inline void glMaterialfv(GLenum a, GLenum b, const float* c) { (void)a;(void)b;(void)c; }


static inline void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; }
static inline void glGenBuffers(GLsizei n, GLuint* b) { (void)n; (void)b; }
static inline void glDeleteBuffers(GLsizei n, const GLuint* b) { (void)n; (void)b; }
static inline void glBufferData(GLenum t, GLsizeiptr s, const void* d, GLenum u) { (void)t;(void)s;(void)d;(void)u; }
static inline void glBufferSubData(GLenum t, GLintptr o, GLsizeiptr s, const void* d) { (void)t;(void)o;(void)s;(void)d; }

static inline void glDrawArrays(GLenum m, GLint first, GLsizei count) { (void)m;(void)first;(void)count; }
static inline void glVertexAttribPointer(GLuint i, GLint s, GLenum t, GLboolean n, GLsizei stride, const void* p) {
        (void)i;(void)s;(void)t;(void)n;(void)stride;(void)p; }
static inline void glEnableVertexAttribArray(GLuint i) { (void)i; }
static inline void glDisableVertexAttribArray(GLuint i) { (void)i; }
static inline void glVertexPointer(GLint s, GLenum t, GLsizei stride, const void* p) { (void)s;(void)t;(void)stride;(void)p; }
static inline void glNormalPointer(GLenum t, GLsizei stride, const void* p) { (void)t;(void)stride;(void)p; }
static inline void glColorPointer(GLint s, GLenum t, GLsizei stride, const void* p) { (void)s;(void)t;(void)stride;(void)p; }
static inline void glTexCoordPointer(GLint s, GLenum t, GLsizei stride, const void* p) { (void)s;(void)t;(void)stride;(void)p; }
static inline void glEnableClientState(GLenum c) { (void)c; }
static inline void glDisableClientState(GLenum c) { (void)c; }
static inline void glEnable(GLenum c) { (void)c; }
static inline void glDisable(GLenum c) { (void)c; }
static inline GLboolean glIsEnabled(GLenum c) { (void)c; return GL_FALSE; }
static inline void glGetFloatv(GLenum p, GLfloat* v) { (void)p; v[0] = 0.0f; }
static inline void glGetIntegerv(GLenum p, GLint* v) { (void)p; v[0] = 0; }
static inline void glPolygonOffset(GLfloat a, GLfloat b) { (void)a; (void)b; }
static inline void glUniform1f(GLint l, GLfloat a) { (void)l; (void)a; }
static inline void glUniform2f(GLint l, GLfloat a, GLfloat b) { (void)l;(void)a;(void)b; }
static inline void glUniform3f(GLint l, GLfloat a, GLfloat b, GLfloat c) { (void)l;(void)a;(void)b;(void)c; }
static inline void glBlendFunc(GLenum a, GLenum b) { (void)a; (void)b; }
static inline void glColor4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) { (void)a;(void)b;(void)c;(void)d; }
static inline void glColor3f(GLfloat a, GLfloat b, GLfloat c) { (void)a;(void)b;(void)c; }
static inline void glColor3ub(GLubyte a, GLubyte b, GLubyte c) { (void)a;(void)b;(void)c; }
static inline void glLineWidth(GLfloat w) { (void)w; }

/* Immediate mode: only reached by code paths the benchmark never takes. */
static inline void glBegin(GLenum m) { (void)m; }
static inline void glEnd(void) {}
static inline void glVertex2f(GLfloat a, GLfloat b) { (void)a; (void)b; }
static inline void glTexCoord2f(GLfloat a, GLfloat b) { (void)a; (void)b; }

/* Enums needed to syntax-check the 2D and post-process translation units. */
#define GL_ALPHA_TEST 0x9100
#define GL_AMBIENT 0x9101
#define GL_COLOR_MATERIAL 0x9102
#define GL_COMPILE 0x9103
#define GL_COMPILE_STATUS 0x9104
#define GL_DIFFUSE 0x9105
#define GL_EYE_LINEAR 0x9106
#define GL_EYE_PLANE 0x9107
#define GL_FRAGMENT_SHADER 0x9108
#define GL_FRONT 0x9109
#define GL_GREATER 0x910A
#define GL_LIGHT1 0x910B
#define GL_LIGHTING 0x910C
#define GL_LIGHT_MODEL_AMBIENT 0x910D
#define GL_LINK_STATUS 0x910E
#define GL_MAX_TEXTURE_SIZE 0x910F
#define GL_MODELVIEW 0x9110
#define GL_MODULATE 0x9111
#define GL_POSITION 0x9112
#define GL_S 0x9113
#define GL_SPOT_CUTOFF 0x9114
#define GL_SPOT_DIRECTION 0x9115
#define GL_SPOT_EXPONENT 0x9116
#define GL_T 0x9117
#define GL_TEXTURE 0x9118
#define GL_TEXTURE_ENV 0x9119
#define GL_TEXTURE_ENV_COLOR 0x911A
#define GL_TEXTURE_ENV_MODE 0x911B
#define GL_TEXTURE_GEN_MODE 0x911C
#define GL_TEXTURE_GEN_S 0x911D
#define GL_TEXTURE_GEN_T 0x911E
#define GL_TEXTURE_WRAP_S 0x911F
#define GL_TEXTURE_WRAP_T 0x9120
#define GL_VERSION 0x9121
#define GL_VERTEX_SHADER 0x9122
#define GL_COLOR_BUFFER_BIT 0x9380
#define GL_DEPTH_BUFFER_BIT 0x9381
#define GL_DEPTH_COMPONENT24 0x9383
#define GL_DRAW_FRAMEBUFFER 0x9384
#define GL_EXTENSIONS 0x9385
#define GL_MAX_SAMPLES 0x9387
#define GL_READ_FRAMEBUFFER 0x9388
#define GL_RENDERBUFFER 0x9389
#define GL_REPEAT 0x938A
#define GL_RGBA8 0x938C
#define GL_TEXTURE_BINDING_2D 0x938D




/* Entry points the harness only needs to type-check, never to run. Signatures
   are as loose as C89 allows; nothing here is linked or executed. */
extern GLuint glCreateShader(GLenum);
extern GLuint glCreateProgram(void);
extern GLint glGetUniformLocation(GLuint, const char*);
extern GLint glGetAttribLocation(GLuint, const char*);
extern const unsigned char* glGetString(GLenum);
extern void* glMapBuffer(GLenum, GLenum);
extern GLenum glCheckFramebufferStatus(GLenum);
extern GLenum glGetError(void);
extern void glGenFramebuffers(GLsizei, void*);
extern void glGenRenderbuffers(GLsizei, void*);
extern void glAlphaFunc();
extern void glAttachShader();
extern void glBindAttribLocation();
extern void glBindFramebuffer();
extern void glBindRenderbuffer();
extern void glBlitFramebuffer();
extern void glCreateRenderbuffers();
extern void glDeleteShader();
extern void glDetachShader();
extern void glFramebufferRenderbuffer();
extern void glFramebufferTexture2D();
extern void glLinkProgram();
extern void glRenderbufferStorage();
extern void glRenderbufferStorageMultisample();
extern void glShaderSource();
extern void glCompileShader();
extern void glUseProgram();
extern void glDeleteProgram();
extern void glUniform1i();
extern void glUniform4f();
extern void glUniform1fv();
extern void glUniform2fv();
extern void glUniform3fv();
extern void glUniform4fv();
extern void glUniformMatrix4fv();
extern void glBindVertexArray();
extern void glGenVertexArrays();
extern void glDeleteVertexArrays();
extern void glGenerateMipmap();
extern void glPixelStorei();
extern void glScissor();
extern void glViewport();
extern void glClear();
extern void glClearColor();
extern void glDepthMask();
extern void glCullFace();
extern void glFrontFace();
extern void glReadPixels();
extern void glDrawElements();


extern void glCallList();
extern void glColorMaterial();
extern void glDeleteFramebuffers();
extern void glDeleteLists();
extern void glDeleteRenderbuffers();
extern void glEndList();
extern GLuint glGenLists(GLsizei);
extern void glGetProgramInfoLog();
extern void glGetProgramiv();
extern void glGetShaderInfoLog();
extern void glGetShaderiv();
extern void glLightf();
extern void glNewList();
extern void glNormal3f();
extern void glTexGenfv();
extern void glTexGeni();
extern void glVertexAttrib2f();
extern void glVertexAttrib3f();
extern void glVertexAttrib4f();
#define GL_NOTEQUAL 0x0205
#define GL_LEQUAL 0x0203
#define GL_EQUAL 0x0202
#define GL_GEQUAL 0x0206
extern void glDepthRange();
extern void glColorMask();
extern void glDepthFunc();

extern void glColor4ub();

#endif /* BENCH_GL_STUB_H */

/* Legacy fixed-function enums and entry points used by font.c/glx.c when the
   whole 2D path is compiled for a syntax/warning check in this harness. */
