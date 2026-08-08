
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <math.h>
#include <sys/stat.h>

#include "common.h"
#include "skins.h"
#include "file.h"
#include "config.h"
#include "log.h"
#include "matrix.h"
#include "glx.h"
#include "window.h"
#include "texture.h"

struct skin_category skin_categories[SKIN_CATEGORIES];

static int entry_compare(const void* a, const void* b) {
	const struct skin_entry* ea = (const struct skin_entry*)a;
	const struct skin_entry* eb = (const struct skin_entry*)b;
	return strcmp(ea->name, eb->name);
}

void skins_scan(void) {
	for(int c = 0; c < SKIN_CATEGORIES; c++) {
		struct skin_category* cat = &skin_categories[c];

		if(cat->entries) {
			for(int i = 0; i < cat->count; i++)
				free(cat->entries[i].previews);
			free(cat->entries);
			cat->entries = NULL;
		}
		cat->count = 0;

		switch(c) {
			case SKIN_SPADE:
				strcpy(cat->label, "Spade");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_spade, "spade", 0.05F};
				cat->sound_count = 0;
				break;
			case SKIN_GRENADE:
				strcpy(cat->label, "Grenade");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_grenade, "grenade", 0.05F};
				cat->sound_count = 0;
				break;
			case SKIN_RIFLE:
				strcpy(cat->label, "Rifle");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_semi, "semi", 0.05F};
				cat->sound_count = 1;
				cat->sounds[0] = (struct skin_sound_def){&sound_rifle_shoot, "semishoot"};
				break;
			case SKIN_SMG:
				strcpy(cat->label, "SMG");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_smg, "smg", 0.05F};
				cat->sound_count = 1;
				cat->sounds[0] = (struct skin_sound_def){&sound_smg_shoot, "smgshoot"};
				break;
			case SKIN_SHOTGUN:
				strcpy(cat->label, "Shotgun");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_shotgun, "shotgun", 0.05F};
				cat->sound_count = 1;
				cat->sounds[0] = (struct skin_sound_def){&sound_shotgun_shoot, "shotgunshoot"};
				break;
			case SKIN_PLAYER:
				strcpy(cat->label, "Player");
				cat->model_count = 7;
				cat->models[0] = (struct skin_model_def){&model_playerhead, "playerhead", 0.1F};
				cat->models[1] = (struct skin_model_def){&model_playertorso, "playertorso", 0.1F};
				cat->models[2] = (struct skin_model_def){&model_playertorsoc, "playertorsoc", 0.1F};
				cat->models[3] = (struct skin_model_def){&model_playerarms, "playerarms", 0.1F};
				cat->models[4] = (struct skin_model_def){&model_playerleg, "playerleg", 0.1F};
				cat->models[5] = (struct skin_model_def){&model_playerlegc, "playerlegc", 0.1F};
				cat->models[6] = (struct skin_model_def){&model_playerdead, "playerdead", 0.1F};
				cat->sound_count = 0;
				break;
			case SKIN_INTEL:
				strcpy(cat->label, "Intel");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_intel, "intel", 0.2F};
				cat->sound_count = 0;
				break;
			case SKIN_TENT:
				strcpy(cat->label, "Tent");
				cat->model_count = 1;
				cat->models[0] = (struct skin_model_def){&model_tent, "cp", 0.278F};
				cat->sound_count = 0;
				break;
		}

		int capacity = 8;
		cat->entries = malloc(sizeof(struct skin_entry) * capacity);
		strcpy(cat->entries[0].name, "Default");
		cat->entries[0].suffix[0] = 0;
		cat->entries[0].previews = calloc(cat->model_count, sizeof(struct kv6_t));
		for(int m = 0; m < cat->model_count; m++) {
			char mp[256];
			snprintf(mp, sizeof(mp), "kv6/%s.kv6", cat->models[m].basename);
			kv6_reload(&cat->entries[0].previews[m], mp, cat->models[m].scale);
		}
		cat->count = 1;

		char prefix[64];
		snprintf(prefix, sizeof(prefix), "%s(", cat->models[0].basename);
		int prefix_len = strlen(prefix);

		DIR* dir = opendir("kv6");
		if(!dir)
			continue;

		struct dirent* entry;
		while((entry = readdir(dir)) != NULL) {
			/* d_type is unreliable: MinGW and some filesystems always report
			   DT_UNKNOWN, so we can't gate on DT_REG. stat() the path and test
			   S_ISREG instead — correct on every platform. This replaces the
			   old CI-time sed patch that rewrote this block on Windows only. */
			struct stat st;
			char stat_path[512];
			snprintf(stat_path, sizeof(stat_path), "kv6/%s", entry->d_name);
			if(stat(stat_path, &st) == 0 && S_ISREG(st.st_mode)) {
				const char* name = entry->d_name;
				int len = strlen(name);
				if(len > 4 && strcmp(name + len - 4, ".kv6") == 0) {
					if(strncmp(name, prefix, prefix_len) == 0) {
						const char* paren_close = strchr(name + prefix_len, ')');
						if(paren_close && paren_close > name + prefix_len
						   && (size_t)(paren_close - name) == (size_t)(len - 5)) {
							int name_len = paren_close - (name + prefix_len);
							if(name_len > 0 && name_len < SKIN_NAME_MAX) {
								if(cat->count >= capacity) {
									capacity *= 2;
									cat->entries = realloc(cat->entries,
										sizeof(struct skin_entry) * capacity);
								}
								struct skin_entry* e = &cat->entries[cat->count];
								memcpy(e->name, name + prefix_len, name_len);
								e->name[name_len] = 0;
								memcpy(e->suffix, e->name, name_len + 1);
								e->previews = calloc(cat->model_count, sizeof(struct kv6_t));
								for(int m = 0; m < cat->model_count; m++) {
									char mp[256];
									snprintf(mp, sizeof(mp), "kv6/%s(%s).kv6",
										cat->models[m].basename, e->suffix);
									kv6_reload(&e->previews[m], mp, cat->models[m].scale);
								}
								cat->count++;
							}
						}
					}
				}
			}
		}
		closedir(dir);

		if(cat->count > 1)
			qsort(cat->entries + 1, cat->count - 1, sizeof(struct skin_entry), entry_compare);
	}
}

