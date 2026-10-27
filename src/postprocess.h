/*
	Copyright (c) 2024 KyroSpades contributors

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

/* Desktop GL 3+ multisampled scene target. The returned FBO is where the world
 * should be rendered; 0 means to use the established single-sampled target.
 * Configuration is cheap to repeat and recreates targets only when the sample
 * request, dimensions, or color format changes. */
unsigned int postprocess_msaa_configure(int requested_samples, int width, int height,
                                        unsigned int color_internal_format);
unsigned int postprocess_msaa_target(void);
/* Resolve both color and depth into the existing single-sampled scene FBO. */
void postprocess_msaa_resolve(unsigned int resolve_fbo);
void postprocess_msaa_reset(void);

/* The HDR/bloom upgrade is intentionally isolated to strict OpenGL Core.
 * Compatibility desktop and GLES retain the established single-pass effect. */
bool postprocess_hdr_supported(void);

/* Build a half-resolution bright pass and four separable Gaussian blur pairs.
 * Returns the blurred texture, or 0 so the caller can use its safe fallback.
 * All OpenGL state touched by this function is restored before it returns. */
unsigned int postprocess_bloom_render(unsigned int scene_texture, int width, int height, float threshold);

/* Set KS_BLOOM_TEST=1 to render a synthetic HDR highlight once and report
 * whether the real GPU bloom pipeline spreads it to adjacent pixels. */
void postprocess_bloom_debug_test(void);

void postprocess_deinit(void);

#endif
