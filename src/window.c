
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
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "main.h"
#include "window.h"

/* SDL's real default framebuffer object, captured once in window_init(). 0 on
   desktop/Android, but a non-zero EAGL FBO on iOS. See window_init() + main.c. */
int window_gl_default_framebuffer = 0;
static float mouse_scale_x = 1.0F, mouse_scale_y = 1.0F;

#include "config.h"
#include "hud.h"
#include "camera.h"
#include "player.h"
#include "network.h"

#if defined(USE_SDL) && defined(__ANDROID__)
#include <jni.h>
#endif

int window_shift_down(void) {
#ifdef USE_GLFW
	if(!hud_window || !hud_window->impl) return 0;
	return glfwGetKey(hud_window->impl, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS
		|| glfwGetKey(hud_window->impl, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
#else
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if(!state) return 0;
	return state[SDL_SCANCODE_LSHIFT] || state[SDL_SCANCODE_RSHIFT];
#endif
}

int window_alt_down(void) {
#ifdef USE_GLFW
	if(!hud_window || !hud_window->impl) return 0;
	return glfwGetKey(hud_window->impl, GLFW_KEY_LEFT_ALT) == GLFW_PRESS
		|| glfwGetKey(hud_window->impl, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
#else
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if(!state) return 0;
	return state[SDL_SCANCODE_LALT] || state[SDL_SCANCODE_RALT];
#endif
}

int window_super_down(void) {
#ifdef USE_GLFW
	if(!hud_window || !hud_window->impl) return 0;
	return glfwGetKey(hud_window->impl, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
		|| glfwGetKey(hud_window->impl, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
#else
	const Uint8* state = SDL_GetKeyboardState(NULL);
	if(!state) return 0;
	return state[SDL_SCANCODE_LGUI] || state[SDL_SCANCODE_RGUI];
#endif
}

void hud_ingame_mouseclick(double x, double y, int button, int action, int mods);
extern float camera_rot_x, camera_rot_y;
extern void camera_overflow_adjust(void);

#ifdef OS_WINDOWS
#include <sysinfoapi.h>
#include <windows.h>
#include <shellapi.h>
#endif

#ifdef OS_LINUX
#include <unistd.h>
#endif

#ifdef OS_APPLE
#include <unistd.h>
#endif

#ifdef OS_HAIKU
#include <kernel/OS.h>
#endif

/* Names for keys whose backend key-name API can't resolve them (arrows and
   other non-printable keys).  Keyed on the internal WINDOW_KEY_* enum so it
   is independent of the SDL/GLFW backend. */
static const char* window_internal_keyname(int internal) {
	switch(internal) {
		case WINDOW_KEY_CURSOR_LEFT:
		case WINDOW_KEY_LEFT:            return "Left Arrow";
		case WINDOW_KEY_CURSOR_RIGHT:
		case WINDOW_KEY_RIGHT:           return "Right Arrow";
		case WINDOW_KEY_CURSOR_UP:
		case WINDOW_KEY_UP:              return "Up Arrow";
		case WINDOW_KEY_CURSOR_DOWN:
		case WINDOW_KEY_DOWN:            return "Down Arrow";
		case WINDOW_KEY_F1:              return "F1";
		case WINDOW_KEY_F2:              return "F2";
		case WINDOW_KEY_F3:              return "F3";
		case WINDOW_KEY_F4:              return "F4";
		case WINDOW_KEY_F5:              return "F5";
		case WINDOW_KEY_F6:              return "F6";
		case WINDOW_KEY_F7:              return "F7";
		case WINDOW_KEY_F8:              return "F8";
		case WINDOW_KEY_F9:              return "F9";
		case WINDOW_KEY_F10:             return "F10";
		case WINDOW_KEY_F11:             return "F11";
		case WINDOW_KEY_F12:             return "F12";
		case WINDOW_KEY_SCREENSHOT:      return "F5";
		case WINDOW_KEY_HIDEHUD:         return "F6";
		case WINDOW_KEY_SAVE_MAP:        return "F8";
		case WINDOW_KEY_FULLSCREEN:      return "F11";
		case WINDOW_KEY_NETWORKSTATS:    return "F12";
		case WINDOW_KEY_DEMO_SEEK_BACK:  return "Left Arrow";
		case WINDOW_KEY_DEMO_SEEK_FWD:   return "Right Arrow";
		case WINDOW_KEY_DEMO_SPEED_DOWN: return "-";
		case WINDOW_KEY_DEMO_SPEED_UP:   return "=";
		default:                         return NULL;
	}
}

static int window_pending_apply = 0;
static int pending_multisamples;
static int pending_vsync;
static int pending_fullscreen;
static int pending_width;
static int pending_height;
/* Last known non-fullscreen window size (points), used to return to the
   right size when leaving fullscreen - reshape() clobbers
   settings.window_width/height with the fullscreen drawable size. */
static int windowed_width = 0;
static int windowed_height = 0;

#ifdef USE_GLFW

static bool joystick_available = false;
static int joystick_id;
static float joystick_mouse[2] = {0, 0};
static GLFWgamepadstate joystick_state;

static void window_impl_joystick(int jid, int event) {
	if(event == GLFW_CONNECTED) {
		joystick_available = true;
		joystick_id = jid;
		log_info("Joystick detected: %s", glfwGetJoystickName(joystick_id));
	} else if(event == GLFW_DISCONNECTED) {
		joystick_available = false;
		log_info("Joystick removed: %s", glfwGetJoystickName(joystick_id));
	}
}

void window_textinput(int allow) { }

void window_setmouseloc(double x, double y) { }

static void window_impl_mouseclick(GLFWwindow* window, int button, int action, int mods) {
	int b = 0;
	switch(button) {
		case GLFW_MOUSE_BUTTON_LEFT: b = WINDOW_MOUSE_LMB; break;
		case GLFW_MOUSE_BUTTON_RIGHT: b = WINDOW_MOUSE_RMB; break;
		case GLFW_MOUSE_BUTTON_MIDDLE: b = WINDOW_MOUSE_MMB; break;
	}

	int a = -1;
	switch(action) {
		case GLFW_RELEASE: a = WINDOW_RELEASE; break;
		case GLFW_PRESS: a = WINDOW_PRESS; break;
	}

	if(a >= 0)
		mouse_click(hud_window, b, a, mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER));
}
static void window_impl_mouse(GLFWwindow* window, double x, double y) {
	if(!joystick_available)
		mouse(hud_window, x, y);
}
static void window_impl_mousescroll(GLFWwindow* window, double xoffset, double yoffset) {
	mouse_scroll(hud_window, xoffset, yoffset);
}
static void window_impl_error(int i, const char* s) {
	on_error(i, s);
}
static void window_impl_reshape(GLFWwindow* window, int width, int height) {
	reshape(hud_window, width, height);
}
static void window_impl_textinput(GLFWwindow* window, unsigned int codepoint) {
	char buf[5];
	int n = 0;
	if(codepoint < 0x80) { buf[n++] = (char)codepoint; }
	else if(codepoint < 0x800) {
		buf[n++] = 0xC0 | (codepoint >> 6);
		buf[n++] = 0x80 | (codepoint & 0x3F);
	} else if(codepoint < 0x10000) {
		buf[n++] = 0xE0 | (codepoint >> 12);
		buf[n++] = 0x80 | ((codepoint >> 6) & 0x3F);
		buf[n++] = 0x80 | (codepoint & 0x3F);
	} else if(codepoint < 0x110000) {
		buf[n++] = 0xF0 | (codepoint >> 18);
		buf[n++] = 0x80 | ((codepoint >> 12) & 0x3F);
		buf[n++] = 0x80 | ((codepoint >> 6) & 0x3F);
		buf[n++] = 0x80 | (codepoint & 0x3F);
	}
	buf[n] = 0;
	text_input(hud_window, buf);
}
static void window_impl_keys(GLFWwindow* window, int key, int scancode, int action, int mods) {
	int count = config_key_translate(key, 0, NULL);

	int a = -1;
	switch(action) {
		case GLFW_RELEASE: a = WINDOW_RELEASE; break;
		case GLFW_PRESS: a = WINDOW_PRESS; break;
		case GLFW_REPEAT: a = WINDOW_REPEAT; break;
	}

	/* Cocoa occasionally drops GLFW_MOD_SUPER from the mods bitmask on
	   Cmd-modified key events, breaking Cmd+C on macOS. Re-derive it from
	   live key state so downstream handlers see a stable modifier bit. */
	if(glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS
	   || glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
		mods |= GLFW_MOD_SUPER;

#ifdef WINDOW_KEY_DEBUG
	log_debug("key=%d scancode=%d action=%d mods=0x%x (CTRL=%d SUPER=%d)",
			  key, scancode, action, mods,
			  !!(mods & GLFW_MOD_CONTROL), !!(mods & GLFW_MOD_SUPER));
#endif

	if(count > 0) {
		int results[count];
		config_key_translate(key, 0, results);

		for(int k = 0; k < count; k++) {
			keys(hud_window, results[k], scancode, a, mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER));

			if(hud_active->input_keyboard)
				hud_active->input_keyboard(results[k], action, mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER), key);
		}
	} else {
		if(hud_active->input_keyboard)
			hud_active->input_keyboard(WINDOW_KEY_UNKNOWN, action, mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER), key);
	}
}

void window_keyname(int keycode, char* output, size_t length) {
#ifdef OS_WINDOWS
	if(glfwGetKeyScancode(keycode) > 0) {
		GetKeyNameTextA(glfwGetKeyScancode(keycode) << 16, output, length);
		if(output[0] && strcmp(output, "?")) return;
	}
#else
	const char* name = glfwGetKeyName(keycode, 0);
	if(name && *name) {
		strncpy(output, name, length);
		output[length - 1] = 0;
		return;
	}
#endif
	if(keycode >= GLFW_KEY_F1 && keycode <= GLFW_KEY_F12) {
		snprintf(output, length, "F%d", keycode - GLFW_KEY_F1 + 1);
		output[length - 1] = 0;
		return;
	}

	/* Backend couldn't name it — fall back to our internal key labels for
	   non-printable keys (arrows, demo controls, etc.). */
	{
		int results[8];
		int count = config_key_translate(keycode, 0, results);
		for(int i = 0; i < count; i++) {
			const char* fb = window_internal_keyname(results[i]);
			if(fb) { strncpy(output, fb, length); output[length - 1] = 0; return; }
		}
	}
	if(length >= 2) strcpy(output, "?");
}

float window_time() {
	return glfwGetTime();
}

int window_pressed_keys[WINDOW_KEY_COUNT] = {0};

const char* window_clipboard() {
	return glfwGetClipboardString(hud_window->impl);
}

void window_setclipboard(const char* text) {
	if(text)
		glfwSetClipboardString(hud_window->impl, text);
}

int window_key_down(int key) {
	return window_pressed_keys[key];
}


static GLFWcursor* window_hand_cursor = NULL;
void window_cursor_hand(int on) {
	if(!hud_window || !hud_window->impl) return;
	if(on && !window_hand_cursor)
		window_hand_cursor = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
	glfwSetCursor(hud_window->impl, on ? window_hand_cursor : NULL);
}

void window_mousemode(int mode) {
	int s = glfwGetInputMode(hud_window->impl, GLFW_CURSOR);
	if((s == GLFW_CURSOR_DISABLED && mode == WINDOW_CURSOR_ENABLED)
	   || (s == GLFW_CURSOR_NORMAL && mode == WINDOW_CURSOR_DISABLED))
		glfwSetInputMode(hud_window->impl, GLFW_CURSOR,
						 mode == WINDOW_CURSOR_ENABLED ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

void window_mouseloc(double* x, double* y) {
	glfwGetCursorPos(hud_window->impl, x, y);
}

void window_swapping(int value) {
	glfwSwapInterval(value);
}

void window_title(char* suffix) {
	if(suffix) {
		char title[128];
		snprintf(title, sizeof(title) - 1, "KyroSpades %s - %s", GIT_COMMIT_HASH, suffix);
		glfwSetWindowTitle(hud_window->impl, title);
	} else {
		glfwSetWindowTitle(hud_window->impl, "KyroSpades " GIT_COMMIT_HASH);
	}
}

void window_init() {
	static struct window_instance i;
	hud_window = &i;

	glfwWindowHint(GLFW_VISIBLE, 0);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 1);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
#ifdef OPENGL_ES
	glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
	glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
#endif

	glfwSetErrorCallback(window_impl_error);

	if(!glfwInit()) {
		log_fatal("GLFW3 init failed");
		exit(1);
	}

	if(settings.multisamples > 0) {
		glfwWindowHint(GLFW_SAMPLES, settings.multisamples);
	}

	/*
	#FIXME: This is intended to fix the issue #145.
	This is dirty because it disables the application-level Hi-DPI support for every installation
	instead of being applied only to those who needs it.
	*/
	glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, GLFW_FALSE);

	hud_window->impl
		= glfwCreateWindow(settings.window_width, settings.window_height, "KyroSpades " KYROSPADES_VERSION,
						   settings.fullscreen ? glfwGetPrimaryMonitor() : NULL, NULL);
	if(!hud_window->impl) {
		log_fatal("Could not open window");
		glfwTerminate();
		exit(1);
	}

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	glfwSetWindowPos(hud_window->impl, (mode->width - settings.window_width) / 2.0F,
					 (mode->height - settings.window_height) / 2.0F);
	glfwShowWindow(hud_window->impl);

	glfwMakeContextCurrent(hud_window->impl);

	glfwSetFramebufferSizeCallback(hud_window->impl, window_impl_reshape);
	glfwSetCursorPosCallback(hud_window->impl, window_impl_mouse);
	glfwSetKeyCallback(hud_window->impl, window_impl_keys);
	glfwSetMouseButtonCallback(hud_window->impl, window_impl_mouseclick);
	glfwSetScrollCallback(hud_window->impl, window_impl_mousescroll);
	glfwSetCharCallback(hud_window->impl, window_impl_textinput);
	glfwSetJoystickCallback(window_impl_joystick);

	if(!settings.disable_raw_input && glfwRawMouseMotionSupported())
		glfwSetInputMode(hud_window->impl, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
}

void window_fromsettings() {
	pending_multisamples = settings.multisamples;
	pending_vsync = settings.vsync;
	pending_fullscreen = settings.fullscreen;
	pending_width = settings.window_width;
	pending_height = settings.window_height;
	window_pending_apply = 1;
}

void window_apply() {
	if(!window_pending_apply) return;
	window_pending_apply = 0;

	glfwWindowHint(GLFW_SAMPLES, pending_multisamples);

	if(pending_vsync < 2)
		window_swapping(pending_vsync);
	if(pending_vsync > 1)
		window_swapping(0);

	const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
	if(pending_fullscreen) {
		/* Remember the windowed size first: reshape() overwrites
		   settings.window_width/height (and thus pending_*) with the
		   fullscreen size, so it can't be recovered on exit otherwise. */
		if(!glfwGetWindowMonitor(hud_window->impl))
			glfwGetWindowSize(hud_window->impl, &windowed_width, &windowed_height);
		glfwSetWindowMonitor(hud_window->impl, glfwGetPrimaryMonitor(), 0, 0,
							 mode->width, mode->height, mode->refreshRate);
	} else {
		int w = windowed_width > 0 ? windowed_width : pending_width;
		int h = windowed_height > 0 ? windowed_height : pending_height;
		glfwSetWindowMonitor(hud_window->impl, NULL, (mode->width - w) / 2,
							 (mode->height - h) / 2, w, h, 0);
	}
}

void window_deinit() {
	glfwTerminate();
}

static void gamepad_translate_key(GLFWgamepadstate* state, GLFWgamepadstate* old, int gamepad, enum window_keys key) {
	if(!old->buttons[gamepad] && state->buttons[gamepad]) {
		keys(hud_window, key, 0, WINDOW_PRESS, 0);

		if(hud_active->input_keyboard)
			hud_active->input_keyboard(key, WINDOW_PRESS, 0, 0);
	} else if(old->buttons[gamepad] && !state->buttons[gamepad]) {
		keys(hud_window, key, 0, WINDOW_RELEASE, 0);

		if(hud_active->input_keyboard)
			hud_active->input_keyboard(key, WINDOW_RELEASE, 0, 0);
	}
}

static void gamepad_translate_button(GLFWgamepadstate* state, GLFWgamepadstate* old, int gamepad,
									   enum window_buttons button) {
	if(!old->buttons[gamepad] && state->buttons[gamepad]) {
		mouse_click(hud_window, button, WINDOW_PRESS, 0);
	} else if(old->buttons[gamepad] && !state->buttons[gamepad]) {
		mouse_click(hud_window, button, WINDOW_RELEASE, 0);
	}
}

void window_update() {
	glfwSwapBuffers(hud_window->impl);
	glfwPollEvents();

	if(joystick_available && glfwJoystickIsGamepad(joystick_id)) {
		GLFWgamepadstate state;

		if(glfwGetGamepadState(joystick_id, &state)) {
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_DPAD_UP, WINDOW_KEY_TOOL1);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_DPAD_DOWN, WINDOW_KEY_TOOL3);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_DPAD_LEFT, WINDOW_KEY_TOOL4);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, WINDOW_KEY_TOOL2);

			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_START, WINDOW_KEY_ESCAPE);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, WINDOW_KEY_SPACE);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_LEFT_THUMB, WINDOW_KEY_CROUCH);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, WINDOW_KEY_SPRINT);
			gamepad_translate_key(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_X, WINDOW_KEY_RELOAD);

			window_pressed_keys[WINDOW_KEY_UP] = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] < -0.25F;
			window_pressed_keys[WINDOW_KEY_DOWN] = state.axes[GLFW_GAMEPAD_AXIS_LEFT_Y] > 0.25F;
			window_pressed_keys[WINDOW_KEY_LEFT] = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] < -0.25F;
			window_pressed_keys[WINDOW_KEY_RIGHT] = state.axes[GLFW_GAMEPAD_AXIS_LEFT_X] > 0.25F;

			joystick_mouse[0] += state.axes[GLFW_GAMEPAD_AXIS_RIGHT_X] * 15.0F;
			joystick_mouse[1] += state.axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] * 15.0F;
			mouse(hud_window, joystick_mouse[0], joystick_mouse[1]);

			gamepad_translate_button(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_A, WINDOW_MOUSE_LMB);
			gamepad_translate_button(&state, &joystick_state, GLFW_GAMEPAD_BUTTON_B, WINDOW_MOUSE_RMB);
		}

		joystick_state = state;
	}
}

