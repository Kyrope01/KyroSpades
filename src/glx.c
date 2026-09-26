
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
#include <math.h>
#include <stdio.h>
#include <string.h>
#ifdef OPENGL_ES
#include <dlfcn.h>
#endif

#include "common.h"
#include "camera.h"
#include "config.h"
#include "map.h"
#include "matrix.h"
#include "texture.h"
#include "glx.h"

int glx_version = 0;
int glx_fog = 0;
int glx_context_version_major = 0;
int glx_context_version_minor = 0;
int gles_version = 0;
float gles_current_color[4] = {1.0F, 1.0F, 1.0F, 1.0F};
static float requested_line_width = 1.0F;

/* ── Tracked shader-program state ──────────────────────────────────────── */

#define GLX_UNIFORM_CACHE_CAPACITY 512U
#define GLX_UNIFORM_NAME_CAPACITY 48U

struct glx_uniform_cache_entry {
        GLuint program;
        uint32_t hash;
        GLint location;
        char name[GLX_UNIFORM_NAME_CAPACITY];
};

static GLuint glx_bound_program = 0;
static struct glx_uniform_cache_entry glx_uniform_cache[GLX_UNIFORM_CACHE_CAPACITY];

static void glx_program_state_reset(void) {
        glx_bound_program = 0;
        memset(glx_uniform_cache, 0, sizeof(glx_uniform_cache));
}

static uint32_t glx_uniform_hash(GLuint program, const char* name, size_t* length) {
        uint32_t hash = 2166136261U ^ (uint32_t)program;
        const unsigned char* start = (const unsigned char*)name;
        const unsigned char* cursor = start;
        while(*cursor) {
                hash ^= *cursor++;
                hash *= 16777619U;
        }
        *length = (size_t)(cursor - start);
        return hash;
}

void glx_use_program(unsigned int program) {
        if(glx_bound_program == (GLuint)program)
                return;
        glUseProgram((GLuint)program);
        glx_bound_program = (GLuint)program;
}

unsigned int glx_current_program(void) {
        return (unsigned int)glx_bound_program;
}

int glx_uniform_location(unsigned int program, const char* name) {
        GLuint object = (GLuint)program;
        size_t name_length;
        uint32_t hash;
        uint32_t first;
        size_t empty = GLX_UNIFORM_CACHE_CAPACITY;

        if(!object || !name)
                return -1;

        hash = glx_uniform_hash(object, name, &name_length);
        if(name_length >= GLX_UNIFORM_NAME_CAPACITY)
                return glGetUniformLocation(object, name);

        first = hash % GLX_UNIFORM_CACHE_CAPACITY;
        for(size_t probe = 0; probe < GLX_UNIFORM_CACHE_CAPACITY; probe++) {
                size_t index = (first + probe) % GLX_UNIFORM_CACHE_CAPACITY;
                struct glx_uniform_cache_entry* entry = &glx_uniform_cache[index];
                if(entry->program == object && entry->hash == hash && strcmp(entry->name, name) == 0)
                        return entry->location;
                if(!entry->program && empty == GLX_UNIFORM_CACHE_CAPACITY)
                        empty = index;
        }

        GLint location = glGetUniformLocation(object, name);
        if(empty < GLX_UNIFORM_CACHE_CAPACITY) {
                struct glx_uniform_cache_entry* entry = &glx_uniform_cache[empty];
                entry->program = object;
                entry->hash = hash;
                entry->location = location;
                memcpy(entry->name, name, name_length + 1U);
        }
        return location;
}

void glx_delete_program(unsigned int program) {
        GLuint object = (GLuint)program;
        if(!object)
                return;

        if(glx_bound_program == object)
                glx_use_program(0);
        for(size_t i = 0; i < GLX_UNIFORM_CACHE_CAPACITY; i++) {
                if(glx_uniform_cache[i].program == object) {
                        glx_uniform_cache[i].program = 0;
                        glx_uniform_cache[i].hash = 0;
                        glx_uniform_cache[i].name[0] = '\0';
                }
        }
        glDeleteProgram(object);
}

/* ── Programmable default shader ───────────────────────────────────────── */

#ifdef GLX_PROGRAMMABLE
static GLuint default_shader = 0;
static GLint loc_u_MVP = -1;
static GLint loc_u_Color = -1;
static GLint loc_u_HasVertexColor = -1;
static GLint loc_u_TextureEnabled = -1;
static GLint loc_u_Texture = -1;
static GLint loc_u_TexCoordScale = -1;
static GLint loc_u_TeamColor = -1;
static GLint loc_u_Model = -1;
static GLint loc_u_Camera = -1;
static GLint loc_u_FogDist = -1;
static GLint loc_u_FogColor = -1;
static GLint loc_u_LightingEnabled = -1;
static GLint loc_u_AlphaCutoff = -1;
static GLint loc_u_LightScale = -1;
static GLuint quad_vbo = 0;
static GLuint line_quad_vbo = 0;
#ifdef OPENGL_CORE
static GLuint default_vao = 0;
static GLuint quad_vao = 0;
#endif

static bool glx_programmable_active(void) {
#ifdef OPENGL_ES
        return gles_version >= 2;
#else
        return glx_version != 0;
#endif
}

static const char* default_vs =
        "attribute vec4 a_Position;\n"
        "attribute vec4 a_Color;\n"
        "attribute vec2 a_TexCoord;\n"
        "attribute vec3 a_Normal;\n"
        "uniform mat4 u_MVP;\n"
        "uniform mat4 u_Model;\n"
        "uniform vec4 u_Color;\n"
        "uniform float u_HasVertexColor;\n"
        "uniform float u_TexCoordScale;\n"
        "uniform vec4 u_TeamColor;\n"
        "uniform vec3 u_Camera;\n"
        "uniform float u_FogDist;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "varying vec3 v_WorldPosition;\n"
        "varying vec3 v_WorldNormal;\n"
        "varying float v_FogDistance;\n"
        "void main() {\n"
        "    v_Color = mix(u_Color, a_Color, u_HasVertexColor) * u_TeamColor;\n"
        "    v_TexCoord = a_TexCoord * u_TexCoordScale;\n"
        "    vec3 world = (u_Model * a_Position).xyz;\n"
        "    v_WorldPosition = world;\n"
        "    v_WorldNormal = normalize((u_Model * vec4(a_Normal, 0.0)).xyz);\n"
        "    v_FogDistance = clamp(length(world.xz - u_Camera.xz) * u_FogDist, 0.0, 1.0);\n"
        "    gl_Position = u_MVP * a_Position;\n"
        "}\n";

