/*
 * Directional terrain shadows for the strict OpenGL 3.3 renderer.
 *
 * The Core path caches nearby wrapped terrain in a stabilized depth map and
 * samples it from the forward terrain shader. The map is redrawn only when its
 * coverage or terrain changes. Legacy desktop, GLES, failed GPU setup, and a
 * disabled forward-lighting path retain the established baked fallback.
 */

#include <math.h>
#include <string.h>

#include "common.h"
#include "camera.h"
#include "chunk.h"
#include "config.h"
#include "glx.h"
#include "lighting.h"
#include "map.h"
#include "matrix.h"
#include "shadow.h"

#ifdef OPENGL_CORE

#define SHADOW_MAP_SIZE 2048
#define SHADOW_MIN_SIZE 512
/* A cached world-space depth map remains exact while the camera moves inside
 * it. Recenter only after enough movement to threaten useful edge coverage. */
#define SHADOW_RECENTER_DISTANCE 4.0F

static GLuint shadow_program;
static GLuint shadow_fbo;
static GLuint shadow_texture;
static GLuint shadow_vao;
static GLint shadow_uniform_mvp = -1;
static int shadow_size;
static bool shadow_ready;
static bool shadow_map_valid;
static bool shadow_dirty = true;
static float shadow_center[3];
static float shadow_cached_sun[3];
static float shadow_cached_extent;
static mat4 shadow_matrix;

static bool shadow_scope_bound;
static GLint shadow_scope_active_texture = GL_TEXTURE0;
static GLint shadow_scope_texture3;

static const char* shadow_vertex_shader =
        "#version 330 core\n"
        "layout(location = 0) in vec3 a_Position;\n"
        "uniform mat4 u_MVP;\n"
        "void main(void) {\n"
        "    gl_Position = u_MVP * vec4(a_Position, 1.0);\n"
        "}\n";

static const char* shadow_fragment_shader =
        "#version 330 core\n"
        "void main(void) { }\n";

static void shadow_restore_capability(GLenum capability, GLboolean enabled) {
        if(enabled)
                glEnable(capability);
        else
                glDisable(capability);
}

static bool shadow_live_enabled(void) {
        return shadow_ready && shadow_map_valid && settings.shadow_quality
                && settings.shadow_intensity > 0.0F && lighting_world_supported();
}

