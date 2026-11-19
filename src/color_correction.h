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
        "uniform float pp_cc_mode;\n" \
        "vec3 colorCorrect(vec3 c) {\n" \
        "    if(pp_cc_mode < 0.5) return c;\n" \
        "    bool hdr = pp_cc_mode > 1.5;\n" \
        "    if(hdr) c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));\n" \
        "    c = mix(vec3(dot(c, vec3(1.0 / 3.0))), c, hdr ? 0.9 : 1.0);\n" \
        "    if(hdr) {\n" \
        "        vec3 x = c * c * 0.8;\n" \
        "        c = sqrt(clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0));\n" \
        "    }\n" \
        "    return mix(c, smoothstep(0.0, 1.0, c), hdr ? 0.0 : 0.3);\n" \
        "}\n"

#endif
