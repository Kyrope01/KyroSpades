
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

#ifndef CONFIG_H
#define CONFIG_H

#include <stddef.h>

#include "list.h"

struct config_file_entry {
        char section[32];
        char name[32];
        char value[32];
};

extern struct RENDER_OPTIONS {
        char name[16];
        char last_address[128];
        int opengl14;
        int color_correction;
        int shadow_entities;
        int ambient_occlusion;
        float render_distance;
        int window_width;
        int window_height;
        int multisamples;
        int player_arms;
        int camera_movement;
        int fullscreen;
        int bg_tile;
        float bg_tile_speed;
        int ui_accent_r;
        int ui_accent_g;
        int ui_accent_b;
        int ui_rgb;
        float ui_rgb_speed;
        int lighten_colors;
        int show_names_in_spec;
        int hud_shadows;
        int healthbar;
        int ammo_crosshair;
        int free_aim;
        float free_aim_deadzone_h;
        float free_aim_deadzone_v;
        int greedy_meshing;
        int vsync;
        float mouse_sensitivity;
        int show_news;
        int show_fps;
        int volume;
        int voxlap_models;
        int force_displaylist;
        int invert_y;
        int smooth_fog;
        int water_shader;
        float camera_fov;
        int hold_down_sights;
        float chat_shadow;
        int chat_flip_on_open;
        int chat_spacing;
        char chat_mention_words[256];
        int chat_mention_r;
        int chat_mention_g;
        int chat_mention_b;
        float spectator_speed;
        float spectator_acceleration;
        float spectator_fog_distance;
        int iron_sight;
        int disable_raw_input;
        int ui_spacing;
        int ui_padding;
        int esp_in_spec;
        float ao_multiplier;
        int shadow_quality;
        float shadow_intensity;
        int sky_gradient;
        float sky_gradient_intensity;
        int water_waves;
        float water_wave_intensity;
        float water_wave_speed;
        int water_wave_mode;
        int water_wave_tile_size;
        int show_live_player_count;
        int ads_zoom_animation;
        float rifle_ads_fov;
        float shotgun_ads_fov;
        float smg_ads_fov;
	int disable_corpse_despawn;
	int auto_demo_recording;
	int blood_marks;
	int damage_numbers;
	int player_stats;
        int player_technical_stats;
        int rain;
        int snow;
        int rain_snow_3d;
        float exposure;
        float saturation;
        float contrast;
        float vignette;
        int   volumetric_light;
        float volumetric_light_strength;
        float volumetric_light_brightness;
        float volumetric_light_range;
        int   lens_flare;
        /* ── New visual shaders (chromatic aberration / filmic) ── */
        int   chromatic_aberration;           /* 0 = off, 1 = on */
        float chromatic_aberration_strength;  /* 0..10 — pixel offset at screen edge */
        int   filmic_tonemapping;             /* 0 = off, 1 = ACES filmic curve */
        int disable_dynamic_fov;
        int textured_blocks;
        int minimap_zoom;
        int skin_spade;
        int skin_grenade;
        int skin_rifle;
        int skin_smg;
        int skin_shotgun;
        int skin_player;
        int skin_intel;
        int skin_tent;
        int debug_log;
        int recording_fps;
        int recording_bitrate_kbps;
        int replay_enabled;
        int replay_duration;
        int replay_save_hotkey;
        /* ── Network smoothing / movement-feel (all client-side, view-only) ── */
        int net_smooth_corrections;   /* blend server position corrections instead of hard-snapping */
        int net_fast_position;        /* report position every ~0.85-0.9 s instead of every 1.0 s */
        int net_early_opt;            /* service ENet at frame start + flush outgoing immediately */
        int net_unsequenced_orient;   /* send orientation as unsequenced (drop-tolerant) packets */
        int render_interpolation;     /* partial-tick interpolation of the 60 Hz physics tic */
        int precise_pacing;           /* precise frame pacing for the software fps cap */
        int raw_aim;                  /* 1:1 view orientation (no artificial low-pass filter) */
        int crouch_instant;           /* instant crouch physics, camera eases separately */
        int view_bob;                 /* subtle first-person view bob while walking */
        int land_dip;                 /* small camera dip when landing from a fall */
        int camera_shake;             /* camera shake from gunfire and explosions (view-only) */
        char audio_monitor_source[64];
} settings, settings_tmp;

extern struct list config_keys;

struct config_macro {
        int key;
        char text[256];
};

extern struct list config_macros;

struct config_key_pair {
        int internal;
        int def;
        int original;
        int toggle;
        char name[24];
        char display[24];
        char category[24];
};

enum {
        CONFIG_TYPE_STRING,
        CONFIG_TYPE_INT,
        CONFIG_TYPE_FLOAT,
};

struct config_setting {
        void* value;
        int type;
        int min;
        int max;
        char name[32];
        char help[64];
        char category[32];
        char subcategory[32];   /* optional: groups settings into collapsible dropdowns within a category */
        int defaults[8];
        int defaults_length;
        void (*label_callback)(char* buffer, size_t length, int value, size_t index);
};

extern struct list config_settings;

void config_register_key(int internal, int def, const char* name, int toggle, const char* display,
                                                 const char* category);
int config_key_translate(int key, int dir, int* results);
struct config_key_pair* config_key(int key);
void config_key_reset_togglestates();
void config_reload(void);
void config_save(void);

#endif
