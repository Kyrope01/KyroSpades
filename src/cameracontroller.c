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

#include "window.h"
#include "map.h"
#include "player.h"
#include "camera.h"
#include "matrix.h"
#include "cameracontroller.h"
#include "config.h"

int cameracontroller_bodyview_mode = 0;
int cameracontroller_bodyview_player = 0;
int cameracontroller_yclamp = 0;
float cameracontroller_bodyview_zoom = 0.0F;

float cameracontroller_death_velocity_x, cameracontroller_death_velocity_y, cameracontroller_death_velocity_z;

void cameracontroller_death_init(int player, float x, float y, float z) {
	camera_mode = CAMERAMODE_DEATH;
	float len = len3D(camera_x - x, camera_y - y, camera_z - z);
	cameracontroller_death_velocity_x = (camera_x - x) / len * 3;
	cameracontroller_death_velocity_y = (camera_y - y) / len * 3;
	cameracontroller_death_velocity_z = (camera_z - z) / len * 3;

	cameracontroller_bodyview_player = player;
	cameracontroller_bodyview_zoom = 0.0F;
}

void cameracontroller_death(float dt) {
	AABB box = {0};
	aabb_set_size(&box, camera_size, camera_height, camera_size);
	aabb_set_center(&box, camera_x + cameracontroller_death_velocity_x * dt,
					camera_y + (cameracontroller_death_velocity_y - dt * 32.0F) * dt,
					camera_z + cameracontroller_death_velocity_z * dt);

	if(!aabb_intersection_terrain(&box, 0)) {
		cameracontroller_death_velocity_y -= dt * 32.0F;
		camera_x += cameracontroller_death_velocity_x * dt;
		camera_y += cameracontroller_death_velocity_y * dt;
		camera_z += cameracontroller_death_velocity_z * dt;
	} else {
		cameracontroller_death_velocity_x *= 0.5F;
		cameracontroller_death_velocity_y *= -0.5F;
		cameracontroller_death_velocity_z *= 0.5F;

		if(len3D(cameracontroller_death_velocity_x, cameracontroller_death_velocity_y,
				 cameracontroller_death_velocity_z)
		   < 0.05F) {
			camera_mode = CAMERAMODE_BODYVIEW;
		}
	}
}

void cameracontroller_death_render() {
	matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + players[local_player_id].orientation.x,
				  camera_y + players[local_player_id].orientation.y, camera_z + players[local_player_id].orientation.z,
				  0.0F, 1.0F, 0.0F);
}

