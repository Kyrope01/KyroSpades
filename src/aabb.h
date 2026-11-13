
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

#ifndef AABB_H
#define AABB_H

#include <stdbool.h>

typedef struct {
	union {
		struct {
			float min_x, min_y, min_z;
		};
		float min[3];
	};
	union {
		struct {
			float max_x, max_y, max_z;
		};
		float max[3];
	};
} AABB;

typedef struct {
	union {
		struct {
			float x, y, z;
		};
		float coords[3];
	} origin;
	union {
		struct {
			float x, y, z;
		};
		float coords[3];
	} direction;
} Ray;

/* Box/box intersections include touching faces, edges and corners. */
bool aabb_intersection(const AABB* a, const AABB* b);
/* Distance is in world units even for non-unit directions. Zero directions
   and non-finite or inverted inputs do not intersect. */
bool aabb_intersection_ray(const AABB* a, const Ray* r, float* distance);
/* Only in-map voxels with y >= max(miny, 0) are considered. */
bool aabb_intersection_terrain(const AABB* a, int miny);
void aabb_set_size(AABB* a, float x, float y, float z);
void aabb_set_center(AABB* a, float x, float y, float z);

#endif