static const char* default_fs =
        "precision mediump float;\n"
        "varying vec4 v_Color;\n"
        "varying vec2 v_TexCoord;\n"
        "uniform sampler2D u_Texture;\n"
        "uniform float u_TextureEnabled;\n"
        "uniform vec3 u_FogColor;\n"
        "uniform float u_LightingEnabled;\n"
        "uniform float u_AlphaCutoff;\n"
        "uniform float u_LightScale;\n"
        "uniform vec3 u_SunDirection;\n"
        "uniform vec3 u_SunColor;\n"
        "uniform vec3 u_AmbientLight;\n"
        "uniform vec4 u_LightPositionRadius[4];\n"
        "uniform vec4 u_LightColorIntensity[4];\n"
        "uniform vec4 u_FlashlightDirection;\n"
        "varying vec3 v_WorldPosition;\n"
        "varying vec3 v_WorldNormal;\n"
        "varying float v_FogDistance;\n"
        "void main() {\n"
        "    vec4 tex = texture2D(u_Texture, v_TexCoord);\n"
        "    vec4 c = mix(vec4(1.0), tex, u_TextureEnabled) * v_Color;\n"
        "    if(c.a < u_AlphaCutoff) discard;\n"
        "    if(u_LightingEnabled > 0.5) {\n"
        "        vec3 normal = normalize(v_WorldNormal);\n"
        "        float sun = max(dot(normal, normalize(u_SunDirection)), 0.0);\n"
        "        vec3 light = u_AmbientLight + u_SunColor * (0.34 * sun);\n"
        "        bool flashlight_active = u_FlashlightDirection.w >= 0.0;\n"
        "        for(int i = 0; i < 4; ++i) {\n"
        "            vec4 pr = u_LightPositionRadius[i];\n"
        "            vec4 ci = u_LightColorIntensity[i];\n"
        "            if(ci.a > 0.0 && (i != 3 || !flashlight_active)) {\n"
        "                vec3 delta = pr.xyz - v_WorldPosition;\n"
        "                float distance_to_light = length(delta);\n"
        "                float distance_ratio = distance_to_light / max(pr.w, 0.0001);\n"
        "                float attenuation = clamp(1.0 - distance_ratio, 0.0, 1.0);\n"
        "                attenuation *= attenuation;\n"
        "                vec3 light_direction = delta / max(distance_to_light, 0.0001);\n"
        "                float diffuse = max(dot(normal, light_direction), 0.0);\n"
        "                light += ci.rgb * ci.a * attenuation * (0.18 + 0.82 * diffuse);\n"
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
        "            float diffuse = max(dot(normal, -ray_direction), 0.0);\n"
        "            float fill = clamp(1.0 - light_distance / 10.0, 0.0, 1.0);\n"
        "            fill = 0.30 * fill * fill;\n"
        "            light += ci.rgb * ci.a * (cone * range + fill) * (0.18 + 0.82 * diffuse);\n"
        "        }\n"
        "        c.rgb *= light * u_LightScale;\n"
        "    }\n"
        "    float fog = v_FogDistance * v_FogDistance * (3.0 - 2.0 * v_FogDistance);\n"
        "    gl_FragColor = vec4(mix(c.rgb, u_FogColor, fog), c.a);\n"
        "}\n";
#endif

/* ── Color tracking ──────────────────────────────────────────────────────── */

void glx_set_color4f(float r, float g, float b, float a) {
        gles_current_color[0] = r;
        gles_current_color[1] = g;
        gles_current_color[2] = b;
        gles_current_color[3] = a;
#ifdef OPENGL_ES
        if(gles_version < 2) {
                /* ES 1.1: call real glColor4f via dlsym to avoid macro recursion */
                static void (*real_glColor4f)(float, float, float, float) = NULL;
                static int resolved = 0;
                if(!resolved) {
                        real_glColor4f = (void (*)(float, float, float, float))dlsym(RTLD_DEFAULT, "glColor4f");
                        resolved = 1;
                }
                if(real_glColor4f)
                        real_glColor4f(r, g, b, a);
        }
#endif
}

void glx_get_current_color(float* dst) {
        dst[0] = gles_current_color[0];
        dst[1] = gles_current_color[1];
        dst[2] = gles_current_color[2];
        dst[3] = gles_current_color[3];
}

void glx_set_line_width(float width) {
        requested_line_width = fmaxf(width, 1.0F);
#ifdef OPENGL_CORE
        /* Core implementations are only required to expose width 1. 2D
           helpers expand wider lines into triangles, while native 3D lines
           stay at the universally valid width to avoid GL_INVALID_VALUE. */
        glLineWidth(1.0F);
#else
        glLineWidth(requested_line_width);
#endif
}

void glx_set_team_color(float r, float g, float b) {
#ifdef GLX_PROGRAMMABLE
        if(glx_programmable_active()) {
                GLint prog;
                prog = (GLint)glx_current_program();
                if(prog) {
                        GLint loc = glx_uniform_location(prog, "u_TeamColor");
                        if(loc >= 0)
                                glUniform4f(loc, r, g, b, 1.0F);
                }
        }
#endif
}

/* ── GL version detection ────────────────────────────────────────────────── */

static void glx_detect_context_version(void) {
#ifdef OPENGL_ES
        glx_context_version_major = gles_version;
        glx_context_version_minor = gles_version >= 2 ? 0 : 1;
#else
        const char* version = (const char*)glGetString(GL_VERSION);
        glx_context_version_major = 0;
        glx_context_version_minor = 0;
        if(version)
                sscanf(version, "%d.%d", &glx_context_version_major, &glx_context_version_minor);
#endif
}

bool glx_version_at_least(int major, int minor) {
        return glx_context_version_major > major
                || (glx_context_version_major == major && glx_context_version_minor >= minor);
}

void glx_init() {
        glx_program_state_reset();
        glx_detect_context_version();
        log_info("Renderer context capabilities: %d.%d", glx_context_version_major, glx_context_version_minor);
#ifndef OPENGL_ES
        glx_version = glx_version_at_least(2, 0);
#else
        glx_version = gles_version >= 2;
#endif

#ifdef OPENGL_CORE
        GLint profile = 0;
        if(!glx_version_at_least(3, 3)) {
                log_fatal("OpenGL Core renderer requires version 3.3 or newer (received %d.%d)",
                          glx_context_version_major, glx_context_version_minor);
                exit(1);
        }
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile);
        if(!(profile & GL_CONTEXT_CORE_PROFILE_BIT)) {
                log_fatal("OpenGL Core renderer received a non-Core context (profile mask 0x%x)", profile);
                exit(1);
        }

        /* A vertex array object is mandatory in Core. Keep one renderer-wide
           VAO bound: individual draws update its attribute descriptions. */
        glGenVertexArrays(1, &default_vao);
        if(!default_vao) {
                log_fatal("Could not create the mandatory OpenGL Core vertex array object");
                exit(1);
        }
        glBindVertexArray(default_vao);
#endif

