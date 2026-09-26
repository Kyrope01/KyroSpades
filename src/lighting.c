/*
 * Forward terrain/model lighting.
 *
 * Strict GLSL 330 is used by the Core renderer, with desktop GLSL 120 and
 * OpenGL ES 2.0 variants retained for fallback builds. Up to four short-lived
 * point lights are selected per frame; OpenGL 1.x disables this subsystem.
 */

#include <math.h>
#include <string.h>

#include "common.h"
#include "camera.h"
#include "config.h"
#include "glx.h"
#include "lighting.h"
#include "map.h"
#include "matrix.h"
#include "shadow.h"
#include "texture.h"
#include "window.h"

#define LIGHTING_FLASH_CAPACITY 32
/* Tracers and the nearest session-glowing blocks share this transient queue;
 * the shader still receives only the four most relevant final lights. */
#define LIGHTING_SUBMISSION_CAPACITY 64

struct lighting_flash {
        float position[3];
        float color[3];
        float radius;
        float intensity;
        float duration;
        double started;
        unsigned int generation;
        bool active;
};

struct lighting_frame_light {
        float position[3];
        float color[3];
        float radius;
        float intensity;
        float score;
};

struct lighting_flashlight {
        float position[3];
        float direction[3];
        double turned_on;
        bool enabled;
        bool frame_active;
        bool direction_initialized;
};

static struct lighting_flash flashes[LIGHTING_FLASH_CAPACITY];
static struct lighting_frame_light submitted_lights[LIGHTING_SUBMISSION_CAPACITY];
static struct lighting_frame_light frame_lights[LIGHTING_SHADER_LIGHTS];
static int submitted_light_count;
static int frame_light_count;
static unsigned int flash_generation;
static struct lighting_flashlight flashlight;

static GLuint world_program;
static GLint previous_program;
static bool world_shader_active;
static bool world_uses_explicit_attributes;
static bool previous_explicit_attributes;
static bool world_material_maps_bound;
static GLint previous_active_texture = GL_TEXTURE0;
static GLint previous_texture1;
static GLint previous_texture2;

static GLint uniform_mvp = -1;
static GLint uniform_model = -1;
static GLint uniform_texture = -1;
static GLint uniform_texture_enabled = -1;
static GLint uniform_normal_map = -1;
static GLint uniform_material_map = -1;
static GLint uniform_material_maps_enabled = -1;
static GLint uniform_sun_direction = -1;
static GLint uniform_sun_color = -1;
static GLint uniform_camera = -1;
static GLint uniform_fog_distance = -1;
static GLint uniform_fog_color = -1;
static GLint uniform_light_position_radius = -1;
static GLint uniform_light_color_intensity = -1;
static GLint uniform_flashlight_direction = -1;

#if defined(OPENGL_ES) || defined(OPENGL_CORE)
static const char* world_vertex_shader =
#ifdef OPENGL_CORE
        "#version 120\n"
#endif
        "attribute vec4 a_Position;\n"
        "attribute vec4 a_Color;\n"
        "attribute vec2 a_TexCoord;\n"
        "attribute vec3 a_Normal;\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying vec3 v_WorldPosition;\n"
        "varying vec3 v_WorldNormal;\n"
        "void main(void) {\n"
        "    vec4 world = u_Model * a_Position;\n"
        "    v_Color = a_Color;\n"
        "    v_TexCoord = a_TexCoord;\n"
        "    v_WorldPosition = world.xyz;\n"
        "    v_WorldNormal = normalize((u_Model * vec4(a_Normal, 0.0)).xyz);\n"
        "    gl_Position = u_MVP * a_Position;\n"
        "}\n";
#else
/* Compatibility built-ins let this shader consume the renderer's existing
 * glVertexPointer/glColorPointer/glNormalPointer arrays.  That is intentional:
 * replacing those arrays with VAOs is a later, independently testable step. */
static const char* world_vertex_shader =
        "#version 120\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying vec3 v_WorldPosition;\n"
        "varying vec3 v_WorldNormal;\n"
        "void main(void) {\n"
        "    vec4 world = u_Model * gl_Vertex;\n"
        "    v_Color = gl_Color;\n"
        "    v_TexCoord = gl_MultiTexCoord0.xy;\n"
        "    v_WorldPosition = world.xyz;\n"
        "    v_WorldNormal = normalize((u_Model * vec4(gl_Normal, 0.0)).xyz);\n"
        "    gl_Position = u_MVP * gl_Vertex;\n"
        "}\n";
#endif

static const char* world_fragment_shader =
#ifdef OPENGL_ES
        "precision mediump float;\n"
#else
        "#version 120\n"
