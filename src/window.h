
/*
	Copyright (c) 2017-2020 ByteBit

	This file is part of KyroSpades.

	KyroSpades is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) later versions.

	KyroSpades is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#ifndef WINDOW_H
#define WINDOW_H

#include "common.h"
#include <stddef.h>

struct window_instance {
	void* impl;
};

enum {
	WINDOW_PRESS,
	WINDOW_RELEASE,
	WINDOW_REPEAT,
};

enum {
	TOUCH_DOWN,
	TOUCH_MOVE,
	TOUCH_UP,
};

enum window_keys {
	WINDOW_KEY_UNKNOWN,
	WINDOW_KEY_UP,
	WINDOW_KEY_DOWN,
	WINDOW_KEY_LEFT,
	WINDOW_KEY_RIGHT,
	WINDOW_KEY_SNEAK,
	WINDOW_KEY_CROUCH,
	WINDOW_KEY_SPRINT,
	WINDOW_KEY_SPACE,
	WINDOW_KEY_CURSOR_UP,
	WINDOW_KEY_CURSOR_DOWN,
	WINDOW_KEY_CURSOR_LEFT,
	WINDOW_KEY_CURSOR_RIGHT,
	WINDOW_KEY_TAB,
	WINDOW_KEY_ESCAPE,
	WINDOW_KEY_YES,
	WINDOW_KEY_NO,
	WINDOW_KEY_RELOAD,
	WINDOW_KEY_TOOL1,
	WINDOW_KEY_TOOL2,
	WINDOW_KEY_TOOL3,
	WINDOW_KEY_TOOL4,
	WINDOW_KEY_PICKCOLOR,
	WINDOW_KEY_CHAT,
	WINDOW_KEY_F1,
	WINDOW_KEY_F2,
	WINDOW_KEY_F3,
	WINDOW_KEY_F4,
	WINDOW_KEY_F5,
	WINDOW_KEY_F6,
	WINDOW_KEY_F7,
	WINDOW_KEY_F8,
	WINDOW_KEY_F9,
	WINDOW_KEY_F10,
	WINDOW_KEY_F11,
	WINDOW_KEY_F12,
	WINDOW_KEY_CHANGETEAM,
	WINDOW_KEY_CHANGEWEAPON,
	WINDOW_KEY_ENTER,
	WINDOW_KEY_BACKSPACE,
	WINDOW_KEY_MAP,
	WINDOW_KEY_VOLUME_UP,
	WINDOW_KEY_VOLUME_DOWN,
	WINDOW_KEY_V,
	WINDOW_KEY_C,
	WINDOW_KEY_FULLSCREEN,
	WINDOW_KEY_SCREENSHOT,
	WINDOW_KEY_COMMAND,
	WINDOW_KEY_HIDEHUD,
	WINDOW_KEY_LASTTOOL,
	WINDOW_KEY_NETWORKSTATS,
	WINDOW_KEY_SHIFT,
	WINDOW_KEY_SAVE_MAP,
	WINDOW_KEY_SELECT1,
	WINDOW_KEY_SELECT2,
	WINDOW_KEY_SELECT3,
	WINDOW_KEY_HISTORY_PREVIOUS,
	WINDOW_KEY_HISTORY_NEXT,
	WINDOW_KEY_YCLAMP,
	WINDOW_KEY_SWITCH_CAMERA,
	WINDOW_KEY_NEXT_PLAYER,
	WINDOW_KEY_ROLL_CW,
	WINDOW_KEY_ROLL_CCW,
	WINDOW_KEY_DEMO_PAUSE,
	WINDOW_KEY_DEMO_SEEK_BACK,
	WINDOW_KEY_DEMO_SEEK_FWD,
	WINDOW_KEY_DEMO_SPEED_DOWN,
	WINDOW_KEY_DEMO_SPEED_UP,
	WINDOW_KEY_MAP_ZOOM,
	WINDOW_KEY_RECORDING,
	WINDOW_KEY_REPLAY_SAVE,
	/* Editing-only keys used by microui text fields. They are registered
	   without user-facing bindings so every backend routes them through the
	   same input path. */
	WINDOW_KEY_HOME,
	WINDOW_KEY_END,
	WINDOW_KEY_DELETE,
	WINDOW_KEY_A,
	WINDOW_KEY_X,
	/* Keep this last: window_pressed_keys must cover every internal key.
	   The old fixed size of 64 ended at WINDOW_KEY_DEMO_PAUSE, so newer
	   bindings (including Map zoom/X at index 68) wrote past the array and
	   could corrupt button_map, making a keyboard press fire the weapon. */
	WINDOW_KEY_COUNT
};

enum {
	WINDOW_CURSOR_DISABLED,
	WINDOW_CURSOR_ENABLED,
};

enum window_buttons {
	WINDOW_MOUSE_LMB,
	WINDOW_MOUSE_MMB,
	WINDOW_MOUSE_RMB,
};

struct window_finger {
#ifdef USE_SDL
	SDL_FingerID finger;
#else
	int finger;
#endif
	float down_time;
	int full;
	int dragged;
	int long_pressed;
	struct {
		float x, y;
	} start;
	struct {
		float x, y;
	} cur;
};

extern int window_pressed_keys[WINDOW_KEY_COUNT];

/* SDL's real default framebuffer (0 on desktop/Android, non-zero on iOS). */
extern int window_gl_default_framebuffer;

#define WINDOW_NOMOUSELOC -1

void window_textinput(int allow);
/* Whether the on-screen/software keyboard text input is currently active.
   Used by the iOS chat UI to label its Hide/Show keyboard toggle. Returns 0
   on platforms without a soft keyboard. */
int window_textinput_active(void);
float window_time(void);
void window_keyname(int keycode, char* output, size_t length);
const char* window_clipboard(void);
void window_setclipboard(const char* text);
int window_key_down(int key);
int window_super_down(void);
int window_alt_down(void);
int window_shift_down(void);
void window_cursor_hand(int on);
void window_open_url(const char* url);
void window_share_file(const char* path);
void window_mousemode(int mode);
void window_mouseloc(double* x, double* y);
void window_setmouseloc(double x, double y);
void window_swapping(int value);
void window_init(void);
void window_fromsettings(void);
void window_apply(void);
void window_deinit(void);
void window_update(void);
int window_closed(void);
int window_cpucores();
void window_title(char* suffix);

#endif