#ifdef GLX_PROGRAMMABLE
        if(glx_programmable_active()) {
                default_shader = glx_shader(default_vs, default_fs);
                if(default_shader) {
                        loc_u_MVP = glx_uniform_location(default_shader, "u_MVP");
                        loc_u_Color = glx_uniform_location(default_shader, "u_Color");
                        loc_u_HasVertexColor = glx_uniform_location(default_shader, "u_HasVertexColor");
                        loc_u_TextureEnabled = glx_uniform_location(default_shader, "u_TextureEnabled");
                        loc_u_Texture = glx_uniform_location(default_shader, "u_Texture");
                        loc_u_TexCoordScale = glx_uniform_location(default_shader, "u_TexCoordScale");
                        loc_u_TeamColor = glx_uniform_location(default_shader, "u_TeamColor");
                        loc_u_Model = glx_uniform_location(default_shader, "u_Model");
                        loc_u_Camera = glx_uniform_location(default_shader, "u_Camera");
                        loc_u_FogDist = glx_uniform_location(default_shader, "u_FogDist");
                        loc_u_FogColor = glx_uniform_location(default_shader, "u_FogColor");
                        loc_u_LightingEnabled = glx_uniform_location(default_shader, "u_LightingEnabled");
                        loc_u_AlphaCutoff = glx_uniform_location(default_shader, "u_AlphaCutoff");
                        loc_u_LightScale = glx_uniform_location(default_shader, "u_LightScale");
                        glx_use_program(default_shader);
                        glUniform1i(loc_u_Texture, 0);
                        glUniform4f(loc_u_Color, 1.0F, 1.0F, 1.0F, 1.0F);
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                        glUniform1f(loc_u_AlphaCutoff, 0.0F);
                        glUniform1f(loc_u_LightScale, 1.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                        glUniform4f(loc_u_TeamColor, 1.0F, 1.0F, 1.0F, 1.0F);
                        glUniformMatrix4fv(loc_u_Model, 1, GL_FALSE,
                                (float[]) {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1});
                        glUniform3f(loc_u_Camera, 0.0F, 0.0F, 0.0F);
                        glUniform1f(loc_u_FogDist, 0.0F);
                        glUniform3f(loc_u_FogColor, 0.0F, 0.0F, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                        GLint sun_location = glx_uniform_location(default_shader, "u_SunDirection");
                        GLint sun_color_location = glx_uniform_location(default_shader, "u_SunColor");
                        GLint ambient_location = glx_uniform_location(default_shader, "u_AmbientLight");
                        if(sun_location >= 0) glUniform3f(sun_location, -0.5F, 0.8F, -0.3F);
                        if(sun_color_location >= 0) glUniform3f(sun_color_location, 1.0F, 0.94F, 0.82F);
                        if(ambient_location >= 0) glUniform3f(ambient_location, 0.68F, 0.72F, 0.80F);
                        glx_use_program(0);
#ifdef OPENGL_ES
                        log_info("GLES 2.0 default shader compiled (program %u)", default_shader);
#else
                        log_info("Core-compatible default shader compiled (program %u)", default_shader);
#endif
                } else {
#ifdef OPENGL_CORE
                        log_fatal("Required OpenGL Core default shader compilation failed");
                        exit(1);
#else
                        log_error("Programmable default shader compilation failed");
#endif
                }
        } else {
                log_info("GLES 1.1 fallback path");
        }
#endif
}

void glx_deinit(void) {
#ifdef GLX_PROGRAMMABLE
        if(line_quad_vbo)
                glDeleteBuffers(1, &line_quad_vbo);
        if(quad_vbo)
                glDeleteBuffers(1, &quad_vbo);
        if(default_shader) {
                glx_use_program(0);
                glx_delete_program(default_shader);
        }
        line_quad_vbo = 0;
        quad_vbo = 0;
        default_shader = 0;
#endif
#ifdef OPENGL_CORE
        if(default_vao || quad_vao)
                glBindVertexArray(0);
        if(quad_vao)
                glDeleteVertexArrays(1, &quad_vao);
        if(default_vao)
                glDeleteVertexArrays(1, &default_vao);
        quad_vao = 0;
        default_vao = 0;
#endif
        glx_program_state_reset();
}

/* ── Shader compilation (unified for desktop GL + ES 2.0) ────────────────── */

#ifdef OPENGL_CORE
static char* glx_replace_all(const char* source, const char* from, const char* to) {
        size_t source_len = strlen(source);
        size_t from_len = strlen(from);
        size_t to_len = strlen(to);
        size_t count = 0;
        const char* scan = source;
        while((scan = strstr(scan, from)) != NULL) {
                count++;
                scan += from_len;
        }

        size_t output_len = source_len - count * from_len + count * to_len;
        char* output = malloc(output_len + 1);
        if(!output)
                return NULL;

        const char* input = source;
        char* out = output;
        while((scan = strstr(input, from)) != NULL) {
                size_t prefix = (size_t)(scan - input);
                memcpy(out, input, prefix);
                out += prefix;
                memcpy(out, to, to_len);
                out += to_len;
                input = scan + from_len;
        }
        strcpy(out, input);
        return output;
}

static bool glx_replace_owned(char** source, const char* from, const char* to) {
        char* replaced = glx_replace_all(*source, from, to);
        if(!replaced)
                return false;
        free(*source);
        *source = replaced;
        return true;
}

/* Most portable shaders in the renderer use the GLES2/GLSL 1.20 spelling.
   Convert that small, well-defined vocabulary to 3.30 Core at the API boundary
   instead of maintaining duplicate copies of every post-process shader. */
static char* glx_prepare_desktop_shader(const char* source, bool fragment) {
        if(!source)
                return NULL;
        size_t source_len = strlen(source);
        char* prepared = malloc(source_len + 1);
        if(!prepared)
                return NULL;
        memcpy(prepared, source, source_len + 1);

        if(strncmp(source, "#version", 8) == 0)
                return prepared;

        if(!glx_replace_owned(&prepared, "precision mediump float;", "")
                || !glx_replace_owned(&prepared, "precision highp float;", "")
                || !glx_replace_owned(&prepared, "precision lowp float;", "")) {
                free(prepared);
                return NULL;
        }

        if(glx_version_at_least(3, 3)) {
                if(!glx_replace_owned(&prepared, "attribute", "in")
                        || !glx_replace_owned(&prepared, "varying", fragment ? "in" : "out")
                        || !glx_replace_owned(&prepared, "texture2D", "texture")
                        || (fragment && !glx_replace_owned(&prepared, "gl_FragColor", "core_FragColor"))) {
                        free(prepared);
                        return NULL;
                }
                const char* preamble = fragment
                        ? "#version 330 core\nout vec4 core_FragColor;\n"
                        : "#version 330 core\n";
                size_t preamble_len = strlen(preamble);
                size_t prepared_len = strlen(prepared);
                char* with_version = malloc(preamble_len + prepared_len + 1);
                if(!with_version) {
                        free(prepared);
                        return NULL;
                }
                memcpy(with_version, preamble, preamble_len);
                memcpy(with_version + preamble_len, prepared, prepared_len + 1);
                free(prepared);
                prepared = with_version;
        }
        return prepared;
}
#endif

int glx_shader(const char* vertex, const char* fragment) {
#ifndef OPENGL_ES
        if(!glx_version)
                return 0;
#endif
#ifdef OPENGL_CORE
        char* prepared_vertex = glx_prepare_desktop_shader(vertex, false);
        char* prepared_fragment = glx_prepare_desktop_shader(fragment, true);
        if((vertex && !prepared_vertex) || (fragment && !prepared_fragment)) {
                free(prepared_vertex);
                free(prepared_fragment);
                log_error("Out of memory while preparing desktop shader source");
                return 0;
        }
        const char* vertex_source = prepared_vertex;
        const char* fragment_source = prepared_fragment;
#else
        const char* vertex_source = vertex;
        const char* fragment_source = fragment;
#endif
        int v = 0, f = 0;
        if(vertex_source) {
                v = glCreateShader(GL_VERTEX_SHADER);
                glShaderSource(v, 1, (const GLchar* const*)&vertex_source, NULL);
                glCompileShader(v);
                GLint ok;
                glGetShaderiv(v, GL_COMPILE_STATUS, &ok);
                if(!ok) {
                        char log[1024];
                        glGetShaderInfoLog(v, sizeof(log), NULL, log);
                        log_error("Vertex shader compile error: %s", log);
                }
        }

        if(fragment_source) {
                f = glCreateShader(GL_FRAGMENT_SHADER);
                glShaderSource(f, 1, (const GLchar* const*)&fragment_source, NULL);
                glCompileShader(f);
                GLint ok;
                glGetShaderiv(f, GL_COMPILE_STATUS, &ok);
                if(!ok) {
                        char log[1024];
                        glGetShaderInfoLog(f, sizeof(log), NULL, log);
                        log_error("Fragment shader compile error: %s", log);
                }
        }

#ifdef OPENGL_CORE
        free(prepared_vertex);
        free(prepared_fragment);
#endif

        int program = glCreateProgram();
        if(v)
                glAttachShader(program, v);
        if(f)
                glAttachShader(program, f);
        glBindAttribLocation(program, 0, "a_Position");
        glBindAttribLocation(program, 1, "a_Color");
        glBindAttribLocation(program, 2, "a_TexCoord");
        glBindAttribLocation(program, 3, "a_Normal");
        glLinkProgram(program);

        GLint linked;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if(!linked) {
                char log[1024];
                glGetProgramInfoLog(program, sizeof(log), NULL, log);
                log_error("Shader link error: %s", log);
        }

        if(v) glDeleteShader(v);
        if(f) glDeleteShader(f);

        /* Previously this returned `program` unconditionally, so a shader that
           failed to compile or link still handed back a non-zero (but unusable)
           program object. Callers that gate on `if(shader)` were then fooled into
           using a dead program, whose draw call no-ops — which on iOS looked like
           "the post-processed frame never reaches the screen". Return 0 on failure
           so callers can detect it and fall back. */
        if(!linked) {
                glx_delete_program(program);
                return 0;
        }
        return program;
}

/* ── Display list abstraction ─────────────────────────────────────────────── */

static bool explicit_vertex_attributes = false;

void glx_set_explicit_attributes(bool enabled) {
        explicit_vertex_attributes = enabled;
}

bool glx_uses_explicit_attributes(void) {
        return explicit_vertex_attributes;
}

void glx_displaylist_create(struct glx_displaylist* x, bool has_color, bool has_normal) {
        x->has_color = has_color;
        x->has_normal = has_normal;
        x->has_texcoord = false;

#ifndef GLX_PROGRAMMABLE
        if(!glx_version || settings.force_displaylist) {
                x->legacy = glGenLists(1);
        } else {
                glGenBuffers(1, &x->modern);
        }
#else
        glGenBuffers(1, &x->modern);
#endif
        x->buffer_size = 0;
        x->size = 0;
}

void glx_displaylist_destroy(struct glx_displaylist* x) {
#ifndef GLX_PROGRAMMABLE
        if(!glx_version || settings.force_displaylist) {
                glDeleteLists(x->legacy, 1);
        } else {
                glDeleteBuffers(1, &x->modern);
        }
#else
        glDeleteBuffers(1, &x->modern);
#endif
}

void glx_displaylist_update(struct glx_displaylist* x, size_t size, int type, void* color, void* vertex, void* normal,
                                                        void* texcoord) {
        x->has_texcoord = (texcoord != NULL);
        x->size = size;
        /* A freshly generated VBO has no data store. Calling glBufferSubData on
           it, even for a zero-byte model/team section, is an invalid operation
           on strict drivers. There is nothing to upload or draw in this case. */
        if(size == 0)
                return;

#ifndef GLX_PROGRAMMABLE
        if(!glx_version || settings.force_displaylist) {
                glEnableClientState(GL_VERTEX_ARRAY);
                if(x->has_color)
                        glEnableClientState(GL_COLOR_ARRAY);
                if(x->has_normal)
                        glEnableClientState(GL_NORMAL_ARRAY);
                if(x->has_texcoord)
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);

                glNewList(x->legacy, GL_COMPILE);
                if(size > 0) {
                        if(x->has_color)
                                glColorPointer(4, GL_UNSIGNED_BYTE, 0, color);

                        switch(type) {
                                case GLX_DISPLAYLIST_NORMAL: glVertexPointer(3, GL_SHORT, 0, vertex); break;
                                case GLX_DISPLAYLIST_POINTS:
                                case GLX_DISPLAYLIST_ENHANCED: glVertexPointer(3, GL_FLOAT, 0, vertex); break;
                        }

                        if(x->has_normal)
                                glNormalPointer(GL_BYTE, 0, normal);
                        if(x->has_texcoord)
                                glTexCoordPointer(2, GL_FLOAT, 0, texcoord);
                        glDrawArrays((type == GLX_DISPLAYLIST_POINTS) ? GL_POINTS : GL_QUADS, 0, x->size);
                }
                glEndList();

                glDisableClientState(GL_VERTEX_ARRAY);
                if(x->has_color)
                        glDisableClientState(GL_COLOR_ARRAY);
                if(x->has_normal)
                        glDisableClientState(GL_NORMAL_ARRAY);
                if(x->has_texcoord)
                        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        } else {
#endif
                size_t len_vertex = ((type == GLX_DISPLAYLIST_NORMAL) ? sizeof(GLshort) : sizeof(GLfloat)) * 3;
                size_t len_color = x->has_color ? (sizeof(GLubyte) * 4) : 0;
                size_t len_normal = x->has_normal ? (sizeof(GLbyte) * 3) : 0;
                size_t len_texcoord = x->has_texcoord ? (sizeof(float) * 2) : 0;
                size_t required_bytes = x->size * (len_vertex + len_color + len_normal + len_texcoord);
                int grow_buffer = required_bytes > x->buffer_size;

                glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                if(grow_buffer) {
                        glBufferData(GL_ARRAY_BUFFER, required_bytes, NULL, GL_STATIC_DRAW);
                        x->buffer_size = required_bytes;
                }

                glBufferSubData(GL_ARRAY_BUFFER, 0, x->size * len_vertex, vertex);

                size_t offset = x->size * len_vertex;

                if(x->has_color) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_color, color);
                        offset += x->size * len_color;
                }

                if(x->has_normal) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_normal, normal);
                        offset += x->size * len_normal;
                }

                if(x->has_texcoord) {
                        glBufferSubData(GL_ARRAY_BUFFER, offset, x->size * len_texcoord, texcoord);
                }

                glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifndef GLX_PROGRAMMABLE
        }
