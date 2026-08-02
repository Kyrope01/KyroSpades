
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

/*
 * KyroSpades audio engine v2 — "YSR-flavoured" OpenAL Soft backend.
 *
 * What this adds over the old plain-OpenAL backend:
 *   1. HRTF binaural rendering (ITD+ILD: real inter-ear delays, not just
 *      loudness panning). Requested via ALC_HRTF_SOFT context attrs.
 *   2. EFX EAXReverb driven by a YSR-style Monte-Carlo room probe:
 *      4 rays/frame from the listener, 128-entry ring, integrated to the
 *      five YSR scalars (roomVolume/roomArea/roomSize/reflections/
 *      feedbackness), mapped through Sabine (RT60 = 0.161*V/A).
 *   3. Continuous occlusion: 3x3x3 grid probe source->listener; blocked
 *      fraction drives BOTH a gain duck and an HF lowpass (smoother than
 *      YSR's binary 1.0<->0.4 step, more muffled than zerospades' pure LP).
 *   4. Distance air absorption (HF rolloff with range), mild Doppler.
 *   5. Voice management: on source exhaustion, steal the farthest voice.
 *
 * Hard requirement: link against OpenAL SOFT. System OpenAL (esp. Apple's)
 * has neither EFX nor HRTF. Everything degrades gracefully at runtime if
 * EFX is absent (falls back to old behaviour), but don't ship that way.
 */

#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "common.h"
#include "sound.h"
#include "config.h"
#include "file.h"
#include "log.h"
#include "camera.h"
#include "entitysystem.h"

#ifdef USE_SOUND
int sound_enabled = 1;
#else
int sound_enabled = 0;
#endif

#ifdef USE_SOUND

/* ---- EFX plumbing -------------------------------------------------- */
/* Constants/typedefs inlined so we don't depend on <AL/efx.h> existing;
 * function pointers are fetched at runtime via alGetProcAddress. */

#define ALC_HRTF_SOFT 0x1992
#define AL_EFFECTSLOT_EFFECT 0x0001
#define AL_EFFECT_TYPE 0x8001
#define AL_EFFECT_EAXREVERB 0x8000
#define AL_FILTER_TYPE 0x8001
#define AL_FILTER_LOWPASS 0x0001
#define AL_LOWPASS_GAIN 0x0001
#define AL_LOWPASS_GAINHF 0x0002
#define AL_DIRECT_FILTER 0x20005
#define AL_AUXILIARY_SEND_FILTER 0x20006
#define AL_AIR_ABSORPTION_FACTOR 0x20007
#define AL_EAXREVERB_DENSITY 0x0001
#define AL_EAXREVERB_DIFFUSION 0x0002
#define AL_EAXREVERB_GAIN 0x0003
#define AL_EAXREVERB_GAINHF 0x0004
#define AL_EAXREVERB_DECAY_TIME 0x0006
#define AL_EAXREVERB_DECAY_HFRATIO 0x0007
#define AL_EAXREVERB_REFLECTIONS_GAIN 0x0009
#define AL_EAXREVERB_REFLECTIONS_DELAY 0x000A
#define AL_EAXREVERB_LATE_REVERB_GAIN 0x000C
#define AL_EAXREVERB_LATE_REVERB_DELAY 0x000D

typedef void (*LPALGENEFFECTS)(int, unsigned int*);
typedef void (*LPALDELETEEFFECTS)(int, const unsigned int*);
typedef void (*LPALEFFECTI)(unsigned int, int, int);
typedef void (*LPALEFFECTF)(unsigned int, int, float);
typedef void (*LPALGENAUXSLOTS)(int, unsigned int*);
typedef void (*LPALAUXSLOTI)(unsigned int, int, int);
typedef void (*LPALGENFILTERS)(int, unsigned int*);
typedef void (*LPALDELETEFILTERS)(int, const unsigned int*);
typedef void (*LPALFILTERI)(unsigned int, int, int);
typedef void (*LPALFILTERF)(unsigned int, int, float);

static LPALGENEFFECTS p_alGenEffects;
static LPALDELETEEFFECTS p_alDeleteEffects;
static LPALEFFECTI p_alEffecti;
static LPALEFFECTF p_alEffectf;
static LPALGENAUXSLOTS p_alGenAuxiliaryEffectSlots;
static LPALAUXSLOTI p_alAuxiliaryEffectSloti;
static LPALGENFILTERS p_alGenFilters;
static LPALDELETEFILTERS p_alDeleteFilters;
static LPALFILTERI p_alFilteri;
static LPALFILTERF p_alFilterf;

static int efx_ok = 0;               /* EFX present + wired */
static unsigned int fx_reverb = 0;   /* EAXReverb effect */
static unsigned int fx_slot = 0;     /* aux slot world sources feed */

