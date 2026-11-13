
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#ifndef _WIN32
#include <unistd.h>
#include <signal.h>
#endif
#include <math.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>

#ifdef NO_FFMPEG
#include "common.h"
#include "recorder.h"
#include "config.h"
#include "log.h"

void recorder_init(void) { log_info("Recorder: disabled (no FFmpeg)"); }
void recorder_shutdown(void) {}
void recorder_toggle_recording(void) { chat_add(0, 0xFF4444, "Recording disabled — FFmpeg not available"); }
int  recorder_is_recording_active(void) { return 0; }
void recorder_buffer_start(void) {}
void recorder_toggle_buffer(void) { chat_add(0, 0xFF4444, "Replay buffer disabled — FFmpeg not available"); }
void recorder_buffer_stop(void) {}
int  recorder_is_buffer_active(void) { return 0; }
int  recorder_save_replay(void) { return 0; }
void recorder_trigger_replay_flash(void) {}
int  recorder_is_flashing(void) { return 0; }
void recorder_trigger_error_flash(void) {}
int  recorder_is_error_flashing(void) { return 0; }
void recorder_capture_frame(void) {}
void recorder_set_fps(int fps) { (void)fps; }
int  recorder_get_fps(void) { return 60; }
void recorder_set_bitrate(int kbps) { (void)kbps; }
int  recorder_get_bitrate(void) { return 10000; }
void audio_detect_monitor(char* buf, int len) { (void)buf; (void)len; }

#else /* NO_FFMPEG */

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
#include <libavutil/mathematics.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#ifdef USE_PULSEAUDIO
#include <pulse/simple.h>
#include <pulse/error.h>
#endif

/* FFmpeg 7.x renamed FF_PROFILE_* to AV_PROFILE_* */
#ifndef FF_PROFILE_H264_HIGH
#define FF_PROFILE_H264_HIGH AV_PROFILE_H264_HIGH
#endif

#include "common.h"
#include "recorder.h"
#include "config.h"
#include "log.h"
#include "file.h"
#include "window.h"

/* ── Raw frame type (shared by both pipelines) ─────────────────────── */
#define MAX_RAW_FRAMES 8
typedef struct {
    unsigned char* data;
    int size;
    int64_t capture_time_us;
} RawFrame;

/* ── Recording (F7) state — fully independent pipeline ─────────────── */
static int rec_running = 0;
static int rec_frames = 0;
static int64_t rec_start_time = 0;
static int rec_buf_w = 0, rec_buf_h = 0;

/* Recording-specific encoder pipeline */
static AVCodecContext* rec_venc_ctx = NULL;
static AVFrame* rec_vframe = NULL;
static struct SwsContext* rec_vsws = NULL;

/* Recording-specific raw frame queue */
static RawFrame rec_raw_frames[MAX_RAW_FRAMES];
static int rec_raw_head = 0, rec_raw_tail = 0, rec_raw_count = 0;
static pthread_mutex_t rec_raw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t rec_raw_cond = PTHREAD_COND_INITIALIZER;
static pthread_t rec_encode_thread;
static int rec_encode_thread_running = 0;

/* Growing video packet buffer for recording */
static AVPacket** rec_vbuf = NULL;
static int rec_vcount = 0;
static int rec_vcapacity = 0;
static pthread_mutex_t rec_vmutex = PTHREAD_MUTEX_INITIALIZER;

/* Growing PCM buffer for recording */
static int16_t* rec_pcm_buf = NULL;
static int rec_pcm_frames = 0;
static int rec_pcm_capacity = 0;
static int rec_pcm_truncated = 0;
/* Estimated wall-clock time of the first complete captured PCM block. */
static int64_t rec_audio_start_us = 0;
static pthread_mutex_t rec_pcm_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Replay buffer state — fully independent pipeline ───────────────── */
static int buf_running = 0;

/* Buffer-specific encoder pipeline */
static AVCodecContext* venc_ctx = NULL;
static AVFrame* vframe = NULL;
static struct SwsContext* vsws = NULL;

/* Buffer-specific raw frame queue */
static RawFrame raw_frames[MAX_RAW_FRAMES];
static int raw_head = 0, raw_tail = 0, raw_count = 0;
static pthread_mutex_t raw_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t raw_cond = PTHREAD_COND_INITIALIZER;
static pthread_t encode_thread;
static int encode_thread_running = 0;

/* Circular video packet buffer for replay */
#define MAX_PACKETS 65536
static AVPacket** vbuf = NULL;
static int vbuf_write_idx = 0;
static int vbuf_count = 0;
static double vbuf_duration = 0;

/* PCM ring buffer for audio (S16LE, 48kHz, stereo) */
static unsigned char* pcm_buf = NULL;
static int pcm_write_byte = 0;
static int pcm_bytes = 0;
static int pcm_max_bytes = 0;

/* Buffer-specific dimensions (set at start, never change) */
static int buf_w = 0, buf_h = 0;
static int64_t buffer_start_time = 0;

/* ── Shared state ──────────────────────────────────────────────────── */
static pthread_t audio_thread;
static int audio_thread_running = 0;
static pthread_mutex_t vbuf_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t pcm_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t audio_state_mutex = PTHREAD_MUTEX_INITIALIZER;
enum { AUDIO_EVENT_CAPTURE_LOST = 1, AUDIO_EVENT_PCM_FULL = 2 };
static unsigned audio_events;
/* running tracks the joinable thread; dead separately tracks unexpected exit. */
static int audio_capture_dead;
static void audio_report_event(unsigned event) {
    pthread_mutex_lock(&audio_state_mutex);
    audio_events |= event;
    if(event & AUDIO_EVENT_CAPTURE_LOST) audio_capture_dead = 1;
    pthread_mutex_unlock(&audio_state_mutex);
}
static int audio_has_failed(void) {
    pthread_mutex_lock(&audio_state_mutex);
    int dead = audio_capture_dead;
    pthread_mutex_unlock(&audio_state_mutex);
    return dead;
}
static void audio_prepare_start(void) {
    pthread_mutex_lock(&audio_state_mutex);
    audio_capture_dead = 0;
    pthread_mutex_unlock(&audio_state_mutex);
}
/* Deliver background errors only on the game thread: chat is not thread-safe. */
static void audio_notify_events(void) {
    pthread_mutex_lock(&audio_state_mutex);
    unsigned events = audio_events;
    audio_events = 0;
    pthread_mutex_unlock(&audio_state_mutex);
    if(events & AUDIO_EVENT_CAPTURE_LOST)
        chat_add(0, 0xFFFF44, "Recorder: audio capture lost; video continues without audio");
    if(events & AUDIO_EVENT_PCM_FULL)
        chat_add(0, 0xFFFF44, "Recorder: audio memory exhausted; remaining audio is missing");
}
static AVCodecParameters* buf_codecpar = NULL;
static AVRational buf_time_base;
static double buf_replay_duration;

static void audio_set_running(int running) {
    pthread_mutex_lock(&audio_state_mutex);
    audio_thread_running = running;
    pthread_mutex_unlock(&audio_state_mutex);
}
static int audio_is_running(void) {
    pthread_mutex_lock(&audio_state_mutex);
    int running = audio_thread_running;
    pthread_mutex_unlock(&audio_state_mutex);
    return running;
}
/* Running flags are also read by audio under their corresponding PCM mutex. */
static void rec_set_running(int running) {
    pthread_mutex_lock(&rec_pcm_mutex);
    rec_running = running;
    if(running) { rec_audio_start_us = 0; rec_pcm_truncated = 0; }
    pthread_mutex_unlock(&rec_pcm_mutex);
}
static void buf_set_running(int running) {
    pthread_mutex_lock(&pcm_mutex);
    buf_running = running;
    pthread_mutex_unlock(&pcm_mutex);
}

static double last_capture_time = 0;
static int target_fps = 60;
static int target_bitrate_kbps = 10000;
static char current_filename[512];
static int w = 0, h = 0;
static double replay_flash_start_time = 0;
static double replay_error_flash_start_time = 0;

/* ── Forward declarations ──────────────────────────────────────────── */
static void recorder_stop_recording(void);
static void* rec_encode_worker_thread(void* arg);
static void* encode_worker_thread(void* arg);
static void* audio_capture_thread(void* arg);