#endif
        "uniform sampler2D u_Texture;\n"
        "uniform float u_TextureEnabled;\n"
        "uniform sampler2D u_NormalMap;\n"
        "uniform sampler2D u_MaterialMap;\n"
        "uniform float u_MaterialMapsEnabled;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform vec3 u_Camera;\n"
        "uniform float u_FogDistance;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform vec4 u_LightPositionRadius[4];\n"
        "uniform vec4 u_LightColorIntensity[4];\n"
        "uniform vec4 u_FlashlightDirection;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying vec3 v_WorldPosition;\n"
        "varying vec3 v_WorldNormal;\n"
        "void main(void) {\n"
        "    vec4 base = v_Color;\n"
        "    if(u_TextureEnabled > 0.5)\n"
        "        base *= texture2D(u_Texture, v_TexCoord);\n"
        "    vec3 normal = normalize(v_WorldNormal);\n"
        "    vec3 material = vec3(0.82, 0.05, 0.0);\n"
        "    if(u_MaterialMapsEnabled > 0.5) {\n"
        "        vec3 mapped = texture2D(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;\n"
        "        vec3 tangent = abs(normal.y) < 0.9 ? normalize(cross(vec3(0.0, 1.0, 0.0), normal)) : vec3(1.0, 0.0, 0.0);\n"
        "        vec3 bitangent = normalize(cross(normal, tangent));\n"
        "        normal = normalize(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z);\n"
        "        material = texture2D(u_MaterialMap, v_TexCoord).rgb;\n"
        "    }\n"
        "    vec3 added = vec3(0.0);\n"
        "    bool flashlight_active = u_FlashlightDirection.w >= 0.0;\n"
        "    for(int i = 0; i < 4; ++i) {\n"
        "        vec4 position_radius = u_LightPositionRadius[i];\n"
        "        vec4 color_intensity = u_LightColorIntensity[i];\n"
        "        if(color_intensity.a > 0.0 && (i != 3 || !flashlight_active)) {\n"
        "            vec3 to_light = position_radius.xyz - v_WorldPosition;\n"
        "            float radius = max(position_radius.w, 0.0001);\n"
        "            vec3 scaled_to_light = to_light / radius;\n"
        "            float distance_ratio = length(scaled_to_light);\n"
        "            float attenuation = clamp(1.0 - distance_ratio, 0.0, 1.0);\n"
        "            attenuation *= attenuation;\n"
        "            vec3 light_direction = scaled_to_light / max(distance_ratio, 0.0001);\n"
        "            float diffuse = max(dot(normal, light_direction), 0.0);\n"
        "            float facing = 0.18 + 0.82 * diffuse;\n"
        "            added += color_intensity.rgb * color_intensity.a * attenuation * facing;\n"
        "        }\n"
        "    }\n"
        "    if(flashlight_active) {\n"
        "        vec4 pr = u_LightPositionRadius[3];\n"
        "        vec4 ci = u_LightColorIntensity[3];\n"
        "        vec3 from_light = v_WorldPosition - pr.xyz;\n"
        "        float light_distance = length(from_light);\n"
        "        vec3 ray_direction = from_light / max(light_distance, 0.0001);\n"
        "        float cone = smoothstep(u_FlashlightDirection.w, min(u_FlashlightDirection.w + 0.12, 0.999),\n"
        "                                dot(ray_direction, normalize(u_FlashlightDirection.xyz)));\n"
        "        float range = clamp(1.0 - light_distance / max(pr.w, 0.0001), 0.0, 1.0);\n"
        "        range *= range;\n"
        "        float diffuse = max(dot(normal, -ray_direction), 0.0);\n"
        "        float fill = clamp(1.0 - light_distance / 10.0, 0.0, 1.0);\n"
        "        fill = 0.30 * fill * fill;\n"
        "        added += ci.rgb * ci.a * (cone * range + fill) * (0.18 + 0.82 * diffuse);\n"
        "    }\n"
        "    vec3 sun_direction = normalize(u_SunDirection);\n"
        "    float sun_diffuse = max(dot(normal, sun_direction), 0.0);\n"
        "    vec3 view_direction = normalize(u_Camera - v_WorldPosition);\n"
        "    vec3 half_direction = normalize(sun_direction + view_direction);\n"
        "    float shininess = mix(64.0, 6.0, clamp(material.r, 0.0, 1.0));\n"
        "    float specular = pow(max(dot(normal, half_direction), 0.0), shininess) * material.g;\n"
        "    base.rgb *= (0.86 + 0.14 * sun_diffuse) * (vec3(1.0) + added);\n"
        "    base.rgb += u_SunColor * specular + base.rgb * material.b;\n"
        "    float fog_distance = clamp(length((v_WorldPosition.xz - u_Camera.xz) * u_FogDistance), 0.0, 1.0);\n"
        "    float fog = fog_distance * fog_distance * (3.0 - 2.0 * fog_distance);\n"
        "    gl_FragColor = vec4(mix(base.rgb, u_FogColor, fog), base.a);\n"
        "}\n";

#ifndef OPENGL_ES
/* Dedicated GLSL 330 terrain shaders for the strict desktop Core path. */
static const char* world_vertex_shader_330 =
        "#version 330 core\n"
        "layout(location = 0) in vec4 a_Position;\n"
        "layout(location = 1) in vec4 a_Color;\n"
        "layout(location = 2) in vec2 a_TexCoord;\n"
        "layout(location = 3) in vec3 a_Normal;\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "out vec4 v_Color;\n"
        "out vec2 v_TexCoord;\n"
        "out vec3 v_WorldPosition;\n"
        "out vec3 v_WorldNormal;\n"
        "void main(void) {\n"
        "    vec4 world = u_Model * a_Position;\n"
        "    v_Color = a_Color;\n"
        "    v_TexCoord = a_TexCoord;\n"
        "    v_WorldPosition = world.xyz;\n"
        "    v_WorldNormal = normalize((u_Model * vec4(a_Normal, 0.0)).xyz);\n"
        "    gl_Position = u_MVP * a_Position;\n"
        "}\n";