/* ---- YSR room probe state ------------------------------------------ */

#define ROOM_RING 128
#define ROOM_RAY_RANGE 40.0F
#define ROOM_RAYS_MAX 16 /* absolute cap for the ray count below */

/* ---- audio tuning knobs (tuned in-game via temporary sliders, now baked) --
 * Change these numbers to re-tune. Ranges are what the old sliders allowed. */
#define SOUND_OCCL_DUCK 0.15F        /* [0..1]  blocked-sound gain duck depth   */
#define SOUND_OCCL_MUFFLE 0.37F      /* [0..1]  blocked-sound HF muffle depth   */
#define SOUND_REVERB_WETNESS 0.91F   /* [0..2]  reverb send level               */
#define SOUND_REVERB_DECAY_MIN 0.10F /* [0.02..2]  shortest reverb tail, sec    */
#define SOUND_REVERB_DECAY_MAX 4.91F /* [2..40]    longest reverb tail, sec     */
#define SOUND_ROOM_RAYS 4            /* [1..16] room-probe rays per frame       */
#define SOUND_OCCL_INTERVAL 4        /* [1..16] re-probe occlusion every N frames */

static float room_dist[ROOM_RING];   /* hit distance, <0 = miss */
static unsigned char room_fb[ROOM_RING]; /* blocked both ways */
static int room_head = 0;
static int room_filled = 0;
static float room_update_timer = 0.0F;

#endif /* USE_SOUND */

struct Sound_source {
        int openal_handle;
        char local;
        int stick_to_player;
        float base_gain;       /* gain before occlusion duck */
        float x, y, z;         /* world pos (unscaled) for occlusion/steal */
        float occl;            /* smoothed blocked fraction [0..1] */
        unsigned int filter;   /* per-source lowpass, 0 if EFX off */
        unsigned char probe_phase; /* stagger occlusion probes */
};

struct entity_system sound_sources;

struct Sound_wav sound_footstep1;
struct Sound_wav sound_footstep2;
struct Sound_wav sound_footstep3;
struct Sound_wav sound_footstep4;

struct Sound_wav sound_wade1;
struct Sound_wav sound_wade2;
struct Sound_wav sound_wade3;
struct Sound_wav sound_wade4;

struct Sound_wav sound_jump;
struct Sound_wav sound_jump_water;

struct Sound_wav sound_land;
struct Sound_wav sound_land_water;

struct Sound_wav sound_hurt_fall;

struct Sound_wav sound_explode;
struct Sound_wav sound_explode_water;
struct Sound_wav sound_grenade_bounce;
struct Sound_wav sound_grenade_pin;

struct Sound_wav sound_pickup;
struct Sound_wav sound_horn;

struct Sound_wav sound_rifle_shoot;
struct Sound_wav sound_rifle_reload;
struct Sound_wav sound_smg_shoot;
struct Sound_wav sound_smg_reload;
struct Sound_wav sound_shotgun_shoot;
struct Sound_wav sound_shotgun_reload;
struct Sound_wav sound_shotgun_cock;

struct Sound_wav sound_hitground;
struct Sound_wav sound_hitplayer;
struct Sound_wav sound_headshot;
struct Sound_wav sound_hitbody;
struct Sound_wav sound_build;

struct Sound_wav sound_spade_woosh;
struct Sound_wav sound_spade_whack;

struct Sound_wav sound_death;
struct Sound_wav sound_kill;
struct Sound_wav sound_beep1;
struct Sound_wav sound_beep2;
struct Sound_wav sound_chat;
struct Sound_wav sound_switch;
struct Sound_wav sound_empty;
struct Sound_wav sound_intro;

struct Sound_wav sound_debris;
struct Sound_wav sound_bounce;
struct Sound_wav sound_impact;

struct Sound_wav sound_zoomin;
struct Sound_wav sound_zoomout;

struct Sound_wav sound_screenshot;
struct Sound_wav sound_rain;

void sound_volume(float vol) {
#ifdef USE_SOUND
        if(sound_enabled)
                alListenerf(AL_GAIN, vol);
#endif
}

static int rain_playing = 0;
#ifdef USE_SOUND
static ALuint rain_source = 0;
// Rain fade state. Matches Mineclonia rain.lua which uses
// core.sound_fade(handle, -0.5, 0.0) — i.e. fade out at -0.5 gain/sec toward 0.
// 0 = idle, 1 = playing (full gain), 2 = fading out
static int rain_fade_state = 0;
static float rain_gain = 0.0F;
#define RAIN_FADE_RATE 0.5F       // gain change per second (Mineclonia: -0.5)
#define RAIN_TARGET_GAIN 1.0F // full volume when active (Mineclonia starts at full)
#endif

