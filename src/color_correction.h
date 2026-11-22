#ifndef COLOR_CORRECTION_H
#define COLOR_CORRECTION_H

/* KyroSpades' neutral SDR grade replaces the original ZeroSpades-style
 * S-curve, which darkened already shaded voxel faces. Lift toward white
 * without a channel bias, then boost muted colors within channel headroom. */

/* Shared verbatim by the Core/GLES and legacy GLSL programs. */
#define COLOR_CORRECTION_GLSL \
        "uniform float pp_cc_enabled;\n" \
        "vec3 colorCorrect(vec3 c) {\n" \
        "    if(pp_cc_enabled < 0.5) return c;\n" \
        "    c = clamp(c, 0.0, 1.0);\n" \
        "    float peak = max(c.r, max(c.g, c.b));\n" \
        "    c *= 1.0 + 0.24 * (1.0 - peak);\n" \
        "    float lo = min(c.r, min(c.g, c.b));\n" \
        "    float hi = max(c.r, max(c.g, c.b));\n" \
        "    float y = dot(c, vec3(0.299, 0.587, 0.114));\n" \
        "    float boost = 0.12 * (1.0 - (hi - lo));\n" \
        "    boost = min(boost, min(lo / max(y - lo, 0.001), (1.0 - hi) / max(hi - y, 0.001)));\n" \
        "    return clamp(c + (c - vec3(y)) * boost, 0.0, 1.0);\n" \
        "}\n"

#endif