static const char* world_fragment_shader_330 =
        "#version 330 core\n"
        "uniform sampler2D u_Texture;\n"
        "uniform float u_TextureEnabled;\n"
        "uniform sampler2D u_NormalMap;\n"
        "uniform sampler2D u_MaterialMap;\n"
        "uniform float u_MaterialMapsEnabled;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform vec3 u_Camera;\n"
        "uniform float u_FogDistance;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform vec4 u_LightPositionRadius[4];\n"
        "uniform vec4 u_LightColorIntensity[4];\n"
        "uniform vec4 u_FlashlightDirection;\n"
        "uniform sampler2D u_ShadowMap;\n"
        "uniform mat4 u_ShadowMatrix;\n"
        "uniform float u_ShadowTexel;\n"
        "uniform float u_ShadowIntensity;\n"
        "uniform float u_ShadowEnabled;\n"
        "in vec4 v_Color;\n"
        "in vec2 v_TexCoord;\n"
        "in vec3 v_WorldPosition;\n"
        "in vec3 v_WorldNormal;\n"
        "layout(location = 0) out vec4 out_Color;\n"
        "float terrain_shadow(vec3 world_position, vec3 geometric_normal, vec3 sun_direction) {\n"
        "    if(u_ShadowEnabled < 0.5) return 1.0;\n"
        "    vec4 light_clip = u_ShadowMatrix * vec4(world_position, 1.0);\n"
        "    if(abs(light_clip.w) < 0.0001) return 1.0;\n"
        "    vec3 projected = light_clip.xyz / light_clip.w * 0.5 + 0.5;\n"
        "    if(any(lessThan(projected, vec3(0.0))) || any(greaterThan(projected, vec3(1.0)))) return 1.0;\n"
        "    float slope = 1.0 - max(dot(geometric_normal, sun_direction), 0.0);\n"
        "    float bias = max(0.00018, 0.0012 * slope);\n"
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
        "void main(void) {\n"
        "    vec4 base = v_Color;\n"
        "    if(u_TextureEnabled > 0.5)\n"
        "        base *= texture(u_Texture, v_TexCoord);\n"
        "    vec3 geometric_normal = normalize(v_WorldNormal);\n"
        "    vec3 normal = geometric_normal;\n"
        "    vec3 material = vec3(0.82, 0.05, 0.0);\n"
        "    if(u_MaterialMapsEnabled > 0.5) {\n"
        "        vec3 mapped = texture(u_NormalMap, v_TexCoord).xyz * 2.0 - 1.0;\n"
        "        vec3 tangent = abs(normal.y) < 0.9 ? normalize(cross(vec3(0.0, 1.0, 0.0), normal)) : vec3(1.0, 0.0, 0.0);\n"
        "        vec3 bitangent = normalize(cross(normal, tangent));\n"
        "        normal = normalize(tangent * mapped.x + bitangent * mapped.y + normal * mapped.z);\n"
        "        material = texture(u_MaterialMap, v_TexCoord).rgb;\n"
        "    }\n"
        "    vec3 added = vec3(0.0);\n"
        "    bool flashlight_active = u_FlashlightDirection.w >= 0.0;\n"
        "    for(int i = 0; i < 4; ++i) {\n"
        "        vec4 position_radius = u_LightPositionRadius[i];\n"
        "        vec4 color_intensity = u_LightColorIntensity[i];\n"
        "        if(color_intensity.a > 0.0 && (i != 3 || !flashlight_active)) {\n"
        "            vec3 to_light = position_radius.xyz - v_WorldPosition;\n"
        "            float radius = max(position_radius.w, 0.0001);\n"
        "            vec3 scaled_to_light = to_light / radius;\n"
        "            float distance_ratio = length(scaled_to_light);\n"
        "            float attenuation = clamp(1.0 - distance_ratio, 0.0, 1.0);\n"
        "            attenuation *= attenuation;\n"
        "            vec3 light_direction = scaled_to_light / max(distance_ratio, 0.0001);\n"
        "            float diffuse = max(dot(normal, light_direction), 0.0);\n"
        "            float facing = 0.18 + 0.82 * diffuse;\n"
        "            added += color_intensity.rgb * color_intensity.a * attenuation * facing;\n"
        "        }\n"
        "    }\n"
        "    if(flashlight_active) {\n"
        "        vec4 pr = u_LightPositionRadius[3];\n"
        "        vec4 ci = u_LightColorIntensity[3];\n"
        "        vec3 from_light = v_WorldPosition - pr.xyz;\n"
        "        float light_distance = length(from_light);\n"
        "        vec3 ray_direction = from_light / max(light_distance, 0.0001);\n"
        "        float cone = smoothstep(u_FlashlightDirection.w, min(u_FlashlightDirection.w + 0.12, 0.999),\n"
        "                                dot(ray_direction, normalize(u_FlashlightDirection.xyz)));\n"
        "        float range = clamp(1.0 - light_distance / max(pr.w, 0.0001), 0.0, 1.0);\n"
        "        range *= range;\n"
        "        float diffuse = max(dot(normal, -ray_direction), 0.0);\n"
        "        float fill = clamp(1.0 - light_distance / 10.0, 0.0, 1.0);\n"
        "        fill = 0.30 * fill * fill;\n"
        "        added += ci.rgb * ci.a * (cone * range + fill) * (0.18 + 0.82 * diffuse);\n"
        "    }\n"
        "    vec3 sun_direction = normalize(u_SunDirection);\n"
        "    float sun_diffuse = max(dot(normal, sun_direction), 0.0);\n"
        "    vec3 view_direction = normalize(u_Camera - v_WorldPosition);\n"
        "    vec3 half_direction = normalize(sun_direction + view_direction);\n"
        "    float shininess = mix(64.0, 6.0, clamp(material.r, 0.0, 1.0));\n"
        "    float specular = pow(max(dot(normal, half_direction), 0.0), shininess) * material.g;\n"
        "    float shadow_visibility = terrain_shadow(v_WorldPosition, geometric_normal, sun_direction);\n"
        "    vec3 albedo = base.rgb;\n"
        "    base.rgb = albedo * ((0.86 + 0.14 * sun_diffuse) * shadow_visibility + added);\n"
        "    base.rgb += u_SunColor * specular * shadow_visibility + albedo * material.b;\n"
        "    float fog_distance = clamp(length((v_WorldPosition.xz - u_Camera.xz) * u_FogDistance), 0.0, 1.0);\n"
        "    float fog = fog_distance * fog_distance * (3.0 - 2.0 * fog_distance);\n"
        "    out_Color = vec4(mix(base.rgb, u_FogColor, fog), base.a);\n"
        "}\n";
