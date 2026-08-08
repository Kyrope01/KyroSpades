
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
#include <string.h>
#include <math.h>

#include "common.h"
#include "window.h"
#include "camera.h"
#include "map.h"
#include "matrix.h"
#include "particle.h"
#include "model.h"
#include "weapon.h"
#include "config.h"
#include "tesselator.h"
#include "entitysystem.h"
#include "player.h"
#include "texture.h"

struct entity_system particles;
struct tesselator particle_tesselator;
struct tesselator rain_tesselator[3];

static float rain_timer = 0.0F;
static float snow_timer = 0.0F;

int particle_stats_count = 0;
int particle_stats_total_created = 0;
int particle_stats_vertices = 0;

void particle_init() {
        entitysys_create(&particles, sizeof(struct Particle), 256);
        tesselator_create(&particle_tesselator, VERTEX_FLOAT, 0, 0);
        for(int i = 0; i < 3; i++)
                tesselator_create(&rain_tesselator[i], VERTEX_FLOAT, 0, 1);
}

struct particle_update_ctx {
        float dt;
        float now; // window_time() hoisted: one call per frame, not per particle
};

static bool particle_update_single(void* obj, void* user) {
        struct Particle* p = (struct Particle*)obj;
        struct particle_update_ctx* ctx = (struct particle_update_ctx*)user;
        float dt = ctx->dt;

        // Rain (type 253) - Mineclonia-style: gravity, lifetime
        if(p->type == 253) {
                p->lifetime -= dt;
                if(p->lifetime <= 0.0F)
                        return true;

                // Mineclonia gravity: -0.8 m/s² on top of initial velocity
                p->vy += -0.8F * dt;

                float movement_y = p->vy * dt;

                // Cached ground check (same optimization as before)
                if(p->vx == 0.0F && p->vz == 0.0F) {
                        if(p->y + movement_y <= p->ground_y)
                                return true; // rain dies on impact
                        p->y += movement_y;
                        return false;
                }

                float movement_x = p->vx * dt;
                float movement_z = p->vz * dt;

                if(movement_x != 0.0F && !map_isair(p->x + movement_x, p->y, p->z))
                        return true;
                if(movement_y != 0.0F && !map_isair(p->x + movement_x, p->y + movement_y, p->z))
                        return true;
                if(movement_z != 0.0F && !map_isair(p->x + movement_x, p->y + movement_y, p->z + movement_z))
                        return true;

                p->x += movement_x;
                p->y += movement_y;
                p->z += movement_z;
                return false;
        }

        // Determine fade time based on particle type (snow fades slower than rain)
        float fade_time = (p->type == 254) ? 16.0F : 2.6F;
        float size = p->size * (1.0F - ((ctx->now - p->fade) / fade_time));

        if(size < 0.01F) {
                return true;
        } else {
                float acc_y = -32.0F * dt;

                // Apply gravity to all particles
                p->vy += acc_y;

                // Cap snow particle velocity to prevent accelerating too fast
                if(p->type == 254) {
                        if(p->vy < -6.0F) p->vy = -6.0F;
                }

                float movement_x = p->vx * dt;
                float movement_y = p->vy * dt;
                float movement_z = p->vz * dt;
                bool on_ground = false;

                // Snow (type 254) - cached ground check
                if(p->type == 254 && p->vx == 0.0F && p->vz == 0.0F) {
                        if(p->y + movement_y <= p->ground_y) {
                                p->y = p->ground_y;
                                p->vy = 0.0F;
                                return false;
                        }
                        p->y += movement_y;
                        return false;
                }

                // general path (debris, casings, deflected snow)
                if(movement_x != 0.0F && !map_isair(p->x + movement_x, p->y, p->z)) {
                        movement_x = 0.0F;
                        if(p->type == 254) { p->vx = 0.0F; } else { p->vx = -p->vx * 0.6F; }
                        on_ground = true;
                }
                if(movement_y != 0.0F && !map_isair(p->x + movement_x, p->y + movement_y, p->z)) {
                        movement_y = 0.0F;
                        if(p->type == 254) { p->vy = 0.0F; } else { p->vy = -p->vy * 0.6F; }
                        on_ground = true;
                }
                if(movement_z != 0.0F && !map_isair(p->x + movement_x, p->y + movement_y, p->z + movement_z)) {
                        movement_z = 0.0F;
                        if(p->type == 254) { p->vz = 0.0F; } else { p->vz = -p->vz * 0.6F; }
                        on_ground = true;
                }

                float pow1_tys = 0.999991F + (2.55114F * dt - 2.30093F) * dt;
                float pow4_tys = 1.0F + (0.413432 * dt - 0.916185F) * dt;

                // air and ground friction
                if(on_ground) {
                        p->vx *= pow1_tys;
                        p->vy *= pow1_tys;
                        p->vz *= pow1_tys;

                        if(fabsf(p->vx) < 0.1F)
                                p->vx = 0.0F;
                        if(fabsf(p->vy) < 0.1F)
                                p->vy = 0.0F;
                        if(fabsf(p->vz) < 0.1F)
                                p->vz = 0.0F;
                } else {
                        p->vx *= pow4_tys;
                        p->vy *= pow4_tys;
                        p->vz *= pow4_tys;
                }

                p->x += movement_x;
                p->y += movement_y;
                p->z += movement_z;

                return false;
        }
}

