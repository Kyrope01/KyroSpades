
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

#include "common.h"
#include "config.h"
#include "map.h"
#include "matrix.h"
#include "tesselator.h"
#include "glx.h"
#include "bloodmarks.h"

/* Blood marks are small alpha-blended "splat" quads glued to a solid block
   face near a bullet/melee hit. Each mark is actually a tiny cluster of 3
   overlapping blots so it reads as an irregular stain rather than a single
   hard-edged square. This is a lightweight, texture-free adaptation of
   ZeroSpades' BloodMarks feature (which used procedurally simulated voxel
   models) to KyroSpades' immediate-mode tesselator pipeline. */

#define BLOODMARKS_MAX 96
#define BLOODMARKS_BLOTS 3
#define BLOODMARKS_FADE_TIME 0.6F
#define BLOODMARKS_SURFACE_OFFSET 0.015F

struct blood_blot {
	float du, dv, size;
};

struct blood_mark {
	bool active;
	bool by_local;
	float time;  /* age in seconds, used for LRU eviction */
	float fade;  /* 0 = fully visible, 1 = fully evicted/removed */
	int anchor_x, anchor_y, anchor_z;
	int axis; /* 0 = X-normal face, 1 = Y-normal face, 2 = Z-normal face */
	int dir;  /* +1 or -1: which side of the anchor block the decal sits on */
	float u, v; /* position of the blot cluster within the face, [0.15, 0.85] */
	struct blood_blot blots[BLOODMARKS_BLOTS];
};

static struct blood_mark marks[BLOODMARKS_MAX];
static struct tesselator bloodmarks_tesselator;
static bool bloodmarks_tesselator_ready = false;

/* The mesh only actually needs to be rebuilt when a mark spawns or is
   removed (marks never move or otherwise change their geometry once
   placed). Everything else is static geometry, so re-tesselating all
   active marks from scratch every single frame -- as the original
   implementation did -- was pure wasted CPU on the common case where
   nothing changed since the last frame. */
static bool bloodmarks_dirty = true;

static float bloodmarks_frand(void) {
	/* ms_rand() returns a 15-bit value (0..32767) */
	return (float)ms_rand() / 32767.0F;
}

void bloodmarks_init(void) {
	memset(marks, 0, sizeof(marks));
	bloodmarks_dirty = true;
	if(!bloodmarks_tesselator_ready) {
		tesselator_create(&bloodmarks_tesselator, VERTEX_FLOAT, 0, 0);
		bloodmarks_tesselator_ready = true;
	}
}

void bloodmarks_clear(void) {
	for(int k = 0; k < BLOODMARKS_MAX; k++)
		marks[k].active = false;
	bloodmarks_dirty = true;
}

static bool bloodmarks_anchor_solid(const struct blood_mark* m) {
	if(m->anchor_x < 0 || m->anchor_y < 0 || m->anchor_z < 0 || m->anchor_x >= map_size_x
	   || m->anchor_y >= map_size_y || m->anchor_z >= map_size_z)
		return false;
	return !map_isair(m->anchor_x, m->anchor_y, m->anchor_z);
}

/* Find a slot to place a new mark: prefer an unused slot, otherwise evict
   the oldest mark currently on screen (simple LRU, good enough for a
   cosmetic decal budget). */
static struct blood_mark* bloodmarks_alloc_slot(void) {
	for(int k = 0; k < BLOODMARKS_MAX; k++) {
		if(!marks[k].active)
			return &marks[k];
	}

	int oldest = 0;
	float oldest_time = marks[0].time;
	for(int k = 1; k < BLOODMARKS_MAX; k++) {
		if(marks[k].time > oldest_time) {
			oldest_time = marks[k].time;
			oldest = k;
		}
	}
	return &marks[oldest];
}