#endif

static void lighting_cache_uniforms(void) {
        uniform_mvp = glx_uniform_location(world_program, "u_MVP");
        uniform_model = glx_uniform_location(world_program, "u_Model");
        uniform_texture = glx_uniform_location(world_program, "u_Texture");
        uniform_texture_enabled = glx_uniform_location(world_program, "u_TextureEnabled");
        uniform_normal_map = glx_uniform_location(world_program, "u_NormalMap");
        uniform_material_map = glx_uniform_location(world_program, "u_MaterialMap");
        uniform_material_maps_enabled = glx_uniform_location(world_program, "u_MaterialMapsEnabled");
        uniform_sun_direction = glx_uniform_location(world_program, "u_SunDirection");
        uniform_sun_color = glx_uniform_location(world_program, "u_SunColor");
        uniform_camera = glx_uniform_location(world_program, "u_Camera");
        uniform_fog_distance = glx_uniform_location(world_program, "u_FogDistance");
        uniform_fog_color = glx_uniform_location(world_program, "u_FogColor");
        uniform_light_position_radius = glx_uniform_location(world_program, "u_LightPositionRadius[0]");
        uniform_light_color_intensity = glx_uniform_location(world_program, "u_LightColorIntensity[0]");
        uniform_flashlight_direction = glx_uniform_location(world_program, "u_FlashlightDirection");
}

static bool lighting_world_program_required(void) {
        if(settings.dynamic_lights)
                return true;
#ifdef OPENGL_CORE
        /* Live sun shadows share the terrain shader, but are an independent
           visual setting: turning point lights off must not silently replace
           them with baked terrain-only shading. */
        return settings.shadow_quality && settings.shadow_intensity > 0.0F;
#else
        return false;
#endif
}

bool lighting_init(void) {
        memset(flashes, 0, sizeof(flashes));
        memset(&flashlight, 0, sizeof(flashlight));
        submitted_light_count = 0;
        frame_light_count = 0;
        flash_generation = 0;

        bool dynamic_enabled = settings.dynamic_lights != 0;
        bool ready = lighting_set_enabled(dynamic_enabled);
        if(!dynamic_enabled)
                log_info("Dynamic point lights disabled by configuration");
        return ready;
}

