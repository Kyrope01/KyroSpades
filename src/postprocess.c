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
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "glx.h"
#include "postprocess.h"

#if !defined(OPENGL_ES)

/* The window's multisample buffer does not affect geometry rendered into the
 * post-process FBO. Keep a multisampled scene target and resolve it into the
 * existing sampled color/depth attachments before flare or outlines. */
static struct {
        GLuint fbo;
        GLuint color_rb;
        GLuint depth_rb;
        int requested_samples;
        int width;
        int height;
        GLenum color_format;
        int samples;
        int failed_samples;
        int failed_width;
        int failed_height;
        GLenum failed_format;
} scene_msaa = {0};

unsigned int postprocess_msaa_target(void) {
        return scene_msaa.fbo;
}

void postprocess_msaa_reset(void) {
        if(scene_msaa.fbo)
                glDeleteFramebuffers(1, &scene_msaa.fbo);
        if(scene_msaa.color_rb)
                glDeleteRenderbuffers(1, &scene_msaa.color_rb);
        if(scene_msaa.depth_rb)
                glDeleteRenderbuffers(1, &scene_msaa.depth_rb);
        memset(&scene_msaa, 0, sizeof(scene_msaa));
}

static bool postprocess_msaa_config_matches(int samples, int width, int height, GLenum format) {
        return scene_msaa.requested_samples == samples && scene_msaa.width == width
               && scene_msaa.height == height && scene_msaa.color_format == format;
}

unsigned int postprocess_msaa_configure(int requested_samples, int width, int height,
                                        unsigned int color_internal_format) {
        /* glTexImage2D accepts the legacy generic GL_RGBA, but renderbuffer
           storage requires a sized format. Both represent the same RGBA8 scene. */
        GLenum format = color_internal_format == GL_RGBA ? GL_RGBA8 : (GLenum)color_internal_format;
        if(requested_samples <= 1 || width <= 0 || height <= 0 || !glx_version_at_least(3, 0)) {
                postprocess_msaa_reset();
                return 0;
        }
        if(scene_msaa.fbo && postprocess_msaa_config_matches(requested_samples, width, height, format))
                return scene_msaa.fbo;
        if(!scene_msaa.fbo && scene_msaa.failed_samples == requested_samples
           && scene_msaa.failed_width == width && scene_msaa.failed_height == height
           && scene_msaa.failed_format == format)
                return 0;

        /* Configuration runs before the scene target is bound. Preserve the
           caller's target so setup/failure has no visible GL-state side effects. */
        GLint previous_fbo = 0;
        GLint previous_rb = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_fbo);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous_rb);
        if(previous_fbo == (GLint)scene_msaa.fbo)
                previous_fbo = 0;
        if(previous_rb == (GLint)scene_msaa.color_rb || previous_rb == (GLint)scene_msaa.depth_rb)
                previous_rb = 0;
        postprocess_msaa_reset();

        GLint max_samples = 0;
        glGetIntegerv(GL_MAX_SAMPLES, &max_samples);
        int candidate = min(requested_samples, max_samples);
        if(candidate >= 2) {
                glGenFramebuffers(1, &scene_msaa.fbo);
                glGenRenderbuffers(1, &scene_msaa.color_rb);
                glGenRenderbuffers(1, &scene_msaa.depth_rb);
        }

        if(scene_msaa.fbo && scene_msaa.color_rb && scene_msaa.depth_rb) {
                glBindFramebuffer(GL_FRAMEBUFFER, scene_msaa.fbo);
                glBindRenderbuffer(GL_RENDERBUFFER, scene_msaa.color_rb);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_RENDERBUFFER, scene_msaa.color_rb);
                glBindRenderbuffer(GL_RENDERBUFFER, scene_msaa.depth_rb);
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                          GL_RENDERBUFFER, scene_msaa.depth_rb);

                /* Some drivers expose a high global limit but support fewer
                   samples for RGBA16F. Retry lower counts before falling back. */
                for(int samples = candidate; samples >= 2; samples--) {
                        glBindRenderbuffer(GL_RENDERBUFFER, scene_msaa.color_rb);
                        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, format, width, height);
                        glBindRenderbuffer(GL_RENDERBUFFER, scene_msaa.depth_rb);
                        glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                                         GL_DEPTH_COMPONENT24, width, height);
                        if(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
                                scene_msaa.requested_samples = requested_samples;
                                scene_msaa.width = width;
                                scene_msaa.height = height;
                                scene_msaa.color_format = format;
                                scene_msaa.samples = samples;
                                log_info("Post-process MSAA: %ix (requested %ix)", samples, requested_samples);
                                glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previous_rb);
                                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
                                return scene_msaa.fbo;
                        }
                }
        }

        log_warn("Post-process MSAA %ix unavailable; using the single-sampled scene target",
                 requested_samples);
        glBindRenderbuffer(GL_RENDERBUFFER, (GLuint)previous_rb);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previous_fbo);
        postprocess_msaa_reset();
        scene_msaa.failed_samples = requested_samples;
        scene_msaa.failed_width = width;
        scene_msaa.failed_height = height;
        scene_msaa.failed_format = format;
        return 0;
}

void postprocess_msaa_resolve(unsigned int resolve_fbo) {
        if(!scene_msaa.fbo || !resolve_fbo || scene_msaa.samples < 2)
                return;

        glBindFramebuffer(GL_READ_FRAMEBUFFER, scene_msaa.fbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)resolve_fbo);
        glBlitFramebuffer(0, 0, scene_msaa.width, scene_msaa.height,
                          0, 0, scene_msaa.width, scene_msaa.height,
                          GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        /* Keep both framebuffer bindings matched after the scoped resolve. */
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)resolve_fbo);
}

#else

unsigned int postprocess_msaa_configure(int requested_samples, int width, int height,
                                        unsigned int color_internal_format) {
        (void)requested_samples;
        (void)width;
        (void)height;
        (void)color_internal_format;
        return 0;
}

unsigned int postprocess_msaa_target(void) {
        return 0;
}

void postprocess_msaa_resolve(unsigned int resolve_fbo) {
        (void)resolve_fbo;
}

void postprocess_msaa_reset(void) {}

#endif

bool postprocess_hdr_supported(void) {
#ifdef OPENGL_CORE
        return glx_version != 0 && glx_version_at_least(3, 3);
#else
        return false;
#endif
}

void postprocess_deinit(void) {
        postprocess_msaa_reset();
}