static void bloodmarks_spawn(int bx, int by, int bz, int axis, int dir, bool by_local) {
	struct blood_mark* m = bloodmarks_alloc_slot();

	m->active = true;
	m->by_local = by_local;
	m->time = 0.0F;
	m->fade = 0.0F;
	m->anchor_x = bx;
	m->anchor_y = by;
	m->anchor_z = bz;
	m->axis = axis;
	m->dir = dir;
	m->u = 0.2F + bloodmarks_frand() * 0.6F;
	m->v = 0.2F + bloodmarks_frand() * 0.6F;

	for(int i = 0; i < BLOODMARKS_BLOTS; i++) {
		m->blots[i].du = (bloodmarks_frand() - 0.5F) * 0.22F;
		m->blots[i].dv = (bloodmarks_frand() - 0.5F) * 0.22F;
		m->blots[i].size = 0.045F + bloodmarks_frand() * 0.07F;
	}

	bloodmarks_dirty = true;
}

void bloodmarks_spatter(float x, float y, float z, float dirx, float diry, float dirz, bool by_local) {
	if(!settings.blood_marks)
		return;

	float speed = len3D(dirx, diry, dirz);
	if(speed < 0.001F)
		return;

	float nx = dirx / speed, ny = diry / speed, nz = dirz / speed;

	const int attempts = 3;
	for(int i = 0; i < attempts; i++) {
		/* Randomize each droplet's direction a bit so the splatter doesn't
		   look like a single laser-straight streak. */
		float rx = nx + (bloodmarks_frand() - 0.5F) * 0.7F;
		float ry = ny + (bloodmarks_frand() - 0.5F) * 0.7F;
		float rz = nz + (bloodmarks_frand() - 0.5F) * 0.7F;
		float rl = len3D(rx, ry, rz);
		if(rl < 0.001F)
			continue;
		rx /= rl;
		ry /= rl;
		rz /= rl;

		float px = x, py = y, pz = z;
		const float step = 0.1F;
		const float max_dist = (speed > 3.0F) ? 3.2F : 2.0F;

		bool hit = false;
		int hbx = 0, hby = 0, hbz = 0, haxis = 0, hdir = 1;

		for(float d = 0.0F; d < max_dist; d += step) {
			float nxp = px + rx * step, nyp = py + ry * step, nzp = pz + rz * step;
			int ox = (int)floorf(px), oy = (int)floorf(py), oz = (int)floorf(pz);
			int qx = (int)floorf(nxp), qy = (int)floorf(nyp), qz = (int)floorf(nzp);

			if((qx != ox || qy != oy || qz != oz)
			   && qx >= 0 && qy >= 0 && qz >= 0 && qx < map_size_x && qy < map_size_y && qz < map_size_z
			   && !map_isair(qx, qy, qz)) {
				if(qx != ox) {
					haxis = 0;
					hdir = (qx > ox) ? -1 : 1;
				} else if(qy != oy) {
					haxis = 1;
					hdir = (qy > oy) ? -1 : 1;
				} else {
					haxis = 2;
					hdir = (qz > oz) ? -1 : 1;
				}
				hbx = qx;
				hby = qy;
				hbz = qz;
				hit = true;
				break;
			}

			px = nxp;
			py = nyp;
			pz = nzp;
		}

		if(!hit)
			continue;

		/* Never stain the water surface/seabed boundary. */
		if(hby <= 0)
			continue;

		bloodmarks_spawn(hbx, hby, hbz, haxis, hdir, by_local);
	}
}

void bloodmarks_update(float dt) {
	if(!settings.blood_marks) {
		bloodmarks_clear();
		return;
	}

	for(int k = 0; k < BLOODMARKS_MAX; k++) {
		struct blood_mark* m = &marks[k];
		if(!m->active)
			continue;

		m->time += dt;

		if(m->fade > 0.0F) {
			m->fade += dt / BLOODMARKS_FADE_TIME;
			if(m->fade >= 1.0F) {
				m->active = false;
				bloodmarks_dirty = true;
			}
			continue;
		}

		/* If the block this decal is glued to got destroyed, the surface no
		   longer exists -- remove immediately rather than floating in air. */
		if(!bloodmarks_anchor_solid(m)) {
			m->active = false;
			bloodmarks_dirty = true;
		}
	}
}

/* Emit one blot's quad into the tesselator. `u,v` are in block-local [0,1]
   face space; `fixed` is the world coordinate along the face normal axis. */
