
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

// see: https://tavianator.com/2011/ray_box.html
bool aabb_intersection_ray(const AABB* a, const Ray* r, float* distance) {
	const float* min_axis = a->min;
	const float* max_axis = a->max;
	const float* origin = r->origin.coords;
	const float* direction = r->direction.coords;
	double tmin = -INFINITY, tmax = INFINITY;
	bool has_direction = false;
	for(int axis = 0; axis < 3; axis++) {
		if(!isfinite(min_axis[axis]) || !isfinite(max_axis[axis]) || min_axis[axis] > max_axis[axis]
		   || !isfinite(origin[axis]) || !isfinite(direction[axis]))
			return false;
		if(direction[axis] == 0.0F) {
			/* A parallel ray outside the slab cannot intersect it; avoid 0*inf. */
			if(origin[axis] < min_axis[axis] || origin[axis] > max_axis[axis])
				return false;
			continue;
		}
		has_direction = true;
		double near = ((double)min_axis[axis] - (double)origin[axis]) / (double)direction[axis];
		double far = ((double)max_axis[axis] - (double)origin[axis]) / (double)direction[axis];
		if(near > far) { double tmp = near; near = far; far = tmp; }
		tmin = fmax(tmin, near);
		tmax = fmin(tmax, far);
		if(tmax < tmin) return false;
	}
	/* A zero-length interval (a point tangent) is not a hit; a ray
	   travelling along a box face can have a nonzero interval and is a hit. */
	if(!has_direction || tmax <= fmax(tmin, 0.0)) return false;
	if(distance) {
		double dx = direction[0], dy = direction[1], dz = direction[2];
		*distance = (float)(fmax(tmin, 0.0) * sqrt(dx * dx + dy * dy + dz * dz));
	}
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

bool aabb_intersection(const AABB* a, const AABB* b) {
	/* Contacts at a face, edge or corner count as intersections. */
	return (a->min_x <= b->max_x && b->min_x <= a->max_x) && (a->min_y <= b->max_y && b->min_y <= a->max_y)
		&& (a->min_z <= b->max_z && b->min_z <= a->max_z);
}

/* Inclusive box contact means a box starting exactly on an integer boundary
   also touches the voxel immediately below it. Clamp in double before the
   integer conversion, including for huge finite or corrupted coordinates. */
static int aabb_voxel_bound(double value, int limit) {
	if(value <= 0.0) return 0;
	if(value >= (double)limit) return limit;
	return (int)value;
}

bool aabb_intersection_terrain(const AABB* a, int miny) {
	for(int i = 0; i < 3; i++) {
		if(!isfinite(a->min[i]) || !isfinite(a->max[i]) || a->min[i] > a->max[i])
			return false;
	}
	if(map_size_x <= 0 || map_size_y <= 0 || map_size_z <= 0) return false;

	int min_x = aabb_voxel_bound(ceil((double)a->min_x) - 1.0, map_size_x);
	int min_y = aabb_voxel_bound(ceil((double)a->min_y) - 1.0, map_size_y);
	int min_z = aabb_voxel_bound(ceil((double)a->min_z) - 1.0, map_size_z);
	int max_x = aabb_voxel_bound(floor((double)a->max_x) + 1.0, map_size_x);
	int max_y = aabb_voxel_bound(floor((double)a->max_y) + 1.0, map_size_y);
	int max_z = aabb_voxel_bound(floor((double)a->max_z) + 1.0, map_size_z);
	if(miny > min_y) min_y = miny;
	if(min_x >= max_x || min_y >= max_y || min_z >= max_z) return false;

	/* One shared lock gives the entire query a consistent map snapshot and
	   avoids acquiring the map lock once for every candidate voxel. */
	map_read_lock();
	for(int x = min_x; x < max_x; x++) {
		for(int z = min_z; z < max_z; z++) {
			for(int y = min_y; y < max_y; y++) {
				if(!map_isair_nolock(x, y, z)) {
					map_read_unlock();
					return true;
				}
			}
		}
	}
	map_read_unlock();
	return false;
}