bool lighting_set_enabled(bool enabled) {
        settings.dynamic_lights = enabled ? 1 : 0;
        if(!enabled) {
                memset(flashes, 0, sizeof(flashes));
                submitted_light_count = 0;
                frame_light_count = 0;
                flash_generation = 0;
                lighting_flashlight_reset();
        }

        if(!lighting_world_program_required()) {
                if(world_shader_active)
                        lighting_world_end();
                if(world_program) {
                        glx_delete_program(world_program);
                        world_program = 0;
                }
                world_uses_explicit_attributes = false;
#ifdef GLX_PROGRAMMABLE
                /* The default model shader survives this subsystem. Clear its
                   point arrays immediately instead of waiting for another frame. */
                lighting_apply_program((unsigned int)glx_default_shader_program());
#endif
                return false;
        }
        if(world_program) {
#ifdef GLX_PROGRAMMABLE
                lighting_apply_program((unsigned int)glx_default_shader_program());
#endif
                return lighting_supported();
        }

#ifdef OPENGL_ES
        if(gles_version < 2) {
                log_warn("Dynamic lights unavailable: OpenGL ES 2.0 is required");
                settings.dynamic_lights = 0;
                return false;
        }
#else
        if(!glx_version) {
                log_warn("World lighting unavailable: OpenGL 2.0 shaders are required");
                settings.dynamic_lights = 0;
                return false;
        }
#endif

        const char* vertex_shader = world_vertex_shader;
        const char* fragment_shader = world_fragment_shader;
        bool use_glsl_330 = false;
#ifdef OPENGL_CORE
        use_glsl_330 = true;
#elif !defined(OPENGL_ES)
        /* Display lists expose compatibility built-ins rather than generic
           attributes, even when the compatibility context itself is 3.3+. */
        use_glsl_330 = glx_version_at_least(3, 3) && !settings.force_displaylist;
#endif
        world_uses_explicit_attributes = use_glsl_330;
#ifndef OPENGL_ES
        if(use_glsl_330) {
                vertex_shader = world_vertex_shader_330;
                fragment_shader = world_fragment_shader_330;
        }
#endif

        world_program = (GLuint)glx_shader(vertex_shader, fragment_shader);
        if(!world_program) {
                log_error("World-lighting shader failed; using the established terrain renderer");
                settings.dynamic_lights = 0;
                return false;
        }

        lighting_cache_uniforms();
        GLint active_program = 0;
        active_program = (GLint)glx_current_program();
        glx_use_program(world_program);
        if(uniform_texture >= 0)
                glUniform1i(uniform_texture, 0);
        if(uniform_normal_map >= 0)
                glUniform1i(uniform_normal_map, 1);
        if(uniform_material_map >= 0)
                glUniform1i(uniform_material_map, 2);
        glx_use_program((GLuint)active_program);

#ifdef OPENGL_ES
        const char* shader_path = "GLSL ES 100";
#else
        const char* shader_path = use_glsl_330 ? "GLSL 330" : "GLSL 120";
#endif
        log_info("World lighting ready (%d point-light slots, %s path)",
                 LIGHTING_SHADER_LIGHTS, shader_path);
        return lighting_supported();
}

void lighting_deinit(void) {
        if(world_shader_active)
                lighting_world_end();
        if(world_program) {
                glx_delete_program(world_program);
                world_program = 0;
        }
        memset(flashes, 0, sizeof(flashes));
        memset(submitted_lights, 0, sizeof(submitted_lights));
        memset(frame_lights, 0, sizeof(frame_lights));
        memset(&flashlight, 0, sizeof(flashlight));
        submitted_light_count = 0;
        frame_light_count = 0;
        flash_generation = 0;
        world_shader_active = false;
        world_material_maps_bound = false;
        world_uses_explicit_attributes = false;
}

bool lighting_supported(void) {
        return settings.dynamic_lights && world_program != 0;
}

bool lighting_world_supported(void) {
#ifdef OPENGL_CORE
        return world_program != 0
                && (settings.dynamic_lights
                    || (settings.shadow_quality && settings.shadow_intensity > 0.0F));
#else
        return lighting_supported();
#endif
}

bool lighting_flashlight_toggle(void) {
        if(!lighting_supported())
                return false;
        flashlight.enabled = !flashlight.enabled;
        flashlight.turned_on = window_time();
        flashlight.frame_active = false;
        flashlight.direction_initialized = false;
        return true;
}

bool lighting_flashlight_enabled(void) { return flashlight.enabled; }

void lighting_flashlight_reset(void) {
        memset(&flashlight, 0, sizeof(flashlight));
}

void lighting_flashlight_update(float dt, bool usable,
                                float x, float y, float z,
                                float direction_x, float direction_y, float direction_z) {
        flashlight.frame_active = false;
        if(!flashlight.enabled || !usable || !lighting_supported())
                return;

        float target_length = sqrtf(direction_x * direction_x + direction_y * direction_y
                                    + direction_z * direction_z);
        if(target_length < 0.0001F)
                return;
        direction_x /= target_length;
        direction_y /= target_length;
        direction_z /= target_length;

        if(!flashlight.direction_initialized) {
                flashlight.direction[0] = direction_x;
                flashlight.direction[1] = direction_y;
                flashlight.direction[2] = direction_z;
                flashlight.direction_initialized = true;
        } else {
                /* Match OpenSpades' responsive-but-weighted flashlight aim: cap
                   angular lag, then converge exponentially independent of FPS. */
                float dx = direction_x - flashlight.direction[0];
                float dy = direction_y - flashlight.direction[1];
                float dz = direction_z - flashlight.direction[2];
                float difference = sqrtf(dx * dx + dy * dy + dz * dz);
                if(difference > 0.1F) {
                        float excess = (difference - 0.1F) / difference;
                        flashlight.direction[0] += dx * excess;
                        flashlight.direction[1] += dy * excess;
                        flashlight.direction[2] += dz * excess;
                }
                float blend = 1.0F - powf(1.0e-6F, fmaxf(dt, 0.0F));
                flashlight.direction[0] += (direction_x - flashlight.direction[0]) * blend;
                flashlight.direction[1] += (direction_y - flashlight.direction[1]) * blend;
                flashlight.direction[2] += (direction_z - flashlight.direction[2]) * blend;
                float length = sqrtf(flashlight.direction[0] * flashlight.direction[0]
                                     + flashlight.direction[1] * flashlight.direction[1]
                                     + flashlight.direction[2] * flashlight.direction[2]);
                if(length > 0.0001F) {
                        flashlight.direction[0] /= length;
                        flashlight.direction[1] /= length;
                        flashlight.direction[2] /= length;
                }
        }

        /* Offset the source just ahead of and slightly below the eye so near
           surfaces do not clip the cone at the camera plane. */
        flashlight.position[0] = x + flashlight.direction[0] * 0.10F;
        flashlight.position[1] = y + flashlight.direction[1] * 0.10F - 0.05F;
        flashlight.position[2] = z + flashlight.direction[2] * 0.10F;
        flashlight.frame_active = true;
}

