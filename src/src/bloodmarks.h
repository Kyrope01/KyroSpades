
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

#ifndef BLOODMARKS_H
#define BLOODMARKS_H

#include <stdbool.h>

/* Persistent blood decals left on solid surfaces near a player hit.
   Ported/adapted from ZeroSpades' BloodMarks feature (voxel-based splatter)
   to KyroSpades' immediate-mode tesselator renderer (flat alpha-blended
   quads instead of small voxel models — much cheaper and dependency-free,
   while still giving a convincing "wall/floor blood stain" effect). */

void bloodmarks_init(void);

/* Remove all active marks (e.g. on disconnect/map change). */
void bloodmarks_clear(void);

/* Spatter blood near `x,y,z` flying towards `dirx,diry,dirz` (not required to
   be normalized; its length is used to gauge impact "energy"). `by_local`
   marks whether the shot/hit was dealt by the local player, used only for
   bookkeeping of the eviction budget so a trigger-happy local player cannot
   evict all marks made by other players' hits. */
void bloodmarks_spatter(float x, float y, float z, float dirx, float diry, float dirz, bool by_local);

/* Advance timers / validate that anchor blocks are still solid. */
void bloodmarks_update(float dt);

/* Issue draw calls. Must be called while the 3D scene matrices are active
   (same place as particle_render()/map_damaged_voxels_render()). */
void bloodmarks_render(void);

#endif
