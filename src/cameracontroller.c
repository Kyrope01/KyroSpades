
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
#include "hud.h"
#include "config.h"
#include "demo.h"

int cameracontroller_bodyview_mode = 0;
int cameracontroller_bodyview_player = 0;
int cameracontroller_yclamp = 0;
float cameracontroller_bodyview_zoom = 0.0F;

// Smooth crouch interpolation for local player
static float crouch_offset = 0.0F;
static float target_crouch_offset = 0.0F;
/* Camera-position offset while crouching (crouch_instant mode): the
   collision state changes instantly (server parity), this eases only
   the camera toward the new height. */
static float crouch_cam_offset = 0.0F;

/* Vertical lag applied to the first-person gun on top of the rendered
   camera (viewmodel anchor, see player_render). */
float cameracontroller_gunlag_y = 0.0F;
/* Smoothed vertical velocity for that lag. The legacy trick
   (last_cy = eye.y - vel.y*0.4) consumes the raw 60 Hz tick velocity,
   which is steppy by nature: gravity bumps it every tick, jumping is an
   instant impulse and landing slams it to zero in one tick. With the
   camera now interpolated/smoothed, that quantization showed up as the
   gun jolting relative to the camera exactly at jumps and landings.
   Low-passing the velocity keeps the identical lag magnitude and feel,
   but makes it continuous; tick rate no longer leaks into the views. */
static float gunlag_vel_smooth = 0.0F;

/* Camera shake (view-only, applied after aim rays are computed) */
static float cam_shake_value = 0.0F;
void cameracontroller_add_shake(float intensity) {
	if(!settings.camera_shake)
		return;
	cam_shake_value += intensity;
	if(cam_shake_value > 1.25F)
		cam_shake_value = 1.25F;
}

/* Landing dip (view-only): instant drop, exponential recovery */
static float cam_land_dip = 0.0F;
void cameracontroller_land_dip(float strength) {
	if(!settings.land_dip)
		return;
	cam_land_dip += strength;
	if(cam_land_dip > 0.3F)
		cam_land_dip = 0.3F;
}

/* First-person view bob (view-only) */
static float cam_bob_phase = 0.0F;
static float cam_bob_strength = 0.0F;

/* adaptive correction-blend decay rate (network.c) */
extern float network_correction_rate(void);


float cameracontroller_death_velocity_x, cameracontroller_death_velocity_y, cameracontroller_death_velocity_z;