/* ── Helpers ───────────────────────────────────────────────────────── */

void audio_detect_monitor(char* buf, int len) {
    buf[0] = '\0';
#ifdef USE_PULSEAUDIO
    FILE* fp = popen("pactl get-default-sink 2>/dev/null", "r");
    if(!fp) return;
    char sink[128];
    if(fgets(sink, sizeof(sink), fp)) {
        char* nl = strchr(sink, '\n');
        if(nl) *nl = '\0';
        snprintf(buf, len, "%s.monitor", sink);
    }
    pclose(fp);
#endif
}

void recorder_trigger_replay_flash(void) {
    replay_flash_start_time = window_time();
}

int recorder_is_flashing(void) {
    if(replay_flash_start_time == 0) return 0;
    if(window_time() - replay_flash_start_time > 0.5) {
        replay_flash_start_time = 0;
        return 0;
    }
    return 1;
}

void recorder_trigger_error_flash(void) {
    replay_error_flash_start_time = window_time();
}

int recorder_is_error_flashing(void) {
    if(replay_error_flash_start_time == 0) return 0;
    if(window_time() - replay_error_flash_start_time > 0.5) {
        replay_error_flash_start_time = 0;
        return 0;
    }
    return 1;
}

void recorder_set_fps(int fps)       { target_fps = fps; }
int  recorder_get_fps(void)          { return target_fps; }
void recorder_set_bitrate(int kbps)  { target_bitrate_kbps = kbps; }
int  recorder_get_bitrate(void)      { return target_bitrate_kbps; }
int  recorder_is_recording_active(void) { return rec_running; }
int  recorder_is_buffer_active(void)    { return buf_running; }

void recorder_init(void) {
    if(!file_dir_create("videos"))
        log_warn("Recorder: failed to create videos/");
    if(!file_dir_create("videos/recordings"))
        log_warn("Recorder: failed to create videos/recordings/");
    if(!file_dir_create("videos/replays"))
        log_warn("Recorder: failed to create videos/replays/");
    audio_detect_monitor(settings.audio_monitor_source, sizeof(settings.audio_monitor_source));
    log_info("Recorder: initialized");
}

/* ── Circular buffer helpers (replay buffer only) ──────────────────── */
static int oldest_idx(int write_idx, int count) {
    return (write_idx - count + MAX_PACKETS) % MAX_PACKETS;
}

static double packet_duration_sec(AVPacket* pkt, AVRational tb) {
    return (double)pkt->duration * av_q2d(tb);
}

static void vbuf_push(AVPacket* pkt) {
    pthread_mutex_lock(&vbuf_mutex);
    int idx = vbuf_write_idx;
    if(vbuf[idx]) {
        vbuf_duration -= packet_duration_sec(vbuf[idx], venc_ctx->time_base);
        av_packet_free(&vbuf[idx]);
    }
    vbuf[idx] = pkt;
    vbuf_duration += packet_duration_sec(pkt, venc_ctx->time_base);
    vbuf_write_idx = (idx + 1) % MAX_PACKETS;
    if(vbuf_count < MAX_PACKETS) vbuf_count++;
    pthread_mutex_unlock(&vbuf_mutex);
}

static void vbuf_trim(double max_dur) {
    pthread_mutex_lock(&vbuf_mutex);
    while(vbuf_duration > max_dur && vbuf_count > 1) {
        int old = oldest_idx(vbuf_write_idx, vbuf_count);
        if(vbuf[old]) {
            vbuf_duration -= packet_duration_sec(vbuf[old], venc_ctx->time_base);
            av_packet_free(&vbuf[old]);
            vbuf[old] = NULL;
        }
        vbuf_count--;
    }
    pthread_mutex_unlock(&vbuf_mutex);
}

/* ══════════════════════════════════════════════════════════════════════
   RECORDING (F7) — fully independent pipeline
   ══════════════════════════════════════════════════════════════════════ */