int window_closed() {
	return glfwWindowShouldClose(hud_window->impl);
}

#endif

#ifdef USE_SDL

void window_textinput(int allow) {
	if(allow && !SDL_IsTextInputActive())
		SDL_StartTextInput();
	if(!allow && SDL_IsTextInputActive())
		SDL_StopTextInput();
}

int window_textinput_active(void) {
	return SDL_IsTextInputActive() ? 1 : 0;
}

void window_fromsettings() {
	pending_multisamples = settings.multisamples;
	pending_vsync = settings.vsync;
	pending_fullscreen = settings.fullscreen;
	pending_width = settings.window_width;
	pending_height = settings.window_height;
	window_pending_apply = 1;
}

#ifdef __ANDROID__
/* Hide/show only the top status bar via KyroSpadesActivity.setStatusBarHidden,
   which keeps the bottom navigation bar (and its Escape-mapped back button).
   SDL's own fullscreen hides both bars, which traps the user in the menu. */
static void android_set_status_bar_hidden(int hidden) {
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if(!env || !activity)
		return;
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID mid = (*env)->GetStaticMethodID(env, cls, "setStatusBarHidden", "(Z)V");
	if(mid)
		(*env)->CallStaticVoidMethod(env, cls, mid, (jboolean)(hidden ? 1 : 0));
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
}
#endif

