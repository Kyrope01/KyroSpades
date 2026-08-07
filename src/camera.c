
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

#include <math.h>
#include <string.h>
#include <float.h>

#include "common.h"
#include "cameracontroller.h"
#include "player.h"
#include "map.h"
#include "matrix.h"
#include "camera.h"
#include "config.h"
#include "window.h"

enum camera_mode camera_mode = CAMERAMODE_SPECTATOR;

float frustum[6][4];
float camera_rot_x = 2.04F, camera_rot_y = 1.79F;
float camera_crosshair_rot_x = 2.04F, camera_crosshair_rot_y = 1.79F;
float camera_muzzle_rot_x = 2.04F, camera_muzzle_rot_y = 1.79F;
float camera_x = 256.0F, camera_y = 60.0F, camera_z = 256.0F;
float camera_vx, camera_vy, camera_vz;
float camera_size = 0.8F;
float camera_height = 0.8F;
float camera_eye_height = 0.0F;
float camera_movement_x = 0.0F, camera_movement_y = 0.0F, camera_movement_z = 0.0F;
float camera_speed = 32.0F;

float camera_fov_scaled(float dt) {
	int render_fpv = (camera_mode == CAMERAMODE_FPS)
		|| ((camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR)
			&& cameracontroller_bodyview_mode);
	int local_id = (camera_mode == CAMERAMODE_FPS) ? local_player_id : cameracontroller_bodyview_player;

	// Validate local_id before any array access to prevent crashes
	if(local_id >= PLAYERS_MAX || local_id < 0) {
		local_id = -1;
	}

	// Calculate target FOV offset based on sprint and crouch state
	// Only apply FOV changes when in first-person view as the local player
	float target_fov_offset = 0.0F;
	if(!settings.disable_dynamic_fov && camera_mode == CAMERAMODE_FPS && local_id >= 0 && local_id < PLAYERS_MAX && players[local_id].alive) {
		if(players[local_id].input.keys.sprint) {
			target_fov_offset += 20.0F;
		}
		if(players[local_id].input.keys.crouch) {
			target_fov_offset -= 10.0F;
		}
	}

	// Smoothly interpolate current FOV offset towards target (zoom in/out effect)
	static float current_fov_offset = 0.0F;
	float lerp_speed = 5.0F * dt; // Adjust this value to control smoothness
	current_fov_offset = current_fov_offset + (target_fov_offset - current_fov_offset) * fminf(lerp_speed, 1.0F);

	float normal_fov = settings.camera_fov + current_fov_offset;
	if(render_fpv && local_id >= 0 && local_id < PLAYERS_MAX
	   && players[local_id].held_item == TOOL_GUN && players[local_id].input.buttons.rmb
	   && (!players[local_id].input.keys.sprint || players[local_id].input.keys.crouch)
	   && players[local_id].alive) {
		float ads_fov = CAMERA_DEFAULT_FOV;
		switch(players[local_id].weapon) {
			case WEAPON_RIFLE: ads_fov = settings.rifle_ads_fov; break;
			case WEAPON_SMG: ads_fov = settings.smg_ads_fov; break;
			case WEAPON_SHOTGUN: ads_fov = settings.shotgun_ads_fov; break;
			default: ads_fov = CAMERA_DEFAULT_FOV; break;
		}
		float target_ads_fov = ads_fov * atan(tan((ads_fov / 180.0F * PI) / 2) / 2.0F) * 2.0F;

		/* Use the same 150 ms smoothstep as the scope PNG. This keeps the
		   optical zoom and image size synchronized instead of snapping the
		   camera FOV while the scope is still growing into place. It also
		   relies on rmb_start representing the initial press (not being reset
		   each held frame; see cameracontroller_fps). */
		if(settings.ads_zoom_animation) {
			float progress = (window_time() - players[local_id].input.buttons.rmb_start) / 0.15F;
			progress = fmaxf(0.0F, fminf(1.0F, progress));
			progress = progress * progress * (3.0F - 2.0F * progress);
			return normal_fov + (target_ads_fov - normal_fov) * progress;
		}
		return target_ads_fov;
	}
	return normal_fov;
}

void camera_vector_from_angles(float yaw, float pitch, float* x, float* y, float* z) {
	*x = sinf(yaw) * sinf(pitch);
	*y = cosf(pitch);
	*z = cosf(yaw) * sinf(pitch);
}

static float camera_angle_delta(float a, float b) {
	float d = a - b;
	while(d > PI) d -= DOUBLEPI;
	while(d < -PI) d += DOUBLEPI;
	return d;
}

