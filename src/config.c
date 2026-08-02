
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

#include <stdlib.h>
#include <float.h>
#include <ctype.h>
#include <string.h>
#include <limits.h>
#include <math.h>

#include "log.h"
#include "window.h"
#include "file.h"
#include "ini.h"
#include "config.h"
#include "sound.h"
#include "model.h"
#include "camera.h"

struct RENDER_OPTIONS settings, settings_tmp;
struct list config_keys;
struct list config_macros;
struct list config_settings;

struct list config_file;

#ifdef USE_SDL
#define CONFIG_BACKEND "sdl"
#else
#define CONFIG_BACKEND "glfw"
#endif

/* Backend that wrote the config file currently being parsed.  Defaults to
   "glfw" because pre-backend-tag config files were all written by GLFW
   builds. */
static char config_file_backend[8] = "glfw";

/* Tracks, by index into config_keys, whether a control binding was read from a
   symbolic key name rather than a raw integer.  Name-resolved bindings already
   hold the current backend's key code, so the legacy integer migration pass
   below must leave them untouched.  Sized well above the number of registered
   keys; entries past the cap simply fall through to the (safe) integer path. */
#define CONFIG_KEYS_MAX 256
static unsigned char config_key_named[CONFIG_KEYS_MAX];

#ifdef USE_SDL
/* Map a raw GLFW key code (as found in a legacy config.ini written by a GLFW
   build) to the equivalent SDL keysym.  Only used when migrating a GLFW config
   into an SDL build.  Covers every key bound by default in config_reload();
   unknown codes are returned unchanged and reported by the caller. */
static int config_glfw_to_sdl(int code) {
        switch(code) {
                case 32:  return SDLK_SPACE;
                case 44:  return SDLK_COMMA;
                case 45:  return SDLK_MINUS;
                case 46:  return SDLK_PERIOD;
                case 47:  return SDLK_SLASH;
                case 49:  return SDLK_1;
                case 50:  return SDLK_2;
                case 51:  return SDLK_3;
                case 52:  return SDLK_4;
                case 61:  return SDLK_EQUALS;
                case 65:  return SDLK_a;
                case 67:  return SDLK_c;
                case 68:  return SDLK_d;
                case 69:  return SDLK_e;
                case 77:  return SDLK_m;
                case 78:  return SDLK_n;
                case 80:  return SDLK_p;
                case 81:  return SDLK_q;
                case 82:  return SDLK_r;
                case 83:  return SDLK_s;
                case 84:  return SDLK_t;
                case 86:  return SDLK_v;
                case 87:  return SDLK_w;
                case 89:  return SDLK_y;
                case 90:  return SDLK_z;
                case 256: return SDLK_ESCAPE;
                case 257: return SDLK_RETURN;
                case 258: return SDLK_TAB;
                case 259: return SDLK_BACKSPACE;
                case 262: return SDLK_RIGHT;
                case 263: return SDLK_LEFT;
                case 264: return SDLK_DOWN;
                case 265: return SDLK_UP;
                case 290: return SDLK_F1;
                case 291: return SDLK_F2;
                case 292: return SDLK_F3;
                case 293: return SDLK_F4;
                case 294: return SDLK_F5;
                case 295: return SDLK_F6;
                case 298: return SDLK_F9;
                case 300: return SDLK_F11;
                case 301: return SDLK_F12;
                case 333: return SDLK_KP_MINUS;
                case 334: return SDLK_KP_PLUS;
                case 340: return SDLK_LSHIFT;
                case 341: return SDLK_LCTRL;
                default:  return code;
        }
}
#endif

/* Human-readable key names written to / read from config.ini, replacing the
   old raw integer key codes.  This is the single source of truth for both
   backends: each row pairs a token with its GLFW and SDL symbol suffix, and the
   macro below selects the column for whichever backend is being compiled (only
   that backend's headers are present, so only its column is ever referenced).
   Because the tokens match across backends, a config written on one backend is
   also understood by the other — moving config.ini between a GLFW machine and
   an SDL one "just works".  Format:  KEY(token, GLFW suffix, SDL suffix). */
#define CONFIG_KEY_NAMES(KEY)                                  \
        KEY("a", A, a) KEY("b", B, b) KEY("c", C, c) KEY("d", D, d) \
        KEY("e", E, e) KEY("f", F, f) KEY("g", G, g) KEY("h", H, h) \
        KEY("i", I, i) KEY("j", J, j) KEY("k", K, k) KEY("l", L, l) \
        KEY("m", M, m) KEY("n", N, n) KEY("o", O, o) KEY("p", P, p) \
        KEY("q", Q, q) KEY("r", R, r) KEY("s", S, s) KEY("t", T, t) \
        KEY("u", U, u) KEY("v", V, v) KEY("w", W, w) KEY("x", X, x) \
        KEY("y", Y, y) KEY("z", Z, z)                               \
        KEY("0", 0, 0) KEY("1", 1, 1) KEY("2", 2, 2) KEY("3", 3, 3) \
        KEY("4", 4, 4) KEY("5", 5, 5) KEY("6", 6, 6) KEY("7", 7, 7) \
        KEY("8", 8, 8) KEY("9", 9, 9)                               \
        KEY("f1", F1, F1)   KEY("f2", F2, F2)   KEY("f3", F3, F3)   \
        KEY("f4", F4, F4)   KEY("f5", F5, F5)   KEY("f6", F6, F6)   \
        KEY("f7", F7, F7)   KEY("f8", F8, F8)   KEY("f9", F9, F9)   \
        KEY("f10", F10, F10) KEY("f11", F11, F11) KEY("f12", F12, F12) \
        KEY("space", SPACE, SPACE)                                 \
        KEY("escape", ESCAPE, ESCAPE)                              \
        KEY("enter", ENTER, RETURN)                                \
        KEY("tab", TAB, TAB)                                       \
        KEY("backspace", BACKSPACE, BACKSPACE)                     \
        KEY("delete", DELETE, DELETE)                              \
        KEY("insert", INSERT, INSERT)                              \
        KEY("home", HOME, HOME) KEY("end", END, END)               \
        KEY("page_up", PAGE_UP, PAGEUP)                            \
        KEY("page_down", PAGE_DOWN, PAGEDOWN)                      \
        KEY("left", LEFT, LEFT)   KEY("right", RIGHT, RIGHT)       \
        KEY("up", UP, UP)         KEY("down", DOWN, DOWN)          \
        KEY("left_shift", LEFT_SHIFT, LSHIFT)                      \
        KEY("right_shift", RIGHT_SHIFT, RSHIFT)                    \
        KEY("left_control", LEFT_CONTROL, LCTRL)                   \
        KEY("right_control", RIGHT_CONTROL, RCTRL)                 \
        KEY("left_alt", LEFT_ALT, LALT)                            \
        KEY("right_alt", RIGHT_ALT, RALT)                          \
        KEY("left_super", LEFT_SUPER, LGUI)                        \
        KEY("right_super", RIGHT_SUPER, RGUI)                      \
        KEY("caps_lock", CAPS_LOCK, CAPSLOCK)                      \
        KEY("comma", COMMA, COMMA)                                 \
        KEY("period", PERIOD, PERIOD)                              \
        KEY("slash", SLASH, SLASH)                                 \
        KEY("minus", MINUS, MINUS)                                 \
        KEY("equals", EQUAL, EQUALS)                               \
        KEY("semicolon", SEMICOLON, SEMICOLON)                     \
        KEY("apostrophe", APOSTROPHE, QUOTE)                       \
        KEY("grave", GRAVE_ACCENT, BACKQUOTE)                      \
        KEY("left_bracket", LEFT_BRACKET, LEFTBRACKET)             \
        KEY("right_bracket", RIGHT_BRACKET, RIGHTBRACKET)          \
        KEY("backslash", BACKSLASH, BACKSLASH)                     \
        KEY("kp_0", KP_0, KP_0) KEY("kp_1", KP_1, KP_1)            \
        KEY("kp_2", KP_2, KP_2) KEY("kp_3", KP_3, KP_3)            \
        KEY("kp_4", KP_4, KP_4) KEY("kp_5", KP_5, KP_5)            \
        KEY("kp_6", KP_6, KP_6) KEY("kp_7", KP_7, KP_7)            \
        KEY("kp_8", KP_8, KP_8) KEY("kp_9", KP_9, KP_9)            \
        KEY("kp_add", KP_ADD, KP_PLUS)                             \
        KEY("kp_subtract", KP_SUBTRACT, KP_MINUS)                  \
        KEY("kp_multiply", KP_MULTIPLY, KP_MULTIPLY)               \
        KEY("kp_divide", KP_DIVIDE, KP_DIVIDE)                     \
        KEY("kp_enter", KP_ENTER, KP_ENTER)                        \
        KEY("kp_decimal", KP_DECIMAL, KP_PERIOD)