bool shadow_init(void) {
        if(shadow_program || shadow_fbo || shadow_texture || shadow_vao)
                shadow_deinit();

        shadow_ready = false;
        shadow_map_valid = false;
        shadow_dirty = true;
        shadow_size = 0;
        shadow_uniform_mvp = -1;
        memset(shadow_center, 0, sizeof(shadow_center));
        memset(shadow_cached_sun, 0, sizeof(shadow_cached_sun));
        shadow_cached_extent = 0.0F;
        matrix_identity(shadow_matrix);

        if(!glx_version_at_least(3, 3)) {
                log_info("GPU shadows unavailable without OpenGL 3.3 Core; using baked shadows");
                return false;
        }

        shadow_program = (GLuint)glx_shader(shadow_vertex_shader, shadow_fragment_shader);
        if(!shadow_program) {
                log_error("GPU shadow depth shader failed; using baked shadows");
                return false;
        }
        shadow_uniform_mvp = glx_uniform_location(shadow_program, "u_MVP");
        if(shadow_uniform_mvp < 0) {
                log_error("GPU shadow depth shader is missing u_MVP; using baked shadows");
                shadow_deinit();
                return false;
        }

        /* Keep depth-pass attribute state out of the renderer-wide Core VAO. */
        glGenVertexArrays(1, &shadow_vao);
        if(!shadow_vao) {
                log_error("GPU shadow vertex array allocation failed; using baked shadows");
                shadow_deinit();
                return false;
        }

        GLint max_texture_size = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_texture_size);
        shadow_size = min(SHADOW_MAP_SIZE, max_texture_size);
        if(shadow_size < SHADOW_MIN_SIZE) {
                log_warn("GPU shadow map unavailable: maximum texture size is %i", max_texture_size);
                shadow_deinit();
                return false;
        }

        GLint previous_active_texture = GL_TEXTURE0;
        GLint previous_texture0 = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture0);

        glGenTextures(1, &shadow_texture);
        if(!shadow_texture) {
                glActiveTexture((GLenum)previous_active_texture);
                log_error("GPU shadow texture allocation failed; using baked shadows");
                shadow_deinit();
                return false;
        }
        glBindTexture(GL_TEXTURE_2D, shadow_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadow_size, shadow_size,
                     0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        /* The receiver performs its own 3x3 comparisons. Interpolating raw
           depth values before those comparisons creates false edge depths. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);

        glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture0);
        glActiveTexture((GLenum)previous_active_texture);

        GLint previous_draw_fbo = 0;
        GLint previous_read_fbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);

        glGenFramebuffers(1, &shadow_fbo);
        if(!shadow_fbo) {
                log_error("GPU shadow framebuffer allocation failed; using baked shadows");
                shadow_deinit();
                return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadow_texture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read_fbo);

        if(!shadow_texture || !shadow_fbo || status != GL_FRAMEBUFFER_COMPLETE) {
                log_error("GPU shadow framebuffer is incomplete (0x%x); using baked shadows", status);
                shadow_deinit();
                return false;
        }

        shadow_ready = true;
        log_info("GPU directional shadows ready (%ix%i, stabilized 3x3 PCF)", shadow_size, shadow_size);
        return true;
}

void shadow_deinit(void) {
        if(shadow_scope_bound)
                shadow_finish_program();
        if(shadow_fbo)
                glDeleteFramebuffers(1, &shadow_fbo);
        if(shadow_texture)
                glDeleteTextures(1, &shadow_texture);
        if(shadow_program)
                glx_delete_program(shadow_program);
        if(shadow_vao)
                glDeleteVertexArrays(1, &shadow_vao);
        shadow_fbo = 0;
        shadow_texture = 0;
        shadow_program = 0;
        shadow_vao = 0;
        shadow_uniform_mvp = -1;
        shadow_size = 0;
        shadow_ready = false;
        shadow_map_valid = false;
        shadow_dirty = true;
        shadow_scope_bound = false;
        memset(shadow_center, 0, sizeof(shadow_center));
        memset(shadow_cached_sun, 0, sizeof(shadow_cached_sun));
        shadow_cached_extent = 0.0F;
        matrix_identity(shadow_matrix);
}

bool shadow_gpu_supported(void) {
        return shadow_ready;
}

bool shadow_baked_enabled(void) {
        if(!settings.shadow_quality || settings.shadow_intensity <= 0.0F)
                return false;
        /* The live map is sampled by the forward world program. If either
           resource is unavailable, retain the established baked fallback. */
        return !shadow_ready || !lighting_world_supported();
}

void shadow_invalidate(void) { shadow_dirty = true; }

