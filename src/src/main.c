
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

#include <time.h>
#include <sys/stat.h>
#if defined(USE_SDL) && defined(__ANDROID__)
#include <SDL_main.h>
#endif
#if defined(__ANDROID__)
#include <unistd.h>   /* chdir */
#include <SDL.h>      /* SDL_AndroidGet*StoragePath */
#endif
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "lodepng/lodepng.h"
#include "common.h"
#include "file.h"
#include "font.h"
#include "weapon.h"
#include "window.h"
#include "rpc.h"
#include "network.h"
#include "demo.h"
#include "sound.h"
#include "map.h"
#include "particle.h"
#include "tracer.h"
#include "camera.h"
#include "cameracontroller.h"
#include "grenade.h"
#include "player.h"
#include "hud.h"
#include "config.h"
#include "log.h"
#include "ping.h"
#include "matrix.h"
#include "water.h"
#include "texture.h"
#include "chunk.h"
#include "skins.h"
#include "chatlog.h"
#include "recorder.h"
#include "bloodmarks.h"
#include "damagenumbers.h"
#include "main.h"

int fps = 0;

float dt_float = 0.0F;

/* Fraction of the current 60 Hz physics tick that has elapsed when the
   frame renders (0..1). Used for partial-tick render interpolation. */
float physics_tick_alpha = 1.0F;

int ms_seed = 1;
int ms_rand() {
        ms_seed = ms_seed * 0x343FD + 0x269EC3;
        return (ms_seed >> 0x10) & 0x7FFF;
}

static struct {
        unsigned int texture;
        unsigned int shader;
        unsigned int fbo;
        unsigned int depth_rb;
        unsigned int depth_tex;
        int w;
        int h;
        int uni_pp_sat;
        int uni_pp_scale;
        int uni_pp_bias;
        int uni_pp_vig;
        /* New post-proc uniforms: chromatic aberration strength,
           filmic tone-mapping flag */
        int uni_pp_ca_strength;
        int uni_pp_filmic;
        /* Volumetric lighting (god-rays) — faithful port of Luanti's shader */
        unsigned int vol_tex;       /* color texture that receives volumetric output */
        unsigned int vol_fbo;       /* FBO bound to vol_tex (no depth attachment) */
        unsigned int vol_shader;    /* GLSL program for the volumetric pass */
        int uni_vol_sun_pos;
        int uni_vol_sun_brightness;
        int uni_vol_strength;
        int uni_vol_daylight;
        int uni_vol_lightdir;
        int vol_applied;
        /* Mode 2: directional volumetric light (rays visible from any angle) */
        unsigned int vol2_shader;
        int uni_vol2_sun_dir;
        int uni_vol2_sun_brightness;
        int uni_vol2_strength;
        int uni_vol2_brightness;
        int uni_vol2_range;
        int uni_vol2_daylight;
        int uni_vol2_lightdir;
        /* Lens flare */
        unsigned int flare_shader;
        int uni_flare_sun_pos;
        int uni_flare_sun_brightness;
        int uni_flare_strength;
        int uni_flare_include_scene;
} postproc = {0};

static struct {
        bool active;
        double start_time;
        int texture;
} screenshot_anim = {0};

int chat_input_mode = CHAT_NO_INPUT;

char chat[3][128][256] = {0}; // chat[0] is current input
                             // chat[2] is history
unsigned int chat_color[3][128];
float chat_timer[3][128];
unsigned int chat_history_pos;
char         session_log_raw[SESSION_LOG_MAX][256];
unsigned int session_log_color[SESSION_LOG_MAX];
int          session_log_count = 0;

static void chat_push_one(int channel, unsigned int color, const char* msg) {
        memmove(chat[channel][2], chat[channel][1], sizeof(chat[channel][0]) * 125);
        memmove(&chat_color[channel][2], &chat_color[channel][1], sizeof(chat_color[channel][0]) * 125);
        memmove(&chat_timer[channel][2], &chat_timer[channel][1], sizeof(chat_timer[channel][0]) * 125);
        strncpy(chat[channel][1], msg, sizeof(chat[channel][1]) - 1);
        chat[channel][1][sizeof(chat[channel][1]) - 1] = 0;
        chat_color[channel][1] = color;
        chat_timer[channel][1] = window_time();
}

void chat_add(int channel, unsigned int color, const char* msg) {
        /* Split on '\n' so each line gets its own slot in the live HUD;
           otherwise font_render would draw the second line atop the next
           message slot. Cap segments to keep one pathological message from
           evicting the entire live ring. */
        const int MAX_SEGMENTS = 16;
        int start = 0, seg = 0;
        int total_len = (int)strlen(msg);
        int reached_end = 0; /* set when we processed the final segment via i == total_len */
        for(int i = 0; i <= total_len && seg < MAX_SEGMENTS - 1; i++) {
                if(i == total_len || msg[i] == '\n') {
                        int n = i - start;
                        char buf[256];
                        if(n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
                        memcpy(buf, msg + start, n);
                        buf[n] = 0;
                        chat_push_one(channel, color, buf);
                        if(channel == 0) {
                                log_info("%s", buf);
                                if(session_log_count >= SESSION_LOG_MAX) {
                                        memmove(session_log_raw,   session_log_raw + 1,
                                                        (SESSION_LOG_MAX - 1) * sizeof(session_log_raw[0]));
                                        memmove(session_log_color, session_log_color + 1,
                                                        (SESSION_LOG_MAX - 1) * sizeof(session_log_color[0]));
                                        session_log_count = SESSION_LOG_MAX - 1;
                                }
                                strncpy(session_log_raw[session_log_count], buf,
                                                sizeof(session_log_raw[0]) - 1);
                                session_log_raw[session_log_count][sizeof(session_log_raw[0]) - 1] = 0;
                                session_log_color[session_log_count] = color;
                                session_log_count++;
                        }
                        if(i == total_len) {
                                reached_end = 1;
                                break;
                        }
                        start = i + 1;
                        seg++;
                }
        }
        /* If we exited the loop because of the segment cap (not because we
           reached the end), and there is still a trailing tail, push it as
           the final segment so the rest of the message is not silently
           dropped. Without the reached_end guard we would re-push the last
           segment the loop already processed. */
        if(!reached_end && start < total_len) {
                int n = total_len - start;
                char buf[256];
                if(n > (int)sizeof(buf) - 1) n = sizeof(buf) - 1;
                memcpy(buf, msg + start, n);
                buf[n] = 0;
                chat_push_one(channel, color, buf);
                if(channel == 0) {
                        log_info("%s", buf);
                        if(session_log_count >= SESSION_LOG_MAX) {
                                memmove(session_log_raw,   session_log_raw + 1,
                                                (SESSION_LOG_MAX - 1) * sizeof(session_log_raw[0]));
                                memmove(session_log_color, session_log_color + 1,
                                                (SESSION_LOG_MAX - 1) * sizeof(session_log_color[0]));
                                session_log_count = SESSION_LOG_MAX - 1;
                        }
                        strncpy(session_log_raw[session_log_count], buf,
                                        sizeof(session_log_raw[0]) - 1);
                        session_log_raw[session_log_count][sizeof(session_log_raw[0]) - 1] = 0;
                        session_log_color[session_log_count] = color;
                        session_log_count++;
                }
        }
}

void chat_clear(int channel) {
        memset(chat[channel][1], 0, sizeof(chat[channel][0]) * 127);
        memset(&chat_color[channel][1], 0, sizeof(chat_color[channel][0]) * 127);
        memset(&chat_timer[channel][1], 0, sizeof(chat_timer[channel][0]) * 127);
        if(channel == 0)
                session_log_count = 0;
}

char chat_popup[256] = {};
int chat_popup_color;
float chat_popup_timer = 0.0F;
float chat_popup_duration = 0.0F;

void chat_showpopup(const char* msg, float duration, int color) {
        /* Bound the copy: msg can come from an untrusted network packet
           (PacketChatMessage, CHAT_BIG/INFO/WARNING) whose payload may not be
           null-terminated within 255 bytes. Previously strcpy could overflow
           the 256-byte chat_popup buffer and clobber adjacent globals. */
        strncpy(chat_popup, msg, sizeof(chat_popup) - 1);
        chat_popup[sizeof(chat_popup) - 1] = 0;
        chat_popup_timer = window_time();
        chat_popup_duration = duration;
        chat_popup_color = color;
}

/* Render-height for a dropped intel: the camera-y translate that makes the
   model's bottom touch the top of the first solid block below the drop cell.
   dx/dy are the horizontal map coords, dz the vertical (map) coord. */
static float intel_drop_render_y(float dx, float dy, float dz) {
        int ix = (int)floorf(dx);
        int iz = (int)floorf(dy);   /* horizontal map coords */
        int izm = (int)floorf(dz);  /* vertical map coord */
        if(ix < 0 || ix >= map_size_x || iz < 0 || iz >= map_size_z)
                return 63.0F;
        if(izm < 0) izm = 0;
        if(izm >= map_size_y) izm = map_size_y - 1;

        int ground = izm;
        while(ground > 0 && map_isair(ix, 63 - ground, iz))
                ground--;

        /* `ground` is the solid block under the flag; its top surface in
           camera-y is 62 - ground.  kv6_render pivots the model around ypiv
           (so the model's bottom sits at translate_y - ypiv*scale), hence
           the +ypiv*scale to touch the surface exactly. */
        return 62.0F - (float)ground + model_intel.ypiv * model_intel.scale;
}

void drawScene() {
        if(settings.ambient_occlusion) {
                glShadeModel(GL_SMOOTH);
        } else {
                glShadeModel(GL_FLAT);
        }

        /* Effective fog color for this frame (darkened 10% when filmic is on).
           Used by every fog path below so the visible fog matches what the
           user picked in the color picker after ACES tone mapping. */
        float fc[4];
        fog_color_render(fc);
        fc[3] = fog_color[3];

        if(settings.textured_blocks) {
#ifndef OPENGL_ES
                if(glx_fog) {
                        glFogi(GL_FOG_MODE, GL_LINEAR);
                        glFogf(GL_FOG_START, 0.0F);
                        glFogf(GL_FOG_END, settings.render_distance);
                        glFogfv(GL_FOG_COLOR, fc);
                        glEnable(GL_FOG);
                }
#endif
        }

        matrix_upload();
        chunk_draw_visible();

        if(settings.textured_blocks) {
#ifndef OPENGL_ES
                if(glx_fog)
                        glDisable(GL_FOG);
#endif
        }

        water_render();

        if(settings.smooth_fog) {
#ifdef OPENGL_ES
                glFogx(GL_FOG_MODE, GL_EXP2);
#else
                glFogi(GL_FOG_MODE, GL_EXP2);
#endif
                glFogf(GL_FOG_DENSITY, 0.015F);
                glFogfv(GL_FOG_COLOR, fc);
                glEnable(GL_FOG);
        }

        glShadeModel(GL_FLAT);
        kv6_calclight(-1, -1, -1);
        matrix_upload();
        particle_render();
        tracer_render();
        grenade_render();
        map_damaged_voxels_render();
        bloodmarks_render();
        matrix_upload();

        /* Rest the dropped intel on the floor: scan downward from the drop
           cell for the first solid block and sit the model's bottom on its
           top surface.  The old code rendered it a full block up in the air
           (plus the model pivot offset), which is why it floated. */
        if(gamestate.gamemode_type == GAMEMODE_CTF) {
                if(!gamestate.gamemode.ctf.team_1_intel) {
                        float x = gamestate.gamemode.ctf.team_1_intel_location.dropped.x;
                        float y = intel_drop_render_y(gamestate.gamemode.ctf.team_1_intel_location.dropped.x,
                                                      gamestate.gamemode.ctf.team_1_intel_location.dropped.y,
                                                      gamestate.gamemode.ctf.team_1_intel_location.dropped.z);
                        float z = gamestate.gamemode.ctf.team_1_intel_location.dropped.y;
                        matrix_push(matrix_model);
                        matrix_translate(matrix_model, x, y, z);
                        kv6_calclight(x, y, z);
                        matrix_upload();
                        kv6_render(&model_intel, TEAM_1);
                        matrix_pop(matrix_model);
                }
                if(!gamestate.gamemode.ctf.team_2_intel) {
                        float x = gamestate.gamemode.ctf.team_2_intel_location.dropped.x;
                        float y = intel_drop_render_y(gamestate.gamemode.ctf.team_2_intel_location.dropped.x,
                                                      gamestate.gamemode.ctf.team_2_intel_location.dropped.y,
                                                      gamestate.gamemode.ctf.team_2_intel_location.dropped.z);
                        float z = gamestate.gamemode.ctf.team_2_intel_location.dropped.y;
                        matrix_push(matrix_model);
                        matrix_translate(matrix_model, x, y, z);
                        kv6_calclight(x, y, z);
                        matrix_upload();
                        kv6_render(&model_intel, TEAM_2);
                        matrix_pop(matrix_model);
                }
                if(map_object_visible(gamestate.gamemode.ctf.team_1_base.x, 63.0F - gamestate.gamemode.ctf.team_1_base.z + 1.0F,
                                                          gamestate.gamemode.ctf.team_1_base.y)) {
                        matrix_push(matrix_model);
                        matrix_translate(matrix_model, gamestate.gamemode.ctf.team_1_base.x,
                                                         63.0F - gamestate.gamemode.ctf.team_1_base.z + 1.0F, gamestate.gamemode.ctf.team_1_base.y);
                        kv6_calclight(gamestate.gamemode.ctf.team_1_base.x, 63.0F - gamestate.gamemode.ctf.team_1_base.z + 1.0F,
                                                  gamestate.gamemode.ctf.team_1_base.y);
                        matrix_upload();
                        kv6_render(&model_tent, TEAM_1);
                        matrix_pop(matrix_model);
                }
                if(map_object_visible(gamestate.gamemode.ctf.team_2_base.x, 63.0F - gamestate.gamemode.ctf.team_2_base.z + 1.0F,
                                                          gamestate.gamemode.ctf.team_2_base.y)) {
                        matrix_push(matrix_model);
                        matrix_translate(matrix_model, gamestate.gamemode.ctf.team_2_base.x,
                                                         63.0F - gamestate.gamemode.ctf.team_2_base.z + 1.0F, gamestate.gamemode.ctf.team_2_base.y);
                        kv6_calclight(gamestate.gamemode.ctf.team_2_base.x, 63.0F - gamestate.gamemode.ctf.team_2_base.z + 1.0F,
                                                  gamestate.gamemode.ctf.team_2_base.y);
                        matrix_upload();
                        kv6_render(&model_tent, TEAM_2);
                        matrix_pop(matrix_model);
                }
        }
        if(gamestate.gamemode_type == GAMEMODE_TC) {
                for(int k = 0; k < gamestate.gamemode.tc.territory_count; k++) {
                        matrix_push(matrix_model);
                        matrix_translate(matrix_model, gamestate.gamemode.tc.territory[k].x,
                                                         63.0F - gamestate.gamemode.tc.territory[k].z + 1.0F, gamestate.gamemode.tc.territory[k].y);
                        kv6_calclight(gamestate.gamemode.tc.territory[k].x, 63.0F - gamestate.gamemode.tc.territory[k].z + 1.0F,
                                                  gamestate.gamemode.tc.territory[k].y);
                        matrix_upload();
                        kv6_render(&model_tent, min(gamestate.gamemode.tc.territory[k].team, 2));
                        matrix_pop(matrix_model);
                }
        }
}

/* SDL's on-screen framebuffer.
   ────────────────────────────────────────────────────────────────────────
   On desktop/Android (EGL) it is object 0; on iOS (UIKit/EAGL) it is a
   NON-zero EAGL viewFramebuffer and object 0 is off-screen. The id lives
   in window.c and is now captured in two places that are both reliable:
   (1) once at window_init after a forced initial swap (so SDL's lazy FBO
   exists and is bound) and (2) every frame in window_update right after
   SDL_GL_SwapWindow, where SDL's bindBackbuffer has just re-bound it.
   Both updates use a non-zero guard so a transient 0 read can never wipe
   out the captured value — that was v12's silent regression: a single 0
   from glGetIntegerv at the start of the render block would clobber the
   id and route every subsequent frame off-screen. */

void display() {
        /* Apply pending window changes (fullscreen, vsync, size) every frame.
           drawScene() is skipped in the menus when not connected, so this must
           not live there or settings only take effect once in-game. */
        window_apply();

        /* Treat near-zero slider values as OFF: the on-screen sliders can't
           always land exactly on 0 (touch precision), and a visually-nil
           post-process pass would otherwise keep running. */
        int needs_postproc = ((glx_version || gles_version >= 2)
                              && (settings.exposure < -0.5F || settings.exposure > 0.5F
                                  || settings.saturation < -0.5F || settings.saturation > 0.5F
                                  || settings.contrast < -0.5F || settings.contrast > 0.5F
                                  || settings.vignette > 0.5F || settings.volumetric_light || settings.lens_flare
                                  || settings.chromatic_aberration || settings.filmic_tonemapping));

        /* Avoid a driver-synchronising glGetIntegerv every frame on the normal
           fast path.  We only need the live framebuffer id when a post-process
           pass may bind away from the screen (or on iOS, where the drawable FBO
           can be non-zero). */
        GLint live_screen_fbo = window_gl_default_framebuffer;
        if(needs_postproc) {
                glGetIntegerv(GL_FRAMEBUFFER_BINDING, &live_screen_fbo);
                if(live_screen_fbo > 0)
                        window_gl_default_framebuffer = (int)live_screen_fbo;
        }

        if(network_map_transfer) {
                glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
        } else {
                float fc[3];
                fog_color_render(fc);
                glClearColor(fc[0], fc[1], fc[2], fog_color[3]);
        }

        if(hud_active->render_world || network_connected) {
                /* Per-frame backup re-capture only when post-processing is on.
                   glGetIntegerv can stall the pipeline, so keep it out of the
                   default gameplay path. */
                if(needs_postproc) {
                        GLint fb = -1;
                        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
                        if(fb > 0) {
                                window_gl_default_framebuffer = fb;
                                live_screen_fbo = fb;
                        }
                }

#if defined(OS_IOS)
                /* Belt-and-braces: if we somehow still don't have a real drawable
                   FBO id, refuse to enter the postproc path. Compositing would
                   target FBO 0 (off-screen on iOS) and the world would vanish.
                   Better to ship the unshaded frame than to lose the game. */
                if(live_screen_fbo == 0)
                        needs_postproc = 0;
#endif

                if(needs_postproc) {
                        if(postproc.texture && (postproc.w != settings.window_width || postproc.h != settings.window_height)) {
                                glDeleteFramebuffers(1, &postproc.fbo);
                                glDeleteRenderbuffers(1, &postproc.depth_rb);
                                glDeleteTextures(1, &postproc.texture);
                                glDeleteTextures(1, &postproc.depth_tex);
                                glDeleteFramebuffers(1, &postproc.vol_fbo);
                                glDeleteTextures(1, &postproc.vol_tex);
                                postproc.fbo = 0;
                                postproc.depth_rb = 0;
                                postproc.texture = 0;
                                postproc.depth_tex = 0;
                                postproc.vol_fbo = 0;
                                postproc.vol_tex = 0;
                        }

                        if(!postproc.texture) {
                                glGenTextures(1, &postproc.texture);
                                glBindTexture(GL_TEXTURE_2D, postproc.texture);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, settings.window_width, settings.window_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        }

                        if(!postproc.fbo) {
#if defined(OS_IOS)
                                /* iOS: do NOT create an off-screen FBO. Repeated attempts to
                                   identify SDL's presented framebuffer and composite into it all
                                   failed — the composited frame went off-screen and the screen
                                   froze. Instead we leave postproc.fbo = 0 and use "copy mode":
                                   render the world straight to the screen (the path that already
                                   works with shaders off), copy it into postproc.texture, then
                                   redraw it through the grade shader. Nothing here needs to know
                                   the screen FBO id. */
#else
                                glGenFramebuffers(1, &postproc.fbo);
                                glBindFramebuffer(GL_FRAMEBUFFER, postproc.fbo);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postproc.texture, 0);
                                /* Use a depth *texture* (not renderbuffer) so the volumetric
                                   light shader can sample it to identify sky pixels. */
                                glGenTextures(1, &postproc.depth_tex);
                                glBindTexture(GL_TEXTURE_2D, postproc.depth_tex);
                                glTexImage2D(GL_TEXTURE_2D, 0,
#ifdef OPENGL_ES
                                        GL_DEPTH_COMPONENT16,
#else
                                        GL_DEPTH_COMPONENT24,
#endif
                                        settings.window_width, settings.window_height, 0,
#ifdef OPENGL_ES
                                        GL_DEPTH_COMPONENT, GL_UNSIGNED_INT,
#else
                                        GL_DEPTH_COMPONENT, GL_FLOAT,
#endif
                                        NULL);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, postproc.depth_tex, 0);
                                if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                                        glDeleteFramebuffers(1, &postproc.fbo);
                                        glDeleteRenderbuffers(1, &postproc.depth_rb);
                                        glDeleteTextures(1, &postproc.texture);
                                        glDeleteTextures(1, &postproc.depth_tex);
                                        postproc.fbo = 0;
                                        postproc.depth_rb = 0;
                                        postproc.texture = 0;
                                        postproc.depth_tex = 0;
                                }
                                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)live_screen_fbo);
                                glBindRenderbuffer(GL_RENDERBUFFER, 0);
