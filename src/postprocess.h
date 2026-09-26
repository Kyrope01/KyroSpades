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

#ifndef POSTPROCESS_H
#define POSTPROCESS_H

#include <stdbool.h>

/* The HDR/bloom upgrade is intentionally isolated to strict OpenGL Core.
 * Compatibility desktop and GLES retain the established single-pass effect. */
bool postprocess_hdr_supported(void);

/* Build a half-resolution bright pass and four separable Gaussian blur pairs.
 * Returns the blurred texture, or 0 so the caller can use its safe fallback.
 * All OpenGL state touched by this function is restored before it returns. */
unsigned int postprocess_bloom_render(unsigned int scene_texture, int width, int height, float threshold);

void postprocess_deinit(void);

#endif