void window_share_file(const char* path) {
#ifdef __ANDROID__
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if(!env || !activity)
		return;
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID mid = (*env)->GetStaticMethodID(env, cls, "shareFile", "(Ljava/lang/String;)V");
	if(mid) {
		jstring jpath = (*env)->NewStringUTF(env, path);
		(*env)->CallStaticVoidMethod(env, cls, mid, jpath);
		(*env)->DeleteLocalRef(env, jpath);
	}
	if((*env)->ExceptionCheck(env))
		(*env)->ExceptionClear(env);
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
#else
	(void)path;
#endif
}

void window_apply() {
	if(!window_pending_apply) return;
	window_pending_apply = 0;

	if(pending_vsync < 2)
		window_swapping(pending_vsync);
	if(pending_vsync > 1)
		window_swapping(0);

	if(pending_fullscreen) {
#ifdef __ANDROID__
		android_set_status_bar_hidden(1);
#else
		/* Capture the current windowed size before switching, so we can
		   restore it on exit. */
		if(!(SDL_GetWindowFlags(hud_window->impl) & SDL_WINDOW_FULLSCREEN_DESKTOP))
			SDL_GetWindowSize(hud_window->impl, &windowed_width, &windowed_height);
		SDL_SetWindowFullscreen(hud_window->impl, SDL_WINDOW_FULLSCREEN_DESKTOP);
#endif
	} else {
#ifdef __ANDROID__
		android_set_status_bar_hidden(0);
#else
		SDL_SetWindowFullscreen(hud_window->impl, 0);
		if(windowed_width > 0 && windowed_height > 0)
			SDL_SetWindowSize(hud_window->impl, windowed_width, windowed_height);
		else
			SDL_SetWindowSize(hud_window->impl, pending_width, pending_height);
#endif
	}
}

