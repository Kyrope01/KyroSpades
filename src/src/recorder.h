
#ifndef RECORDER_H
#define RECORDER_H

void recorder_init(void);
void recorder_shutdown(void);

/* Normal recording (F7) — single file */
void recorder_toggle_recording(void);
int recorder_is_recording_active(void);

/* Replay buffer (UI toggle) — rolling segments */
void recorder_buffer_start(void);
void recorder_buffer_stop(void);
void recorder_toggle_buffer(void);
int recorder_is_buffer_active(void);

/* Replay save (F8) — concatenate buffer segments */
int recorder_save_replay(void);
void recorder_trigger_replay_flash(void);
int recorder_is_flashing(void);
void recorder_trigger_error_flash(void);
int recorder_is_error_flashing(void);

/* Frame capture — writes pixels to active pipes */
void recorder_capture_frame(void);

void recorder_set_fps(int fps);
int recorder_get_fps(void);
void recorder_set_bitrate(int kbps);
int recorder_get_bitrate(void);

#endif
