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

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <time.h>
#endif

#include "profiler.h"

/* Rolling window: ~4 seconds at 60 fps, ~1 second at 240 fps. Long enough to
   be stable on screen, short enough to react to a settings change. */
#define PROF_SAMPLES 240

static const char* stage_names[PROF_STAGE_COUNT] = {
        "frame",
        "net-in",
        "physics",
        "chunks",
        "scene",
        "postproc",
        "hud",
        "net-out",
        "sound",
        "swap",
};

/* ── Clock ─────────────────────────────────────────────────────────────── */

static double clock_now(void) {
#if defined(_WIN32)
        static LARGE_INTEGER freq;
        static bool freq_ready = false;
        LARGE_INTEGER counter;

        if(!freq_ready) {
                QueryPerformanceFrequency(&freq);
                freq_ready = true;
        }
        QueryPerformanceCounter(&counter);
        return (double)counter.QuadPart / (double)freq.QuadPart;
#elif defined(CLOCK_MONOTONIC)
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#else
        return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

/* ── State ─────────────────────────────────────────────────────────────── */

static bool prof_on = false;

static double frame_start = 0.0;
static double stage_start[PROF_STAGE_COUNT];
static int stage_depth[PROF_STAGE_COUNT];
static double stage_frame_ms[PROF_STAGE_COUNT];

/* Ring buffer of completed frames. */
static float sample_frame_ms[PROF_SAMPLES];
static float sample_stage_ms[PROF_STAGE_COUNT][PROF_SAMPLES];
static int sample_head = 0;   /* next write position */
static int sample_count = 0;  /* valid samples, <= PROF_SAMPLES */

static double sum_frame_ms = 0.0;
static double sum_stage_ms[PROF_STAGE_COUNT];
static double max_frame_ms = 0.0;
static double max_stage_ms[PROF_STAGE_COUNT];

static FILE* csv = NULL;
static unsigned long csv_frames = 0;
static double csv_last_flush = 0.0;
static char csv_path_copy[512];

/* ── CSV ───────────────────────────────────────────────────────────────── */

static void csv_write_header(void) {
        if(!csv)
                return;
        fprintf(csv, "frame,frame_ms");
        for(int s = 1; s < PROF_STAGE_COUNT; s++)
                fprintf(csv, ",%s_ms", stage_names[s]);
        fprintf(csv, "\n");
}

static void csv_write_row(double frame_ms) {
        if(!csv)
                return;

        fprintf(csv, "%lu,%.3f", csv_frames, frame_ms);
        for(int s = 1; s < PROF_STAGE_COUNT; s++)
                fprintf(csv, ",%.3f", stage_frame_ms[s]);
        fprintf(csv, "\n");
        csv_frames++;

        /* Flushing every frame turns profiling into an I/O benchmark. */
        double now = clock_now();
        if(now - csv_last_flush >= 5.0) {
                fflush(csv);
                csv_last_flush = now;
        }
}

/* ── Lifecycle ─────────────────────────────────────────────────────────── */

void prof_init(bool enabled, const char* path) {
        memset(sample_frame_ms, 0, sizeof(sample_frame_ms));
        memset(sample_stage_ms, 0, sizeof(sample_stage_ms));
        memset(sum_stage_ms, 0, sizeof(sum_stage_ms));
        memset(max_stage_ms, 0, sizeof(max_stage_ms));
        memset(stage_depth, 0, sizeof(stage_depth));
        sample_head = 0;
        sample_count = 0;
        sum_frame_ms = 0.0;
        max_frame_ms = 0.0;
        prof_on = enabled;

        if(csv) {
                fflush(csv);
                fclose(csv);
                csv = NULL;
        }

        csv_path_copy[0] = '\0';
        if(path && path[0]) {
                csv = fopen(path, "w");
                if(csv) {
                        /* 64 KiB buffer: profiling must not add a syscall per
                           frame to the very frames it is measuring. */
                        setvbuf(csv, NULL, _IOFBF, 64 * 1024);
                        snprintf(csv_path_copy, sizeof(csv_path_copy), "%s", path);
                        csv_write_header();
                        csv_frames = 0;
                        csv_last_flush = clock_now();
                        prof_on = true;
                }
        }
}

void prof_shutdown(void) {
        if(csv) {
                fflush(csv);
                fclose(csv);
                csv = NULL;
        }
        csv_path_copy[0] = '\0';
        prof_on = false;
}

bool prof_enabled(void) {
        return prof_on;
}

void prof_set_enabled(bool enabled) {
        prof_on = enabled;
}

void prof_toggle(void) {
        prof_on = !prof_on;
}

/* ── Sampling ──────────────────────────────────────────────────────────── */

void prof_frame_begin(void) {
        if(!prof_on)
                return;

        memset(stage_frame_ms, 0, sizeof(stage_frame_ms));
        memset(stage_depth, 0, sizeof(stage_depth));
        frame_start = clock_now();
}

void prof_stage_begin(enum prof_stage stage) {
        if(!prof_on || stage <= PROF_FRAME || stage >= PROF_STAGE_COUNT)
                return;

        if(stage_depth[stage]++ == 0)
                stage_start[stage] = clock_now();
}

void prof_stage_end(enum prof_stage stage) {
        if(!prof_on || stage <= PROF_FRAME || stage >= PROF_STAGE_COUNT)
                return;

        if(stage_depth[stage] <= 0)
                return;

        if(--stage_depth[stage] == 0)
                stage_frame_ms[stage] += (clock_now() - stage_start[stage]) * 1000.0;
}

void prof_frame_end(void) {
        if(!prof_on)
                return;

        double frame_ms = (clock_now() - frame_start) * 1000.0;

        /* Evict the oldest sample from the rolling sums before overwriting it. */
        if(sample_count == PROF_SAMPLES) {
                sum_frame_ms -= (double)sample_frame_ms[sample_head];
                for(int s = 1; s < PROF_STAGE_COUNT; s++)
                        sum_stage_ms[s] -= (double)sample_stage_ms[s][sample_head];
        }

        sample_frame_ms[sample_head] = (float)frame_ms;
        for(int s = 1; s < PROF_STAGE_COUNT; s++)
                sample_stage_ms[s][sample_head] = (float)stage_frame_ms[s];

        sum_frame_ms += frame_ms;
        for(int s = 1; s < PROF_STAGE_COUNT; s++)
                sum_stage_ms[s] += stage_frame_ms[s];

        /* Unaccounted stage time (main loop glue, input, recorder, pacing). */
        double accounted = 0.0;
        for(int s = 1; s < PROF_STAGE_COUNT; s++)
                accounted += stage_frame_ms[s];
        sample_stage_ms[0][sample_head] = (float)(frame_ms - accounted);

        if(sample_count < PROF_SAMPLES)
                sample_count++;

        sample_head = (sample_head + 1) % PROF_SAMPLES;

        /* Max is recomputed lazily from the window below; track a cheap
           running max that is reset when the ring wraps. */
        if(sample_head == 0 || sample_count == 1) {
                max_frame_ms = frame_ms;
                for(int s = 1; s < PROF_STAGE_COUNT; s++)
                        max_stage_ms[s] = stage_frame_ms[s];
        } else {
                if(frame_ms > max_frame_ms)
                        max_frame_ms = frame_ms;
                for(int s = 1; s < PROF_STAGE_COUNT; s++)
                        if(stage_frame_ms[s] > max_stage_ms[s])
                                max_stage_ms[s] = stage_frame_ms[s];
        }

        csv_write_row(frame_ms);
}

/* ── Queries ───────────────────────────────────────────────────────────── */

double prof_frame_ms_avg(void) {
        return sample_count ? sum_frame_ms / (double)sample_count : 0.0;
}

double prof_frame_ms_max(void) {
        return max_frame_ms;
}

double prof_stage_ms_avg(enum prof_stage stage) {
        if(stage <= PROF_FRAME || stage >= PROF_STAGE_COUNT || !sample_count)
                return 0.0;
        return sum_stage_ms[stage] / (double)sample_count;
}

double prof_stage_ms_max(enum prof_stage stage) {
        if(stage <= PROF_FRAME || stage >= PROF_STAGE_COUNT)
                return 0.0;
        return max_stage_ms[stage];
}

double prof_stage_pct(enum prof_stage stage) {
        double frame = prof_frame_ms_avg();
        if(frame <= 0.0001)
                return 0.0;
        return prof_stage_ms_avg(stage) * 100.0 / frame;
}

const char* prof_stage_name(enum prof_stage stage) {
        if(stage < 0 || stage >= PROF_STAGE_COUNT)
                return "?";
        return stage_names[stage];
}

int prof_stage_count(void) {
        return PROF_STAGE_COUNT;
}

int prof_sample_count(void) {
        return sample_count;
}

void prof_format_ms(char* out, unsigned long out_size, double ms) {
        if(!out || out_size == 0)
                return;

        if(ms < 0.0)
                ms = 0.0;
        if(ms < 10.0)
                snprintf(out, out_size, "%.2f", ms);
        else if(ms < 100.0)
                snprintf(out, out_size, "%.1f", ms);
        else
                snprintf(out, out_size, "%.0f", ms);
}