void sound_rain_start(void) {
#ifdef USE_SOUND
        if(!sound_enabled)
                return;
        if(!sound_rain.openal_buffer)
                return;
        // If currently fading out, just cancel the fade and snap back to full volume.
        if(rain_fade_state == 2) {
                rain_fade_state = 1;
                rain_gain = RAIN_TARGET_GAIN;
                if(rain_source)
                        alSourcef(rain_source, AL_GAIN, rain_gain);
                return;
        }
        if(rain_playing)
                return;
        alGenSources(1, &rain_source);
        alSourcei(rain_source, AL_LOOPING, AL_TRUE);
        alSourcef(rain_source, AL_GAIN, RAIN_TARGET_GAIN);
        alSource3f(rain_source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcei(rain_source, AL_SOURCE_RELATIVE, AL_TRUE);
        alSourcei(rain_source, AL_BUFFER, sound_rain.openal_buffer);
        alSourcePlay(rain_source);
        rain_playing = 1;
        rain_fade_state = 1;
        rain_gain = RAIN_TARGET_GAIN;
#endif
}

void sound_rain_stop(void) {
#ifdef USE_SOUND
        if(!rain_playing)
                return;
        // Begin fading out instead of cutting abruptly — mirrors Mineclonia's
        // core.sound_fade(handler, -0.5, 0.0). sound_rain_update() finishes the job.
        rain_fade_state = 2;
#endif
}

void sound_rain_update(float dt) {
#ifdef USE_SOUND
        if(rain_fade_state != 2 || !rain_playing)
                return;
        rain_gain -= RAIN_FADE_RATE * dt;
        if(rain_gain <= 0.0F) {
                rain_gain = 0.0F;
                alSourceStop(rain_source);
                alDeleteSources(1, &rain_source);
                rain_source = 0;
                rain_playing = 0;
                rain_fade_state = 0;
        } else {
                alSourcef(rain_source, AL_GAIN, rain_gain);
        }
#endif
}

#ifdef USE_SOUND

static float randf(void) {
        return ms_rand() / 32767.0F;
}

/* ---- voice stealing ------------------------------------------------ */

struct steal_ctx {
        struct Sound_source* victim;
        float best_d2;
};

static bool sound_steal_visit(void* obj, void* user) {
        struct Sound_source* s = (struct Sound_source*)obj;
        struct steal_ctx* c = (struct steal_ctx*)user;
        if(s->local)
                return false;
        float d2 = distance3D(camera_x, camera_y, camera_z, s->x, s->y, s->z);
        if(d2 > c->best_d2) {
                c->best_d2 = d2;
                c->victim = s;
        }
        return false;
}

static void sound_source_free(struct Sound_source* s) {
        alDeleteSources(1, (ALuint*)&s->openal_handle);
        if(s->filter && efx_ok)
                p_alDeleteFilters(1, &s->filter);
}

/* ---- occlusion ------------------------------------------------------ */

/* 3x3x3 grid (+-0.2u) around the source, ray each point -> listener.
 * Returns blocked fraction [0..1]. Continuous, unlike YSR's 1.0/0.4 step. */
static float sound_occlusion_probe(float sx, float sy, float sz) {
        float dx = camera_x - sx, dy = camera_y - sy, dz = camera_z - sz;
        float dist = sqrtf(dx * dx + dy * dy + dz * dz);
        if(dist < 1.0F)
                return 0.0F;

        int blocked = 0;
        struct Camera_HitType hit;
        for(int ix = -1; ix <= 1; ix++) {
                for(int iy = -1; iy <= 1; iy++) {
                        for(int iz = -1; iz <= 1; iz++) {
                                float ox = sx + ix * 0.2F, oy = sy + iy * 0.2F, oz = sz + iz * 0.2F;
                                float rx = camera_x - ox, ry = camera_y - oy, rz = camera_z - oz;
                                camera_hit_mask(&hit, -1, ox, oy, oz, rx, ry, rz, dist);
                                if(hit.type == CAMERA_HITTYPE_BLOCK && hit.distance < dist - 0.5F)
                                        blocked++;
                        }
                }
        }
        return blocked / 27.0F;
}

static void sound_apply_occlusion(struct Sound_source* s) {
        /* Tuned knobs (were live sliders during dev, now baked in).
         * duck: 1.0 (clear) -> (1 - OCCL_DUCK) when fully buried
         * HF:   1.0 (clear) -> (1 - OCCL_MUFFLE) when fully buried */
        float duck = 1.0F - s->occl * SOUND_OCCL_DUCK;
        float hf = 1.0F - s->occl * SOUND_OCCL_MUFFLE;
        alSourcef(s->openal_handle, AL_GAIN, s->base_gain * duck);
        if(efx_ok && s->filter) {
                p_alFilterf(s->filter, AL_LOWPASS_GAIN, 1.0F);
                p_alFilterf(s->filter, AL_LOWPASS_GAINHF, hf);
                alSourcei(s->openal_handle, AL_DIRECT_FILTER, s->filter);
        }
}

/* ---- room probe (YSR verbatim math) --------------------------------- */

static void sound_room_probe(void) {
        struct Camera_HitType hit;
        int rays = SOUND_ROOM_RAYS;
        if(rays < 1) rays = 1;
        if(rays > ROOM_RAYS_MAX) rays = ROOM_RAYS_MAX;
        for(int i = 0; i < rays; i++) {
                /* random direction on sphere */
                float a = randf() * 2.0F * PI, c = randf() * 2.0F - 1.0F;
                float sc = sqrtf(1.0F - c * c);
                float rx = cosf(a) * sc, ry = c, rz = sinf(a) * sc;

                float d = -1.0F;
                unsigned char fb = 0;
                camera_hit_mask(&hit, -1, camera_x, camera_y, camera_z, rx, ry, rz, ROOM_RAY_RANGE);
                if(hit.type == CAMERA_HITTYPE_BLOCK) {
                        d = hit.distance;
                        camera_hit_mask(&hit, -1, camera_x, camera_y, camera_z, -rx, -ry, -rz, ROOM_RAY_RANGE);
                        fb = (hit.type == CAMERA_HITTYPE_BLOCK);
                }

                room_dist[room_head] = d;
                room_fb[room_head] = fb;
                room_head = (room_head + 1) % ROOM_RING;
                if(room_filled < ROOM_RING)
                        room_filled++;
        }
}

static void sound_room_apply(void) {
        if(!efx_ok || room_filled < ROOM_RING / 2)
                return;

        int hits = 0, fbs = 0;
        float vol = 0.0F, area = 0.0F, size = 0.0F;
        for(int i = 0; i < room_filled; i++) {
                if(room_dist[i] >= 0.0F) {
                        hits++;
                        vol += room_dist[i] * room_dist[i];
                        area += room_dist[i];
                        size += room_dist[i];
                        fbs += room_fb[i];
                }
        }

        float reflections = (float)hits / room_filled;
        float feedbackness = (float)fbs / room_filled;
        float roomVolume, roomArea;

        if(reflections > 0.25F) {
                roomVolume = (vol / hits) * (4.0F / 3.0F) * PI;
                roomArea = (area / hits) * 4.0F * PI;
        } else {
                /* open sky: canned small values, reverb fades out */
                roomVolume = 8.0F;
                roomArea = 24.0F;
        }

        /* Sabine (zerospades mapping) */
        float decay = 0.161F * roomVolume / roomArea / 0.4F;
        float decay_min = SOUND_REVERB_DECAY_MIN;
        float decay_max = SOUND_REVERB_DECAY_MAX;
        if(decay_max < decay_min) decay_max = decay_min;
        if(decay < decay_min) decay = decay_min;
        if(decay > decay_max) decay = decay_max;

        float late = powf(reflections, 4.0F) * powf(feedbackness, 3.0F) * 0.34F;
        float refl = reflections * reflections * 0.16F;
        float sz = size / (hits ? hits : 1);

        p_alEffectf(fx_reverb, AL_EAXREVERB_DECAY_TIME, decay);
        p_alEffectf(fx_reverb, AL_EAXREVERB_LATE_REVERB_GAIN, fminf(late, 10.0F));
        p_alEffectf(fx_reverb, AL_EAXREVERB_REFLECTIONS_GAIN, fminf(refl, 3.16F));
        p_alEffectf(fx_reverb, AL_EAXREVERB_REFLECTIONS_DELAY, fminf(sz / 343.0F, 0.3F));
        p_alEffectf(fx_reverb, AL_EAXREVERB_LATE_REVERB_DELAY, fminf(sz / 343.0F * 2.0F, 0.1F));
        p_alEffectf(fx_reverb, AL_EAXREVERB_DIFFUSION, fminf(reflections + 0.3F, 1.0F));
        p_alEffectf(fx_reverb, AL_EAXREVERB_GAIN, SOUND_REVERB_WETNESS * reflections);
        p_alEffectf(fx_reverb, AL_EAXREVERB_GAINHF, 0.6F);
        p_alEffectf(fx_reverb, AL_EAXREVERB_DECAY_HFRATIO, 0.7F);
        /* re-attach updated effect */
        p_alAuxiliaryEffectSloti(fx_slot, AL_EFFECTSLOT_EFFECT, fx_reverb);
}

#endif /* USE_SOUND */

static void sound_createEx(enum sound_space option, struct Sound_wav* w, float x, float y, float z, float vx, float vy,
                                                   float vz, int player) {
#ifdef USE_SOUND
        if(!sound_enabled)
                return;

        struct Sound_source s = (struct Sound_source) {
                .local = option == SOUND_LOCAL,
                .stick_to_player = player,
                .x = x, .y = y, .z = z,
                .occl = 0.0F,
                .filter = 0,
                .probe_phase = (unsigned char)(ms_rand() % (SOUND_OCCL_INTERVAL < 1 ? 1 : SOUND_OCCL_INTERVAL)),
        };

        alGetError();
        alGenSources(1, (ALuint*)&s.openal_handle);

        if(alGetError() != AL_NO_ERROR) {
                /* out of voices: steal the farthest world source and retry */
                struct steal_ctx c = {NULL, -1.0F};
                entitysys_iterate(&sound_sources, &c, sound_steal_visit);
                if(!c.victim)
                        return;
                sound_source_free(c.victim);
                /* mark victim dead; its entity gets reaped next update */
                c.victim->openal_handle = 0;
                c.victim->stick_to_player = -1;
                alGetError();
                alGenSources(1, (ALuint*)&s.openal_handle);
                if(alGetError() != AL_NO_ERROR)
                        return;
        }

        float pitch = 1.0F;
        float gain = 1.0F;

        if(w == &sound_rifle_shoot || w == &sound_smg_shoot || w == &sound_shotgun_shoot) {
                pitch = 0.9F + randf() * 0.2F;
                gain = 3.0F;
        }

        s.base_gain = gain;

        alSourcef(s.openal_handle, AL_PITCH, pitch);
        alSourcef(s.openal_handle, AL_GAIN, gain);
        alSourcef(s.openal_handle, AL_REFERENCE_DISTANCE, s.local ? 0.0F : 15.0F);
        alSourcef(s.openal_handle, AL_MAX_DISTANCE, s.local ? 2048.0F : 1e10F);
        alSourcef(s.openal_handle, AL_ROLLOFF_FACTOR, s.local ? 0.0F : 1.0F);
        alSource3f(s.openal_handle, AL_POSITION, s.local ? 0.0F : x * SOUND_SCALE, s.local ? 0.0F : y * SOUND_SCALE,
                           s.local ? 0.0F : z * SOUND_SCALE);
        alSource3f(s.openal_handle, AL_VELOCITY, s.local ? 0.0F : vx * SOUND_SCALE, s.local ? 0.0F : vy * SOUND_SCALE,
                           s.local ? 0.0F : vz * SOUND_SCALE);
        alSourcei(s.openal_handle, AL_SOURCE_RELATIVE, s.local);
        alSourcei(s.openal_handle, AL_LOOPING, AL_FALSE);
        alSourcei(s.openal_handle, AL_BUFFER, w->openal_buffer);

        if(efx_ok && !s.local) {
                /* world sources feed the room reverb + get an occlusion filter */
                alSource3i(s.openal_handle, AL_AUXILIARY_SEND_FILTER, fx_slot, 0, 0);
                alSourcef(s.openal_handle, AL_AIR_ABSORPTION_FACTOR, 1.0F);
                p_alGenFilters(1, &s.filter);
                p_alFilteri(s.filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
                /* first occlusion answer immediately, so shots behind walls
                 * never pop at full brightness for a frame */
                s.occl = sound_occlusion_probe(x, y, z);
                struct Sound_source* sp = &s;
                sound_apply_occlusion(sp);
        }

        alSourcePlay(s.openal_handle);

        if(alGetError() == AL_NO_ERROR) {
                entitysys_add(&sound_sources, &s);
        } else {
                sound_source_free(&s);
        }
#endif
}

void sound_create_sticky(struct Sound_wav* w, struct Player* player, int player_id) {
        sound_createEx(SOUND_WORLD, w, player->pos.x, player->pos.y, player->pos.z, 0.0F, 0.0F, 0.0F, player_id);
}

void sound_create(enum sound_space option, struct Sound_wav* w, float x, float y, float z) {
        sound_createEx(option, w, x, y, z, 0.0F, 0.0F, 0.0F, -1);
}

void sound_velocity(struct Sound_source* s, float vx, float vy, float vz) {
#ifdef USE_SOUND
        if(!sound_enabled || s->local)
                return;
        alSource3f(s->openal_handle, AL_VELOCITY, vx * SOUND_SCALE, vy * SOUND_SCALE, vz * SOUND_SCALE);
#endif
}

void sound_position(struct Sound_source* s, float x, float y, float z) {
#ifdef USE_SOUND
        if(!sound_enabled || s->local)
                return;

        s->x = x;
        s->y = y;
        s->z = z;
        alSource3f(s->openal_handle, AL_POSITION, x * SOUND_SCALE, y * SOUND_SCALE, z * SOUND_SCALE);
#endif
}

#ifdef USE_SOUND
static unsigned int sound_frame = 0;

static bool sound_update_single(void* obj, void* user) {
        struct Sound_source* s = (struct Sound_source*)obj;

        if(!s->openal_handle) /* stolen voice */
                return true;

        int source_state;
        alGetSourcei(s->openal_handle, AL_SOURCE_STATE, &source_state);
        if(source_state == AL_STOPPED || (s->stick_to_player >= 0 && !players[s->stick_to_player].connected)) {
                sound_source_free(s);
                return true;
        }

        if(s->stick_to_player >= 0) {
                sound_position(s, players[s->stick_to_player].pos.x, players[s->stick_to_player].pos.y,
                                           players[s->stick_to_player].pos.z);
                sound_velocity(s, players[s->stick_to_player].physics.velocity.x,
                                           players[s->stick_to_player].physics.velocity.y,
                                           players[s->stick_to_player].physics.velocity.z);
        }

        /* amortised occlusion: each source re-probes every 8th frame,
         * phase-staggered so probes spread evenly across frames */
        int interval = SOUND_OCCL_INTERVAL < 1 ? 1 : SOUND_OCCL_INTERVAL;
        if(efx_ok && !s->local && ((sound_frame % interval) == (s->probe_phase % interval))) {
                float target = sound_occlusion_probe(s->x, s->y, s->z);
                s->occl += (target - s->occl) * 0.5F; /* smooth, no zipper */
                sound_apply_occlusion(s);
        }

        return false;
}
#endif

void sound_update() {
#ifdef USE_SOUND
        if(!sound_enabled)
                return;

        sound_frame++;

        float orientation[] = {
                sin(camera_rot_x) * sin(camera_rot_y),
                cos(camera_rot_y),
                cos(camera_rot_x) * sin(camera_rot_y),
                0.0F,
                1.0F,
                0.0F,
        };

        alListener3f(AL_POSITION, camera_x * SOUND_SCALE, camera_y * SOUND_SCALE, camera_z * SOUND_SCALE);
        alListener3f(AL_VELOCITY, camera_vx * SOUND_SCALE, camera_vy * SOUND_SCALE, camera_vz * SOUND_SCALE);
        alListenerfv(AL_ORIENTATION, orientation);

        if(efx_ok) {
                sound_room_probe();
                /* integrating 128 samples + 9 effect params every frame is
                 * pointless; 10 Hz is plenty for a room that changes as you walk */
                if((sound_frame % 6) == 0)
                        sound_room_apply();
        }

        entitysys_iterate(&sound_sources, NULL, sound_update_single);
#endif
}

extern short* drwav_open_and_read_file_s16(const char* filename, unsigned int* channels, unsigned int* sampleRate,
                                                                                   uint64_t* totalFrameCount);
extern short* drwav_open_and_read_memory_s16(const void* data, size_t dataSize, unsigned int* channels,
                                                                                          unsigned int* sampleRate, uint64_t* totalFrameCount);

/* Read + decode a wav file, transparently handling APK assets on Android
 * (dr_wav's fopen() based loader can't see inside the APK). */
static short* sound_read_samples(const char* name, unsigned int* channels, unsigned int* samplerate,
                                                                 uint64_t* samplecount) {
#ifdef USE_ANDROID_FILE
        short* samples = NULL;
        int file_len = file_size(name);
        unsigned char* file_data = (file_len > 0) ? file_load(name) : NULL;
        if(file_data) {
                samples = drwav_open_and_read_memory_s16(file_data, file_len, channels, samplerate, samplecount);
                free(file_data);
        }
        return samples;
#else
        return drwav_open_and_read_file_s16(name, channels, samplerate, samplecount);
#endif
}

int sound_reload(struct Sound_wav* wav, const char* name, float min, float max) {
#ifdef USE_SOUND
        if(!sound_enabled)
                return 0;
        if(!file_exists(name))
                return -1;
        if(wav->openal_buffer)
                alDeleteBuffers(1, (ALuint*)&wav->openal_buffer);
        unsigned int channels, samplerate;
        uint64_t samplecount;
        short* samples = sound_read_samples(name, &channels, &samplerate, &samplecount);
        if(samples == NULL) {
                wav->openal_buffer = 0;
                return -1;
        }
        short* audio;
        if(channels > 1) {
                audio = malloc(samplecount * sizeof(short) / 2);
                if(!audio) { free(samples); return -1; }
                for(uint64_t k = 0; k < samplecount / 2; k++)
                        audio[k] = ((int)samples[k * 2] + (int)samples[k * 2 + 1]) / 2;
                free(samples);
        }
        alGenBuffers(1, (ALuint*)&wav->openal_buffer);
        alBufferData(wav->openal_buffer, AL_FORMAT_MONO16, (channels > 1) ? audio : samples,
                                 samplecount * sizeof(short) / channels, samplerate);
        if(channels > 1) free(audio);
        wav->min = min;
        wav->max = max;
        return 0;
#else
        return -1;
#endif
}

void sound_load(struct Sound_wav* wav, char* name, float min, float max) {
#ifdef USE_SOUND
        if(!sound_enabled)
                return;
        unsigned int channels, samplerate;
        uint64_t samplecount;
        short* samples = sound_read_samples(name, &channels, &samplerate, &samplecount);
        if(samples == NULL) {
                log_fatal("Could not load sound %s", name);
                exit(1);
        }
        log_debug("Loaded sound: %s (%ich, %iHz, %lu samples)", name, channels, samplerate, (unsigned long)samplecount);

        short* audio;
        if(channels > 1) { // convert stereo to mono
                audio = malloc(samplecount * sizeof(short) / 2);
                CHECK_ALLOCATION_ERROR(audio)
                for(int k = 0; k < samplecount / 2; k++)
                        audio[k] = ((int)samples[k * 2] + (int)samples[k * 2 + 1]) / 2; // prevent overflow
        free(samples);
        }

        alGenBuffers(1, (ALuint*)&wav->openal_buffer);
        alBufferData(wav->openal_buffer, AL_FORMAT_MONO16, (channels > 1) ? audio : samples,
                                 samplecount * sizeof(short) / channels, samplerate);

        wav->min = min;
        wav->max = max;
#endif
}

#ifdef USE_SOUND
static void sound_efx_init(ALCdevice* device) {
        if(!alcIsExtensionPresent(device, "ALC_EXT_EFX")) {
                log_warn("EFX not available - no reverb/occlusion filtering (use OpenAL Soft!)");
                return;
        }

        p_alGenEffects = (LPALGENEFFECTS)alGetProcAddress("alGenEffects");
        p_alDeleteEffects = (LPALDELETEEFFECTS)alGetProcAddress("alDeleteEffects");
        p_alEffecti = (LPALEFFECTI)alGetProcAddress("alEffecti");
        p_alEffectf = (LPALEFFECTF)alGetProcAddress("alEffectf");
        p_alGenAuxiliaryEffectSlots = (LPALGENAUXSLOTS)alGetProcAddress("alGenAuxiliaryEffectSlots");
        p_alAuxiliaryEffectSloti = (LPALAUXSLOTI)alGetProcAddress("alAuxiliaryEffectSloti");
        p_alGenFilters = (LPALGENFILTERS)alGetProcAddress("alGenFilters");
        p_alDeleteFilters = (LPALDELETEFILTERS)alGetProcAddress("alDeleteFilters");
        p_alFilteri = (LPALFILTERI)alGetProcAddress("alFilteri");
        p_alFilterf = (LPALFILTERF)alGetProcAddress("alFilterf");

        if(!p_alGenEffects || !p_alGenAuxiliaryEffectSlots || !p_alGenFilters)
                return;

        alGetError();
        p_alGenEffects(1, &fx_reverb);
        p_alEffecti(fx_reverb, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        if(alGetError() != AL_NO_ERROR) {
                log_warn("EAXReverb unsupported, reverb disabled");
                return;
        }
        p_alGenAuxiliaryEffectSlots(1, &fx_slot);
        p_alAuxiliaryEffectSloti(fx_slot, AL_EFFECTSLOT_EFFECT, fx_reverb);
        if(alGetError() != AL_NO_ERROR)
                return;

        for(int i = 0; i < ROOM_RING; i++)
                room_dist[i] = -1.0F;

        efx_ok = 1;
        log_info("EFX reverb + occlusion enabled");
}
#endif

void sound_init() {
#ifdef USE_SOUND
        entitysys_create(&sound_sources, sizeof(struct Sound_source), 256);

        ALCdevice* device = alcOpenDevice(NULL);

        if(!device) {
                sound_enabled = 0;
                log_warn("Could not open sound device!");
                return;
        }

        /* Request HRTF: real binaural rendering (interaural time delay +
         * level + spectral cues), not loudness-only panning. OpenAL Soft
         * honours this; other implementations ignore unknown attrs. */
        int attrs[] = {ALC_HRTF_SOFT, ALC_TRUE, 0};
        ALCcontext* context = alcCreateContext(device, attrs);
        if(!context)
                context = alcCreateContext(device, NULL); /* picky driver fallback */
        if(!alcMakeContextCurrent(context)) {
                sound_enabled = 0;
                log_warn("Could not enter sound device context!");
                return;
        }

        {
                int hrtf = 0;
                alcGetIntegerv(device, ALC_HRTF_SOFT, 1, &hrtf);
                log_info("HRTF: %s", hrtf ? "on" : "off (stereo panning fallback)");
        }

        alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
        alDopplerFactor(0.3F); /* subtle; full doppler sounds cartoonish at AoS speeds */

        sound_efx_init(device);

        sound_volume(settings.volume / 10.0F);

        sound_load(&sound_footstep1, "wav/footstep1.wav", 0.1F, 32.0F);
        sound_load(&sound_footstep2, "wav/footstep2.wav", 0.1F, 32.0F);
        sound_load(&sound_footstep3, "wav/footstep3.wav", 0.1F, 32.0F);
        sound_load(&sound_footstep4, "wav/footstep4.wav", 0.1F, 32.0F);

        sound_load(&sound_wade1, "wav/wade1.wav", 0.1F, 32.0F);
        sound_load(&sound_wade2, "wav/wade2.wav", 0.1F, 32.0F);
        sound_load(&sound_wade3, "wav/wade3.wav", 0.1F, 32.0F);
        sound_load(&sound_wade4, "wav/wade4.wav", 0.1F, 32.0F);

        sound_load(&sound_jump, "wav/jump.wav", 0.1F, 32.0F);
        sound_load(&sound_land, "wav/land.wav", 0.1F, 32.0F);
        sound_load(&sound_jump_water, "wav/waterjump.wav", 0.1F, 32.0F);
        sound_load(&sound_land_water, "wav/waterland.wav", 0.1F, 32.0F);

        sound_load(&sound_explode, "wav/explode.wav", 0.1F, 53.0F);
        sound_load(&sound_explode_water, "wav/waterexplode.wav", 0.1F, 53.0F);
        sound_load(&sound_grenade_bounce, "wav/grenadebounce.wav", 0.1F, 48.0F);
        sound_load(&sound_grenade_pin, "wav/pin.wav", 0.1F, 48.0F);

        sound_load(&sound_hurt_fall, "wav/fallhurt.wav", 0.1F, 32.0F);

        sound_load(&sound_pickup, "wav/pickup.wav", 0.1F, 1024.0F);
        sound_load(&sound_horn, "wav/horn.wav", 0.1F, 1024.0F);

        sound_load(&sound_rifle_shoot, "wav/semishoot.wav", 0.1F, 96.0F);
        sound_load(&sound_rifle_reload, "wav/semireload.wav", 0.1F, 16.0F);
        sound_load(&sound_smg_shoot, "wav/smgshoot.wav", 0.1F, 96.0F);
        sound_load(&sound_smg_reload, "wav/smgreload.wav", 0.1F, 16.0F);
        sound_load(&sound_shotgun_shoot, "wav/shotgunshoot.wav", 0.1F, 96.0F);
        sound_load(&sound_shotgun_reload, "wav/shotgunreload.wav", 0.1F, 16.0F);
        sound_load(&sound_shotgun_cock, "wav/cock.wav", 0.1F, 16.0F);

        sound_load(&sound_hitground, "wav/hitground.wav", 0.1F, 32.0F);
        sound_load(&sound_hitplayer, "wav/hitplayer.wav", 0.1F, 32.0F);
        sound_load(&sound_headshot, "wav/headshot.wav", 0.1F, 32.0F);
        sound_load(&sound_hitbody, "wav/hitbody.wav", 0.1F, 32.0F);
        sound_load(&sound_build, "wav/build.wav", 0.1F, 32.0F);

        sound_load(&sound_spade_woosh, "wav/woosh.wav", 0.1F, 32.0F);
        sound_load(&sound_spade_whack, "wav/whack.wav", 0.1F, 32.0F);

        sound_load(&sound_death, "wav/death.wav", 0.1F, 24.0F);
        /* Optional kill sound: loaded via sound_reload so a missing
         * wav/kill.wav is ignored instead of crashing the game. */
        sound_reload(&sound_kill, "wav/kill.wav", 0.1F, 1024.0F);
        sound_load(&sound_beep1, "wav/beep1.wav", 0.1F, 1024.0F);
        sound_load(&sound_beep2, "wav/beep2.wav", 0.1F, 1024.0F);
        sound_load(&sound_chat, "wav/chat.wav", 0.1F, 1024.0F);
        sound_load(&sound_switch, "wav/switch.wav", 0.1F, 1024.0F);
        sound_load(&sound_empty, "wav/empty.wav", 0.1F, 1024.0F);
        sound_load(&sound_intro, "wav/intro.wav", 0.1F, 1024.0F);

        sound_load(&sound_debris, "wav/debris.wav", 0.1F, 53.0F);
        sound_load(&sound_bounce, "wav/bounce.wav", 0.1F, 32.0F);
        sound_load(&sound_impact, "wav/impact.wav", 0.1F, 53.0F);

        sound_load(&sound_zoomin, "wav/zoomin.wav", 0.1F, 1024.0F);
        sound_load(&sound_zoomout, "wav/zoomout.wav", 0.1F, 1024.0F);

        sound_load(&sound_screenshot, "wav/screenshot.wav", 0.1F, 1024.0F);

        sound_load(&sound_rain, "wav/weather_rain.wav", 0.1F, 48.0F);
#endif
}
