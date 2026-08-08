
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

#ifndef PARTICLE_H
#define PARTICLE_H

#include "player.h"
#include "tesselator.h"

struct Particle {
        float x, y, z;
        float vx, vy, vz;
        float ox, oy, oz;
        unsigned char type;
        float size;
        float fade;
        float ground_y; // cached surface height for rain/snow (types 253/254) — avoids per-frame map_isair rwlock
        unsigned int color;
        float lifetime; // remaining life for rain particles (Mineclonia-style, 1-4 seconds)
        int texture_id; // which raindrop texture to use (0, 1, or 2) — matches Mineclonia's 3-texture rain
};

extern int particle_stats_count;
extern int particle_stats_total_created;
extern int particle_stats_vertices;

extern struct tesselator rain_tesselator[3];

void particle_init(void);
void particle_update(float dt);
void particle_render(void);
void particle_create_casing(struct Player* p);
void particle_create(unsigned int color, float x, float y, float z, float velocity, float velocity_y, int amount,
                                         float min_size, float max_size);
void particle_create_rain(void);
void particle_create_snow(void);

#endif
