
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

#ifndef DAMAGEFX_H
#define DAMAGEFX_H

/* Local-player damage feedback (toggleable in settings, "Visual Effects"):
   a red screen vignette with smooth fade animations, rendered by the
   post-process pass, plus a mild camera shake. network.c calls
   damagefx_hit()/damagefx_death() when the local player loses HP, main.c
   calls damagefx_update() every frame and feeds damagefx_vignette() to
   the post-process shader. */

/* Register a hit; amount is HP lost (1..100). */
void damagefx_hit(float amount);
/* One-shot full flash (the local player died). */
void damagefx_death(void);
/* Per-frame decay; call once per frame with the frame delta. */
void damagefx_update(float dt);
/* Clear the effect (respawn / new connection). */
void damagefx_reset(void);
/* Smoothed vignette intensity, 0..1 (consumed by the post-process shader). */
float damagefx_vignette(void);

#endif