#endif
}

void glx_displaylist_draw(struct glx_displaylist* x, int type) {
        if(x->size == 0)
                return;
#ifndef GLX_PROGRAMMABLE
        if(!glx_version || settings.force_displaylist) {
                if(x->has_texcoord) {
                        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                        glCallList(x->legacy);
                        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                } else {
                        glCallList(x->legacy);
                }
        } else {
#endif
                size_t len_vertex = ((type == GLX_DISPLAYLIST_NORMAL) ? sizeof(GLshort) : sizeof(GLfloat)) * 3;
                size_t len_color = x->has_color ? (sizeof(GLubyte) * 4) : 0;
                size_t len_normal = x->has_normal ? (sizeof(GLbyte) * 3) : 0;

#if defined(GLX_PROGRAMMABLE)
                bool use_explicit_attributes = glx_programmable_active() && default_shader;
#ifdef OPENGL_CORE
                GLint active_program = 0;
                active_program = (GLint)glx_current_program();
                if(use_explicit_attributes && !active_program)
                        glx_use_default_shader();
#endif
#else
                bool use_explicit_attributes = explicit_vertex_attributes;
#endif
                if(use_explicit_attributes) {
                        /* Programmable path: use fixed attribute locations 0..3. */
                        glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                        /* a_Position (location 0) — tightly packed, offset 0 */
                        glVertexAttribPointer(0, 3,
                                (type == GLX_DISPLAYLIST_NORMAL) ? GL_SHORT : GL_FLOAT,
                                GL_FALSE, 0, (void*)0);
                        glEnableVertexAttribArray(0);

                        /* a_Color (location 1) — tightly packed after vertex block */
                        if(x->has_color) {
                                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0,
                                        (void*)(x->size * len_vertex));
                                glEnableVertexAttribArray(1);
                        } else {
                                glDisableVertexAttribArray(1);
                                glVertexAttrib4f(1, 1.0F, 1.0F, 1.0F, 1.0F);
                        }
#ifdef GLX_PROGRAMMABLE
                        /* Only the programmable default shader owns this uniform. */
                        GLint cur_prog;
                        cur_prog = (GLint)glx_current_program();
                        if(cur_prog == (GLint)default_shader)
                                glUniform1f(loc_u_HasVertexColor, x->has_color ? 1.0F : 0.0F);
#endif

                        /* a_TexCoord (location 2) — tightly packed after vertex+color+normal */
                        if(x->has_texcoord) {
                                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0,
                                        (void*)(x->size * (len_vertex + len_color + len_normal)));
                                glEnableVertexAttribArray(2);
                        } else {
                                glDisableVertexAttribArray(2);
                                glVertexAttrib2f(2, 0.0F, 0.0F);
                        }
#ifdef GLX_PROGRAMMABLE
                        if(cur_prog == (GLint)default_shader) {
                                glUniform1f(loc_u_TextureEnabled, x->has_texcoord ? 1.0F : 0.0F);
                                /* Font rendering can change this on the default shader. */
                                glUniform1f(loc_u_TexCoordScale, 1.0F);
                        }
#endif

                        /* a_Normal (location 3) — for KV6 shader lighting */
                        if(x->has_normal) {
                                glVertexAttribPointer(3, 3, GL_BYTE, GL_TRUE, 0,
                                        (void*)(x->size * (len_vertex + len_color)));
                                glEnableVertexAttribArray(3);
                        } else {
                                glDisableVertexAttribArray(3);
                                glVertexAttrib3f(3, 0.0F, 1.0F, 0.0F);
                        }
#ifdef GLX_PROGRAMMABLE
                        if(cur_prog == (GLint)default_shader) {
#ifdef OPENGL_CORE
                                glUniform1f(loc_u_LightingEnabled, x->has_normal ? 1.0F : 0.0F);
#else
                                glUniform1f(loc_u_LightingEnabled,
                                            x->has_normal && settings.dynamic_lights ? 1.0F : 0.0F);
#endif
                        }
#endif

#ifdef GLX_PROGRAMMABLE
                        GLenum draw_mode = (type == GLX_DISPLAYLIST_POINTS) ? GL_POINTS : GL_TRIANGLES;
#else
                        GLenum draw_mode = (type == GLX_DISPLAYLIST_POINTS) ? GL_POINTS : GL_QUADS;
#endif
                        glDrawArrays(draw_mode, 0, x->size);

                        glDisableVertexAttribArray(0);
                        if(x->has_color) glDisableVertexAttribArray(1);
                        if(x->has_texcoord) glDisableVertexAttribArray(2);
                        if(x->has_normal) glDisableVertexAttribArray(3);
                        glBindBuffer(GL_ARRAY_BUFFER, 0);
#ifndef OPENGL_CORE
                } else {
                        /* GLES 1.1 / desktop GL 2.0+ fallback: use client-state arrays */
                        glEnableClientState(GL_VERTEX_ARRAY);
                        glBindBuffer(GL_ARRAY_BUFFER, x->modern);

                        switch(type) {
                                case GLX_DISPLAYLIST_NORMAL: glVertexPointer(3, GL_SHORT, 0, NULL); break;
                                case GLX_DISPLAYLIST_POINTS:
                                case GLX_DISPLAYLIST_ENHANCED: glVertexPointer(3, GL_FLOAT, 0, NULL); break;
                        }

                        size_t offset = x->size * len_vertex;

                        if(x->has_color) {
                                glEnableClientState(GL_COLOR_ARRAY);
                                glColorPointer(4, GL_UNSIGNED_BYTE, 0, (const void*)offset);
                                offset += x->size * len_color;
                        }

                        if(x->has_normal) {
                                glEnableClientState(GL_NORMAL_ARRAY);
                                glNormalPointer(GL_BYTE, 0, (const void*)offset);
                                offset += x->size * len_normal;
                        }

                        if(x->has_texcoord) {
                                glEnableClientState(GL_TEXTURE_COORD_ARRAY);
                                glTexCoordPointer(2, GL_FLOAT, 0, (const void*)offset);
                        }

                        glBindBuffer(GL_ARRAY_BUFFER, 0);

                        if(type == GLX_DISPLAYLIST_POINTS) {
                                glDrawArrays(GL_POINTS, 0, x->size);
                        } else {
#ifdef GLX_PROGRAMMABLE
                                glDrawArrays(GL_TRIANGLES, 0, x->size);
#else
                                glDrawArrays(GL_QUADS, 0, x->size);
#endif
                        }

                        if(x->has_texcoord)
                                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                        if(x->has_normal)
                                glDisableClientState(GL_NORMAL_ARRAY);
                        if(x->has_color)
                                glDisableClientState(GL_COLOR_ARRAY);
                        glDisableClientState(GL_VERTEX_ARRAY);
#endif
                }