void particle_update(float dt) {
        struct particle_update_ctx ctx = {.dt = dt, .now = window_time()};
        entitysys_iterate(&particles, &ctx, particle_update_single);
        particle_stats_count = particles.count;
}

struct particle_render_ctx {
        struct tesselator* tess;
        struct tesselator* rain_tess[3];
        float now;   // window_time() hoisted: one call per frame
        float rd_sq; // render distance squared, precomputed
};

static bool particle_render_single(void* obj, void* user) {
        struct Particle* p = (struct Particle*)obj;
        struct particle_render_ctx* ctx = (struct particle_render_ctx*)user;
        struct tesselator* tess = ctx->tess;

        if(distance2D(camera_x, camera_z, p->x, p->z) > ctx->rd_sq)
                return false;

        // Rain (type 253) - Mineclonia-style vertical billboard textured with raindrop PNGs.
        // Mineclonia's psdef has vertical=true, meaning each particle is a quad that stays
        // vertical (Y-up) but rotates around Y to face the camera. We compute the camera
        // direction in the XZ plane and orient the quad accordingly, so raindrops are
        // always visible as front-facing streaks regardless of camera angle.
        if(p->type == 253) {
                // Size: thin vertical streak matching Mineclonia's raindrop aspect ratio
                float w = p->size * 0.3F;       // half-width  (thin)
                float h = p->size * 2.0F;       // half-height (tall streak)

                int tex_idx = p->texture_id;
                if(tex_idx < 0 || tex_idx > 2)
                        tex_idx = 0;
                struct tesselator* rtess = ctx->rain_tess[tex_idx];

                // Camera-facing direction in XZ plane (particle → camera)
                float dx = camera_x - p->x;
                float dz = camera_z - p->z;
                float len = sqrtf(dx * dx + dz * dz);
                if(len < 0.0001F) {
                        dx = 1.0F;
                        dz = 0.0F;
                } else {
                        dx /= len;
                        dz /= len;
                }
                // "right" vector = perpendicular to view dir in XZ plane = (-dz, 0, dx)
                float rx = -dz * w;
                float rz = dx * w;

                // Vertical billboard quad — 4 corners, CCW from camera, full UV (0,0)→(1,1)
                float coords[12] = {
                        p->x - rx, p->y - h, p->z - rz, // bottom-left
                        p->x + rx, p->y - h, p->z + rz, // bottom-right
                        p->x + rx, p->y + h, p->z + rz, // top-right
                        p->x - rx, p->y + h, p->z - rz, // top-left
                };
                float uvs[8] = {0.0F, 0.0F, 1.0F, 0.0F, 1.0F, 1.0F, 0.0F, 1.0F};

                tesselator_set_color(rtess, p->color);
                tesselator_addf_uv(rtess, coords, uvs);
                particle_stats_vertices += 4;
                return false;
        }

        // Determine fade time based on particle type (snow fades slower than rain)
        float fade_time = (p->type == 254) ? 16.0F : 2.6F;
        float size = p->size / 2.0F * (1.0F - ((ctx->now - p->fade) / fade_time));

        if(size < 0.01F)
                return false;

        tesselator_set_color(tess, p->color);

        if(p->type == 255) {
                // Block break / spade hit / gun hit particles - always full 3D (6 faces = 24 vertices)
                particle_stats_vertices += 24;
                tesselator_addf_cube_face(tess, CUBE_FACE_X_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                tesselator_addf_cube_face(tess, CUBE_FACE_X_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                tesselator_addf_cube_face(tess, CUBE_FACE_Y_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                tesselator_addf_cube_face(tess, CUBE_FACE_Y_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                tesselator_addf_cube_face(tess, CUBE_FACE_Z_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                tesselator_addf_cube_face(tess, CUBE_FACE_Z_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
        } else if(p->type == 254) {
                // Snow (254) - 2D unless 3D setting enabled
                if(settings.rain_snow_3d) {
                        particle_stats_vertices += 24;
                        tesselator_addf_cube_face(tess, CUBE_FACE_X_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_X_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_Y_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_Y_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_Z_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_Z_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                } else {
                        particle_stats_vertices += 8;
                        tesselator_addf_cube_face(tess, CUBE_FACE_X_N, p->x - size, p->y - size, p->z - size, size * 2.0F);
                        tesselator_addf_cube_face(tess, CUBE_FACE_X_P, p->x - size, p->y - size, p->z - size, size * 2.0F);
                }
        } else {
                struct kv6_t* casing = weapon_casing(p->type);

                if(casing) {
                        particle_stats_vertices += casing->voxel_count * 24;
                        matrix_push(matrix_model);
                        matrix_identity(matrix_model);
                        matrix_translate(matrix_model, p->x, p->y, p->z);
                        matrix_pointAt(matrix_model, p->ox, p->oy * max(1.0F - (ctx->now - p->fade) / 0.5F, 0.0F), p->oz);
                        matrix_rotate(matrix_model, 90.0F, 0.0F, 1.0F, 0.0F);
                        matrix_upload();
                        kv6_render(casing, TEAM_SPECTATOR);
                        matrix_pop(matrix_model);
                }
        }

        return false;
}

void particle_render() {
        tesselator_clear(&particle_tesselator);
        for(int i = 0; i < 3; i++)
                tesselator_clear(&rain_tesselator[i]);
        particle_stats_vertices = 0;

        struct particle_render_ctx ctx = {
                .tess = &particle_tesselator,
                .rain_tess = {&rain_tesselator[0], &rain_tesselator[1], &rain_tesselator[2]},
                .now = window_time(),
                .rd_sq = (float)settings.render_distance * (float)settings.render_distance,
        };
        entitysys_iterate(&particles, &ctx, particle_render_single);

        matrix_upload();

        // Draw regular (untextured) particles
        tesselator_draw(&particle_tesselator, 1);

        // Draw textured rain particles only when rain is actually present.  The
        // old path toggled texture/blend/cull state and rebound textures every
        // frame even with weather disabled, adding needless driver work to the
        // normal gameplay loop.
        int rain_quads = rain_tesselator[0].quad_count + rain_tesselator[1].quad_count + rain_tesselator[2].quad_count;
        if(rain_quads > 0) {
                // Mineclonia uses 3 raindrop textures. Enable GL_TEXTURE_2D +
                // GL_MODULATE so the raindrop PNG alpha is sampled correctly.
                glActiveTexture(GL_TEXTURE0);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glEnable(GL_TEXTURE_2D);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_CULL_FACE); // rain billboards must be visible from any angle

                static const struct texture* rain_textures[3] = {&texture_rain1, &texture_rain2, &texture_rain3};
                for(int i = 0; i < 3; i++) {
                        if(rain_tesselator[i].quad_count > 0) {
                                glBindTexture(GL_TEXTURE_2D, rain_textures[i]->texture_id);
                                tesselator_draw(&rain_tesselator[i], 1);
                        }
                }

                glBindTexture(GL_TEXTURE_2D, 0);
                glDisable(GL_BLEND);
                glDisable(GL_TEXTURE_2D);
                glEnable(GL_CULL_FACE);
        }
}

void particle_create_casing(struct Player* p) {
        entitysys_add(&particles,
                                  &(struct Particle) {
                                          .size = 0.1F,
                                          .x = p->gun_pos.x,
                                          .y = p->gun_pos.y,
                                          .z = p->gun_pos.z,
                                          .ox = p->orientation.x,
                                          .oy = p->orientation.y,
                                          .oz = p->orientation.z,
                                          .vx = p->casing_dir.x * 3.5F,
                                          .vy = p->casing_dir.y * 3.5F,
                                          .vz = p->casing_dir.z * 3.5F,
                                          .fade = window_time(),
                                          .type = p->weapon,
                                          .color = 0x00FFFF,
                                  });
        particle_stats_total_created++;
}

void particle_create(unsigned int color, float x, float y, float z, float velocity, float velocity_y, int amount,
                                         float min_size, float max_size) {
        for(int k = 0; k < amount; k++) {
                float vx = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F);
                float vy = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F);
                float vz = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F);
                float len = len3D(vx, vy, vz);

                vx = (vx / len) * velocity;
                vy = (vy / len) * velocity * velocity_y;
                vz = (vz / len) * velocity;

                entitysys_add(&particles,
                                          &(struct Particle) {
                                                  .size = ((float)rand() / (float)RAND_MAX) * (max_size - min_size) + min_size,
                                                  .x = x,
                                                  .y = y,
                                                  .z = z,
                                                  .vx = vx,
                                                  .vy = vy,
                                                  .vz = vz,
                                                  .fade = window_time(),
                                                  .color = color,
                                                  .type = 255,
                                          });
                particle_stats_total_created++;
        }
}

void particle_create_rain(void) {
        rain_timer += 0.016F;
        if(rain_timer < 0.05F) {
                return;
        }
        rain_timer = 0.0F;

        float player_x, player_y, player_z;

        if(camera_mode == CAMERAMODE_SPECTATOR) {
                player_x = camera_x;
                player_y = camera_y;
                player_z = camera_z;
        } else {
                struct Player* local = &players[local_player_id];
                if(!local || !local->connected) {
                        return;
                }
                player_x = local->pos.x;
                player_y = local->pos.y;
                player_z = local->pos.z;
        }

        // Mineclonia rain spawns in a +-15 box around the player
        // (rain.lua: minpos=(-15,20,-15), maxpos=(15,25,15)).
        // Clamp to the render distance so rain never spawns beyond what the player can see.
        float spawn_area = 15.0F;
        if(settings.render_distance < spawn_area)
                spawn_area = settings.render_distance;
        float spawn_height_min = player_y + 20.0F; // Mineclonia: +20 to +25 above player
        float spawn_height_max = player_y + 25.0F;

        // Mineclonia runs 2 particlespawners at 500 particles/s each = 1000 particles/s total
        // (rain.lua: amount=500, time=0, two textures). At a 0.05s tick that is 50 particles per tick.
        int particles_per_tick = 50;

        for(int i = 0; i < particles_per_tick; i++) {
                float offset_x = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F) * spawn_area;
                float offset_z = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F) * spawn_area;

                float spawn_x = player_x + offset_x;
                float spawn_z = player_z + offset_z;

                if(spawn_x < 0 || spawn_x >= map_size_x || spawn_z < 0 || spawn_z >= map_size_z) {
                        continue;
                }

                float rain_y = spawn_height_min + ((float)rand() / (float)RAND_MAX) * (spawn_height_max - spawn_height_min);
                float life = 1.0F + ((float)rand() / (float)RAND_MAX) * 3.0F; // Mineclonia: 1-4 seconds (minexptime=1, maxexptime=4)

                // Mineclonia rain particles use the texture's own alpha (no color tint, full alpha;
                // psdef has no `color`). Cycle through the 3 raindrop textures (raindrop_1/2/3)
                // to match Mineclonia's client-side rain
                // (mcl_serverplayer/effects.lua default_precipitation_spawners.default.textures).
                int tex_id = rand() % 3;

                entitysys_add(&particles,
                                          &(struct Particle) {
                                                  .size = 0.2F + ((float)rand() / (float)RAND_MAX) * 0.15F,
                                                  .x = spawn_x,
                                                  .y = rain_y,
                                                  .z = spawn_z,
                                                  .vx = 0.0F,
                                                  .vy = -15.0F - ((float)rand() / (float)RAND_MAX) * 5.0F, // Mineclonia: minvel=-15, maxvel=-20
                                                  .vz = 0.0F,
                                                  .fade = window_time(),
                                                  .ground_y = map_height_at((int)spawn_x, (int)spawn_z) + 0.5F,
                                                  .color = rgba(0xFF, 0xFF, 0xFF, 0xFF), // full alpha, matches Mineclonia default
                                                  .type = 253,
                                                  .lifetime = life,
                                                  .texture_id = tex_id,
                                          });
                particle_stats_total_created++;
        }
}

void particle_create_snow(void) {
        snow_timer += 0.016F;
        if(snow_timer < 0.05F) {
                return;
        }
        snow_timer = 0.0F;

        float player_x, player_y, player_z;

        if(camera_mode == CAMERAMODE_SPECTATOR) {
                player_x = camera_x;
                player_y = camera_y;
                player_z = camera_z;
        } else {
                struct Player* local = &players[local_player_id];
                if(!local || !local->connected) {
                        return;
                }
                player_x = local->pos.x;
                player_y = local->pos.y;
                player_z = local->pos.z;
        }

        float snow_height = player_y + 40.0F; // Spawn twice as high (same as rain)
        float render_dist = settings.render_distance; // was sqrtf(x*x) — pointless

        int particles_per_frame = 225; // Increased by 50% (was 150)

        for(int i = 0; i < particles_per_frame; i++) {
                float offset_x = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F) * render_dist;
                float offset_z = (((float)rand() / (float)RAND_MAX) * 2.0F - 1.0F) * render_dist;

                float spawn_x = player_x + offset_x;
                float spawn_z = player_z + offset_z;

                if(spawn_x < 0 || spawn_x >= map_size_x || spawn_z < 0 || spawn_z >= map_size_z) {
                        continue;
                }

                entitysys_add(&particles,
                                          &(struct Particle) {
                                                  .size = 0.15F + ((float)rand() / (float)RAND_MAX) * 0.1F,
                                                  .x = spawn_x,
                                                  .y = snow_height,
                                                  .z = spawn_z,
                                                  .vx = 0.0F,
                                                  .vy = -3.0F - ((float)rand() / (float)RAND_MAX) * 3.0F, // Tripled speed for snow
                                                  .vz = 0.0F,
                                                  .fade = window_time(),
                                                  .ground_y = map_height_at((int)spawn_x, (int)spawn_z) + 1.0F, // one rwlock at spawn vs per-frame
                                                  .color = rgba(0xFF, 0xFF, 0xFF, 0xFF), // White color for snow
                                                  .type = 254, // Special type for snow (different fade time)
                                          });
                particle_stats_total_created++;
        }
}
