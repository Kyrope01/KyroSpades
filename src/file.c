
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
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>

#include "common.h"
#include "log.h"
#include "file.h"
#include "window.h"

#if defined(USE_ANDROID_FILE) && defined(__ANDROID__)
#include <jni.h>
#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>

/* Lazily fetch the app's AAssetManager through SDL's JNI helpers. The Java
   AssetManager object is pinned with a global ref so the GC can never
   collect it out from under the native pointer. Must be called from a
   JVM-attached thread (SDLThread is). */
static AAssetManager* file_asset_manager(void) {
	static AAssetManager* mgr = NULL;
	if(mgr)
		return mgr;
	JNIEnv* env = (JNIEnv*)SDL_AndroidGetJNIEnv();
	jobject activity = (jobject)SDL_AndroidGetActivity();
	if(!env || !activity)
		return NULL;
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID mid = (*env)->GetMethodID(env, cls, "getAssets", "()Landroid/content/res/AssetManager;");
	if(mid) {
		jobject am = (*env)->CallObjectMethod(env, activity, mid);
		if(am) {
			jobject global = (*env)->NewGlobalRef(env, am);
			mgr = AAssetManager_fromJava(env, global);
			(*env)->DeleteLocalRef(env, am);
		}
	}
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
	return mgr;
}
#endif

/* List the entries in a directory, invoking cb(name, user) for each.
   Returns the number of entries reported, or -1 if the directory could not
   be opened anywhere. Tries the real filesystem first (desktop, and files
   unpacked next to the Android external-storage CWD), then falls back to
   the APK asset manager: assets live inside the APK, so opendir() cannot
   see them -- this is why directory-driven features (random backgrounds)
   silently degraded on Android while direct file loads worked. */