#ifndef GLX_PROGRAMMABLE
        }
#endif
}

/* ── Programmable helper draw functions ────────────────────────────────────────── */

#ifdef GLX_PROGRAMMABLE
void glx_use_default_shader(void) {
        if(default_shader)
                glx_use_program(default_shader);
}

int glx_default_shader_program(void) {
        return default_shader;
}

void glx_default_shader_set_draw_state(int with_vertex_color, int texture_enabled, int lighting_enabled) {
        if(glx_programmable_active() && default_shader) {
                glUniform4fv(loc_u_Color, 1, gles_current_color);
                glUniform1f(loc_u_HasVertexColor, with_vertex_color ? 1.0F : 0.0F);
                glUniform1f(loc_u_TextureEnabled, texture_enabled ? 1.0F : 0.0F);
                glUniform1f(loc_u_LightingEnabled, lighting_enabled ? 1.0F : 0.0F);
                if(texture_enabled)
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
        }
}

void glx_default_shader_set_texcoord_scale(float scale) {
        if(glx_programmable_active() && default_shader)
                glUniform1f(loc_u_TexCoordScale, scale);
}

void glx_default_shader_set_alpha_cutoff(float threshold) {
        if(glx_programmable_active() && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_AlphaCutoff, fmaxf(threshold, 0.0F));
        }
}