static float camera_clamp_angle_around(float center, float value, float range) {
	float d = camera_angle_delta(value, center);
	if(d > range) d = range;
	if(d < -range) d = -range;
	return center + d;
}

void camera_freeaim_reset(void) {
	camera_crosshair_rot_x = camera_muzzle_rot_x = camera_rot_x;
	camera_crosshair_rot_y = camera_muzzle_rot_y = camera_rot_y;
}

void camera_freeaim_update_muzzle(float dt) {
	if(!settings.free_aim || camera_mode != CAMERAMODE_FPS) {
		camera_freeaim_reset();
		return;
	}

	/* TigerSpades-style free aim: the visible crosshair moves immediately,
	   while the actual muzzle/shot direction eases into it quickly. */
	const float tau = 0.039F;
	float a = dt / (tau + dt);
	if(a < 0.0F) a = 0.0F;
	if(a > 1.0F) a = 1.0F;
	camera_muzzle_rot_x += camera_angle_delta(camera_crosshair_rot_x, camera_muzzle_rot_x) * a;
	camera_muzzle_rot_y += (camera_crosshair_rot_y - camera_muzzle_rot_y) * a;
}

void camera_look_delta(float yaw_delta, float pitch_delta) {
	if(settings.free_aim && camera_mode == CAMERAMODE_FPS) {
		camera_crosshair_rot_x -= yaw_delta;
		camera_crosshair_rot_y += pitch_delta;

		float h = settings.free_aim_deadzone_h * PI / 180.0F;
		float v = settings.free_aim_deadzone_v * PI / 180.0F;
		if(h < 0.0F) h = 0.0F;
		if(v < 0.0F) v = 0.0F;

		if(fabsf(camera_angle_delta(camera_crosshair_rot_x, camera_rot_x)) >= h)
			camera_rot_x -= yaw_delta;
		if(fabsf(camera_crosshair_rot_y - camera_rot_y) >= v)
			camera_rot_y += pitch_delta;

		camera_overflow_adjust();
		camera_crosshair_rot_x = camera_clamp_angle_around(camera_rot_x, camera_crosshair_rot_x, h);
		camera_crosshair_rot_y = fmaxf(camera_rot_y - v, fminf(camera_rot_y + v, camera_crosshair_rot_y));
		camera_overflow_adjust();
		return;
	}

	camera_rot_x -= yaw_delta;
	camera_rot_y += pitch_delta;
	camera_overflow_adjust();
	camera_freeaim_reset();
}

void camera_overflow_adjust() {
	/* Pitch is clamped to just under +-90 degrees in EVERY mode now.
	   Spectator used to allow pitch past the pole ("FPV-style free
	   rotation"): the auto-upright view flip at the pole made screen up
	   and screen right swap sides, so mouse-Y and A/D inverted while the
	   view itself stayed upright. Over-the-pole spectator angles are
	   instead reached with camera roll (drone-style flip), which keeps
	   the input mapping consistent in every orientation. */
	if(camera_rot_y < EPSILON) camera_rot_y = EPSILON;
	if(camera_rot_y > 3.14F) camera_rot_y = 3.14F;
	if(camera_crosshair_rot_y < EPSILON) camera_crosshair_rot_y = EPSILON;
	if(camera_crosshair_rot_y > 3.14F) camera_crosshair_rot_y = 3.14F;
	if(camera_muzzle_rot_y < EPSILON) camera_muzzle_rot_y = EPSILON;
	if(camera_muzzle_rot_y > 3.14F) camera_muzzle_rot_y = 3.14F;

	while(camera_rot_x > DOUBLEPI) camera_rot_x -= DOUBLEPI;
	while(camera_rot_x < 0.0F) camera_rot_x += DOUBLEPI;
	while(camera_crosshair_rot_x > DOUBLEPI) camera_crosshair_rot_x -= DOUBLEPI;
	while(camera_crosshair_rot_x < 0.0F) camera_crosshair_rot_x += DOUBLEPI;
	while(camera_muzzle_rot_x > DOUBLEPI) camera_muzzle_rot_x -= DOUBLEPI;
	while(camera_muzzle_rot_x < 0.0F) camera_muzzle_rot_x += DOUBLEPI;
}

void camera_apply() {
	switch(camera_mode) {
		case CAMERAMODE_FPS: cameracontroller_fps_render(); break;
		case CAMERAMODE_BODYVIEW: cameracontroller_bodyview_render(); break;
		case CAMERAMODE_SPECTATOR: cameracontroller_spectator_render(); break;
		case CAMERAMODE_SELECTION: cameracontroller_selection_render(); break;
		case CAMERAMODE_DEATH: cameracontroller_death_render(); break;
	}
}

