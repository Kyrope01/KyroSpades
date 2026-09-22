
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

#include "damagefx.h"

/* Two-stage envelope for a smooth fade in/out:
   - dmg_envelope spikes on each hit (capped at 1.0) and decays
     exponentially, so a burst of hits piles up then fades together.
   - dmg_value chases the envelope with a fast low-pass, so the vignette
     ramps up over ~70 ms instead of popping, and fades out smoothly. */
static float dmg_envelope = 0.0F;
static float dmg_value = 0.0F;

void damagefx_hit(float amount) {
	if(amount <= 0.0F)
		return;
	/* ~10 HP tick -> ~0.28, a full-HP loss saturates at 1.0. Small hits
	   still get a visible (but clearly milder) flash. */
	dmg_envelope += fmaxf(0.12F, amount * 0.028F);
	if(dmg_envelope > 1.0F)
		dmg_envelope = 1.0F;
}

void damagefx_death(void) {
	dmg_envelope = 1.0F;
}

void damagefx_update(float dt) {
	if(dt <= 0.0F)
		return;
	dmg_envelope *= expf(-dt * 2.5F); /* flash decays over ~1 s */
	dmg_value += (dmg_envelope - dmg_value) * fminf(dt * 16.0F, 1.0F);
	if(dmg_envelope < 0.001F && dmg_value < 0.001F) {
		dmg_envelope = 0.0F;
		dmg_value = 0.0F;
	}
}

void damagefx_reset(void) {
	dmg_envelope = 0.0F;
	dmg_value = 0.0F;
}

float damagefx_vignette(void) {
	return dmg_value;
}
