
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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "common.h"
#include "glx.h"
#include "camera.h"
#include "player.h"
#include "matrix.h"
#include "font.h"
#include "config.h"
#include "window.h"
#include "entitysystem.h"
#include "damagenum.h"

struct entity_system damage_numbers;

struct damagenum_merge_info {
	int player_id;
	int damage;
	int merged;
};

/* merge into an indicator created by another pellet of the same shot
   (e.g. shotgun blasts), like ZeroSpades aggregates per firing session */
static bool damagenum_merge_single(void* obj, void* user) {
	struct DamageNumber* d = (struct DamageNumber*)obj;
	struct damagenum_merge_info* info = (struct damagenum_merge_info*)user;

	if(d->player_id == info->player_id && window_time() - d->last_hit_time < 0.05F) {
		d->damage += info->damage;
		if(!d->crit && d->damage >= 100) {
			d->crit = 1;
			d->vx = 0.0F;
			d->vz = 0.0F;
			d->vy = 2.0F;
		}
		d->fade = d->crit ? 2.0F : 1.5F;
		d->last_hit_time = window_time();
		info->merged = 1;
	}

	return false;
}

void damagenum_add(int player_id, float x, float y, float z, int damage) {
	if(damage <= 0)
		return;

	struct damagenum_merge_info info = {
		.player_id = player_id,
		.damage = damage,
		.merged = 0,
	};
	entitysys_iterate(&damage_numbers, &info, damagenum_merge_single);
	if(info.merged)
		return;

	struct DamageNumber d = (struct DamageNumber) {
		.damage = damage,
		.player_id = player_id,
		.x = x,
		.y = y,
		.z = z,
		.crit = damage >= 100,
		.vy = 2.0F,
		.last_hit_time = window_time(),
	};

	if(!d.crit) {
		d.vx = ((float)rand() / (float)RAND_MAX - 0.5F) * 2.0F;
		d.vz = ((float)rand() / (float)RAND_MAX - 0.5F) * 2.0F;
	}

	d.fade = d.crit ? 2.0F : 1.5F;

	entitysys_add(&damage_numbers, &d);
}

static bool damagenum_update_single(void* obj, void* user) {
	struct DamageNumber* d = (struct DamageNumber*)obj;
	float dt = *(float*)user;

	d->fade -= dt;
	if(d->fade < 0.0F)
		return true;

	d->x += d->vx * dt;
	d->y += d->vy * dt;
	d->z += d->vz * dt;

	return false;
}

void damagenum_update(float dt) {
	entitysys_iterate(&damage_numbers, &dt, damagenum_update_single);
}

static bool damagenum_render_single(void* obj, void* user) {
	struct DamageNumber* d = (struct DamageNumber*)obj;

	float fade = d->fade;
	if(fade > 1.0F)
		fade = 1.0F;

	char str[16];
	snprintf(str, sizeof(str), "%i", d->damage);

	matrix_push(matrix_model);
	matrix_translate(matrix_model, d->x, d->y, d->z);
	matrix_rotate(matrix_model, camera_rot_x / PI * 180.0F + 180.0F, 0.0F, 1.0F, 0.0F);
	matrix_rotate(matrix_model, -camera_rot_y / PI * 180.0F + 90.0F, 1.0F, 0.0F, 0.0F);
	// scale with distance so the number stays readable at long range
	float dist = distance3D(d->x, d->y, d->z, camera_x, camera_y, camera_z);
	float scale = sqrt(dist) / 8.0F;
	if(scale < 1.0F)
		scale = 1.0F;
	float size_mult = settings.damage_number_size;
	if(size_mult < 0.1F) size_mult = 0.1F;
	if(size_mult > 2.0F) size_mult = 2.0F;
	if(size_mult <= 0.0F) size_mult = 0.5F;
	matrix_scale(matrix_model, scale / 92.0F * size_mult, scale / 92.0F * size_mult, scale / 92.0F * size_mult);
	matrix_upload();

	float h = (d->crit ? 48 : 32) * size_mult;

	font_select(FONT_FIXEDSYS);
	glDisable(GL_DEPTH_TEST);

	// black outline
	glColor4f(0.0F, 0.0F, 0.0F, fade);
	font_centered(-2, 0, h, str);
	font_centered(2, 0, h, str);
	font_centered(0, -2, h, str);
	font_centered(0, 2, h, str);

	// red text
	glColor4f(1.0F, 0.0F, 0.0F, fade);
	font_centered(0, 0, h, str);

	glEnable(GL_DEPTH_TEST);
	glColor3f(1.0F, 1.0F, 1.0F);

	matrix_pop(matrix_model);
	matrix_upload();

	return false;
}

void damagenum_render() {
	if(!settings.damage_numbers)
		return;

	entitysys_iterate(&damage_numbers, NULL, damagenum_render_single);
}

static bool damagenum_clear_single(void* obj, void* user) {
	return true;
}

void damagenum_clear() {
	entitysys_iterate(&damage_numbers, NULL, damagenum_clear_single);
}

void damagenum_init() {
	entitysys_create(&damage_numbers, sizeof(struct DamageNumber), PLAYERS_MAX);
}