void camera_update(float dt) {
	switch(camera_mode) {
		case CAMERAMODE_FPS: cameracontroller_fps(dt); break;
		case CAMERAMODE_BODYVIEW: cameracontroller_bodyview(dt); break;
		case CAMERAMODE_SPECTATOR: cameracontroller_spectator(dt); break;
		case CAMERAMODE_SELECTION: cameracontroller_selection(dt); break;
		case CAMERAMODE_DEATH: cameracontroller_death(dt); break;
	}
}

// Reset spectator camera velocity (call when entering spectator mode)
void cameracontroller_reset_spectator_velocity() {
	extern void cameracontroller_reset_spectator_velocity_impl();
	cameracontroller_reset_spectator_velocity_impl();
}

void camera_hit_fromplayer(struct Camera_HitType* hit, int player_id, float range) {
	if(player_id != local_player_id) {
		camera_hit(hit, player_id, players[player_id].physics.eye.x,
				   players[player_id].physics.eye.y + player_height(&players[player_id]),
				   players[player_id].physics.eye.z, players[player_id].orientation.x, players[player_id].orientation.y,
				   players[player_id].orientation.z, range);
	} else {
		float rx, ry, rz;
		if(settings.free_aim)
			camera_vector_from_angles(camera_crosshair_rot_x, camera_crosshair_rot_y, &rx, &ry, &rz);
		else
			camera_vector_from_angles(camera_rot_x, camera_rot_y, &rx, &ry, &rz);
		camera_hit(hit, player_id, players[player_id].physics.eye.x,
				   players[player_id].physics.eye.y + player_height(&players[player_id]),
				   players[player_id].physics.eye.z, rx, ry, rz, range);
	}
}

void camera_hit(struct Camera_HitType* hit, int exclude_player, float x, float y, float z, float ray_x, float ray_y,
				float ray_z, float range) {
	camera_hit_mask(hit, exclude_player, x, y, z, ray_x, ray_y, ray_z, range);
}

void camera_hit_mask(struct Camera_HitType* hit, int exclude_player, float x, float y, float z, float ray_x,
					 float ray_y, float ray_z, float range) {
	Ray dir = (Ray) {
		.origin.coords = {x, y, z},
		.direction.coords = {ray_x, ray_y, ray_z},
	};

	hit->type = CAMERA_HITTYPE_NONE;
	hit->distance = FLT_MAX;

	int* pos = camera_terrain_pickEx(1, x, y, z, ray_x, ray_y, ray_z);
	if(pos != NULL && distance3D(x, y, z, pos[0], pos[1], pos[2]) <= range * range) {
		AABB block = (AABB) {
			.min = {pos[0], pos[1], pos[2]},
			.max = {pos[0] + 1, pos[1] + 1, pos[2] + 1},
		};

		float d;
		if(aabb_intersection_ray(&block, &dir, &d)) {
			hit->type = CAMERA_HITTYPE_BLOCK;
			hit->distance = d;
			hit->x = pos[0];
			hit->y = pos[1];
			hit->z = pos[2];
			hit->xb = pos[3];
			hit->yb = pos[4];
			hit->zb = pos[5];
		}
	}

	for(int i = 0; i < PLAYERS_MAX; i++) {
		float l = distance2D(x, z, players[i].pos.x, players[i].pos.z);
		if(players[i].connected && players[i].alive && l < range * range
		   && (exclude_player < 0 || (exclude_player >= 0 && exclude_player != i))) {
			struct player_intersection intersects = {0};
			player_collision(players + i, &dir, &intersects);

			float d;
			int type = player_intersection_choose(&intersects, &d);
			if(player_intersection_exists(&intersects) && d < hit->distance) {
				hit->type = CAMERA_HITTYPE_PLAYER;
				hit->distance = d;
				hit->x = players[i].pos.x;
				hit->y = players[i].pos.y;
				hit->z = players[i].pos.z;
				hit->player_id = i;
				hit->player_section = type;
			}
		}
	}
}

int* camera_terrain_pick(unsigned char mode) {
	float rx, ry, rz;
	if(settings.free_aim && camera_mode == CAMERAMODE_FPS)
		camera_vector_from_angles(camera_crosshair_rot_x, camera_crosshair_rot_y, &rx, &ry, &rz);
	else
		camera_vector_from_angles(camera_rot_x, camera_rot_y, &rx, &ry, &rz);
	return camera_terrain_pickEx(mode, camera_x, camera_y, camera_z, rx, ry, rz);
}

