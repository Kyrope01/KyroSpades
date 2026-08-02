
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

#include <math.h>
#include <string.h>
#include <stdio.h>

#include <cglm/call.h>

#include "common.h"
#include "config.h"
#include "matrix.h"
#include "font.h"
#include "window.h"
#include "glx.h"
#include "damagenumbers.h"

/* Floating damage numbers, adapted from ZeroSpades' cg_damageIndicators.
   Multiple hits on the same target landed within a short time window are
   merged into a single, growing number (so a shotgun blast or an SMG burst
   shows one cumulative total rather than a wall of overlapping digits). */

#define DAMAGENUMBERS_MAX 32
#define DAMAGENUMBERS_MERGE_WINDOW 0.12F
#define DAMAGENUMBERS_CRIT_THRESHOLD 100
#define DAMAGENUMBERS_FONT_SIZE 14.0F
#define DAMAGENUMBERS_FONT_SIZE_CRIT 20.0F

struct damage_indicator {
	bool active;
	int victim_id;
	int damage;
	bool crit;
	float x, y, z;
	float vx, vy, vz;
	float fade;     /* seconds remaining before fully gone */
	float max_fade; /* the fade value this indicator started at (for alpha ramp) */
	float age;      /* seconds since last (re)merge, used for the merge window */
	float pulse_time;
};

static struct damage_indicator indicators[DAMAGENUMBERS_MAX];

/* Cached once per frame via damagenumbers_capture_camera(): the world-space
   view/projection matrices at the moment the 3D scene camera was finalized.
   By the time the HUD's 2D/ortho pass runs (where we actually draw the
   numbers), matrix_view/matrix_projection have long since been overwritten
   for other purposes, so we cannot read the live globals there. */
static mat4 cached_view;
static mat4 cached_projection;
static bool cached_valid = false;

/* ms_rand() (declared in common.h) returns a 15-bit value (0..32767). */
static float damagenumbers_frand(void) {
	return (float)ms_rand() / 32767.0F;
}

void damagenumbers_init(void) {
	memset(indicators, 0, sizeof(indicators));
	cached_valid = false;
}

void damagenumbers_clear(void) {
	for(int k = 0; k < DAMAGENUMBERS_MAX; k++)
		indicators[k].active = false;
}

static struct damage_indicator* damagenumbers_find_recent(int victim_id) {
	for(int k = 0; k < DAMAGENUMBERS_MAX; k++) {
		if(indicators[k].active && indicators[k].victim_id == victim_id
		   && indicators[k].age < DAMAGENUMBERS_MERGE_WINDOW)
			return &indicators[k];
	}
	return NULL;
}

static struct damage_indicator* damagenumbers_alloc_slot(void) {
	for(int k = 0; k < DAMAGENUMBERS_MAX; k++) {
		if(!indicators[k].active)
			return &indicators[k];
	}

	int oldest = 0;
	float oldest_fade = indicators[0].fade;
	for(int k = 1; k < DAMAGENUMBERS_MAX; k++) {
		if(indicators[k].fade < oldest_fade) {
			oldest_fade = indicators[k].fade;
			oldest = k;
		}
	}
	return &indicators[oldest];
}

void damagenumbers_add(int victim_id, int damage, float x, float y, float z) {
	if(!settings.damage_numbers || damage <= 0)
		return;

	struct damage_indicator* ind = damagenumbers_find_recent(victim_id);
	if(ind) {
		ind->damage += damage;
		ind->age = 0.0F;
		bool was_crit = ind->crit;
		ind->crit = ind->damage >= DAMAGENUMBERS_CRIT_THRESHOLD;
		ind->max_fade = ind->crit ? 2.0F : 1.5F;
		ind->fade = ind->max_fade;
		if(ind->crit && !was_crit) {
			/* Just crossed into "crit" territory: stop horizontal drift,
			   go straight up, and start the pulse animation fresh. */
			ind->vx = 0.0F;
			ind->vz = 0.0F;
			ind->vy = 1.2F;
			ind->pulse_time = 0.0F;
		}
		return;
	}

	struct damage_indicator* slot = damagenumbers_alloc_slot();
	slot->active = true;
	slot->victim_id = victim_id;
	slot->damage = damage;
	slot->crit = damage >= DAMAGENUMBERS_CRIT_THRESHOLD;
	slot->x = x;
	slot->y = y + 0.4F; /* nudge above the hit point so it clears the model */
	slot->z = z;
	slot->age = 0.0F;
	slot->pulse_time = 0.0F;

	if(slot->crit) {
		slot->vx = 0.0F;
		slot->vz = 0.0F;
		slot->vy = 1.2F;
	} else {
		slot->vx = (damagenumbers_frand() - 0.5F) * 1.2F;
		slot->vz = (damagenumbers_frand() - 0.5F) * 1.2F;
		slot->vy = 1.2F;
	}

	slot->max_fade = slot->crit ? 2.0F : 1.5F;
	slot->fade = slot->max_fade;
}