void shadow_render(void) {
        if(!shadow_ready || !settings.shadow_quality || settings.shadow_intensity <= 0.0F
           || !lighting_world_supported() || map_size_x <= 0 || map_size_y <= 0 || map_size_z <= 0) {
                shadow_map_valid = false;
                return;
        }

        float sun_length = sqrtf(sun_dir[0] * sun_dir[0] + sun_dir[1] * sun_dir[1] + sun_dir[2] * sun_dir[2]);
        float sx = 0.35F;
        float sy = 0.82F;
        float sz = 0.45F;
        if(sun_length >= 0.0001F) {
                sx = sun_dir[0] / sun_length;
                sy = sun_dir[1] / sun_length;
                sz = sun_dir[2] / sun_length;
        }

        float extent = fminf(fmaxf(settings.render_distance * 0.85F, 64.0F), 176.0F);
        float center_x = camera_x;
        float center_y = fminf(fmaxf(camera_y, 8.0F), (float)map_size_y - 4.0F);
        float center_z = camera_z;
        float center_dx = center_x - shadow_center[0];
        float center_dy = center_y - shadow_center[1];
        float center_dz = center_z - shadow_center[2];
        bool coverage_moved = center_dx * center_dx + center_dy * center_dy + center_dz * center_dz
                > SHADOW_RECENTER_DISTANCE * SHADOW_RECENTER_DISTANCE;
        bool projection_changed = fabsf(extent - shadow_cached_extent) > 0.001F
                || fabsf(sx - shadow_cached_sun[0]) > 0.0001F
                || fabsf(sy - shadow_cached_sun[1]) > 0.0001F
                || fabsf(sz - shadow_cached_sun[2]) > 0.0001F;
        if(shadow_map_valid && !shadow_dirty && !coverage_moved && !projection_changed)
                return;

        GLint previous_draw_fbo = 0;
        GLint previous_read_fbo = 0;
        GLint previous_program = 0;
        GLint previous_viewport[4] = {0};
        GLint previous_active_texture = GL_TEXTURE0;
        GLint previous_texture0 = 0;
        GLint previous_array_buffer = 0;
        GLint previous_vertex_array = 0;
        GLboolean previous_color_mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
        GLboolean previous_depth_mask = GL_TRUE;
        GLboolean blend_was_enabled = glIsEnabled(GL_BLEND);
        GLboolean depth_was_enabled = glIsEnabled(GL_DEPTH_TEST);
        GLboolean cull_was_enabled = glIsEnabled(GL_CULL_FACE);
        GLboolean scissor_was_enabled = glIsEnabled(GL_SCISSOR_TEST);
        GLboolean polygon_offset_was_enabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLint previous_depth_func = GL_LESS;
        GLint previous_cull_face = GL_BACK;
        GLfloat previous_polygon_factor = 0.0F;
        GLfloat previous_polygon_units = 0.0F;

        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_fbo);
        previous_program = (GLint)glx_current_program();
        glGetIntegerv(GL_VIEWPORT, previous_viewport);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previous_array_buffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
        glGetIntegerv(GL_DEPTH_FUNC, &previous_depth_func);
        glGetIntegerv(GL_CULL_FACE_MODE, &previous_cull_face);
        glGetBooleanv(GL_COLOR_WRITEMASK, previous_color_mask);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &previous_depth_mask);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previous_polygon_factor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previous_polygon_units);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous_texture0);

        mat4 saved_projection;
        mat4 saved_view;
        mat4 saved_model;
        memcpy(saved_projection, matrix_projection, sizeof(mat4));
        memcpy(saved_view, matrix_view, sizeof(mat4));
        memcpy(saved_model, matrix_model, sizeof(mat4));

        float light_distance = 220.0F;

        matrix_identity(matrix_projection);
        matrix_ortho(matrix_projection, -extent, extent, -extent, extent, 1.0F, 480.0F);
        matrix_identity(matrix_view);
        matrix_lookAt(matrix_view,
                      center_x + sx * light_distance,
                      center_y + sy * light_distance,
                      center_z + sz * light_distance,
                      center_x, center_y, center_z,
                      0.0F, fabsf(sy) > 0.95F ? 0.0F : 1.0F, fabsf(sy) > 0.95F ? 1.0F : 0.0F);
        matrix_identity(matrix_model);

        /* Lock the orthographic projection to shadow-map texels. Without this,
           sub-pixel camera movement makes every projected edge shimmer. */
        glmc_mat4_mul(matrix_projection, matrix_view, shadow_matrix);
        vec4 world_origin = {0.0F, 0.0F, 0.0F, 1.0F};
        vec4 shadow_origin;
        glmc_mat4_mulv(shadow_matrix, world_origin, shadow_origin);
        if(fabsf(shadow_origin[3]) > 0.0001F) {
                float texels_per_clip = (float)shadow_size * 0.5F;
                float origin_x = shadow_origin[0] / shadow_origin[3] * texels_per_clip;
                float origin_y = shadow_origin[1] / shadow_origin[3] * texels_per_clip;
                matrix_projection[3][0] += (roundf(origin_x) - origin_x) / texels_per_clip;
                matrix_projection[3][1] += (roundf(origin_y) - origin_y) / texels_per_clip;
                glmc_mat4_mul(matrix_projection, matrix_view, shadow_matrix);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, shadow_fbo);
        glViewport(0, 0, shadow_size, shadow_size);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(2.0F, 4.0F);
        glClear(GL_DEPTH_BUFFER_BIT);
        glBindVertexArray(shadow_vao);
        glx_use_program(shadow_program);

        /* Include off-camera terrain that can project into the visible area. */
        chunk_draw_shadow(extent + (float)map_size_y);

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)previous_draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)previous_read_fbo);
        glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
        glColorMask(previous_color_mask[0], previous_color_mask[1],
                    previous_color_mask[2], previous_color_mask[3]);
        glDepthMask(previous_depth_mask);
        glDepthFunc((GLenum)previous_depth_func);
        glCullFace((GLenum)previous_cull_face);
        glPolygonOffset(previous_polygon_factor, previous_polygon_units);
        shadow_restore_capability(GL_POLYGON_OFFSET_FILL, polygon_offset_was_enabled);
        shadow_restore_capability(GL_CULL_FACE, cull_was_enabled);
        shadow_restore_capability(GL_BLEND, blend_was_enabled);
        shadow_restore_capability(GL_DEPTH_TEST, depth_was_enabled);
        shadow_restore_capability(GL_SCISSOR_TEST, scissor_was_enabled);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)previous_texture0);
        glActiveTexture((GLenum)previous_active_texture);
        glBindVertexArray((GLuint)previous_vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)previous_array_buffer);

        memcpy(matrix_projection, saved_projection, sizeof(mat4));
        memcpy(matrix_view, saved_view, sizeof(mat4));
        memcpy(matrix_model, saved_model, sizeof(mat4));
        glx_use_program((GLuint)previous_program);
        if(previous_program)
                matrix_upload();

        shadow_center[0] = center_x;
        shadow_center[1] = center_y;
        shadow_center[2] = center_z;
        shadow_cached_sun[0] = sx;
        shadow_cached_sun[1] = sy;
        shadow_cached_sun[2] = sz;
        shadow_cached_extent = extent;
        shadow_dirty = false;
        shadow_map_valid = true;
}

