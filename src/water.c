
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
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "window.h"
#include "config.h"
#include "matrix.h"
#include "glx.h"
#include "map.h"
#include "camera.h"
#include "lighting.h"
#include "shadow.h"
#include "tesselator.h"
#include "water.h"

#define WATER_RAY_STEPS 96
#ifdef OPENGL_ES
#define WATER_CELL_BUDGET 4096
#else
#define WATER_CELL_BUDGET 16384
#endif
#define WATER_TILE 16
#define WATER_VIEW_REFRESH_DISTANCE 0.25F
#define WATER_HEIGHT_REFRESH_DISTANCE 0.10F
#define WATER_MESH_RECENTER_DISTANCE 1.0F
#define WATER_MESH_HEIGHT_DISTANCE 0.25F
#define WATER_MESH_ORIENTATION_EPSILON 0.01F

#ifdef OPENGL_CORE
static GLuint water_program;
static GLint water_uniform_enhanced = -1;
static GLint water_uniform_camera = -1;
static GLint water_uniform_map_size = -1;
static GLint water_uniform_fog_distance = -1;
static GLint water_uniform_fog_color = -1;
static GLint water_uniform_time = -1;
static GLint water_uniform_waves = -1;
static GLint water_uniform_wave_intensity = -1;
static GLint water_uniform_wave_speed = -1;
static GLint water_uniform_wave_mode = -1;

static const char* water_vertex_shader =
        "#version 330 core\n"
        "layout(location = 0) in vec3 a_Position;\n"
        "layout(location = 1) in vec4 a_Color;\n"
        "layout(location = 3) in vec3 a_Normal;\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "out vec4 v_Color;\n"
        "out vec3 v_WorldPosition;\n"
        "out vec3 v_WorldNormal;\n"
        "void main(void) {\n"
        "    vec4 world = u_Model * vec4(a_Position, 1.0);\n"
        "    v_Color = a_Color;\n"
        "    v_WorldPosition = world.xyz;\n"
        "    v_WorldNormal = normalize((u_Model * vec4(a_Normal, 0.0)).xyz);\n"
        "    gl_Position = u_MVP * vec4(a_Position, 1.0);\n"
        "}\n";

