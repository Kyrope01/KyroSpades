
/*
	Copyright (c) 2017-2020 ByteBit

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with KyroSpades.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef COMMON_H
#define COMMON_H

#ifndef OPENGL_ES
#define GLEW_STATIC
#include <GL/glew.h>
#else
#ifdef USE_SDL
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_IPHONE
/* iOS ships GL ES under the OpenGLES framework with different header paths
   than the Khronos/Android layout used everywhere else. */
#include <OpenGLES/ES1/gl.h>
#include <OpenGLES/ES1/glext.h>
#include <OpenGLES/ES2/gl.h>
#include <OpenGLES/ES2/glext.h>
#else
#include <GLES/gl.h>    /* ES 1.x — types, constants, function declarations */
#include <GLES2/gl2.h>  /* ES 2.0 — FBOs, shaders */
#endif
#endif
/* Compatibility shims */
#define glOrtho(l, r, b, t, n, f)     glOrthof((float)(l), (float)(r), (float)(b), (float)(t), (float)(n), (float)(f))
#define glReadBuffer(x) ((void)(x))
#include "gles_immediate_stubs.h" /* immediate-mode no-ops + ES 2.0 stubs */

/* Color tracking declarations are in glx.h (included by all callers) */

#undef glColor4f
#undef glColor3f
#undef glColor3ub
#undef glColor4ub
#define glColor4f(r, g, b, a)    glx_set_color4f((r), (g), (b), (a))
#define glColor3f(r, g, b)       glx_set_color4f((r), (g), (b), 1.0F)
#define glColor3ub(r, g, b)      glx_set_color4f((r)/255.0F, (g)/255.0F, (b)/255.0F, 1.0F)
#define glColor4ub(r, g, b, a)   glx_set_color4f((r)/255.0F, (g)/255.0F, (b)/255.0F, (a)/255.0F)

#define glDepthRange(a, b) glDepthRangef(a, b)
#define glClearDepth(a) glClearDepthf(a)
#endif

#ifdef USE_GLFW
#include <GLFW/glfw3.h>
#endif

#ifdef USE_SDL
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
/* On iOS SDL must own main() so it can start the UIKit run loop via its
   SDL2main shim. Defining SDL_MAIN_HANDLED there would skip that bootstrap and
   the app would launch to a black screen with no error. Every other platform
   keeps the existing manual-main behaviour. */
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
#define SDL_MAIN_HANDLED
#endif
#include <SDL2/SDL.h>
#endif

#ifdef USE_RPC
#include <discord_rpc.h>
#endif

#ifdef _WIN32
#define OS_WINDOWS
#include <sys/stat.h>
#include <stdint.h>
#include <direct.h>
#endif

#ifdef __linux__
#define OS_LINUX
#include <sys/sysinfo.h>
#endif

#ifdef __APPLE__
#define OS_APPLE
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define OS_IOS
#endif
#endif

#ifdef __HAIKU__
#define OS_HAIKU
#endif

/* Portable directory creation. POSIX mkdir() takes a mode; Windows _mkdir()
   does not. This wrapper takes the mode everywhere and ignores it on Windows,
   so call sites stay uniform: ks_mkdir(path, 0755). Replaces the old CI-time
   sed patch that rewrote mkdir() calls on Windows builds. (file.c does its own
   #ifdef split for mkdir and deliberately does NOT use this.) */
#ifdef _WIN32
static inline int ks_mkdir(const char* path, int mode) { (void)mode; return _mkdir(path); }
#else
#include <sys/stat.h>
static inline int ks_mkdir(const char* path, int mode) { return mkdir(path, mode); }
#endif

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif

#define absf(a) (((a) > 0) ? (a) : -(a))

#define distance2D(x1, y1, x2, y2) (((x2) - (x1)) * ((x2) - (x1)) + ((y2) - (y1)) * ((y2) - (y1)))
#define distance3D(x1, y1, z1, x2, y2, z2)                                                                             \
	(((x2) - (x1)) * ((x2) - (x1)) + ((y2) - (y1)) * ((y2) - (y1)) + ((z2) - (z1)) * ((z2) - (z1)))
#define chebyshev(dx, dy, dz)                                                                                          \
	max(absf(dx), max(absf(dy), absf(dz)))
#define angle3D(x1, y1, z1, x2, y2, z2) acos((x1) * (x2) + (y1) * (y2) + (z1) * (z2)) // vectors should be normalized
#define len2D(x, y) sqrt((x) * (x) + (y) * (y))
#define len3D(x, y, z) sqrt((x) * (x) + (y) * (y) + (z) * (z))

#define rgba(r, g, b, a) (((int)(a) << 24) | ((int)(b) << 16) | ((int)(g) << 8) | (int)(r))
#define rgb(r, g, b) (((b) << 16) | ((g) << 8) | (r))
#define rgb2bgr(col) rgb(blue(col), green(col), red(col))
#define red(col) ((col)&0xFF)
#define green(col) (((col) >> 8) & 0xFF)
#define blue(col) (((col) >> 16) & 0xFF)
#define alpha(col) (((col) >> 24) & 0xFF)

#define PI 3.1415F
#define DOUBLEPI (PI * 2.0F)
#define HALFPI (PI * 0.5F)
#define EPSILON 0.005F

#define MOUSE_SENSITIVITY 0.002F

#define CHAT_NO_INPUT 0
#define CHAT_ALL_INPUT 1
#define CHAT_TEAM_INPUT 2

extern int chat_input_mode;
extern float last_cy;

extern int fps;

extern char chat[3][128][256];
extern unsigned int chat_color[3][128];
extern float chat_timer[3][128];
extern unsigned int chat_history_pos;
extern char chat_popup[256];
extern float chat_popup_timer;
extern float chat_popup_duration;
extern int chat_popup_color;
void chat_add(int channel, unsigned int color, const char* msg);
void chat_clear(int channel);
void chat_showpopup(const char* msg, float duration, int color);
const char* reason_disconnect(int code);

/* Session log: append-only buffer of channel-0 chat for the chatlog wall.
   Holds the full current connection session, unlike chat[0][] which is
   capped at 127 slots. Reset on chat_clear(0). */
#define SESSION_LOG_MAX 4096
extern char         session_log_raw[SESSION_LOG_MAX][256];
extern unsigned int session_log_color[SESSION_LOG_MAX];
extern int          session_log_count;

#define SCREEN_NONE 0
#define SCREEN_TEAM_SELECT 1
#define SCREEN_GUN_SELECT 2

extern int ms_seed;
int ms_rand(void);

#include <stdlib.h>
#include "log.h"

#define CHECK_ALLOCATION_ERROR(ret)                                                                                    \
	if(!ret) {                                                                                                         \
		log_fatal("Critical error: memory allocation failed (%s:%d)", __func__, __LINE__);                             \
		exit(1);                                                                                                       \
	}

#endif


