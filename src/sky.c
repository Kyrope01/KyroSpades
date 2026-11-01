/* Camera-oriented, purely visual sky. It never changes the map or fog state. */
#include <math.h>
#include "common.h"
#include "config.h"
#include "glx.h"
#include "lighting.h"
#include "map.h"
#include "matrix.h"
#include "window.h"
#include "sky.h"

static GLuint sky_program;
/* Compiling is retried a few times. The old code latched "attempted" before
   the compile result was known, so one transient failure (driver hiccup,
   context loss) removed the sky for the rest of the session. */
#define SKY_COMPILE_ATTEMPTS 3
static int sky_compile_attempts;

#if defined(OPENGL_CORE)
#define SKY_VERTEX_HEADER "#version 330 core\nlayout(location = 0) in vec2 a_Position;\nout vec2 v_Clip;\n"
#define SKY_FRAGMENT_HEADER "#version 330 core\nin vec2 v_Clip;\nout vec4 sky_color;\n#define SKY_OUT sky_color\n"
#define SKY_POSITION "a_Position"
#elif defined(OPENGL_ES)
#define SKY_VERTEX_HEADER "attribute vec2 a_Position;\nvarying vec2 v_Clip;\n"
#define SKY_FRAGMENT_HEADER "#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\nprecision mediump float;\n#endif\nvarying vec2 v_Clip;\n#define SKY_OUT gl_FragColor\n"
#define SKY_POSITION "a_Position"
#else
#define SKY_VERTEX_HEADER "#version 120\nvarying vec2 v_Clip;\n"
#define SKY_FRAGMENT_HEADER "#version 120\nvarying vec2 v_Clip;\n#define SKY_OUT gl_FragColor\n"
#define SKY_POSITION "gl_Vertex.xy"
#endif

static const char* sky_vertex_shader = SKY_VERTEX_HEADER
        "void main() { v_Clip = " SKY_POSITION "; gl_Position = vec4(v_Clip, 0.0, 1.0); }\n";

static const char* sky_fragment_shader = SKY_FRAGMENT_HEADER
        "uniform mat4 u_InverseProjection;\n"
        "uniform mat4 u_InverseView;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform float u_Daylight;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform float u_Gradient;\n"
        "uniform float u_CloudCoverage;\n"
        "uniform float u_CloudOffset;\n"
        "float hash2(vec2 p) {\n"
        "    vec3 q = fract(vec3(mod(p, 256.0).xyx) * 0.1031);\n"
        "    q += dot(q, q.yzx + 33.33);\n"
        "    return fract((q.x + q.y) * q.z);\n"
        "}\n"
        "float noise2(vec2 p) {\n"
        "    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);\n"
        "    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), f.x),\n"
        "               mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), f.x), f.y);\n"
        "}\n"
        "void main() {\n"
        "    vec4 eye = u_InverseProjection * vec4(v_Clip, -1.0, 1.0);\n"
        "    vec3 ray = normalize((u_InverseView * vec4(eye.xyz, 0.0)).xyz);\n"
        "    float height = smoothstep(0.0, 0.85, max(ray.y, 0.0));\n"
        "    vec3 zenith = mix(u_FogColor, vec3(0.18, 0.28, 0.55) * u_Daylight, u_Gradient);\n"
        "    vec3 color = mix(u_FogColor, zenith, height);\n"
        "    float sun_glow = pow(max(dot(ray, normalize(u_SunDirection)), 0.0), 32.0);\n"
        "    color += vec3(1.0, 0.85, 0.65) * sun_glow * height * u_Daylight * u_Gradient * 0.12;\n"
        /* March through a finite cloud layer instead of projecting a flat
           texture onto the sky. Occupancy is constant within 8-unit cubes;
           density blending softens their edges and gives the layer depth. */
        "    float cloud_opacity = 0.0;\n"
        "    vec3 cloud_light = vec3(0.0);\n"
        "    if(u_CloudCoverage > 0.0 && abs(ray.y) > 0.001) {\n"
        "        vec3 camera = u_InverseView[3].xyz;\n"
        "        float t1 = (96.0 - camera.y) / ray.y;\n"
        "        float t2 = (128.0 - camera.y) / ray.y;\n"
        "        float start = max(min(t1, t2), 0.0);\n"
        "        float end = min(max(t1, t2), 320.0);\n"
        "        if(end > start && start < 320.0) {\n"
        "            float step_len = (end - start) / 24.0;\n"
        "            for(int i = 0; i < 24; ++i) {\n"
        "                float distance = start + (float(i) + 0.5) * step_len;\n"
        "                vec3 p = camera + ray * distance;\n"
        "                p.x += u_CloudOffset * 8.0;\n"
        "                vec3 voxel = floor(p / 8.0);\n"
        "                float shape = noise2((voxel.xz + 0.5) * 0.20);\n"
        "                float layer = abs((voxel.y + 0.5) * 8.0 - 112.0) / 16.0;\n"
        "                float threshold = mix(0.85, 0.48, u_CloudCoverage) + layer * 0.17;\n"
        "                float density = smoothstep(threshold - 0.035, threshold + 0.065, shape);\n"
        "                density *= 1.0 - smoothstep(220.0, 320.0, distance);\n"
        "                float amount = (1.0 - exp(-density * step_len * 0.12)) * (1.0 - cloud_opacity);\n"
        "                float light = mix(0.68, 1.05, clamp((p.y - 96.0) / 32.0, 0.0, 1.0));\n"
        "                vec3 tint = mix(u_FogColor, vec3(0.85, 0.88, 0.92) * u_Daylight, 0.65);\n"
        "                cloud_light += amount * tint * light;\n"
        "                cloud_opacity += amount;\n"
        "                if(cloud_opacity > 0.98) break;\n"
        "            }\n"
        "        }\n"
        "    }\n"
        "    color = color * (1.0 - cloud_opacity) + cloud_light;\n"
        /* The sky alpha carries sun visibility to the flare pass. No depth
           buffer writes: world geometry remains in front of the sky. */
        "    SKY_OUT = vec4(color, 1.0 - cloud_opacity);\n"
        "}\n";