static const char* water_fragment_shader =
        "#version 330 core\n"
        "uniform float u_Enhanced;\n"
        "uniform vec3 u_Camera;\n"
        "uniform vec2 u_MapSize;\n"
        "uniform float u_FogDistance;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform float u_Time;\n"
        "uniform float u_WavesEnabled;\n"
        "uniform float u_WaveIntensity;\n"
        "uniform float u_WaveSpeed;\n"
        "uniform float u_WaveMode;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform vec4 u_LightPositionRadius[4];\n"
        "uniform vec4 u_LightColorIntensity[4];\n"
        "uniform vec4 u_FlashlightDirection;\n"
        "uniform sampler2D u_ShadowMap;\n"
        "uniform mat4 u_ShadowMatrix;\n"
        "uniform float u_ShadowTexel;\n"
        "uniform float u_ShadowIntensity;\n"
        "uniform float u_ShadowEnabled;\n"
        "in vec4 v_Color;\n"
        "in vec3 v_WorldPosition;\n"
        "in vec3 v_WorldNormal;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "vec3 ripple_normal(vec2 position) {\n"
        "    if(u_WavesEnabled < 0.5) return vec3(0.0, 1.0, 0.0);\n"
        "    float t = u_Time * u_WaveSpeed;\n"
        "    float strength = clamp(u_WaveIntensity, 0.0, 5.0);\n"
        "    float dx = strength * (0.056 * cos(t * 0.9 + position.x * 0.7 + position.y * 0.5)\n"
        "                         + 0.024 * cos(t * 1.3 + position.x * 0.4 + position.y * 1.1)\n"
        "                         + 0.060 * cos(t * 1.8 + position.x * 1.5 + position.y * 0.2));\n"
        "    float dz = strength * (0.040 * cos(t * 0.9 + position.x * 0.7 + position.y * 0.5)\n"
        "                         + 0.066 * cos(t * 1.3 + position.x * 0.4 + position.y * 1.1)\n"
        "                         + 0.008 * cos(t * 1.8 + position.x * 1.5 + position.y * 0.2));\n"
        "    float mode_scale = u_WaveMode > 0.5 ? 0.32 : 0.68;\n"
        "    return normalize(vec3(-dx * mode_scale, 1.0, -dz * mode_scale));\n"
        "}\n"
        "float water_shadow(vec3 world_position, vec3 normal, vec3 sun_direction) {\n"
        "    if(u_ShadowEnabled < 0.5) return 1.0;\n"
        "    vec4 light_clip = u_ShadowMatrix * vec4(world_position, 1.0);\n"
        "    if(abs(light_clip.w) < 0.0001) return 1.0;\n"
        "    vec3 projected = light_clip.xyz / light_clip.w * 0.5 + 0.5;\n"
        "    if(any(lessThan(projected, vec3(0.0))) || any(greaterThan(projected, vec3(1.0)))) return 1.0;\n"
        "    float slope = 1.0 - max(dot(normal, sun_direction), 0.0);\n"
        "    float bias = max(0.00022, 0.0013 * slope);\n"
        "    float visible = 0.0;\n"
        "    for(int y = -1; y <= 1; ++y) {\n"
        "        for(int x = -1; x <= 1; ++x) {\n"
        "            vec2 offset = vec2(float(x), float(y)) * u_ShadowTexel;\n"
        "            float closest = texture(u_ShadowMap, projected.xy + offset).r;\n"
        "            visible += projected.z - bias <= closest ? 1.0 : 0.0;\n"
        "        }\n"
        "    }\n"
        "    float pcf = visible / 9.0;\n"
        "    float edge = max(abs(projected.x - 0.5), abs(projected.y - 0.5)) * 2.0;\n"
        "    pcf = mix(pcf, 1.0, smoothstep(0.86, 1.0, edge));\n"
        "    return mix(1.0, pcf, clamp(u_ShadowIntensity, 0.0, 1.0));\n"
        "}\n"
        "float animation_hash(vec2 position) {\n"
        "    ivec2 cell = ivec2(mod(floor(position), max(u_MapSize, vec2(1.0))));\n"
        "    uint h = uint(cell.x) * 374761393u + uint(cell.y) * 668265263u;\n"
        "    h = (h ^ (h >> 13u)) * 1274126177u;\n"
        "    return float(h & 65535u) / 65535.0;\n"
        "}\n"
        "void main(void) {\n"
        "    float h = animation_hash(v_WorldPosition.xz);\n"
        "    float base_shimmer = 0.85 + 0.22 * h;\n"
        "    float shimmer = 1.0 + 0.08 / base_shimmer * sin(u_Time * (0.6 + h) + h * 6.2831);\n"
        "    vec3 animated_color = min(v_Color.rgb * shimmer, vec3(1.0));\n"
        "    vec3 color = animated_color;\n"
        "    if(u_Enhanced > 0.5) {\n"
        "        vec3 geometric = length(v_WorldNormal) > 0.001 ? normalize(v_WorldNormal) : vec3(0.0, 1.0, 0.0);\n"
        "        float upward = smoothstep(0.30, 0.90, geometric.y);\n"
        "        vec3 normal = normalize(mix(geometric, ripple_normal(v_WorldPosition.xz), upward * 0.72));\n"
        "        vec3 to_view = u_Camera - v_WorldPosition;\n"
        "        vec3 view_direction = to_view / max(length(to_view), 0.0001);\n"
        "        vec3 sun_direction = length(u_SunDirection) > 0.001 ? normalize(u_SunDirection) : normalize(vec3(0.35, 0.82, 0.45));\n"
        "        float visibility = water_shadow(v_WorldPosition, normal, sun_direction);\n"
        "        float ndv = clamp(dot(normal, view_direction), 0.0, 1.0);\n"
        "        float fresnel = 0.035 + 0.965 * pow(1.0 - ndv, 5.0);\n"
        "        vec3 deep_tint = vec3(0.025, 0.105, 0.18);\n"
        "        vec3 horizon = mix(u_FogColor, vec3(0.30, 0.50, 0.68), 0.28);\n"
        "        vec3 body = mix(deep_tint, animated_color, 0.74);\n"
        "        vec3 reflection = mix(animated_color, horizon, 0.18 + 0.24 * fresnel);\n"
        "        color = mix(body, reflection, 0.22 + 0.50 * fresnel);\n"
        "        float sun_diffuse = max(dot(normal, sun_direction), 0.0);\n"
        "        color *= 0.88 + 0.12 * sun_diffuse * visibility;\n"
        "        vec3 sun_half_sum = sun_direction + view_direction;\n"
        "        vec3 sun_half = sun_half_sum / max(length(sun_half_sum), 0.0001);\n"
        "        float sun_specular = pow(max(dot(normal, sun_half), 0.0), 96.0);\n"
        "        color += u_SunColor * sun_specular * (0.28 + 0.62 * fresnel) * visibility;\n"
        "        bool flashlight_active = u_FlashlightDirection.w >= 0.0;\n"
        "        for(int i = 0; i < 4; ++i) {\n"
        "            vec4 pr = u_LightPositionRadius[i];\n"
        "            vec4 ci = u_LightColorIntensity[i];\n"
        "            if(ci.a > 0.0 && (i != 3 || !flashlight_active)) {\n"
        "                vec3 delta = pr.xyz - v_WorldPosition;\n"
        "                float distance_to_light = length(delta);\n"
        "                float attenuation = clamp(1.0 - distance_to_light / max(pr.w, 0.0001), 0.0, 1.0);\n"
        "                attenuation *= attenuation;\n"
        "                vec3 light_direction = delta / max(distance_to_light, 0.0001);\n"
        "                float diffuse = max(dot(normal, light_direction), 0.0);\n"
        "                vec3 light_half_sum = light_direction + view_direction;\n"
        "                vec3 light_half = light_half_sum / max(length(light_half_sum), 0.0001);\n"
        "                float specular = pow(max(dot(normal, light_half), 0.0), 48.0);\n"
        "                color += ci.rgb * ci.a * attenuation * (0.05 + 0.18 * diffuse + 0.32 * specular);\n"
        "            }\n"
        "        }\n"
        "        if(flashlight_active) {\n"
        "            vec4 pr = u_LightPositionRadius[3];\n"
        "            vec4 ci = u_LightColorIntensity[3];\n"
        "            vec3 from_light = v_WorldPosition - pr.xyz;\n"
        "            float light_distance = length(from_light);\n"
        "            vec3 ray_direction = from_light / max(light_distance, 0.0001);\n"
        "            float cone = smoothstep(u_FlashlightDirection.w, min(u_FlashlightDirection.w + 0.12, 0.999),\n"
        "                                    dot(ray_direction, normalize(u_FlashlightDirection.xyz)));\n"
        "            float range = clamp(1.0 - light_distance / max(pr.w, 0.0001), 0.0, 1.0);\n"
        "            range *= range;\n"
        "            vec3 light_direction = -ray_direction;\n"
        "            float diffuse = max(dot(normal, light_direction), 0.0);\n"
        "            vec3 light_half_sum = light_direction + view_direction;\n"
        "            vec3 light_half = light_half_sum / max(length(light_half_sum), 0.0001);\n"
        "            float specular = pow(max(dot(normal, light_half), 0.0), 48.0);\n"
        "            float fill = clamp(1.0 - light_distance / 10.0, 0.0, 1.0);\n"
        "            fill = 0.30 * fill * fill;\n"
        "            color += ci.rgb * ci.a * (cone * range + fill) * (0.05 + 0.18 * diffuse + 0.32 * specular);\n"
        "        }\n"
        "    }\n"
        "    float fog_distance = clamp(length(v_WorldPosition.xz - u_Camera.xz) * u_FogDistance, 0.0, 1.0);\n"
        "    float fog = fog_distance * fog_distance * (3.0 - 2.0 * fog_distance);\n"
        "    out_Color = vec4(mix(color, u_FogColor, fog), v_Color.a);\n"
        "}\n";
#endif

enum water_cell_state {
        WATER_CELL_UNKNOWN,
        WATER_CELL_DRY,
        WATER_CELL_SURFACE,
};

static struct {
        /* The worker writes cache/state while the renderer consumes a separate
           main-thread snapshot. This avoids reading partially-written colors. */
        uint32_t* cache;
        uint8_t* state;
        uint32_t* render_cache;
        uint8_t* render_state;
        uint8_t* dirty_rows;
        unsigned int dirty_row_count;
        int size_x, size_z;
        int render_size_x, render_size_z;
        unsigned int cursor;
        int span;
        int rows_remaining;

        bool last_reflective;
        bool was_active;
        bool snapshot_invalidated;
        bool refresh_initialized;
        unsigned int seen_map_revision;
        unsigned int snapshot_revision;
        float refresh_cam_x, refresh_cam_y, refresh_cam_z;
        float refresh_render_distance;
        float refresh_fog[3];

        /* Strict-Core flat water geometry is reusable between meaningful view
           changes. The current MVP still uploads every frame; only CPU culling
           and VBO replacement are cached. CPU-animated paths rebuild. */
        bool mesh_valid;
        bool mesh_had_waves;
        unsigned int mesh_snapshot_revision;
        float mesh_render_distance;
        float mesh_camera_x, mesh_camera_y, mesh_camera_z;
        mat4 mesh_projection;
        mat4 mesh_view;

        struct tesselator tess;
        struct glx_displaylist dl;
        bool gl_init;

        pthread_t thread;
        bool thread_started;
        bool pending;
        bool stop;
        struct {
                float cx, cy, cz;
                float rd;
                int span;
                bool reflective;
                float fog[3];
        } job;
} wr = {0};

static pthread_mutex_t water_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t water_cond = PTHREAD_COND_INITIALIZER;
/* Map edits can arrive from the collapse worker. Keep their tiny notification
 * lock independent from the expensive reflection-cache lock. */
static pthread_mutex_t water_revision_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int water_map_revision;

/* Publish completed worker rows before the next slice is queued. Keeping this
 * declaration near the shared state makes that producer/consumer ordering
 * explicit even though the implementation lives beside water_render(). */
static void water_snapshot_refresh_locked(void);

#ifdef OPENGL_CORE
static void water_reset_uniforms(void) {
        water_uniform_enhanced = -1;
        water_uniform_camera = -1;
        water_uniform_map_size = -1;
        water_uniform_fog_distance = -1;
        water_uniform_fog_color = -1;
        water_uniform_time = -1;
        water_uniform_waves = -1;
        water_uniform_wave_intensity = -1;
        water_uniform_wave_speed = -1;
        water_uniform_wave_mode = -1;
}
#endif

bool water_init(void) {
        pthread_mutex_lock(&water_lock);
        if(!wr.thread_started) {
                wr.stop = false;
                wr.pending = false;
        }
        pthread_mutex_unlock(&water_lock);

#ifdef OPENGL_CORE
        if(water_program)
                return true;
        water_reset_uniforms();
        if(!glx_version_at_least(3, 3)) {
                log_info("GPU water shading unavailable without OpenGL 3.3 Core; using the established water renderer");
                return false;
        }

        water_program = (GLuint)glx_shader(water_vertex_shader, water_fragment_shader);
        if(!water_program) {
                log_error("GPU water shader failed; using the established water renderer");
                return false;
        }

        GLint uniform_mvp = glx_uniform_location(water_program, "u_MVP");
        GLint uniform_model = glx_uniform_location(water_program, "u_Model");
        water_uniform_enhanced = glx_uniform_location(water_program, "u_Enhanced");
        water_uniform_camera = glx_uniform_location(water_program, "u_Camera");
        water_uniform_map_size = glx_uniform_location(water_program, "u_MapSize");
        water_uniform_fog_distance = glx_uniform_location(water_program, "u_FogDistance");
        water_uniform_fog_color = glx_uniform_location(water_program, "u_FogColor");
        water_uniform_time = glx_uniform_location(water_program, "u_Time");
        water_uniform_waves = glx_uniform_location(water_program, "u_WavesEnabled");
        water_uniform_wave_intensity = glx_uniform_location(water_program, "u_WaveIntensity");
        water_uniform_wave_speed = glx_uniform_location(water_program, "u_WaveSpeed");
        water_uniform_wave_mode = glx_uniform_location(water_program, "u_WaveMode");

        if(uniform_mvp < 0 || uniform_model < 0 || water_uniform_enhanced < 0
           || water_uniform_camera < 0 || water_uniform_map_size < 0
           || water_uniform_fog_distance < 0 || water_uniform_fog_color < 0 || water_uniform_time < 0
           || water_uniform_waves < 0 || water_uniform_wave_intensity < 0
           || water_uniform_wave_speed < 0 || water_uniform_wave_mode < 0) {
                log_error("GPU water shader contract is incomplete; using the established water renderer");
                glx_delete_program(water_program);
                water_program = 0;
                water_reset_uniforms();
                return false;
        }

        log_info("GPU water shading ready (Fresnel reflections, wave normals, lights, and terrain shadows)");
        return true;
#else
        return false;
#endif
}

void water_deinit(void) {
        pthread_mutex_lock(&water_lock);
        wr.stop = true;
        wr.pending = false;
        pthread_cond_signal(&water_cond);
        pthread_mutex_unlock(&water_lock);

        if(wr.thread_started)
                pthread_join(wr.thread, NULL);

        pthread_mutex_lock(&water_lock);
        if(wr.gl_init) {
                glx_displaylist_destroy(&wr.dl);
                tesselator_free(&wr.tess);
        }
        free(wr.cache);
        free(wr.state);
        free(wr.render_cache);
        free(wr.render_state);
        free(wr.dirty_rows);
        memset(&wr, 0, sizeof(wr));
        pthread_mutex_unlock(&water_lock);

#ifdef OPENGL_CORE
        if(water_program)
                glx_delete_program(water_program);
        water_program = 0;
        water_reset_uniforms();
#endif
}

bool water_gpu_supported(void) {
#ifdef OPENGL_CORE
        return water_program != 0;
#else
        return false;
#endif
}

void water_map_changed(void) {
        pthread_mutex_lock(&water_revision_lock);
        water_map_revision++;
        pthread_mutex_unlock(&water_revision_lock);
}

static unsigned int water_current_map_revision(void) {
        pthread_mutex_lock(&water_revision_lock);
        unsigned int revision = water_map_revision;
        pthread_mutex_unlock(&water_revision_lock);
        return revision;
}

void water_invalidate(void) {
        pthread_mutex_lock(&water_lock);
        if(wr.cache)
                memset(wr.cache, 0, (size_t)wr.size_x * (size_t)wr.size_z * sizeof(uint32_t));
        if(wr.state)
                memset(wr.state, 0, (size_t)wr.size_x * (size_t)wr.size_z * sizeof(uint8_t));
        if(wr.dirty_rows)
                memset(wr.dirty_rows, 0, (size_t)wr.size_z * sizeof(uint8_t));
        wr.dirty_row_count = 0;
        wr.cursor = 0;
        wr.rows_remaining = 0;
        wr.refresh_initialized = false;
        wr.snapshot_invalidated = true;
        wr.mesh_valid = false;
        pthread_mutex_unlock(&water_lock);
}

bool water_shader_active(void) {
        /* Keep the original BetterSpades fast path unless the user explicitly
           enables enhanced water. Above-surface rendering also preserves the
           established underwater terrain fallback. */
        if(!settings.water_shader && !settings.water_waves)
                return false;
        return camera_y > WATER_LEVEL + 0.05F && map_size_x > 0 && map_size_z > 0;
}

static int water_span(void) {
        int span = (int)(settings.render_distance * 2.0F) + 8;
        int m = max(map_size_x, map_size_z);
        span = min(span, m);
        span = min(span, 512);
        span = max(span, 64);
        return span;
}

static float cell_hash(int x, int z) {
        uint32_t h = (uint32_t)x * 374761393u + (uint32_t)z * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return (float)(h & 0xFFFF) / 65535.0F;
}

static bool reflect_raycast(float ox, float oz, float dx, float dy, float dz, float max_dist, int max_steps,
                                                        float* out_r, float* out_g, float* out_b) {
        int gx = (int)floorf(ox);
        int gy = (int)WATER_LEVEL;
        int gz = (int)floorf(oz);

        int step_x = (dx > 0.0F) ? 1 : -1;
        int step_z = (dz > 0.0F) ? 1 : -1;

        float tdelta_x = (dx != 0.0F) ? fabsf(1.0F / dx) : 1e30F;
        float tdelta_y = (dy != 0.0F) ? fabsf(1.0F / dy) : 1e30F;
        float tdelta_z = (dz != 0.0F) ? fabsf(1.0F / dz) : 1e30F;

        float tmax_x = (dx > 0.0F) ? ((gx + 1) - ox) * tdelta_x : (dx < 0.0F ? (ox - gx) * tdelta_x : 1e30F);
        float tmax_y = tdelta_y;
        float tmax_z = (dz > 0.0F) ? ((gz + 1) - oz) * tdelta_z : (dz < 0.0F ? (oz - gz) * tdelta_z : 1e30F);

        float t = 0.0F;

        for(int i = 0; i < max_steps; i++) {
                int axis;
                if(tmax_x < tmax_y && tmax_x < tmax_z) {
                        gx += step_x;
                        t = tmax_x;
                        tmax_x += tdelta_x;
                        axis = 0;
                } else if(tmax_y < tmax_z) {
                        gy++;
                        t = tmax_y;
                        tmax_y += tdelta_y;
                        axis = 1;
                } else {
                        gz += step_z;
                        t = tmax_z;
                        tmax_z += tdelta_z;
                        axis = 2;
                }

                if(gy >= map_size_y || t > max_dist)
                        return false;

                int wx = ((gx % map_size_x) + map_size_x) % map_size_x;
                int wz = ((gz % map_size_z) + map_size_z) % map_size_z;

                if(!map_isair_nolock(wx, gy, wz)) {
                        uint32_t c = map_get_nolock(wx, gy, wz);
                        float shade = (axis == 1) ? 0.60F : 0.85F;
                        *out_r = red(c) * shade;
                        *out_g = green(c) * shade;
                        *out_b = blue(c) * shade;
                        return true;
                }
        }

        return false;
}

static int water_wrap(int v, int m) {
        return ((v % m) + m) % m;
}

/* Return the actual block color below the water at (wx, wz) so each
   water cell has individual color variation. Returns a default
   deep-water tone if nothing is found (void column). */
static uint32_t water_seabed_color(int wx, int wz) {
        for (int y = (int)WATER_LEVEL - 1; y >= 0; y--) {
                if (!map_isair_nolock(wx, y, wz))
                        return map_get_nolock(wx, y, wz);
        }
        return rgba(30, 60, 90, 255);
}

static uint32_t water_cell_compute(int x, int z, int wx, int wz) {
        /* The worker only consumes its immutable job snapshot; settings and
           fog can change on the main thread while a slice is running. */
        if(!wr.job.reflective) {
                uint32_t wc = water_seabed_color(wx, wz);
                float h = cell_hash(wx, wz);
                /* Keep worker-produced colors deterministic for a fixed view.
                   Temporal shimmer is applied continuously during rendering;
                   doing it per asynchronous row made brightness update in
                   visible strips and forced needless cache uploads at rest. */
                float shimmer = 0.85F + 0.22F * h;
                float wr_ = red(wc) * shimmer;
                float wg_ = green(wc) * shimmer;
                float wb_ = blue(wc) * shimmer;
                float hr = wr.job.fog[0] * 255.0F;
                float hg = wr.job.fog[1] * 255.0F;
                float hb = wr.job.fog[2] * 255.0F;
                float w = 0.20F + 0.70F * 0.25F;
                float r = wr_ + (hr - wr_) * w;
                float g = wg_ + (hg - wg_) * w;
                float b = wb_ + (hb - wb_) * w;
                return rgba((int)fminf(r, 255.0F), (int)fminf(g, 255.0F), (int)fminf(b, 255.0F), 255);
        }

        float px = x + 0.5F;
        float pz = z + 0.5F;

        float dx = px - wr.job.cx;
        float dy = WATER_LEVEL - wr.job.cy;
        float dz = pz - wr.job.cz;
        float len = sqrtf(dx * dx + dy * dy + dz * dz);
        if(len < 0.01F)
                len = 0.01F;
        dx /= len;
        dy = -dy / len;
        dz /= len;

        float frac = fminf(sqrtf((px - wr.job.cx) * (px - wr.job.cx) + (pz - wr.job.cz) * (pz - wr.job.cz)) / wr.job.rd,
                                           1.0F);
        int max_steps = WATER_RAY_STEPS - (int)((WATER_RAY_STEPS - 16) * frac);

        float hr, hg, hb;
        bool hit = false;
        if(dy > 0.001F)
                hit = reflect_raycast(px, pz, dx, dy, dz, wr.job.rd, max_steps, &hr, &hg, &hb);

        if(!hit) {
                hr = wr.job.fog[0] * 255.0F;
                hg = wr.job.fog[1] * 255.0F;
                hb = wr.job.fog[2] * 255.0F;
        }

        uint32_t wc = water_seabed_color(wx, wz);
        float h = cell_hash(wx, wz);
        float shimmer = 0.85F + 0.22F * h;
        float wr_ = red(wc) * shimmer;
        float wg_ = green(wc) * shimmer;
        float wb_ = blue(wc) * shimmer;

        float f = 1.0F - fminf(fmaxf(dy, 0.0F), 1.0F);
        float w = 0.20F + 0.70F * f * f;

        float r = wr_ + (hr - wr_) * w;
        float g = wg_ + (hg - wg_) * w;
        float b = wb_ + (hb - wb_) * w;

        if(!hit) {
                float sx = 0.35F, sy = 0.90F, sz = 0.45F;
                float sl = sqrtf(sx * sx + sy * sy + sz * sz);
                float dotp = (dx * sx + dy * sy + dz * sz) / sl;
                if(dotp > 0.0F) {
                        float spec = powf(dotp, 60.0F) * (100.0F + 250.0F * h);
                        r += spec;
                        g += spec;
                        b += spec;
                }
        }

        int ri = (int)fminf(r, 255.0F);
        int gi = (int)fminf(g, 255.0F);
        int bi = (int)fminf(b, 255.0F);
        return rgba(ri, gi, bi, 255);
}

static void water_mark_row_dirty(int z) {
        if(!wr.dirty_rows || z < 0 || z >= wr.size_z || wr.dirty_rows[z])
                return;
        wr.dirty_rows[z] = 1;
        wr.dirty_row_count++;
}

static bool water_buffers_ensure(void) {
        if(wr.cache && (wr.size_x != map_size_x || wr.size_z != map_size_z)) {
                free(wr.cache);
                free(wr.state);
                free(wr.render_cache);
                free(wr.render_state);
                free(wr.dirty_rows);
                wr.cache = NULL;
                wr.state = NULL;
                wr.render_cache = NULL;
                wr.render_state = NULL;
                wr.dirty_rows = NULL;
                wr.dirty_row_count = 0;
                wr.size_x = wr.size_z = 0;
                wr.render_size_x = wr.render_size_z = 0;
                wr.cursor = 0;
                wr.rows_remaining = 0;
                wr.refresh_initialized = false;
                wr.mesh_valid = false;
        }

        if(!wr.cache) {
                size_t cells = (size_t)map_size_x * (size_t)map_size_z;
                wr.cache = calloc(cells, sizeof(uint32_t));
                wr.state = calloc(cells, sizeof(uint8_t));
                wr.render_cache = calloc(cells, sizeof(uint32_t));
                wr.render_state = calloc(cells, sizeof(uint8_t));
                wr.dirty_rows = calloc((size_t)map_size_z, sizeof(uint8_t));
                if(!wr.cache || !wr.state || !wr.render_cache || !wr.render_state || !wr.dirty_rows) {
                        free(wr.cache);
                        free(wr.state);
                        free(wr.render_cache);
                        free(wr.render_state);
                        free(wr.dirty_rows);
                        wr.cache = NULL;
                        wr.state = NULL;
                        wr.render_cache = NULL;
                        wr.render_state = NULL;
                        wr.dirty_rows = NULL;
                        wr.dirty_row_count = 0;
                        return false;
                }
                wr.size_x = map_size_x;
                wr.size_z = map_size_z;
                wr.render_size_x = map_size_x;
                wr.render_size_z = map_size_z;
                wr.dirty_row_count = 0;
        }

        if(!wr.gl_init) {
                int has_normals = 0;
#ifdef OPENGL_CORE
                has_normals = water_program != 0;
#endif
                tesselator_create(&wr.tess, VERTEX_FLOAT, has_normals, 0);
                glx_displaylist_create(&wr.dl, true, has_normals != 0);
                wr.gl_init = true;
        }

        return true;
}

static int water_row_offset(unsigned int phase, int span) {
        if(phase == 0U)
                return 0;
        /* An even span has one more row on its negative side. */
        if((span & 1) == 0 && phase == (unsigned int)span - 1U)
                return -(span / 2);
        int distance = (int)((phase + 1U) / 2U);
        return (phase & 1U) ? distance : -distance;
}

static void water_slice(void) {
        int span = wr.job.span;
        float rd = wr.job.rd;
        int budget = WATER_CELL_BUDGET;
        int rows_to_process = min(span, wr.rows_remaining);
        int rows_processed = 0;

        for(; rows_processed < rows_to_process && budget > 0; rows_processed++) {
                /* Visit the eye row first, then alternate outwards. Fresh or
                   moving views therefore update nearby water before the distant
                   edge, while the persistent cursor still covers every row. */
                unsigned int phase = wr.cursor++ % (unsigned int)span;
                int z = (int)floorf(wr.job.cz) + water_row_offset(phase, span);

                float dz = z + 0.5F - wr.job.cz;
                float w2 = rd * rd - dz * dz;
                if(w2 < 0.0F)
                        continue;
                int hw = (int)sqrtf(w2) + 1;

                int xa = (int)wr.job.cx - hw;
                int xb = (int)wr.job.cx + hw + 1;

                map_read_lock();

                if(map_size_x != wr.size_x || map_size_z != wr.size_z) {
                        map_read_unlock();
                        return;
                }

                int wz = water_wrap(z, wr.size_z);
                uint32_t* row = wr.cache + (size_t)wz * wr.size_x;
                uint8_t* state_row = wr.state + (size_t)wz * wr.size_x;
                bool row_changed = false;

                for(int x = xa; x < xb; x++) {
                        int wx = water_wrap(x, wr.size_x);
                        uint32_t color = 0;
                        uint8_t state = WATER_CELL_DRY;
                        if(map_isair_nolock(wx, (int)WATER_LEVEL, wz)) {
                                color = water_cell_compute(x, z, wx, wz);
                                state = WATER_CELL_SURFACE;
                                budget--;
                        }
                        if(row[wx] != color || state_row[wx] != state) {
                                row[wx] = color;
                                state_row[wx] = state;
                                row_changed = true;
                        }
                }
                if(row_changed)
                        water_mark_row_dirty(wz);
                map_read_unlock();
        }
        wr.rows_remaining = max(wr.rows_remaining - rows_processed, 0);
}

static void* water_worker(void* arg) {
        (void)arg;
        pthread_mutex_lock(&water_lock);
        while(!wr.stop) {
                while(!wr.pending && !wr.stop)
                        pthread_cond_wait(&water_cond, &water_lock);
                if(wr.stop)
                        break;
                wr.pending = false;
                water_slice();
        }
        pthread_mutex_unlock(&water_lock);
        return NULL;
}

void water_reflection_pass(void) {
        if(!water_shader_active()) {
                wr.was_active = false;
                return;
        }

        unsigned int map_revision = water_current_map_revision();
        if(pthread_mutex_trylock(&water_lock) != 0)
                return;

        if(!water_buffers_ensure()) {
                pthread_mutex_unlock(&water_lock);
                return;
        }

        /* Publish the previous completed slice before waking the worker again.
           The old order queued work first, so water_render() usually lost the
           mutex race and could miss updates indefinitely while moving. */
        water_snapshot_refresh_locked();

        /* Preserve the last complete colors while the view changes. Clearing
           the whole map every block exposed fallback-blue strips and repeatedly
           discarded useful work during continuous movement. A new slice simply
           replaces rows as their view-dependent reflections become ready. */
        bool reflective = settings.water_shader != 0;
        float fog[3];
        fog_color_render(fog);
        int span = water_span();
        float dx = camera_x - wr.refresh_cam_x;
        float dz = camera_z - wr.refresh_cam_z;
        bool first_refresh = !wr.refresh_initialized || !wr.was_active;
        bool mode_changed = wr.refresh_initialized && reflective != wr.last_reflective;
        bool map_changed = !wr.refresh_initialized || map_revision != wr.seen_map_revision;
        bool view_changed = first_refresh || mode_changed || map_changed
                || dx * dx + dz * dz
                           > WATER_VIEW_REFRESH_DISTANCE * WATER_VIEW_REFRESH_DISTANCE
                || (reflective && fabsf(camera_y - wr.refresh_cam_y) > WATER_HEIGHT_REFRESH_DISTANCE)
                || fabsf(settings.render_distance - wr.refresh_render_distance) > 0.01F
                || fabsf(fog[0] - wr.refresh_fog[0]) > 0.001F
                || fabsf(fog[1] - wr.refresh_fog[1]) > 0.001F
                || fabsf(fog[2] - wr.refresh_fog[2]) > 0.001F;

        if(view_changed) {
                /* A target change requests one complete center-out sweep. During
                   continuous movement the cursor is deliberately preserved, so
                   repeatedly updated targets cannot starve the outer rows. */
                if(first_refresh || mode_changed)
                        wr.cursor = 0;
                wr.span = span;
                wr.rows_remaining = span;
                wr.refresh_cam_x = camera_x;
                wr.refresh_cam_y = camera_y;
                wr.refresh_cam_z = camera_z;
                wr.refresh_render_distance = settings.render_distance;
                memcpy(wr.refresh_fog, fog, sizeof(fog));
                wr.last_reflective = reflective;
                wr.seen_map_revision = map_revision;
                wr.refresh_initialized = true;
        }
        wr.was_active = true;

        if(wr.rows_remaining > 0 && !wr.thread_started) {
                wr.stop = false;
                if(pthread_create(&wr.thread, NULL, water_worker, NULL) != 0) {
                        wr.pending = false;
                        pthread_mutex_unlock(&water_lock);
                        return;
                }
                wr.thread_started = true;
        }

        if(wr.rows_remaining > 0) {
                wr.job.cx = wr.refresh_cam_x;
                wr.job.cy = wr.refresh_cam_y;
                wr.job.cz = wr.refresh_cam_z;
                wr.job.rd = wr.refresh_render_distance;
                wr.job.span = wr.span;
                wr.job.reflective = wr.last_reflective;
                memcpy(wr.job.fog, wr.refresh_fog, sizeof(wr.job.fog));
                wr.pending = true;
                pthread_cond_signal(&water_cond);
        }
        pthread_mutex_unlock(&water_lock);
}

static uint32_t water_animated_color(uint32_t color, float x, float z, float render_time) {
        float h = cell_hash((int)floorf(x), (int)floorf(z));
        float base_shimmer = 0.85F + 0.22F * h;
        float shimmer = 1.0F
                + 0.08F / base_shimmer * sinf(render_time * (0.6F + h) + h * 6.2831F);
        int r = min((int)(red(color) * shimmer), 255);
        int g = min((int)(green(color) * shimmer), 255);
        int b = min((int)(blue(color) * shimmer), 255);
        return rgba(r, g, b, alpha(color));
}

static void water_render_tile(int tx, int tz, int x0, int x1, int z0, int z1,
                              float rd, float y, float render_time, size_t* n) {
        int za = max(tz, z0);
        int zb = min(tz + WATER_TILE, z1);

        for(int z = za; z < zb; z++) {
                float dz = z + 0.5F - camera_z;
                float w2 = rd * rd - dz * dz;
                if(w2 < 0.0F)
                        continue;
                int hw = (int)sqrtf(w2) + 1;

                int xa = max(max(tx, x0), (int)camera_x - hw);
                int xb = min(min(tx + WATER_TILE, x1), (int)camera_x + hw + 1);

                int wrapped_z = water_wrap(z, wr.render_size_z);
                uint32_t* row = wr.render_cache + (size_t)wrapped_z * wr.render_size_x;
                uint8_t* state_row = wr.render_state + (size_t)wrapped_z * wr.render_size_x;

                for(int x = xa; x < xb; x++) {
                        int wrapped_x = water_wrap(x, wr.render_size_x);
                        bool tile_waves = settings.water_waves && settings.water_wave_mode == 1;
                        if(!tile_waves && state_row[wrapped_x] != WATER_CELL_SURFACE)
                                continue;

                        uint32_t c = row[wrapped_x];
                        /* A known water cell can temporarily have no fresh
                           reflection color after camera movement. Render a safe
                           water tint rather than exposing a hole. */
                        if(!(c >> 24))
                                c = rgba(60, 100, 160, 255);
#ifdef OPENGL_CORE
                        if(!water_program)
                                c = water_animated_color(c, (float)wrapped_x, (float)wrapped_z, render_time);
#else
                        c = water_animated_color(c, (float)wrapped_x, (float)wrapped_z, render_time);
#endif

                        if(settings.water_waves) {
                                float t = render_time * settings.water_wave_speed;
                                float amp = settings.water_wave_intensity;

                                if(settings.water_wave_mode == 1) {
                                        /* Tile mode: the tile acts as a single solid unit.
                                           - Wave height is computed from tile coordinate (quantized)
                                           - Color is the average of all blocks in the tile
                                           - All 5 faces (top + 4 sides) are drawn as a solid block
                                           - Sides only drawn at tile boundaries (edges of the tile)
                                             so interior blocks don't waste quads */
                                        int ts = settings.water_wave_tile_size;
                                        ts = max(1, min(ts, 4));

                                        /* Only render the block at the top-left corner of its tile.
                                           That single render draws the ENTIRE tile as one big block. */
                                        if(x % ts != 0 || z % ts != 0)
                                                continue;

                                        /* Compute tile-averaged color from all blocks in this tile */
                                        int ar = 0, ag = 0, ab = 0, acount = 0;
                                        for(int dz2 = 0; dz2 < ts && (z + dz2) < zb; dz2++) {
                                                int sample_z = water_wrap(z + dz2, wr.render_size_z);
                                                uint32_t* row2 = wr.render_cache + (size_t)sample_z * wr.render_size_x;
                                                uint8_t* state_row2 = wr.render_state + (size_t)sample_z * wr.render_size_x;
                                                for(int dx2 = 0; dx2 < ts && (x + dx2) < xb; dx2++) {
                                                        int sample_x = water_wrap(x + dx2, wr.render_size_x);
                                                        if(state_row2[sample_x] != WATER_CELL_SURFACE)
                                                                continue;
                                                        uint32_t bc = row2[sample_x];
                                                        if(!(bc >> 24))
                                                                bc = rgba(60, 100, 160, 255);
                                                        ar += red(bc);
                                                        ag += green(bc);
                                                        ab += blue(bc);
                                                        acount++;
                                                }
                                        }
                                        /* Skip tiles that contain no water cells.
                                           Without this, ar/ag/ab stay 0 and the
                                           tile would be rendered as a solid black
                                           block (rgba(0,0,0,255)). Its 4 side
                                           faces sit at tile boundaries and, thanks
                                           to GL_POLYGON_OFFSET_FILL, bleed on top
                                           of adjacent non-water blocks — visible
                                           as a grid of black lines the size of the
                                           wave tile. Skipping the tile entirely is
                                           correct: there is no water to render
                                           here. */
                                        if(acount == 0)
                                                continue;
                                        uint32_t tile_color = rgba(ar / acount, ag / acount, ab / acount, 255);
#ifdef OPENGL_CORE
                                        if(!water_program)
                                                tile_color = water_animated_color(tile_color, (float)wrapped_x,
                                                                                  (float)wrapped_z, render_time);
#else
                                        tile_color = water_animated_color(tile_color, (float)wrapped_x,
                                                                          (float)wrapped_z, render_time);
#endif

                                        /* Tile coordinate for wave */
                                        int tlx = x / ts;
                                        int tlz = z / ts;
                                        float wave = (sinf(t * 0.9F + (float)tlx * 2.8F + (float)tlz * 2.0F) * 0.12F
                                                     + sinf(t * 1.3F + (float)tlx * 1.6F + (float)tlz * 4.4F) * 0.09F
                                                     + sinf(t * 1.8F + (float)tlx * 6.0F + (float)tlz * 0.8F) * 0.06F) * amp;
                                        float yb = y - 1.0F;
                                        /* Extreme settings must not invert side faces below
                                           their base, which would flip normals and culling. */
                                        float yw = fmaxf(y + wave, yb + 0.05F);
                                        float w = (float)ts; /* tile width/depth */

                                        /* Top face (CW winding: viewed from above) */
                                        tesselator_set_color(&wr.tess, tile_color);
                                        tesselator_set_normal(&wr.tess, 0, 127, 0);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yw, z,  x, yw, z+w,  x+w, yw, z+w,  x+w, yw, z});
                                        (*n)++;

                                        /* X- side (left face, normal points -X) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.75F, green(tile_color)*0.75F, blue(tile_color)*0.75F, 255));
                                        tesselator_set_normal(&wr.tess, -127, 0, 0);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yb, z,  x, yb, z+w,  x, yw, z+w,  x, yw, z});
                                        (*n)++;

                                        /* X+ side (right face, normal points +X) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.75F, green(tile_color)*0.75F, blue(tile_color)*0.75F, 255));
                                        tesselator_set_normal(&wr.tess, 127, 0, 0);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x+w, yb, z+w,  x+w, yb, z,  x+w, yw, z,  x+w, yw, z+w});
                                        (*n)++;

                                        /* Z- side (front face, normal points -Z) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.625F, green(tile_color)*0.625F, blue(tile_color)*0.625F, 255));
                                        tesselator_set_normal(&wr.tess, 0, 0, -127);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x+w, yb, z,  x, yb, z,  x, yw, z,  x+w, yw, z});
                                        (*n)++;

                                        /* Z+ side (back face, normal points +Z) */
                                        tesselator_set_color(&wr.tess, rgba(red(tile_color)*0.625F, green(tile_color)*0.625F, blue(tile_color)*0.625F, 255));
                                        tesselator_set_normal(&wr.tess, 0, 0, 127);
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, yb, z+w,  x+w, yb, z+w,  x+w, yw, z+w,  x, yw, z+w});
                                        (*n)++;

                                        tesselator_set_color(&wr.tess, tile_color);
                                } else {
                                        /* Vertex mode (default): each vertex gets its own
                                           wave height — smooth per-vertex displacement. */
                                        tesselator_set_color(&wr.tess, c);
                                        tesselator_set_normal(&wr.tess, 0, 127, 0);
                                        #define WAVE3(vx, vz) \
                                            ((sinf(t * 0.9F + (float)(vx) * 0.7F + (float)(vz) * 0.5F) * 0.08F \
                                           + sinf(t * 1.3F + (float)(vx) * 0.4F + (float)(vz) * 1.1F) * 0.06F \
                                           + sinf(t * 1.8F + (float)(vx) * 1.5F + (float)(vz) * 0.2F) * 0.04F) * amp)
                                        float y0 = y + WAVE3(x    , z    );
                                        float y1 = y + WAVE3(x    , z + 1);
                                        float y2 = y + WAVE3(x + 1, z + 1);
                                        float y3 = y + WAVE3(x + 1, z    );
                                        #undef WAVE3
                                        tesselator_addf_simple(&wr.tess,
                                                (float[]){x, y0, z, x, y1, z + 1.0F, x + 1.0F, y2, z + 1.0F, x + 1.0F, y3, z});
                                        (*n)++;
                                }
                        } else {
                                tesselator_set_color(&wr.tess, c);
                                tesselator_set_normal(&wr.tess, 0, 127, 0);
                                tesselator_addf_simple(&wr.tess,
                                        (float[]){x, y, z, x, y, z + 1.0F, x + 1.0F, y, z + 1.0F, x + 1.0F, y, z});
                                (*n)++;
                        }
                }
        }
}