void shadow_apply_program(unsigned int program) {
        if(shadow_scope_bound)
                shadow_finish_program();
        if(!program)
                return;

        GLint previous_program = 0;
        previous_program = (GLint)glx_current_program();
        if((GLuint)previous_program != (GLuint)program)
                glx_use_program((GLuint)program);

        bool enabled = shadow_live_enabled();
        GLint location = glx_uniform_location((GLuint)program, "u_ShadowEnabled");
        if(location >= 0)
                glUniform1f(location, enabled ? 1.0F : 0.0F);

        if(enabled) {
                location = glx_uniform_location((GLuint)program, "u_ShadowMatrix");
                if(location >= 0)
                        glUniformMatrix4fv(location, 1, GL_FALSE, (float*)shadow_matrix);
                location = glx_uniform_location((GLuint)program, "u_ShadowMap");
                if(location >= 0)
                        glUniform1i(location, 3);
                location = glx_uniform_location((GLuint)program, "u_ShadowTexel");
                if(location >= 0)
                        glUniform1f(location, 1.0F / (float)shadow_size);
                location = glx_uniform_location((GLuint)program, "u_ShadowIntensity");
                if(location >= 0)
                        glUniform1f(location, settings.shadow_intensity);
        }

        if((GLuint)previous_program != (GLuint)program)
                glx_use_program((GLuint)previous_program);
        if(!enabled)
                return;

        glGetIntegerv(GL_ACTIVE_TEXTURE, &shadow_scope_active_texture);
        glActiveTexture(GL_TEXTURE3);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &shadow_scope_texture3);
        glBindTexture(GL_TEXTURE_2D, shadow_texture);
        glActiveTexture((GLenum)shadow_scope_active_texture);
        shadow_scope_bound = true;
}

void shadow_finish_program(void) {
        if(!shadow_scope_bound)
                return;
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, (GLuint)shadow_scope_texture3);
        glActiveTexture((GLenum)shadow_scope_active_texture);
        shadow_scope_bound = false;
}

#else

bool shadow_init(void) { return false; }
void shadow_deinit(void) {}
bool shadow_gpu_supported(void) { return false; }
bool shadow_baked_enabled(void) { return settings.shadow_quality && settings.shadow_intensity > 0.0F; }
void shadow_invalidate(void) {}
void shadow_render(void) {}
void shadow_apply_program(unsigned int program) { (void)program; }
void shadow_finish_program(void) {}

#endif