void window_keyname(int keycode, char* output, size_t length) {
	/* Ask SDL first — it knows the human name for every printable key, plus
	   named non-printables ("Left", "Right", "Escape", etc.).  Only fall back
	   to our internal labels when SDL genuinely can't help. */
	const char* nm = SDL_GetKeyName(keycode);
	if(nm && *nm && strcmp(nm, "Unknown Key")) {
		strncpy(output, nm, length);
		output[length - 1] = 0;
		return;
	}
	/* Last resort: our label table for keys SDL can't name. */
	{
		int results[8];
		int count = config_key_translate(keycode, 0, results);
		for(int i = 0; i < count; i++) {
			const char* fb = window_internal_keyname(results[i]);
			if(fb) { strncpy(output, fb, length); output[length - 1] = 0; return; }
		}
	}
	if(length >= 2) strcpy(output, "?");
}

float window_time() {
	return ((double)SDL_GetTicks()) / 1000.0F;
}

int window_pressed_keys[WINDOW_KEY_COUNT] = {0};

const char* window_clipboard() {
	return SDL_HasClipboardText() ? SDL_GetClipboardText() : NULL;
}

void window_setclipboard(const char* text) {
	if(text)
		SDL_SetClipboardText(text);
}

int window_key_down(int key) {
	return window_pressed_keys[key];
}

static SDL_Cursor* window_hand_cursor = NULL;
void window_cursor_hand(int on) {
	if(on && !window_hand_cursor)
		window_hand_cursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
	SDL_SetCursor(on ? window_hand_cursor : SDL_GetDefaultCursor());
}
void window_mousemode(int mode) {
	int s = SDL_GetRelativeMouseMode();
	if((s && mode == WINDOW_CURSOR_ENABLED) || (!s && mode == WINDOW_CURSOR_DISABLED))
		SDL_SetRelativeMouseMode(mode == WINDOW_CURSOR_ENABLED ? SDL_FALSE : SDL_TRUE);
}

static double mx = -1, my = -1;

void window_setmouseloc(double x, double y) {
	mx = x;
	my = y;
}

void window_mouseloc(double* x, double* y) {
	if(mx < 0 && my < 0) {
		int xi, yi;
		SDL_GetMouseState(&xi, &yi);
		*x = xi;
		*y = yi;
	} else {
		*x = mx;
		*y = my;
	}
}

void window_swapping(int value) {
	SDL_GL_SetSwapInterval(value);
}

static struct window_finger fingers[8];