int skins_apply(enum skin_category_type category, int entry, int models) {
	if(category < 0 || category >= SKIN_CATEGORIES)
		return -1;
	struct skin_category* cat = &skin_categories[category];
	if(entry < 0 || entry >= cat->count)
		return -1;

	struct skin_entry* e = &cat->entries[entry];
	int ok = 0;

	if(models) {
		for(int m = 0; m < cat->model_count; m++) {
			char path[256];
			if(e->suffix[0])
				snprintf(path, sizeof(path), "kv6/%s(%s).kv6",
					cat->models[m].basename, e->suffix);
			else
				snprintf(path, sizeof(path), "kv6/%s.kv6",
					cat->models[m].basename);

			if(kv6_reload(cat->models[m].ptr, path, cat->models[m].scale) == 0)
				ok = 1;
		}
	}

	for(int s = 0; s < cat->sound_count; s++) {
		char path[256];
		if(e->suffix[0]) {
			snprintf(path, sizeof(path), "wav/%s(%s).wav",
				cat->sounds[s].basename, e->suffix);
			if(!file_exists(path))
				snprintf(path, sizeof(path), "wav/%s.wav",
					cat->sounds[s].basename);
		} else {
			snprintf(path, sizeof(path), "wav/%s.wav",
				cat->sounds[s].basename);
		}
		sound_reload(cat->sounds[s].ptr, path,
			cat->sounds[s].ptr->min, cat->sounds[s].ptr->max);
	}

	char path[256];
	const char* default_png = NULL;
	struct texture* zoom_tex = NULL;

	switch(category) {
		case SKIN_RIFLE:
			default_png = "png/semi.png";
			zoom_tex = &texture_zoom_semi;
			break;
		case SKIN_SMG:
			default_png = "png/smg.png";
			zoom_tex = &texture_zoom_smg;
			break;
		case SKIN_SHOTGUN:
			default_png = "png/shotgun.png";
			zoom_tex = &texture_zoom_shotgun;
			break;
		default:
			break;
	}

	if(zoom_tex) {
		if(e->suffix[0]) {
			snprintf(path, sizeof(path), "png/%s(%s).png",
				cat->models[0].basename, e->suffix);
			if(!file_exists(path)) {
				log_warn("Skin PNG not found: %s, falling back to default", path);
				snprintf(path, sizeof(path), "%s", default_png);
			}
		} else {
			snprintf(path, sizeof(path), "%s", default_png);
		}
		texture_delete(zoom_tex);
		texture_create(zoom_tex, path);
	}

	return ok ? 0 : -1;
}