void lighting_add_flash(float x, float y, float z, float red, float green, float blue,
                        float radius, float intensity, float duration) {
        if(!settings.flash_lights || !lighting_supported()
           || radius <= 0.0F || intensity <= 0.0F || duration <= 0.0F)
                return;

        int slot = -1;
        unsigned int oldest_generation = ~0u;
        int oldest_slot = 0;
        for(int i = 0; i < LIGHTING_FLASH_CAPACITY; i++) {
                if(!flashes[i].active) {
                        slot = i;
                        break;
                }
                if(flashes[i].generation < oldest_generation) {
                        oldest_generation = flashes[i].generation;
                        oldest_slot = i;
                }
        }
        if(slot < 0)
                slot = oldest_slot;

        struct lighting_flash* flash = &flashes[slot];
        flash->position[0] = x;
        flash->position[1] = y;
        flash->position[2] = z;
        flash->color[0] = fmaxf(red, 0.0F);
        flash->color[1] = fmaxf(green, 0.0F);
        flash->color[2] = fmaxf(blue, 0.0F);
        flash->radius = radius;
        flash->intensity = intensity;
        flash->duration = duration;
        flash->started = window_time();
        flash->generation = ++flash_generation;
        flash->active = true;
}

void lighting_submit_point(float x, float y, float z, float red, float green, float blue,
                           float radius, float intensity) {
        if(!lighting_supported() || radius <= 0.0F || intensity <= 0.0F)
                return;

        int slot = submitted_light_count;
        if(slot >= LIGHTING_SUBMISSION_CAPACITY) {
                /* The queue is deliberately fixed-size. If a future caller can
                   exceed it, retain the strongest raw emitters rather than
                   allocating in the render loop. View relevance is still
                   decided later alongside persistent flashes. */
                slot = 0;
                float weakest = submitted_lights[0].intensity * submitted_lights[0].radius
                                * submitted_lights[0].radius;
                for(int i = 1; i < LIGHTING_SUBMISSION_CAPACITY; i++) {
                        float strength = submitted_lights[i].intensity * submitted_lights[i].radius
                                         * submitted_lights[i].radius;
                        if(strength < weakest) {
                                weakest = strength;
                                slot = i;
                        }
                }
                if(intensity * radius * radius <= weakest)
                        return;
        } else {
                submitted_light_count++;
        }

        struct lighting_frame_light* light = &submitted_lights[slot];
        light->position[0] = x;
        light->position[1] = y;
        light->position[2] = z;
        light->color[0] = fmaxf(red, 0.0F);
        light->color[1] = fmaxf(green, 0.0F);
        light->color[2] = fmaxf(blue, 0.0F);
        light->radius = radius;
        light->intensity = intensity;
        light->score = 0.0F;
}

static void lighting_insert_frame_light(const struct lighting_frame_light* candidate) {
        /* Slot 3 becomes the local spotlight while it is active. The same
           bounded selection still picks the three most relevant world lights. */
        int capacity = flashlight.frame_active ? LIGHTING_SHADER_LIGHTS - 1 : LIGHTING_SHADER_LIGHTS;
        if(frame_light_count < capacity) {
                frame_lights[frame_light_count++] = *candidate;
                return;
        }

        int weakest = 0;
        for(int i = 1; i < frame_light_count; i++)
                if(frame_lights[i].score < frame_lights[weakest].score)
                        weakest = i;

        if(candidate->score > frame_lights[weakest].score)
                frame_lights[weakest] = *candidate;
}

static void lighting_consider_point(const struct lighting_frame_light* source,
                                    float view_x, float view_y, float view_z) {
        struct lighting_frame_light candidate = *source;
        /* The map wraps in X/Z. Chunks across an edge are rendered as
           translated copies, so move each light to the corresponding nearest
           copy as well or lights would disappear at seams. */
        if(map_size_x > 0)
                candidate.position[0] += roundf((view_x - candidate.position[0]) / map_size_x) * map_size_x;
        if(map_size_z > 0)
                candidate.position[2] += roundf((view_z - candidate.position[2]) / map_size_z) * map_size_z;

        float dx = candidate.position[0] - view_x;
        float dy = candidate.position[1] - view_y;
        float dz = candidate.position[2] - view_z;
        float distance_sq = dx * dx + dy * dy + dz * dz;
        float radius_sq = candidate.radius * candidate.radius;
        /* Prefer bright nearby lights, but do not discard a large light merely
           because its centre is just outside its own radius. */
        candidate.score = candidate.intensity * radius_sq / (radius_sq + distance_sq);
        lighting_insert_frame_light(&candidate);
}