static void water_snapshot_refresh_locked(void) {
        bool changed = false;
        if(wr.snapshot_invalidated && wr.render_cache && wr.render_state
           && wr.size_x > 0 && wr.size_z > 0) {
                size_t cells = (size_t)wr.size_x * (size_t)wr.size_z;
                memset(wr.render_cache, 0, cells * sizeof(uint32_t));
                memset(wr.render_state, 0, cells * sizeof(uint8_t));
                wr.snapshot_invalidated = false;
                changed = true;
        }
        if(!wr.cache || !wr.state || !wr.render_cache || !wr.render_state || !wr.dirty_rows
           || wr.size_x <= 0 || wr.size_z <= 0)
                return;

        if(wr.dirty_row_count) {
                size_t color_row_bytes = (size_t)wr.size_x * sizeof(uint32_t);
                size_t state_row_bytes = (size_t)wr.size_x * sizeof(uint8_t);
                for(int z = 0; z < wr.size_z; z++) {
                        if(!wr.dirty_rows[z])
                                continue;
                        size_t offset = (size_t)z * (size_t)wr.size_x;
                        memcpy(wr.render_cache + offset, wr.cache + offset, color_row_bytes);
                        memcpy(wr.render_state + offset, wr.state + offset, state_row_bytes);
                        wr.dirty_rows[z] = 0;
                }
                wr.dirty_row_count = 0;
                changed = true;
        }
        wr.render_size_x = wr.size_x;
        wr.render_size_z = wr.size_z;
        if(changed)
                wr.snapshot_revision++;
}

