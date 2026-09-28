
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

#include <float.h>
#include <stdlib.h>
#include <math.h>

#include "common.h"
#include "map.h"
#include "aabb.h"
#include "tesselator.h"
#include "matrix.h"

void aabb_render(AABB* a) { }

// see: https://tavianator.com/2011/ray_box.html
bool aabb_intersection_ray(AABB* a, Ray* r, float* distance) {
	const float* min_axis = a->min;
	const float* max_axis = a->max;
	const float* origin = r->origin.coords;
	const float* direction = r->direction.coords;
	double tmin = -INFINITY, tmax = INFINITY;
	for(int axis = 0; axis < 3; axis++) {
		if(direction[axis] == 0.0F) {
			/* A parallel ray outside the slab cannot intersect it; avoid 0*inf. */
			if(origin[axis] < min_axis[axis] || origin[axis] > max_axis[axis])
				return false;
			continue;
		}
		double near = (min_axis[axis] - origin[axis]) / (double)direction[axis];
		double far = (max_axis[axis] - origin[axis]) / (double)direction[axis];
		if(near > far) { double tmp = near; near = far; far = tmp; }
		tmin = fmax(tmin, near);
		tmax = fmin(tmax, far);
		if(tmax < tmin) return false;
	}
	if(tmax <= fmax(tmin, 0.0)) return false;
	if(distance)
		*distance = fmax(tmin, 0.0) * len3D(r->direction.x, r->direction.y, r->direction.z);
	return true;
}

void aabb_set_center(AABB* a, float x, float y, float z) {
	float size_x = a->max_x - a->min_x;
	float size_y = a->max_y - a->min_y;
	float size_z = a->max_z - a->min_z;

	a->min_x = x - size_x / 2;
	a->min_y = y - size_y / 2;
	a->min_z = z - size_z / 2;
	a->max_x = x + size_x / 2;
	a->max_y = y + size_y / 2;
	a->max_z = z + size_z / 2;
}

void aabb_set_size(AABB* a, float x, float y, float z) {
	a->max_x = a->min_x + x;
	a->max_y = a->min_y + y;
	a->max_z = a->min_z + z;
}

bool aabb_intersection(AABB* a, AABB* b) {
	return (a->min_x <= b->max_x && b->min_x <= a->max_x) && (a->min_y <= b->max_y && b->min_y <= a->max_y)
		&& (a->min_z <= b->max_z && b->min_z <= a->max_z);
}

bool aabb_intersection_terrain(AABB* a, int miny) {
	AABB terrain_cube;

	int min_x = min(max(floor(a->min_x) - 1, 0), map_size_x);
	int min_y = min(max(floor(a->min_y) - 1, miny), map_size_y);
	int min_z = min(max(floor(a->min_z) - 1, 0), map_size_z);

	int max_x = min(max(ceil(a->max_x) + 1, 0), map_size_x);
	int max_y = min(max(ceil(a->max_y) + 1, 0), map_size_y);
	int max_z = min(max(ceil(a->max_z) + 1, 0), map_size_z);

	for(int x = min_x; x < max_x; x++) {
		for(int z = min_z; z < max_z; z++) {
			for(int y = min_y; y < max_y; y++) {
				if(!map_isair(x, y, z)) {
					terrain_cube.min_x = x;
					terrain_cube.min_y = y;
					terrain_cube.min_z = z;
					terrain_cube.max_x = x + 1;
					terrain_cube.max_y = y + 1;
					terrain_cube.max_z = z + 1;

					if(aabb_intersection(a, &terrain_cube))
						return true;
				}
			}
		}
	}

	return false;
}