#endif
                        }

                        /* Volumetric light output texture + FBO (color only, no depth).
                           This is where the god-rays shader writes its result. */
                        if(settings.volumetric_light && !postproc.vol_tex) {
                                glGenTextures(1, &postproc.vol_tex);
                                glBindTexture(GL_TEXTURE_2D, postproc.vol_tex);
                                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, settings.window_width, settings.window_height, 0,
                                                GL_RGBA, GL_UNSIGNED_BYTE, NULL);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

                                glGenFramebuffers(1, &postproc.vol_fbo);
                                glBindFramebuffer(GL_FRAMEBUFFER, postproc.vol_fbo);
                                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, postproc.vol_tex, 0);
                                if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                                        glDeleteFramebuffers(1, &postproc.vol_fbo);
                                        glDeleteTextures(1, &postproc.vol_tex);
                                        postproc.vol_fbo = 0;
                                        postproc.vol_tex = 0;
                                }
                                glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)live_screen_fbo);
                        }

                        postproc.w = settings.window_width;
                        postproc.h = settings.window_height;

                        if(!postproc.shader) {
#if defined(OPENGL_ES)
                                if(gles_version >= 2) {
                                        const char* vert =
                                                "attribute vec2 a_Position;\n"
                                                "attribute vec2 a_TexCoord;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "void main(){\n"
                                                "    v_TexCoord = a_TexCoord;\n"
                                                "    gl_Position = vec4(a_Position, 0.0, 1.0);\n"
                                                "}\n";
                                        const char* frag =
                                                "precision mediump float;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                // pp_scale = (1+exposure/100)*(1+contrast/100), pp_bias = 0.5*(1-(1+contrast/100))
                                                // exposure folded into contrast: (e*x-0.5)*ct+0.5 == x*(e*ct)+0.5*(1-ct)
                                                // all derived math done once on CPU, not per pixel
                                                "uniform float pp_sat;\n"
                                                "uniform float pp_scale;\n"
                                                "uniform float pp_bias;\n"
                                                "uniform float pp_vig;\n"
                                                "uniform float pp_ca_strength;\n"
                                                "uniform float pp_filmic;\n"
                                                "uniform sampler2D tex;\n"
                                                "vec3 acesFilm(vec3 x){\n"
                                                "    const float a=2.51; const float b=0.03;\n"
                                                "    const float c=2.43; const float d=0.59; const float e=0.14;\n"
                                                "    return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);\n"
                                                "}\n"
                                                "void main(){\n"
                                                "    vec2 uv = v_TexCoord;\n"
                                                "    vec2 dd = uv - 0.5;\n"
                                                "    float r2 = dot(dd, dd);\n"
                                                "    vec3 c;\n"
                                                "    if(pp_ca_strength > 0.001){\n"
                                                "        vec2 off = dd * r2 * pp_ca_strength;\n"
                                                "        c.r = texture2D(tex, uv + off).r;\n"
                                                "        c.g = texture2D(tex, uv).g;\n"
                                                "        c.b = texture2D(tex, uv - off).b;\n"
                                                "    } else {\n"
                                                "        c = texture2D(tex, uv).rgb;\n"
                                                "    }\n"
                                                "    float g = dot(c, vec3(0.299, 0.587, 0.114));\n"
                                                "    c = mix(vec3(g), c, pp_sat);\n"
                                                "    c = c * pp_scale + pp_bias;\n"
                                                "    if(pp_filmic > 0.5) c = acesFilm(c);\n"
                                                "    c *= clamp(1.0 - pp_vig * r2, 0.0, 1.0);\n"
                                                "    gl_FragColor = vec4(c, 1.0);\n"
                                                "}\n";
                                        postproc.shader = glx_shader(vert, frag);
                                } else {
#endif
                                const char* vert = "void main(){gl_TexCoord[0]=gl_MultiTexCoord0;gl_Position=ftransform();}";
                                // derived factors precomputed CPU-side (see pp_* upload); exposure folded into contrast scale
                                const char* frag =
                                        "uniform float pp_sat;"
                                        "uniform float pp_scale;"
                                        "uniform float pp_bias;"
                                        "uniform float pp_vig;"
                                        "uniform float pp_ca_strength;"
                                        "uniform float pp_filmic;"
                                        "uniform sampler2D tex;"
                                        "vec3 acesFilm(vec3 x){"
                                        "const float a=2.51;const float b=0.03;"
                                        "const float c=2.43;const float d=0.59;const float e=0.14;"
                                        "return clamp((x*(a*x+b))/(x*(c*x+d)+e),0.0,1.0);"
                                        "}"
                                        "void main(){"
                                        "vec2 uv=gl_TexCoord[0].xy;"
                                        "vec2 dd=uv-0.5;"
                                        "float r2=dot(dd,dd);"
                                        "vec3 c;"
                                        "if(pp_ca_strength>0.001){"
                                        "vec2 off=dd*r2*pp_ca_strength;"
                                        "c.r=texture2D(tex,uv+off).r;"
                                        "c.g=texture2D(tex,uv).g;"
                                        "c.b=texture2D(tex,uv-off).b;"
                                        "}else{c=texture2D(tex,uv).rgb;}"
                                        "float g=dot(c,vec3(0.299,0.587,0.114));"
                                        "c=mix(vec3(g),c,pp_sat);"
                                        "c=c*pp_scale+pp_bias;"
                                        "if(pp_filmic>0.5)c=acesFilm(c);"
                                        "c*=clamp(1.0-pp_vig*r2,0.0,1.0);"
                                        "gl_FragColor=vec4(c,1.0);}";
                                postproc.shader = glx_shader(vert, frag);
#if defined(OPENGL_ES)
                                }
#endif
                                if(postproc.shader) {
                                        postproc.uni_pp_sat = glGetUniformLocation(postproc.shader, "pp_sat");
                                        postproc.uni_pp_scale = glGetUniformLocation(postproc.shader, "pp_scale");
                                        postproc.uni_pp_bias = glGetUniformLocation(postproc.shader, "pp_bias");
                                        postproc.uni_pp_vig = glGetUniformLocation(postproc.shader, "pp_vig");
                                        postproc.uni_pp_ca_strength = glGetUniformLocation(postproc.shader, "pp_ca_strength");
                                        postproc.uni_pp_filmic = glGetUniformLocation(postproc.shader, "pp_filmic");
                                        // sampler binding never changes — set once here, not per frame
                                        glUseProgram(postproc.shader);
                                        glUniform1i(glGetUniformLocation(postproc.shader, "tex"), 0);
                                        glUseProgram(0);
                                }
                        }

                        /* Compile the volumetric light (god-rays) shader once.
                           Faithful port of Luanti's client/shaders/volumetric_light/opengl_fragment.glsl:
                           30-sample radial blur toward the screen-space sun position, modulated
                           by depth (sky pixels have depth==1.0), Preetham atmospheric scattering
                           tint, and additive blend onto the original color. */
                        if(settings.volumetric_light && !postproc.vol_shader) {
#if defined(OPENGL_ES)
                                if(gles_version >= 2) {
                                        const char* vvert =
                                                "attribute vec2 a_Position;\n"
                                                "attribute vec2 a_TexCoord;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "void main(){\n"
                                                "    v_TexCoord = a_TexCoord;\n"
                                                "    gl_Position = vec4(a_Position, 0.0, 1.0);\n"
                                                "}\n";
                                        const char* vfrag =
                                                "precision mediump float;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "uniform sampler2D rendered;\n"
                                                "uniform sampler2D depthmap;\n"
                                                "uniform vec3 sunPositionScreen;\n"
                                                "uniform float sunBrightness;\n"
                                                "uniform float volumetricLightStrength;\n"
                                                "uniform vec3 dayLight;\n"
                                                "uniform vec3 v_LightDirection;\n"
                                                "float noise(vec3 uvd){\n"
                                                "    return fract(dot(sin(uvd*vec3(13041.19699,27723.29171,61029.77801)),vec3(73137.11101,37312.92319,10108.89991)));\n"
                                                "}\n"
                                                "float sampleVolumetricLight(vec2 uv, vec3 lightVec, float rawDepth){\n"
                                                "    lightVec = 0.5*lightVec/lightVec.z + 0.5;\n"
                                                "    const float samples = 30.0;\n"
                                                "    float result = texture2D(depthmap, uv).r < 1.0 ? 0.0 : 1.0;\n"
                                                "    float bias = noise(vec3(uv, rawDepth));\n"
                                                "    vec2 samplepos;\n"
                                                "    for (float i = 1.0; i < samples; i++) {\n"
                                                "        samplepos = mix(uv, lightVec.xy, (i + bias) / samples);\n"
                                                "        if (min(samplepos.x, samplepos.y) > 0.0 && max(samplepos.x, samplepos.y) < 1.0)\n"
                                                "            result += texture2D(depthmap, samplepos).r < 1.0 ? 0.0 : 1.0;\n"
                                                "    }\n"
                                                "    return result / samples * pow(texture2D(depthmap, uv).r, 128.0);\n"
                                                "}\n"
                                                "vec3 getDirectLightScatteringAtGround(vec3 L){\n"
                                                "    const float beta_r0 = 1e-5;\n"
                                                "    const vec3 beta_r0_l = vec3(3.3362176e-01, 8.75378289198826e-01, 1.95342379700656) * beta_r0;\n"
                                                "    const float atmosphere_height = 15000.0;\n"
                                                "    return exp(-beta_r0_l * atmosphere_height / (1e-5 - dot(L, vec3(0.0, 1.0, 0.0))));\n"
                                                "}\n"
                                                "void main(){\n"
                                                "    vec2 uv = v_TexCoord;\n"
                                                "    vec3 color = texture2D(rendered, uv).rgb;\n"
                                                "    if (volumetricLightStrength > 0.0 && sunBrightness > 0.0 && sunPositionScreen.z > 0.0) {\n"
                                                "        float rawDepth = texture2D(depthmap, uv).r;\n"
                                                "        vec3 lookDirection = normalize(vec3(uv.x*2.0-1.0, uv.y*2.0-1.0, rawDepth));\n"
                                                "        const float boost = 4.0;\n"
                                                "        vec3 sourcePosition = sunPositionScreen;\n"
                                                "        float cameraDirectionFactor = pow(clamp(dot(sourcePosition, vec3(0.0,0.0,1.0)), 0.0, 0.7), 2.5);\n"
                                                "        float viewAngleFactor = pow(max(0.0, dot(sourcePosition, lookDirection)), 8.0);\n"
                                                "        float lightFactor = sunBrightness * sampleVolumetricLight(uv, sourcePosition, rawDepth) *\n"
                                                "                            (0.05*cameraDirectionFactor + 0.95*viewAngleFactor);\n"
                                                "        vec3 godray_color = boost * getDirectLightScatteringAtGround(v_LightDirection) * dayLight;\n"
                                                "        color += godray_color * lightFactor * volumetricLightStrength * 2.0;\n"
                                                "    }\n"
                                                "    gl_FragColor = vec4(color, 1.0);\n"
                                                "}\n";
                                        postproc.vol_shader = glx_shader(vvert, vfrag);
                                } else {
#endif
                                const char* vvert = "void main(){gl_TexCoord[0]=gl_MultiTexCoord0;gl_Position=ftransform();}";
                                const char* vfrag =
                                        "uniform sampler2D rendered;"
                                        "uniform sampler2D depthmap;"
                                        "uniform vec3 sunPositionScreen;"
                                        "uniform float sunBrightness;"
                                        "uniform float volumetricLightStrength;"
                                        "uniform vec3 dayLight;"
                                        "uniform vec3 v_LightDirection;"
                                        "float noise(vec3 uvd){"
                                        "return fract(dot(sin(uvd*vec3(13041.19699,27723.29171,61029.77801)),vec3(73137.11101,37312.92319,10108.89991)));"
                                        "}"
                                        "float sampleVolumetricLight(vec2 uv, vec3 lightVec, float rawDepth){"
                                        "lightVec=0.5*lightVec/lightVec.z+0.5;"
                                        "const float samples=30.0;"
                                        "float result=texture2D(depthmap,uv).r<1.0?0.0:1.0;"
                                        "float bias=noise(vec3(uv,rawDepth));"
                                        "vec2 samplepos;"
                                        "for(float i=1.0;i<samples;i++){"
                                        "samplepos=mix(uv,lightVec.xy,(i+bias)/samples);"
                                        "if(min(samplepos.x,samplepos.y)>0.0&&max(samplepos.x,samplepos.y)<1.0)"
                                        "result+=texture2D(depthmap,samplepos).r<1.0?0.0:1.0;"
                                        "}"
                                        "return result/samples*pow(texture2D(depthmap,uv).r,128.0);"
                                        "}"
                                        "vec3 getDirectLightScatteringAtGround(vec3 L){"
                                        "const float beta_r0=1e-5;"
                                        "const vec3 beta_r0_l=vec3(3.3362176e-01,8.75378289198826e-01,1.95342379700656)*beta_r0;"
                                        "const float atmosphere_height=15000.0;"
                                        "return exp(-beta_r0_l*atmosphere_height/(1e-5-dot(L,vec3(0.0,1.0,0.0))));"
                                        "}"
                                        "void main(){"
                                        "vec2 uv=gl_TexCoord[0].xy;"
                                        "vec3 color=texture2D(rendered,uv).rgb;"
                                        "if(volumetricLightStrength>0.0&&sunBrightness>0.0&&sunPositionScreen.z>0.0){"
                                        "float rawDepth=texture2D(depthmap,uv).r;"
                                        "vec3 lookDirection=normalize(vec3(uv.x*2.0-1.0,uv.y*2.0-1.0,rawDepth));"
                                        "const float boost=4.0;"
                                        "vec3 sourcePosition=sunPositionScreen;"
                                        "float cameraDirectionFactor=pow(clamp(dot(sourcePosition,vec3(0.0,0.0,1.0)),0.0,0.7),2.5);"
                                        "float viewAngleFactor=pow(max(0.0,dot(sourcePosition,lookDirection)),8.0);"
                                        "float lightFactor=sunBrightness*sampleVolumetricLight(uv,sourcePosition,rawDepth)*"
                                        "(0.05*cameraDirectionFactor+0.95*viewAngleFactor);"
                                        "vec3 godray_color=boost*getDirectLightScatteringAtGround(v_LightDirection)*dayLight;"
                                        "color+=godray_color*lightFactor*volumetricLightStrength*2.0;"
                                        "}"
                                        "gl_FragColor=vec4(color,1.0);"
                                        "}";
                                postproc.vol_shader = glx_shader(vvert, vfrag);
#if defined(OPENGL_ES)
                                }
#endif
                                if(postproc.vol_shader) {
                                        postproc.uni_vol_sun_pos = glGetUniformLocation(postproc.vol_shader, "sunPositionScreen");
                                        postproc.uni_vol_sun_brightness = glGetUniformLocation(postproc.vol_shader, "sunBrightness");
                                        postproc.uni_vol_strength = glGetUniformLocation(postproc.vol_shader, "volumetricLightStrength");
                                        postproc.uni_vol_daylight = glGetUniformLocation(postproc.vol_shader, "dayLight");
                                        postproc.uni_vol_lightdir = glGetUniformLocation(postproc.vol_shader, "v_LightDirection");
                                        // sampler bindings never change — set once here
                                        glUseProgram(postproc.vol_shader);
                                        glUniform1i(glGetUniformLocation(postproc.vol_shader, "rendered"), 0);
                                        glUniform1i(glGetUniformLocation(postproc.vol_shader, "depthmap"), 1);
                                        glUseProgram(0);
                                }
                        }

                        /* Compile the mode 2 directional volumetric light shader.
                           Mode 2 uses the SAME sun screen position as mode 1 and lens flare,
                           but with a longer march range and stronger intensity, making the
                           rays visible from any angle — not just when looking at the sun.
                           The shader marches from each pixel toward the sun's screen UV and
                           counts how many samples hit sky (depth == 1.0) vs terrain. */
                        if(settings.volumetric_light && !postproc.vol2_shader) {
#if defined(OPENGL_ES)
                                if(gles_version >= 2) {
                                        const char* v2vert =
                                                "attribute vec2 a_Position;\n"
                                                "attribute vec2 a_TexCoord;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "void main(){\n"
                                                "    v_TexCoord = a_TexCoord;\n"
                                                "    gl_Position = vec4(a_Position, 0.0, 1.0);\n"
                                                "}\n";
                                        const char* v2frag =
                                                "precision mediump float;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "uniform sampler2D rendered;\n"
                                                "uniform sampler2D depthmap;\n"
                                                "uniform vec3 sunPositionScreen;\n"
                                                "uniform float sunBrightness;\n"
                                                "uniform float volumetricLightStrength;\n"
                                                "uniform float rayBrightness;\n"
                                                "uniform float rayRange;\n"
                                                "uniform vec3 dayLight;\n"
                                                "float noise(vec3 uvd){\n"
                                                "    return fract(dot(sin(uvd*vec3(13041.19699,27723.29171,61029.77801)),vec3(73137.11101,37312.92319,10108.89991)));\n"
                                                "}\n"
                                                "void main(){\n"
                                                "    vec2 uv = v_TexCoord;\n"
                                                "    vec3 color = texture2D(rendered, uv).rgb;\n"
                                                "    if (sunBrightness > 0.0 && volumetricLightStrength > 0.0 && sunPositionScreen.z > 0.0) {\n"
                                                "        vec2 sunUV = 0.5 * sunPositionScreen.xy / sunPositionScreen.z + 0.5;\n"
                                                "        float rawDepth = texture2D(depthmap, uv).r;\n"
                                                "        const float samples = 50.0;\n"
                                                "        float result = 0.0;\n"
                                                "        float bias = noise(vec3(uv, rawDepth));\n"
                                                "        vec2 dir = sunUV - uv;\n"
                                                "        for (float i = 0.0; i < samples; i++) {\n"
                                                "            float t = (i + bias) / samples;\n"
                                                "            vec2 samplepos = uv + dir * t;\n"
                                                "            if (min(samplepos.x, samplepos.y) > 0.0 && max(samplepos.x, samplepos.y) < 1.0) {\n"
                                                "                float d = texture2D(depthmap, samplepos).r;\n"
                                                "                result += (d < 1.0) ? 0.0 : 1.0;\n"
                                                "            }\n"
                                                "        }\n"
                                                "        float occlusion = result / samples;\n"
                                                "        float distToSun = length(dir);\n"
                                                "        float falloff = 1.0 - clamp(distToSun * 0.7 / rayRange, 0.0, 0.85);\n"
                                                "        vec3 rayColor = dayLight * rayBrightness;\n"
                                                "        color += rayColor * occlusion * falloff * sunBrightness * volumetricLightStrength * 3.0;\n"
                                                "    }\n"
                                                "    gl_FragColor = vec4(color, 1.0);\n"
                                                "}\n";
                                        postproc.vol2_shader = glx_shader(v2vert, v2frag);
                                } else {
#else
                                const char* v2vert = "void main(){gl_TexCoord[0]=gl_MultiTexCoord0;gl_Position=ftransform();}";
                                const char* v2frag =
                                        "uniform sampler2D rendered;"
                                        "uniform sampler2D depthmap;"
                                        "uniform vec3 sunPositionScreen;"
                                        "uniform float sunBrightness;"
                                        "uniform float volumetricLightStrength;"
                                        "uniform float rayBrightness;"
                                        "uniform float rayRange;"
                                        "uniform vec3 dayLight;"
                                        "float noise(vec3 uvd){"
                                        "return fract(dot(sin(uvd*vec3(13041.19699,27723.29171,61029.77801)),vec3(73137.11101,37312.92319,10108.89991)));"
                                        "}"
                                        "void main(){"
                                        "vec2 uv=gl_TexCoord[0].xy;"
                                        "vec3 color=texture2D(rendered,uv).rgb;"
                                        "if(sunBrightness>0.0&&volumetricLightStrength>0.0&&sunPositionScreen.z>0.0){"
                                        "vec2 sunUV=0.5*sunPositionScreen.xy/sunPositionScreen.z+0.5;"
                                        "float rawDepth=texture2D(depthmap,uv).r;"
                                        "const float samples=50.0;"
                                        "float result=0.0;"
                                        "float bias=noise(vec3(uv,rawDepth));"
                                        "vec2 dir=sunUV-uv;"
                                        "for(float i=0.0;i<samples;i++){"
                                        "float t=(i+bias)/samples;"
                                        "vec2 samplepos=uv+dir*t;"
                                        "if(min(samplepos.x,samplepos.y)>0.0&&max(samplepos.x,samplepos.y)<1.0){"
                                        "float d=texture2D(depthmap,samplepos).r;"
                                        "result+=(d<1.0)?0.0:1.0;"
                                        "}"
                                        "}"
                                        "float occlusion=result/samples;"
                                        "float distToSun=length(dir);"
                                        "float falloff=1.0-clamp(distToSun*0.7/rayRange,0.0,0.85);"
                                        "vec3 rayColor=dayLight*rayBrightness;"
                                        "color+=rayColor*occlusion*falloff*sunBrightness*volumetricLightStrength*3.0;"
                                        "}"
                                        "gl_FragColor=vec4(color,1.0);"
                                        "}";
                                postproc.vol2_shader = glx_shader(v2vert, v2frag);
#if defined(OPENGL_ES)
                                }
#endif
#endif
#if defined(OPENGL_ES)
                                /* Close the "else" branch opened by "} else {" above.
                                   On ES builds the else block was opened but its
                                   matching close was inside the inactive #else
                                   branch, leaving the block unclosed. */
                                }
#endif
                                if(postproc.vol2_shader) {
                                        postproc.uni_vol2_sun_dir = glGetUniformLocation(postproc.vol2_shader, "sunPositionScreen");
                                        postproc.uni_vol2_sun_brightness = glGetUniformLocation(postproc.vol2_shader, "sunBrightness");
                                        postproc.uni_vol2_strength = glGetUniformLocation(postproc.vol2_shader, "volumetricLightStrength");
                                        postproc.uni_vol2_brightness = glGetUniformLocation(postproc.vol2_shader, "rayBrightness");
                                        postproc.uni_vol2_range = glGetUniformLocation(postproc.vol2_shader, "rayRange");
                                        postproc.uni_vol2_daylight = glGetUniformLocation(postproc.vol2_shader, "dayLight");
                                        postproc.uni_vol2_lightdir = -1;
                                        glUseProgram(postproc.vol2_shader);
                                        glUniform1i(glGetUniformLocation(postproc.vol2_shader, "rendered"), 0);
                                        glUniform1i(glGetUniformLocation(postproc.vol2_shader, "depthmap"), 1);
                                        glUseProgram(0);
                                }
                        }

                        /* Compile the lens flare shader once. Half-size flare. */
                        if(settings.lens_flare && !postproc.flare_shader) {
#if defined(OPENGL_ES)
                                if(gles_version >= 2) {
                                        const char* fvert =
                                                "attribute vec2 a_Position;\n"
                                                "attribute vec2 a_TexCoord;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "void main(){\n"
                                                "    v_TexCoord = a_TexCoord;\n"
                                                "    gl_Position = vec4(a_Position, 0.0, 1.0);\n"
                                                "}\n";
                                        const char* ffrag =
                                                "precision mediump float;\n"
                                                "varying vec2 v_TexCoord;\n"
                                                "uniform sampler2D rendered;\n"
                                                "uniform sampler2D depthmap;\n"
                                                "uniform vec3 sunPositionScreen;\n"
                                                "uniform float sunBrightness;\n"
                                                "uniform float lensFlareStrength;\n"
                                                "uniform float includeScene;\n"
                                                "void main(){\n"
                                                "    vec2 uv = v_TexCoord;\n"
                                                "    vec3 color = vec3(0.0);\n"
                                                "    if (includeScene > 0.5) color = texture2D(rendered, uv).rgb;\n"
                                                "    if (sunBrightness > 0.0 && lensFlareStrength > 0.0 && sunPositionScreen.z > 0.0) {\n"
                                                "        vec2 sunUV = 0.5 * sunPositionScreen.xy / sunPositionScreen.z + 0.5;\n"
                                                "        float sunDepth = texture2D(depthmap, sunUV).r;\n"
                                                "        float occlusion = (sunDepth < 1.0) ? 0.15 : 1.0;\n"
                                                "        vec2 dir = uv - sunUV;\n"
                                                "        float dist = length(dir);\n"
                                                "        vec3 flare = vec3(0.0);\n"
                                                "        flare += vec3(1.0, 0.95, 0.8) * exp(-dist * 20.0) * 0.75;\n"
                                                "        vec2 toCenter = vec2(0.5) - sunUV;\n"
                                                "        float axisLen = length(toCenter);\n"
                                                "        if (axisLen > 0.001) {\n"
                                                "            vec2 axis = toCenter / axisLen;\n"
                                                "            vec2 g1 = sunUV + axis * 0.25;\n"
                                                "            flare += vec3(0.6, 0.7, 1.0) * exp(-length(uv - g1) * 60.0) * 0.3;\n"
                                                "            vec2 g2 = sunUV + axis * 0.5;\n"
                                                "            flare += vec3(0.9, 0.6, 1.0) * exp(-length(uv - g2) * 50.0) * 0.25;\n"
                                                "            vec2 g3 = sunUV + axis * 0.75;\n"
                                                "            flare += vec3(1.0, 0.8, 0.5) * exp(-length(uv - g3) * 40.0) * 0.2;\n"
                                                "            vec2 g4 = sunUV + axis * 1.1;\n"
                                                "            flare += vec3(0.5, 1.0, 0.8) * exp(-length(uv - g4) * 36.0) * 0.175;\n"
                                                "            vec2 g5 = sunUV - axis * 0.175;\n"
                                                "            flare += vec3(0.7, 0.5, 1.0) * exp(-length(uv - g5) * 80.0) * 0.15;\n"
                                                "        }\n"
                                                "        color += flare * lensFlareStrength * occlusion * sunBrightness;\n"
                                                "    }\n"
                                                "    gl_FragColor = vec4(color, 1.0);\n"
                                                "}\n";
                                        postproc.flare_shader = glx_shader(fvert, ffrag);
                                } else {
#else
                                const char* fvert = "void main(){gl_TexCoord[0]=gl_MultiTexCoord0;gl_Position=ftransform();}";
                                const char* ffrag =
                                        "uniform sampler2D rendered;"
                                        "uniform sampler2D depthmap;"
                                        "uniform vec3 sunPositionScreen;"
                                        "uniform float sunBrightness;"
                                        "uniform float lensFlareStrength;"
                                        "uniform float includeScene;"
                                        "void main(){"
                                        "vec2 uv=gl_TexCoord[0].xy;"
                                        "vec3 color=vec3(0.0);"
                                        "if(includeScene>0.5)color=texture2D(rendered,uv).rgb;"
                                        "if(sunBrightness>0.0&&lensFlareStrength>0.0&&sunPositionScreen.z>0.0){"
                                        "vec2 sunUV=0.5*sunPositionScreen.xy/sunPositionScreen.z+0.5;"
                                        "float sunDepth=texture2D(depthmap,sunUV).r;"
                                        "float occlusion=(sunDepth<1.0)?0.15:1.0;"
                                        "vec2 dir=uv-sunUV;"
                                        "float dist=length(dir);"
                                        "vec3 flare=vec3(0.0);"
                                        "flare+=vec3(1.0,0.95,0.8)*exp(-dist*20.0)*0.75;"
                                        "vec2 toCenter=vec2(0.5)-sunUV;"
                                        "float axisLen=length(toCenter);"
                                        "if(axisLen>0.001){"
                                        "vec2 axis=toCenter/axisLen;"
                                        "vec2 g1=sunUV+axis*0.25;"
                                        "flare+=vec3(0.6,0.7,1.0)*exp(-length(uv-g1)*60.0)*0.3;"
                                        "vec2 g2=sunUV+axis*0.5;"
                                        "flare+=vec3(0.9,0.6,1.0)*exp(-length(uv-g2)*50.0)*0.25;"
                                        "vec2 g3=sunUV+axis*0.75;"
                                        "flare+=vec3(1.0,0.8,0.5)*exp(-length(uv-g3)*40.0)*0.2;"
                                        "vec2 g4=sunUV+axis*1.1;"
                                        "flare+=vec3(0.5,1.0,0.8)*exp(-length(uv-g4)*36.0)*0.175;"
                                        "vec2 g5=sunUV-axis*0.175;"
                                        "flare+=vec3(0.7,0.5,1.0)*exp(-length(uv-g5)*80.0)*0.15;"
                                        "}"
                                        "color+=flare*lensFlareStrength*occlusion*sunBrightness;"
                                        "}"
                                        "gl_FragColor=vec4(color,1.0);"
                                        "}";
                                postproc.flare_shader = glx_shader(fvert, ffrag);
#if defined(OPENGL_ES)
                                }
#endif
#endif
#if defined(OPENGL_ES)
                                /* Close the "else" branch opened by "} else {" above.
                                   Same fix as the vol2_shader block above. */
                                }
#endif
                                if(postproc.flare_shader) {
                                        postproc.uni_flare_sun_pos = glGetUniformLocation(postproc.flare_shader, "sunPositionScreen");
                                        postproc.uni_flare_sun_brightness = glGetUniformLocation(postproc.flare_shader, "sunBrightness");
                                        postproc.uni_flare_strength = glGetUniformLocation(postproc.flare_shader, "lensFlareStrength");
                                        postproc.uni_flare_include_scene = glGetUniformLocation(postproc.flare_shader, "includeScene");
                                        glUseProgram(postproc.flare_shader);
                                        glUniform1i(glGetUniformLocation(postproc.flare_shader, "rendered"), 0);
                                        glUniform1i(glGetUniformLocation(postproc.flare_shader, "depthmap"), 1);
                                        glUseProgram(0);
                                }
                        }

#if defined(OS_IOS)
                        /* Copy mode: no FBO required, only the grade shader. */
                        if(!postproc.shader)
                                needs_postproc = 0;
#else
                        if(!postproc.fbo || !postproc.shader)
                                needs_postproc = 0;
#endif

                        if(needs_postproc && postproc.fbo)
                                glBindFramebuffer(GL_FRAMEBUFFER, postproc.fbo);
                }

                glEnable(GL_DEPTH_TEST);
                glDepthRange(0.0F, 1.0F);

                chunk_update_all();

                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

                float saved_render_distance = settings.render_distance;
                if(camera_mode == CAMERAMODE_SPECTATOR)
                        settings.render_distance = settings.spectator_fog_distance;

                if(settings.opengl14) {
                        matrix_identity(matrix_projection);
                        matrix_perspective(matrix_projection, camera_fov_scaled(dt_float),
                                                           ((float)settings.window_width) / ((float)settings.window_height), 0.1F,
                                                           settings.render_distance + CHUNK_SIZE * 4.0F);
                        matrix_upload_p();

                        matrix_identity(matrix_view);
                        camera_apply();
                        matrix_identity(matrix_model);
                        matrix_upload();

                        float lpos[4] = {0.0F, -1.0F, 1.0F, 0.0F};
                        glLightfv(GL_LIGHT0, GL_POSITION, lpos);
                }

                camera_ExtractFrustum();

                /* Snapshot the camera matrices now, while they still describe
                   the real 3D scene -- everything from here on (view-model
                   rendering, HUD ortho projection, etc.) will overwrite
                   matrix_view/matrix_projection for its own purposes. */
                if(settings.damage_numbers)
                        damagenumbers_capture_camera();

                if(!network_map_transfer) {
                        /* Sky gradient: drawn as a background BEFORE the 3D scene so
                           terrain correctly draws on top of it. The gradient makes the
                           upper sky darker (like real atmospheric perspective), blending
                           from fog_color at the horizon to a darker tint at the top.
                           Previously this was drawn AFTER the scene with GL_FLAT shade
                           model still active, which made per-vertex colors not
                           interpolate — the entire quad rendered as fog_color (invisible). */
                        if(settings.sky_gradient) {
                                glMatrixMode(GL_PROJECTION);
                                glPushMatrix();
                                glLoadIdentity();
                                glOrtho(0.0, settings.window_width, 0.0, settings.window_height, -1.0, 1.0);
                                glMatrixMode(GL_MODELVIEW);
                                glPushMatrix();
                                glLoadIdentity();

                                glDisable(GL_DEPTH_TEST);
                                glDepthMask(GL_FALSE);
                                glDisable(GL_TEXTURE_2D);
#ifndef OPENGL_ES
                                glShadeModel(GL_SMOOTH);
#endif

                                float h = (float)settings.window_height;
                                float w = (float)settings.window_width;
                                float horizon = h * 0.5F;
                                float intensity = settings.sky_gradient_intensity;
                                /* Use the render-time fog color (darkened 10%
                                   when filmic is on) so the sky gradient
                                   horizon matches the actual fog color. */
                                float fc[3];
                                fog_color_render(fc);
                                float zr = fc[0] + (0.18F - fc[0]) * intensity;
                                float zg = fc[1] + (0.28F - fc[1]) * intensity;
                                float zb = fc[2] + (0.55F - fc[2]) * intensity;

                                glx_draw_gradient_quad_2d(0, h, w, h - horizon, zr, zg, zb,
                                                           fc[0], fc[1], fc[2]);

                                glDepthMask(GL_TRUE);
                                glEnable(GL_DEPTH_TEST);

                                glMatrixMode(GL_MODELVIEW);
                                glPopMatrix();
                                glMatrixMode(GL_PROJECTION);
                                glPopMatrix();
                                glMatrixMode(GL_MODELVIEW);
                        }

                        water_reflection_pass();
                        glx_enable_sphericalfog();
                        drawScene();
                        settings.render_distance = saved_render_distance;

                        int render_fpv = (camera_mode == CAMERAMODE_FPS)
                                || ((camera_mode == CAMERAMODE_BODYVIEW || camera_mode == CAMERAMODE_SPECTATOR)
                                        && cameracontroller_bodyview_mode);
                        int is_local = (camera_mode == CAMERAMODE_FPS) || (cameracontroller_bodyview_player == local_player_id);
                        int local_id = (camera_mode == CAMERAMODE_FPS) ? local_player_id : cameracontroller_bodyview_player;
                        
                        // Validate local_id before any array access to prevent crashes in spectator mode
                        if(local_id < 0 || local_id >= PLAYERS_MAX) {
                                local_id = local_player_id;
                        }

                        if(players[local_player_id].items_show && window_time() - players[local_player_id].items_show_start >= 0.5F)
                                players[local_player_id].items_show = 0;

                        if(camera_mode == CAMERAMODE_FPS) {
                                weapon_update();
                                
                                /* Check for pending block placement when landing */
                                if(local_player_pending_block_active && !players[local_player_id].physics.airborne) {
                                        float ex, ey, ez;
                                        camera_local_eye(&ex, &ey, &ez);
                                        int* pos = camera_terrain_pick_local(0);
                                        if(pos != NULL && pos[1] > 1
                                           && chebyshev(pos[0] - ex, pos[1] - ey, pos[2] - ez) < 3.0F
                                           && !overlaps_with_player(pos[0], pos[1], pos[2])) {
                                                players[local_player_id].item_showup = window_time();
                                                local_player_blocks = max(local_player_blocks - 1, 0);

                                                struct PacketBlockAction blk;
                                                blk.player_id = local_player_id;
                                                blk.action_type = ACTION_BUILD;
                                                blk.x = pos[0];
                                                blk.y = pos[2];
                                                blk.z = 63 - pos[1];
                                                network_send(PACKET_BLOCKACTION_ID, &blk, sizeof(blk));
                                        }
                                        local_player_pending_block_active = 0;
                                }
                                
                                if(players[local_player_id].input.buttons.lmb && players[local_player_id].held_item == TOOL_BLOCK
                                   && (window_time() - players[local_player_id].item_showup) >= 0.5F && local_player_blocks > 0) {
                                        float ex, ey, ez;
                                        camera_local_eye(&ex, &ey, &ez);
                                        int* pos = camera_terrain_pick_local(0);
                                        if(pos != NULL && pos[1] > 1
                                           && chebyshev(pos[0] - ex, pos[1] - ey, pos[2] - ez) < 3.0F
                                           && !overlaps_with_player(pos[0], pos[1], pos[2])) {
                                                players[local_player_id].item_showup = window_time();
                                                local_player_blocks = max(local_player_blocks - 1, 0);

                                                struct PacketBlockAction blk;
                                                blk.player_id = local_player_id;
                                                blk.action_type = ACTION_BUILD;
                                                blk.x = pos[0];
                                                blk.y = pos[2];
                                                blk.z = 63 - pos[1];
                                                network_send(PACKET_BLOCKACTION_ID, &blk, sizeof(blk));
                                                // read_PacketBlockAction(&blk,sizeof(blk));
                                        } else if(pos != NULL && pos[1] > 1
                                                  && chebyshev(pos[0] - ex, pos[1] - ey, pos[2] - ez) < 3.0F
                                                  && overlaps_with_player(pos[0], pos[1], pos[2])
                                                  && !local_player_pending_block_active) {
                                                /* Queue block placement for when we land */
                                                local_player_pending_block_active = 1;
                                                local_player_pending_block_x = pos[0];
                                                local_player_pending_block_y = pos[1];
                                                local_player_pending_block_z = pos[2];
                                        }
                                }
                                if(players[local_player_id].input.buttons.lmb && players[local_player_id].held_item == TOOL_GRENADE
                                   && window_time() - players[local_player_id].input.buttons.lmb_start > 3.0F) {
                                        local_player_grenades = max(local_player_grenades - 1, 0);
                                        struct PacketGrenade g;
                                        g.player_id = local_player_id;
                                        g.x = players[local_player_id].pos.x;
                                        g.y = players[local_player_id].pos.z;
                                        g.z = 63.0F - players[local_player_id].pos.y;
                                        g.fuse_length = g.vx = g.vy = g.vz = 0.0F;
                                        network_send(PACKET_GRENADE_ID, &g, sizeof(g));
                                        read_PacketGrenade(&g, sizeof(g));
                                        players[local_player_id].input.buttons.lmb_start = window_time();
                                }
                        }

                        local_player_drag_amount = 0;
                        int* pos = NULL;
                        float pick_ox = camera_x, pick_oy = camera_y, pick_oz = camera_z;
                        switch(players[local_id].held_item) {
                                case TOOL_BLOCK:
                                        if(!players[local_id].input.keys.sprint && render_fpv) {
                                                if(is_local) {
                                                        pos = camera_terrain_pick_local(0);
                                                        camera_local_eye(&pick_ox, &pick_oy, &pick_oz);
                                                } else
                                                        pos = camera_terrain_pickEx(
                                                                0, camera_x, camera_y, camera_z, players[local_id].orientation_smooth.x,
                                                                players[local_id].orientation_smooth.y, players[local_id].orientation_smooth.z);
                                        }
                                        break;
                                default: pos = NULL;
                        }
                        if(pos != NULL && pos[1] > 1
                           && chebyshev(pos[0] - pick_ox, pos[1] - pick_oy, pos[2] - pick_oz) < 3.0F
                           && !overlaps_with_player(pos[0], pos[1], pos[2])) {
                                matrix_upload();
                                glDisable(GL_DEPTH_TEST);
                                glDepthMask(GL_FALSE);
                                struct Point cubes[64];
                                int amount = 0;
                                if(is_local && local_player_drag_active && players[local_player_id].input.buttons.rmb
                                   && players[local_player_id].held_item == TOOL_BLOCK) {
                                        amount = map_cube_line(local_player_drag_x, local_player_drag_z, 63 - local_player_drag_y, pos[0],
                                                                                   pos[2], 63 - pos[1], cubes);
                                        local_player_drag_amount = amount;
                                } else {
                                        amount = 1;
                                        cubes[0].x = pos[0];
                                        cubes[0].y = pos[2];
                                        cubes[0].z = 63 - pos[1];
                                }
                                glEnableClientState(GL_VERTEX_ARRAY);
                                float drag_active_f = (float)local_player_drag_active;
                                int line_invalid = 0;
                                if(amount > local_player_blocks) {
                                        line_invalid = 1;
                                }
                                float r, g, b;
                                if(line_invalid) {
                                        r = 1.0F; g = 0.0F; b = 0.0F;
                                } else if(drag_active_f > 0.5F) {
                                        r = 1.0F; g = 1.0F; b = 0.0F;
                                } else {
                                        r = 1.0F - drag_active_f; g = 1.0F - drag_active_f; b = 1.0F - drag_active_f;
                                }
                                glColor3f(r, g, b);
                                glLineWidth(1.0F + 7.0F * drag_active_f);
                                while(amount > 0) {
                                        int tmp = cubes[amount - 1].y;
                                        cubes[amount - 1].y = 63 - cubes[amount - 1].z;
                                        cubes[amount - 1].z = tmp;
                                        short vertices[72] = {cubes[amount - 1].x,         cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y,         cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y,         cubes[amount - 1].z + 1,

                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z + 1,

                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y,         cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x + 1, cubes[amount - 1].y + 1, cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y,         cubes[amount - 1].z + 1,
                                                                                  cubes[amount - 1].x,     cubes[amount - 1].y + 1, cubes[amount - 1].z + 1};
                                        glVertexPointer(3, GL_SHORT, 0, vertices);
                                        glDrawArrays(GL_LINES, 0, 24);
                                        amount--;
                                }
                                glDisableClientState(GL_VERTEX_ARRAY);
                                glEnable(GL_DEPTH_TEST);
                                glDepthMask(GL_TRUE);
                        }

                        if(window_time() - players[local_player_id].item_disabled < 0.3F) {
                                players[local_player_id].item_showup = window_time();
                                if(players[local_player_id].input.buttons.lmb)
                                        players[local_player_id].input.buttons.lmb_start = window_time() + 0.5F;
                                if(players[local_player_id].input.buttons.rmb)
                                        players[local_player_id].input.buttons.rmb_start = window_time() + 0.5F;
                                players[local_player_id].input.buttons.rmb = 0;
                        }
			if(1) {
				/* swap player models to interpolated render positions for the
				   ENTIRE scene pass (local viewmodel, player_render_all and
				   hud render_3D), then restore the true simulation state at
				   the end of display(). Previously this wrapper only covered
				   render_3D, so camera + grenades were interpolated while
				   player models (incl. the FPV gun block above) were not -
				   that mismatch was the movement judder. */
				player_render_interpolate_begin();
				if(hud_active->render_localplayer) {
                                        float tmp2 = players[local_player_id].physics.eye.y;
                                        players[local_player_id].physics.eye.y = last_cy;
                                        if(camera_mode == CAMERAMODE_FPS)
                                                glDepthRange(0.0F, 0.05F);
                                        matrix_push(matrix_projection);
                                        matrix_translate(matrix_projection, 0.0F, -0.25F, 0.0F);
                                        matrix_upload_p();
#ifdef OPENGL_ES
                                        if(camera_mode == CAMERAMODE_FPS)
                                                glx_disable_sphericalfog();
#endif
                                        player_render(&players[local_player_id], local_player_id);
#ifdef OPENGL_ES
                                        if(camera_mode == CAMERAMODE_FPS)
                                                glx_enable_sphericalfog();
#endif
                                        matrix_pop(matrix_projection);
                                        glDepthRange(0.0F, 1.0F);
                                        players[local_player_id].physics.eye.y = tmp2;
                                }
                        }

                        matrix_upload_p();
                        matrix_upload();
                        player_render_all();

                        matrix_upload();
                        map_collapsing_render();
                        matrix_upload();

                        /* Camera inside (or poking into) a solid voxel:
                           black out the view instead of showing the fog
                           color or seeing through the block.  The old code
                           glClear()ed here, but the clear color is the fog
                           color -- so it showed fog, and it only triggered
                           for the center voxel, never for partial
                           penetration. */
                        camera_inside_block_render();

                        glx_disable_sphericalfog();
                        if(settings.smooth_fog)
                                glDisable(GL_FOG);

                        if(needs_postproc) {
                                mat4 saved_proj2, saved_view2, saved_model2;
                                memcpy(saved_proj2, matrix_projection, sizeof(mat4));
                                memcpy(saved_view2, matrix_view, sizeof(mat4));
                                memcpy(saved_model2, matrix_model, sizeof(mat4));
                                matrix_ortho(matrix_projection, 0.0, settings.window_width, 0.0, settings.window_height, -1.0, 1.0);
                                matrix_identity(matrix_view);
                                matrix_identity(matrix_model);
                                matrix_upload_p();
                                matrix_upload();

                                glDisable(GL_DEPTH_TEST);
                                glDepthMask(GL_FALSE);
                                /* The world is rendered into postproc.texture with alpha=0 in
                                   many places (geometry colours use a=0). If GL_BLEND is still
                                   enabled from earlier rendering, the composite quad would blend
                                   using that 0 alpha and write NOTHING to the screen — the frame
                                   stays on whatever was last presented (the serverlist). That is
                                   the "frozen serverlist with shaders on" bug. The shader now
                                   also forces alpha=1, but disabling blend here makes it robust
                                   regardless of the sampled colour's alpha. */
                                GLboolean blend_was_on = glIsEnabled(GL_BLEND);
                                glDisable(GL_BLEND);
                                glDisable(GL_SCISSOR_TEST);
                                glDisable(GL_CULL_FACE);
                                glViewport(0, 0, settings.window_width, settings.window_height);
                                glActiveTexture(GL_TEXTURE0);

                                /* === Volumetric light (god-rays) pass ===
                                   Faithful port of Luanti's pipeline: sample the scene color
                                   + depth, run a 30-tap radial blur toward the screen-space sun
                                   position, modulate by Preetham scattering, and write the
                                   additive result into postproc.vol_tex. The postproc blit below
                                   then reads vol_tex instead of the raw scene texture. */
                                postproc.vol_applied = 0;
                                if(settings.volumetric_light && settings.volumetric_light_strength > 0.0F
                                   && !network_map_transfer
                                   && postproc.vol_fbo && postproc.vol_tex
                                   && postproc.fbo && postproc.depth_tex) {

                                        /* Compute sun position in clip space, normalized to length 1.
                                           Mirrors Luanti's GameGlobalShaderUniformSetter::onSetUniforms:
                                           sun_position = normalize(Projection * View * (cameraPos + 10000 * sunDir)) */
                                        mat4 vol_mv, vol_mvp;
                                        glmc_mat4_mul(saved_view2, saved_model2, vol_mv);
                                        glmc_mat4_mul(saved_proj2, vol_mv, vol_mvp);
                                        vec4 vol_sun_world = {
                                                camera_x + 10000.0F * sun_dir[0],
                                                camera_y + 10000.0F * sun_dir[1],
                                                camera_z + 10000.0F * sun_dir[2],
                                                1.0F
                                        };
                                        vec4 vol_sun_clip;
                                        glmc_mat4_mulv(vol_mvp, vol_sun_world, vol_sun_clip);
                                        float vol_sun_len = sqrtf(vol_sun_clip[0] * vol_sun_clip[0]
                                                                        + vol_sun_clip[1] * vol_sun_clip[1]
                                                                        + vol_sun_clip[2] * vol_sun_clip[2]);
                                        if(vol_sun_len > 0.0001F) {
                                                vol_sun_clip[0] /= vol_sun_len;
                                                vol_sun_clip[1] /= vol_sun_len;
                                                vol_sun_clip[2] /= vol_sun_len;
                                        }

                                        /* Also compute a point at the camera (origin in sun-relative
                                           space) to derive the screen-space sun DIRECTION for mode 2. */
                                        vec4 vol_cam_world = { camera_x, camera_y, camera_z, 1.0F };
                                        vec4 vol_cam_clip;
                                        glmc_mat4_mulv(vol_mvp, vol_cam_world, vol_cam_clip);
                                        if(vol_cam_clip[3] != 0.0F) {
                                                vol_cam_clip[0] /= vol_cam_clip[3];
                                                vol_cam_clip[1] /= vol_cam_clip[3];
                                        }
                                        /* Screen-space direction from camera to sun (UV space) */
                                        float vol_sun_dir_screen[2] = { 0.0F, 0.0F };
                                        if(vol_sun_clip[3] != 0.0F) {
                                                float sun_ndc_x = vol_sun_clip[0] / vol_sun_clip[3];
                                                float sun_ndc_y = vol_sun_clip[1] / vol_sun_clip[3];
                                                float cam_ndc_x = vol_cam_clip[0];
                                                float cam_ndc_y = vol_cam_clip[1];
                                                float dx = sun_ndc_x - cam_ndc_x;
                                                float dy = sun_ndc_y - cam_ndc_y;
                                                float dlen = sqrtf(dx * dx + dy * dy);
                                                if(dlen > 0.001F) {
                                                        vol_sun_dir_screen[0] = dx / dlen * 0.5F;
                                                        vol_sun_dir_screen[1] = dy / dlen * 0.5F;
                                                }
                                        }

                                        /* Sun brightness: clamp(107.143 * sunDir.Y, 0, 1) — same as Luanti. */
                                        float vol_sun_brightness = 107.143F * sun_dir[1];
                                        if(vol_sun_brightness < 0.0F) vol_sun_brightness = 0.0F;
                                        if(vol_sun_brightness > 1.0F) vol_sun_brightness = 1.0F;

                                        /* Warm daylight tint for the ray color. */
                                        float vol_day_light[3] = { 1.0F, 0.95F, 0.8F };

                                        /* Render into vol_fbo (color = vol_tex). */
                                        glBindFramebuffer(GL_FRAMEBUFFER, postproc.vol_fbo);

                                        /* Bind color (TEXTURE0) + depth (TEXTURE1). */
                                        glActiveTexture(GL_TEXTURE1);
                                        glBindTexture(GL_TEXTURE_2D, postproc.depth_tex);
                                        glActiveTexture(GL_TEXTURE0);
                                        glBindTexture(GL_TEXTURE_2D, postproc.texture);

                                        /* Use the vol2 shader (the only volumetric light method). */
                                        unsigned int active_shader = postproc.vol2_shader;

                                        if(active_shader) {
                                                glUseProgram(active_shader);
                                                glUniform3f(postproc.uni_vol2_sun_dir, vol_sun_clip[0], vol_sun_clip[1], vol_sun_clip[2]);
                                                glUniform1f(postproc.uni_vol2_sun_brightness, vol_sun_brightness);
                                                glUniform1f(postproc.uni_vol2_strength, settings.volumetric_light_strength);
                                                glUniform1f(postproc.uni_vol2_brightness, settings.volumetric_light_brightness);
                                                glUniform1f(postproc.uni_vol2_range, settings.volumetric_light_range);
                                                glUniform3f(postproc.uni_vol2_daylight, vol_day_light[0], vol_day_light[1], vol_day_light[2]);

#if defined(OPENGL_ES)
                                                if(gles_version >= 2) {
                                                        glx_draw_screen_quad();
                                                } else {
#else
                                                glBegin(GL_QUADS);
                                                glTexCoord2f(0.0F, 0.0F); glVertex2f(0.0F, 0.0F);
                                                glTexCoord2f(1.0F, 0.0F); glVertex2f((float)settings.window_width, 0.0F);
                                                glTexCoord2f(1.0F, 1.0F); glVertex2f((float)settings.window_width, (float)settings.window_height);
                                                glTexCoord2f(0.0F, 1.0F); glVertex2f(0.0F, (float)settings.window_height);
                                                glEnd();
#endif
#if defined(OPENGL_ES)
                                                }
#endif

                                                glUseProgram(0);
                                        }

                                        /* Unbind both texture units. */
                                        glActiveTexture(GL_TEXTURE1);
                                        glBindTexture(GL_TEXTURE_2D, 0);
                                        glActiveTexture(GL_TEXTURE0);
                                        glBindTexture(GL_TEXTURE_2D, 0);

                                        postproc.vol_applied = 1;
                                }

                                /* === Lens flare pass === */
                                if(settings.lens_flare
                                   && postproc.flare_shader && postproc.vol_fbo && postproc.vol_tex
                                   && postproc.fbo && postproc.depth_tex
                                   && !network_map_transfer) {

                                        mat4 fl_mv, fl_mvp;
                                        glmc_mat4_mul(saved_view2, saved_model2, fl_mv);
                                        glmc_mat4_mul(saved_proj2, fl_mv, fl_mvp);
                                        vec4 fl_sun_world = {
                                                camera_x + 10000.0F * sun_dir[0],
                                                camera_y + 10000.0F * sun_dir[1],
                                                camera_z + 10000.0F * sun_dir[2],
                                                1.0F
                                        };
                                        vec4 fl_sun_clip;
                                        glmc_mat4_mulv(fl_mvp, fl_sun_world, fl_sun_clip);
                                        float fl_sun_len = sqrtf(fl_sun_clip[0] * fl_sun_clip[0]
                                                                       + fl_sun_clip[1] * fl_sun_clip[1]
                                                                       + fl_sun_clip[2] * fl_sun_clip[2]);
                                        if(fl_sun_len > 0.0001F) {
                                                fl_sun_clip[0] /= fl_sun_len;
                                                fl_sun_clip[1] /= fl_sun_len;
                                                fl_sun_clip[2] /= fl_sun_len;
                                        }
                                        float fl_sun_brightness = 107.143F * sun_dir[1];
                                        if(fl_sun_brightness < 0.0F) fl_sun_brightness = 0.0F;
                                        if(fl_sun_brightness > 1.0F) fl_sun_brightness = 1.0F;

                                        glBindFramebuffer(GL_FRAMEBUFFER, postproc.vol_fbo);
                                        glActiveTexture(GL_TEXTURE1);
                                        glBindTexture(GL_TEXTURE_2D, postproc.depth_tex);
                                        glActiveTexture(GL_TEXTURE0);
                                        glBindTexture(GL_TEXTURE_2D, postproc.texture);

                                        glUseProgram(postproc.flare_shader);
                                        glUniform3f(postproc.uni_flare_sun_pos, fl_sun_clip[0], fl_sun_clip[1], fl_sun_clip[2]);
                                        glUniform1f(postproc.uni_flare_sun_brightness, fl_sun_brightness);
                                        glUniform1f(postproc.uni_flare_strength, 1.0F);

                                        if(postproc.vol_applied) {
                                                glUniform1f(postproc.uni_flare_include_scene, 0.0F);
                                                glEnable(GL_BLEND);
                                                glBlendFunc(GL_ONE, GL_ONE);
                                        } else {
                                                glUniform1f(postproc.uni_flare_include_scene, 1.0F);
                                        }

#if defined(OPENGL_ES)
                                        if(gles_version >= 2) {
                                                glx_draw_screen_quad();
                                        } else {
#else
                                        glBegin(GL_QUADS);
                                        glTexCoord2f(0.0F, 0.0F); glVertex2f(0.0F, 0.0F);
                                        glTexCoord2f(1.0F, 0.0F); glVertex2f((float)settings.window_width, 0.0F);
                                        glTexCoord2f(1.0F, 1.0F); glVertex2f((float)settings.window_width, (float)settings.window_height);
                                        glTexCoord2f(0.0F, 1.0F); glVertex2f(0.0F, (float)settings.window_height);
                                        glEnd();
#endif
#if defined(OPENGL_ES)
                                        }
#endif

                                        if(postproc.vol_applied) glDisable(GL_BLEND);
                                        glUseProgram(0);
                                        glActiveTexture(GL_TEXTURE1);
                                        glBindTexture(GL_TEXTURE_2D, 0);
                                        glActiveTexture(GL_TEXTURE0);
                                        glBindTexture(GL_TEXTURE_2D, 0);
                                        postproc.vol_applied = 1;
                                }

                                if(postproc.fbo) {
                                        /* Off-screen-FBO path (desktop/non-iOS): bind the screen and
                                           sample the world texture we rendered into. */
                                        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)live_screen_fbo);
                                        glBindTexture(GL_TEXTURE_2D, postproc.vol_applied ? postproc.vol_tex : postproc.texture);
                                } else if(postproc.texture) {
                                        /* Copy mode (iOS): the world was just rendered straight to the
                                           screen. Copy it into postproc.texture (reads the currently
                                           bound framebuffer — no FBO id needed, no glReadBuffer which
                                           isn't in GLES2), then the shader quad below redraws it graded
                                           over the same screen. If the quad pass fails for any reason,
                                           the un-graded world is already visible — no more freeze. */
                                        glBindTexture(GL_TEXTURE_2D, postproc.texture);
                                        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, settings.window_width, settings.window_height);
                                }

                                if(postproc.shader) {
                                        // derive final shader factors on CPU once per frame
                                        float e = 1.0F + settings.exposure / 100.0F;
                                        float ct = 1.0F + settings.contrast / 100.0F;
                                        glUseProgram(postproc.shader);
                                        glUniform1f(postproc.uni_pp_sat, 1.0F + settings.saturation / 100.0F); // pp_sat
                                        glUniform1f(postproc.uni_pp_scale, e * ct);                            // pp_scale
                                        glUniform1f(postproc.uni_pp_bias, 0.5F * (1.0F - ct));                  // pp_bias
                                        glUniform1f(postproc.uni_pp_vig, (settings.vignette / 100.0F) * 4.0F); // pp_vig
                                        glUniform1f(postproc.uni_pp_ca_strength,
                                                    settings.chromatic_aberration ? settings.chromatic_aberration_strength * 0.01F : 0.0F);
                                        glUniform1f(postproc.uni_pp_filmic, settings.filmic_tonemapping ? 1.0F : 0.0F);

#if defined(OPENGL_ES)
                                        if(gles_version >= 2) {
                                                glx_draw_screen_quad();
                                        } else {
#else
                                        glBegin(GL_QUADS);
                                        glTexCoord2f(0.0F, 0.0F); glVertex2f(0.0F, 0.0F);
                                        glTexCoord2f(1.0F, 0.0F); glVertex2f((float)settings.window_width, 0.0F);
                                        glTexCoord2f(1.0F, 1.0F); glVertex2f((float)settings.window_width, (float)settings.window_height);
                                        glTexCoord2f(0.0F, 1.0F); glVertex2f(0.0F, (float)settings.window_height);
                                        glEnd();
#endif
#if defined(OPENGL_ES)
                                        }
#endif

                                        glUseProgram(0);
                                }

                                glBindTexture(GL_TEXTURE_2D, 0);
                                glDepthMask(GL_TRUE);
                                glClear(GL_DEPTH_BUFFER_BIT);
                                glEnable(GL_DEPTH_TEST);
                                if(blend_was_on)
                                        glEnable(GL_BLEND);

                                memcpy(matrix_projection, saved_proj2, sizeof(mat4));
                                memcpy(matrix_view, saved_view2, sizeof(mat4));
                                memcpy(matrix_model, saved_model2, sizeof(mat4));
                                matrix_upload_p();
                                matrix_upload();
                        }
                }
                if(needs_postproc && network_map_transfer && postproc.fbo) {
                        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)live_screen_fbo);
                }
        }

	if(hud_active->render_3D)
		/* (interpolated render positions were already swapped in before the
		   local player block; see above) */
		hud_active->render_3D();
	player_render_interpolate_end();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_MULTISAMPLE);
        matrix_identity(matrix_projection);
        matrix_ortho(matrix_projection, 0.0F, settings.window_width, 0.0F, settings.window_height, -1.0F, 1.0F);
        matrix_identity(matrix_view);
        matrix_identity(matrix_model);
        matrix_upload();
        matrix_upload_p();
        float scalex = (settings.window_width / 800.0F);
        float scalef = (settings.window_height / 600.0F);

        if(hud_active->render_2D) {
                mu_Context* ctx = hud_active->ctx;

                if(ctx) {
                        /* hud_ui_scale() enlarges touch targets so buttons and list rows
                           aren't a few mm tall on phones. It returns 1.0 on non-Android
                           builds, so desktop layout is unchanged. */
                        float us = hud_ui_scale();
                        /* Row HEIGHT for height==0 layout rows is style->size.y + padding*2,
                           so scaling size.y AND padding is what enlarges nav/menu buttons and
                           list rows. Base values match microui's default_style. */
                        hud_active->ctx->style->size.x = (int)(68 * us);
                        hud_active->ctx->style->size.y = (int)(10 * us);
                        hud_active->ctx->style->padding = (int)(5 * us);
                        hud_active->ctx->style->spacing = (int)(4 * us);
                        hud_active->ctx->style->title_height = (int)(24 * us);
                        hud_active->ctx->style->scrollbar_size = (int)(12 * us);
                        hud_active->ctx->style->thumb_size = (int)(8 * us);

                        mu_begin(ctx);
                }

                hud_active->render_2D(ctx, scalex, scalef);

                if(ctx) {
                        mu_end(ctx);
                        hud_ime_update();

                        glEnable(GL_BLEND);
                        glEnable(GL_SCISSOR_TEST);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        mu_Command* cmd = NULL;
                        while(mu_next_command(ctx, &cmd)) {
                                switch(cmd->type) {
                                        case MU_COMMAND_TEXT:
                                                glColor4ub(cmd->text.color.r, cmd->text.color.g, cmd->text.color.b, cmd->text.color.a);
                                                font_render(cmd->text.pos.x, settings.window_height - cmd->text.pos.y,
                                                                        ctx->text_height(cmd->text.font), cmd->text.str);
                                                glEnable(GL_BLEND);
                                                break;
                                        case MU_COMMAND_RECT:
                                                glColor4ub(cmd->rect.color.r, cmd->rect.color.g, cmd->rect.color.b, cmd->rect.color.a);
                                                texture_draw_empty(cmd->rect.rect.x, settings.window_height - cmd->rect.rect.y,
                                                                                   cmd->rect.rect.w, cmd->rect.rect.h);
                                                break;
                                        case MU_COMMAND_ICON:
                                                glColor4ub(cmd->icon.color.r, cmd->icon.color.g, cmd->icon.color.b, cmd->icon.color.a);
                                                int size = min(cmd->icon.rect.w, cmd->icon.rect.h);

                                                if(cmd->icon.id >= HUD_FLAG_INDEX_START - 1) {
                                                        float u, v;
                                                        texture_flag_offset(cmd->icon.id - HUD_FLAG_INDEX_START, &u, &v);

                                                        texture_draw_sector(&texture_ui_flags, cmd->icon.rect.x,
                                                                                                settings.window_height - cmd->icon.rect.y - size * 0.167F, size,
                                                                                                size * 0.667F, u, v, 18.0F / 256.0F, 12.0F / 256.0F);
                                                        glEnable(GL_BLEND);
                                                } else if(hud_active->ui_images) {
                                                        bool resize = false;
                                                        struct texture* img = hud_active->ui_images(cmd->icon.id, &resize);

                                                        if(img) {
                                                                texture_draw(img, cmd->icon.rect.x, settings.window_height - cmd->icon.rect.y,
                                                                                         resize ? size : cmd->icon.rect.w, resize ? size : cmd->icon.rect.h);
                                                                glEnable(GL_BLEND);
                                                        }
                                                }

                                                break;
                                        case MU_COMMAND_CLIP:
                                                glScissor(cmd->clip.rect.x, settings.window_height - (cmd->clip.rect.y + cmd->clip.rect.h),
                                                                  cmd->clip.rect.w, cmd->clip.rect.h);
                                                break;
                                }
                        }
                        glDisable(GL_BLEND);
                        glDisable(GL_SCISSOR_TEST);
                }
        }

        if(screenshot_anim.active && screenshot_anim.texture) {
                double elapsed = window_time() - screenshot_anim.start_time;
                float t = (float)(elapsed / 1.5);
                if(t >= 1.0F) {
                        screenshot_anim.active = false;
                        glDeleteTextures(1, (GLuint*)&screenshot_anim.texture);
                        screenshot_anim.texture = 0;
                } else {
                        float target_scale = 1.0F / 6.0F;
                        float margin = 10.0F;
                        float shrink_end = 0.2F;
                        float fade_start = 0.63F;
                        float ease = min(t / shrink_end, 1.0F);
                        float w = (float)settings.window_width * (1.0F + (target_scale - 1.0F) * ease);
                        float h = (float)settings.window_height * (1.0F + (target_scale - 1.0F) * ease);
                        float x = margin * ease;
                        float y = (float)settings.window_height - margin * ease;
                        float alpha = t <= fade_start ? 1.0F : 1.0F - (t - fade_start) / (1.0F - fade_start);

                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#if !defined(OPENGL_ES)
                glEnable(GL_TEXTURE_2D);
#endif
                glBindTexture(GL_TEXTURE_2D, screenshot_anim.texture);
                glColor4f(1.0F, 1.0F, 1.0F, alpha);
                texture_draw_empty(x, y, w, h);
#if !defined(OPENGL_ES)
                glDisable(GL_TEXTURE_2D);
#endif
                        glLineWidth(2.0F);
                        glColor4f(1.0F, 1.0F, 1.0F, alpha);
                        glx_draw_line_2d(x, y, x + w, y);
                        glx_draw_line_2d(x + w, y, x + w, y - h);
                        glx_draw_line_2d(x + w, y - h, x, y - h);
                        glx_draw_line_2d(x, y - h, x, y);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        glDisable(GL_BLEND);
                }
        }

        if(settings.multisamples > 0)
                glEnable(GL_MULTISAMPLE);
}