#ifdef USE_SDL
#define CONFIG_KEY_ROW(tok, g, s) { tok, SDLK_##s },
#else
#define CONFIG_KEY_ROW(tok, g, s) { tok, GLFW_KEY_##g },
#endif

static const struct {
        const char* name;
        int code;
} config_key_table[] = {
        CONFIG_KEY_NAMES(CONFIG_KEY_ROW)
};

#undef CONFIG_KEY_ROW

#define CONFIG_KEY_TABLE_LEN ((int)(sizeof(config_key_table) / sizeof(config_key_table[0])))

/* Symbolic key name (e.g. "w", "left_shift") -> current backend key code, or
   -1 if the token is not a known name.  Case-insensitive. */
static int config_keyname_to_code(const char* name) {
        for(int i = 0; i < CONFIG_KEY_TABLE_LEN; i++) {
                const char* a = config_key_table[i].name;
                const char* b = name;
                while(*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
                        a++;
                        b++;
                }
                if(*a == 0 && *b == 0)
                        return config_key_table[i].code;
        }
        return -1;
}

/* Current backend key code -> canonical symbolic name, or NULL if the code has
   no name (the binding is then saved as a raw integer to avoid losing it). */
static const char* config_keyname_from_code(int code) {
        for(int i = 0; i < CONFIG_KEY_TABLE_LEN; i++)
                if(config_key_table[i].code == code)
                        return config_key_table[i].name;
        return NULL;
}