void cameracontroller_death_init(int player, float x, float y, float z) {
	camera_mode = CAMERAMODE_DEATH;
	float len = len3D(camera_x - x, camera_y - y, camera_z - z);
	cameracontroller_death_velocity_x = (camera_x - x) / len * 3;
	cameracontroller_death_velocity_y = (camera_y - y) / len * 3;
	cameracontroller_death_velocity_z = (camera_z - z) / len * 3;

	cameracontroller_bodyview_player = player;
	cameracontroller_bodyview_zoom = 0.0F;

	/* clear feel-state left over from FPS mode */
	view_offset.x = view_offset.y = view_offset.z = 0.0F;
	cam_shake_value = 0.0F;
	cam_bob_strength = 0.0F;
	crouch_cam_offset = 0.0F;
	gunlag_vel_smooth = 0.0F;
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
	if(local_player_id >= 0 && local_player_id < PLAYERS_MAX) {
		matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + players[local_player_id].orientation.x,
					  camera_y + players[local_player_id].orientation.y, camera_z + players[local_player_id].orientation.z,
					  0.0F, 1.0F, 0.0F);
	}
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
			if(settings.crouch_instant) {
				/* player_uncrouch raised the feet instantly (server parity);
				   ease only the camera up. */
				if(settings.disable_dynamic_fov)
					crouch_cam_offset = 0.0F;
				else
					crouch_cam_offset -= 0.9F;
				player_prev_snap(local_player_id);
			}
		}

		if(settings.crouch_instant) {
			/* Collision state changes instantly with the key -- identical to
			   the server's set_crouch, which shifts the feet by 0.9
			   unconditionally (even mid-air). Only the camera ease remains. */
			if(window_key_down(WINDOW_KEY_CROUCH)
			   && !players[local_player_id].input.keys.crouch) {
				players[local_player_id].pos.y -= 0.9F;
				players[local_player_id].physics.eye.y -= 0.9F;
				last_cy -= 0.9F;
				if(settings.disable_dynamic_fov)
					crouch_cam_offset = 0.0F;
				else
					crouch_cam_offset += 0.9F;
				player_prev_snap(local_player_id);
			}
			if(window_key_down(WINDOW_KEY_CROUCH))
				players[local_player_id].input.keys.crouch = 1;
		} else {
		if(window_key_down(WINDOW_KEY_CROUCH)) {
			// Smooth crouch transition with interpolation
			target_crouch_offset = 0.9F;
		} else {
			target_crouch_offset = 0.0F;
		}

		if(settings.disable_dynamic_fov) {
			crouch_offset = target_crouch_offset;
		} else {
			// Quick smooth crouch transition (~100ms)
			float crouch_lerp_speed = 40.0F * dt;
			crouch_offset = crouch_offset + (target_crouch_offset - crouch_offset) * fminf(crouch_lerp_speed, 1.0F);
		}

		// Apply smooth crouch offset to player position and eye
		if(window_key_down(WINDOW_KEY_CROUCH)) {
			// following if-statement disables smooth crouching on local player
			if(!players[local_player_id].input.keys.crouch && !players[local_player_id].physics.airborne) {
				players[local_player_id].pos.y -= crouch_offset;
				players[local_player_id].physics.eye.y -= crouch_offset;
				last_cy -= crouch_offset;
			}
			players[local_player_id].input.keys.crouch = 1;
		} else {
			// Uncrouching - raise back up smoothly
			if(players[local_player_id].input.keys.crouch && crouch_offset > 0.01F) {
				// Check if we can uncrouch
				if(player_uncrouch(&players[local_player_id])) {
					players[local_player_id].input.keys.crouch = 0;
				}
			}
		}
		}

		/* Crouch and sprint are mutually exclusive. This is especially
		   important for touch/controller movement, where the virtual stick can
		   leave Sprint held while the separate Crouch control is pressed. A
		   stale sprint bit blocks ADS and continuously marks the held item as
		   disabled below, so crouching appeared to make ADS impossible. */
		players[local_player_id].input.keys.sprint
			= window_key_down(WINDOW_KEY_SPRINT) && !players[local_player_id].input.keys.crouch;
		players[local_player_id].input.keys.jump = window_key_down(WINDOW_KEY_SPACE);
		players[local_player_id].input.keys.sneak = window_key_down(WINDOW_KEY_SNEAK);

		if(window_key_down(WINDOW_KEY_SPACE) && !players[local_player_id].physics.airborne) {
			players[local_player_id].physics.jump = 1;
		}
	}

	/* View-only offset decays. The fast loop runs in substeps whose dts
	   sum to the frame time, and exp() composes exactly across them. */
	float decay_corr = expf(-dt * network_correction_rate());
	view_offset.x *= decay_corr;
	view_offset.y *= decay_corr;
	view_offset.z *= decay_corr;
	if(fabsf(view_offset.x) < 0.001F && fabsf(view_offset.y) < 0.001F && fabsf(view_offset.z) < 0.001F) {
		view_offset.x = view_offset.y = view_offset.z = 0.0F;
	}
	cam_land_dip *= expf(-dt * 7.5F);
	cam_shake_value *= expf(-dt * 5.0F);
	if(crouch_cam_offset != 0.0F) {
		crouch_cam_offset += (0.0F - crouch_cam_offset) * fminf(40.0F * dt, 1.0F);
		if(fabsf(crouch_cam_offset) < 0.001F)
			crouch_cam_offset = 0.0F;
	}

	/* Partial-tick interpolation of the 60 Hz physics (Minecraft-style):
	   the camera renders between the previous and the current tick. */
	if(settings.render_interpolation) {
		camera_x = players[local_player_id].prev_eye.x
			+ (players[local_player_id].physics.eye.x - players[local_player_id].prev_eye.x) * physics_tick_alpha;
		camera_y = players[local_player_id].prev_eye.y
			+ (players[local_player_id].physics.eye.y - players[local_player_id].prev_eye.y) * physics_tick_alpha;
		camera_z = players[local_player_id].prev_eye.z
			+ (players[local_player_id].physics.eye.z - players[local_player_id].prev_eye.z) * physics_tick_alpha;
	} else {
		camera_x = players[local_player_id].physics.eye.x;
		camera_y = players[local_player_id].physics.eye.y;
		camera_z = players[local_player_id].physics.eye.z;
	}

	/* Gun lag = the legacy -0.4*vel.y offset... except the engine's
	   VERTICAL axis is z (jump impulse and landing/fall detection both
	   use velocity.z; y is a horizontal map-plane axis). The legacy code
	   lagged a HORIZONTAL axis into the vertical gun offset - authentic
	   historical bug. Lag the true vertical velocity instead:
	   +z is down (falling = positive vel.z), so falling gives a positive
	   offset -> the gun visibly stays up relative to the dropping eye.
	   Smoothed so it never quantizes to the 60 Hz tick. */
	gunlag_vel_smooth += (players[local_player_id].physics.velocity.z - gunlag_vel_smooth)
						 * fminf(dt * 18.0F, 1.0F);
	cameracontroller_gunlag_y = 0.4F * gunlag_vel_smooth;

	if(settings.net_smooth_corrections) {
		/* smoothed server position corrections (rubberband hiding) */
		camera_x += view_offset.x;
		camera_y += view_offset.y;
		camera_z += view_offset.z;
	}
	if(settings.crouch_instant)
		camera_y += crouch_cam_offset;
	if(settings.land_dip)
		camera_y -= cam_land_dip;

	camera_y += player_height(&players[local_player_id]);

	/* View bob phase advance (render-only effect; applied in
	   cameracontroller_fps_render so aim rays stay unaffected) */
	if(settings.view_bob) {
		/* horizontal plane is x,y -- z is the VERTICAL axis here */
		float hsp = hypotf(players[local_player_id].physics.velocity.x,
						   players[local_player_id].physics.velocity.y) * 32.0F;
		float target = (!players[local_player_id].physics.airborne && hsp > 0.4F)
			? fminf(hsp / 8.0F, 1.25F) * 0.8F : 0.0F;
		cam_bob_strength += (target - cam_bob_strength) * fminf(dt * 8.0F, 1.0F);
		if(target > 0.0F)
			/* ~2.8-4.8 Hz: head-bob territory, NOT a buzz (the old rate
			   reached ~8.5 Hz lateral + 17 Hz vertical - vibrated). */
			cam_bob_phase += dt * (2.8F + 2.0F * target);
	} else {
		cam_bob_strength = 0.0F;
	}

	if(players[local_player_id].input.keys.sprint && chat_input_mode == CHAT_NO_INPUT) {
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
		/* In hold-to-ADS mode this block runs every frame. The old code reset
		   rmb_start on every one of those frames while RMB was held, so the
		   scope animation permanently remained at its first (half-size) frame.
		   Timestamp only the actual up->down transition. */
		int was_rmb = players[local_player_id].input.buttons.rmb;
		players[local_player_id].input.buttons.rmb = button_map[1];
		if(button_map[1] && !was_rmb)
			players[local_player_id].input.buttons.rmb_start = window_time();
	}

	if(chat_input_mode != CHAT_NO_INPUT) {
		players[local_player_id].input.keys.packed &= 0b00100000;
		players[local_player_id].input.buttons.packed = 0;
	}

	float smooth_decay = powf(0.7F, dt * 60.0F);
	float smooth_gain = 1.0F - smooth_decay;

	float lx = players[local_player_id].orientation_smooth.x * smooth_decay
		+ (sin(camera_rot_x) * sin(camera_rot_y)) * smooth_gain;
	float ly = players[local_player_id].orientation_smooth.y * smooth_decay
		+ (cos(camera_rot_y)) * smooth_gain;
	float lz = players[local_player_id].orientation_smooth.z * smooth_decay
		+ (cos(camera_rot_x) * sin(camera_rot_y)) * smooth_gain;

	players[local_player_id].orientation_smooth.x = lx;
	players[local_player_id].orientation_smooth.y = ly;
	players[local_player_id].orientation_smooth.z = lz;

	if(settings.raw_aim) {
		/* 1:1 aim: the orientation used for movement, shots and network
		   sync follows the camera exactly (no artificial low-pass filter).
		   The smoothed copy above stays alive for cosmetic uses only
		   (arm sway, third-person model). sin()/cos() are normalized by
		   construction. */
		players[local_player_id].orientation.x = sin(camera_rot_x) * sin(camera_rot_y);
		players[local_player_id].orientation.y = cos(camera_rot_y);
		players[local_player_id].orientation.z = cos(camera_rot_x) * sin(camera_rot_y);
	} else {
		float len = sqrt(lx * lx + ly * ly + lz * lz);
		players[local_player_id].orientation.x = lx / len;
		players[local_player_id].orientation.y = ly / len;
		players[local_player_id].orientation.z = lz / len;
	}

	camera_vx = players[local_player_id].physics.velocity.x;
	camera_vy = players[local_player_id].physics.velocity.y;
	camera_vz = players[local_player_id].physics.velocity.z;
}