void init() {
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
#ifdef OPENGL_ES
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);
#else
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
#endif
        glClearDepth(1.0F);
        glDepthFunc(GL_LEQUAL);
        glShadeModel(GL_SMOOTH);
        glDisable(GL_FOG);

        map_init();

        glx_init();

        font_init();
        player_init();
        particle_init();
        network_init();
        ping_init();
        kv6_init();
        texture_init();
        sound_init();
        skins_init();
        tracer_init();
        hud_init();
        chunk_init();
        grenade_init();
        bloodmarks_init();
        damagenumbers_init();

        weapon_set(false);

        rpc_init();
}

void reshape(struct window_instance* window, int width, int height) {
        font_reset();
        glViewport(0, 0, width, height);
        settings.window_width = width;
        settings.window_height = height;
        /* Keep the settings-menu working copy aligned, otherwise an OS resize
           looks like a pending edit there and gets reverted on commit. */
        settings_tmp.window_width = width;
        settings_tmp.window_height = height;
        if(settings.vsync < 2)
                window_swapping(settings.vsync);
        if(settings.vsync > 1)
                window_swapping(0);
}

static int mu_button_translate(int button) {
        switch(button) {
                case WINDOW_MOUSE_LMB: return MU_MOUSE_LEFT;
                case WINDOW_MOUSE_MMB: return MU_MOUSE_MIDDLE;
                case WINDOW_MOUSE_RMB: return MU_MOUSE_RIGHT;
                default: return 0;
        }
}