float last_cy;
void cameracontroller_fps(float dt) {
	players[local_player_id].connected = 1;
	players[local_player_id].alive = 1;

	int cooldown = 0;
	if(players[local_player_id].held_item == TOOL_GRENADE && local_player_grenades == 0) {
		local_player_lasttool = players[local_player_id].held_item--;
		cooldown = 1;
	}

	if(players[local_player_id].held_item == TOOL_GUN && local_player_ammo + local_player_ammo_reserved == 0) {
		local_player_lasttool = players[local_player_id].held_item--;
		cooldown = 1;
	}

	if(players[local_player_id].held_item == TOOL_BLOCK && local_player_blocks == 0) {
		local_player_lasttool = players[local_player_id].held_item--;
		cooldown = 1;
	}

	if(cooldown) {
		player_on_held_item_change(players + local_player_id);
	}

#ifdef USE_TOUCH
	if(!local_player_ammo) {
		hud_ingame.input_keyboard(WINDOW_KEY_RELOAD, WINDOW_PRESS, 0, 0);
		hud_ingame.input_keyboard(WINDOW_KEY_RELOAD, WINDOW_RELEASE, 0, 0);
	}
#endif

	last_cy = players[local_player_id].physics.eye.y - players[local_player_id].physics.velocity.y * 0.4F;

	if(chat_input_mode == CHAT_NO_INPUT) {
		players[local_player_id].input.keys.up = window_key_down(WINDOW_KEY_UP);
		players[local_player_id].input.keys.down = window_key_down(WINDOW_KEY_DOWN);
		players[local_player_id].input.keys.left = window_key_down(WINDOW_KEY_LEFT);
		players[local_player_id].input.keys.right = window_key_down(WINDOW_KEY_RIGHT);
		if(players[local_player_id].input.keys.crouch && !window_key_down(WINDOW_KEY_CROUCH)
		   && player_uncrouch(&players[local_player_id])) {
			players[local_player_id].input.keys.crouch = 0;
		}

		if(window_key_down(WINDOW_KEY_CROUCH)) {
			// following if-statement disables smooth crouching on local player
			if(!players[local_player_id].input.keys.crouch && !players[local_player_id].physics.airborne) {
				players[local_player_id].pos.y -= 0.9F;
				players[local_player_id].physics.eye.y -= 0.9F;
				last_cy -= 0.9F;
			}
			players[local_player_id].input.keys.crouch = 1;
		}

		players[local_player_id].input.keys.sprint = window_key_down(WINDOW_KEY_SPRINT);
		players[local_player_id].input.keys.jump = window_key_down(WINDOW_KEY_SPACE);
		players[local_player_id].input.keys.sneak = window_key_down(WINDOW_KEY_SNEAK);

		if(window_key_down(WINDOW_KEY_SPACE) && !players[local_player_id].physics.airborne) {
			players[local_player_id].physics.jump = 1;
		}
	}

	camera_x = players[local_player_id].physics.eye.x;
	camera_y = players[local_player_id].physics.eye.y + player_height(&players[local_player_id]);
	camera_z = players[local_player_id].physics.eye.z;

	if(window_key_down(WINDOW_KEY_SPRINT) && chat_input_mode == CHAT_NO_INPUT) {
		players[local_player_id].item_disabled = window_time();
	} else {
		if(window_time() - players[local_player_id].item_disabled < 0.4F && !players[local_player_id].items_show) {
			// players[local_player_id].items_show_start = window_time();
			// players[local_player_id].items_show = 1;
		}
	}

	players[local_player_id].input.buttons.lmb = button_map[0];

	if(players[local_player_id].held_item != TOOL_GUN
	   || (settings.hold_down_sights && !players[local_player_id].items_show)) {
		players[local_player_id].input.buttons.rmb = button_map[1];
		if(button_map[1]) {
			players[local_player_id].input.buttons.rmb_start = window_time();
		}
	}

	if(chat_input_mode != CHAT_NO_INPUT) {
		players[local_player_id].input.keys.packed &= 0b00100000;
		players[local_player_id].input.buttons.packed = 0;
	}

	float lx = players[local_player_id].orientation_smooth.x * pow(0.7F, dt * 60.0F)
		+ (sin(camera_rot_x) * sin(camera_rot_y)) * (1.0F - pow(0.7F, dt * 60.0F));
	float ly = players[local_player_id].orientation_smooth.y * pow(0.7F, dt * 60.0F)
		+ (cos(camera_rot_y)) * (1.0F - pow(0.7F, dt * 60.0F));
	float lz = players[local_player_id].orientation_smooth.z * pow(0.7F, dt * 60.0F)
		+ (cos(camera_rot_x) * sin(camera_rot_y)) * (1.0F - pow(0.7F, dt * 60.0F));

	players[local_player_id].orientation_smooth.x = lx;
	players[local_player_id].orientation_smooth.y = ly;
	players[local_player_id].orientation_smooth.z = lz;

	float len = sqrt(lx * lx + ly * ly + lz * lz);
	players[local_player_id].orientation.x = lx / len;
	players[local_player_id].orientation.y = ly / len;
	players[local_player_id].orientation.z = lz / len;

	camera_vx = players[local_player_id].physics.velocity.x;
	camera_vy = players[local_player_id].physics.velocity.y;
	camera_vz = players[local_player_id].physics.velocity.z;
}

void cameracontroller_fps_render() {
	matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + sin(camera_rot_x) * sin(camera_rot_y),
				  camera_y + cos(camera_rot_y), camera_z + cos(camera_rot_x) * sin(camera_rot_y), 0.0F, 1.0F, 0.0F);
}

// Spectator camera velocity with smooth acceleration/deceleration
static float spec_vel_x = 0.0F, spec_vel_y = 0.0F, spec_vel_z = 0.0F;

// Spectator camera roll angle
static float camera_roll = 0.0F;

void cameracontroller_reset_spectator_velocity_impl() {
	spec_vel_x = 0.0F;
	spec_vel_y = 0.0F;
	spec_vel_z = 0.0F;
	camera_roll = 0.0F;
}

