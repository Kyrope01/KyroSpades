
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

#ifndef DAMAGENUMBERS_H
#define DAMAGENUMBERS_H

/* Floating damage numbers shown above a hit player when the local player
   damages them, ported/adapted from ZeroSpades' cg_damageIndicators feature.
   The estimated damage is client-side (weapon table lookup), matching what
   ZeroSpades does -- the server remains authoritative for actual HP. */

void damagenumbers_init(void);
void damagenumbers_clear(void);

/* Register that the local player just dealt `damage` HP worth of hit to
   `victim_id` (a player index) at world position x,y,z (typically the hit
   voxel/eye position). Repeated hits on the same victim within a short
   window are merged into a single, growing indicator (matches ZeroSpades). */
void damagenumbers_add(int victim_id, int damage, float x, float y, float z);

void damagenumbers_update(float dt);

/* Snapshot the current world-space view/projection matrices (matrix_view /
   matrix_projection from matrix.h) for later use by damagenumbers_render().
   Must be called exactly once per frame, right after the game camera's
   matrices are finalized for the 3D scene and before anything else
   (weapon view-model rendering, the HUD's ortho projection, etc.)
   overwrites those globals. */
void damagenumbers_capture_camera(void);

/* Issue 2D draw calls. Must be called during HUD/2D rendering, after the
   ortho projection matrix for the HUD is active (same place other floating
   HUD text is drawn). */
void damagenumbers_render(void);

#endif