static int mu_key_translate(int key) {
        switch(key) {
                case WINDOW_KEY_BACKSPACE: return MU_KEY_BACKSPACE;
                case WINDOW_KEY_ENTER: return MU_KEY_RETURN;
                case WINDOW_KEY_SHIFT: return MU_KEY_SHIFT;
                case WINDOW_KEY_CURSOR_LEFT: return MU_KEY_LEFT;
                case WINDOW_KEY_CURSOR_RIGHT: return MU_KEY_RIGHT;
                case WINDOW_KEY_HOME: return MU_KEY_HOME;
                case WINDOW_KEY_END: return MU_KEY_END;
                case WINDOW_KEY_DELETE: return MU_KEY_DELETE;
                default: return 0;
        }
}

void text_input(struct window_instance* window, const char* utf8) {
        if(!utf8 || !utf8[0]) return;

        if(hud_active->ctx)
                mu_input_text(hud_active->ctx, utf8);

        /* Chatlog search has its own per-frame text buffer; route to it
           when the user is on the chatlog HUD with the search bar open.
           The HUD doesn't use a microui textbox (selection, context menu,
           and link modal are all hand-rolled there), so without this hook
           typed characters would feed mu_input_text above and then drop
           into the chat input branch below - which short-circuits anyway
           because chat_input_mode is always CHAT_NO_INPUT while the
           chatlog HUD is active. Net result: the search bar would render
           but never see a single character. */
        if(hud_active == &hud_chatlog && chatlog_search_active()) {
                chatlog_search_text_input(utf8);
                return;
        }

        if(chat_input_mode == CHAT_NO_INPUT)
                return;

        extern int chat_cursor;
        extern int chat_sel_anchor;

        /* Reject control bytes (0x01..0x07 are inline color codes, 0x08..0x1F
           non-printable). \n is allowed for multi-line input. */
        int add = (int)strlen(utf8);
        for(int i = 0; i < add; i++) {
                unsigned char c = (unsigned char)utf8[i];
                if(c < 0x20 && c != '\n') return;
        }

        /* Replace selection if any. */
        if(chat_sel_anchor >= 0 && chat_sel_anchor != chat_cursor) {
                int lo = chat_sel_anchor < chat_cursor ? chat_sel_anchor : chat_cursor;
                int hi = chat_sel_anchor < chat_cursor ? chat_cursor : chat_sel_anchor;
                int len0 = (int)strlen(chat[0][0]);
                memmove(chat[0][0] + lo, chat[0][0] + hi, len0 - hi + 1);
                chat_cursor = lo;
                chat_sel_anchor = -1;
        }

        int len = (int)strlen(chat[0][0]);
        int cap = (int)sizeof(chat[0][0]);

        if(len + add >= cap - 1) return;
        if(chat_cursor < 0) chat_cursor = 0;
        if(chat_cursor > len) chat_cursor = len;
        memmove(chat[0][0] + chat_cursor + add,
                        chat[0][0] + chat_cursor,
                        len - chat_cursor + 1);
        memcpy(chat[0][0] + chat_cursor, utf8, add);
        chat_cursor += add;
}