static int ensure_rec_encoder(int width, int height) {
    if(rec_venc_ctx && rec_venc_ctx->width == width && rec_venc_ctx->height == height
       && rec_vframe && rec_vsws)
        return 1;

    if(rec_vsws) { sws_freeContext(rec_vsws); rec_vsws = NULL; }
    if(rec_vframe) { av_frame_free(&rec_vframe); rec_vframe = NULL; }
    if(rec_venc_ctx) { avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL; }

    const AVCodec* venc = avcodec_find_encoder_by_name("libx264");
    if(!venc) { log_error("Recorder: libx264 encoder not found for recording"); return 0; }

    rec_venc_ctx = avcodec_alloc_context3(venc);
    if(!rec_venc_ctx) return 0;

    rec_venc_ctx->width = width;
    rec_venc_ctx->height = height;
    rec_venc_ctx->time_base = (AVRational){ 1, target_fps };
    rec_venc_ctx->framerate = (AVRational){ target_fps, 1 };
    rec_venc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    rec_venc_ctx->bit_rate = target_bitrate_kbps * 1000;
    rec_venc_ctx->rc_max_rate = target_bitrate_kbps * 1000;
    rec_venc_ctx->rc_buffer_size = target_bitrate_kbps * 1000 * 2;
    rec_venc_ctx->gop_size = target_fps;
    rec_venc_ctx->max_b_frames = 0;
    rec_venc_ctx->profile = FF_PROFILE_H264_HIGH;
    rec_venc_ctx->level = 41;
    av_opt_set(rec_venc_ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(rec_venc_ctx->priv_data, "tune", "zerolatency", 0);

    if(avcodec_open2(rec_venc_ctx, venc, NULL) < 0) {
        avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL; return 0;
    }

    rec_vframe = av_frame_alloc();
    if(!rec_vframe) { avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL; return 0; }
    rec_vframe->format = AV_PIX_FMT_YUV420P;
    rec_vframe->width = width;
    rec_vframe->height = height;
    if(av_frame_get_buffer(rec_vframe, 0) < 0) {
        av_frame_free(&rec_vframe);
        avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL;
        return 0;
    }

    rec_vsws = sws_getContext(width, height, AV_PIX_FMT_RGBA,
                              width, height, AV_PIX_FMT_YUV420P,
                              SWS_BILINEAR, NULL, NULL, NULL);
    if(!rec_vsws) {
        av_frame_free(&rec_vframe); rec_vframe = NULL;
        avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL;
        return 0;
    }

    return 1;
}

/* Save-time AAC output is bounded; failures produce a partial or silent
   recording, not an out-of-bounds write or a NULL packet dereference. */
static void recorder_encode_audio(AVCodecContext* enc, AVFrame* frame, SwrContext* swr,
                                  const int16_t* pcm, int frames,
                                  AVPacket*** packets_out, int64_t** pts_out, int* count_out) {
    if(frames <= 0 || frames > INT_MAX / 2) return;
    int capacity = frames * 2;
    AVPacket** packets = calloc((size_t)capacity, sizeof(*packets));
    int64_t* pts = calloc((size_t)capacity, sizeof(*pts));
    if(!packets || !pts) {
        free(packets); free(pts);
        log_warn("Recorder: unable to allocate audio packet arrays");
        return;
    }
    int count = 0, failed = 0;
    for(int i = 0; i <= frames && !failed; i++) {
        if(i < frames) {
            const uint8_t* input[] = {(const uint8_t*)(pcm + (size_t)i * 2048)};
            if(av_frame_make_writable(frame) < 0 ||
               swr_convert(swr, frame->data, frame->nb_samples, input, 1024) != 1024) {
                log_warn("Recorder: audio conversion failed");
                break;
            }
            frame->pts = (int64_t)i * 1024;
        }
        if(avcodec_send_frame(enc, i == frames ? NULL : frame) < 0) {
            log_warn("Recorder: audio encoder rejected frame");
            break;
        }
        AVPacket* pkt = av_packet_alloc();
        if(!pkt) { log_warn("Recorder: audio packet allocation failed"); break; }
        while(avcodec_receive_packet(enc, pkt) == 0) {
            if(count == capacity) {
                log_warn("Recorder: audio packet limit reached");
                failed = 1;
                break;
            }
            if(pkt->duration == 0) pkt->duration = 1024;
            packets[count] = pkt;
            pts[count++] = pkt->pts;
            pkt = av_packet_alloc();
            if(!pkt) { log_warn("Recorder: audio packet allocation failed"); failed = 1; break; }
        }
        av_packet_free(&pkt);
    }
    *packets_out = packets;
    *pts_out = pts;
    *count_out = count;
}

/* Timestamps are measured on the same clock as captured video. Negative
   offsets are clamped because this mux path starts video at zero. */
static int64_t recorder_audio_lead_us(int64_t first_audio_us, int64_t first_video_us) {
    return first_audio_us > first_video_us ? first_audio_us - first_video_us : 0;
}

static void recorder_free_recording_resources(void) {
    free(rec_vbuf); rec_vbuf = NULL; rec_vcount = rec_vcapacity = 0;
    pthread_mutex_lock(&rec_pcm_mutex);
    free(rec_pcm_buf); rec_pcm_buf = NULL; rec_pcm_frames = rec_pcm_capacity = 0;
    pthread_mutex_unlock(&rec_pcm_mutex);
    if(rec_vsws) { sws_freeContext(rec_vsws); rec_vsws = NULL; }
    if(rec_vframe) av_frame_free(&rec_vframe);
    if(rec_venc_ctx) avcodec_free_context(&rec_venc_ctx);
}

static void recorder_free_buffer_resources(void) {
    free(vbuf); vbuf = NULL;
    pthread_mutex_lock(&pcm_mutex);
    free(pcm_buf); pcm_buf = NULL; pcm_bytes = pcm_write_byte = pcm_max_bytes = 0;
    pthread_mutex_unlock(&pcm_mutex);
    if(vsws) { sws_freeContext(vsws); vsws = NULL; }
    if(vframe) av_frame_free(&vframe);
    if(venc_ctx) avcodec_free_context(&venc_ctx);
    avcodec_parameters_free(&buf_codecpar);
}

static void recorder_start_recording(void) {
    log_info("Recorder: starting recording...");
    w = settings.window_width;
    h = settings.window_height;
    if(w <= 0 || h <= 0) {
        log_error("Recorder: invalid window dimensions for recording (%dx%d)", w, h);
        chat_add(0, 0xFF4444, "Recording failed: invalid window size");
        return;
    }

    {
        time_t t;
        time(&t);
        struct tm* tm = localtime(&t);
        char buf[64];
        strftime(buf, sizeof(buf), "KyroSpades_%Y-%m-%d_%H-%M-%S", tm);
        snprintf(current_filename, sizeof(current_filename),
                 "videos/recordings/%s.mp4", buf);
    }

    if(!ensure_rec_encoder(w, h)) {
        log_error("Recorder: failed to initialize video encoder (%dx%d)", w, h);
        chat_add(0, 0xFF4444, "Recording failed: libx264 encoder not found");
        return;
    }
    log_info("Recorder: video encoder ready (%dx%d)", w, h);

    rec_buf_w = w;
    rec_buf_h = h;

    /* Allocate growing video packet buffer */
    rec_vcapacity = 4096;
    rec_vbuf = calloc(rec_vcapacity, sizeof(AVPacket*));
    if(!rec_vbuf) {
        log_error("Recorder: failed to allocate video packet buffer");
        chat_add(0, 0xFF4444, "Recording failed: out of memory");
        recorder_free_recording_resources();
        return;
    }
    rec_vcount = 0;

    /* Allocate growing PCM buffer (start with 5 seconds) */
    rec_pcm_capacity = 5 * 48000;
    rec_pcm_buf = malloc(rec_pcm_capacity * 2 * 2);
    if(!rec_pcm_buf) {
        log_error("Recorder: failed to allocate PCM buffer");
        free(rec_vbuf); rec_vbuf = NULL;
        chat_add(0, 0xFF4444, "Recording failed: out of memory");
        recorder_free_recording_resources();
        return;
    }
    rec_pcm_frames = 0;

    rec_start_time = av_gettime();
    rec_set_running(1);
    rec_frames = 0;
    last_capture_time = 0;

    /* Start recording's own encode thread */
    rec_raw_head = rec_raw_tail = rec_raw_count = 0;
    rec_encode_thread_running = 1;
    if(pthread_create(&rec_encode_thread, NULL, rec_encode_worker_thread, NULL) != 0) {
        log_error("Recorder: failed to start recording encode thread");
        rec_encode_thread_running = 0;
        rec_set_running(0);
        chat_add(0, 0xFF4444, "Recording failed: could not start encode thread");
        recorder_free_recording_resources();
        return;
    }

    /* Start audio capture thread if not already running */
    if(!audio_thread_running) {
        audio_prepare_start();
        audio_set_running(1);
        if(pthread_create(&audio_thread, NULL, audio_capture_thread, NULL) != 0) {
            log_warn("Recorder: failed to start audio capture thread for recording");
            audio_set_running(0);
            chat_add(0, 0xFFFF44, "Audio capture unavailable");
        }
    }

    if(audio_thread_running && audio_has_failed())
        chat_add(0, 0xFFFF44, "Audio capture unavailable; stop both recorder modes to retry");
    log_info("Recorder: started recording to %s", current_filename);
    chat_add(0, 0x44FF44, "Recording started");
}

static void recorder_stop_recording(void) {
    if(!rec_running)
        return;

    audio_notify_events();
    log_info("Recorder: stopping recording, %d frames captured", rec_frames);
    rec_set_running(0);

    /* Always stop recording's own encode thread */
    if(rec_encode_thread_running) {
        pthread_mutex_lock(&rec_raw_mutex);
        rec_encode_thread_running = 0;
        pthread_cond_broadcast(&rec_raw_cond);
        pthread_mutex_unlock(&rec_raw_mutex);
        pthread_join(rec_encode_thread, NULL);
    }

    /* Always drain recording's own encoder */
    if(rec_venc_ctx) {
        avcodec_send_frame(rec_venc_ctx, NULL);
        AVPacket* pkt = av_packet_alloc();
        while(pkt && avcodec_receive_packet(rec_venc_ctx, pkt) == 0) {
            pkt->duration = 1;
            pthread_mutex_lock(&rec_vmutex);
            if(rec_vcount >= rec_vcapacity) {
                rec_vcapacity = rec_vcapacity ? rec_vcapacity * 2 : 4096;
                AVPacket** new_buf = realloc(rec_vbuf, rec_vcapacity * sizeof(AVPacket*));
                if(!new_buf) { rec_vcapacity = rec_vcount; av_packet_free(&pkt); pthread_mutex_unlock(&rec_vmutex); break; }
                rec_vbuf = new_buf;
            }
            rec_vbuf[rec_vcount++] = pkt;
            pthread_mutex_unlock(&rec_vmutex);
            pkt = av_packet_alloc();
        }
        av_packet_free(&pkt);
    }

    /* Stop audio thread only if buffer isn't running */
    if(audio_thread_running && !buf_running) {
        audio_set_running(0);
        pthread_join(audio_thread, NULL);
        audio_notify_events();
    }

    /* Snapshot video packets */
    pthread_mutex_lock(&rec_vmutex);
    int v_count = rec_vcount;
    int64_t* v_pts_arr = v_count > 0 ? malloc(v_count * sizeof(int64_t)) : NULL;
    AVPacket** v_clones = v_count > 0 ? malloc(v_count * sizeof(AVPacket*)) : NULL;
    if(v_count > 0 && (!v_pts_arr || !v_clones)) {
        for(int i = 0; i < rec_vcount; i++) av_packet_free(&rec_vbuf[i]);
        free(rec_vbuf); rec_vbuf = NULL;
        rec_vcount = 0; rec_vcapacity = 0;
        pthread_mutex_unlock(&rec_vmutex);
        free(v_pts_arr); free(v_clones);
        log_error("Recorder: malloc failed for video clones");
        recorder_free_recording_resources();
        return;
    }
    for(int i = 0; i < v_count; i++) {
        v_pts_arr[i] = rec_vbuf[i]->pts;
        v_clones[i] = av_packet_clone(rec_vbuf[i]);
        if(!v_clones[i]) {
            for(int j = 0; j < i; j++) av_packet_free(&v_clones[j]);
            free(v_clones); free(v_pts_arr);
            for(int j = 0; j < rec_vcount; j++) av_packet_free(&rec_vbuf[j]);
            pthread_mutex_unlock(&rec_vmutex);
            log_error("Recorder: could not clone recording video packet");
            recorder_free_recording_resources();
            return;
        }
    }
    /* Free recording video buffer */
    for(int i = 0; i < rec_vcount; i++) av_packet_free(&rec_vbuf[i]);
    free(rec_vbuf); rec_vbuf = NULL;
    rec_vcount = 0; rec_vcapacity = 0;
    pthread_mutex_unlock(&rec_vmutex);

    /* Snapshot PCM buffer */
    int16_t* pcm_copy = NULL;
    int pcm_num_frames = 0;
    pthread_mutex_lock(&rec_pcm_mutex);
    int64_t pcm_start_us = rec_audio_start_us;
    if(rec_pcm_frames > 0 && rec_pcm_buf) {
        pcm_copy = malloc((size_t)rec_pcm_frames * 2 * sizeof(*rec_pcm_buf));
        if(pcm_copy) {
            memcpy(pcm_copy, rec_pcm_buf, (size_t)rec_pcm_frames * 2 * sizeof(*rec_pcm_buf));
            pcm_num_frames = rec_pcm_frames / 1024;
        }
    }
    free(rec_pcm_buf); rec_pcm_buf = NULL;
    rec_pcm_frames = 0; rec_pcm_capacity = 0;
    pthread_mutex_unlock(&rec_pcm_mutex);

    if(v_count == 0) {
        log_warn("Recorder: no video frames captured");
        free(v_pts_arr); free(v_clones); free(pcm_copy);
        goto rec_finalize;
    }

    /* ── Encode audio from PCM ─────────────────────────────────────── */
    int a_count = 0;
    AVPacket** a_packets = NULL;
    int64_t* a_pts_arr = NULL;
    AVCodecContext* save_aenc = NULL;
    AVFrame* save_aframe = NULL;
    SwrContext* save_aswr = NULL;

    if(pcm_num_frames > 0 && pcm_copy) {
        const AVCodec* aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if(aenc) {
            save_aenc = avcodec_alloc_context3(aenc);
            if(save_aenc) {
                save_aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
                save_aenc->sample_rate = 48000;
                save_aenc->time_base = (AVRational){1, 48000};
                save_aenc->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                save_aenc->bit_rate = 192000;
                if(avcodec_open2(save_aenc, aenc, NULL) < 0) {
                    avcodec_free_context(&save_aenc); save_aenc = NULL;
                }
            }
        }
        if(save_aenc) {
            save_aframe = av_frame_alloc();
            if(save_aframe) {
                save_aframe->format = AV_SAMPLE_FMT_FLTP;
                save_aframe->sample_rate = 48000;
                save_aframe->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                save_aframe->nb_samples = 1024;
                if(av_frame_get_buffer(save_aframe, 0) < 0) {
                    av_frame_free(&save_aframe); save_aframe = NULL;
                }
            }
            swr_alloc_set_opts2(&save_aswr,
                    &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 48000,
                    &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, AV_SAMPLE_FMT_S16,  48000,
                    0, NULL);
            if(save_aswr && swr_init(save_aswr) < 0) {
                log_warn("Recorder: audio resampler initialization failed");
                swr_free(&save_aswr);
            }
        }
        if(save_aenc && save_aframe && save_aswr)
            recorder_encode_audio(save_aenc, save_aframe, save_aswr, pcm_copy,
                                  pcm_num_frames, &a_packets, &a_pts_arr, &a_count);
    }

    /* ── Mux video + audio into final MP4 ──────────────────────────── */
    {
        AVFormatContext* oc = NULL;
        avformat_alloc_output_context2(&oc, NULL, "mp4", current_filename);
        if(!oc) {
            log_error("Recorder: failed to create muxer for recording");
            goto rec_cleanup;
        }

        AVStream* vs = avformat_new_stream(oc, NULL);
        if(!vs) goto rec_cleanup;
        avcodec_parameters_from_context(vs->codecpar, rec_venc_ctx);
        vs->time_base = rec_venc_ctx->time_base;

        int mux_a_count = a_count;
        AVStream* as = NULL;
        if(a_count > 0 && save_aenc) {
            as = avformat_new_stream(oc, NULL);
            if(as) {
                avcodec_parameters_from_context(as->codecpar, save_aenc);
                as->time_base = save_aenc->time_base;
            } else {
                log_warn("Recorder: could not create audio stream; saving video only");
                mux_a_count = 0;
            }
        }

        if(avio_open(&oc->pb, current_filename, AVIO_FLAG_WRITE) < 0) {
            log_error("Recorder: failed to open output file");
            goto rec_cleanup;
        }
        if(avformat_write_header(oc, NULL) < 0) {
            log_error("Recorder: avformat_write_header failed");
            goto rec_cleanup;
        }

        /* Interleave video + audio by PTS */
        int v_written = 0, a_written = 0;
        int64_t v_pts_offset = v_clones[0]->pts;
        int64_t a_pts_offset = mux_a_count > 0 ? a_pts_arr[0] : 0;
        /* PCM starts when the first capture read completes, which may be
           seconds after video starts if the audio device opens slowly. */
        int64_t video_start_us = rec_start_time + av_rescale_q(v_pts_offset, rec_venc_ctx->time_base, AV_TIME_BASE_Q);
        int64_t audio_lead_us = recorder_audio_lead_us(pcm_start_us, video_start_us);
        int64_t audio_lead = mux_a_count ? av_rescale_q(audio_lead_us, AV_TIME_BASE_Q, save_aenc->time_base) : 0;
        log_info("Recorder: PCM start offset %.3fs, captured audio %.3fs", audio_lead_us / 1000000.0, pcm_num_frames * (1024.0 / 48000.0));

        while(v_written < v_count || a_written < mux_a_count) {
            int use_video = 0;
            if(v_written >= v_count) use_video = 0;
            else if(a_written >= mux_a_count) use_video = 1;
            else {
                int64_t v_abs = av_rescale_q_rnd(
                        v_pts_arr[v_written] - v_pts_offset,
                        rec_venc_ctx->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF);
                int64_t a_abs = av_rescale_q_rnd(
                        a_pts_arr[a_written] - a_pts_offset + audio_lead,
                        save_aenc->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF);
                use_video = (v_abs <= a_abs);
            }

            if(use_video) {
                AVPacket out = {0};
                av_packet_ref(&out, v_clones[v_written]);
                out.pts = av_rescale_q_rnd(
                        v_pts_arr[v_written] - v_pts_offset,
                        rec_venc_ctx->time_base, vs->time_base, AV_ROUND_NEAR_INF);
                out.dts = out.pts;
                out.duration = av_rescale_q(
                        v_clones[v_written]->duration, rec_venc_ctx->time_base, vs->time_base);
                out.stream_index = 0;
                if(av_interleaved_write_frame(oc, &out) < 0)
                    log_error("Recorder: av_interleaved_write_frame failed for video");
                v_written++;
            } else {
                AVPacket out = {0};
                av_packet_ref(&out, a_packets[a_written]);
                out.pts = av_rescale_q(a_pts_arr[a_written] - a_pts_offset + audio_lead, save_aenc->time_base, as->time_base);
                out.dts = out.pts;
                out.duration = av_rescale_q(a_packets[a_written]->duration, save_aenc->time_base, as->time_base);
                out.stream_index = 1;
                if(av_interleaved_write_frame(oc, &out) < 0)
                    log_error("Recorder: av_interleaved_write_frame failed for audio");
                a_written++;
            }
        }

        av_write_trailer(oc);
        avio_closep(&oc->pb);
        avformat_free_context(oc);
        oc = NULL;

        log_info("Recorder: saved recording as %s (%d video, %d audio packets)",
                 current_filename, v_count, a_count);

    rec_cleanup:
        if(oc) { avio_closep(&oc->pb); avformat_free_context(oc); }
    }

    free(v_pts_arr); free(a_pts_arr);
    for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
    free(v_clones);
    free(pcm_copy);
    for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
    free(a_packets);
    if(save_aswr) swr_free(&save_aswr);
    if(save_aframe) av_frame_free(&save_aframe);
    if(save_aenc) avcodec_free_context(&save_aenc);

rec_finalize:
    /* Always free recording's own encoder resources */
    if(rec_vsws) { sws_freeContext(rec_vsws); rec_vsws = NULL; }
    if(rec_vframe) { av_frame_free(&rec_vframe); rec_vframe = NULL; }
    if(rec_venc_ctx) { avcodec_free_context(&rec_venc_ctx); rec_venc_ctx = NULL; }
}

void recorder_toggle_recording(void) {
    if(rec_running)
        recorder_stop_recording();
    else
        recorder_start_recording();
}

/* ── Recording encode worker thread (recording only) ───────────────── */

static void* rec_encode_worker_thread(void* arg) {
    (void)arg;
    log_info("Recorder: recording encode thread started");

    while(1) {
        pthread_mutex_lock(&rec_raw_mutex);
        while(rec_raw_count == 0 && rec_encode_thread_running) {
            pthread_cond_wait(&rec_raw_cond, &rec_raw_mutex);
        }
        if(rec_raw_count == 0) {
            pthread_mutex_unlock(&rec_raw_mutex);
            break;
        }
        RawFrame rf = rec_raw_frames[rec_raw_head];
        rec_raw_head = (rec_raw_head + 1) % MAX_RAW_FRAMES;
        rec_raw_count--;
        pthread_mutex_unlock(&rec_raw_mutex);

        int cur_w = rec_buf_w, cur_h = rec_buf_h;
        unsigned char* pixels = rf.data;

        if(rec_venc_ctx && rec_vframe && rec_vsws) {
            uint8_t* src_data[] = { pixels + (cur_h - 1) * cur_w * 4 };
            int src_stride[] = { -cur_w * 4 };
            sws_scale(rec_vsws, (const uint8_t* const*)src_data, src_stride, 0, cur_h,
                      rec_vframe->data, rec_vframe->linesize);

            rec_vframe->pts = av_rescale_q(rf.capture_time_us - rec_start_time,
                                          (AVRational){1, 1000000},
                                          rec_venc_ctx->time_base);
            int ret = avcodec_send_frame(rec_venc_ctx, rec_vframe);
            if(ret < 0) {
                log_error("Recorder: recording video encode send failed");
            } else {
                AVPacket* pkt = av_packet_alloc();
                while(pkt && avcodec_receive_packet(rec_venc_ctx, pkt) == 0) {
                    pkt->duration = 1;
                    pthread_mutex_lock(&rec_vmutex);
                    if(rec_vcount >= rec_vcapacity) {
                        rec_vcapacity = rec_vcapacity ? rec_vcapacity * 2 : 4096;
                        AVPacket** new_buf = realloc(rec_vbuf, rec_vcapacity * sizeof(AVPacket*));
                        if(!new_buf) {
                            rec_vcapacity = rec_vcount;
                            av_packet_free(&pkt);
                            pthread_mutex_unlock(&rec_vmutex);
                        } else {
                            rec_vbuf = new_buf;
                            rec_vbuf[rec_vcount++] = pkt;
                            pthread_mutex_unlock(&rec_vmutex);
                        }
                    } else {
                        rec_vbuf[rec_vcount++] = pkt;
                        pthread_mutex_unlock(&rec_vmutex);
                    }
                    pkt = av_packet_alloc();
                }
                av_packet_free(&pkt);
            }
        }

        free(pixels);
    }

    log_info("Recorder: recording encode thread stopped");
    return NULL;
}

/* ── Audio capture thread (shared — writes to active pipeline(s)) ──── */

static void* audio_capture_thread(void* arg) {
    (void)arg;
#ifdef USE_PULSEAUDIO
    const pa_sample_spec ss = {
        .format = PA_SAMPLE_S16LE,
        .rate = 48000,
        .channels = 2
    };
    int error;
    pa_simple* s = pa_simple_new(NULL, "KyroSpades", PA_STREAM_RECORD,
                                 settings.audio_monitor_source[0] ? settings.audio_monitor_source : NULL,
                                 "replay-buffer", &ss, NULL, NULL, &error);
    if(!s) {
        log_warn("Recorder: audio capture unavailable: %s", pa_strerror(error));
        audio_report_event(AUDIO_EVENT_CAPTURE_LOST);
        return NULL;
    }

    unsigned char read_buf[1024 * 2 * 2];
    const int frame_bytes = sizeof(read_buf);

    while(audio_is_running()) {
        if(pa_simple_read(s, read_buf, frame_bytes, &error) < 0) {
            if(audio_is_running()) {
                log_warn("Recorder: audio capture read failed: %s", pa_strerror(error));
                audio_report_event(AUDIO_EVENT_CAPTURE_LOST);
            }
            break;
        }
        /* pa_simple does not supply per-sample timestamps. Approximate the
           first sample as one block before read completion; server buffering
           can make this later than the actual capture time. */
        int64_t chunk_start_us = av_gettime() - (int64_t)1024 * 1000000 / 48000;

        /* Write to PCM ring buffer for replay (fast, never blocks) */
        pthread_mutex_lock(&pcm_mutex);
        if(buf_running && pcm_buf && pcm_max_bytes >= frame_bytes) {
            int remaining = pcm_max_bytes - pcm_write_byte;
            if(frame_bytes <= remaining) {
                memcpy(pcm_buf + pcm_write_byte, read_buf, frame_bytes);
            } else {
                memcpy(pcm_buf + pcm_write_byte, read_buf, remaining);
                memcpy(pcm_buf, read_buf + remaining, frame_bytes - remaining);
            }
            pcm_write_byte = (pcm_write_byte + frame_bytes) % pcm_max_bytes;
            if(pcm_bytes <= pcm_max_bytes - frame_bytes)
                pcm_bytes += frame_bytes;
            else
                pcm_bytes = pcm_max_bytes;
        }
        pthread_mutex_unlock(&pcm_mutex);

        /* Write raw PCM to growing buffer for F7 recording */
        pthread_mutex_lock(&rec_pcm_mutex);
        if(rec_running && rec_pcm_buf && !rec_pcm_truncated) {
            if(!rec_audio_start_us)
                rec_audio_start_us = chunk_start_us < rec_start_time ? rec_start_time : chunk_start_us;
            int needed = rec_pcm_frames <= INT_MAX - 1024 ? rec_pcm_frames + 1024 : INT_MAX;
            if(needed > rec_pcm_capacity && needed <= INT_MAX - 5 * 48000) {
                int capacity = needed + 5 * 48000;
                void* next = (size_t)capacity <= SIZE_MAX / (2 * sizeof(*rec_pcm_buf))
                    ? realloc(rec_pcm_buf, (size_t)capacity * 2 * sizeof(*rec_pcm_buf)) : NULL;
                if(next) { rec_pcm_buf = next; rec_pcm_capacity = capacity; }
            }
            if(needed <= rec_pcm_capacity) {
                memcpy(rec_pcm_buf + (size_t)rec_pcm_frames * 2, read_buf, frame_bytes);
                rec_pcm_frames += 1024;
            } else {
                rec_pcm_truncated = 1;
                log_warn("Recorder: PCM allocation failed at %d frames; recording audio truncated", rec_pcm_frames);
                audio_report_event(AUDIO_EVENT_PCM_FULL);
            }
        }
        pthread_mutex_unlock(&rec_pcm_mutex);
    }

    pa_simple_free(s);
#else
    log_warn("Recorder: audio capture unavailable (PulseAudio support not built)");
    audio_report_event(AUDIO_EVENT_CAPTURE_LOST);
#endif
    return NULL;
}

/* ══════════════════════════════════════════════════════════════════════
   REPLAY BUFFER (F8) — fully independent pipeline
   ══════════════════════════════════════════════════════════════════════ */

/* ── Buffer encode worker thread (buffer only) ─────────────────────── */

static void* encode_worker_thread(void* arg) {
    (void)arg;
    log_info("Recorder: buffer encode thread started");

    while(1) {
        pthread_mutex_lock(&raw_mutex);
        while(raw_count == 0 && encode_thread_running) {
            pthread_cond_wait(&raw_cond, &raw_mutex);
        }
        if(raw_count == 0) {
            pthread_mutex_unlock(&raw_mutex);
            break;
        }
        RawFrame rf = raw_frames[raw_head];
        raw_head = (raw_head + 1) % MAX_RAW_FRAMES;
        raw_count--;
        pthread_mutex_unlock(&raw_mutex);

        int cur_w = buf_w, cur_h = buf_h;
        unsigned char* pixels = rf.data;

        if(venc_ctx && vframe && vsws) {
            uint8_t* src_data[] = { pixels + (cur_h - 1) * cur_w * 4 };
            int src_stride[] = { -cur_w * 4 };
            sws_scale(vsws, (const uint8_t* const*)src_data, src_stride, 0, cur_h,
                      vframe->data, vframe->linesize);

            vframe->pts = av_rescale_q(rf.capture_time_us - buffer_start_time,
                                      (AVRational){1, 1000000},
                                      venc_ctx->time_base);
            int ret = avcodec_send_frame(venc_ctx, vframe);
            if(ret < 0) {
                log_error("Recorder: buffer video encode send failed");
            } else {
                AVPacket* pkt = av_packet_alloc();
                while(pkt && avcodec_receive_packet(venc_ctx, pkt) == 0) {
                    pkt->duration = 1;
                    vbuf_push(pkt);
                    pkt = av_packet_alloc();
                }
                av_packet_free(&pkt);
            }

            vbuf_trim(buf_replay_duration);
        }

        free(pixels);
    }

    log_info("Recorder: buffer encode thread stopped");
    return NULL;
}

/* ── Replay buffer start / stop ────────────────────────────────────── */

void recorder_buffer_start(void) {
    log_info("Recorder: starting replay buffer...");
    if(buf_running) {
        log_warn("Recorder: buffer already running, ignoring start request");
        return;
    }
    w = settings.window_width;
    h = settings.window_height;

    if(w <= 0 || h <= 0) {
        log_error("Recorder: invalid window dimensions for buffer (%dx%d)", w, h);
        chat_add(0, 0xFF4444, "Replay buffer failed: invalid window size");
        return;
    }
    buf_w = w;
    buf_h = h;

    /* Always allocate a fresh encoder for the buffer */
    if(venc_ctx) {
        if(vsws) { sws_freeContext(vsws); vsws = NULL; }
        if(vframe) { av_frame_free(&vframe); vframe = NULL; }
        avcodec_free_context(&venc_ctx); venc_ctx = NULL;
    }

    const AVCodec* venc = avcodec_find_encoder_by_name("libx264");
    if(!venc) { log_error("Recorder: libx264 encoder not found"); chat_add(0, 0xFF4444, "Replay buffer failed: libx264 not found"); return; }

    venc_ctx = avcodec_alloc_context3(venc);
    if(!venc_ctx) { log_error("Recorder: failed to alloc video codec context"); return; }

    venc_ctx->width = w;
    venc_ctx->height = h;
    venc_ctx->time_base = (AVRational){ 1, target_fps };
    venc_ctx->framerate = (AVRational){ target_fps, 1 };
    venc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    venc_ctx->bit_rate = target_bitrate_kbps * 1000;
    venc_ctx->rc_max_rate = target_bitrate_kbps * 1000;
    venc_ctx->rc_buffer_size = target_bitrate_kbps * 1000 * 2;
    venc_ctx->gop_size = target_fps;
    venc_ctx->max_b_frames = 0;
    venc_ctx->profile = FF_PROFILE_H264_HIGH;
    venc_ctx->level = 41;
    av_opt_set(venc_ctx->priv_data, "preset", "veryfast", 0);
    av_opt_set(venc_ctx->priv_data, "tune", "zerolatency", 0);

    if(avcodec_open2(venc_ctx, venc, NULL) < 0) {
        log_error("Recorder: failed to open video encoder (%dx%d)", w, h);
        avcodec_free_context(&venc_ctx);
        return;
    }

    buf_codecpar = avcodec_parameters_alloc();
    if(!buf_codecpar || avcodec_parameters_from_context(buf_codecpar, venc_ctx) < 0) {
        log_error("Recorder: could not snapshot replay encoder parameters");
        recorder_free_buffer_resources();
        return;
    }
    buf_time_base = venc_ctx->time_base;
    buf_replay_duration = settings.replay_duration;

    vframe = av_frame_alloc();
    if(!vframe) { log_error("Recorder: failed to alloc video frame"); avcodec_free_context(&venc_ctx); venc_ctx = NULL; avcodec_parameters_free(&buf_codecpar); return; }
    vframe->format = AV_PIX_FMT_YUV420P;
    vframe->width = w;
    vframe->height = h;
    if(av_frame_get_buffer(vframe, 0) < 0) {
        log_error("Recorder: failed to alloc video frame buffer");
        av_frame_free(&vframe); vframe = NULL;
        avcodec_free_context(&venc_ctx); venc_ctx = NULL;
        avcodec_parameters_free(&buf_codecpar);
        return;
    }

    vsws = sws_getContext(w, h, AV_PIX_FMT_RGBA, w, h, AV_PIX_FMT_YUV420P,
                          SWS_BILINEAR, NULL, NULL, NULL);
    if(!vsws) {
        log_error("Recorder: failed to alloc SWScale");
        av_frame_free(&vframe); vframe = NULL;
        avcodec_free_context(&venc_ctx); venc_ctx = NULL;
        avcodec_parameters_free(&buf_codecpar);
        return;
    }
    log_info("Recorder: video encoder ready for buffer (%dx%d)", w, h);

    /* Allocate circular buffers */
    vbuf = calloc(MAX_PACKETS, sizeof(AVPacket*));
    if(!vbuf) {
        log_error("Recorder: failed to allocate circular buffer");
        sws_freeContext(vsws); vsws = NULL;
        av_frame_free(&vframe); vframe = NULL;
        avcodec_free_context(&venc_ctx); venc_ctx = NULL;
        avcodec_parameters_free(&buf_codecpar);
        return;
    }

    /* Allocate PCM ring buffer (S16LE, 48kHz, stereo) */
    if(settings.replay_duration < 0 || settings.replay_duration > INT_MAX / 192000 - 5) {
        log_error("Recorder: invalid replay duration");
        recorder_free_buffer_resources();
        return;
    }
    pcm_max_bytes = (settings.replay_duration + 5) * 48000 * 2 * 2;
    pcm_buf = calloc(pcm_max_bytes, 1);
    if(!pcm_buf) {
        log_error("Recorder: failed to allocate PCM ring buffer (%d bytes)", pcm_max_bytes);
        free(vbuf); vbuf = NULL;
        sws_freeContext(vsws); vsws = NULL;
        av_frame_free(&vframe); vframe = NULL;
        avcodec_free_context(&venc_ctx); venc_ctx = NULL;
        avcodec_parameters_free(&buf_codecpar);
        return;
    }

    vbuf_write_idx = 0;
    vbuf_count = 0;
    vbuf_duration = 0;
    pcm_write_byte = 0;
    pcm_bytes = 0;

    buffer_start_time = av_gettime();

    /* Start audio capture thread if not already running */
    if(!audio_thread_running) {
        audio_prepare_start();
        audio_set_running(1);
        if(pthread_create(&audio_thread, NULL, audio_capture_thread, NULL) != 0) {
            log_warn("Recorder: failed to start audio capture thread — video-only buffer");
            audio_set_running(0);
        }
    }

    /* Start buffer's own encode thread */
    raw_head = raw_tail = raw_count = 0;
    encode_thread_running = 1;
    if(pthread_create(&encode_thread, NULL, encode_worker_thread, NULL) != 0) {
        log_error("Recorder: failed to start buffer encode worker thread");
        encode_thread_running = 0;
        if(audio_thread_running && !rec_running) {
            audio_set_running(0);
            pthread_join(audio_thread, NULL);
        }
        recorder_free_buffer_resources();
        chat_add(0, 0xFF4444, "Replay buffer failed: could not start encode thread");
        return;
    }

    buf_set_running(1);
    last_capture_time = 0;
    if(audio_thread_running && audio_has_failed())
        chat_add(0, 0xFFFF44, "Audio capture unavailable; stop both recorder modes to retry");
    log_info("Recorder: replay buffer started");
}

void recorder_buffer_stop(void) {
    if(!buf_running) return;
    buf_set_running(0);

    log_info("Recorder: stopping replay buffer");

    /* Always stop buffer's encode thread */
    if(encode_thread_running) {
        pthread_mutex_lock(&raw_mutex);
        encode_thread_running = 0;
        pthread_cond_broadcast(&raw_cond);
        pthread_mutex_unlock(&raw_mutex);
        pthread_join(encode_thread, NULL);
    }

    /* Stop audio thread only if recording is not still active */
    if(audio_thread_running && !rec_running) {
        audio_set_running(0);
        pthread_join(audio_thread, NULL);
        audio_notify_events();
    }

    /* Drain video encoder */
    if(venc_ctx) {
        avcodec_send_frame(venc_ctx, NULL);
        AVPacket* pkt = av_packet_alloc();
        while(pkt && avcodec_receive_packet(venc_ctx, pkt) == 0) {
            pkt->duration = 1;
            vbuf_push(pkt);
            pkt = av_packet_alloc();
        }
        av_packet_free(&pkt);
    }

    /* Free circular buffers */
    pthread_mutex_lock(&vbuf_mutex);
    for(int i = 0; i < vbuf_count; i++) {
        int idx = (oldest_idx(vbuf_write_idx, vbuf_count) + i) % MAX_PACKETS;
        if(vbuf[idx]) {
            av_packet_free(&vbuf[idx]);
            vbuf[idx] = NULL;
        }
    }
    free(vbuf); vbuf = NULL;
    vbuf_count = 0;
    vbuf_duration = 0;
    pthread_mutex_unlock(&vbuf_mutex);

    pthread_mutex_lock(&pcm_mutex);
    free(pcm_buf); pcm_buf = NULL;
    pcm_bytes = 0;
    pcm_write_byte = 0;
    pthread_mutex_unlock(&pcm_mutex);

    /* Always free buffer's encoder resources */
    if(vsws) { sws_freeContext(vsws); vsws = NULL; }
    if(vframe) { av_frame_free(&vframe); vframe = NULL; }
    if(venc_ctx) { avcodec_free_context(&venc_ctx); venc_ctx = NULL; }
    raw_head = raw_tail = raw_count = 0;
    avcodec_parameters_free(&buf_codecpar);

    log_info("Recorder: replay buffer stopped");
}

void recorder_toggle_buffer(void) {
    if(buf_running)
        recorder_buffer_stop();
    else
        recorder_buffer_start();
}

/* ── Replay save (F8) — mux from RAM buffers ────────────────────────── */

int recorder_save_replay(void) {
    if(!buf_running || !buf_codecpar) {
        log_warn("Recorder: no replay buffer data to save");
        return 0;
    }

    /* Snapshot video buffer */
    pthread_mutex_lock(&vbuf_mutex);
    int v_count = vbuf_count;
    int v_write = vbuf_write_idx;

    int64_t* v_pts_arr = v_count > 0 ? malloc(v_count * sizeof(int64_t)) : NULL;
    AVPacket** v_clones = v_count > 0 ? malloc(v_count * sizeof(AVPacket*)) : NULL;
    if(v_count > 0 && (!v_pts_arr || !v_clones)) {
        pthread_mutex_unlock(&vbuf_mutex);
        free(v_pts_arr); free(v_clones);
        log_error("Recorder: malloc failed for video clones");
        return 0;
    }
    for(int i = 0; i < v_count; i++) {
        int idx = (oldest_idx(v_write, v_count) + i) % MAX_PACKETS;
        v_pts_arr[i] = vbuf[idx]->pts;
        v_clones[i] = av_packet_clone(vbuf[idx]);
        if(!v_clones[i]) {
            log_error("Recorder: av_packet_clone failed for video packet %d", i);
            pthread_mutex_unlock(&vbuf_mutex);
            for(int j = 0; j < i; j++) av_packet_free(&v_clones[j]);
            free(v_clones); free(v_pts_arr);
            return 0;
        }
    }
    pthread_mutex_unlock(&vbuf_mutex);

    if(v_count == 0) {
        log_warn("Recorder: no video data in replay buffer");
        free(v_pts_arr);
        for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
        free(v_clones);
        return 0;
    }

    /* Snapshot PCM buffer */
    int16_t* pcm_copy = NULL;
    int pcm_num_frames = 0;
    pthread_mutex_lock(&pcm_mutex);
    if(pcm_bytes > 0) {
        int oldest_byte = (int)(((int64_t)pcm_write_byte - pcm_bytes + pcm_max_bytes) % pcm_max_bytes);
        pcm_copy = malloc(pcm_bytes);
        if(pcm_copy) {
            int first_part = pcm_max_bytes - oldest_byte;
            if(first_part > pcm_bytes) first_part = pcm_bytes;
            memcpy(pcm_copy, pcm_buf + oldest_byte, first_part);
            if(first_part < pcm_bytes)
                memcpy((unsigned char*)pcm_copy + first_part, pcm_buf, pcm_bytes - first_part);
            pcm_num_frames = pcm_bytes / (1024 * 2 * 2);
        }
    }
    pthread_mutex_unlock(&pcm_mutex);

    /* On-save AAC encode from PCM */
    int a_count = 0;
    AVPacket** a_packets = NULL;
    int64_t* a_pts_arr = NULL;
    AVCodecContext* save_aenc = NULL;
    AVFrame* save_aframe = NULL;
    SwrContext* save_aswr = NULL;

    if(pcm_num_frames > 0 && pcm_copy) {
        const AVCodec* aenc = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if(aenc) {
            save_aenc = avcodec_alloc_context3(aenc);
            if(save_aenc) {
                save_aenc->sample_fmt = AV_SAMPLE_FMT_FLTP;
                save_aenc->sample_rate = 48000;
                save_aenc->time_base = (AVRational){1, 48000};
                save_aenc->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                save_aenc->bit_rate = 192000;
                if(avcodec_open2(save_aenc, aenc, NULL) < 0) {
                    avcodec_free_context(&save_aenc);
                    save_aenc = NULL;
                }
            }
        }

        if(save_aenc) {
            save_aframe = av_frame_alloc();
            if(save_aframe) {
                save_aframe->format = AV_SAMPLE_FMT_FLTP;
                save_aframe->sample_rate = 48000;
                save_aframe->ch_layout = (AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO;
                save_aframe->nb_samples = 1024;
                if(av_frame_get_buffer(save_aframe, 0) < 0) {
                    av_frame_free(&save_aframe);
                    save_aframe = NULL;
                }
            }
            swr_alloc_set_opts2(&save_aswr,
                    &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, AV_SAMPLE_FMT_FLTP, 48000,
                    &(AVChannelLayout)AV_CHANNEL_LAYOUT_STEREO, AV_SAMPLE_FMT_S16,  48000,
                    0, NULL);
            if(save_aswr && swr_init(save_aswr) < 0) {
                log_warn("Recorder: audio resampler initialization failed");
                swr_free(&save_aswr);
            }
        }

        if(save_aenc && save_aframe && save_aswr)
            recorder_encode_audio(save_aenc, save_aframe, save_aswr, pcm_copy,
                                  pcm_num_frames, &a_packets, &a_pts_arr, &a_count);
    }

    /* Generate filename */
    time_t t;
    time(&t);
    struct tm* tm = localtime(&t);
    char buf[64];
    strftime(buf, sizeof(buf), "Replay_%Y-%m-%d_%H-%M-%S.mp4", tm);
    char replay_filename[512];
    snprintf(replay_filename, sizeof(replay_filename), "videos/replays/%s", buf);

    /* Create muxer */
    AVFormatContext* oc = NULL;
    avformat_alloc_output_context2(&oc, NULL, "mp4", replay_filename);
    if(!oc) {
        log_error("Recorder: failed to create muxer for replay");
        free(v_pts_arr);
        for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
        free(v_clones);
        free(pcm_copy);
        free(a_pts_arr);
        for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
        free(a_packets);
        if(save_aswr) swr_free(&save_aswr);
        if(save_aframe) av_frame_free(&save_aframe);
        if(save_aenc) avcodec_free_context(&save_aenc);
        return 0;
    }

    /* Add video stream */
    AVStream* vs = avformat_new_stream(oc, NULL);
    if(!vs || avcodec_parameters_copy(vs->codecpar, buf_codecpar) < 0) {
        log_error("Recorder: could not create replay video stream");
        avformat_free_context(oc);
        free(v_pts_arr);
        for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
        free(v_clones);
        free(pcm_copy);
        free(a_pts_arr);
        for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
        free(a_packets);
        if(save_aswr) swr_free(&save_aswr);
        if(save_aframe) av_frame_free(&save_aframe);
        if(save_aenc) avcodec_free_context(&save_aenc);
        return 0;
    }
    vs->time_base = buf_time_base;

    /* Add audio stream if available */
    int mux_a_count = a_count;
    AVStream* as = NULL;
    if(a_count > 0 && save_aenc) {
        as = avformat_new_stream(oc, NULL);
        if(as) {
            avcodec_parameters_from_context(as->codecpar, save_aenc);
            as->time_base = save_aenc->time_base;
        } else {
            log_warn("Recorder: could not create replay audio stream; saving video only");
            mux_a_count = 0;
        }
    }

    /* Open output */
    if(avio_open(&oc->pb, replay_filename, AVIO_FLAG_WRITE) < 0) {
        log_error("Recorder: failed to open output file for replay");
        avformat_free_context(oc);
        free(v_pts_arr);
        for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
        free(v_clones);
        free(pcm_copy);
        free(a_pts_arr);
        for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
        free(a_packets);
        if(save_aswr) swr_free(&save_aswr);
        if(save_aframe) av_frame_free(&save_aframe);
        if(save_aenc) avcodec_free_context(&save_aenc);
        return 0;
    }
    if(avformat_write_header(oc, NULL) < 0) {
        log_error("Recorder: avformat_write_header failed for replay");
        avio_closep(&oc->pb);
        avformat_free_context(oc);
        free(v_pts_arr);
        for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
        free(v_clones);
        free(pcm_copy);
        free(a_pts_arr);
        for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
        free(a_packets);
        if(save_aswr) swr_free(&save_aswr);
        if(save_aframe) av_frame_free(&save_aframe);
        if(save_aenc) avcodec_free_context(&save_aenc);
        return 0;
    }

    /* Interleave video + audio by PTS */
    int v_written = 0, a_written = 0;
    int64_t v_pts_offset = v_clones[0]->pts;
    int64_t a_pts_offset = mux_a_count > 0 ? a_pts_arr[0] : 0;

    while(v_written < v_count || a_written < mux_a_count) {
        int use_video = 0;
        if(v_written >= v_count) use_video = 0;
        else if(a_written >= mux_a_count) use_video = 1;
        else {
            int64_t v_abs = av_rescale_q_rnd(
                    v_pts_arr[v_written] - v_pts_offset,
                    buf_time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF);
            int64_t a_abs = av_rescale_q_rnd(
                    a_pts_arr[a_written] - a_pts_offset,
                    save_aenc->time_base, AV_TIME_BASE_Q, AV_ROUND_NEAR_INF);
            use_video = (v_abs <= a_abs);
        }

        if(use_video) {
            AVPacket out = {0};
            av_packet_ref(&out, v_clones[v_written]);
            out.pts = av_rescale_q_rnd(
                    v_pts_arr[v_written] - v_pts_offset,
                    buf_time_base, vs->time_base, AV_ROUND_NEAR_INF);
            out.dts = out.pts;
            out.duration = av_rescale_q(
                    v_clones[v_written]->duration, buf_time_base, vs->time_base);
            out.stream_index = 0;
            int ret = av_interleaved_write_frame(oc, &out);
            if(ret < 0) {
                log_error("Recorder: av_interleaved_write_frame failed for video (ret=%d)", ret);
            }
            v_written++;
        } else {
            AVPacket out = {0};
            av_packet_ref(&out, a_packets[a_written]);
            out.pts = av_rescale_q(a_pts_arr[a_written] - a_pts_offset, save_aenc->time_base, as->time_base);
            out.dts = out.pts;
            out.duration = av_rescale_q(a_packets[a_written]->duration, save_aenc->time_base, as->time_base);
            out.stream_index = 1;
            int ret = av_interleaved_write_frame(oc, &out);
            if(ret < 0) {
                log_error("Recorder: av_interleaved_write_frame failed for audio (ret=%d)", ret);
            }
            a_written++;
        }
    }

    free(v_pts_arr);
    free(a_pts_arr);

    for(int i = 0; i < v_count; i++) av_packet_free(&v_clones[i]);
    free(v_clones);
    free(pcm_copy);
    for(int i = 0; i < a_count; i++) av_packet_free(&a_packets[i]);
    free(a_packets);
    if(save_aswr) swr_free(&save_aswr);
    if(save_aframe) av_frame_free(&save_aframe);
    if(save_aenc) avcodec_free_context(&save_aenc);

    av_write_trailer(oc);
    avio_closep(&oc->pb);
    avformat_free_context(oc);

    log_info("Recorder: Replay saved as %s", replay_filename);
    return 1;
}

/* ══════════════════════════════════════════════════════════════════════
   FRAME CAPTURE — pushes to both pipelines independently
   ══════════════════════════════════════════════════════════════════════ */

void recorder_capture_frame(void) {
    audio_notify_events();
    if(!rec_running && !buf_running) return;
    int cur_w = settings.window_width, cur_h = settings.window_height;
    if(cur_w <= 0 || cur_h <= 0) return; /* minimized: wait for drawable */
    /* Encoders and queued frames have fixed dimensions. Stop on the main
       thread before accepting a differently sized frame; do not read mutable
       window settings from a worker. Replay stop discards its ring. */
    if(rec_running && (cur_w != rec_buf_w || cur_h != rec_buf_h)) {
        log_warn("Recorder: window resized; stopping recording");
        chat_add(0, 0xFFFF44, "Recording stopped: window size changed");
        recorder_stop_recording();
    }
    if(buf_running && (cur_w != buf_w || cur_h != buf_h)) {
        log_warn("Recorder: window resized; stopping and clearing replay buffer");
        chat_add(0, 0xFFFF44, "Replay buffer cleared: window size changed");
        recorder_buffer_stop();
    }
    if(!rec_running && !buf_running) return;
    double now = window_time();
    if(target_fps <= 0 || now - last_capture_time < 0.9 / target_fps) return;
    last_capture_time = now;
    if(cur_w > INT_MAX / 4 || cur_h > (INT_MAX / 4) / cur_w) {
        log_warn("Recorder: capture dimensions too large");
        return;
    }
    int buf_size = cur_w * cur_h * 4;
    int capture_rec = rec_running && rec_encode_thread_running;
    int capture_buf = buf_running && encode_thread_running;
    if(!capture_rec && !capture_buf) return;
    unsigned char* pixels = malloc((size_t)buf_size);
    if(!pixels) return;
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, cur_w, cur_h, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    int64_t captured = av_gettime();
    unsigned char* rec_pixels = capture_rec ? pixels : NULL;
    unsigned char* buf_pixels = capture_buf ? pixels : NULL;
    if(capture_rec && capture_buf) {
        buf_pixels = malloc((size_t)buf_size);
        if(buf_pixels) memcpy(buf_pixels, pixels, (size_t)buf_size);
    }
    if(rec_pixels) {
        RawFrame rf = {rec_pixels, buf_size, captured};
        pthread_mutex_lock(&rec_raw_mutex);
        if(rec_raw_count < MAX_RAW_FRAMES) {
            rec_raw_frames[rec_raw_tail] = rf;
            rec_raw_tail = (rec_raw_tail + 1) % MAX_RAW_FRAMES;
            rec_raw_count++;
            pthread_cond_signal(&rec_raw_cond);
            rec_frames++;
        } else free(rec_pixels);
        pthread_mutex_unlock(&rec_raw_mutex);
    }
    if(buf_pixels) {
        RawFrame rf = {buf_pixels, buf_size, captured};
        pthread_mutex_lock(&raw_mutex);
        if(raw_count < MAX_RAW_FRAMES) {
            raw_frames[raw_tail] = rf;
            raw_tail = (raw_tail + 1) % MAX_RAW_FRAMES;
            raw_count++;
            pthread_cond_signal(&raw_cond);
        } else free(buf_pixels);
        pthread_mutex_unlock(&raw_mutex);
    }
}

void recorder_shutdown(void) {
    if(rec_running)
        recorder_stop_recording();
    if(buf_running)
        recorder_buffer_stop();
}

#endif /* NO_FFMPEG */
