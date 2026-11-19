#ifndef COLOR_CORRECTION_H
#define COLOR_CORRECTION_H

/* Adapted from ZeroSpades (OpenSpades), copyright (c) 2013 yvt,
 * GPL-3.0-or-later. Upstream revision 8ca45f4cc3418b90afb0fe2ee60fc83775b8eccc:
 * Resources/Shaders/OpenGL/PostFilters/ColorCorrection.fs and
 * Sources/Draw/OpenGL/GLRenderer.cpp, GLColorCorrectionFilter.cpp.
 * Uses the upstream bloom-off profile because KyroSpades has no bloom pass.
 * Keep the upstream curve, but omit the fog-dependent white balance so
 * blue skies do not push the entire frame toward pink. */

/* Shared verbatim by the Core/GLES and legacy GLSL programs. */
#define COLOR_CORRECTION_GLSL \
        "uniform float pp_cc_enabled;\n" \
        "vec3 colorCorrect(vec3 c) {\n" \
        "    if(pp_cc_enabled < 0.5) return c;\n" \
        "    return mix(c, smoothstep(0.0, 1.0, c), 0.3);\n" \
        "}\n"

#endif
