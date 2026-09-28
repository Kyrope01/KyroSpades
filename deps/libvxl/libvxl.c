#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>

#include "libvxl.h"

#define LIBVXL_SPAN(base, off) ((struct libvxl_span*)((uint8_t*)(base) + (off)))

static struct libvxl_chunk* chunk_fposition(struct libvxl_map* map, size_t x,
											size_t y) {
	libvxl_assert(map && x < map->width && y < map->height,
				  "invalid input parameters");

	size_t chunk_cnt = (map->width + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	size_t chunk_x = x / LIBVXL_CHUNK_SIZE;
	size_t chunk_y = y / LIBVXL_CHUNK_SIZE;
	return map->chunks + chunk_x + chunk_y * chunk_cnt;
}

static bool libvxl_geometry_get(struct libvxl_map* map, size_t x, size_t y,
								size_t z) {
	libvxl_assert(map && x < map->width && y < map->height && z < map->depth,
				  "invalid input parameters");

	size_t offset = z + (x + y * map->width) * map->depth;
	return (map->geometry[offset / (sizeof(size_t) * 8)]
			& ((size_t)1 << (offset % (sizeof(size_t) * 8))))
		> 0;
}

static void libvxl_geometry_set(struct libvxl_map* map, size_t x, size_t y,
								size_t z, size_t state) {
	libvxl_assert(map && x < map->width && y < map->height && z < map->depth,
				  "invalid input parameters");

	size_t offset = z + (x + y * map->width) * map->depth;

	size_t* val = map->geometry + offset / (sizeof(size_t) * 8);
	size_t bit = offset % (sizeof(size_t) * 8);

	*val = (*val & ~((size_t)1 << bit)) | (state << bit);
}

static int cmp(const void* a, const void* b) {
	struct libvxl_block* aa = (struct libvxl_block*)a;
	struct libvxl_block* bb = (struct libvxl_block*)b;

	return (aa->position > bb->position) - (aa->position < bb->position);
}

static bool libvxl_chunk_put(struct libvxl_chunk* chunk, uint32_t pos,
							 uint32_t color) {
	libvxl_assert(chunk, "chunk pointer is null");

	if(chunk->index == chunk->length) { // needs to grow
		if(chunk->length > SIZE_MAX / LIBVXL_CHUNK_GROWTH / sizeof(struct libvxl_block)) return false;
		size_t length = chunk->length * LIBVXL_CHUNK_GROWTH;
		void* blocks = libvxl_mem_realloc(chunk->blocks, length * sizeof(struct libvxl_block));
		if(!blocks) return false;
		chunk->blocks = blocks;
		chunk->length = length;
	}

	memcpy(chunk->blocks + (chunk->index++),
		   &(struct libvxl_block) {
			   .position = pos,
			   .color = color,
		   },
		   sizeof(struct libvxl_block));
	return true;
}

static struct libvxl_block*
libvxl_chunk_gequal_block(struct libvxl_chunk* chunk, uint32_t pos) {
	libvxl_assert(chunk, "chunk pointer is null");

	size_t start = 0;
	size_t end = chunk->index;
	while(end > start) {
		size_t mid = (start + end) / 2;
		if(pos > chunk->blocks[mid].position) {
			start = mid + 1;
		} else if(pos < chunk->blocks[mid].position) {
			end = mid;
		} else {
			return chunk->blocks + mid;
		}
	}

	return chunk->blocks + start;
}

static bool libvxl_chunk_insert(struct libvxl_chunk* chunk, uint32_t pos,
								uint32_t color) {
	libvxl_assert(chunk, "chunk pointer is null");

	// TODO: use libvxl_chunk_gequal_block

	size_t start = 0;
	size_t end = chunk->index;
	while(end > start) {
		size_t mid = (start + end) / 2;
		if(pos > chunk->blocks[mid].position) {
			start = mid + 1;
		} else if(pos < chunk->blocks[mid].position) {
			end = mid;
		} else { // diff=0, replace color
			chunk->blocks[mid].color = color;
			return true;
		}
	}

	if(chunk->index == chunk->length) { // needs to grow
		if(chunk->length > SIZE_MAX / LIBVXL_CHUNK_GROWTH / sizeof(struct libvxl_block)) return false;
		size_t length = chunk->length * LIBVXL_CHUNK_GROWTH;
		void* blocks = libvxl_mem_realloc(chunk->blocks, length * sizeof(struct libvxl_block));
		if(!blocks) return false;
		chunk->blocks = blocks;
		chunk->length = length;
	}

	memmove(chunk->blocks + start + 1, chunk->blocks + start,
			(chunk->index - start) * sizeof(struct libvxl_block));
	chunk->blocks[start].position = pos;
	chunk->blocks[start].color = color;
	chunk->index++;
	return true;
}

void libvxl_free(struct libvxl_map* map) {
	if(!map)
		return;
	size_t sx = (map->width + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	size_t sy = (map->height + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	if(map->chunks)
		for(size_t k = 0; k < sx * sy; k++)
			libvxl_mem_free(map->chunks[k].blocks);
	libvxl_mem_free(map->chunks);
	libvxl_mem_free(map->geometry);
	memset(map, 0, sizeof(*map));
}

bool libvxl_size(size_t* size, size_t* depth, const void* data, size_t len) {
	if(!data || !size || !depth) return false;
	size_t offset = 0, columns = 0, max_depth = 0;
	while(offset < len) {
		if(len - offset < sizeof(struct libvxl_span)) return false;
		const struct libvxl_span* desc = LIBVXL_SPAN(data, offset);
		if(desc->color_start < desc->air_start || (int)desc->color_end + 1 < desc->color_start)
			return false;
		size_t top_len = (size_t)((int)desc->color_end + 1 - desc->color_start);
		if(desc->length && (size_t)desc->length <= top_len) return false;
		size_t span_len = desc->length ? (size_t)desc->length * 4 : (top_len + 1) * 4;
		if(span_len > len - offset) return false;
		if(top_len && (size_t)desc->color_end + 1 > max_depth) max_depth = desc->color_end + 1;
		if(!desc->length) columns++;
		offset += span_len;
	}
	if(!columns || !max_depth) return false;
	size_t side = (size_t)sqrt((double)columns);
	if(side * side != columns) return false;
	*depth = 1;
	while(*depth < max_depth) *depth *= 2;
	*size = side;
	return true;
}

bool libvxl_create(struct libvxl_map* map, size_t w, size_t h, size_t d,
				   const void* data, size_t len) {
	if(!map || !w || !h || !d || w > 4096 || h > 4096 || d > 256
	   || w > SIZE_MAX / h || w * h > SIZE_MAX / d)
		return false;
	memset(map, 0, sizeof(*map));
	map->width = w;
	map->height = h;
	map->depth = d;
	size_t sx = (w + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	size_t sy = (h + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	if(sx > SIZE_MAX / sy || sx * sy > SIZE_MAX / sizeof(struct libvxl_chunk))
		goto fail;
	map->chunks = libvxl_mem_malloc(sx * sy * sizeof(struct libvxl_chunk));
	if(!map->chunks) goto fail;
	memset(map->chunks, 0, sx * sy * sizeof(struct libvxl_chunk));
	for(size_t y = 0; y < sy; y++) {
		for(size_t x = 0; x < sx; x++) {
			struct libvxl_chunk* chunk = &map->chunks[x + y * sx];
			chunk->length = LIBVXL_CHUNK_SIZE * LIBVXL_CHUNK_SIZE * 2;
			chunk->blocks = libvxl_mem_malloc(chunk->length * sizeof(struct libvxl_block));
			if(!chunk->blocks) goto fail;
		}
	}

	size_t voxels = w * h * d;
	size_t bits = sizeof(size_t) * 8;
	size_t words = (voxels - 1) / bits + 1;
	if(words > SIZE_MAX / sizeof(size_t)) goto fail;
	size_t sg = words * sizeof(size_t);
	map->geometry = libvxl_mem_malloc(sg);
	if(!map->geometry) goto fail;
	memset(map->geometry, data ? 0xFF : 0x00, sg);
	if(!data) {
		for(size_t y = 0; y < h; y++)
			for(size_t x = 0; x < w; x++) {
				libvxl_geometry_set(map, x, y, d - 1, 1);
				if(!libvxl_chunk_put(chunk_fposition(map, x, y), pos_key(x, y, d - 1),
				                     DEFAULT_COLOR(x, y, d - 1))) goto fail;
			}
		return true;
	}

	size_t offset = 0;
	for(size_t y = 0; y < h; y++) {
		for(size_t x = 0; x < w; x++) {
			struct libvxl_chunk* chunk = chunk_fposition(map, x, y);
			size_t previous_end = 0;
			bool first = true;
			while(1) {
				if(offset > len || len - offset < sizeof(struct libvxl_span)) goto fail;
				const struct libvxl_span* desc = LIBVXL_SPAN(data, offset);
				if(desc->air_start > desc->color_start || (int)desc->color_end + 1 < desc->color_start
				   || (desc->color_start <= desc->color_end && desc->color_end >= d)
				   || desc->color_start > d || (!first && desc->air_start <= previous_end)) goto fail;
				size_t top_len = (size_t)((int)desc->color_end + 1 - desc->color_start);
				if(desc->length && (size_t)desc->length <= top_len) goto fail;
				size_t span_len = desc->length ? (size_t)desc->length * 4 : (top_len + 1) * 4;
				if(span_len > len - offset) goto fail;
				const uint8_t* colors = (const uint8_t*)data + offset + sizeof(struct libvxl_span);

				for(size_t z = desc->air_start; z < desc->color_start; z++)
					libvxl_geometry_set(map, x, y, z, 0);
				for(size_t z = desc->color_start; z <= desc->color_end; z++) {
					uint32_t color;
					memcpy(&color, colors + (z - desc->color_start) * 4, sizeof(color));
					if(!libvxl_chunk_put(chunk, pos_key(x, y, z), color)) goto fail;
				}

				size_t bottom_len = desc->length ? (size_t)desc->length - 1 - top_len : 0;
				previous_end = desc->color_end;
				first = false;
				offset += span_len;
				if(!desc->length) break;
				if(offset > len || len - offset < sizeof(struct libvxl_span)) goto fail;
				const struct libvxl_span* next = LIBVXL_SPAN(data, offset);
				if(next->air_start <= desc->color_end || next->air_start > d
				   || bottom_len > (size_t)next->air_start - desc->color_end - 1) goto fail;
				for(size_t z = next->air_start - bottom_len; z < next->air_start; z++) {
					uint32_t color;
					memcpy(&color, colors + (top_len + z - (next->air_start - bottom_len)) * 4, sizeof(color));
					if(!libvxl_chunk_put(chunk, pos_key(x, y, z), color)) goto fail;
				}
			}
		}
	}
	if(offset != len) goto fail;

	for(size_t z = 0; z < map->depth; z++) {
		for(size_t x = 0; x < map->width; x++) {
			bool A = libvxl_geometry_get(map, x, 0, z);
			bool B = libvxl_geometry_get(map, x, map->height - 1, z);

			struct libvxl_chunk* c1 = chunk_fposition(map, x, 0);
			struct libvxl_block* b1 = bsearch(
				&(struct libvxl_block) {
					.position = pos_key(x, 0, z),
				},
				c1->blocks, c1->index, sizeof(struct libvxl_block), cmp);

			struct libvxl_chunk* c2 = chunk_fposition(map, x, map->height - 1);
			struct libvxl_block* b2 = bsearch(
				&(struct libvxl_block) {
					.position = pos_key(x, map->height - 1, z),
				},
				c2->blocks, c2->index, sizeof(struct libvxl_block), cmp);

			if(A && !B && !b1 && !libvxl_chunk_insert(c1, pos_key(x, 0, z),
									DEFAULT_COLOR(x, 0, z))) goto fail;

			if(!A && B && !b2 && !libvxl_chunk_insert(c2, pos_key(x, map->height - 1, z),
									DEFAULT_COLOR(x, map->height - 1, z))) goto fail;
		}

		for(size_t y = 0; y < map->height; y++) {
			bool A = libvxl_geometry_get(map, 0, y, z);
			bool B = libvxl_geometry_get(map, map->width - 1, y, z);

			struct libvxl_chunk* c1 = chunk_fposition(map, 0, y);
			struct libvxl_block* b1 = bsearch(
				&(struct libvxl_block) {
					.position = pos_key(0, y, z),
				},
				c1->blocks, c1->index, sizeof(struct libvxl_block), cmp);

			struct libvxl_chunk* c2 = chunk_fposition(map, map->width - 1, y);
			struct libvxl_block* b2 = bsearch(
				&(struct libvxl_block) {
					.position = pos_key(map->width - 1, y, z),
				},
				c2->blocks, c2->index, sizeof(struct libvxl_block), cmp);

			if(A && !B && !b1 && !libvxl_chunk_insert(c1, pos_key(0, y, z),
									DEFAULT_COLOR(0, y, z))) goto fail;

			if(!A && B && !b2 && !libvxl_chunk_insert(c2, pos_key(map->width - 1, y, z),
									DEFAULT_COLOR(map->width - 1, y, z))) goto fail;
		}
	}

	return true;
fail:
	libvxl_free(map);
	memset(map, 0, sizeof(*map));
	return false;
}

static size_t find_successive_surface(struct libvxl_chunk* chunk,
									  size_t block_offset, int x, int y,
									  size_t start,
									  struct libvxl_block** current) {
	libvxl_assert(chunk && current, "chunk or block pointer is null");

	*current = chunk->blocks + block_offset;
	struct libvxl_block* end_marker = chunk->blocks + chunk->index;

	if(*current < end_marker && (*current)->position == pos_key(x, y, start)) {
		while(1) {
			uint32_t next_z = key_getz((*current)->position) + 1;
			(*current)++;

			if(*current >= end_marker
			   || (*current)->position != pos_key(x, y, next_z))
				return next_z;
		}
	} else {
		return start;
	}
}

static void libvxl_column_encode(struct libvxl_map* map, size_t* chunk_offsets,
								 int x, int y, void* out, size_t* offset) {
	libvxl_assert(map && chunk_offsets && out && offset,
				  "invalid input parameters");

	struct libvxl_chunk* chunk = chunk_fposition(map, x, y);

	bool first_run = true;
	size_t z
		= key_getz(chunk->blocks[chunk_offsets[chunk - map->chunks]].position);
	while(1) {
		size_t top_start = libvxl_geometry_get(map, x, y, z) ?
			z :
			key_getz(
				chunk->blocks[chunk_offsets[chunk - map->chunks]].position);

		struct libvxl_block* last_surface_block;
		size_t top_end
			= find_successive_surface(chunk, chunk_offsets[chunk - map->chunks],
									  x, y, top_start, &last_surface_block);

		size_t bottom_start = map->depth;

		if(top_end == map->depth || !libvxl_geometry_get(map, x, y, top_end)) {
			bottom_start = top_end;
		} else if(last_surface_block < chunk->blocks + chunk->index
				  && key_discardz(last_surface_block->position)
					  == pos_key(x, y, 0)) {
			bottom_start = key_getz(last_surface_block->position);
		}

		struct libvxl_span* desc = LIBVXL_SPAN(out, *offset);
		desc->color_start = top_start;
		desc->color_end = top_end - 1;
		desc->air_start = first_run ? 0 : z;
		*offset += sizeof(struct libvxl_span);

		libvxl_assert(desc->air_start <= desc->color_start,
					  "air_start is bigger than color_start");
		libvxl_assert(desc->color_start <= desc->color_end + 1,
					  "color_start is bigger than color_end + 1");

		first_run = false;

		for(size_t k = top_start; k < top_end; k++) {
			*(uint32_t*)LIBVXL_SPAN(out, *offset)
				= (chunk->blocks[chunk_offsets[chunk - map->chunks]++].color
				   & 0xFFFFFF)
				| 0x7F000000;
			*offset += sizeof(uint32_t);
		}

		if(bottom_start == map->depth) {
			/* this is the last span of this column, do not emit bottom colors,
			 * set length to 0 */
			desc->length = 0;
			break;
		} else { // bottom_start < map->depth
			struct libvxl_block* last_surface_block;
			size_t bottom_end = find_successive_surface(
				chunk, chunk_offsets[chunk - map->chunks], x, y, bottom_start,
				&last_surface_block);

			// there are more spans to follow, emit bottom colors
			if(bottom_end < map->depth) {
				desc->length
					= 1 + top_end - top_start + bottom_end - bottom_start;

				for(size_t k = bottom_start; k < bottom_end; k++) {
					*(uint32_t*)LIBVXL_SPAN(out, *offset)
						= (chunk->blocks[chunk_offsets[chunk - map->chunks]++]
							   .color
						   & 0xFFFFFF)
						| 0x7F000000;
					*offset += sizeof(uint32_t);
				}

				z = bottom_end;
			} else { // bottom_end == map->depth
				desc->length = 1 + top_end - top_start;
				z = bottom_start;
			}
		}
	}
}

void libvxl_stream(struct libvxl_stream* stream, struct libvxl_map* map,
				   size_t chunk_size) {
	if(!stream || !map || chunk_size == 0)
		return;
	stream->map = map;
	map->streamed++;
	stream->chunk_size = chunk_size;
	stream->pos = pos_key(0, 0, 0);
	stream->buffer_offset = 0;
	stream->buffer = libvxl_mem_malloc(stream->chunk_size * 2);

	size_t sx = (map->width + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	size_t sy = (map->height + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	stream->chunk_offsets = libvxl_mem_malloc(sx * sy * sizeof(size_t));
	memset(stream->chunk_offsets, 0, sx * sy * sizeof(size_t));
}

void libvxl_stream_free(struct libvxl_stream* stream) {
	if(!stream)
		return;
	stream->map->streamed--;
	libvxl_mem_free(stream->buffer);
	libvxl_mem_free(stream->chunk_offsets);
}

#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

size_t libvxl_stream_read(struct libvxl_stream* stream, void* out) {
	if(!stream || !out || key_gety(stream->pos) >= stream->map->height)
		return 0;
	while(stream->buffer_offset < stream->chunk_size
		  && key_gety(stream->pos) < stream->map->height) {
		libvxl_column_encode(stream->map, stream->chunk_offsets,
							 key_getx(stream->pos), key_gety(stream->pos),
							 stream->buffer, &stream->buffer_offset);
		if(key_getx(stream->pos) + 1 < stream->map->width)
			stream->pos
				= pos_key(key_getx(stream->pos) + 1, key_gety(stream->pos), 0);
		else
			stream->pos = pos_key(0, key_gety(stream->pos) + 1, 0);
	}
	size_t length = stream->buffer_offset;
	memcpy(out, stream->buffer, min(length, stream->chunk_size));
	if(length < stream->chunk_size) {
		stream->buffer_offset = 0;
	} else {
		memmove(stream->buffer, (uint8_t*)stream->buffer + stream->chunk_size,
				length - stream->chunk_size);
		stream->buffer_offset -= stream->chunk_size;
	}
	return min(length, stream->chunk_size);
}

void libvxl_write(struct libvxl_map* map, void* out, size_t* size) {
	if(!map || !out)
		return;
	size_t sx = (map->width + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	size_t sy = (map->height + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;

	size_t* chunk_offsets = libvxl_mem_malloc(sx * sy * sizeof(size_t));
	memset(chunk_offsets, 0, sx * sy * sizeof(size_t));

	size_t offset = 0;
	for(uint32_t y = 0; y < map->height; y++)
		for(uint32_t x = 0; x < map->width; x++)
			libvxl_column_encode(map, chunk_offsets, x, y, out, &offset);

	if(size)
		*size = offset;

	libvxl_mem_free(chunk_offsets);
}

size_t libvxl_writefile(struct libvxl_map* map, const char* name) {
	if(!map || !name)
		return 0;
	uint8_t buf[1024];
	struct libvxl_stream s;
	libvxl_stream(&s, map, 1024);
	FILE* f = fopen(name, "wb");
	size_t read, total = 0;
	while((read = libvxl_stream_read(&s, buf))) {
		fwrite(buf, 1, read, f);
		total += read;
	}
	fclose(f);
	libvxl_stream_free(&s);
	return total;
}

/*void libvxl_kv6_write(struct libvxl_map* map, char* name) {
	FILE* f = fopen(name, "wb");

	struct libvxl_kv6 header;
	strcpy(header.magic, "Kvxl");
	header.width = map->width;
	header.height = map->height;
	header.depth = map->depth;
	header.pivot[0] = map->width / 2.0F;
	header.pivot[1] = map->height / 2.0F;
	header.pivot[2] = map->depth / 2.0F;

	header.len = 0;

	int sx = (map->width + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	int sy = (map->height + LIBVXL_CHUNK_SIZE - 1) / LIBVXL_CHUNK_SIZE;
	for(int y = 0; y < sy; y++)
		for(int x = 0; x < sx; x++)
			header.len += map->chunks[x + y * sx].index;

	fwrite(&header, sizeof(header), 1, f);

	int xoffset[map->width];
	short xyoffset[map->width * map->height];
	memset(xoffset, 0, sizeof(xoffset));
	memset(xyoffset, 0, sizeof(xyoffset));

	size_t chunk_offsets[sx * sy];
	memset(chunk_offsets, 0, sizeof(chunk_offsets));

	for(size_t y = 0; y < map->height; y++) {
		for(size_t x = 0; x < map->width; x++) {
			struct libvxl_chunk* chunk = chunk_fposition(map, x, y);

			while(1) {
				struct libvxl_block* next
					= &chunk->blocks[chunk_offsets[chunk - map->chunks]];
				if(key_getx(next->position) != x)
					break;

				chunk_offsets[chunk - map->chunks]++;

				struct libvxl_kv6_block blk;
				blk.color = next->color & 0xFFFFFF;
				blk.z = key_getz(next->position);
				blk.normal = 0;

				blk.visfaces = 0;
				if(libvxl_map_issolid(map, x + 1, y, blk.z))
					blk.visfaces |= 0x01;
				if(libvxl_map_issolid(map, x - 1, y, blk.z))
					blk.visfaces |= 0x02;
				if(libvxl_map_issolid(map, x, y + 1, blk.z))
					blk.visfaces |= 0x04;
				if(libvxl_map_issolid(map, x, y - 1, blk.z))
					blk.visfaces |= 0x08;
				if(libvxl_map_issolid(map, x, y, blk.z + 1))
					blk.visfaces |= 0x10;
				if(libvxl_map_issolid(map, x, y, blk.z - 1))
					blk.visfaces |= 0x20;

				fwrite(&blk, sizeof(blk), 1, f);

				xoffset[x]++;
				xyoffset[x + y * map->width]++;
			}
		}
	}

	fwrite(xoffset, sizeof(xoffset), 1, f);
	fwrite(xyoffset, sizeof(xyoffset), 1, f);

	fclose(f);
}*/

bool libvxl_map_isinside(struct libvxl_map* map, int x, int y, int z) {
	return map && x >= 0 && y >= 0 && z >= 0 && x < (int)map->width
		&& y < (int)map->height && z < (int)map->depth;
}

uint32_t libvxl_map_get(struct libvxl_map* map, int x, int y, int z) {
	if(!map || x < 0 || y < 0 || z < 0 || x >= (int)map->width
	   || y >= (int)map->height || z >= (int)map->depth)
		return 0;
	if(!libvxl_geometry_get(map, x, y, z))
		return 0;
	struct libvxl_chunk* chunk = chunk_fposition(map, x, y);
	struct libvxl_block* loc = bsearch(
		&(struct libvxl_block) {
			.position = pos_key(x, y, z),
		},
		chunk->blocks, chunk->index, sizeof(struct libvxl_block), cmp);
	return loc ? loc->color : DEFAULT_COLOR(x, y, z);
}

bool libvxl_map_issolid(struct libvxl_map* map, int x, int y, int z) {
	if(z < 0)
		return false;
	if(!map || z >= (int)map->depth)
		return true;
	return libvxl_geometry_get(map, (size_t)x % (size_t)map->width,
							   (size_t)y % (size_t)map->height, z);
}

bool libvxl_map_onsurface(struct libvxl_map* map, int x, int y, int z) {
	if(!map)
		return false;
	return !libvxl_map_issolid(map, x, y + 1, z)
		|| !libvxl_map_issolid(map, x, y - 1, z)
		|| !libvxl_map_issolid(map, x + 1, y, z)
		|| !libvxl_map_issolid(map, x - 1, y, z)
		|| !libvxl_map_issolid(map, x, y, z + 1)
		|| !libvxl_map_issolid(map, x, y, z - 1);
}

bool libvxl_map_gettop(struct libvxl_map* map, int x, int y, uint32_t* result) {
	if(!map || !result || !map->chunks || x < 0 || y < 0
	   || x >= (int)map->width || y >= (int)map->height)
		return false;

	struct libvxl_chunk* c = chunk_fposition(map, x, y);
	struct libvxl_block* block = libvxl_chunk_gequal_block(c, pos_key(x, y, 0));

	if(block >= c->blocks + c->index
	   || key_discardz(block->position) != pos_key(x, y, 0)
	   || key_getz(block->position) >= map->depth)
		return false;
	result[0] = block->color;
	result[1] = key_getz(block->position);
	return true;
}

static void libvxl_map_set_internal(struct libvxl_map* map, int x, int y, int z,
									uint32_t color) {
	libvxl_assert(map, "map is null");

	if(z < 0 || z >= (int)map->depth)
		return;

	x = (size_t)x % (size_t)map->width;
	y = (size_t)y % (size_t)map->height;

	if(libvxl_geometry_get(map, x, y, z) && !libvxl_map_onsurface(map, x, y, z))
		return;

	(void)libvxl_chunk_insert(chunk_fposition(map, x, y), pos_key(x, y, z), color);
}

static void libvxl_map_setair_internal(struct libvxl_map* map, int x, int y,
									   int z) {
	libvxl_assert(map, "map is null");

	if(z < 0 || z >= (int)map->depth)
		return;

	x = (size_t)x % (size_t)map->width;
	y = (size_t)y % (size_t)map->height;

	if(!libvxl_geometry_get(map, x, y, z))
		return;

	struct libvxl_chunk* chunk = chunk_fposition(map, x, y);

	struct libvxl_block* loc = bsearch(
		&(struct libvxl_block) {
			.position = pos_key(x, y, z),
		},
		chunk->blocks, chunk->index, sizeof(struct libvxl_block), cmp);

	if(loc) {
		memmove(loc, loc + 1,
				(chunk->blocks + (--chunk->index) - loc)
					* sizeof(struct libvxl_block));

		if(chunk->index * LIBVXL_CHUNK_SHRINK <= chunk->length) {
			chunk->length /= LIBVXL_CHUNK_GROWTH;
			chunk->blocks = libvxl_mem_realloc(
				chunk->blocks, chunk->length * sizeof(struct libvxl_block));
		}
	}
}

void libvxl_map_set(struct libvxl_map* map, int x, int y, int z,
					uint32_t color) {
	if(!map || x < 0 || y < 0 || z < 0 || x >= (int)map->width
	   || y >= (int)map->height || z >= (int)map->depth)
		return;

	libvxl_geometry_set(map, x, y, z, 1);
	libvxl_map_set_internal(map, x, y, z, color);

	if(!libvxl_map_onsurface(map, x, y + 1, z))
		libvxl_map_setair_internal(map, x, y + 1, z);
	if(!libvxl_map_onsurface(map, x, y - 1, z))
		libvxl_map_setair_internal(map, x, y - 1, z);

	if(!libvxl_map_onsurface(map, x + 1, y, z))
		libvxl_map_setair_internal(map, x + 1, y, z);
	if(!libvxl_map_onsurface(map, x - 1, y, z))
		libvxl_map_setair_internal(map, x - 1, y, z);

	if(!libvxl_map_onsurface(map, x, y, z + 1))
		libvxl_map_setair_internal(map, x, y, z + 1);
	if(!libvxl_map_onsurface(map, x, y, z - 1))
		libvxl_map_setair_internal(map, x, y, z - 1);
}

void libvxl_map_setair(struct libvxl_map* map, int x, int y, int z) {
	if(!map || x < 0 || y < 0 || z < 0 || x >= (int)map->width
	   || y >= (int)map->height || z >= (int)map->depth - 1)
		return;

	bool surface_prev[6] = {
		libvxl_map_issolid(map, x, y + 1, z) ?
			libvxl_map_onsurface(map, x, y + 1, z) :
			true,
		libvxl_map_issolid(map, x, y - 1, z) ?
			libvxl_map_onsurface(map, x, y - 1, z) :
			true,
		libvxl_map_issolid(map, x + 1, y, z) ?
			libvxl_map_onsurface(map, x + 1, y, z) :
			true,
		libvxl_map_issolid(map, x - 1, y, z) ?
			libvxl_map_onsurface(map, x - 1, y, z) :
			true,
		libvxl_map_issolid(map, x, y, z + 1) ?
			libvxl_map_onsurface(map, x, y, z + 1) :
			true,
		libvxl_map_issolid(map, x, y, z - 1) ?
			libvxl_map_onsurface(map, x, y, z - 1) :
			true,
	};

	libvxl_map_setair_internal(map, x, y, z);
	libvxl_geometry_set(map, x, y, z, 0);

	if(!surface_prev[0] && libvxl_map_onsurface(map, x, y + 1, z))
		libvxl_map_set_internal(map, x, y + 1, z, DEFAULT_COLOR(x, y + 1, z));
	if(!surface_prev[1] && libvxl_map_onsurface(map, x, y - 1, z))
		libvxl_map_set_internal(map, x, y - 1, z, DEFAULT_COLOR(x, y - 1, z));

	if(!surface_prev[2] && libvxl_map_onsurface(map, x + 1, y, z))
		libvxl_map_set_internal(map, x + 1, y, z, DEFAULT_COLOR(x + 1, y, z));
	if(!surface_prev[3] && libvxl_map_onsurface(map, x - 1, y, z))
		libvxl_map_set_internal(map, x - 1, y, z, DEFAULT_COLOR(x - 1, y, z));

	if(!surface_prev[4] && libvxl_map_onsurface(map, x, y, z + 1))
		libvxl_map_set_internal(map, x, y, z + 1, DEFAULT_COLOR(x, y, z + 1));
	if(!surface_prev[5] && libvxl_map_onsurface(map, x, y, z - 1))
		libvxl_map_set_internal(map, x, y, z - 1, DEFAULT_COLOR(x, y, z - 1));
}

void libvxl_copy_chunk_destroy(struct libvxl_chunk_copy* copy) {
	if(!copy)
		return;

	libvxl_mem_free(copy->geometry);
	libvxl_mem_free(copy->colors);
	libvxl_mem_free(copy->blocks_sorted);
}

void libvxl_copy_chunk(struct libvxl_map* map, struct libvxl_chunk_copy* copy,
					   size_t x, size_t y) {
	if(!map || !copy || x >= map->width || y >= map->height)
		return;

	copy->width = map->width;
	copy->height = map->height;
	copy->depth = map->depth;

	size_t span = LIBVXL_CHUNK_SIZE + 2 * LIBVXL_COPY_BORDER;
	copy->geo_w = span < map->width ? span : map->width;
	copy->geo_h = span < map->height ? span : map->height;
	copy->geo_x = (x + map->width - LIBVXL_COPY_BORDER) % map->width;
	copy->geo_y = (y + map->height - LIBVXL_COPY_BORDER) % map->height;

	size_t bits_per_word = sizeof(size_t) * 8;
	size_t sg = (copy->geo_w * copy->geo_h * map->depth + bits_per_word - 1)
		/ bits_per_word * sizeof(size_t);
	copy->geometry = libvxl_mem_malloc(sg);

	if(map->depth % bits_per_word == 0) {
		size_t wpc = map->depth / bits_per_word;
		for(size_t ly = 0; ly < copy->geo_h; ly++) {
			size_t gy = (copy->geo_y + ly) % map->height;
			size_t* dst = copy->geometry + ly * copy->geo_w * wpc;
			const size_t* row = map->geometry + gy * map->width * wpc;
			size_t first = copy->geo_w;
			if(copy->geo_x + copy->geo_w > map->width)
				first = map->width - copy->geo_x;
			memcpy(dst, row + copy->geo_x * wpc, first * wpc * sizeof(size_t));
			if(first < copy->geo_w)
				memcpy(dst + first * wpc, row,
					   (copy->geo_w - first) * wpc * sizeof(size_t));
		}
	} else {
		memset(copy->geometry, 0, sg);
		for(size_t ly = 0; ly < copy->geo_h; ly++) {
			size_t gy = (copy->geo_y + ly) % map->height;
			for(size_t lx = 0; lx < copy->geo_w; lx++) {
				size_t gx = (copy->geo_x + lx) % map->width;
				for(size_t z = 0; z < map->depth; z++) {
					size_t src = z + (gx + gy * map->width) * map->depth;
					if(map->geometry[src / bits_per_word]
					   & ((size_t)1 << (src % bits_per_word))) {
						size_t dst = z + (lx + ly * copy->geo_w) * map->depth;
						copy->geometry[dst / bits_per_word]
							|= (size_t)1 << (dst % bits_per_word);
					}
				}
			}
		}
	}

	struct libvxl_chunk* c = chunk_fposition(map, x, y);
	copy->blocks_sorted
		= libvxl_mem_malloc(c->index * sizeof(struct libvxl_block));
	copy->blocks_sorted_count = c->index;
	memcpy(copy->blocks_sorted, c->blocks,
		   c->index * sizeof(struct libvxl_block));

	copy->grid_x = x / LIBVXL_CHUNK_SIZE * LIBVXL_CHUNK_SIZE;
	copy->grid_y = y / LIBVXL_CHUNK_SIZE * LIBVXL_CHUNK_SIZE;
	size_t grid_sz = LIBVXL_CHUNK_SIZE * LIBVXL_CHUNK_SIZE * map->depth;
	copy->colors = libvxl_mem_malloc(grid_sz * sizeof(uint32_t));
	memset(copy->colors, 0, grid_sz * sizeof(uint32_t));
	for(size_t k = 0; k < c->index; k++) {
		uint32_t pos = c->blocks[k].position;
		size_t lx = key_getx(pos) - copy->grid_x;
		size_t ly = key_gety(pos) - copy->grid_y;
		size_t z = key_getz(pos);
		if(lx < LIBVXL_CHUNK_SIZE && ly < LIBVXL_CHUNK_SIZE && z < map->depth)
			copy->colors[z + (lx + ly * LIBVXL_CHUNK_SIZE) * map->depth]
				= c->blocks[k].color & 0xFFFFFF;
	}
}