static void bloodmarks_emit_blot(int axis, float fixed_coord, float base_x, float base_y, float base_z, float u,
                                  float v, float size) {
	float u0 = u - size, u1 = u + size;
	float v0 = v - size, v1 = v + size;

	float coords[12];

	switch(axis) {
		case 0: /* X-normal face: in-plane axes are Y (u) and Z (v) */
			coords[0] = fixed_coord; coords[1] = base_y + u0; coords[2] = base_z + v0;
			coords[3] = fixed_coord; coords[4] = base_y + u1; coords[5] = base_z + v0;
			coords[6] = fixed_coord; coords[7] = base_y + u1; coords[8] = base_z + v1;
			coords[9] = fixed_coord; coords[10] = base_y + u0; coords[11] = base_z + v1;
			break;
		case 1: /* Y-normal face: in-plane axes are X (u) and Z (v) */
			coords[0] = base_x + u0; coords[1] = fixed_coord; coords[2] = base_z + v0;
			coords[3] = base_x + u1; coords[4] = fixed_coord; coords[5] = base_z + v0;
			coords[6] = base_x + u1; coords[7] = fixed_coord; coords[8] = base_z + v1;
			coords[9] = base_x + u0; coords[10] = fixed_coord; coords[11] = base_z + v1;
			break;
		default: /* case 2: Z-normal face: in-plane axes are X (u) and Y (v) */
			coords[0] = base_x + u0; coords[1] = base_y + v0; coords[2] = fixed_coord;
			coords[3] = base_x + u1; coords[4] = base_y + v0; coords[5] = fixed_coord;
			coords[6] = base_x + u1; coords[7] = base_y + v1; coords[8] = fixed_coord;
			coords[9] = base_x + u0; coords[10] = base_y + v1; coords[11] = fixed_coord;
			break;
	}

	tesselator_addf_simple(&bloodmarks_tesselator, coords);
}

void bloodmarks_render(void) {
	if(!settings.blood_marks)
		return;

	if(bloodmarks_dirty) {
		tesselator_clear(&bloodmarks_tesselator);

		for(int k = 0; k < BLOODMARKS_MAX; k++) {
			struct blood_mark* m = &marks[k];
			if(!m->active)
				continue;

			float alpha = (1.0F - m->fade) * 0.85F;
			if(alpha <= 0.01F)
				continue;

			/* Deep hemoglobin red, matching the tone used for the existing
			   blood particle effect (0x0000FF packed as R=255 in this engine's
			   byte order), just darker so it reads as a stain, not fresh spray. */
			tesselator_set_color(&bloodmarks_tesselator, rgba(150, 14, 10, (int)(alpha * 255.0F)));

			float fixed_coord = (float)((m->axis == 0) ? m->anchor_x : (m->axis == 1) ? m->anchor_y : m->anchor_z);
			fixed_coord += (m->dir > 0) ? (1.0F + BLOODMARKS_SURFACE_OFFSET) : -BLOODMARKS_SURFACE_OFFSET;

			for(int i = 0; i < BLOODMARKS_BLOTS; i++) {
				float u = m->u + m->blots[i].du;
				float v = m->v + m->blots[i].dv;
				/* Keep each blot from spilling past the block's own face. */
				u = fmaxf(0.05F, fminf(0.95F, u));
				v = fmaxf(0.05F, fminf(0.95F, v));
				bloodmarks_emit_blot(m->axis, fixed_coord, (float)m->anchor_x, (float)m->anchor_y,
				                      (float)m->anchor_z, u, v, m->blots[i].size);
			}
		}

		bloodmarks_dirty = false;
	}

	/* Nothing to draw -- either no marks were ever spawned, or the last
	   rebuild produced an empty mesh (all marks evicted/destroyed). */
	if(bloodmarks_tesselator.quad_count == 0)
		return;

	matrix_upload();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_FALSE);

	/* with_color=1: each blot carries its own per-mark fade alpha, so we
	   need the tesselator's vertex color array rather than a single flat
	   glColor for the whole batch. */
	tesselator_draw(&bloodmarks_tesselator, 1);

	glDepthMask(GL_TRUE);
	glEnable(GL_CULL_FACE);
	glDisable(GL_BLEND);
}
