
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

/* List the regular files in a directory, invoking cb(name, user) for each.
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
	return errno == EEXIST;
#else
	if(mkdir(path, 0755) == 0) return 1;
	return errno == EEXIST;
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

int file_size(const char* name) {
#ifdef USE_ANDROID_FILE
	struct file_handle* f = (struct file_handle*)file_open(name, "rb");
	if(!f)
		return 0;
	if(f->type == FILE_SDL) {
		int size = SDL_RWsize((struct SDL_RWops*)f->internal);
		file_close(f);
		return size;
	}
	if(f->type == FILE_STD) {
		fseek(f->internal, 0, SEEK_END);
		int size = ftell(f->internal);
		file_close(f);
		return size;
	}
	return 0;
#else
	FILE* f = fopen(name, "rb");
	if(!f)
		return 0;
	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	fclose(f);
	return size;
#endif
}

unsigned char* file_load(const char* name) {
#ifdef USE_ANDROID_FILE
	int size = file_size(name);
	struct file_handle* f = (struct file_handle*)file_open(name, "rb");
	if(!f)
		return NULL;
	unsigned char* data = malloc(size + 1);
	CHECK_ALLOCATION_ERROR(data)
	data[size] = 0;
	if(f->type == FILE_SDL) {
		int offset = 0;
		while(1) {
			int read = SDL_RWread((struct SDL_RWops*)f->internal, data + offset, 1, size - offset);
			if(!read)
				break;
			offset += read;
		}
		SDL_RWclose((struct SDL_RWops*)f->internal);
		if(!offset) {
			free(data);
			return NULL;
		}
	}
	if(f->type == FILE_STD) {
		fread(data, size, 1, f->internal);
		fclose(f->internal);
	}
	return data;
#else
	FILE* f;
	f = fopen(name, "rb");
	if(!f) {
		log_fatal("ERROR: failed to open '%s', exiting", name);
		exit(1);
	}
	fseek(f, 0, SEEK_END);
	int size = ftell(f);
	unsigned char* data = malloc(size + 1);
	CHECK_ALLOCATION_ERROR(data)
	data[size] = 0;
	fseek(f, 0, SEEK_SET);
	fread(data, size, 1, f);
	fclose(f);
	return data;
#endif
}

void* file_open(const char* name, const char* mode) {
#ifdef USE_ANDROID_FILE
	struct file_handle* handle = malloc(sizeof(struct file_handle));
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
		char str[256];
		vsprintf(str, fmt, args);
		int written = 0;
		int total = strlen(str);
		while(written < total)
			written += SDL_RWwrite((struct SDL_RWops*)f->internal, str + written, 1, total - written);
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
	return ((float*)(buffer + index))[0];
}

unsigned int buffer_read32(unsigned char* buffer, int index) {
	return (buffer[index + 3] << 24) | (buffer[index + 2] << 16) | (buffer[index + 1] << 8) | buffer[index];
}

unsigned short buffer_read16(unsigned char* buffer, int index) {
	return (buffer[index + 1] << 8) | buffer[index];
}

unsigned char buffer_read8(unsigned char* buffer, int index) {
	return buffer[index];
}