static void window_dispatch_key(int sym, int action, int mod) {
	int count = config_key_translate(sym, 0, NULL);

	if(count > 0) {
		int results[count];
		config_key_translate(sym, 0, results);

		for(int k = 0; k < count; k++) {
			keys(hud_window, results[k], sym, action, mod);

			if(hud_active->input_keyboard)
				hud_active->input_keyboard(results[k], action, mod, sym);
		}
	} else {
		if(hud_active->input_keyboard)
			hud_active->input_keyboard(WINDOW_KEY_UNKNOWN, action, mod, sym);
	}
}

void window_init() {
	static struct window_instance i;
	hud_window = &i;

#ifdef USE_TOUCH
	SDL_SetHintWithPriority(SDL_HINT_MOUSE_TOUCH_EVENTS, "1", SDL_HINT_OVERRIDE);
#endif

#if defined(OS_IOS)
	/* Hide the home indicator (value "2"). The real reason this is here isn't
	   cosmetics: SDL's iOS view controller ties
	   preferredScreenEdgesDeferringSystemGestures to this same hint, so hiding
	   the indicator also tells UIKit to DEFER its bottom-edge system gesture.
	   Without it, UIKit holds back any touch that STARTS in the bottom ~20pt
	   strip for a few hundred ms to disambiguate it from a home-swipe — which
	   is exactly why the Crouch/Jump plates (hard against the bottom edge) felt
	   like they needed a long press before they "took". With the gesture
	   deferred, the first touch is delivered to the app immediately and the
	   buttons fire on contact. Must be set before window creation. */
	SDL_SetHintWithPriority(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2", SDL_HINT_OVERRIDE);
#endif

	/* Landscape-only on Android: allow both landscape directions (180-degree
	   flip) but never portrait. Must be set before SDL_CreateWindow; no effect
	   on desktop. Pair with android:screenOrientation="sensorLandscape" in
	   AndroidManifest.xml. */
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");

	SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER);

	/* GL attributes MUST be set before SDL_CreateWindow: the EGLConfig /
	   pixel format (color sizes, depth size and, crucially, the renderable
	   type ES1 vs ES2) is chosen at window creation time. Setting them after
	   the window exists makes the context/config pairing driver-dependent.
	   Try ES 2.0 first; fall back to ES 1.1 if context creation fails. */
	int es_major = 2, es_minor = 0;
#ifdef OPENGL_ES
retry_context:
#endif
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, es_major);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, es_minor);
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16);
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
#ifdef OPENGL_ES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
#endif

	/* ALLOW_HIGHDPI is needed on iOS/Android where the native pixel density is
	   part of the expected UX. On macOS desktop it makes the framebuffer 2x
	   (Retina) while the UI was designed for logical points, so everything
	   renders at half size and mouse coords are off. Let macOS scale for us. */
	Uint32 win_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
#if defined(__ANDROID__) || defined(OS_IOS)
	win_flags |= SDL_WINDOW_ALLOW_HIGHDPI;
#endif

	hud_window->impl
		= SDL_CreateWindow("KyroSpades " KYROSPADES_VERSION, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
						   settings.window_width, settings.window_height,
						   win_flags);

	SDL_GLContext ctx = SDL_GL_CreateContext(hud_window->impl);
	if(!ctx) {
#ifdef OPENGL_ES
		if(es_major == 2) {
			log_warn("GLES 2.0 context failed (%s), trying GLES 1.1 fallback", SDL_GetError());
			SDL_DestroyWindow(hud_window->impl);
			es_major = 1;
			es_minor = 1;
			goto retry_context;
		}
#endif
		log_error("SDL_GL_CreateContext failed: %s", SDL_GetError());
	}
#ifdef OPENGL_ES
	gles_version = es_major;
	log_info("OpenGL ES context: %d.%d", es_major, es_minor);
#endif

	/* The actual drawable size can differ from the requested window size
	   (Android renders fullscreen at native resolution; HighDPI desktops
	   scale). reshape() only corrects this on SDL resize events, which never
	   arrive when the app launches already in its final orientation, so the
	   viewport must be queried and set here before the first frame. Do NOT
	   call reshape() for this: it calls font_reset(), and the font hashtable
	   isn't initialized yet at this point in startup. */
	int drawable_w = 0, drawable_h = 0;
	SDL_GL_GetDrawableSize(hud_window->impl, &drawable_w, &drawable_h);
	if(drawable_w > 0 && drawable_h > 0) {
		settings.window_width = drawable_w;
		settings.window_height = drawable_h;
		glViewport(0, 0, drawable_w, drawable_h);
		int win_w = 0, win_h = 0;
		SDL_GetWindowSize(hud_window->impl, &win_w, &win_h);
		if(win_w > 0 && win_h > 0) {
			mouse_scale_x = (float)drawable_w / (float)win_w;
			mouse_scale_y = (float)drawable_h / (float)win_h;
		}
	}

	/* Capture SDL's real default framebuffer.
	   ──────────────────────────────────────────────────────────────────────
	   Desktop/Android (EGL): the window's default FBO is object 0 and is
	     bound right after CreateContext, so a single glGetIntegerv here gives
	     the correct answer (0) and we're done.
	   iOS (UIKit/EAGL): SDL creates a NON-zero EAGL viewFramebuffer LAZILY,
	     during the first SDL_GL_SwapWindow. So at this point the query
	     returns 0, which on iOS is *off-screen*. v12 captured this 0 and
	     every later "return to screen" bound 0 → invisible game world.
	     Force the lazy creation by performing one swap now, then re-query.
	     The black frame is hidden behind iOS's launch image; nothing visible
	     happens before the main loop renders the first real frame.
	   We then keep a non-zero guard: window_update re-captures after every
	   swap so the value tracks any FBO recreation (e.g. orientation change),
	   but never overwrites with 0. */
	{
		GLint fb = -1;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		log_info("FBO at CreateContext = %d, drawable = %dx%d", (int)fb, drawable_w, drawable_h);
		if(fb > 0) window_gl_default_framebuffer = (int)fb;
#if defined(OS_IOS)
		/* Lazy-init kick: swap once to materialise SDL's viewFramebuffer. */
		glClearColor(0.0F, 0.0F, 0.0F, 1.0F);
		glClear(GL_COLOR_BUFFER_BIT);
		SDL_GL_SwapWindow(hud_window->impl);
		fb = -1;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		log_info("FBO after forced initial swap = %d", (int)fb);
		if(fb > 0) window_gl_default_framebuffer = (int)fb;
#endif
	}

	memset(fingers, 0, sizeof(fingers));
}