float cameracontroller_get_roll(void) {
	return camera_roll;
}

void cameracontroller_spectator(float dt) {
	AABB camera = {0};
	aabb_set_size(&camera, camera_size, camera_height, camera_size);
	
	// Use setting for accel/decel rates (with sensible defaults if not set)
	float spec_accel = settings.spectator_acceleration > 0.0F ? settings.spectator_acceleration : 80.0F;
	float spec_decel = spec_accel * 0.75F;  // Deceleration is 75% of acceleration
	float spec_damping = 0.9F;              // Velocity damping factor
	aabb_set_center(&camera, camera_x, camera_y - camera_eye_height, camera_z);

	float input_x = 0.0F, input_y = 0.0F, input_z = 0.0F;

	if(chat_input_mode == CHAT_NO_INPUT) {
		// Calculate forward direction vector from yaw and pitch
		float forward_x = sin(camera_rot_x) * sin(camera_rot_y);
		float forward_y = cos(camera_rot_y);
		float forward_z = cos(camera_rot_x) * sin(camera_rot_y);
		
		// Calculate right vector (perpendicular to forward and world up)
		float right_x = sin(camera_rot_x + 1.57079632679F); // sin(yaw + 90°)
		float right_y = 0.0F;
		float right_z = cos(camera_rot_x + 1.57079632679F); // cos(yaw + 90°)
		
		// Calculate base up vector (perpendicular to forward and right)
		float up_x = -forward_x * forward_y;
		float up_y = 1.0F - forward_y * forward_y;
		float up_z = -forward_z * forward_y;
		
		// Normalize the base up vector
		float up_len = sqrt(up_x * up_x + up_y * up_y + up_z * up_z);
		if(up_len > 0.0001F) {
			up_x /= up_len;
			up_y /= up_len;
			up_z /= up_len;
		} else {
			// Forward is pointing straight up or down
			up_x = 0.0F;
			up_y = 0.0F;
			up_z = 1.0F;
		}
		
		// Apply roll: rotate up and right vectors around forward axis
		float cos_roll = cos(camera_roll);
		float sin_roll = sin(camera_roll);
		
		// Rotated right = right * cos(roll) - up * sin(roll)
		// Rotated up = up * cos(roll) + right * sin(roll)
		float rolled_right_x = right_x * cos_roll - up_x * sin_roll;
		float rolled_right_y = right_y * cos_roll - up_y * sin_roll;
		float rolled_right_z = right_z * cos_roll - up_z * sin_roll;
		
		float rolled_up_x = up_x * cos_roll + right_x * sin_roll;
		float rolled_up_y = up_y * cos_roll + right_y * sin_roll;
		float rolled_up_z = up_z * cos_roll + right_z * sin_roll;
		
		// Now use rolled vectors for movement input (FPV-style)
		if(window_key_down(WINDOW_KEY_UP)) {
			input_x += forward_x;
			input_y += forward_y;
			input_z += forward_z;
		} else {
			if(window_key_down(WINDOW_KEY_DOWN)) {
				input_x -= forward_x;
				input_y -= forward_y;
				input_z -= forward_z;
			}
		}

		if(window_key_down(WINDOW_KEY_LEFT)) {
			input_x += rolled_right_x;
			input_y += rolled_right_y;
			input_z += rolled_right_z;
		} else {
			if(window_key_down(WINDOW_KEY_RIGHT)) {
				input_x -= rolled_right_x;
				input_y -= rolled_right_y;
				input_z -= rolled_right_z;
			}
		}

		if(window_key_down(WINDOW_KEY_SPACE)) {
			input_x += rolled_up_x;
			input_y += rolled_up_y;
			input_z += rolled_up_z;
		} else {
			if(window_key_down(WINDOW_KEY_CROUCH)) {
				input_x -= rolled_up_x;
				input_y -= rolled_up_y;
				input_z -= rolled_up_z;
			}
		}

		// Handle camera roll input
		float roll_speed = 2.0F; // radians per second
		if(window_key_down(WINDOW_KEY_ROLL_CW)) {
			camera_roll -= roll_speed * dt;
		}
		if(window_key_down(WINDOW_KEY_ROLL_CCW)) {
			camera_roll += roll_speed * dt;
		}
	}

	// Normalize input direction
	float input_len = sqrt(input_x * input_x + input_y * input_y + input_z * input_z);
	
	// Calculate target velocity based on input
	float target_speed = 0.0F;
	if(input_len > 0.0F) {
		target_speed = camera_speed * settings.spectator_speed;
		input_x /= input_len;
		input_y /= input_len;
		input_z /= input_len;
	}

	// Smoothly accelerate/decelerate towards target velocity
	if(target_speed > 0.0F) {
		// Accelerate towards target velocity
		float current_speed = sqrt(spec_vel_x * spec_vel_x + spec_vel_y * spec_vel_y + spec_vel_z * spec_vel_z);
		float speed_diff = target_speed - current_speed;
		
		if(speed_diff > 0.0F) {
			// Accelerating
			float accel = spec_accel * dt;
			if(accel > speed_diff) accel = speed_diff;
			current_speed += accel;
		} else {
			// Decelerating (when changing direction)
			float decel = spec_decel * dt;
			if(decel > -speed_diff) decel = -speed_diff;
			current_speed += decel;
		}
		
		// Apply new speed in the input direction
		spec_vel_x = input_x * current_speed;
		spec_vel_y = input_y * current_speed;
		spec_vel_z = input_z * current_speed;
	} else {
		// No input - apply damping for smooth deceleration
		spec_vel_x *= spec_damping;
		spec_vel_y *= spec_damping;
		spec_vel_z *= spec_damping;
		
		// Stop completely when velocity is very small
		if(fabs(spec_vel_x) < 0.01F) spec_vel_x = 0.0F;
		if(fabs(spec_vel_y) < 0.01F) spec_vel_y = 0.0F;
		if(fabs(spec_vel_z) < 0.01F) spec_vel_z = 0.0F;
	}

	// Apply velocity to position with collision detection
	camera_movement_x = spec_vel_x * dt;
	camera_movement_y = spec_vel_y * dt;
	camera_movement_z = spec_vel_z * dt;

	aabb_set_center(&camera, camera_x + camera_movement_x, camera_y - camera_eye_height, camera_z);

	if(camera_x + camera_movement_x < 0 || camera_x + camera_movement_x > map_size_x
	   || aabb_intersection_terrain(&camera, 0)) {
		camera_movement_x = 0.0F;
		spec_vel_x = 0.0F;
	}

	aabb_set_center(&camera, camera_x + camera_movement_x, camera_y + camera_movement_y - camera_eye_height, camera_z);
	if(camera_y + camera_movement_y < 0 || aabb_intersection_terrain(&camera, 0)) {
		camera_movement_y = 0.0F;
		spec_vel_y = 0.0F;
	}

	aabb_set_center(&camera, camera_x + camera_movement_x, camera_y + camera_movement_y - camera_eye_height,
					camera_z + camera_movement_z);
	if(camera_z + camera_movement_z < 0 || camera_z + camera_movement_z > map_size_z
	   || aabb_intersection_terrain(&camera, 0)) {
		camera_movement_z = 0.0F;
		spec_vel_z = 0.0F;
	}

	if(cameracontroller_bodyview_mode) {
		// check if we cant spectate the player anymore
		for(int k = 0; k < PLAYERS_MAX * 2;
			k++) { // a while(1) loop caused it to get stuck on map change when playing on babel
			if(player_can_spectate(&players[cameracontroller_bodyview_player]))
				break;
			cameracontroller_bodyview_player = (cameracontroller_bodyview_player + 1) % PLAYERS_MAX;
		}
	}

	if(cameracontroller_bodyview_mode && players[cameracontroller_bodyview_player].alive) {
		struct Player* p = &players[cameracontroller_bodyview_player];
		camera_x = p->physics.eye.x;
		camera_y = p->physics.eye.y + player_height(p);
		camera_z = p->physics.eye.z;

		camera_vx = p->physics.velocity.x;
		camera_vy = p->physics.velocity.y;
		camera_vz = p->physics.velocity.z;
		
		// Reset spectator velocity when in bodyview mode
		spec_vel_x = 0.0F;
		spec_vel_y = 0.0F;
		spec_vel_z = 0.0F;
	} else {
		camera_x += camera_movement_x;
		camera_y += camera_movement_y;
		camera_z += camera_movement_z;
		camera_vx = camera_movement_x;
		camera_vy = camera_movement_y;
		camera_vz = camera_movement_z;
	}
}