void glx_default_shader_set_light_scale(float scale) {
        if(glx_programmable_active() && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_LightScale, fmaxf(scale, 0.0F));
        }
}

static void glx_ensure_quad_vbo(void) {
#ifdef OPENGL_CORE
        if(quad_vbo && quad_vao)
                return;
#else
        if(quad_vbo)
                return;
#endif
        /* Clip-space fullscreen quad with texcoords */
        static const float quad_verts[] = {
                -1.0f, -1.0f,  0.0f,  0.0f,
                 1.0f, -1.0f,  1.0f,  0.0f,
                 1.0f,  1.0f,  1.0f,  1.0f,
                -1.0f, -1.0f,  0.0f,  0.0f,
                 1.0f,  1.0f,  1.0f,  1.0f,
                -1.0f,  1.0f,  0.0f,  1.0f,
        };
#ifdef OPENGL_CORE
        GLint previous_vao = 0;
        GLint previous_array_buffer = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
        if(quad_vbo)
                glDeleteBuffers(1, &quad_vbo);
        if(quad_vao)
                glDeleteVertexArrays(1, &quad_vao);
        quad_vbo = 0;
        quad_vao = 0;
        glGenVertexArrays(1, &quad_vao);
        if(!quad_vao)
                return;
        glBindVertexArray(quad_vao);
#endif
        glGenBuffers(1, &quad_vbo);
#ifdef OPENGL_CORE
        if(!quad_vbo) {
                glBindVertexArray((GLuint)previous_vao);
                glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_buffer);
                return;
        }
#endif
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quad_verts), quad_verts, GL_STATIC_DRAW);
#ifdef OPENGL_CORE
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(2);
        glBindVertexArray((GLuint)previous_vao);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_buffer);
#else
        glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}

static void glx_ensure_line_quad_vbo(void) {
        if(line_quad_vbo)
                return;
        glGenBuffers(1, &line_quad_vbo);
}

void glx_draw_screen_quad(void) {
#if defined(GLX_PROGRAMMABLE)
        if(!default_shader)
                return;
        glx_ensure_quad_vbo();
#ifdef OPENGL_CORE
        if(!quad_vbo || !quad_vao)
                return;
        GLint previous_vao = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vao);
        glBindVertexArray(quad_vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray((GLuint)previous_vao);
#else
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(2);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glDisableVertexAttribArray(0);
        glDisableVertexAttribArray(2);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
#endif
}

#endif /* GLX_PROGRAMMABLE */

void glx_set_alpha_test(bool enabled, float threshold) {
#ifdef GLX_PROGRAMMABLE
        if(glx_programmable_active() && default_shader) {
                glx_default_shader_set_alpha_cutoff(enabled ? threshold : 0.0F);
                return;
        }
#endif
#ifndef OPENGL_CORE
        if(enabled) {
                glEnable(GL_ALPHA_TEST);
                glAlphaFunc(GL_GREATER, threshold);
        } else {
                glDisable(GL_ALPHA_TEST);
        }
#else
        (void)enabled;
        (void)threshold;
#endif
}

void glx_draw_vertices_3d(const float* vertices, size_t count, unsigned int mode) {
        if(!vertices || count == 0)
                return;
#if defined(GLX_PROGRAMMABLE)
        if(default_shader) {
                glx_use_default_shader();
                glx_default_shader_set_draw_state(0, 0, 0);
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, count * 3 * sizeof(float), vertices, GL_STREAM_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays((GLenum)mode, 0, (GLsizei)count);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
#endif
#ifndef OPENGL_CORE
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, vertices);
        glDrawArrays((GLenum)mode, 0, (GLsizei)count);
        glDisableClientState(GL_VERTEX_ARRAY);
#else
        (void)vertices;
        (void)count;
        (void)mode;
#endif
}

void glx_draw_line_2d(float x1, float y1, float x2, float y2) {
#if defined(GLX_PROGRAMMABLE)
        if(default_shader) {
                float thin_vertices[] = {x1, y1, 0.0F, x2, y2, 0.0F};
                const float* vertices = thin_vertices;
                size_t vertex_count = 2;
                GLenum draw_mode = GL_LINES;
#ifdef OPENGL_CORE
                float wide_vertices[18];
                float dx = x2 - x1;
                float dy = y2 - y1;
                float length = sqrtf(dx * dx + dy * dy);
                if(requested_line_width > 1.0F && length > 0.0001F) {
                        float px = -dy / length * requested_line_width * 0.5F;
                        float py = dx / length * requested_line_width * 0.5F;
                        const float expanded[] = {
                                x1 + px, y1 + py, 0.0F, x1 - px, y1 - py, 0.0F, x2 - px, y2 - py, 0.0F,
                                x1 + px, y1 + py, 0.0F, x2 - px, y2 - py, 0.0F, x2 + px, y2 + py, 0.0F,
                        };
                        memcpy(wide_vertices, expanded, sizeof(expanded));
                        vertices = wide_vertices;
                        vertex_count = 6;
                        draw_mode = GL_TRIANGLES;
                }
#endif
                glx_use_default_shader();
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, vertex_count * 3 * sizeof(float), vertices, GL_STREAM_DRAW);
                GLint cur_prog;
                cur_prog = (GLint)glx_current_program();
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform4fv(loc_u_Color, 1, gles_current_color);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                }
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays(draw_mode, 0, (GLsizei)vertex_count);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
#ifndef OPENGL_CORE
        {
                float verts[] = {x1, y1, x2, y2};
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_NORMAL_ARRAY);
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 0, verts);
                glDrawArrays(GL_LINES, 0, 2);
                glDisableClientState(GL_VERTEX_ARRAY);
                glEnable(GL_TEXTURE_2D);
        }