static struct window_finger* aim_finger = NULL;
static struct window_finger* aim_finger2 = NULL;

static int window_aim_zone(float x, float y) {
	/* Exclude the right-side action buttons — but only the ROWS they actually
	   occupy, not the whole right column. The old full-height strip meant you
	   could never start a camera drag from the lower-right corner (e.g. with a
	   weapon held), which was the reported bug. In FPS the buttons are LMB
	   (GL y 0.6H) and RMB (0.45H); in spectator it's the Cam toggle (0.3H).
	   x,y are screen coords (y DOWN); gy converts to GL (y UP). */
	if(x > settings.window_width - settings.window_height * 0.225F) {
		float gy = settings.window_height - y;
		if(camera_mode == CAMERAMODE_SPECTATOR) {
			/* Cam (0.3H) AND the LMB/RMB plates (0.45H/0.6H): the plates are
			   drawn and hit-tested in spectator too (RMB tap = cycle spectated
			   player), so they must not be eaten by the aim zone. Contiguous
			   band Cam bottom → LMB top. */
			if(gy > settings.window_height * 0.225F && gy < settings.window_height * 0.675F)
				return 0; /* Cam .. RMB .. LMB */
		} else {
			if(gy > settings.window_height * 0.375F && gy < settings.window_height * 0.675F)
				return 0; /* RMB .. LMB */
		}
	}
	/* Exclude the bottom-left movement joystick. */
	if(x < settings.window_width * 0.35F && y > settings.window_height * 0.45F)
		return 0;
	/* Exclude the top menu bar. */
	if(y < settings.window_height * 0.18F)
		return 0;
	/* Exclude the centered bottom block-colour palette ONLY while the block
	   tool is held in FPS. Fractions mirror hud.c's palette helpers. x,y are
	   screen coordinates (y DOWN), so convert the GL-space vertical bounds. */
	if(camera_mode == CAMERAMODE_FPS && players[local_player_id].held_item == TOOL_BLOCK) {
		float psize = settings.window_height * 0.024F * 8.0F;
		float pleft = (settings.window_width - psize) * 0.5F;
		float pright = pleft + psize;
		float pbottom_gl = settings.window_height * 0.045F;
		float ptop_sc = settings.window_height - (pbottom_gl + psize);
		float pbot_sc = settings.window_height - pbottom_gl;
		if(x >= pleft && x <= pright && y >= ptop_sc && y <= pbot_sc)
			return 0;
	}
	return 1;
}

void window_deinit() {
	SDL_DestroyWindow(hud_window->impl);
	SDL_Quit();
}

static int quit = 0;
void window_update() {
	SDL_GL_SwapWindow(hud_window->impl);

#if defined(OS_IOS)
	/* SDL's iOS GL backend calls bindBackbuffer inside SwapWindow, which
	   re-binds viewFramebuffer to GL_FRAMEBUFFER. This is THE most reliable
	   moment in the frame to read SDL's current default FBO id: state at
	   the top of display() can drift (rotation, layer resize, multitasking
	   restore) and v12's capture inside the render block was empirically
	   not reliable enough to fix the off-screen-render bug.
	   Non-zero guard: if for any reason this read returns 0, KEEP the last
	   good value — binding 0 on iOS is off-screen. */
	{
		GLint fb = -1;
		glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fb);
		if(fb > 0 && fb != window_gl_default_framebuffer) {
			log_info("default FBO id changed: %d -> %d",
					 window_gl_default_framebuffer, (int)fb);
			window_gl_default_framebuffer = (int)fb;
		} else if(fb > 0) {
			window_gl_default_framebuffer = (int)fb;
		}
	}