#ifdef OPENGL_CORE
static bool water_mesh_view_changed(void) {
        for(int column = 0; column < 3; column++)
                for(int row = 0; row < 3; row++)
                        if(fabsf(wr.mesh_view[column][row] - matrix_view[column][row])
                           > WATER_MESH_ORIENTATION_EPSILON)
                                return true;
        return false;
}
#endif

void water_render(void) {
        if(!water_shader_active() || !wr.gl_init || !wr.render_cache || !wr.render_state)
                return;

        /* water_reflection_pass() publishes completed rows before it queues the
           next worker slice. Rendering only the stable snapshot here avoids a
           second mutex race in the hot draw path and never waits on raycasts. */
        if(wr.render_size_x <= 0 || wr.render_size_z <= 0)
                return;

        float rd = settings.render_distance;
        float render_time = (float)window_time();
        bool waves_enabled = settings.water_waves != 0;
#ifdef OPENGL_CORE
        float mesh_dx = camera_x - wr.mesh_camera_x;
        float mesh_dz = camera_z - wr.mesh_camera_z;
        bool rebuild_mesh = !water_program || waves_enabled || !wr.mesh_valid
                || wr.mesh_had_waves != waves_enabled
                || wr.mesh_snapshot_revision != wr.snapshot_revision
                || wr.mesh_render_distance != rd
                || mesh_dx * mesh_dx + mesh_dz * mesh_dz
                           >= WATER_MESH_RECENTER_DISTANCE * WATER_MESH_RECENTER_DISTANCE
                || fabsf(camera_y - wr.mesh_camera_y) >= WATER_MESH_HEIGHT_DISTANCE
                || memcmp(wr.mesh_projection, matrix_projection, sizeof(mat4)) != 0
                || water_mesh_view_changed();
#else
        /* Fixed-function paths have no time uniform; rebuild their colors so
           the established shimmer remains continuous rather than freezing. */
        bool rebuild_mesh = true;
#endif

        if(rebuild_mesh) {
                int span = water_span();
                int half = span / 2;
                int x0 = (int)camera_x - half;
                int z0 = (int)camera_z - half;
                int x1 = (int)camera_x + half;
                int z1 = (int)camera_z + half;
                float y = WATER_LEVEL + 0.008F;
                tesselator_clear(&wr.tess);
                size_t n = 0;

                for(int tz = z0 & ~(WATER_TILE - 1); tz < z1; tz += WATER_TILE)
                        for(int tx = x0 & ~(WATER_TILE - 1); tx < x1; tx += WATER_TILE)
                                if(camera_CubeInFrustum(tx + WATER_TILE / 2, 0.0F, tz + WATER_TILE / 2, WATER_TILE / 2, 2.0F))
                                        water_render_tile(tx, tz, x0, x1, z0, z1, rd, y, render_time, &n);

                if(!n) {
                        wr.mesh_valid = false;
                        return;
                }
        }

        matrix_push(matrix_model);
        matrix_identity(matrix_model);

#ifdef OPENGL_CORE
        bool custom_shader = false;
        bool shadow_scoped = false;
        GLint previous_program = 0;
        if(water_program) {
                previous_program = (GLint)glx_current_program();
                glx_use_program(water_program);
                custom_shader = true;
        }
#endif
        matrix_upload();

#ifdef OPENGL_CORE
        if(custom_shader) {
                float fog[3];
                fog_color_render(fog);
                glUniform1f(water_uniform_enhanced, settings.water_shader ? 1.0F : 0.0F);
                glUniform3f(water_uniform_camera, camera_x, camera_y, camera_z);
                glUniform2f(water_uniform_map_size, (float)wr.render_size_x, (float)wr.render_size_z);
                glUniform1f(water_uniform_fog_distance,
                            glx_fog && settings.render_distance > 0.0F ? 1.0F / settings.render_distance : 0.0F);
                glUniform3f(water_uniform_fog_color, fog[0], fog[1], fog[2]);
                glUniform1f(water_uniform_time, render_time);
                glUniform1f(water_uniform_waves, settings.water_waves ? 1.0F : 0.0F);
                glUniform1f(water_uniform_wave_intensity, settings.water_wave_intensity);
                glUniform1f(water_uniform_wave_speed, settings.water_wave_speed);
                glUniform1f(water_uniform_wave_mode, (float)settings.water_wave_mode);
                if(settings.water_shader) {
                        lighting_apply_program((unsigned int)water_program);
                        shadow_apply_program((unsigned int)water_program);
                        shadow_scoped = true;
                }
        }
#endif

        GLboolean polygon_offset_was_enabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLfloat previous_polygon_factor = 0.0F;
        GLfloat previous_polygon_units = 0.0F;
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previous_polygon_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previous_polygon_units);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-2.0F, -2.0F);

        if(rebuild_mesh) {
                tesselator_glx(&wr.tess, &wr.dl);
                wr.mesh_valid = true;
                wr.mesh_had_waves = waves_enabled;
                wr.mesh_snapshot_revision = wr.snapshot_revision;
                wr.mesh_render_distance = rd;
                wr.mesh_camera_x = camera_x;
                wr.mesh_camera_y = camera_y;
                wr.mesh_camera_z = camera_z;
                memcpy(wr.mesh_projection, matrix_projection, sizeof(mat4));
                memcpy(wr.mesh_view, matrix_view, sizeof(mat4));
        }
        glx_displaylist_draw(&wr.dl, GLX_DISPLAYLIST_ENHANCED);

        glPolygonOffset(previous_polygon_factor, previous_polygon_units);
        if(polygon_offset_was_enabled)
                glEnable(GL_POLYGON_OFFSET_FILL);
        else
                glDisable(GL_POLYGON_OFFSET_FILL);

#ifdef OPENGL_CORE
        if(custom_shader) {
                if(shadow_scoped)
                        shadow_finish_program();
                glx_use_program((GLuint)previous_program);
        }
#endif

        matrix_pop(matrix_model);
#ifdef OPENGL_CORE
        if(!custom_shader || previous_program)
                matrix_upload();
#else
        matrix_upload();
#endif
}