// kindly borrowed from
// https://stackoverflow.com/questions/16505905/walk-a-line-between-two-points-in-a-3d-voxel-space-visiting-all-cells
// adapted, original code by Wivlaro
int* camera_terrain_pickEx(unsigned char mode, float gx0, float gy0, float gz0, float ray_x, float ray_y, float ray_z) {
	float gx1 = gx0 + ray_x * 128.0F;
	float gy1 = gy0 + ray_y * 128.0F;
	float gz1 = gz0 + ray_z * 128.0F;

	int gx0idx = floor(gx0);
	int gy0idx = floor(gy0);
	int gz0idx = floor(gz0);

	int gx1idx = floor(gx1);
	int gy1idx = floor(gy1);
	int gz1idx = floor(gz1);

	int sx = gx1idx > gx0idx ? 1 : gx1idx < gx0idx ? -1 : 0;
	int sy = gy1idx > gy0idx ? 1 : gy1idx < gy0idx ? -1 : 0;
	int sz = gz1idx > gz0idx ? 1 : gz1idx < gz0idx ? -1 : 0;

	int gx = gx0idx;
	int gy = gy0idx;
	int gz = gz0idx;

	int gxp = gx0idx + (gx1idx > gx0idx ? 1 : 0);
	int gyp = gy0idx + (gy1idx > gy0idx ? 1 : 0);
	int gzp = gz0idx + (gz1idx > gz0idx ? 1 : 0);

	float vx = gx1 == gx0 ? 1 : gx1 - gx0;
	float vy = gy1 == gy0 ? 1 : gy1 - gy0;
	float vz = gz1 == gz0 ? 1 : gz1 - gz0;

	float vxvy = vx * vy;
	float vxvz = vx * vz;
	float vyvz = vy * vz;

	float errx = (gxp - gx0) * vyvz;
	float erry = (gyp - gy0) * vxvz;
	float errz = (gzp - gz0) * vxvy;

	float derrx = sx * vyvz;
	float derry = sy * vxvz;
	float derrz = sz * vxvy;

	int gx_pre = gx, gy_pre = gy, gz_pre = gz;

	static int ret[6];
	ret[0] = ret[1] = ret[2] = 0;
	ret[3] = ret[4] = ret[5] = 0;

	while(1) {
		if(gx >= map_size_x || gx < 0 || gy >= map_size_y || gy < 0 || gz >= map_size_z || gz < 0) {
			return NULL;
		}
		switch(mode) {
			case 0:
				if(!map_isair(gx, gy, gz) && map_isair(gx_pre, gy_pre, gz_pre)) {
					ret[0] = gx_pre;
					ret[1] = gy_pre;
					ret[2] = gz_pre;
					return ret;
				}
				break;
			case 1:
				if(!map_isair(gx, gy, gz)) {
					ret[0] = gx;
					ret[1] = gy;
					ret[2] = gz;
					ret[3] = gx_pre;
					ret[4] = gy_pre;
					ret[5] = gz_pre;
					return ret;
				}
				break;
		}
		gx_pre = gx;
		gy_pre = gy;
		gz_pre = gz;

		if(gx == gx1idx && gy == gy1idx && gz == gz1idx)
			break;

		int xr = (int)fabsf(errx);
		int yr = (int)fabsf(erry);
		int zr = (int)fabsf(errz);

		if(sx != 0 && (sy == 0 || xr < yr) && (sz == 0 || xr < zr)) {
			gx += sx;
			errx += derrx;
		} else if(sy != 0 && (sz == 0 || yr < zr)) {
			gy += sy;
			erry += derry;
		} else if(sz != 0) {
			gz += sz;
			errz += derrz;
		}
	}

	return NULL;
}