void cameracontroller_spectator_render() {
	if(cameracontroller_bodyview_mode && players[cameracontroller_bodyview_player].alive) {
		struct Player* p = &players[cameracontroller_bodyview_player];
		float l = len3D(p->orientation_smooth.x, p->orientation_smooth.y, p->orientation_smooth.z);
		float ox = p->orientation_smooth.x / l;
		float oy = p->orientation_smooth.y / l;
		float oz = p->orientation_smooth.z / l;

		matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + ox, camera_y + oy, camera_z + oz, 0.0F,
					  1.0F, 0.0F);
	} else {
		// Calculate forward direction from yaw and pitch
		float forward_x = sin(camera_rot_x) * sin(camera_rot_y);
		float forward_y = cos(camera_rot_y);
		float forward_z = cos(camera_rot_x) * sin(camera_rot_y);
		
		// Calculate right vector (perpendicular to forward and world up)
		float right_x = sin(camera_rot_x + 1.57079632679F); // sin(yaw + 90°)
		float right_y = 0.0F;
		float right_z = cos(camera_rot_x + 1.57079632679F); // cos(yaw + 90°)
		
		// Calculate up vector with roll applied (FPV-style roll around forward axis)
		// Start with world up projected perpendicular to forward
		float up_x = -forward_x * forward_y;
		float up_y = 1.0F - forward_y * forward_y;
		float up_z = -forward_z * forward_y;
		
		// Normalize the base up vector
		float up_len = sqrt(up_x * up_x + up_y * up_y + up_z * up_z);
		if(up_len > 0.0001F) {
			up_x /= up_len;
			up_y /= up_len;
			up_z /= up_len;
		} else {
			// Forward is pointing straight up or down, use alternative up
			up_x = 0.0F;
			up_y = 0.0F;
			up_z = 1.0F;
		}
		
		// Apply roll: rotate up vector around forward axis
		// Using Rodrigues' rotation formula components
		float cos_roll = cos(camera_roll);
		float sin_roll = sin(camera_roll);
		
		// Rotated up = up * cos(roll) + (forward × up) * sin(roll) + forward * (forward · up) * (1 - cos(roll))
		// Since forward · up = 0 (they're perpendicular), the last term is zero
		// forward × up = right (by construction)
		float rolled_up_x = up_x * cos_roll + right_x * sin_roll;
		float rolled_up_y = up_y * cos_roll + right_y * sin_roll;
		float rolled_up_z = up_z * cos_roll + right_z * sin_roll;
		
		// Use the rolled up vector in lookAt
		matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, 
					  camera_x + forward_x, camera_y + forward_y, camera_z + forward_z, 
					  rolled_up_x, rolled_up_y, rolled_up_z);
	}
}

