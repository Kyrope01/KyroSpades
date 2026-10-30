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

#ifndef WATER_H
#define WATER_H

#include <stdbool.h>
#include <stddef.h>

#define WATER_LEVEL 1.0F

/* Initialize/release the optional strict-Core water shader and the shared
 * CPU reflection worker. Legacy desktop and GLES keep the established path. */
bool water_init(void);
void water_deinit(void);
bool water_gpu_supported(void);
/* Discard cached topology/colors after a whole-map replacement. */
void water_invalidate(void);
/* Queue an incremental refresh after an individual terrain edit. */
void water_map_changed(void);

/* True whenever the dedicated water surface/wave pass is active. Terrain
 * meshes omit y < WATER_LEVEL blocks only where this pass draws replacement
 * geometry (air at WATER_LEVEL); floors in solid, dry columns remain meshed.
 * Camera-independent, safe to read on chunk worker threads. */
bool water_surface_enabled(void);
bool water_shader_active(void);

void water_reflection_pass(void);

/* Diagnostic: true once a full reflection sweep has been published for the
 * current map revision, i.e. water_render() no longer needs to probe the live
 * map for every cell. Used by the headless benchmark. */
bool water_snapshot_ready(void);

void water_render(void);

/* Diagnostic: quads emitted by the last water mesh rebuild. The headless
 * benchmark prints it so mesh-shape changes show up in the output. */
size_t water_last_quad_count(void);

#endif
