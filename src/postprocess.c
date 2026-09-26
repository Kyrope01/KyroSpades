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
#include <string.h>

#include "common.h"
#include "glx.h"
#include "postprocess.h"

#ifdef OPENGL_CORE

#define BLOOM_BLUR_PASSES 8

static const char* postprocess_vertex_shader =
        "#version 330 core\n"
        "layout(location = 0) in vec2 a_Position;\n"
        "layout(location = 2) in vec2 a_TexCoord;\n"
        "out vec2 v_TexCoord;\n"
        "void main() {\n"
        "    v_TexCoord = a_TexCoord;\n"
        "    gl_Position = vec4(a_Position, 0.0, 1.0);\n"
        "}\n";

static const char* bloom_extract_fragment_shader =
        "#version 330 core\n"
        "in vec2 v_TexCoord;\n"
        "layout(location = 0) out vec4 o_Color;\n"
        "uniform sampler2D u_Scene;\n"
        "uniform float u_Threshold;\n"
        "void main() {\n"
        "    vec3 color = max(texture(u_Scene, v_TexCoord).rgb, vec3(0.0));\n"
        "    float brightness = max(color.r, max(color.g, color.b));\n"
        "    float knee = max(u_Threshold * 0.5, 0.0001);\n"
        "    float soft = clamp(brightness - u_Threshold + knee, 0.0, 2.0 * knee);\n"
        "    soft = soft * soft / (4.0 * knee + 0.0001);\n"
        "    float contribution = max(brightness - u_Threshold, soft);\n"
        "    contribution /= max(brightness, 0.0001);\n"
        "    o_Color = vec4(color * contribution, 1.0);\n"
        "}\n";

static const char* bloom_blur_fragment_shader =
        "#version 330 core\n"
        "in vec2 v_TexCoord;\n"
        "layout(location = 0) out vec4 o_Color;\n"
        "uniform sampler2D u_Image;\n"
        "uniform vec2 u_Direction;\n"
        "void main() {\n"
        "    vec3 color = texture(u_Image, v_TexCoord).rgb * 0.2270270270;\n"
        "    color += texture(u_Image, v_TexCoord + u_Direction * 1.3846153846).rgb * 0.3162162162;\n"
        "    color += texture(u_Image, v_TexCoord - u_Direction * 1.3846153846).rgb * 0.3162162162;\n"
        "    color += texture(u_Image, v_TexCoord + u_Direction * 3.2307692308).rgb * 0.0702702703;\n"
        "    color += texture(u_Image, v_TexCoord - u_Direction * 3.2307692308).rgb * 0.0702702703;\n"
        "    o_Color = vec4(color, 1.0);\n"
        "}\n";

static struct {
        GLuint extract_program;
        GLuint blur_program;
        GLint extract_scene;
        GLint extract_threshold;
        GLint blur_image;
        GLint blur_direction;
        GLuint vao;
        GLuint vbo;
        GLuint texture[2];
        GLuint fbo[2];
        int width;
        int height;
        int failed_width;
        int failed_height;
        bool shader_attempted;
} bloom = {0};

struct postprocess_gl_state {
        GLint draw_fbo;
        GLint read_fbo;
        GLint program;
        GLint active_texture;
        GLint texture0;
        GLint viewport[4];
        GLint vao;
        GLint array_buffer;
        GLboolean depth_mask;
        GLboolean depth_test;
        GLboolean blend;
        GLboolean scissor;
        GLboolean cull;
        GLboolean framebuffer_srgb;
};

static void postprocess_restore_capability(GLenum capability, GLboolean enabled) {
        if(enabled)
                glEnable(capability);
        else
                glDisable(capability);
}

static void postprocess_state_capture(struct postprocess_gl_state* state) {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &state->draw_fbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &state->read_fbo);
        state->program = (GLint)glx_current_program();
        glGetIntegerv(GL_ACTIVE_TEXTURE, &state->active_texture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture0);
        glGetIntegerv(GL_VIEWPORT, state->viewport);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state->vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &state->depth_mask);
        state->depth_test = glIsEnabled(GL_DEPTH_TEST);
        state->blend = glIsEnabled(GL_BLEND);
        state->scissor = glIsEnabled(GL_SCISSOR_TEST);
        state->cull = glIsEnabled(GL_CULL_FACE);
        state->framebuffer_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
}

