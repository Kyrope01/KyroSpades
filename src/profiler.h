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

/* Lightweight per-frame CPU stage profiler.
 *
 * Why this exists: the client has grown a lot of per-frame work (chunk
 * uploads, water mesh rebuilds, lighting, post-processing, HUD) and there was
 * no way to tell which stage actually costs the frame time. Every optimisation
 * before this file was guesswork.
 *
 * Cost when disabled: one predictable branch per call site.
 * Cost when enabled: two clock_gettime() calls per stage plus a small ring
 * update per frame — a few microseconds, far below the noise floor of any
 * stage it measures.
 *
 * GPU timing intentionally lives outside this module: the CPU side is enough
 * to find the hot stages, and it works on every backend (GL 2.1, GLES, Core).
 */

#ifndef PROFILER_H
#define PROFILER_H

#include <stdbool.h>

enum prof_stage {
        PROF_FRAME = 0,   /* whole frame, measured automatically */
        PROF_NETWORK_IN,  /* early network_service() */
        PROF_PHYSICS,     /* player/particle/tracer/camera updates */
        PROF_CHUNKS,      /* chunk_update_all(): mesh upload + minimap */
        PROF_SCENE,       /* drawScene(): terrain, water, entities */
        PROF_POSTPROC,    /* off-screen target + composite pass */
        PROF_HUD,         /* HUD/UI command list and draw */
        PROF_NETWORK_OUT, /* network_update() */
        PROF_SOUND,       /* sound_update() */
        PROF_SWAP,        /* window_update()/buffer swap */
        PROF_STAGE_COUNT
};

/* enabled: overlay accumulation. csv_path: optional CSV, NULL for none.
   Both may be changed at runtime through prof_toggle()/prof_enabled(). */
void prof_init(bool enabled, const char* csv_path);
void prof_shutdown(void);

bool prof_enabled(void);
void prof_set_enabled(bool enabled);
void prof_toggle(void);

/* Starts a new frame sample. Called once at the top of the main loop. */
void prof_frame_begin(void);

/* Ends the frame sample and pushes it into the rolling window. */
void prof_frame_end(void);

void prof_stage_begin(enum prof_stage stage);
void prof_stage_end(enum prof_stage stage);

/* Rolling window statistics (ms). Valid even when disabled (last window). */
double prof_frame_ms_avg(void);
double prof_frame_ms_max(void);
double prof_stage_ms_avg(enum prof_stage stage);
double prof_stage_ms_max(enum prof_stage stage);
double prof_stage_pct(enum prof_stage stage);
const char* prof_stage_name(enum prof_stage stage);
int prof_stage_count(void);
int prof_sample_count(void);

/* Human readable "12.3" style formatting helper for the overlay. */
void prof_format_ms(char* out, unsigned long out_size, double ms);

#endif /* PROFILER_H */