int file_dir_list(const char* path, void (*cb)(const char* name, void* user), void* user) {
	int count = 0;
	DIR* dir = opendir(path);
	if(dir) {
		struct dirent* entry;
		while((entry = readdir(dir)) != NULL) {
			if(!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
				continue;
			cb(entry->d_name, user);
			count++;
		}
		closedir(dir);
		return count;
	}
#if defined(USE_ANDROID_FILE) && defined(__ANDROID__)
	AAssetManager* mgr = file_asset_manager();
	if(mgr) {
		/* Note: AAssetDir reports files only (no subdirectories), and
		   openDir returns an empty listing rather than NULL for missing
		   directories -- count 0 lets callers fall back gracefully. */
		AAssetDir* adir = AAssetManager_openDir(mgr, path);
		if(adir) {
			const char* name;
			while((name = AAssetDir_getNextFileName(adir)) != NULL) {
				cb(name, user);
				count++;
			}
			AAssetDir_close(adir);
			return count;
		}
	}
#endif
	return -1;
}

struct file_handle {
	void* internal;
	int type;
};

enum {
	FILE_STD,
	FILE_SDL,
};

void file_url(char* url) {
	/* Legacy entry point. The shell-based implementation was unsafe
	   (command injection via chat/news URLs that get passed through
	   system()). Route everything through window_open_url(), which
	   uses SDL_OpenURL with a safety check on the URL contents
	   (no control chars, no shell metacharacters). */
	window_open_url(url);
}

int file_dir_exists(const char* path) {
	DIR* d = opendir(path);
	if(d) {
		closedir(d);
		return 1;
	} else {
		return 0;
	}
}

int file_dir_create(const char* path) {
#ifdef OS_WINDOWS
	if(mkdir(path) == 0) return 1;
	return errno == EEXIST && file_dir_exists(path);
#else
	if(mkdir(path, 0755) == 0) return 1;
	return errno == EEXIST && file_dir_exists(path);
#endif
}

int file_exists(const char* name) {
#ifdef USE_ANDROID_FILE
	void* f = file_open(name, "rb");
	if(f == NULL)
		return 0;
	file_close(f);
	return 1;
#else
	return !access(name, F_OK);
#endif
}

/* Determine length from an already-open handle. Errors and lengths outside
   the public int-sized file API are rejected before allocation or seeking. */
static int file_handle_size(void* file) {
#ifdef USE_ANDROID_FILE
	struct file_handle* f = file;
	if(f->type == FILE_SDL) {
		Sint64 n = SDL_RWsize((struct SDL_RWops*)f->internal);
		return n < 0 || n > INT_MAX ? -1 : (int)n;
	}
	FILE* stream = (FILE*)f->internal;
#else
	FILE* stream = (FILE*)file;
#endif
	if(fseek(stream, 0, SEEK_END) != 0) return -1;
	long n = ftell(stream);
	if(n < 0 || n > INT_MAX || fseek(stream, 0, SEEK_SET) != 0) return -1;
	return (int)n;
}

int file_size(const char* name) {
	void* f = file_open(name, "rb");
	if(!f) return 0;
	int n = file_handle_size(f);
	file_close(f);
	return n < 0 ? 0 : n;
}

unsigned char* file_load_sized(const char* name, size_t* size_out) {
	if(size_out) *size_out = 0;
	void* handle = file_open(name, "rb");
	if(!handle) return NULL;
	int size = file_handle_size(handle);
	if(size < 0) { file_close(handle); return NULL; }
	unsigned char* data = malloc((size_t)size + 1);
	if(!data) { file_close(handle); return NULL; }
	bool ok = true;
#ifdef USE_ANDROID_FILE
	struct file_handle* f = handle;
	if(f->type == FILE_SDL) {
		SDL_RWops* stream = (SDL_RWops*)f->internal;
		if(SDL_RWseek(stream, 0, RW_SEEK_SET) < 0) ok = false;
		size_t offset = 0;
		while(ok && offset < (size_t)size) {
			size_t n = SDL_RWread(stream, data + offset, 1, (size_t)size - offset);
			if(!n) { ok = false; break; }
			offset += n;
		}
	} else {
		if(size && fread(data, 1, (size_t)size, (FILE*)f->internal) != (size_t)size) ok = false;
	}
#else
	if(size && fread(data, 1, (size_t)size, (FILE*)handle) != (size_t)size) ok = false;
#endif
	file_close(handle);
	if(!ok) { free(data); return NULL; }
	data[size] = 0;
	if(size_out) *size_out = (size_t)size;
	return data;
}

unsigned char* file_load(const char* name) {
	return file_load_sized(name, NULL);
}

void* file_open(const char* name, const char* mode) {
#ifdef USE_ANDROID_FILE
	struct file_handle* handle = malloc(sizeof(struct file_handle));
	if(!handle) return NULL;
	/* Real filesystem first, relative to the writable CWD set in main();
	   this also holds anything previously saved (config.ini, demos, ...). */
	handle->internal = fopen(name, mode);
	handle->type = FILE_STD;

	/* APK assets are invisible to fopen(), so reads fall back to SDL. */
	if(!handle->internal && strchr(mode, 'r') != NULL) {
		handle->internal = SDL_RWFromFile(name, mode);
		handle->type = FILE_SDL;
	}

	if(!handle->internal) {
		/* Legacy /sdcard location from old installs. */
		char str[256];
		snprintf(str, sizeof(str), "/sdcard/KyroSpades/%s", name);
		handle->internal = fopen(str, mode);
		handle->type = FILE_STD;
	}
	if(!handle->internal) {
		free(handle);
		return NULL;
	}
	return handle;
#else
	return fopen(name, mode);
#endif
}

void file_printf(void* file, const char* fmt, ...) {
	va_list args;
	va_start(args, fmt);
#ifdef USE_ANDROID_FILE
	struct file_handle* f = (struct file_handle*)file;
	if(f->type == FILE_SDL) {
		va_list copy;
		va_copy(copy, args);
		int length = vsnprintf(NULL, 0, fmt, copy);
		va_end(copy);
		if(length >= 0) {
			char* str = malloc((size_t)length + 1);
			if(str) {
				vsnprintf(str, (size_t)length + 1, fmt, args);
				size_t written = 0;
				while(written < (size_t)length) {
					size_t n = SDL_RWwrite((struct SDL_RWops*)f->internal, str + written, 1, (size_t)length - written);
					if(!n) break;
					written += n;
				}
				free(str);
			}
		}
	}
	if(f->type == FILE_STD) {
		vfprintf((FILE*)f->internal, fmt, args);
	}
#else
	vfprintf((FILE*)file, fmt, args);
#endif
	va_end(args);
}

void file_close(void* file) {
#ifdef USE_ANDROID_FILE
	struct file_handle* f = (struct file_handle*)file;
	if(f->type == FILE_SDL) {
		SDL_RWclose((struct SDL_RWops*)f->internal);
	}
	if(f->type == FILE_STD) {
		fclose((FILE*)f->internal);
	}
	free(f);
#else
	fclose((FILE*)file);
#endif
}

float buffer_readf(unsigned char* buffer, int index) {
	uint32_t bits = buffer_read32(buffer, index);
	float value;
	memcpy(&value, &bits, sizeof(value));
	return value;
}

unsigned int buffer_read32(unsigned char* buffer, int index) {
	return ((unsigned int)buffer[index + 3] << 24) | ((unsigned int)buffer[index + 2] << 16) | ((unsigned int)buffer[index + 1] << 8) | (unsigned int)buffer[index];
}

unsigned short buffer_read16(unsigned char* buffer, int index) {
	return (buffer[index + 1] << 8) | buffer[index];
}

unsigned char buffer_read8(unsigned char* buffer, int index) {
	return buffer[index];
}