bool sky_render(void) {
#ifdef OPENGL_ES
        if(gles_version < 2)
                return false;
#else
        if(!glx_version)
                return false;
#endif
#ifdef GLX_PROGRAMMABLE
        if(!glx_default_shader_program())
                return false;
#endif
        if(!sky_program && sky_compile_attempts < SKY_COMPILE_ATTEMPTS) {
                ++sky_compile_attempts;
                sky_program = (GLuint)glx_shader(sky_vertex_shader, sky_fragment_shader);
        }
        if(!sky_program)
                return false;

        mat4 inverse_projection, inverse_view;
        glmc_mat4_inv(matrix_projection, inverse_projection);
        glmc_mat4_inv(matrix_view, inverse_view);
        float fog[3];
        fog_color_render(fog);
        glx_batch2d_flush();
        GLint previous_program = (GLint)glx_current_program();
        GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blend_enabled = glIsEnabled(GL_BLEND);
        GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);
        GLboolean depth_mask;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glx_use_program(sky_program);
        glUniformMatrix4fv(glx_uniform_location(sky_program, "u_InverseProjection"), 1, GL_FALSE, (float*)inverse_projection);
        glUniformMatrix4fv(glx_uniform_location(sky_program, "u_InverseView"), 1, GL_FALSE, (float*)inverse_view);
        glUniform3f(glx_uniform_location(sky_program, "u_FogColor"), fog[0], fog[1], fog[2]);
        glUniform1f(glx_uniform_location(sky_program, "u_Daylight"), lighting_sunlight_scale());
        glUniform3f(glx_uniform_location(sky_program, "u_SunDirection"), sun_dir[0], sun_dir[1], sun_dir[2]);
        glUniform1f(glx_uniform_location(sky_program, "u_Gradient"), settings.sky_gradient ? settings.sky_gradient_intensity : 0.0F);
        glUniform1f(glx_uniform_location(sky_program, "u_CloudCoverage"), settings.sky_clouds ? settings.sky_cloud_coverage : 0.0F);
        glUniform1f(glx_uniform_location(sky_program, "u_CloudOffset"), (float)fmod(window_time() * 0.05, 1280.0));
#ifdef GLX_PROGRAMMABLE
        glx_draw_screen_quad();
#else
        glx_client_state_invalidate();
        glBegin(GL_TRIANGLES);
        glVertex2f(-1.0F, -1.0F); glVertex2f(1.0F, -1.0F); glVertex2f(1.0F, 1.0F);
        glVertex2f(-1.0F, -1.0F); glVertex2f(1.0F, 1.0F); glVertex2f(-1.0F, 1.0F);
        glEnd();
#endif
        glx_use_program((GLuint)previous_program);
        glDepthMask(depth_mask);
        if(depth_enabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if(blend_enabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if(cull_enabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        return true;
}

void sky_deinit(void) {
        if(sky_program)
                glx_delete_program(sky_program);
        sky_program = 0;
        sky_compile_attempts = 0;
}
