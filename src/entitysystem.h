
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

#ifndef ENTITY_SYSTEM_H
#define ENTITY_SYSTEM_H

#include <stddef.h>
#include <stdbool.h>

/* NOTE: entity_system is used exclusively from the main thread. Every
   instance (particles, tracers, grenades, sound sources, falling-block
   debris) is a file-local global that is never declared `extern` in a
   header, so no other translation unit -- and in particular none of the
   background worker threads (chunk generation, water reflection, map
   collapse physics, chat history search, replay/recording encode) -- can
   ever reach these entitysys_* calls. The pthread_mutex this struct used to
   carry was therefore always uncontended dead weight: real, non-zero
   lock/unlock overhead paid every single frame (once per entitysys_iterate
   call and once per entitysys_add call, across 5+ systems) for
   synchronization nothing ever needed. Removed. If a future change ever
   does need to touch one of these systems from another thread, add locking
   back at that call site (or reintroduce it here) at that point. */
struct entity_system {
	void* buffer;
	size_t count;
	size_t length;
	size_t object_size;
};

void entitysys_create(struct entity_system* es, size_t object_size, size_t initial_size);

void entitysys_add(struct entity_system* es, void* object);

void entitysys_iterate(struct entity_system* es, void* user, bool (*callback)(void* object, void* user));

#endif