#define IMPORT_SETTING(key, ini, _value)        \
    if(!strcmp(name, #ini)) {                   \
        key = _value;                           \
        return 0;                               \
    }                                           \

#define IMPORT_SETTING_STR(key, ini)            \
    if(!strcmp(name, #ini)) {                   \
        strncpy(key, value, sizeof(key) - 1);   \
        key[sizeof(key) - 1] = 0;               \
        return 0;                               \
    }                                           \

static void config_sets(const char* section, const char* name, const char* value) {
        for(int k = 0; k < list_size(&config_file); k++) {
                struct config_file_entry* e = list_get(&config_file, k);
                if(strcmp(e->name, name) == 0) {
                        strncpy(e->value, value, sizeof(e->value) - 1);
                        return;
                }
        }

        struct config_file_entry e;
        strncpy(e.section, section, sizeof(e.section) - 1);
        strncpy(e.name, name, sizeof(e.name) - 1);
        strncpy(e.value, value, sizeof(e.value) - 1);
        list_add(&config_file, &e);
}

static void config_seti(const char* section, const char* name, int value) {
        char tmp[32];
        sprintf(tmp, "%i", value);
        config_sets(section, name, tmp);
}

static void config_setf(const char* section, const char* name, float value) {
        char tmp[32];
        sprintf(tmp, "%0.6f", value);
        config_sets(section, name, tmp);
}

void config_save() {
        kv6_rebuild_complete();

        config_sets("client", "name", settings.name);
        config_sets("client", "last_address", settings.last_address);
        config_seti("client", "xres", settings.window_width);
        config_seti("client", "yres", settings.window_height);
        config_seti("client", "windowed", !settings.fullscreen);
        config_seti("client", "bg_tile", settings.bg_tile);
        config_setf("client", "bg_tile_speed", settings.bg_tile_speed);
        config_seti("client", "ui_accent_r", settings.ui_accent_r);
        config_seti("client", "ui_accent_g", settings.ui_accent_g);
        config_seti("client", "ui_accent_b", settings.ui_accent_b);
        config_seti("client", "ui_rgb", settings.ui_rgb);
        config_setf("client", "ui_rgb_speed", settings.ui_rgb_speed);
        config_seti("client", "lighten_colors", settings.lighten_colors);
        config_seti("client", "show_names_in_spec", settings.show_names_in_spec);
        config_seti("client", "esp_in_spec", settings.esp_in_spec);
        config_seti("client", "hud_shadows", settings.hud_shadows);
        config_seti("client", "healthbar", settings.healthbar);
        config_seti("client", "ammo_crosshair", settings.ammo_crosshair);
        config_seti("client", "multisamples", settings.multisamples);
        config_seti("client", "greedy_meshing", settings.greedy_meshing);
        config_seti("client", "vsync", settings.vsync);
        config_setf("client", "mouse_sensitivity", settings.mouse_sensitivity);
        config_seti("client", "show_news", settings.show_news);
        config_seti("client", "vol", settings.volume);
        config_seti("client", "show_fps", settings.show_fps);
        config_seti("client", "voxlap_models", settings.voxlap_models);
        config_seti("client", "force_displaylist", settings.force_displaylist);
        config_seti("client", "inverty", settings.invert_y);
        config_seti("client", "smooth_fog", settings.smooth_fog);
        config_seti("client", "water_shader", settings.water_shader);
        config_seti("client", "ambient_occlusion", settings.ambient_occlusion);
        config_seti("client", "shadow_quality", settings.shadow_quality);
        config_setf("client", "shadow_intensity", settings.shadow_intensity);
        config_seti("client", "sky_gradient", settings.sky_gradient);
        config_setf("client", "sky_gradient_intensity", settings.sky_gradient_intensity);
        config_seti("client", "water_waves", settings.water_waves);
        config_setf("client", "water_wave_intensity", settings.water_wave_intensity);
        config_setf("client", "water_wave_speed", settings.water_wave_speed);
        config_seti("client", "water_wave_mode", settings.water_wave_mode);
        config_seti("client", "water_wave_tile_size", settings.water_wave_tile_size);
        config_setf("client", "camera_fov", settings.camera_fov);
        config_seti("client", "hold_down_sights", settings.hold_down_sights);
        config_setf("client", "chat_shadow", settings.chat_shadow);
        config_seti("client", "chat_flip_on_open", settings.chat_flip_on_open);
        config_seti("client", "show_player_arms", settings.player_arms);
        config_seti("client", "camera_movement", settings.camera_movement);
        config_seti("client", "chat_spacing", settings.chat_spacing);
        config_sets("client", "chat_mention_words", settings.chat_mention_words);
        config_seti("client", "chat_mention_r", settings.chat_mention_r);
        config_seti("client", "chat_mention_g", settings.chat_mention_g);
        config_seti("client", "chat_mention_b", settings.chat_mention_b);
        config_setf("client", "spectator_speed", settings.spectator_speed);
        config_setf("client", "spectator_acceleration", settings.spectator_acceleration);
        config_setf("client", "spectator_fog_distance", settings.spectator_fog_distance);
        config_seti("client", "iron_sight", settings.iron_sight);
        config_seti("client", "disable_raw_input", settings.disable_raw_input);
        config_seti("client", "ui_spacing", settings.ui_spacing);
        config_seti("client", "ui_padding", settings.ui_padding);
        config_setf("client", "ao_multiplier", settings.ao_multiplier);
        config_setf("client", "exposure", settings.exposure);
        config_setf("client", "saturation", settings.saturation);
        config_setf("client", "contrast", settings.contrast);
        config_setf("client", "vignette", settings.vignette);
        config_seti("client", "volumetric_light", settings.volumetric_light);
        config_setf("client", "volumetric_light_strength", settings.volumetric_light_strength);
        config_setf("client", "volumetric_light_brightness", settings.volumetric_light_brightness);
        config_setf("client", "volumetric_light_range", settings.volumetric_light_range);
        config_seti("client", "lens_flare", settings.lens_flare);
        config_seti("client", "chromatic_aberration", settings.chromatic_aberration);
        config_setf("client", "chromatic_aberration_strength", settings.chromatic_aberration_strength);
        config_seti("client", "filmic_tonemapping", settings.filmic_tonemapping);
        config_seti("client", "show_live_player_count", settings.show_live_player_count);
        config_seti("client", "ads_zoom_animation", settings.ads_zoom_animation);
        config_seti("client", "auto_demo_recording", settings.auto_demo_recording);
        config_seti("client", "blood_marks", settings.blood_marks);
        config_seti("client", "damage_numbers", settings.damage_numbers);
        config_seti("client", "player_stats", settings.player_stats);
        config_seti("client", "player_technical_stats", settings.player_technical_stats);
        config_seti("client", "rain", settings.rain);
        config_seti("client", "snow", settings.snow);
        config_seti("client", "rain_snow_3d", settings.rain_snow_3d);
        config_setf("client", "rifle_ads_fov", settings.rifle_ads_fov);
        config_setf("client", "shotgun_ads_fov", settings.shotgun_ads_fov);
        config_setf("client", "smg_ads_fov", settings.smg_ads_fov);
        config_seti("client", "disable_corpse_despawn", settings.disable_corpse_despawn);
        config_seti("client", "disable_dynamic_fov", settings.disable_dynamic_fov);
        config_seti("client", "textured_blocks", settings.textured_blocks);
        config_seti("client", "skin_spade", settings.skin_spade);
        config_seti("client", "skin_grenade", settings.skin_grenade);
        config_seti("client", "skin_rifle", settings.skin_rifle);
        config_seti("client", "skin_smg", settings.skin_smg);
        config_seti("client", "skin_shotgun", settings.skin_shotgun);
        config_seti("client", "skin_player", settings.skin_player);
        config_seti("client", "skin_intel", settings.skin_intel);
        config_seti("client", "skin_tent", settings.skin_tent);
        config_seti("client", "debug_log", settings.debug_log);
        config_seti("client", "recording_fps", settings.recording_fps);
        config_seti("client", "recording_bitrate", settings.recording_bitrate_kbps);
        config_seti("client", "replay_enabled", settings.replay_enabled);
        config_seti("client", "replay_duration", settings.replay_duration);
        config_seti("client", "replay_save_hotkey", settings.replay_save_hotkey);
        config_sets("client", "audio_monitor_source", settings.audio_monitor_source);

        config_sets("meta", "backend", CONFIG_BACKEND);

        for(int k = 0; k < list_size(&config_macros); k++) {
                struct config_macro* m = list_get(&config_macros, k);
                char buf[64];
                snprintf(buf, sizeof(buf), "macro_%d_key", k);
                const char* kn = config_keyname_from_code(m->key);
                if(kn)
                        config_sets("macros", buf, kn);
                else
                        config_seti("macros", buf, m->key);
                snprintf(buf, sizeof(buf), "macro_%d_text", k);
                config_sets("macros", buf, m->text);
        }

        for(int k = 0; k < list_size(&config_keys); k++) {
                struct config_key_pair* e = list_get(&config_keys, k);
                if(strlen(e->name) > 0) {
                        const char* kn = config_keyname_from_code(e->def);
                        if(kn)
                                config_sets("controls", e->name, kn);
                        else
                                config_seti("controls", e->name, e->def); /* exotic key: keep raw code */
                }
        }

        void* f = file_open("config.ini", "w");
        if(f) {
                char last_section[32] = {0};
                for(int k = 0; k < list_size(&config_file); k++) {
                        struct config_file_entry* e = list_get(&config_file, k);
                        if(strcmp(e->section, last_section) != 0) {
                                file_printf(f, "\r\n[%s]\r\n", e->section);
                                strcpy(last_section, e->section);
                        }
                        file_printf(f, "%s", e->name);
                        for(int l = 0; l < 31 - strlen(e->name); l++)
                                file_printf(f, " ");
                        file_printf(f, "= %s\r\n", e->value);
                }
                file_close(f);
        }
}

static int config_read_key(void* user, const char* section, const char* name, const char* value) {
        if(!strcmp(section, "client")) {
                IMPORT_SETTING_STR(settings.name, name);
                IMPORT_SETTING(settings.window_width, xres, atoi(value));
                IMPORT_SETTING(settings.window_height, yres, atoi(value));
                IMPORT_SETTING(settings.fullscreen, windowed, !atoi(value));
                IMPORT_SETTING(settings.multisamples, multisamples, atoi(value));
                IMPORT_SETTING(settings.greedy_meshing, greedy_meshing, atoi(value));
                IMPORT_SETTING(settings.vsync, vsync, atoi(value));
                IMPORT_SETTING(settings.mouse_sensitivity, mouse_sensitivity, atof(value));
                /* Migrate configs written while the default was the raw 0.002F
                   constant; such values are meaningless for the `setting / 5`
                   formula and would freeze the camera. */
                if(settings.mouse_sensitivity > 0.0F && settings.mouse_sensitivity < 0.01F)
                        settings.mouse_sensitivity = 5.0F;
                IMPORT_SETTING(settings.show_news, show_news, atoi(value));
                if(!strcmp(name, "vol")) { settings.volume = max(min(atoi(value), 10), 0); sound_volume(settings.volume / 10.0F); }
                IMPORT_SETTING(settings.show_fps, show_fps, atoi(value));
                IMPORT_SETTING(settings.voxlap_models, voxlap_models, atoi(value));
                IMPORT_SETTING(settings.force_displaylist, force_displaylist, atoi(value));
                IMPORT_SETTING(settings.invert_y, inverty, atoi(value));
                IMPORT_SETTING(settings.smooth_fog, smooth_fog, atoi(value));
                IMPORT_SETTING(settings.water_shader, water_shader, atoi(value));
                IMPORT_SETTING(settings.ambient_occlusion, ambient_occlusion, atoi(value));
                IMPORT_SETTING(settings.camera_fov, camera_fov, fmax(fmin(atof(value), CAMERA_MAX_FOV), CAMERA_DEFAULT_FOV));
                IMPORT_SETTING(settings.hold_down_sights, hold_down_sights, atoi(value));
                IMPORT_SETTING(settings.chat_shadow, chat_shadow, fmax(0, fmin(1.f, atof(value))));
                IMPORT_SETTING(settings.chat_flip_on_open, chat_flip_on_open, atoi(value));
                IMPORT_SETTING(settings.player_arms, show_player_arms, atoi(value));
IMPORT_SETTING(settings.camera_movement, camera_movement, atoi(value));
                IMPORT_SETTING(settings.chat_spacing, chat_spacing, atoi(value));
                IMPORT_SETTING_STR(settings.chat_mention_words, chat_mention_words);
                IMPORT_SETTING(settings.chat_mention_r, chat_mention_r, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.chat_mention_g, chat_mention_g, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.chat_mention_b, chat_mention_b, max(0, min(255, atoi(value))));
                IMPORT_SETTING_STR(settings.last_address, last_address);
                IMPORT_SETTING(settings.bg_tile, bg_tile, atoi(value));
                IMPORT_SETTING(settings.show_names_in_spec, show_names_in_spec, atoi(value));
                IMPORT_SETTING(settings.esp_in_spec, esp_in_spec, atoi(value));
                IMPORT_SETTING(settings.bg_tile_speed, bg_tile_speed, fmax(0.0F, atof(value)));
                IMPORT_SETTING(settings.ui_accent_r, ui_accent_r, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.ui_accent_g, ui_accent_g, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.ui_accent_b, ui_accent_b, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.ui_rgb, ui_rgb, atoi(value) ? 1 : 0);
                IMPORT_SETTING(settings.ui_rgb_speed, ui_rgb_speed, fmaxf(1.0F, fminf(10.0F, atof(value))));
                IMPORT_SETTING(settings.lighten_colors, lighten_colors, max(0, min(255, atoi(value))));
                IMPORT_SETTING(settings.hud_shadows, hud_shadows, atoi(value));
                IMPORT_SETTING(settings.healthbar, healthbar, atoi(value) ? 1 : 0);
                IMPORT_SETTING(settings.ammo_crosshair, ammo_crosshair, atoi(value) ? 1 : 0);
                IMPORT_SETTING(settings.spectator_speed, spectator_speed, max(0.1F, min(4.F, atof(value))));
                IMPORT_SETTING(settings.spectator_acceleration, spectator_acceleration, max(10.0F, min(200.0F, atof(value))));
                IMPORT_SETTING(settings.spectator_fog_distance, spectator_fog_distance, max(5.f, min(512.f, atof(value))));
                IMPORT_SETTING(settings.iron_sight, iron_sight, atoi(value));
                IMPORT_SETTING(settings.disable_raw_input, disable_raw_input, atoi(value));
                IMPORT_SETTING(settings.ui_spacing, ui_spacing, atoi(value));
                IMPORT_SETTING(settings.ui_padding, ui_padding, atoi(value));
                IMPORT_SETTING(settings.ao_multiplier, ao_multiplier, fmaxf(0.0F, atof(value)));
                IMPORT_SETTING(settings.shadow_quality, shadow_quality, atoi(value));
                IMPORT_SETTING(settings.shadow_intensity, shadow_intensity, fmaxf(0.0F, fminf(1.0F, atof(value))));
                IMPORT_SETTING(settings.sky_gradient, sky_gradient, atoi(value));
                IMPORT_SETTING(settings.sky_gradient_intensity, sky_gradient_intensity, fmaxf(0.0F, fminf(1.0F, atof(value))));
                IMPORT_SETTING(settings.water_waves, water_waves, atoi(value));
                IMPORT_SETTING(settings.water_wave_intensity, water_wave_intensity,
                               fmaxf(1.0F, fminf(5.0F, atof(value))));
                IMPORT_SETTING(settings.water_wave_speed, water_wave_speed,
                               fmaxf(0.0F, fminf(5.0F, atof(value))));
                IMPORT_SETTING(settings.water_wave_mode, water_wave_mode, atoi(value));
                IMPORT_SETTING(settings.water_wave_tile_size, water_wave_tile_size, atoi(value));
                IMPORT_SETTING(settings.exposure, exposure, fmaxf(-100.0F, fminf(100.0F, atof(value))));
                IMPORT_SETTING(settings.saturation, saturation, fmaxf(-100.0F, fminf(100.0F, atof(value))));
                IMPORT_SETTING(settings.contrast, contrast, fmaxf(-100.0F, fminf(100.0F, atof(value))));
                IMPORT_SETTING(settings.vignette, vignette, fmaxf(0.0F, fminf(100.0F, atof(value))));
                IMPORT_SETTING(settings.volumetric_light, volumetric_light, atoi(value));
                IMPORT_SETTING(settings.volumetric_light_strength, volumetric_light_strength,
                               fmaxf(0.0F, fminf(1.0F, atof(value))));
                IMPORT_SETTING(settings.volumetric_light_brightness, volumetric_light_brightness,
                               fmaxf(0.0F, fminf(1.0F, atof(value))));
                IMPORT_SETTING(settings.volumetric_light_range, volumetric_light_range,
                               fmaxf(0.1F, fminf(2.0F, atof(value))));
                IMPORT_SETTING(settings.lens_flare, lens_flare, atoi(value));
                IMPORT_SETTING(settings.chromatic_aberration, chromatic_aberration, atoi(value));
                IMPORT_SETTING(settings.chromatic_aberration_strength, chromatic_aberration_strength,
                               fmaxf(0.0F, fminf(10.0F, atof(value))));
                IMPORT_SETTING(settings.filmic_tonemapping, filmic_tonemapping, atoi(value));
                IMPORT_SETTING(settings.show_live_player_count, show_live_player_count, atoi(value));
                IMPORT_SETTING(settings.ads_zoom_animation, ads_zoom_animation, atoi(value));
                IMPORT_SETTING(settings.auto_demo_recording, auto_demo_recording, atoi(value));
                IMPORT_SETTING(settings.blood_marks, blood_marks, atoi(value));
                IMPORT_SETTING(settings.damage_numbers, damage_numbers, atoi(value));
                IMPORT_SETTING(settings.player_stats, player_stats, atoi(value));
                IMPORT_SETTING(settings.player_technical_stats, player_technical_stats, atoi(value));
                IMPORT_SETTING(settings.rain, rain, atoi(value));
                IMPORT_SETTING(settings.snow, snow, atoi(value));
                IMPORT_SETTING(settings.rain_snow_3d, rain_snow_3d, atoi(value));
                IMPORT_SETTING(settings.rifle_ads_fov, rifle_ads_fov, fmaxf(5.0F, fminf(atof(value), CAMERA_DEFAULT_FOV)));
                IMPORT_SETTING(settings.shotgun_ads_fov, shotgun_ads_fov, fmaxf(5.0F, fminf(atof(value), CAMERA_DEFAULT_FOV)));
                IMPORT_SETTING(settings.smg_ads_fov, smg_ads_fov, fmaxf(5.0F, fminf(atof(value), CAMERA_DEFAULT_FOV)));
                IMPORT_SETTING(settings.disable_corpse_despawn, disable_corpse_despawn, atoi(value));
                IMPORT_SETTING(settings.disable_dynamic_fov, disable_dynamic_fov, atoi(value));
                IMPORT_SETTING(settings.textured_blocks, textured_blocks, atoi(value));
                IMPORT_SETTING(settings.skin_spade, skin_spade, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_grenade, skin_grenade, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_rifle, skin_rifle, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_smg, skin_smg, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_shotgun, skin_shotgun, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_player, skin_player, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_intel, skin_intel, max(0, atoi(value)));
                IMPORT_SETTING(settings.skin_tent, skin_tent, max(0, atoi(value)));
                IMPORT_SETTING(settings.debug_log, debug_log, atoi(value));
                IMPORT_SETTING(settings.recording_fps, recording_fps, atoi(value));
                IMPORT_SETTING(settings.recording_bitrate_kbps, recording_bitrate, atoi(value));
                IMPORT_SETTING(settings.replay_enabled, replay_enabled, atoi(value));
                IMPORT_SETTING(settings.replay_duration, replay_duration, atoi(value));
                IMPORT_SETTING(settings.replay_save_hotkey, replay_save_hotkey, atoi(value));
                IMPORT_SETTING_STR(settings.audio_monitor_source, audio_monitor_source);
        }
        if(!strcmp(section, "meta")) {
                if(!strcmp(name, "backend")) {
                        strncpy(config_file_backend, value, sizeof(config_file_backend) - 1);
                        config_file_backend[sizeof(config_file_backend) - 1] = 0;
                }
        }
        if(!strcmp(section, "macros")) {
                int idx;
                char type;
                if(sscanf(name, "macro_%d_%c", &idx, &type) == 2) {
                        while(list_size(&config_macros) <= idx)
                                list_add(&config_macros, &(struct config_macro){0});
                        struct config_macro* m = list_get(&config_macros, idx);
                        if(type == 'k') {
                                int code = config_keyname_to_code(value);
                                if(code >= 0)
                                        m->key = code;
                                else
                                        m->key = atoi(value);
                        } else if(type == 't') {
                                strncpy(m->text, value, sizeof(m->text) - 1);
                                m->text[sizeof(m->text) - 1] = '\0';
                        }
                }
                return 0;
        }
        if(!strcmp(section, "controls")) {
                for(int k = 0; k < list_size(&config_keys); k++) {
                        struct config_key_pair* key = list_get(&config_keys, k);
                        if(!strcmp(name, key->name)) {
                                int named = config_keyname_to_code(value);
                                if(named >= 0) {
                                        /* Symbolic name: already expressed in this backend's key
                                           codes, so it must not be touched by the migration pass. */
                                        key->def = named;
                                        if(k < CONFIG_KEYS_MAX)
                                                config_key_named[k] = 1;
                                } else {
                                        /* Legacy raw integer code; migrated post-parse if the file
                                           was written by a different backend. */
                                        key->def = strtol(value, NULL, 0);
                                }
                                log_debug("found override for %s -> %i (%s)", key->name, key->def,
                                                  named >= 0 ? "name" : "legacy code");
                                break;
                        }
                }
        }
        return 1;
}

void config_register_key(int internal, int def, const char* name, int toggle, const char* display,
                                                 const char* category) {
        struct config_key_pair key;
        key.internal = internal;
        key.def = def;
        key.original = def;
        key.toggle = toggle;
        if(display)
                strncpy(key.display, display, sizeof(key.display) - 1);
        else
                *key.display = 0;

        if(name)
                strncpy(key.name, name, sizeof(key.name) - 1);
        else
                *key.name = 0;

        if(category)
                strncpy(key.category, category, sizeof(key.category) - 1);
        else
                *key.category = 0;
        list_add(&config_keys, &key);
}

int config_key_translate(int key, int dir, int* results) {
        int count = 0;

        for(int k = 0; k < list_size(&config_keys); k++) {
                struct config_key_pair* a = list_get(&config_keys, k);

                if(dir && a->internal == key) {
                        if(results)
                                results[count] = a->def;
                        count++;
                } else if(!dir && a->def == key) {
                        if(results)
                                results[count] = a->internal;
                        count++;
                }
        }

        return count;
}

struct config_key_pair* config_key(int key) {
        for(int k = 0; k < list_size(&config_keys); k++) {
                struct config_key_pair* a = list_get(&config_keys, k);
                if(a->internal == key)
                        return a;
        }
        return NULL;
}

void config_key_reset_togglestates() {
        for(int k = 0; k < list_size(&config_keys); k++) {
                struct config_key_pair* a = list_get(&config_keys, k);
                if(a->toggle)
                        window_pressed_keys[a->internal] = 0;
        }
}

static int config_key_cmp(const void* a, const void* b) {
        const struct config_key_pair* A = (const struct config_key_pair*)a;
        const struct config_key_pair* B = (const struct config_key_pair*)b;

        int cmp = strcmp(A->category, B->category);
        return cmp ? cmp : strcmp(A->display, B->display);
}

static void config_label_pixels(char* buffer, size_t length, int value, size_t index) {
        if(value == 800 || value == 600) {
                snprintf(buffer, length, "default: %ipx", value);
        } else {
                snprintf(buffer, length, "%ipx", value);
        }
}

static void config_label_vsync(char* buffer, size_t length, int value, size_t index) {
        if(value == 0) {
                snprintf(buffer, length, "disabled");
        } else if(value == 1) {
                snprintf(buffer, length, "enabled");
        } else {
                snprintf(buffer, length, "max %i fps", value);
        }
}

static void config_label_msaa(char* buffer, size_t length, int value, size_t index) {
        if(index == 0) {
                snprintf(buffer, length, "No MSAA");
        } else {
                snprintf(buffer, length, "%ix MSAA", value);
        }
}

void config_reload() {
        if(!list_created(&config_file))
                list_create(&config_file, sizeof(struct config_file_entry));
        else
                list_clear(&config_file);

        if(!list_created(&config_keys))
                list_create(&config_keys, sizeof(struct config_key_pair));
        else
                list_clear(&config_keys);

        if(!list_created(&config_macros))
                list_create(&config_macros, sizeof(struct config_macro));
        else
                list_clear(&config_macros);

#ifdef USE_SDL
        config_register_key(WINDOW_KEY_UP, SDLK_w, "move_forward", 0, "Forward", "Movement");
        config_register_key(WINDOW_KEY_DOWN, SDLK_s, "move_backward", 0, "Backward", "Movement");
        config_register_key(WINDOW_KEY_LEFT, SDLK_a, "move_left", 0, "Left", "Movement");
        config_register_key(WINDOW_KEY_RIGHT, SDLK_d, "move_right", 0, "Right", "Movement");
        config_register_key(WINDOW_KEY_SPACE, SDLK_SPACE, "jump", 0, "Jump", "Movement");
        config_register_key(WINDOW_KEY_SPRINT, SDLK_LSHIFT, "sprint", 0, "Sprint", "Movement");
        config_register_key(WINDOW_KEY_SHIFT, SDLK_LSHIFT, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_CURSOR_UP, SDLK_UP, "cube_color_up", 0, "Color up", "Block");
        config_register_key(WINDOW_KEY_CURSOR_DOWN, SDLK_DOWN, "cube_color_down", 0, "Color down", "Block");
        config_register_key(WINDOW_KEY_CURSOR_LEFT, SDLK_LEFT, "cube_color_left", 0, "Color left", "Block");
        config_register_key(WINDOW_KEY_CURSOR_RIGHT, SDLK_RIGHT, "cube_color_right", 0, "Color right", "Block");
        config_register_key(WINDOW_KEY_BACKSPACE, SDLK_BACKSPACE, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_HOME, SDLK_HOME, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_END, SDLK_END, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_DELETE, SDLK_DELETE, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_A, SDLK_a, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_X, SDLK_x, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_TOOL1, SDLK_1, "tool_spade", 0, "Select spade", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL2, SDLK_2, "tool_block", 0, "Select block", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL3, SDLK_3, "tool_gun", 0, "Select gun", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL4, SDLK_4, "tool_grenade", 0, "Select grenade", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TAB, SDLK_TAB, "view_score", 0, "Score", "Information");
        config_register_key(WINDOW_KEY_ESCAPE, SDLK_ESCAPE, "quit_game", 0, "Quit", "Game");
        config_register_key(WINDOW_KEY_ESCAPE, SDLK_AC_BACK, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_MAP, SDLK_m, "view_map", 1, "Map", "Information");
        config_register_key(WINDOW_KEY_MAP_ZOOM, SDLK_x, "map_zoom", 0, "Map zoom", "Information");
        config_register_key(WINDOW_KEY_CROUCH, SDLK_LCTRL, "crouch", 0, "Crouch", "Movement");
        config_register_key(WINDOW_KEY_SNEAK, SDLK_v, "sneak", 0, "Sneak", "Movement");
        config_register_key(WINDOW_KEY_ENTER, SDLK_RETURN, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F1, SDLK_F1, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F2, SDLK_F2, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F3, SDLK_F3, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F4, SDLK_F4, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_YES, SDLK_y, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_YES, SDLK_z, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_NO, SDLK_n, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_VOLUME_UP, SDLK_KP_PLUS, "volume_up", 0, "Volume up", "Game");
        config_register_key(WINDOW_KEY_VOLUME_DOWN, SDLK_KP_MINUS, "volume_down", 0, "Volume down", "Game");
        config_register_key(WINDOW_KEY_V, SDLK_v, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_C, SDLK_c, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_RELOAD, SDLK_r, "reload", 0, "Reload", "Tools & Weapons");
        config_register_key(WINDOW_KEY_CHAT, SDLK_t, "chat_global", 0, "Chat", "Game");
        config_register_key(WINDOW_KEY_FULLSCREEN, SDLK_F11, "fullscreen", 0, "Fullscreen", "Game");
        config_register_key(WINDOW_KEY_SCREENSHOT, SDLK_F5, "screenshot", 0, "Screenshot", "Information");
        config_register_key(WINDOW_KEY_CHANGETEAM, SDLK_COMMA, "change_team", 0, "Team select", "Game");
        config_register_key(WINDOW_KEY_CHANGEWEAPON, SDLK_PERIOD, "change_weapon", 0, "Gun select", "Tools & Weapons");
        config_register_key(WINDOW_KEY_PICKCOLOR, SDLK_e, "cube_color_sample", 0, "Pick color", "Block");
        config_register_key(WINDOW_KEY_COMMAND, SDLK_SLASH, "chat_command", 0, "Command", "Game");
        config_register_key(WINDOW_KEY_HIDEHUD, SDLK_F6, "hide_hud", 1, "Hide HUD", "Game");
        config_register_key(WINDOW_KEY_LASTTOOL, SDLK_q, "last_tool", 0, "Last tool", "Tools & Weapons");
        config_register_key(WINDOW_KEY_NETWORKSTATS, SDLK_F12, "network_stats", 1, "Network stats", "Information");
         config_register_key(WINDOW_KEY_SAVE_MAP, SDLK_F9, "save_map", 0, "Save map", "Game");
        config_register_key(WINDOW_KEY_SELECT1, SDLK_1, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_SELECT2, SDLK_2, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_SELECT3, SDLK_3, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_HISTORY_PREVIOUS, SDLK_UP, "history_previous", 0, "Previous message", "Chat history");
        config_register_key(WINDOW_KEY_HISTORY_NEXT, SDLK_DOWN, "history_next", 0, "Next message", "Chat history");
        config_register_key(WINDOW_KEY_YCLAMP, SDLK_c, "y_clamp", 0, "Toggle Y-Clamp", "Spectator");
        config_register_key(WINDOW_KEY_SWITCH_CAMERA, SDLK_v, "switch_camera", 0, "Toggle 1st/3rd person view", "Spectator");
        config_register_key(WINDOW_KEY_NEXT_PLAYER, SDLK_p, "next_player", 0, "Next alive player", "Spectator");
        config_register_key(WINDOW_KEY_ROLL_CW, SDLK_e, "roll_cw", 0, "Roll clockwise", "Spectator");
        config_register_key(WINDOW_KEY_DEMO_PAUSE,      SDLK_r,      "demo_pause",      0, "Play / pause",            "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SEEK_BACK,  SDLK_LEFT,   "demo_seek_back",  0, "Skip back 10 seconds",    "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SEEK_FWD,   SDLK_RIGHT,  "demo_seek_fwd",   0, "Skip forward 10 seconds", "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SPEED_DOWN, SDLK_MINUS,  "demo_speed_down", 0, "Half playback speed",     "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SPEED_UP,   SDLK_EQUALS, "demo_speed_up",   0, "Double playback speed",   "Watch Demo");
        config_register_key(WINDOW_KEY_ROLL_CCW, SDLK_q, "roll_ccw", 0, "Roll counter-clockwise", "Spectator");
                                 config_register_key(WINDOW_KEY_RECORDING, SDLK_F7, "recording", 0, "Start/Stop recording", "Game");
                                 config_register_key(WINDOW_KEY_REPLAY_SAVE, SDLK_F8, "replay_save", 0, "Save Replay", "Game");

#endif

#ifdef USE_GLFW
        config_register_key(WINDOW_KEY_UP, GLFW_KEY_W, "move_forward", 0, "Forward", "Movement");
        config_register_key(WINDOW_KEY_DOWN, GLFW_KEY_S, "move_backward", 0, "Backward", "Movement");
        config_register_key(WINDOW_KEY_LEFT, GLFW_KEY_A, "move_left", 0, "Left", "Movement");
        config_register_key(WINDOW_KEY_RIGHT, GLFW_KEY_D, "move_right", 0, "Right", "Movement");
        config_register_key(WINDOW_KEY_SPACE, GLFW_KEY_SPACE, "jump", 0, "Jump", "Movement");
        config_register_key(WINDOW_KEY_SPRINT, GLFW_KEY_LEFT_SHIFT, "sprint", 0, "Sprint", "Movement");
        config_register_key(WINDOW_KEY_SHIFT, GLFW_KEY_LEFT_SHIFT, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_CURSOR_UP, GLFW_KEY_UP, "cube_color_up", 0, "Color up", "Block");
        config_register_key(WINDOW_KEY_CURSOR_DOWN, GLFW_KEY_DOWN, "cube_color_down", 0, "Color down", "Block");
        config_register_key(WINDOW_KEY_CURSOR_LEFT, GLFW_KEY_LEFT, "cube_color_left", 0, "Color left", "Block");
        config_register_key(WINDOW_KEY_CURSOR_RIGHT, GLFW_KEY_RIGHT, "cube_color_right", 0, "Color right", "Block");
        config_register_key(WINDOW_KEY_BACKSPACE, GLFW_KEY_BACKSPACE, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_HOME, GLFW_KEY_HOME, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_END, GLFW_KEY_END, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_DELETE, GLFW_KEY_DELETE, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_A, GLFW_KEY_A, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_X, GLFW_KEY_X, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_TOOL1, GLFW_KEY_1, "tool_spade", 0, "Select spade", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL2, GLFW_KEY_2, "tool_block", 0, "Select block", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL3, GLFW_KEY_3, "tool_gun", 0, "Select gun", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TOOL4, GLFW_KEY_4, "tool_grenade", 0, "Select grenade", "Tools & Weapons");
        config_register_key(WINDOW_KEY_TAB, GLFW_KEY_TAB, "view_score", 0, "Score", "Information");
        config_register_key(WINDOW_KEY_ESCAPE, GLFW_KEY_ESCAPE, "quit_game", 0, "Quit", "Game");
        config_register_key(WINDOW_KEY_MAP, GLFW_KEY_M, "view_map", 1, "Map", "Information");
        config_register_key(WINDOW_KEY_MAP_ZOOM, GLFW_KEY_X, "map_zoom", 0, "Map zoom", "Information");
        config_register_key(WINDOW_KEY_CROUCH, GLFW_KEY_LEFT_CONTROL, "crouch", 0, "Crouch", "Movement");
        config_register_key(WINDOW_KEY_SNEAK, GLFW_KEY_V, "sneak", 0, "Sneak", "Movement");
        config_register_key(WINDOW_KEY_ENTER, GLFW_KEY_ENTER, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F1, GLFW_KEY_F1, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F2, GLFW_KEY_F2, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F3, GLFW_KEY_F3, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_F4, GLFW_KEY_F4, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_YES, GLFW_KEY_Y, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_YES, GLFW_KEY_Z, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_NO, GLFW_KEY_N, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_VOLUME_UP, GLFW_KEY_KP_ADD, "volume_up", 0, "Volume up", "Game");
        config_register_key(WINDOW_KEY_VOLUME_DOWN, GLFW_KEY_KP_SUBTRACT, "volume_down", 0, "Volume down", "Game");
        config_register_key(WINDOW_KEY_V, GLFW_KEY_V, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_C, GLFW_KEY_C, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_RELOAD, GLFW_KEY_R, "reload", 0, "Reload", "Tools & Weapons");
        config_register_key(WINDOW_KEY_CHAT, GLFW_KEY_T, "chat_global", 0, "Chat", "Game");
        config_register_key(WINDOW_KEY_FULLSCREEN, GLFW_KEY_F11, "fullscreen", 0, "Fullscreen", "Game");
        config_register_key(WINDOW_KEY_SCREENSHOT, GLFW_KEY_F5, "screenshot", 0, "Screenshot", "Game");
        config_register_key(WINDOW_KEY_CHANGETEAM, GLFW_KEY_COMMA, "change_team", 0, "Team select", "Game");
        config_register_key(WINDOW_KEY_CHANGEWEAPON, GLFW_KEY_PERIOD, "change_weapon", 0, "Gun select", "Tools & Weapons");
        config_register_key(WINDOW_KEY_PICKCOLOR, GLFW_KEY_E, "cube_color_sample", 0, "Pick color", "Block");
        config_register_key(WINDOW_KEY_COMMAND, GLFW_KEY_SLASH, "chat_command", 0, "Command", "Game");
        config_register_key(WINDOW_KEY_HIDEHUD, GLFW_KEY_F6, "hide_hud", 1, "Hide HUD", "Game");
        config_register_key(WINDOW_KEY_LASTTOOL, GLFW_KEY_Q, "last_tool", 0, "Last tool", "Tools & Weapons");
        config_register_key(WINDOW_KEY_NETWORKSTATS, GLFW_KEY_F12, "network_stats", 1, "Network stats", "Information");
         config_register_key(WINDOW_KEY_SAVE_MAP, GLFW_KEY_F9, "save_map", 0, "Save map", "Game");
        config_register_key(WINDOW_KEY_SELECT1, GLFW_KEY_1, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_SELECT2, GLFW_KEY_2, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_SELECT3, GLFW_KEY_3, NULL, 0, NULL, NULL);
        config_register_key(WINDOW_KEY_HISTORY_PREVIOUS, GLFW_KEY_UP, "history_previous", 0, "Previous message", "Chat history");
        config_register_key(WINDOW_KEY_HISTORY_NEXT, GLFW_KEY_DOWN, "history_next", 0, "Next message", "Chat history");
        config_register_key(WINDOW_KEY_YCLAMP, GLFW_KEY_C, "y_clamp", 0, "Toggle Y-Clamp", "Spectator");
        config_register_key(WINDOW_KEY_SWITCH_CAMERA, GLFW_KEY_V, "switch_camera", 0, "Toggle 1st/3rd person view", "Spectator");
        config_register_key(WINDOW_KEY_NEXT_PLAYER, GLFW_KEY_P, "next_player", 0, "Next alive player", "Spectator");
        config_register_key(WINDOW_KEY_ROLL_CW, GLFW_KEY_E, "roll_cw", 0, "Roll clockwise", "Spectator");
        config_register_key(WINDOW_KEY_DEMO_PAUSE,      GLFW_KEY_R,     "demo_pause",      0, "Play / pause",            "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SEEK_BACK,  GLFW_KEY_LEFT,  "demo_seek_back",  0, "Skip back 10 seconds",    "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SEEK_FWD,   GLFW_KEY_RIGHT, "demo_seek_fwd",   0, "Skip forward 10 seconds", "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SPEED_DOWN, GLFW_KEY_MINUS, "demo_speed_down", 0, "Half playback speed",     "Watch Demo");
        config_register_key(WINDOW_KEY_DEMO_SPEED_UP,   GLFW_KEY_EQUAL, "demo_speed_up",   0, "Double playback speed",   "Watch Demo");
        config_register_key(WINDOW_KEY_ROLL_CCW, GLFW_KEY_Q, "roll_ccw", 0, "Roll counter-clockwise", "Spectator");
                                 config_register_key(WINDOW_KEY_RECORDING, GLFW_KEY_F7, "recording", 0, "Start/Stop recording", "Game");
                                 config_register_key(WINDOW_KEY_REPLAY_SAVE, GLFW_KEY_F8, "replay_save", 0, "Save Replay", "Game");

#endif

        list_sort(&config_keys, config_key_cmp);

        strcpy(config_file_backend, "glfw");
        memset(config_key_named, 0, sizeof(config_key_named));

        char* s = file_load("config.ini");
        if(s) {
                log_debug("Loading config.ini (%zu bytes)", strlen(s));
                ini_parse_string(s, config_read_key, NULL);
                free(s);
        } else {
                log_debug("No config.ini found, using defaults");
        }

#ifdef USE_SDL
        /* Single post-parse migration: now that the whole file has been read,
           config_file_backend is final, so [meta] and [controls] order in the file
           no longer matters.  Convert legacy raw GLFW key codes to SDL keysyms once.
           Bindings that were read as symbolic names are already correct and skipped. */
        if(strcmp(config_file_backend, CONFIG_BACKEND) != 0) {
                for(int k = 0; k < list_size(&config_keys); k++) {
                        if(k < CONFIG_KEYS_MAX && config_key_named[k])
                                continue;
                        struct config_key_pair* key = list_get(&config_keys, k);
                        int converted = config_glfw_to_sdl(key->def);
                        if(converted == key->def && key->def != key->original)
                                log_warn("config: no %s->%s mapping for key code %i (%s), kept as-is",
                                                 config_file_backend, CONFIG_BACKEND, key->def,
                                                 key->name[0] ? key->name : "<unnamed>");
                        key->def = converted;
                }
        }
#endif

        if(!list_created(&config_settings))
                list_create(&config_settings, sizeof(struct config_setting));
        else
                list_clear(&config_settings);

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = settings_tmp.name,
                                 .type = CONFIG_TYPE_STRING,
                                 .max = sizeof(settings.name) - 1,
                                 .name = "Name",
                                 .help = "Ingame player name",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.mouse_sensitivity,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = INT_MAX,
                                 .name = "Mouse sensitivity",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.camera_fov,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = CAMERA_DEFAULT_FOV,
                                 .max = CAMERA_MAX_FOV,
                                 .name = "Camera FOV",
                                 .help = "Field of View in degrees",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.volume,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 10,
                                 .name = "Volume",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.window_width,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = INT_MAX,
                                 .name = "Game width",
                                 .help = "Default: 960",
                                 .category = "Graphic Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.window_height,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = INT_MAX,
                                 .name = "Game height",
                                 .help = "Default: 540",
                                 .category = "Graphic Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.vsync,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = INT_MAX,
                                 .name = "V-Sync",
                                 .help = "Limits your game's fps",
                                 .defaults = 0,
                                 1,
                                 60,
                                 120,
                                 144,
                                 240,
                                 .defaults_length = 6,
                                 .label_callback = config_label_vsync,
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.fullscreen,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Fullscreen",
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.multisamples,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 16,
                                 .name = "Multisamples",
                                 .help = "Smooth out block edges",
                                 .defaults = 0,
                                 2,
                                 4,
                                 8,
                                 16,
                                 .defaults_length = 5,
                                 .label_callback = config_label_msaa,
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.voxlap_models,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Render models like in voxlap",
                                 .name = "Voxlap models",
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.player_arms,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Render player hand during gameplay",
                                 .name = "Render hand",
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.hold_down_sights,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Only aim while pressing RMB",
                                 .name = "Hold down sights",
                                 .category = "Weapon Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.greedy_meshing,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Join similar mesh faces",
                                 .name = "Greedy meshing",
                                 .category = "Graphic Settings",
                                 .subcategory = "Meshing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.force_displaylist,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable this on buggy drivers",
                                 .name = "Force Displaylist",
                                 .category = "Graphic Settings",
                                 .subcategory = "Display",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.smooth_fog,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable this on buggy drivers",
                                 .name = "Smooth fog",
                                 .category = "Graphic Settings",
                                 .subcategory = "Meshing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_shader,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                  .help = "Reflective water surface",
                                  .name = "Water shader",
                                  .category = "Visual Effects",
                                  .subcategory = "Water",
                          });
         list_add(&config_settings,
                          &(struct config_setting) {
                                  .value = &settings_tmp.ambient_occlusion,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "(won't work with greedy mesh)",
                                 .name = "Ambient occlusion",
                                 .category = "Graphic Settings",
                                 .subcategory = "Meshing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ao_multiplier,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 5,
                                 .help = "Multiplier for ambient occlusion strength",
                                 .name = "AO multiplier",
                                 .category = "Graphic Settings",
                                 .subcategory = "Meshing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.shadow_quality,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                  .help = "Realistic directional shadows from blocks",
                                  .name = "Realistic shadows",
                                  .category = "Visual Effects",
                                  .subcategory = "Shadows",
                          });
         list_add(&config_settings,
                          &(struct config_setting) {
                                  .value = &settings_tmp.shadow_intensity,
                                  .type = CONFIG_TYPE_FLOAT,
                                  .min = 0,
                                  .max = 1,
                                  .help = "How dark sun shadows are",
                                  .name = "Shadow intensity",
                                  .category = "Visual Effects",
                                  .subcategory = "Shadows",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.sky_gradient,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Blended sky gradient instead of flat color",
                                 .name = "Sky gradient",
                                 .category = "Visual Effects",
                                 .subcategory = "Sky",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.sky_gradient_intensity,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Intensity of the sky gradient effect",
                                 .name = "Sky gradient intensity",
                                 .category = "Visual Effects",
                                 .subcategory = "Sky",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_waves,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Animated waves on the water surface",
                                 .name = "Water waves",
                                 .category = "Visual Effects",
                                 .subcategory = "Water",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_wave_intensity,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 1,
                                 .max = 5,
                                 .help = "Height of water waves (1=normal, 5=extreme)",
                                 .name = "Wave intensity",
                                 .category = "Visual Effects",
                                 .subcategory = "Water",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_wave_speed,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 5,
                                 .help = "Wave speed (0=frozen, 1=normal, 5=fast)",
                                 .name = "Wave speed",
                                 .category = "Visual Effects",
                                 .subcategory = "Water",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_wave_mode,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable tile-based wave mode",
                                 .name = "Tile mode",
                                 .category = "Visual Effects",
                                 .subcategory = "Water",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.water_wave_tile_size,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 1,
                                 .max = 4,
                                 .help = "Tile size (1, 2, or 4)",
                                 .name = "Wave tile size",
                                 .category = "Visual Effects",
                                 .subcategory = "Water",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.volumetric_light,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable volumetric light scattering (a.k.a. Godrays)",
                                 .name = "Volumetric light",
                                 .category = "Visual Effects",
                                 .subcategory = "Lighting",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.volumetric_light_strength,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Strength of volumetric light (0 = off, 1 = strongest)",
                                 .name = "Volumetric light strength",
                                 .category = "Visual Effects",
                                 .subcategory = "Lighting",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.volumetric_light_brightness,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Brightness of the white ray glow (0=none, 1=full)",
                                 .name = "Ray brightness",
                                 .category = "Visual Effects",
                                 .subcategory = "Lighting",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.volumetric_light_range,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0.1F,
                                 .max = 2.0F,
                                 .help = "Range of rays from sun (0.1=short, 2=full screen)",
                                 .name = "Ray range",
                                 .category = "Visual Effects",
                                 .subcategory = "Lighting",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.lens_flare,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable lens flare from the sun",
                                 .name = "Lens flare",
                                 .category = "Visual Effects",
                                 .subcategory = "Lighting",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chromatic_aberration,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable subtle RGB split at screen edges (lens effect)",
                                 .name = "Chromatic aberration",
                                 .category = "Visual Effects",
                                 .subcategory = "Post Processing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chromatic_aberration_strength,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 10,
                                 .help = "Chromatic aberration strength (0=none, 10=extreme)",
                                 .name = "Chromatic aberration strength",
                                 .category = "Visual Effects",
                                 .subcategory = "Post Processing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.filmic_tonemapping,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable ACES filmic tone mapping (cinematic color curve)",
                                 .name = "Filmic tone mapping",
                                 .category = "Visual Effects",
                                 .subcategory = "Post Processing",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.show_fps,
                                 .category = "HUD/UI Settings",
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Show fps",
                                 .help = "Show current fps and ping ingame",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.invert_y,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Invert y",
                                 .help = "Invert vertical mouse movement",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.show_news,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Show news",
                                 .help = "Show news on server list",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_shadow,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0.f,
                                 .max = 1.f,
                                 .help = "Chat background opacity",
                                 .name = "Chat background opacity",
                                 .category = "Chat Settings",
                                 .subcategory = "Chat",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_rgb,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "RGB",
                                 .help = "Smoothly cycle the UI accent through RGB colors",
                                 .category = "HUD/UI Settings",
                                 .subcategory = "RGB",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_rgb_speed,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 1,
                                 .max = 10,
                                 .name = "RGB Speed",
                                 .help = "Speed of the animated RGB UI accent",
                                 .category = "HUD/UI Settings",
                                 .subcategory = "RGB",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_accent_r,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "UI Accent: Red",
                                 .help = "Static UI accent color (red), used when RGB is disabled",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_accent_g,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "UI Accent: Green",
                                 .help = "Static UI accent color (green), used when RGB is disabled",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_accent_b,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "UI Accent: Blue",
                                 .help = "Static UI accent color (blue), used when RGB is disabled",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.lighten_colors,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "Lighten colors",
                                 .help = "Makes in-game team colors in the HUD brighter",
                                 .category = "Graphic Settings",
                                 .subcategory = "Color",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.show_names_in_spec,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Spectator Player Names",
                                 .help = "Displays player names in spectator",
                                 .category = "Spectator Mode Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.esp_in_spec,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Spectator ESP",
                                 .help = "See players through walls in spectator mode",
                                 .category = "Spectator Mode Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.hud_shadows,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "HUD shadows",
                                 .help = "Enables text shadows in various UI elements",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.healthbar,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Healthbar",
                                 .help = "Show a smooth segmented health bar below the health display",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ammo_crosshair,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Display ammo around crosshair",
                                 .help = "Show segmented magazine ammo around the crosshair",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_spacing,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 8,
                                 .help = "Spacing between messages in chat",
                                 .name = "Chat spacing",
                                 .category = "Chat Settings",
                                 .subcategory = "Chat",
                                 .subcategory = "Chat",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_flip_on_open,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Reverse chat order, newest messages at the bottom",
                                 .name = "Reverse chat on open",
                                 .category = "Chat Settings",
                                 .subcategory = "Chat",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_mention_words,
                                 .type = CONFIG_TYPE_STRING,
                                 .max = sizeof(settings.chat_mention_words) - 1,
                                 .name = "Mention words",
                                 .help = "Words separated by commas that highlight chat messages",
                                 .category = "Chat Settings",
                                 .subcategory = "Mentions",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_mention_r,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "Mention Highlight: Red",
                                 .help = "Mention highlight color (red)",
                                 .category = "Chat Settings",
                                 .subcategory = "Mentions",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_mention_g,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "Mention Highlight: Green",
                                 .help = "Mention highlight color (green)",
                                 .category = "Chat Settings",
                                 .subcategory = "Mentions",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.chat_mention_b,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 255,
                                 .name = "Mention Highlight: Blue",
                                 .help = "Mention highlight color (blue)",
                                 .category = "Chat Settings",
                                 .subcategory = "Mentions",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.spectator_speed,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0.1F,
                                 .max = 4.F,
                                 .help = "Speed of movement in spectator",
                                 .name = "Spectator Speed",
                                 .category = "Spectator Mode Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.spectator_acceleration,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 10,
                                 .max = 200,
                        .help = "Rate of acceleration and deceleration for spectator camera",
                         .name = "Spectator Camera Acceleration",
                         .category = "Spectator Mode Settings",
                 });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.spectator_fog_distance,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 5,
                                 .max = 512,
                                 .help = "Fog distance in spectator mode (also increases render distance)",
                                 .name = "Spectator Fog Distance",
                                 .category = "Spectator Mode Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.iron_sight,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Use weapon-specific iron sights instead of a dot",
                                 .name = "Iron sight",
                                 .category = "Weapon Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.show_live_player_count,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Show live player count in the HUD",
                                 .name = "Show live player count",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.disable_raw_input,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Disables raw mouse input. Can help with buggy mice",
                                 .name = "Disable raw input",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_spacing,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 8,
                                 .max = 32,
                                 .help = "Spacing between UI elements",
                                 .name = "UI Spacing",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ui_padding,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 5,
                                 .max = 32,
                                 .help = "Added padding for UI elements",
                                 .name = "UI Padding",
                                 .category = "HUD/UI Settings",
                         });
        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.ads_zoom_animation,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable zoom animation when aiming down sights (ADS)",
                                 .name = "ADS zoom animation",
                                 .category = "Weapon Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.disable_corpse_despawn,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Dead player models remain permanently on the map",
                                 .name = "Disable corpse despawn",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.auto_demo_recording,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Automatically record demo files when connecting to a server",
                                 .name = "Auto Demo Recording",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.blood_marks,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Leaves persistent blood decals on surfaces near player hits",
                                 .name = "Blood Marks",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.damage_numbers,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Shows floating damage numbers when you hit a player",
                                 .name = "Damage Numbers",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.player_stats,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Displays player statistics",
                                 .name = "Player stats display",
                                 .category = "HUD/UI Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.player_technical_stats,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                         .help = "Displays technical statistics (particles, vertices)",
                                 .name = "Technical stats display",
                                 .category = "HUD/UI Settings",
                 });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.disable_dynamic_fov,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Disable FOV changes on sprint/crouch and makes crouch instant",
                                 .name = "Disable Dynamic FOV",
                 });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.textured_blocks,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enables multitextured blocks with texture atlas blending",
                                 .name = "Textured Blocks",
                                 .category = "Graphic Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.rain,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable rain weather effect",
                                 .name = "Rain",
                                 .category = "Weather",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.snow,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable snow weather effect",
                                 .name = "Snow",
                                 .category = "Weather",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.rain_snow_3d,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .help = "Enable 3D rain and snow (full cube rendering)",
                                 .name = "3D Rain & Snow",
                                 .category = "Weather",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.exposure,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = -100,
                                 .max = 100,
                                 .help = "Adjust image exposure (-100 to +100)",
                                 .name = "Exposure",
                                 .category = "Graphic Settings",
                                 .subcategory = "Color",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.saturation,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = -100,
                                 .max = 100,
                                 .help = "Adjust image saturation (-100 to +100)",
                                 .name = "Saturation",
                                 .category = "Graphic Settings",
                                 .subcategory = "Color",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.contrast,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = -100,
                                 .max = 100,
                                 .help = "Adjust image contrast (-100 to +100)",
                                 .name = "Contrast",
                                 .category = "Graphic Settings",
                                 .subcategory = "Color",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.vignette,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 0,
                                 .max = 100,
                                 .help = "Darkens edges of the screen (0 to 100)",
                                 .name = "Vignette",
                                 .category = "Graphic Settings",
                                 .subcategory = "Color",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.rifle_ads_fov,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 5,
                                 .max = CAMERA_DEFAULT_FOV,
                                 .help = "Field of View for rifles when aiming down sights",
                                 .name = "Rifle ADS FOV",
                                 .category = "Weapon Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.shotgun_ads_fov,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 5,
                                 .max = CAMERA_DEFAULT_FOV,
                                 .help = "Field of View for shotguns when aiming down sights",
                                 .name = "Shotgun ADS FOV",
                                 .category = "Weapon Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.smg_ads_fov,
                                 .type = CONFIG_TYPE_FLOAT,
                                 .min = 5,
                                 .max = CAMERA_DEFAULT_FOV,
                                 .help = "Field of View for SMGs when aiming down sights",
                                 .name = "SMG ADS FOV",
                                 .category = "Weapon Settings",
                         });

        list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.recording_fps,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 30,
                                 .max = 60,
                                 .name = "Recording FPS",
                                 .help = "Frames per second for video recording",
                                    .category = "Recording & Replay",

                        });
                list_add(&config_settings,
                                 &(struct config_setting) {
                                         .value = &settings_tmp.recording_bitrate_kbps,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 500,
                                 .max = 50000,
                                 .name = "Recording bitrate (kbps)",
                                  .help = "Higher bitrate improves quality but creates larger files",
                                    .category = "Recording & Replay",

                        });
                list_add(&config_settings,
                                 &(struct config_setting) {
                                         .value = &settings_tmp.replay_enabled,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = 1,
                                 .name = "Replay Enabled",
                                 .help = "Continuously record segments for replay",
                                   .category = "Recording & Replay",

                         });
                list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.replay_duration,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 1,
                                 .max = 300,
                                 .name = "Replay Duration (s)",
                                 .help = "Number of seconds of buffer to keep",
                                   .category = "Recording & Replay",

                         });
                list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = &settings_tmp.replay_save_hotkey,
                                 .type = CONFIG_TYPE_INT,
                                 .min = 0,
                                 .max = INT_MAX,
                                 .name = "Replay Save Hotkey",
                                 .help = "Hotkey to save the current replay buffer",
                                   .category = "Recording & Replay",

                         });
                list_add(&config_settings,
                         &(struct config_setting) {
                                 .value = settings_tmp.audio_monitor_source,
                                 .type = CONFIG_TYPE_STRING,
                                 .max = sizeof(settings.audio_monitor_source) - 1,
                                 .name = "Audio Monitor Source",
                                 .help = "PulseAudio monitor source for system audio capture",
                                 .category = "Video Record/Replay",
                         });

}