void cameracontroller_bodyview(float dt) {
	// check if we cant spectate the player anymore
	for(int k = 0; k < PLAYERS_MAX * 2;
		k++) { // a while(1) loop caused it to get stuck on map change when playing on babel
		if(player_can_spectate(&players[cameracontroller_bodyview_player]))
			break;
		cameracontroller_bodyview_player = (cameracontroller_bodyview_player + 1) % PLAYERS_MAX;
	}

	AABB camera = {0};
	aabb_set_size(&camera, 0.4F, 0.4F, 0.4F);

	float k;
	float traverse_lengths[2] = {-1, -1};
	for(k = 0.0F; k < 5.0F; k += 0.05F) {
		aabb_set_center(&camera,
						players[cameracontroller_bodyview_player].pos.x - sin(camera_rot_x) * sin(camera_rot_y) * k,
						players[cameracontroller_bodyview_player].pos.y - cos(camera_rot_y) * k
							+ player_height2(&players[cameracontroller_bodyview_player]),
						players[cameracontroller_bodyview_player].pos.z - cos(camera_rot_x) * sin(camera_rot_y) * k);
		if(aabb_intersection_terrain(&camera, 0) && traverse_lengths[0] < 0) {
			traverse_lengths[0] = fmax(k - 0.1F, 0);
		}
		aabb_set_center(&camera,
						players[cameracontroller_bodyview_player].pos.x + sin(camera_rot_x) * sin(camera_rot_y) * k,
						players[cameracontroller_bodyview_player].pos.y + cos(camera_rot_y) * k
							+ player_height2(&players[cameracontroller_bodyview_player]),
						players[cameracontroller_bodyview_player].pos.z + cos(camera_rot_x) * sin(camera_rot_y) * k);
		if(!aabb_intersection_terrain(&camera, 0) && traverse_lengths[1] < 0) {
			traverse_lengths[1] = fmax(k - 0.1F, 0);
		}
	}
	if(traverse_lengths[0] < 0)
		traverse_lengths[0] = 5.0F;
	if(traverse_lengths[1] < 0)
		traverse_lengths[1] = 5.0F;

	float tmp = (traverse_lengths[0] <= 0) ? (-traverse_lengths[1]) : traverse_lengths[0];

	cameracontroller_bodyview_zoom
		= (tmp < cameracontroller_bodyview_zoom) ? tmp : fmin(tmp, cameracontroller_bodyview_zoom + dt * 8.0F);

	// this is needed to determine which chunks need/can be rendered and for sound, minimap etc...
	camera_x = players[cameracontroller_bodyview_player].pos.x
		- sin(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom;
	camera_y = players[cameracontroller_bodyview_player].pos.y - cos(camera_rot_y) * cameracontroller_bodyview_zoom
		+ player_height2(&players[cameracontroller_bodyview_player]);
	camera_z = players[cameracontroller_bodyview_player].pos.z
		- cos(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom;
	camera_vx = players[cameracontroller_bodyview_player].physics.velocity.x;
	camera_vy = players[cameracontroller_bodyview_player].physics.velocity.y;
	camera_vz = players[cameracontroller_bodyview_player].physics.velocity.z;

	if(cameracontroller_bodyview_mode && players[cameracontroller_bodyview_player].alive) {
		struct Player* p = &players[cameracontroller_bodyview_player];
		camera_x = p->physics.eye.x;
		camera_y = p->physics.eye.y + player_height(p);
		camera_z = p->physics.eye.z;

		camera_vx = p->physics.velocity.x;
		camera_vy = p->physics.velocity.y;
		camera_vz = p->physics.velocity.z;
	}
}

void cameracontroller_bodyview_render() {
	if(cameracontroller_bodyview_mode && players[cameracontroller_bodyview_player].alive) {
		struct Player* p = &players[cameracontroller_bodyview_player];
		float l = sqrt(distance3D(p->orientation_smooth.x, p->orientation_smooth.y, p->orientation_smooth.z, 0, 0, 0));
		float ox = p->orientation_smooth.x / l;
		float oy = p->orientation_smooth.y / l;
		float oz = p->orientation_smooth.z / l;

		matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + ox, camera_y + oy, camera_z + oz, 0.0F,
					  1.0F, 0.0F);
	} else {
		matrix_lookAt(matrix_view,
					  players[cameracontroller_bodyview_player].pos.x
						  - sin(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom,
					  players[cameracontroller_bodyview_player].pos.y
						  - cos(camera_rot_y) * cameracontroller_bodyview_zoom
						  + player_height2(&players[cameracontroller_bodyview_player]),
					  players[cameracontroller_bodyview_player].pos.z
						  - cos(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom,
					  players[cameracontroller_bodyview_player].pos.x,
					  players[cameracontroller_bodyview_player].pos.y
						  + player_height2(&players[cameracontroller_bodyview_player]),
					  players[cameracontroller_bodyview_player].pos.z, 0.0F, 1.0F, 0.0F);
	}
}

void cameracontroller_selection(float dt) {
	camera_x = 256.0F;
	camera_y = 79.0F;
	camera_z = 256.0F;
	camera_vx = 0.0F;
	camera_vy = 0.0F;
	camera_vz = 0.0F;

	matrix_rotate(matrix_view, 90.0F, 1.0F, 0.0F, 0.0F);
	matrix_translate(matrix_view, -camera_x, -camera_y, -camera_z);
}

void cameracontroller_selection_render() {
	matrix_rotate(matrix_view, 90.0F, 1.0F, 0.0F, 0.0F);
	matrix_translate(matrix_view, -camera_x, -camera_y, -camera_z);
}