#endif
#else
        glBegin(GL_LINES);
        glVertex2f(x1, y1);
        glVertex2f(x2, y2);
        glEnd();
#endif
}

void glx_draw_ring_segment_2d(float cx, float cy, float inner_radius, float outer_radius,
                              float start_angle, float end_angle, int steps) {
        if(steps < 1 || end_angle <= start_angle || outer_radius <= inner_radius)
                return;

#if defined(GLX_PROGRAMMABLE)
        /* Two vertices (outer, inner) per angular sample, xyz each. A triangle
           strip produces a solid annular sector with identical geometry at
           every rotation, unlike thick GL lines whose rasterized width varied
           visibly by screen angle. */
        float verts[(steps + 1) * 6];
        for(int i = 0; i <= steps; i++) {
                float a = start_angle + (end_angle - start_angle) * i / steps;
                int o = i * 6;
                verts[o + 0] = cx + sinf(a) * outer_radius;
                verts[o + 1] = cy + cosf(a) * outer_radius;
                verts[o + 2] = 0.0F;
                verts[o + 3] = cx + sinf(a) * inner_radius;
                verts[o + 4] = cy + cosf(a) * inner_radius;
                verts[o + 5] = 0.0F;
        }
        if(default_shader) {
                glx_use_default_shader();
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
                GLint cur_prog;
                cur_prog = (GLint)glx_current_program();
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform4fv(loc_u_Color, 1, gles_current_color);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                }
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_TRIANGLE_STRIP, 0, (steps + 1) * 2);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
#ifndef OPENGL_CORE
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, verts);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, (steps + 1) * 2);
        glDisableClientState(GL_VERTEX_ARRAY);
        glEnable(GL_TEXTURE_2D);
#endif
#else
        glBegin(GL_TRIANGLE_STRIP);
        for(int i = 0; i <= steps; i++) {
                float a = start_angle + (end_angle - start_angle) * i / steps;
                glVertex2f(cx + sinf(a) * outer_radius, cy + cosf(a) * outer_radius);
                glVertex2f(cx + sinf(a) * inner_radius, cy + cosf(a) * inner_radius);
        }
        glEnd();
#endif
}

void glx_draw_quad_2d(float x, float y, float w, float h) {
#if defined(GLX_PROGRAMMABLE)
        if(default_shader) {
                float verts[] = {
                        x, y, 0.0f,     x, y - h, 0.0f,   x + w, y - h, 0.0f,
                        x, y, 0.0f,     x + w, y - h, 0.0f, x + w, y, 0.0f,
                };
                glx_use_default_shader();
                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
                GLint cur_prog;
                cur_prog = (GLint)glx_current_program();
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                        glUniform4fv(loc_u_Color, 1, gles_current_color);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                        glUniform1f(loc_u_TexCoordScale, 1.0F);
                }
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableVertexAttribArray(0);
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                return;
        }
#ifndef OPENGL_CORE
        {
                float verts[] = {
                        x, y,        x, y - h,     x + w, y - h,
                        x, y,        x + w, y - h, x + w, y,
                };
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                glDisable(GL_TEXTURE_2D);
                glDisableClientState(GL_TEXTURE_COORD_ARRAY);
                glDisableClientState(GL_COLOR_ARRAY);
                glDisableClientState(GL_NORMAL_ARRAY);
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(2, GL_FLOAT, 0, verts);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                glDisableClientState(GL_VERTEX_ARRAY);
                glEnable(GL_TEXTURE_2D);
        }
#endif
#else
        glBegin(GL_QUADS);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glVertex2f(x + w, y - h);
        glVertex2f(x, y - h);
        glEnd();
#endif
}

/* Vertical gradient quad — needed because GLES has no glBegin/glVertex2f
 * (immediate mode never existed in any GLES spec, ES 1.1 included), so a
 * plain glBegin(GL_QUADS) two-color quad has to become a 4-vertex
 * GL_TRIANGLE_STRIP with per-vertex color everywhere but classic desktop GL. */
void glx_draw_gradient_quad_2d(float x, float y, float w, float h, float r1, float g1, float b1, float r2, float g2,
                                                                float b2) {
#if defined(GLX_PROGRAMMABLE)
        /* Strip order top-left, bottom-left, top-right, bottom-right. */
        float pos[] = {
                x,     y,     0.0f,
                x,     y - h, 0.0f,
                x + w, y,     0.0f,
                x + w, y - h, 0.0f,
        };
        unsigned char col[] = {
                (unsigned char)(r1 * 255.0f), (unsigned char)(g1 * 255.0f), (unsigned char)(b1 * 255.0f), 255,
                (unsigned char)(r2 * 255.0f), (unsigned char)(g2 * 255.0f), (unsigned char)(b2 * 255.0f), 255,
                (unsigned char)(r1 * 255.0f), (unsigned char)(g1 * 255.0f), (unsigned char)(b1 * 255.0f), 255,
                (unsigned char)(r2 * 255.0f), (unsigned char)(g2 * 255.0f), (unsigned char)(b2 * 255.0f), 255,
        };

        if(glx_programmable_active() && default_shader) {
                glx_use_default_shader();
                unsigned char buf[4 * (3 * sizeof(float) + 4)];
                size_t voff = 4 * 3 * sizeof(float);
                memcpy(buf, pos, voff);
                memcpy(buf + voff, col, sizeof(col));

                glx_ensure_line_quad_vbo();
                glBindBuffer(GL_ARRAY_BUFFER, line_quad_vbo);
                glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_STREAM_DRAW);

                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, (void*)voff);
                glEnableVertexAttribArray(1);

                GLint cur_prog;
                cur_prog = (GLint)glx_current_program();
                if(cur_prog == (GLint)default_shader) {
                        glUniform1f(loc_u_HasVertexColor, 1.0F);
                        glUniform1f(loc_u_TextureEnabled, 0.0F);
                        glUniform1f(loc_u_LightingEnabled, 0.0F);
                }

                glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                glDisableVertexAttribArray(0);
                glDisableVertexAttribArray(1);
                glBindBuffer(GL_ARRAY_BUFFER, 0);

                if(cur_prog == (GLint)default_shader)
                        glUniform1f(loc_u_HasVertexColor, 0.0F);
                return;
        }