void lighting_prepare_frame(double now, float view_x, float view_y, float view_z) {
        int pending_count = submitted_light_count;
        submitted_light_count = 0;
        frame_light_count = 0;
        if(!lighting_supported())
                return;

        if(settings.flash_lights) {
                for(int i = 0; i < LIGHTING_FLASH_CAPACITY; i++) {
                        struct lighting_flash* flash = &flashes[i];
                        if(!flash->active)
                                continue;

                        float age = (float)(now - flash->started);
                        if(age < 0.0F)
                                age = 0.0F;
                        if(age >= flash->duration) {
                                flash->active = false;
                                continue;
                        }

                        float life = 1.0F - age / flash->duration;
                        struct lighting_frame_light source;
                        memcpy(source.position, flash->position, sizeof(source.position));
                        memcpy(source.color, flash->color, sizeof(source.color));
                        source.radius = flash->radius;
                        source.intensity = flash->intensity * life * life;
                        source.score = 0.0F;
                        lighting_consider_point(&source, view_x, view_y, view_z);
                }
        } else {
                /* Make the user-facing toggle immediate instead of allowing a
                   previously queued explosion to linger for its remaining life. */
                memset(flashes, 0, sizeof(flashes));
        }

        for(int i = 0; i < pending_count; i++)
                lighting_consider_point(&submitted_lights[i], view_x, view_y, view_z);
}

static void lighting_build_uniform_arrays(float* position_radius, float* color_intensity,
                                          float flashlight_direction[4]) {
        memset(position_radius, 0, sizeof(float) * LIGHTING_SHADER_LIGHTS * 4);
        memset(color_intensity, 0, sizeof(float) * LIGHTING_SHADER_LIGHTS * 4);
        flashlight_direction[0] = 0.0F;
        flashlight_direction[1] = 0.0F;
        flashlight_direction[2] = 1.0F;
        flashlight_direction[3] = -1.0F;
        if(!lighting_supported())
                return;

        float intensity_multiplier = fmaxf(1.0F, fminf(10.0F, settings.dynamic_light_intensity));
        for(int i = 0; i < frame_light_count; i++) {
                position_radius[i * 4 + 0] = frame_lights[i].position[0];
                position_radius[i * 4 + 1] = frame_lights[i].position[1];
                position_radius[i * 4 + 2] = frame_lights[i].position[2];
                position_radius[i * 4 + 3] = frame_lights[i].radius;
                color_intensity[i * 4 + 0] = frame_lights[i].color[0];
                color_intensity[i * 4 + 1] = frame_lights[i].color[1];
                color_intensity[i * 4 + 2] = frame_lights[i].color[2];
                color_intensity[i * 4 + 3] = frame_lights[i].intensity * intensity_multiplier;
        }

        if(flashlight.frame_active) {
                const int i = LIGHTING_SHADER_LIGHTS - 1;
                float age = fmaxf((float)(window_time() - flashlight.turned_on), 0.0F);
                float fade = 1.0F - expf(-age * 5.0F);
                float brightness = 1.5F;
#ifdef OPENGL_CORE
                if(settings.hdr_rendering)
                        brightness = 3.0F;
#endif
                position_radius[i * 4 + 0] = flashlight.position[0];
                position_radius[i * 4 + 1] = flashlight.position[1];
                position_radius[i * 4 + 2] = flashlight.position[2];
                position_radius[i * 4 + 3] = 60.0F;
                color_intensity[i * 4 + 0] = 1.0F;
                color_intensity[i * 4 + 1] = 0.70F;
                color_intensity[i * 4 + 2] = 0.50F;
                color_intensity[i * 4 + 3] = brightness * fade * intensity_multiplier;
                flashlight_direction[0] = flashlight.direction[0];
                flashlight_direction[1] = flashlight.direction[1];
                flashlight_direction[2] = flashlight.direction[2];
                flashlight_direction[3] = 0.70710678F; /* cos(90 degree cone / 2) */
        }
}

void lighting_apply_program(unsigned int program) {
        if(!program)
                return;
        GLint previous = 0;
        previous = (GLint)glx_current_program();
        glx_use_program((GLuint)program);

        float position_radius[LIGHTING_SHADER_LIGHTS * 4];
        float color_intensity[LIGHTING_SHADER_LIGHTS * 4];
        float flashlight_direction[4];
        lighting_build_uniform_arrays(position_radius, color_intensity, flashlight_direction);
        GLint location = glx_uniform_location((GLuint)program, "u_LightPositionRadius[0]");
        if(location >= 0)
                glUniform4fv(location, LIGHTING_SHADER_LIGHTS, position_radius);
        location = glx_uniform_location((GLuint)program, "u_LightColorIntensity[0]");
        if(location >= 0)
                glUniform4fv(location, LIGHTING_SHADER_LIGHTS, color_intensity);
        location = glx_uniform_location((GLuint)program, "u_FlashlightDirection");
        if(location >= 0)
                glUniform4fv(location, 1, flashlight_direction);

        float sun_length = sqrtf(sun_dir[0] * sun_dir[0] + sun_dir[1] * sun_dir[1] + sun_dir[2] * sun_dir[2]);
        if(sun_length < 0.0001F)
                sun_length = 1.0F;
        location = glx_uniform_location((GLuint)program, "u_SunDirection");
        if(location >= 0)
                glUniform3f(location, sun_dir[0] / sun_length, sun_dir[1] / sun_length, sun_dir[2] / sun_length);
        location = glx_uniform_location((GLuint)program, "u_SunColor");
        if(location >= 0)
                glUniform3f(location, 1.0F, 0.94F, 0.82F);
        location = glx_uniform_location((GLuint)program, "u_AmbientLight");
        if(location >= 0)
                glUniform3f(location, 0.68F, 0.72F, 0.80F);

        glx_use_program((GLuint)previous);
}