void keys(struct window_instance* window, int key, int scancode, int action, int mods) {
        if(hud_active->ctx) {
                if(mu_key_translate(key)) {
                        switch(action) {
                                case WINDOW_RELEASE: mu_input_keyup(hud_active->ctx, mu_key_translate(key)); break;
                                case WINDOW_REPEAT:
                                case WINDOW_PRESS: mu_input_keydown(hud_active->ctx, mu_key_translate(key)); break;
                        }
                }

                /* Standard textbox shortcuts. Physical A/X have dedicated
                   internal aliases because those keys are also bound to Move
                   Left and Map Zoom respectively. Ctrl and Command both arrive
                   through `mods`, so this works on Windows/Linux/macOS. */
                if(action == WINDOW_PRESS && mods) {
                        if(key == WINDOW_KEY_A)
                                mu_input_keydown(hud_active->ctx, MU_KEY_SELECT_ALL);
                        if(key == WINDOW_KEY_C)
                                mu_input_keydown(hud_active->ctx, MU_KEY_COPY);
                        if(key == WINDOW_KEY_X)
                                mu_input_keydown(hud_active->ctx, MU_KEY_CUT);
                        if(key == WINDOW_KEY_V) {
                                const char* clipboard = window_clipboard();
                                if(clipboard)
                                        mu_input_text(hud_active->ctx, clipboard);
                        }
                }
        }

        if(action == WINDOW_PRESS) {
                if(show_exit && key != WINDOW_KEY_NO && key != WINDOW_KEY_YES && key != WINDOW_KEY_ESCAPE) {
                        return;
                }

                if(config_key(key)->toggle) {
                        if(chat_input_mode == CHAT_NO_INPUT) {
                                window_pressed_keys[key] = !window_pressed_keys[key];
                        }
                } else {
                        window_pressed_keys[key] = 1;
                }
        }
        if(action == WINDOW_RELEASE && !config_key(key)->toggle)
                window_pressed_keys[key] = 0;

#ifdef USE_GLFW
        if(key == WINDOW_KEY_FULLSCREEN && action == WINDOW_PRESS) { // switch between fullscreen
                const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
                if(!settings.fullscreen) {
                        glfwSetWindowMonitor(window->impl, glfwGetPrimaryMonitor(), 0, 0, mode->width, mode->height,
                                                                 mode->refreshRate);
                        settings.fullscreen = 1;
                } else {
                        glfwSetWindowMonitor(window->impl, NULL, (mode->width - 800) / 2, (mode->height - 600) / 2, 800, 600, 0);
                        settings.fullscreen = 0;
                }
        }
#else
        if(key == WINDOW_KEY_FULLSCREEN && action == WINDOW_PRESS) {
                settings.fullscreen = !settings.fullscreen;
                window_fromsettings();
        }
#endif

        if(key == WINDOW_KEY_SCREENSHOT && action == WINDOW_PRESS) { // take screenshot
                time_t pic_time;
                time(&pic_time);
                char pic_name[128];
                sprintf(pic_name, "screenshots/%ld.png", (long)pic_time);

                unsigned char* pic_data = malloc(settings.window_width * settings.window_height * 4 * 2);
                CHECK_ALLOCATION_ERROR(pic_data)
                glReadBuffer(GL_FRONT);
                glReadPixels(0, 0, settings.window_width, settings.window_height, GL_RGBA, GL_UNSIGNED_BYTE, pic_data);
                glReadBuffer(GL_BACK);

                for(int y = 0; y < settings.window_height; y++) { // mirror image (top-bottom)
                        for(int x = 0; x < settings.window_width; x++)
                                pic_data[(x + (settings.window_height - y - 1) * settings.window_width) * 4 + 3] = 255;
                        memcpy(pic_data + settings.window_width * 4 * (y + settings.window_height),
                                   pic_data + settings.window_width * 4 * (settings.window_height - y - 1), settings.window_width * 4);
                }

                lodepng_encode32_file(pic_name, pic_data + settings.window_width * settings.window_height * 4,
                                                          settings.window_width, settings.window_height);

                if(screenshot_anim.texture)
                        glDeleteTextures(1, (GLuint*)&screenshot_anim.texture);
                glGenTextures(1, (GLuint*)&screenshot_anim.texture);
                glBindTexture(GL_TEXTURE_2D, screenshot_anim.texture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, settings.window_width, settings.window_height, 0, GL_RGBA,
                                         GL_UNSIGNED_BYTE, pic_data + settings.window_width * settings.window_height * 4);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glBindTexture(GL_TEXTURE_2D, 0);
                screenshot_anim.active = true;
                screenshot_anim.start_time = window_time();

                free(pic_data);

                sprintf(pic_name, "Saved screenshot as screenshots/%ld.png", (long)pic_time);
                chat_add(0, 0x00FFFF, pic_name);

                sound_create(SOUND_LOCAL, &sound_screenshot, 0.0F, 0.0F, 0.0F);
        }

        if(key == WINDOW_KEY_SAVE_MAP && action == WINDOW_PRESS) {
                time_t save_time;
                time(&save_time);
                char save_name[128];
                sprintf(save_name, "vxl/%ld.vxl", (long)save_time);

                map_save_file(save_name);

                sprintf(save_name, "Saved map as vxl/%ld.vxl", (long)save_time);
                chat_add(0, 0x00FFFF, save_name);
        } else if(key == WINDOW_KEY_RECORDING && action == WINDOW_PRESS) {
                recorder_toggle_recording();
        }
}