void cameracontroller_fps_render() {
	/* These are RENDER-ONLY modifiers: camera_x/y/z and camera_rot_x/y
	   keep their gameplay-exact values (aim rays, block/grenade picks,
	   hit tests are computed from the unmodified globals). */
	float ex = camera_x, ey = camera_y, ez = camera_z;
	float rx = camera_rot_x, ry = camera_rot_y;

	if(settings.view_bob && cam_bob_strength > 0.001F) {
		/* Single-frequency quadrature ellipse (lat = cos, up = sin): one
		   smooth circular sway at walking cadence. The previous version
		   ran the vertical axis at 2x the phase frequency, which turns
		   into visible high-frequency vibration at high frame rates. */
		float lat = cosf(cam_bob_phase) * 0.045F * cam_bob_strength;
		float up = sinf(cam_bob_phase) * 0.032F * cam_bob_strength;
		/* yaw-right vector: derivative of (sin rx, cos rx) is (cos rx, -sin rx) */
		ex += cosf(rx) * lat;
		ez -= sinf(rx) * lat;
		ey += up;
	}

	if(settings.camera_shake && cam_shake_value > 0.0001F) {
		/* smooth sum-of-sines wobble; amplitude mix tuned so a single
		   rifle shot is a readable nudge and close explosions a real
		   wallop (the original pure-quadratic 0.006 was sub-perceptual) */
		float t = game_time();
		float amp = cam_shake_value * cam_shake_value * 0.02F + cam_shake_value * 0.012F;
		rx += (sinf(t * 137.0F) + 0.5F * sinf(t * 311.0F)) * amp;
		ry += (sinf(t * 181.0F + 1.3F) + 0.5F * sinf(t * 271.0F + 0.4F)) * amp * 0.8F;
	}

	matrix_lookAt(matrix_view, ex, ey, ez, ex + sin(rx) * sin(ry), ey + cos(ry), ez + cos(rx) * sin(ry),
				  0.0F, 1.0F, 0.0F);
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

	/* Ease the whole velocity VECTOR toward the target velocity, instead
	   of only scaling the speed and slamming it onto the new input
	   direction every frame (the old code redirected ALL momentum
	   instantly, so strafing while flying forward snapped straight onto
	   the diagonal). Direction changes now swing smoothly: accelerating,
	   braking and turning all use the same rate-limited approach, and it
	   is frame-time scaled so it behaves identically at any frame rate. */
	float target_vel_x = input_x * target_speed;
	float target_vel_y = input_y * target_speed;
	float target_vel_z = input_z * target_speed;

	float dv_x = target_vel_x - spec_vel_x;
	float dv_y = target_vel_y - spec_vel_y;
	float dv_z = target_vel_z - spec_vel_z;
	float dv_len = sqrt(dv_x * dv_x + dv_y * dv_y + dv_z * dv_z);

	if(dv_len > 0.0001F) {
		float rate = (target_speed > 0.0F) ? spec_accel : spec_decel;
		float step = rate * dt;
		if(step > dv_len)
			step = dv_len;
		spec_vel_x += dv_x / dv_len * step;
		spec_vel_y += dv_y / dv_len * step;
		spec_vel_z += dv_z / dv_len * step;
	}

	// Stop completely when drifting with no input and velocity is tiny
	if(target_speed == 0.0F) {
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
		int found = 0;
		for(int k = 0; k < PLAYERS_MAX; k++) {
			// Validate cameracontroller_bodyview_player before accessing players array
			if(cameracontroller_bodyview_player >= PLAYERS_MAX || cameracontroller_bodyview_player < 0) {
				cameracontroller_bodyview_player = 0;
			}
			if(player_can_spectate(&players[cameracontroller_bodyview_player])) {
				found = 1;
				break;
			}
			cameracontroller_bodyview_player = (cameracontroller_bodyview_player + 1) % PLAYERS_MAX;
		}
		// If no valid player found, disable bodyview mode
		if(!found) {
			cameracontroller_bodyview_mode = 0;
			cameracontroller_bodyview_player = 0;
		}
	}

	// Validate cameracontroller_bodyview_player before accessing players array
	if(cameracontroller_bodyview_mode && cameracontroller_bodyview_player >= 0 
	   && cameracontroller_bodyview_player < PLAYERS_MAX 
	   && players[cameracontroller_bodyview_player].alive) {
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
	// Validate cameracontroller_bodyview_player before accessing players array
	if(cameracontroller_bodyview_mode && cameracontroller_bodyview_player >= 0 
	   && cameracontroller_bodyview_player < PLAYERS_MAX 
	   && players[cameracontroller_bodyview_player].alive) {
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
	int found = 0;
	for(int k = 0; k < PLAYERS_MAX; k++) {
		// Validate cameracontroller_bodyview_player before accessing players array
		if(cameracontroller_bodyview_player >= PLAYERS_MAX || cameracontroller_bodyview_player < 0) {
			cameracontroller_bodyview_player = 0;
		}
		if(player_can_spectate(&players[cameracontroller_bodyview_player])) {
			found = 1;
			break;
		}
		cameracontroller_bodyview_player = (cameracontroller_bodyview_player + 1) % PLAYERS_MAX;
	}
	// If no valid player found, disable bodyview mode
	if(!found) {
		cameracontroller_bodyview_mode = 0;
		cameracontroller_bodyview_player = 0;
		return;
	}

	AABB camera = {0};
	aabb_set_size(&camera, 0.4F, 0.4F, 0.4F);

	float k;
	float traverse_lengths[2] = {-1, -1};
	for(k = 0.0F; k < 5.0F; k += 0.05F) {
		// early exit: both forward and backward traverse lengths found
		if(traverse_lengths[0] >= 0 && traverse_lengths[1] >= 0)
			break;
		// Validate cameracontroller_bodyview_player before each access
		if(cameracontroller_bodyview_player >= PLAYERS_MAX || cameracontroller_bodyview_player < 0) {
			break;
		}
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
	// Validate cameracontroller_bodyview_player before accessing players array
	if(cameracontroller_bodyview_player >= 0 && cameracontroller_bodyview_player < PLAYERS_MAX) {
		camera_x = players[cameracontroller_bodyview_player].pos.x
			- sin(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom;
		camera_y = players[cameracontroller_bodyview_player].pos.y - cos(camera_rot_y) * cameracontroller_bodyview_zoom
			+ player_height2(&players[cameracontroller_bodyview_player]);
		camera_z = players[cameracontroller_bodyview_player].pos.z
			- cos(camera_rot_x) * sin(camera_rot_y) * cameracontroller_bodyview_zoom;
		camera_vx = players[cameracontroller_bodyview_player].physics.velocity.x;
		camera_vy = players[cameracontroller_bodyview_player].physics.velocity.y;
		camera_vz = players[cameracontroller_bodyview_player].physics.velocity.z;
	}

	// Validate cameracontroller_bodyview_player before accessing players array
	if(cameracontroller_bodyview_mode && cameracontroller_bodyview_player >= 0 
	   && cameracontroller_bodyview_player < PLAYERS_MAX 
	   && players[cameracontroller_bodyview_player].alive) {
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
	// Validate cameracontroller_bodyview_player before accessing players array
	if(cameracontroller_bodyview_mode && cameracontroller_bodyview_player >= 0 
	   && cameracontroller_bodyview_player < PLAYERS_MAX 
	   && players[cameracontroller_bodyview_player].alive) {
		struct Player* p = &players[cameracontroller_bodyview_player];
		float l = sqrt(distance3D(p->orientation_smooth.x, p->orientation_smooth.y, p->orientation_smooth.z, 0, 0, 0));
		float ox = p->orientation_smooth.x / l;
		float oy = p->orientation_smooth.y / l;
		float oz = p->orientation_smooth.z / l;

		matrix_lookAt(matrix_view, camera_x, camera_y, camera_z, camera_x + ox, camera_y + oy, camera_z + oz, 0.0F,
					  1.0F, 0.0F);
	} else {
		// Validate cameracontroller_bodyview_player before accessing players array
		if(cameracontroller_bodyview_player >= 0 && cameracontroller_bodyview_player < PLAYERS_MAX) {
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