#ifndef OPENGL_CORE
        /* ES 1.1 fixed-function fallback */
        float posf[] = {x, y, x, y - h, x + w, y, x + w, y - h};
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glDisable(GL_TEXTURE_2D);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_NORMAL_ARRAY);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(2, GL_FLOAT, 0, posf);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, col);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisableClientState(GL_COLOR_ARRAY);
        glEnable(GL_TEXTURE_2D);
#endif
#else
        glBegin(GL_QUADS);
        glColor3f(r1, g1, b1);
        glVertex2f(x, y);
        glVertex2f(x + w, y);
        glColor3f(r2, g2, b2);
        glVertex2f(x + w, y - h);
        glVertex2f(x, y - h);
        glEnd();
#endif
}

/* ── Spherical fog ───────────────────────────────────────────────────────── */

void glx_enable_sphericalfog() {
        /* Effective fog color (darkened 10% when filmic tone mapping is on). */
        float fc[3];
        fog_color_render(fc);
#ifdef OPENGL_CORE
        if(default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 1.0F / settings.render_distance);
                glUniform3f(loc_u_FogColor, fc[0], fc[1], fc[2]);
                glUniform3f(loc_u_Camera, camera_x, camera_y, camera_z);
        }
#elif !defined(OPENGL_ES)
        if(!settings.smooth_fog) {
                glActiveTexture(GL_TEXTURE1);
                glEnable(GL_TEXTURE_2D);
                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, (float[]) {fc[0], fc[1], fc[2], 1.0F});
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_BLEND);
                glBindTexture(GL_TEXTURE_2D, texture_gradient.texture_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_EYE_LINEAR);
                glTexGenfv(GL_T, GL_EYE_PLANE,
                                   (float[]) {1.0F / settings.render_distance / 2.0F, 0.0F, 0.0F,
                                                          -camera_x / settings.render_distance / 2.0F + 0.5F});
                glTexGenfv(GL_S, GL_EYE_PLANE,
                                   (float[]) {0.0F, 0.0F, 1.0F / settings.render_distance / 2.0F,
                                                          -camera_z / settings.render_distance / 2.0F + 0.5F});
                glEnable(GL_TEXTURE_GEN_T);
                glEnable(GL_TEXTURE_GEN_S);
                glActiveTexture(GL_TEXTURE0);
        } else {
                matrix_push(matrix_model);
                matrix_identity(matrix_model);
                matrix_upload();
                matrix_pop(matrix_model);

                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT1);
                glEnable(GL_COLOR_MATERIAL);
                glColorMaterial(GL_FRONT, GL_DIFFUSE);
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, (float[]) {fc[0], fc[1], fc[2], 1.0F});

                glLightfv(GL_LIGHT1, GL_POSITION,
                                  (float[]) {camera_x, (settings.render_distance * map_size_y) / 16.0F, camera_z, 1.0F});
                glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, (float[]) {0.0F, -1.0F, 0.0F});
                glLightfv(GL_LIGHT1, GL_DIFFUSE, (float[]) {1.0F, 1.0F, 1.0F, 1.0F});
                glLightfv(GL_LIGHT1, GL_AMBIENT, (float[]) {-fc[0], -fc[1], -fc[2], 1.0F});
                glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, tan(16.0F / map_size_y) / PI * 180.0F);
                glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 128.0F);
                glNormal3f(0.0F, 1.0F, 0.0F);
        }
#elif defined(OPENGL_ES)
        if(gles_version < 2) {
                /* ES 1.1: use fixed-function fog/lighting */
                matrix_push(matrix_model);
                matrix_identity(matrix_model);
                matrix_upload();
                matrix_pop(matrix_model);

                glEnable(GL_LIGHTING);
                glEnable(GL_LIGHT1);
                glEnable(GL_COLOR_MATERIAL);
                float amb[4] = {0.0F, 0.0F, 0.0F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);

                float lpos[4] = {camera_x, (settings.render_distance * map_size_y) / 16.0F, camera_z, 1.0F};
                glLightfv(GL_LIGHT1, GL_POSITION, lpos);
                float dir[3] = {0.0F, -1.0F, 0.0F};
                glLightfv(GL_LIGHT1, GL_SPOT_DIRECTION, dir);
                float dif[4] = {0.0F, 0.0F, 0.0F, 1.0F};
                glLightfv(GL_LIGHT1, GL_DIFFUSE, dif);
                float amb2[4] = {1.0F, 1.0F, 1.0F, 1.0F};
                glLightfv(GL_LIGHT1, GL_AMBIENT, amb2);
                glLightf(GL_LIGHT1, GL_SPOT_CUTOFF, tan(16.0F / map_size_y) / PI * 180.0F);
                glLightf(GL_LIGHT1, GL_SPOT_EXPONENT, 128.0F);
                glNormal3f(0.0F, 1.0F, 0.0F);
                glEnable(GL_FOG);
                glFogf(GL_FOG_MODE, GL_LINEAR);
                glFogf(GL_FOG_START, 0.0F);
                glFogf(GL_FOG_END, settings.render_distance);
                glFogfv(GL_FOG_COLOR, (float[]) {fc[0], fc[1], fc[2], 1.0F});
        }
        if(glx_programmable_active() && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 1.0F / settings.render_distance);
                glUniform3f(loc_u_FogColor, fc[0], fc[1], fc[2]);
                glUniform3f(loc_u_Camera, camera_x, camera_y, camera_z);
        }
#endif
        glx_fog = 1;
}

void glx_disable_sphericalfog() {
#ifdef OPENGL_CORE
        if(default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 0.0F);
        }
#elif !defined(OPENGL_ES)
        if(!settings.smooth_fog) {
                glActiveTexture(GL_TEXTURE1);
                glDisable(GL_TEXTURE_GEN_T);
                glDisable(GL_TEXTURE_GEN_S);
                glBindTexture(GL_TEXTURE_2D, 0);
                glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
                glDisable(GL_TEXTURE_2D);
                glActiveTexture(GL_TEXTURE0);
        } else {
                glDisable(GL_COLOR_MATERIAL);
                glDisable(GL_LIGHT1);
                glDisable(GL_LIGHTING);
                float a[4] = {0.2F, 0.2F, 0.2F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a);
        }
#elif defined(OPENGL_ES)
        if(gles_version < 2) {
                glDisable(GL_FOG);
                glDisable(GL_COLOR_MATERIAL);
                glDisable(GL_LIGHT1);
                glDisable(GL_LIGHTING);
                float a[4] = {0.2F, 0.2F, 0.2F, 1.0F};
                glLightModelfv(GL_LIGHT_MODEL_AMBIENT, a);
        }
        if(glx_programmable_active() && default_shader) {
                glx_use_default_shader();
                glUniform1f(loc_u_FogDist, 0.0F);
        }
#endif
        glx_fog = 0;
}