void mouse_click(struct window_instance* window, int button, int action, int mods) {
        if(hud_active->input_mouseclick) {
                double x, y;
                window_mouseloc(&x, &y);
                hud_active->input_mouseclick(x, y, button, action, mods);
        }

        if(hud_active->ctx) {
                double x, y;
                window_mouseloc(&x, &y);
                switch(action) {
                        case WINDOW_PRESS: mu_input_mousedown(hud_active->ctx, x, y, mu_button_translate(button)); break;
                        case WINDOW_RELEASE: mu_input_mouseup(hud_active->ctx, x, y, mu_button_translate(button)); break;
                }
        }
}

void mouse(struct window_instance* window, double x, double y) {
        if(hud_active->input_mouselocation)
                hud_active->input_mouselocation(x, y);
        if(hud_active->ctx)
                mu_input_mousemove(hud_active->ctx, x, y);
}

void mouse_scroll(struct window_instance* window, double xoffset, double yoffset) {
        if(hud_active->input_mousescroll)
                hud_active->input_mousescroll(yoffset);
        if(hud_active->ctx)
                mu_input_scroll(hud_active->ctx, -xoffset * 50, -yoffset * 50);
}

void deinit() {
        recorder_shutdown();
        rpc_deinit();
        ping_deinit();
        if(network_connected)
                network_disconnect();
        window_deinit();
}

