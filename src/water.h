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

/* True whenever the dedicated water surface/wave pass is active. The pass
 * only draws columns that contain water (air at WATER_LEVEL); terrain meshes
 * keep their solid y < WATER_LEVEL blocks regardless of this value.
 * Camera-independent, safe to read on chunk worker threads. */
bool water_surface_enabled(void);
bool water_shader_active(void);

void water_reflection_pass(void);

void water_render(void);

#endif