static void postprocess_state_restore(const struct postprocess_gl_state* state) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)state->draw_fbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)state->read_fbo);
        glViewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
        glx_use_program((GLuint)state->program);
        glBindVertexArray((GLuint)state->vao);
        glBindBuffer(GL_ARRAY_BUFFER, (GLuint)state->array_buffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, (GLuint)state->texture0);
        glActiveTexture((GLenum)state->active_texture);
        glDepthMask(state->depth_mask);
        postprocess_restore_capability(GL_DEPTH_TEST, state->depth_test);
        postprocess_restore_capability(GL_BLEND, state->blend);
        postprocess_restore_capability(GL_SCISSOR_TEST, state->scissor);
        postprocess_restore_capability(GL_CULL_FACE, state->cull);
        postprocess_restore_capability(GL_FRAMEBUFFER_SRGB, state->framebuffer_srgb);
}

static void bloom_targets_destroy(void) {
        if(bloom.fbo[0] || bloom.fbo[1])
                glDeleteFramebuffers(2, bloom.fbo);
        if(bloom.texture[0] || bloom.texture[1])
                glDeleteTextures(2, bloom.texture);
        bloom.fbo[0] = bloom.fbo[1] = 0;
        bloom.texture[0] = bloom.texture[1] = 0;
        bloom.width = 0;
        bloom.height = 0;
}

static bool bloom_programs_create(void) {
        if(bloom.extract_program && bloom.blur_program && bloom.vao && bloom.vbo)
                return true;
        if(bloom.shader_attempted)
                return false;
        bloom.shader_attempted = true;

        bloom.extract_program = (GLuint)glx_shader(postprocess_vertex_shader, bloom_extract_fragment_shader);
        bloom.blur_program = (GLuint)glx_shader(postprocess_vertex_shader, bloom_blur_fragment_shader);
        if(!bloom.extract_program || !bloom.blur_program)
                goto fail;

        bloom.extract_scene = glx_uniform_location(bloom.extract_program, "u_Scene");
        bloom.extract_threshold = glx_uniform_location(bloom.extract_program, "u_Threshold");
        bloom.blur_image = glx_uniform_location(bloom.blur_program, "u_Image");
        bloom.blur_direction = glx_uniform_location(bloom.blur_program, "u_Direction");
        if(bloom.extract_scene < 0 || bloom.extract_threshold < 0
           || bloom.blur_image < 0 || bloom.blur_direction < 0) {
                log_error("HDR bloom shader contract is incomplete; using single-pass bloom");
                goto fail;
        }

        const GLfloat vertices[] = {
                -1.0F, -1.0F, 0.0F, 0.0F,
                 3.0F, -1.0F, 2.0F, 0.0F,
                -1.0F,  3.0F, 0.0F, 2.0F,
        };
        glGenVertexArrays(1, &bloom.vao);
        glGenBuffers(1, &bloom.vbo);
        if(!bloom.vao || !bloom.vbo)
                goto fail;
        glBindVertexArray(bloom.vao);
        glBindBuffer(GL_ARRAY_BUFFER, bloom.vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(2);

        glx_use_program(bloom.extract_program);
        glUniform1i(bloom.extract_scene, 0);
        glx_use_program(bloom.blur_program);
        glUniform1i(bloom.blur_image, 0);
        return true;

fail:
        if(bloom.extract_program)
                glx_delete_program(bloom.extract_program);
        if(bloom.blur_program)
                glx_delete_program(bloom.blur_program);
        if(bloom.vbo)
                glDeleteBuffers(1, &bloom.vbo);
        if(bloom.vao)
                glDeleteVertexArrays(1, &bloom.vao);
        bloom.extract_program = 0;
        bloom.blur_program = 0;
        bloom.vbo = 0;
        bloom.vao = 0;
        log_warn("Multipass HDR bloom unavailable; using the compatibility bloom path");
        return false;
}

static bool bloom_targets_create(int source_width, int source_height) {
        int width = max(1, (source_width + 1) / 2);
        int height = max(1, (source_height + 1) / 2);
        if(bloom.texture[0] && bloom.width == width && bloom.height == height)
                return true;
        if(!bloom.texture[0] && bloom.failed_width == width && bloom.failed_height == height)
                return false;

        bloom_targets_destroy();
        bloom.failed_width = 0;
        bloom.failed_height = 0;
        glGenTextures(2, bloom.texture);
        glGenFramebuffers(2, bloom.fbo);

        for(int i = 0; i < 2; i++) {
                if(!bloom.texture[i] || !bloom.fbo[i])
                        goto fail;
                glBindTexture(GL_TEXTURE_2D, bloom.texture[i]);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, NULL);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[i]);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, bloom.texture[i], 0);
                if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                        goto fail;
        }

        bloom.width = width;
        bloom.height = height;
        return true;