#endif

	/* Reshape from the real drawable if it drifted from the framebuffer
	   (fullscreen toggle / orientation change) so the viewport and HUD agree. */
	{
		int dw = 0, dh = 0;
		SDL_GL_GetDrawableSize(hud_window->impl, &dw, &dh);
		if(dw > 0 && dh > 0
		   && (dw != settings.window_width || dh != settings.window_height))
			reshape(hud_window, dw, dh);
	}

	SDL_Event event;
	while(SDL_PollEvent(&event)) {
		switch(event.type) {
			case SDL_QUIT: quit = 1; break;
			case SDL_KEYDOWN:
				window_dispatch_key(event.key.keysym.sym, WINDOW_PRESS,
									 event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI));
				break;
			case SDL_KEYUP:
				window_dispatch_key(event.key.keysym.sym, WINDOW_RELEASE,
									 event.key.keysym.mod & (KMOD_CTRL | KMOD_GUI));
				break;
			case SDL_MOUSEBUTTONDOWN: {
				/* Touch is driven directly from the FINGER events below (with
				   tap/drag discrimination), so ignore SDL's synthetic mouse for
				   touch entirely. Real mouse/trackpad still works. */
				if(event.button.which == SDL_TOUCH_MOUSEID) break;
				int a = 0;
				switch(event.button.button) {
					case SDL_BUTTON_LEFT: a = WINDOW_MOUSE_LMB; break;
					case SDL_BUTTON_RIGHT: a = WINDOW_MOUSE_RMB; break;
					case SDL_BUTTON_MIDDLE: a = WINDOW_MOUSE_MMB; break;
				}
				mouse_click(hud_window, a, WINDOW_PRESS, 0);
				break;
			}
			case SDL_MOUSEBUTTONUP: {
				if(event.button.which == SDL_TOUCH_MOUSEID) break;
				int a = 0;
				switch(event.button.button) {
					case SDL_BUTTON_LEFT: a = WINDOW_MOUSE_LMB; break;
					case SDL_BUTTON_RIGHT: a = WINDOW_MOUSE_RMB; break;
					case SDL_BUTTON_MIDDLE: a = WINDOW_MOUSE_MMB; break;
				}
				mouse_click(hud_window, a, WINDOW_RELEASE, 0);
				break;
			}
			case SDL_WINDOWEVENT:
				if(event.window.event == SDL_WINDOWEVENT_RESIZED
				   || event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
					/* data1/data2 are window coordinates; reshape from the
					   drawable pixels, which differ on HighDPI / Android. */
					int dw = 0, dh = 0;
				SDL_GL_GetDrawableSize(hud_window->impl, &dw, &dh);
				if(dw <= 0 || dh <= 0) { dw = event.window.data1; dh = event.window.data2; }
				if(event.window.data1 > 0 && event.window.data2 > 0) {
					mouse_scale_x = (float)dw / (float)event.window.data1;
					mouse_scale_y = (float)dh / (float)event.window.data2;
				}
				reshape(hud_window, dw, dh);
				}
				break;
			case SDL_MOUSEWHEEL:
				if(event.wheel.which == SDL_TOUCH_MOUSEID && hud_active == &hud_ingame) break; /* drop touch-synth only ingame */
				mouse_scroll(hud_window, event.wheel.x, event.wheel.y); break;
			case SDL_MOUSEMOTION: {
				if(event.motion.which == SDL_TOUCH_MOUSEID && hud_active == &hud_ingame) break; /* drop touch-synth only ingame */
				if(SDL_GetRelativeMouseMode()) {
					/* Preserve fractional drawable-scaled motion. The old integer
					   accumulator truncated every individual SDL event before it
					   reached the camera. Small/slow movements were therefore lost
					   and later arrived as visible one-pixel steps (especially with
					   High-DPI scaling), which felt much less obvious at half-speed
					   ADS sensitivity. */
					static double x, y;
					x += (double)event.motion.xrel * mouse_scale_x;
					y += (double)event.motion.yrel * mouse_scale_y;
					mouse(hud_window, x, y);
				} else {
					mouse(hud_window, event.motion.x * mouse_scale_x, event.motion.y * mouse_scale_y);
				}
				break;
			}
			case SDL_TEXTINPUT: text_input(hud_window, event.text.text); break;
			case SDL_FINGERDOWN: {
				struct window_finger* f = NULL;
				float fx = event.tfinger.x * settings.window_width;
				float fy = event.tfinger.y * settings.window_height;
				for(int k = 0; k < 8; k++) {
					if(!fingers[k].full) {
						fingers[k].finger = event.tfinger.fingerId;
						fingers[k].start.x = fx;
						fingers[k].start.y = fy;
						fingers[k].cur.x = fx;
						fingers[k].cur.y = fy;
						fingers[k].down_time = window_time();
						fingers[k].dragged = 0;
						fingers[k].long_pressed = 0;
						fingers[k].full = 1;
						f = fingers + k;
						break;
					}
				}

				/* While a selection overlay (team/gun select) is open, the whole
				   screen belongs to the overlay's tap targets: capturing the
				   finger for camera aim here would swallow the TOUCH_UP and make
				   most of the overlay unclickable. */
				if(hud_active == &hud_ingame && screen_current == SCREEN_NONE && window_aim_zone(fx, fy)) {
					if(!aim_finger) {
						aim_finger = f;
						break;
					} else if(!aim_finger2) {
						aim_finger2 = f;
						hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_RMB, WINDOW_PRESS, 0);
						break;
					}
				}

				if(hud_active == &hud_ingame) {
					if(f == aim_finger || f == aim_finger2) break;
					if(hud_active->input_touch)
						hud_active->input_touch(f, TOUCH_DOWN, fx, fy,
												event.tfinger.dx * settings.window_width,
												event.tfinger.dy * settings.window_height);
				} else {
					window_setmouseloc(fx, fy);
					mouse(hud_window, fx, fy);
				}
				break;
			}
			case SDL_FINGERUP: {
				struct window_finger* f = NULL;
				int was_dragged = 1;
				float fx = event.tfinger.x * settings.window_width;
				float fy = event.tfinger.y * settings.window_height;
				for(int k = 0; k < 8; k++) {
					if(fingers[k].full && fingers[k].finger == event.tfinger.fingerId) {
						fingers[k].full = 0;
						was_dragged = fingers[k].dragged;
						f = fingers + k;
						break;
					}
				}

				if(f && (f == aim_finger || f == aim_finger2)) {
					if(f == aim_finger2) {
						/* The matching FINGERDOWN sent RMB PRESS; without a
						   RELEASE here button_map[1] stays latched (stuck
						   block-line drag / spade secondary). The ADS scope
						   toggle itself happens on PRESS, so releasing does
						   not un-scope. */
						hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_RMB, WINDOW_RELEASE, 0);
						aim_finger2 = NULL;
					} else if(f == aim_finger) {
						/* The secondary finger is promoted to primary look
						   finger; its RMB hold conceptually ends here too. */
						if(aim_finger2)
							hud_ingame_mouseclick(0, 0, WINDOW_MOUSE_RMB, WINDOW_RELEASE, 0);
						aim_finger = aim_finger2;
						aim_finger2 = NULL;
					}
					break;
				}

				if(hud_active == &hud_ingame) {
					if(hud_active->input_touch)
						hud_active->input_touch(f, TOUCH_UP, fx, fy,
												event.tfinger.dx * settings.window_width,
												event.tfinger.dy * settings.window_height);
					break;
				}

				window_setmouseloc(fx, fy);
				mouse(hud_window, fx, fy);
				int holding_widget = hud_active->ctx && hud_active->ctx->mouse_down == MU_MOUSE_LEFT;
				if(f && f->long_pressed) {
					if(hud_active->input_mouseclick)
						hud_active->input_mouseclick(f->start.x, f->start.y, WINDOW_MOUSE_RMB, WINDOW_RELEASE, 0);
				} else if(holding_widget) {
					mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_RELEASE, 0);
				} else if(!was_dragged) {
					mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_PRESS, 0);
					mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_RELEASE, 0);
				}
				break;
			}
			case SDL_FINGERMOTION: {
				struct window_finger* f = NULL;
				float fx = event.tfinger.x * settings.window_width;
				float fy = event.tfinger.y * settings.window_height;
				float fdx = event.tfinger.dx * settings.window_width;
				float fdy = event.tfinger.dy * settings.window_height;
				for(int k = 0; k < 8; k++) {
					if(fingers[k].full && fingers[k].finger == event.tfinger.fingerId) {
						f = fingers + k;
						f->cur.x = fx;
						f->cur.y = fy;
						break;
					}
				}

				if(hud_active == &hud_ingame && f == aim_finger) {
					/* Mirror the desktop mouse-look formula from hud.c so the
					   "Mouse sensitivity" setting (and invert Y / ADS slowdown)
					   actually affects touch aiming, instead of the previous
					   hardcoded 0.002F. */
					float sens = settings.mouse_sensitivity / 5.0F * (float)MOUSE_SENSITIVITY;
					float s = 1.0F;
					if(camera_mode == CAMERAMODE_FPS && players[local_player_id].held_item == TOOL_GUN
					   && players[local_player_id].input.buttons.rmb)
						s = 0.5F;
					if(settings.invert_y)
						fdy *= -1.0F;
					camera_rot_x -= fdx * sens * s;
					camera_rot_y += fdy * sens * s;
					camera_overflow_adjust();
					break;
				}
				if(hud_active == &hud_ingame && f == aim_finger2)
					break;

				if(hud_active == &hud_ingame) {
					if(hud_active->input_touch)
						hud_active->input_touch(f, TOUCH_MOVE, fx, fy, fdx, fdy);
					break;
				}

				int just_crossed = 0;
				if(f && !f->dragged) {
					float mdx = fx - f->start.x, mdy = fy - f->start.y;
					float thresh = 0.025F * settings.window_height;
					if(mdx * mdx + mdy * mdy > thresh * thresh) {
						f->dragged = 1;
						just_crossed = 1;
					}
				}

				window_setmouseloc(fx, fy);
				if(just_crossed && hud_active->ctx && hud_active->ctx->hover_is_draggable)
					mouse_click(hud_window, WINDOW_MOUSE_LMB, WINDOW_PRESS, 0);

				mouse(hud_window, fx, fy);

				int holding_widget = hud_active->ctx && hud_active->ctx->mouse_down == MU_MOUSE_LEFT;
				if(f && f->dragged && !holding_widget) {
					if(hud_active->ctx)
						mu_input_scroll(hud_active->ctx, 0, (int)-fdy);
					if(hud_active->input_mousescroll)
						hud_active->input_mousescroll(fdy / 50.0F);
				}
				break;
			}
		}
	}

	if(hud_active != &hud_ingame) {
		for(int k = 0; k < 8; k++) {
			struct window_finger* f = fingers + k;
			if(!f->full || f->dragged || f->long_pressed) continue;
			if(window_time() - f->down_time < 0.35F) continue;
			f->long_pressed = 1;
			if(hud_active->input_mouseclick)
				hud_active->input_mouseclick(f->start.x, f->start.y, WINDOW_MOUSE_RMB, WINDOW_PRESS, 0);
		}
	}
}