void lighting_world_update_matrices(void) {
        if(!world_shader_active || !world_program)
                return;

        mat4 model_view;
        mat4 mvp;
        glmc_mat4_mul(matrix_view, matrix_model, model_view);
        glmc_mat4_mul(matrix_projection, model_view, mvp);
        if(uniform_mvp >= 0)
                glUniformMatrix4fv(uniform_mvp, 1, GL_FALSE, (float*)mvp);
        if(uniform_model >= 0)
                glUniformMatrix4fv(uniform_model, 1, GL_FALSE, (float*)matrix_model);
}

bool lighting_world_begin(bool textured) {
        if(!lighting_world_supported() || world_shader_active)
                return false;
        world_material_maps_bound = false;
        bool has_material_maps = textured && texture_blocks_materials_ready;
        /* Keep one stable terrain baseline while lighting is enabled. Switching
           shaders only for the lifetime of a flash changes the global sunlight
           response and causes a visible full-world brightness pulse. */
        /* The GLSL 330 shader consumes generic attributes and therefore cannot
         * draw legacy display lists.  Keep that opt-in compatibility setting on
         * the proven GLSL 120 path instead of producing missing terrain. */
#ifndef OPENGL_CORE
        if(world_uses_explicit_attributes && settings.force_displaylist)
                return false;
#endif

        previous_program = (GLint)glx_current_program();
        previous_explicit_attributes = glx_uses_explicit_attributes();
        if(world_uses_explicit_attributes)
                glx_set_explicit_attributes(true);
        glx_use_program(world_program);
        world_shader_active = true;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
        glActiveTexture(GL_TEXTURE0);

        float position_radius[LIGHTING_SHADER_LIGHTS * 4];
        float color_intensity[LIGHTING_SHADER_LIGHTS * 4];
        float flashlight_direction[4];
        lighting_build_uniform_arrays(position_radius, color_intensity, flashlight_direction);

        if(uniform_light_position_radius >= 0)
                glUniform4fv(uniform_light_position_radius, LIGHTING_SHADER_LIGHTS, position_radius);
        if(uniform_light_color_intensity >= 0)
                glUniform4fv(uniform_light_color_intensity, LIGHTING_SHADER_LIGHTS, color_intensity);
        if(uniform_flashlight_direction >= 0)
                glUniform4fv(uniform_flashlight_direction, 1, flashlight_direction);
        if(uniform_texture_enabled >= 0)
                glUniform1f(uniform_texture_enabled, textured ? 1.0F : 0.0F);
        bool use_material_maps = has_material_maps;
        world_material_maps_bound = use_material_maps;
        if(uniform_material_maps_enabled >= 0)
                glUniform1f(uniform_material_maps_enabled, use_material_maps ? 1.0F : 0.0F);
        if(use_material_maps) {
                /* Texture unit 1 can hold the legacy spherical-fog gradient.
                   Preserve both auxiliary bindings instead of silently breaking
                   water/model draws when the terrain pass ends. */
                glActiveTexture(GL_TEXTURE1);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture1);
                glBindTexture(GL_TEXTURE_2D, texture_blocks_normal.texture_id);
                glActiveTexture(GL_TEXTURE2);
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture2);
                glBindTexture(GL_TEXTURE_2D, texture_blocks_material.texture_id);
                glActiveTexture(GL_TEXTURE0);
        }
        float sun_length = sqrtf(sun_dir[0] * sun_dir[0] + sun_dir[1] * sun_dir[1] + sun_dir[2] * sun_dir[2]);
        if(sun_length < 0.0001F) sun_length = 1.0F;
        if(uniform_sun_direction >= 0)
                glUniform3f(uniform_sun_direction, sun_dir[0] / sun_length, sun_dir[1] / sun_length, sun_dir[2] / sun_length);
        if(uniform_sun_color >= 0)
                glUniform3f(uniform_sun_color, 1.0F, 0.94F, 0.82F);
        if(uniform_camera >= 0)
                glUniform3f(uniform_camera, camera_x, camera_y, camera_z);
        if(uniform_fog_distance >= 0)
                glUniform1f(uniform_fog_distance,
                            glx_fog && settings.render_distance > 0.0F ? 1.0F / settings.render_distance : 0.0F);
        if(uniform_fog_color >= 0) {
                float color[3];
                fog_color_render(color);
                glUniform3f(uniform_fog_color, color[0], color[1], color[2]);
        }
        lighting_world_update_matrices();
        shadow_apply_program((unsigned int)world_program);
        return true;
}

void lighting_world_end(void) {
        if(!world_shader_active)
                return;
        shadow_finish_program();
        if(world_material_maps_bound) {
                glActiveTexture(GL_TEXTURE2);
                glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture2);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture1);
                world_material_maps_bound = false;
        }
        glActiveTexture((GLenum)previous_active_texture);
        glx_use_program((GLuint)previous_program);
        glx_set_explicit_attributes(previous_explicit_attributes);
        world_shader_active = false;
}