fail:
        bloom_targets_destroy();
        bloom.failed_width = width;
        bloom.failed_height = height;
        log_warn("HDR bloom framebuffer allocation failed at %ix%i; using single-pass bloom", width, height);
        return false;
}

static void bloom_draw(void) {
        glBindVertexArray(bloom.vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
}

bool postprocess_hdr_supported(void) {
        return glx_version != 0 && glx_version_at_least(3, 3);
}

unsigned int postprocess_bloom_render(unsigned int scene_texture, int width, int height, float threshold) {
        if(!scene_texture || width <= 0 || height <= 0 || !postprocess_hdr_supported())
                return 0;

        struct postprocess_gl_state state;
        postprocess_state_capture(&state);

        bool ready = bloom_programs_create() && bloom_targets_create(width, height);
        if(!ready) {
                postprocess_state_restore(&state);
                return 0;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_FRAMEBUFFER_SRGB);
        glDepthMask(GL_FALSE);
        glActiveTexture(GL_TEXTURE0);
        glViewport(0, 0, bloom.width, bloom.height);

        glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[0]);
        glBindTexture(GL_TEXTURE_2D, (GLuint)scene_texture);
        glx_use_program(bloom.extract_program);
        glUniform1f(bloom.extract_threshold, fmaxf(threshold, 0.0001F));
        bloom_draw();

        int source = 0;
        for(int pass = 0; pass < BLOOM_BLUR_PASSES; pass++) {
                int destination = 1 - source;
                glBindFramebuffer(GL_FRAMEBUFFER, bloom.fbo[destination]);
                glBindTexture(GL_TEXTURE_2D, bloom.texture[source]);
                glx_use_program(bloom.blur_program);
                if((pass & 1) == 0)
                        glUniform2f(bloom.blur_direction, 1.0F / (float)bloom.width, 0.0F);
                else
                        glUniform2f(bloom.blur_direction, 0.0F, 1.0F / (float)bloom.height);
                bloom_draw();
                source = destination;
        }

        GLuint result = bloom.texture[source];
        postprocess_state_restore(&state);
        return result;
}

void postprocess_deinit(void) {
        bloom_targets_destroy();
        if(bloom.extract_program)
                glx_delete_program(bloom.extract_program);
        if(bloom.blur_program)
                glx_delete_program(bloom.blur_program);
        if(bloom.vbo)
                glDeleteBuffers(1, &bloom.vbo);
        if(bloom.vao)
                glDeleteVertexArrays(1, &bloom.vao);
        memset(&bloom, 0, sizeof(bloom));
}

#else

bool postprocess_hdr_supported(void) {
        return false;
}

unsigned int postprocess_bloom_render(unsigned int scene_texture, int width, int height, float threshold) {
        (void)scene_texture;
        (void)width;
        (void)height;
        (void)threshold;
        return 0;
}

void postprocess_deinit(void) {}

#endif