void camera_ExtractFrustum() {
	float clip[16];
	float t;

	mat4 mvp;
	matrix_load(mvp, matrix_model);
	matrix_multiply(mvp, matrix_view);
	matrix_multiply(mvp, matrix_projection);
	memcpy(clip, (float*)mvp, 16 * sizeof(float));

	/* Extract the numbers for the RIGHT plane */
	frustum[0][0] = clip[3] - clip[0];
	frustum[0][1] = clip[7] - clip[4];
	frustum[0][2] = clip[11] - clip[8];
	frustum[0][3] = clip[15] - clip[12];

	/* Normalize the result */
	t = sqrt(frustum[0][0] * frustum[0][0] + frustum[0][1] * frustum[0][1] + frustum[0][2] * frustum[0][2]);
	frustum[0][0] /= t;
	frustum[0][1] /= t;
	frustum[0][2] /= t;
	frustum[0][3] /= t;

	/* Extract the numbers for the LEFT plane */
	frustum[1][0] = clip[3] + clip[0];
	frustum[1][1] = clip[7] + clip[4];
	frustum[1][2] = clip[11] + clip[8];
	frustum[1][3] = clip[15] + clip[12];

	/* Normalize the result */
	t = sqrt(frustum[1][0] * frustum[1][0] + frustum[1][1] * frustum[1][1] + frustum[1][2] * frustum[1][2]);
	frustum[1][0] /= t;
	frustum[1][1] /= t;
	frustum[1][2] /= t;
	frustum[1][3] /= t;

	/* Extract the BOTTOM plane */
	frustum[2][0] = clip[3] + clip[1];
	frustum[2][1] = clip[7] + clip[5];
	frustum[2][2] = clip[11] + clip[9];
	frustum[2][3] = clip[15] + clip[13];

	/* Normalize the result */
	t = sqrt(frustum[2][0] * frustum[2][0] + frustum[2][1] * frustum[2][1] + frustum[2][2] * frustum[2][2]);
	frustum[2][0] /= t;
	frustum[2][1] /= t;
	frustum[2][2] /= t;
	frustum[2][3] /= t;

	/* Extract the TOP plane */
	frustum[3][0] = clip[3] - clip[1];
	frustum[3][1] = clip[7] - clip[5];
	frustum[3][2] = clip[11] - clip[9];
	frustum[3][3] = clip[15] - clip[13];

	/* Normalize the result */
	t = sqrt(frustum[3][0] * frustum[3][0] + frustum[3][1] * frustum[3][1] + frustum[3][2] * frustum[3][2]);
	frustum[3][0] /= t;
	frustum[3][1] /= t;
	frustum[3][2] /= t;
	frustum[3][3] /= t;

	/* Extract the FAR plane */
	frustum[4][0] = clip[3] - clip[2];
	frustum[4][1] = clip[7] - clip[6];
	frustum[4][2] = clip[11] - clip[10];
	frustum[4][3] = clip[15] - clip[14];

	/* Normalize the result */
	t = sqrt(frustum[4][0] * frustum[4][0] + frustum[4][1] * frustum[4][1] + frustum[4][2] * frustum[4][2]);
	frustum[4][0] /= t;
	frustum[4][1] /= t;
	frustum[4][2] /= t;
	frustum[4][3] /= t;

	/* Extract the NEAR plane */
	frustum[5][0] = clip[3] + clip[2];
	frustum[5][1] = clip[7] + clip[6];
	frustum[5][2] = clip[11] + clip[10];
	frustum[5][3] = clip[15] + clip[14];

	/* Normalize the result */
	t = sqrt(frustum[5][0] * frustum[5][0] + frustum[5][1] * frustum[5][1] + frustum[5][2] * frustum[5][2]);
	frustum[5][0] /= t;
	frustum[5][1] /= t;
	frustum[5][2] /= t;
	frustum[5][3] /= t;
}

unsigned char camera_PointInFrustum(float x, float y, float z) {
	int p;

	for(p = 0; p < 6; p++)
		if(frustum[p][0] * x + frustum[p][1] * y + frustum[p][2] * z + frustum[p][3] <= 0)
			return 0;

	return 1;
}

int camera_CubeInFrustum(float x, float y, float z, float size, float size_y) {
	int p;
	int c;
	int c2 = 0;

	for(p = 0; p < 6; p++) {
		c = 0;
		if(frustum[p][0] * (x - size) + frustum[p][1] * (y - size) + frustum[p][2] * (z - size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x + size) + frustum[p][1] * (y - size) + frustum[p][2] * (z - size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x - size) + frustum[p][1] * (y + size_y) + frustum[p][2] * (z - size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x + size) + frustum[p][1] * (y + size_y) + frustum[p][2] * (z - size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x - size) + frustum[p][1] * (y) + frustum[p][2] * (z + size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x + size) + frustum[p][1] * (y) + frustum[p][2] * (z + size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x - size) + frustum[p][1] * (y + size_y) + frustum[p][2] * (z + size) + frustum[p][3] > 0)
			c++;
		if(frustum[p][0] * (x + size) + frustum[p][1] * (y + size_y) + frustum[p][2] * (z + size) + frustum[p][3] > 0)
			c++;
		if(c == 0)
			return 0;
		if(c == 8)
			c2++;
	}

	return (c2 == 6) ? 2 : 1;
}