void skins_apply_all(int models) {
	skins_apply(SKIN_SPADE, settings.skin_spade, models);
	skins_apply(SKIN_GRENADE, settings.skin_grenade, models);
	skins_apply(SKIN_RIFLE, settings.skin_rifle, models);
	skins_apply(SKIN_SMG, settings.skin_smg, models);
	skins_apply(SKIN_SHOTGUN, settings.skin_shotgun, models);
	skins_apply(SKIN_PLAYER, settings.skin_player, models);
	skins_apply(SKIN_INTEL, settings.skin_intel, models);
	skins_apply(SKIN_TENT, settings.skin_tent, models);
}

void skins_render_preview(enum skin_category_type category, int entry, float cx, float cy, float size) {
	if(category < 0 || category >= SKIN_CATEGORIES)
		return;
	struct skin_category* cat = &skin_categories[category];
	if(entry < 0 || entry >= cat->count)
		return;

	mat4 saved_proj;
	memcpy(saved_proj, matrix_projection, sizeof(mat4));
	matrix_ortho(matrix_projection, 0.0, settings.window_width, 0.0, settings.window_height, -100.0, 100.0);
	matrix_upload_p();

	mat4 saved_model;
	memcpy(saved_model, matrix_model, sizeof(mat4));

	float preview_scale = size * 0.5F;
	if(category == SKIN_INTEL)
		preview_scale *= 0.25F;
	else if(category == SKIN_TENT)
		preview_scale *= 0.18F;

	matrix_identity(matrix_model);
	matrix_translate(matrix_model, cx, cy, 0.0F);
	matrix_scale(matrix_model, preview_scale, preview_scale, preview_scale);
	matrix_rotate(matrix_model, window_time() * 20.0F, 0.0F, 1.0F, 0.0F);
	matrix_rotate(matrix_model, -0.524F, 1.0F, 0.0F, 0.0F);

	for(int m = 0; m < cat->model_count; m++) {
		/* skip colored variants and dead body for player */
		if(category == SKIN_PLAYER && (m == 2 || m == 5 || m == 6))
			continue;

		struct kv6_t* model = &cat->entries[entry].previews[m];

		matrix_push(matrix_model);

		if(category == SKIN_PLAYER) {
			float offsets[][3] = {
				{0.0F, 0.9F, 0.0F},  // head
				{0.0F, 0.0F, 0.0F},  // torso
				{0.0F, 0.0F, 0.0F},  // arms
				{0.0F, -0.8F, 0.0F}, // leg
			};
			int idx = (m < 2) ? m : (m - 1);
			if(idx >= 0 && idx < 4)
				matrix_translate(matrix_model, offsets[idx][0], offsets[idx][1], offsets[idx][2]);
		} else if(category == SKIN_INTEL || category == SKIN_TENT) {
			/* center models using their pivot */
			matrix_translate(matrix_model,
				-model->xpiv * model->scale,
				-model->ypiv * model->scale,
				-model->zpiv * model->scale);
		}

		matrix_upload();
		kv6_render(model, 0);
		matrix_pop(matrix_model);
	}

	memcpy(matrix_model, saved_model, sizeof(mat4));
	memcpy(matrix_projection, saved_proj, sizeof(mat4));
	matrix_upload();
	matrix_upload_p();
}

void skins_init(void) {
	skins_scan();

	settings.skin_spade = max(0, min(settings.skin_spade, skin_categories[SKIN_SPADE].count - 1));
	settings.skin_grenade = max(0, min(settings.skin_grenade, skin_categories[SKIN_GRENADE].count - 1));
	settings.skin_rifle = max(0, min(settings.skin_rifle, skin_categories[SKIN_RIFLE].count - 1));
	settings.skin_smg = max(0, min(settings.skin_smg, skin_categories[SKIN_SMG].count - 1));
	settings.skin_shotgun = max(0, min(settings.skin_shotgun, skin_categories[SKIN_SHOTGUN].count - 1));
	settings.skin_player = max(0, min(settings.skin_player, skin_categories[SKIN_PLAYER].count - 1));
	settings.skin_intel = max(0, min(settings.skin_intel, skin_categories[SKIN_INTEL].count - 1));
	settings.skin_tent = max(0, min(settings.skin_tent, skin_categories[SKIN_TENT].count - 1));

	skins_apply_all(1);
}