int window_closed() {
	return quit;
}

void window_title(char* suffix) {
	if(suffix) {
		char title[128];
		snprintf(title, sizeof(title) - 1, "KyroSpades %s - %s", KYROSPADES_VERSION, suffix);
		SDL_SetWindowTitle(hud_window->impl, title);
	} else {
		SDL_SetWindowTitle(hud_window->impl, "KyroSpades " KYROSPADES_VERSION);
	}
}

#endif

/* Validate that an external URL is safe to hand to a shell. We only allow
   http:// or https:// schemes and reject any character that could break out
   of the quoted argument we hand to system() (quotes, backslash, control
   chars). Returns 1 if accepted. */
static int window_url_is_safe(const char* url) {
	if(!url) return 0;
	if(strncmp(url, "http://", 7) != 0 && strncmp(url, "https://", 8) != 0)
		return 0;
	size_t len = strlen(url);
	if(len == 0 || len > 1024) return 0;
	for(size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)url[i];
		if(c < 0x20 || c == '"' || c == '\'' || c == '\\' || c == '`' || c == '$')
			return 0;
	}
	return 1;
}

void window_open_url(const char* url) {
	if(!window_url_is_safe(url)) {
		log_warn("window_open_url: refused unsafe URL");
		return;
	}
#if defined(__ANDROID__) || defined(OS_IOS)
	/* Neither Android nor iOS has a shell/xdg-open, and system() is
	   unavailable on iOS entirely. SDL routes this through the platform's
	   native "open URL" mechanism (ACTION_VIEW intent / UIApplication
	   openURL), which hands off to the default browser. This branch must be
	   chained (#elif/#else) with the others so the system() call below is
	   removed by the preprocessor here, not merely skipped at runtime. */
#if SDL_VERSION_ATLEAST(2, 0, 14)
	if(SDL_OpenURL(url) != 0)
		log_warn("window_open_url: SDL_OpenURL failed: %s", SDL_GetError());
#else
	log_warn("window_open_url: SDL too old for SDL_OpenURL");
#endif
#elif defined(OS_WINDOWS)
	ShellExecuteA(NULL, "open", url, NULL, NULL, SW_SHOWNORMAL);
#else
	char cmd[1152];
#ifdef OS_APPLE
	snprintf(cmd, sizeof(cmd), "open \"%s\" >/dev/null 2>&1 &", url);
#else
	snprintf(cmd, sizeof(cmd), "xdg-open \"%s\" >/dev/null 2>&1 &", url);
#endif
	int rc = system(cmd);
	(void)rc;
#endif
}

int window_cpucores() {
#ifdef OS_LINUX
#ifdef USE_TOUCH
	return sysconf(_SC_NPROCESSORS_CONF);
#else
	return get_nprocs();
#endif
#endif
#ifdef OS_WINDOWS
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
#endif
#ifdef OS_HAIKU
	system_info info;
	get_system_info(&info);
	return info.cpu_count;
#endif
return 1;
}


