#ifndef COLOR_CORRECTION_H
#define COLOR_CORRECTION_H

/* Adapted from ZeroSpades (OpenSpades), copyright (c) 2013 yvt,
 * GPL-3.0-or-later. Upstream revision 8ca45f4cc3418b90afb0fe2ee60fc83775b8eccc:
 * Resources/Shaders/OpenGL/PostFilters/ColorCorrection.fs and
 * Sources/Draw/OpenGL/GLRenderer.cpp, GLColorCorrectionFilter.cpp.
 * Uses the upstream bloom-off profile because KyroSpades has no bloom pass.
 * Optional sharpening is off by default upstream. */

#include <math.h>

static inline void color_correction_tint(float smoothed[3], const float fog[3], float tint[3]) {
        if(smoothed[0] < 0.0F)
                for(int i = 0; i < 3; ++i)
                        smoothed[i] = fog[i];
        for(int i = 0; i < 3; ++i)
                tint[i] = 0.8F / (smoothed[i] + 0.5F) + 0.2F;
        float min_tint = fminf(tint[0], fminf(tint[1], tint[2]));
        for(int i = 0; i < 3; ++i) {
                tint[i] /= min_tint;
                smoothed[i] = smoothed[i] * 0.998F + fog[i] * 0.002F;
        }
}

/* Shared verbatim by the Core/GLES and legacy GLSL programs. */
#define COLOR_CORRECTION_GLSL \
        "uniform vec3 pp_cc_tint;\n" \
        "uniform float pp_cc_mode;\n" \
        "vec3 colorCorrect(vec3 c) {\n" \
        "    if(pp_cc_mode < 0.5) return c;\n" \
        "    bool hdr = pp_cc_mode > 1.5;\n" \
        "    if(hdr) c = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));\n" \
        "    c *= pp_cc_tint;\n" \
        "    c = mix(vec3(dot(c, vec3(1.0 / 3.0))), c, hdr ? 0.9 : 1.0);\n" \
        "    if(hdr) {\n" \
        "        vec3 x = c * c * 0.8;\n" \
        "        c = sqrt(clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0));\n" \
        "    }\n" \
        "    return mix(c, smoothstep(0.0, 1.0, c), hdr ? 0.0 : 0.3);\n" \
        "}\n"

#endif
