
#pragma once
/*
 * gles_immediate_stubs.h  —  Android / OpenGL ES constant defines
 *
 * <GLES/gl.h> provides all ES 1.x function declarations and most constants.
 * This header defines extra constants not present in <GLES2/gl2.h> or
 * <GLES/gl.h> (e.g. GL_QUADS, extension enums).
 *
 * ES 1.x functions are linked from libGLESv1_CM.so — no stubs needed.
 */
#ifdef OPENGL_ES

extern int gles_version;

/* ── Constants not in GLES2 headers ─────────────────────────────────────── */
#ifndef GL_QUADS
#define GL_QUADS 0x0007
#endif
#ifndef GL_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#ifndef GL_POINT_DISTANCE_ATTENUATION
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#endif
#ifndef GL_COMBINE
#define GL_COMBINE 0x8570
#endif

#endif /* OPENGL_ES */