void on_error(int i, const char* s) {
        log_fatal("Major error occured: [%i] %s", i, s);
        getchar();
}

#if defined(OS_APPLE)
/* ── iOS resource staging ─────────────────────────────────────────────────────
   Copies the read-only bundle assets into the writable prefs dir on first run
   (or after a version bump) so the rest of the engine can keep using relative
   paths verbatim. Pure POSIX + SDL filesystem; no Objective-C needed. */
#include <dirent.h>

static void ios_copy_file(const char* src, const char* dst) {
        struct stat ss, ds;
        if(stat(src, &ss) != 0) return;
        if(stat(dst, &ds) == 0 && ds.st_mtime >= ss.st_mtime) return;
        FILE* in = fopen(src, "rb");
        if(!in) return;
        FILE* out = fopen(dst, "wb");
        if(!out) { fclose(in); return; }
        char buf[1 << 16];
        size_t n;
        while((n = fread(buf, 1, sizeof(buf), in)) > 0)
                fwrite(buf, 1, n, out);
        fclose(out);
        fclose(in);
}

static void ios_copy_tree(const char* src, const char* dst) {
        ks_mkdir(dst, 0755);
        DIR* d = opendir(src);
        if(!d) return;
        struct dirent* e;
        while((e = readdir(d))) {
                const char* nm = e->d_name;
                if(!strcmp(nm, ".") || !strcmp(nm, ".."))
                        continue;
                /* Never copy bundle metadata or the executable itself. */
                if(!strcmp(nm, "client") || !strcmp(nm, "Info.plist")
                   || !strcmp(nm, "PkgInfo") || !strcmp(nm, "_CodeSignature")
                   || !strcmp(nm, "embedded.mobileprovision"))
                        continue;
                char s[2048], t[2048];
                snprintf(s, sizeof(s), "%s/%s", src, nm);
                snprintf(t, sizeof(t), "%s/%s", dst, nm);
                struct stat st;
                if(stat(s, &st) != 0)
                        continue;
                if(S_ISDIR(st.st_mode))
                        ios_copy_tree(s, t);
                else
                        ios_copy_file(s, t);
        }
        closedir(d);
}

static void ios_stage_resources_and_chdir(void) {
        char* base = SDL_GetBasePath();                       /* read-only bundle */
        char* pref = SDL_GetPrefPath("KyroSpades", "client"); /* writable sandbox */
        if(!pref) {
                if(base) SDL_free(base);
                return;
        }

        char stamp[2048];
        snprintf(stamp, sizeof(stamp), "%s.staged_version", pref);

        int need_stage = 1;
        FILE* sf = fopen(stamp, "r");
        if(sf) {
                char have[64] = {0};
                if(fgets(have, sizeof(have), sf))
                        need_stage = strcmp(have, KYROSPADES_VERSION) != 0;
                fclose(sf);
        }

        if(need_stage && base) {
                log_info("iOS: staging bundle resources into %s", pref);
                ios_copy_tree(base, pref);
                sf = fopen(stamp, "w");
                if(sf) { fputs(KYROSPADES_VERSION, sf); fclose(sf); }
        }

        if(chdir(pref) != 0)
                log_error("iOS: chdir to prefs dir failed");

        if(base) SDL_free(base);
        SDL_free(pref);
}
#endif /* OS_APPLE */

