
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

#ifndef DAMAGENUM_H
#define DAMAGENUM_H

struct DamageNumber {
	int damage;
	int player_id;
	float fade;
	float last_hit_time;
	float x, y, z;
	float vx, vy, vz;
	int crit;
};

void damagenum_add(int player_id, float x, float y, float z, int damage);
void damagenum_update(float dt);
void damagenum_render(void);
void damagenum_clear(void);
void damagenum_init(void);

#endif