void damagenumbers_update(float dt) {
	if(!settings.damage_numbers) {
		damagenumbers_clear();
		return;
	}

	for(int k = 0; k < DAMAGENUMBERS_MAX; k++) {
		struct damage_indicator* ind = &indicators[k];
		if(!ind->active)
			continue;

		ind->age += dt;
		ind->pulse_time += dt;
		ind->fade -= dt;
		if(ind->fade <= 0.0F) {
			ind->active = false;
			continue;
		}

		ind->x += ind->vx * dt;
		ind->y += ind->vy * dt;
		ind->z += ind->vz * dt;
	}
}

/* Projects a world point to 2D screen coordinates in this engine's HUD space
   (origin bottom-left, +Y up -- the same convention used by the main ortho
   HUD matrix set up in display()). Returns false if the point is behind the
   camera and therefore should not be drawn. */
static bool damagenumbers_project(float wx, float wy, float wz, float* sx, float* sy) {
	if(!cached_valid)
		return false;

	mat4 mvp;
	glm_mat4_mul(cached_projection, cached_view, mvp);

	vec4 clip = {wx, wy, wz, 1.0F};
	vec4 out;
	glm_mat4_mulv(mvp, clip, out);

	if(out[3] <= 0.0001F)
		return false;

	float ndc_x = out[0] / out[3];
	float ndc_y = out[1] / out[3];

	*sx = (ndc_x * 0.5F + 0.5F) * (float)settings.window_width;
	*sy = (ndc_y * 0.5F + 0.5F) * (float)settings.window_height;
	return true;
}

void damagenumbers_capture_camera(void) {
	glm_mat4_copy(matrix_view, cached_view);
	glm_mat4_copy(matrix_projection, cached_projection);
	cached_valid = true;
}

void damagenumbers_render(void) {
	if(!settings.damage_numbers)
		return;

	font_select(FONT_FIXEDSYS);

	for(int k = 0; k < DAMAGENUMBERS_MAX; k++) {
		struct damage_indicator* ind = &indicators[k];
		if(!ind->active)
			continue;

		float sx, sy;
		if(!damagenumbers_project(ind->x, ind->y, ind->z, &sx, &sy))
			continue;

		/* Cheap off-screen cull with generous margin (text is small, no
		   need to be pixel-exact here). */
		if(sx < -64.0F || sx > (float)settings.window_width + 64.0F || sy < -64.0F
		   || sy > (float)settings.window_height + 64.0F)
			continue;

		float alpha = fminf(ind->fade / fmaxf(ind->max_fade * 0.4F, 0.001F), 1.0F);
		alpha = fmaxf(0.0F, fminf(1.0F, alpha));
		if(alpha <= 0.02F)
			continue;

		char buf[16];
		snprintf(buf, sizeof(buf), "%d", ind->damage);

		float size = ind->crit ? DAMAGENUMBERS_FONT_SIZE_CRIT : DAMAGENUMBERS_FONT_SIZE;
		float text_w = font_length(size, buf);
		float draw_x = sx - text_w * 0.5F;
		float draw_y = sy;

		/* Always red -- brighter/pulsing for a crit, dimmer for a regular
		   hit -- with a solid black outline instead of a plain drop shadow,
		   so the number stays readable against bright terrain/sky. */
		float red;
		if(ind->crit) {
			float pulse = sinf(ind->pulse_time * 10.0F) * 0.5F + 0.5F;
			red = 0.7F + pulse * 0.3F;
		} else {
			red = 1.0F;
		}

		glColor4f(0.0F, 0.0F, 0.0F, alpha);
		font_render(draw_x - 1.F, draw_y, size, buf);
		font_render(draw_x + 1.F, draw_y, size, buf);
		font_render(draw_x, draw_y - 1.F, size, buf);
		font_render(draw_x, draw_y + 1.F, size, buf);

		glColor4f(red, 0.0F, 0.0F, alpha);
		font_render(draw_x, draw_y, size, buf);
	}
}