int main(int argc, char** argv) {
        settings.opengl14 = 1;
        settings.color_correction = 0;
        settings.multisamples = 0;
        settings.shadow_entities = 0;
        settings.ambient_occlusion = 1;
        settings.ao_multiplier = 1.0F;
        settings.shadow_quality = 0;
        settings.shadow_intensity = 0.40F;
        settings.sky_gradient = 0;
        settings.sky_gradient_intensity = 0.5F;
        settings.water_waves = 0;
        settings.water_wave_intensity = 2.0F;
        settings.water_wave_speed = 1.0F;
        settings.water_wave_mode = 0;
        settings.water_wave_tile_size = 2;
        settings.render_distance = 128.0F;
        settings.spectator_fog_distance = 128.0F;
        settings.spectator_acceleration = 90.0F;
        settings.window_width = 800;
        settings.window_height = 600;
        settings.player_arms = 1;
        settings.camera_movement = 0;
        settings.fullscreen = 0;
        settings.ui_accent_r = 200;
        settings.ui_accent_g = 200;
        settings.ui_accent_b = 200;
        settings.ui_rgb = 0;
        settings.ui_rgb_speed = 1.0F;
        settings.hud_shadows = 0;
        settings.healthbar = 0;
        settings.ammo_crosshair = 0;
        settings.free_aim = 0;
        settings.free_aim_deadzone_h = 6.0F;
        settings.free_aim_deadzone_v = 4.0F;
        settings.greedy_meshing = 0;
        /* The look formula is `setting / 5.0F * MOUSE_SENSITIVITY`, so the
           neutral value of this setting is 5 — NOT the raw radians/pixel
           constant MOUSE_SENSITIVITY (0.002), which would render the camera
           effectively frozen. */
        settings.mouse_sensitivity = 5.0F;
        settings.show_news = 1;
        settings.show_fps = 0;
        settings.volume = 10;
        settings.voxlap_models = 0;
        settings.force_displaylist = 0;
        settings.invert_y = 0;
        settings.smooth_fog = 0;
        settings.water_shader = 0;
        settings.camera_fov = CAMERA_DEFAULT_FOV;
        settings.rifle_ads_fov = CAMERA_DEFAULT_FOV;
        settings.shotgun_ads_fov = CAMERA_DEFAULT_FOV;
        settings.smg_ads_fov = CAMERA_DEFAULT_FOV;
        settings.disable_dynamic_fov = 0;
        settings.textured_blocks = 0;
        settings.minimap_zoom = 3;
        settings.skin_spade = 0;
        settings.skin_grenade = 0;
        settings.skin_rifle = 0;
        settings.skin_smg = 0;
        settings.skin_shotgun = 0;
        settings.skin_player = 0;
        settings.skin_intel = 0;
        settings.skin_tent = 0;
        /* BetterSpades' smoothness comes from keeping the default render path
           simple.  Color grading/filmic/vignette are still available in the
           settings menu, but they must be opt-in: any non-zero value forces a
           full-screen FBO + shader pass every frame. */
        settings.exposure = 0.0F;
        settings.contrast = 0.0F;
        settings.vignette = 0.0F;
        settings.volumetric_light = 0;
        settings.volumetric_light_strength = 0.2F;
        settings.volumetric_light_brightness = 0.3F;
        settings.volumetric_light_range = 1.0F;
        /* ── New post-proc shaders ───────────────────────────────────────────
           These are available as opt-in visual effects, but default off so the
           client keeps the original BetterSpades fast render path. */
        settings.chromatic_aberration = 0;
        settings.chromatic_aberration_strength = 1.5F;
        settings.filmic_tonemapping = 1;
        settings.chat_mention_r = 255;
        settings.chat_mention_g = 255;
        /* Movement-feel / network smoothing defaults: all on, each can be
           toggled independently in settings for A/B comparison. */
        settings.net_smooth_corrections = 1;
        settings.net_fast_position = 1;
        settings.net_early_opt = 1;
        settings.net_unsequenced_orient = 1;
        settings.render_interpolation = 1;
        settings.precise_pacing = 1;
        settings.raw_aim = 1;
        settings.crouch_instant = 1;
        settings.view_bob = 1;
        settings.land_dip = 1;
        settings.camera_shake = 1;
        strcpy(settings.name, "DEV_CLIENT");
        strcpy(settings.ui_language, "en_US");

#if defined(__ANDROID__)
        /* The process CWD on Android is "/" (read-only). Switch to the app's
           private external storage dir (writable, needs no permission, removed on
           uninstall) so every relative write path below resolves correctly. */
        {
                const char* base = SDL_AndroidGetExternalStoragePath();
                if(!base || chdir(base) != 0) {
                        const char* internal = SDL_AndroidGetInternalStoragePath();
                        if(internal) chdir(internal);
                }
        }
#elif defined(OS_IOS)
        /* iOS sandboxes every app: the .app bundle is read-only and the process
           CWD is not writable. We copy the read-only assets out of the bundle
           into the writable prefs dir once (re-staged when the version changes),
           then chdir there so all the existing relative paths — both reads and
           writes — resolve exactly like they do on desktop. */
        ios_stage_resources_and_chdir();
#elif defined(OS_APPLE)
        {
                char* base = SDL_GetBasePath();
                char* pref = SDL_GetPrefPath("KyroSpades", "client");
                if(pref) {
                        if(base) {
                                char res[2048];
                                snprintf(res, sizeof(res), "%s../Resources", base);
                                ios_copy_tree(res, pref);
                        }
                        chdir(pref);
                        SDL_free(pref);
                }
                if(base) SDL_free(base);
        }
#endif
        /* Create the writable subdirs on every platform. "demos" is included now —
           it was never created before, which is why the Demos screen failed. */
        if(!file_dir_exists("logs"))
                file_dir_create("logs");
        if(!file_dir_exists("demos"))
                file_dir_create("demos");
        if(!file_dir_exists("cache"))
                file_dir_create("cache");
        if(!file_dir_exists("screenshots"))
                file_dir_create("screenshots");
        if(!file_dir_exists("vxl"))
                file_dir_create("vxl");

        log_set_level(LOG_INFO);

        time_t t = time(NULL);
        char buf[32];
        strftime(buf, 32, "logs/%Y-%m-%d.log", localtime(&t));
        FILE* logfp = fopen(buf, "a");
        if(logfp) {
                /* Match the log content's ISO-8601 date and keep the file
                   line-buffered so messages survive a crash / hard quit. */
                setvbuf(logfp, NULL, _IOLBF, 0);
                log_set_fp(logfp);
        }

        srand(t);

        log_info("Game started!");
        log_info("KyroSpades %s %s", KYROSPADES_VERSION, BS_VER_INFO);

        settings.iron_sight = 1;
        settings.replay_enabled = -1;
        config_reload();

        if(settings.recording_fps == 0) settings.recording_fps = 60;
        if(settings.recording_bitrate_kbps == 0) settings.recording_bitrate_kbps = 2000;
        if(settings.replay_enabled == -1) {
                settings.replay_enabled = 0;
                if(settings.replay_duration == 0) settings.replay_duration = 30;
        }
        settings_tmp.recording_fps = settings.recording_fps;
        settings_tmp.recording_bitrate_kbps = settings.recording_bitrate_kbps;

        if(settings.debug_log) {
                log_set_level(LOG_TRACE);
                log_info("Debug logging enabled (LOG_TRACE)");
        }

        window_init();
        window_fromsettings();

#ifndef OPENGL_ES
        if(glewInit())
                log_error("Could not load extended OpenGL functions!");
        else
                log_debug("GLEW initialized successfully");
#endif

        log_info("Vendor: %s", glGetString(GL_VENDOR));
        log_info("Renderer: %s", glGetString(GL_RENDERER));
        log_info("Version: %s", glGetString(GL_VERSION));
        log_debug("GLSL: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

        if(settings.multisamples > 0) {
                glEnable(GL_MULTISAMPLE);
                log_info("MSAAx%i on", settings.multisamples);
        }

        while(glGetError() != GL_NO_ERROR)
                ;

        init();
        recorder_init();
        atexit(deinit);

        if(settings.replay_enabled)
                recorder_buffer_start();

        if(settings.vsync < 2)
                window_swapping(settings.vsync);
        if(settings.vsync > 1)
                window_swapping(0);

        if(argc > 1) {
                if(!strcmp(argv[1], "--help")) {
                        log_info("Usage: client                     [server browser]");
                        log_info("       client -aos://<ip>:<port>  [custom address]");
                        exit(0);
                }

                if(!network_connect_string(argv[1] + 1)) {
                        log_error("Error: Connection failed (use --help for instructions)");
                        exit(1);
                } else {
                        log_info("Connection to %s successful", argv[1] + 1);
                        hud_change(&hud_ingame);
                }
        }

        double last_frame_start = window_time();
        double physics_time_fixed = 0.0F;
        double physics_time_fast = 0.0F;

        while(!window_closed()) {
                double dt = window_time() - last_frame_start;
                last_frame_start = window_time();
                dt_float = (float)dt;

                /* Process incoming network events as early as possible so
                   this frame's physics, input and render see them (instead
                   of only at end-of-frame). */
                if(settings.net_early_opt)
                        network_service();

                if(hud_active->render_world) {
                        physics_time_fast += dt;
                        physics_time_fixed += dt;

// these run at exactly ~60fps
#define PHYSICS_STEP_TIME (1.0 / 60.0)
                        while(physics_time_fixed >= PHYSICS_STEP_TIME) {
                                physics_time_fixed -= PHYSICS_STEP_TIME;
                                if(!demo_is_frozen()) {
                                        /* snapshot pre-tick state for the
                                           partial-tick render interpolation */
                                        player_snapshot_prev();
                                        player_update(PHYSICS_STEP_TIME, 1);
                                        grenade_update(PHYSICS_STEP_TIME);
                                }
                        }

                // these run at min. ~60fps but as fast as possible
                // cap catch-up iterations to prevent spiral of death:
                // if a frame is slow (high dt), the loop would run many times,
                // each calling expensive camera updates (e.g. bodyview ray-march),
                // making the next frame even slower in a feedback loop.
                double step = fmin(dt, PHYSICS_STEP_TIME);
                int max_catchup = 4;
                int catchup_count = 0;
                while(step > 0 && physics_time_fast >= step && catchup_count < max_catchup) {
                        physics_time_fast -= step;
                        if(!demo_is_frozen())
                                player_update(step, 0);
                        camera_update(step);
                        tracer_update(step);
                        particle_update(step);
                        bloodmarks_update(step);
                        damagenumbers_update(step);
                        if(settings.rain) {
                                particle_create_rain();
                        }
                        if(settings.snow) {
                                particle_create_snow();
                        }

                        static int rain_sound_active = 0;
                        if(settings.rain && !rain_sound_active) {
                                sound_rain_start();
                                rain_sound_active = 1;
                        } else if(!settings.rain && rain_sound_active) {
                                sound_rain_stop();
                                rain_sound_active = 0;
                        }
                        // Advance rain sound fade (Mineclonia-style -0.5/s fade-out when stopping).
                        sound_rain_update(step);
                        map_collapsing_update(step);
                        catchup_count++;
                }
                // discard excess accumulated time to prevent buildup.
                // Apply the same clamp to BOTH accumulators so the fixed-step
                // physics loop and the fast loop stay in sync; otherwise a
                // stutter causes fixed-step player_update to keep catching up
                // in slow-mo while particles/tracers jump ahead.
                if(physics_time_fast > PHYSICS_STEP_TIME * 2)
                        physics_time_fast = PHYSICS_STEP_TIME;
                if(physics_time_fixed > PHYSICS_STEP_TIME * 2)
                        physics_time_fixed = PHYSICS_STEP_TIME;

                /* partial-tick fraction for render interpolation: how far we
                   are into the current, still-unfinished physics tick */
                physics_tick_alpha = settings.render_interpolation
                        ? (float)fmin(fmax(physics_time_fixed / PHYSICS_STEP_TIME, 0.0), 1.0)
                        : 1.0F;
                } else {
                        physics_tick_alpha = 1.0F;
                }

                 display();
 
                  static bool replay_save_key_pressed = false;
                    if (window_key_down(WINDOW_KEY_REPLAY_SAVE)) {
                            if (!replay_save_key_pressed) {
                                    if(recorder_save_replay()) {
                                            recorder_trigger_replay_flash();
                                    } else {
                                            recorder_trigger_error_flash();
                                    }
                                    replay_save_key_pressed = true;
                            }
                    } else {
                            replay_save_key_pressed = false;
                    }

 
                 recorder_capture_frame();
 
#ifndef OPENGL_ES
                if(recorder_is_flashing()) {
                        glColor4f(0.0F, 1.0F, 0.0F, 0.5F);
                        glLineWidth(5.0F);
                        glBegin(GL_LINE_LOOP);
                        glVertex2f(0.0F, 0.0F);
                        glVertex2f((float)settings.window_width, 0.0F);
                        glVertex2f((float)settings.window_width, (float)settings.window_height);
                        glVertex2f(0.0F, (float)settings.window_height);
                        glEnd();
                        glLineWidth(1.0F);
                }
                if(recorder_is_error_flashing()) {
                        glColor4f(1.0F, 0.3F, 0.0F, 0.5F);
                        glLineWidth(5.0F);
                        glBegin(GL_LINE_LOOP);
                        glVertex2f(0.0F, 0.0F);
                        glVertex2f((float)settings.window_width, 0.0F);
                        glVertex2f((float)settings.window_width, (float)settings.window_height);
                        glVertex2f(0.0F, (float)settings.window_height);
                        glEnd();
                        glLineWidth(1.0F);
                }
 
                 if(recorder_is_recording_active()) {
                         float blink = sin(window_time() * 3.0F) * 0.5F + 0.5F;
                         if(blink > 0.0F) {
                                 glDisable(GL_TEXTURE_2D);
                                 glEnable(GL_BLEND);
                                 glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                                 glColor4f(1.0F, 0.0F, 0.0F, 0.7F);
                                 glLineWidth(2.0F);
                                 glBegin(GL_LINE_LOOP);
                                 glVertex2f(0.5F, 0.5F);
                                 glVertex2f((float)settings.window_width - 0.5F, 0.5F);
                                 glVertex2f((float)settings.window_width - 0.5F, (float)settings.window_height - 0.5F);
                                 glVertex2f(0.5F, (float)settings.window_height - 0.5F);
                                 glEnd();
                                 glLineWidth(1.0F);
                                 glDisable(GL_BLEND);
                                 glEnable(GL_TEXTURE_2D);
                         }
                 }
#endif
 
                 if(network_map_transfer_end) {
                         static float loading_screen_seen_time = 0.0F;
                         if(loading_screen_seen_time == 0.0F)
                                 loading_screen_seen_time = window_time();
                         if(window_time() - loading_screen_seen_time >= 0.5F) {
                                 network_map_transfer = 0;
                                 network_map_transfer_end = 0;
                                 loading_screen_seen_time = 0.0F;
                         }
                 }
 
                 sound_update();
                 network_update();
                 window_update();
 
                 rpc_update();
 
                 if(settings.vsync > 1 && (window_time() - last_frame_start) < (1.0 / settings.vsync)) {
                         if(settings.precise_pacing) {
                                 /* precise pacing: coarse nanosleep until ~1ms
                                    before the deadline, then spin the rest.
                                    A plain nanosleep regularly overshoots by
                                    milliseconds, which shows up as judder. */
                                 double target = last_frame_start + 1.0 / settings.vsync;
                                 for(;;) {
                                         double remain = target - window_time();
                                         if(remain <= 0.0)
                                                 break;
                                         if(remain > 0.0012) {
                                                 double sl = remain - 0.001;
                                                 struct timespec ts;
                                                 ts.tv_sec = (int)sl;
                                                 ts.tv_nsec = (sl - ts.tv_sec) * 1000000000.0;
                                                 nanosleep(&ts, NULL);
                                         }
                                 }
                         } else {
                                 double sleep_s = 1.0 / settings.vsync - (window_time() - last_frame_start);
                                 struct timespec ts;
                                 ts.tv_sec = (int)sleep_s;
                                 ts.tv_nsec = (sleep_s - ts.tv_sec) * 1000000000.0;
                                 nanosleep(&ts, NULL);
                         }
                 }
 
                 fps = 1.0F / dt;

        }
}
