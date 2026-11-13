
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

#include "common.h"
#include "skins.h"
#include "weapon_skin_scale.h"
#include "legacy_mod_skin.h"
#include "file.h"
#include "config.h"
#include "log.h"
#include "matrix.h"
#include "glx.h"
#include "window.h"
#include "texture.h"

struct skin_category skin_categories[SKIN_CATEGORIES];

/* Flip preferences are keyed by category + filename suffix, never by the
   sorted menu index. Keep missing mods in the file across rescans/restarts. */
struct skin_flip_entry { int category; char suffix[SKIN_NAME_MAX]; };
static struct skin_flip_entry* skin_flips;
static size_t skin_flip_count;

bool skins_weapon_category(enum skin_category_type c) {
    return c == SKIN_RIFLE || c == SKIN_SMG || c == SKIN_SHOTGUN;
}
static int flip_find(int c, const char* suffix) {
    for(size_t i=0; i<skin_flip_count; i++)
        if(skin_flips[i].category == c && !strcmp(skin_flips[i].suffix, suffix)) return (int)i;
    return -1;
}
static void flip_add(int c, const char* suffix) {
    if(!skins_weapon_category(c) || strlen(suffix) >= SKIN_NAME_MAX || flip_find(c,suffix) >= 0) return;
    void* p = realloc(skin_flips,(skin_flip_count+1)*sizeof(*skin_flips));
    if(!p) { log_warn("Could not store skin flip preference"); return; }
    skin_flips=p;
    skin_flips[skin_flip_count].category=c;
    strcpy(skin_flips[skin_flip_count].suffix,suffix);
    skin_flip_count++;
}
static void flip_load(void) {
    free(skin_flips); skin_flips=NULL; skin_flip_count=0;
    if(!file_exists("skin-flips.txt")) return;
    char* data=(char*)file_load("skin-flips.txt");
    if(!data) return;
    /* file_load returns a writable, NUL-terminated heap buffer. */
    for(char* line=strtok(data,"\r\n");line;line=strtok(NULL,"\r\n")) {
        char* end;
        errno = 0;
        long category = strtol(line, &end, 10);
        if(end == line || errno == ERANGE || category < INT_MIN || category > INT_MAX ||
           !isspace((unsigned char)*end)) continue;
        while(isspace((unsigned char)*end)) end++;
        char* start = end;
        while(*end && !isspace((unsigned char)*end)) end++;
        size_t len = (size_t)(end - start);
        char hex[SKIN_NAME_MAX * 2 + 1], suffix[SKIN_NAME_MAX];
        if(!len || len >= sizeof(hex)) continue;
        memcpy(hex, start, len);
        hex[len] = 0;
        int c = (int)category;
        if(!strcmp(hex,"-")) suffix[0]=0;
        else {
            size_t n=strlen(hex); bool valid=!(n&1) && n/2 < SKIN_NAME_MAX;
            for(size_t i=0;valid && i<n/2;i++) {
                unsigned int v=0;
                for(int j=0;j<2;j++) {
                    char ch=hex[2*i+j];
                    int digit=ch>='0' && ch<='9' ? ch-'0' : ch>='a' && ch<='f' ? ch-'a'+10 : -1;
                    if(digit<0) {valid=false; break;}
                    v=v*16+(unsigned)digit;
                }
                if(!v) valid=false;
                suffix[i]=(char)v;
            }
            if(!valid) continue;
            suffix[n/2]=0;
        }
        flip_add(c,suffix);
    }
    free(data);
}
static void flip_save(void) {
    void* f=file_open("skin-flips.txt","w");
    if(!f) {log_warn("Could not save skin flip preferences"); return;}
    for(size_t i=0;i<skin_flip_count;i++) {
        file_printf(f,"%d ",skin_flips[i].category);
        const unsigned char* p=(const unsigned char*)skin_flips[i].suffix;
        if(!*p) file_printf(f,"-");
        while(*p) file_printf(f,"%02x",(unsigned)*p++);
        file_printf(f,"\n");
    }
    file_close(f);
}
bool skins_flip_get(enum skin_category_type c, int entry) {
    return skins_weapon_category(c) && entry>=0 && entry<skin_categories[c].count &&
           flip_find(c,skin_categories[c].entries[entry].suffix)>=0;
}
void skins_flip_toggle(enum skin_category_type c, int entry) {
    if(!skins_weapon_category(c) || entry<0 || entry>=skin_categories[c].count) return;
    struct skin_entry* e=&skin_categories[c].entries[entry];
    int idx=flip_find(c,e->suffix);
    if(idx>=0) {
        memmove(&skin_flips[idx],&skin_flips[idx+1],(skin_flip_count-(size_t)idx-1)*sizeof(*skin_flips));
        skin_flip_count--;
    } else flip_add(c,e->suffix);
    bool flipped=flip_find(c,e->suffix)>=0;
    e->previews[0].flip_yaw=flipped;
    int selected = c==SKIN_RIFLE ? settings.skin_rifle : c==SKIN_SMG ? settings.skin_smg : settings.skin_shotgun;
    if(selected==entry) skin_categories[c].models[0].ptr->flip_yaw=flipped;
    flip_save();
}

static int entry_compare(const void* a, const void* b) {
	const struct skin_entry* ea = (const struct skin_entry*)a;
	const struct skin_entry* eb = (const struct skin_entry*)b;
	return strcmp(ea->name, eb->name);
}

/* Apply the same sizing to the menu preview and the in-game skin.  The KV6
   pivot is already expressed in voxel coordinates; scaling the whole model
   also scales its offset, preserving the author's intended hand position. */
static int skin_reload_model(enum skin_category_type category, struct skin_category* cat,
                             int model, struct kv6_t* target, const char* path) {
	if(kv6_reload(target, path, cat->models[model].scale) != 0)
		return -1;
	if(category == SKIN_RIFLE && strcmp(path, "kv6/semi.kv6") == 0)
		target->scale = weapon_default_rifle_scale(target->scale, target->ysiz);
	else if(category == SKIN_RIFLE || category == SKIN_SMG || category == SKIN_SHOTGUN)
		target->scale = weapon_skin_scale(target->scale, target->ysiz);
	return 0;
}

/* Keep these in the same order as the player's live model definitions. */
enum skin_player_model {
	SKIN_PLAYER_HEAD, SKIN_PLAYER_TORSO, SKIN_PLAYER_TORSO_CROUCH,
	SKIN_PLAYER_ARMS, SKIN_PLAYER_LEG, SKIN_PLAYER_LEG_CROUCH,
	SKIN_PLAYER_DEAD, SKIN_PLAYER_MODEL_COUNT
};

struct skin_scan_context {
	struct skin_category* cat;
	enum skin_category_type category;
	int capacity;
};

/* file_dir_list also sees packaged Android assets, unlike opendir/stat. */
static void skin_scan_file(const char* name, void* user) {
	struct skin_scan_context* ctx = user;
	struct skin_category* cat = ctx->cat;
	char prefix[64];
	snprintf(prefix, sizeof(prefix), "%s(", cat->models[0].basename);
	size_t prefix_len = strlen(prefix), len = strlen(name);
	if(len <= prefix_len + 5 || strncmp(name, prefix, prefix_len) ||
	   strcmp(name + len - 5, ").kv6")) return;
	size_t name_len = len - prefix_len - 5;
	if(name_len >= SKIN_NAME_MAX || memchr(name + prefix_len, ')', name_len)) return;
	if(cat->count >= ctx->capacity) {
		int capacity = ctx->capacity * 2;
		void* entries = realloc(cat->entries, sizeof(struct skin_entry) * capacity);
		if(!entries) { log_warn("Could not allocate skin entry"); return; }
		cat->entries = entries;
		ctx->capacity = capacity;
	}
	struct skin_entry* e = &cat->entries[cat->count];
	memcpy(e->name, name + prefix_len, name_len);
	e->name[name_len] = 0;
	memcpy(e->suffix, e->name, name_len + 1);
	/* Keep a user skin named Default selectable without disguising it. */
	if(!strcmp(e->name, "Default"))
		snprintf(e->name, sizeof(e->name), "Default (custom)");
	e->previews = calloc(cat->model_count, sizeof(struct kv6_t));
	if(!e->previews) { log_warn("Could not allocate skin previews"); return; }
	for(int m = 0; m < cat->model_count; m++) {
		char mp[256];
		snprintf(mp, sizeof(mp), "kv6/%s(%s).kv6", cat->models[m].basename, e->suffix);
		skin_reload_model(ctx->category, cat, m, &e->previews[m], mp);
		e->previews[m].flip_yaw = skins_weapon_category(ctx->category) && flip_find(ctx->category, e->suffix) >= 0;
	}
	/* Do not offer directories or malformed models as selectable skins. */
	if(!e->previews[0].voxels) {
		for(int m = 0; m < cat->model_count; m++) free(e->previews[m].voxels);
		free(e->previews);
		return;
	}
	cat->count++;
}

void skins_scan(void) {
	for(int c = 0; c < SKIN_CATEGORIES; c++) {
		struct skin_category* cat = &skin_categories[c];

		if(cat->entries) {
			for(int i = 0; i < cat->count; i++) {
				for(int m = 0; m < cat->model_count; m++) {
					kv6_rebuild(&cat->entries[i].previews[m]);
					free(cat->entries[i].previews[m].voxels);
				}
				free(cat->entries[i].previews);
			}
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
				cat->sound_count = 2;
				cat->sounds[0] = (struct skin_sound_def){&sound_rifle_shoot, "semishoot"};
				cat->sounds[1] = (struct skin_sound_def){&sound_rifle_reload, "semireload"};
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
				cat->model_count = SKIN_PLAYER_MODEL_COUNT;
				cat->models[SKIN_PLAYER_HEAD] = (struct skin_model_def){&model_playerhead, "playerhead", 0.1F};
				cat->models[SKIN_PLAYER_TORSO] = (struct skin_model_def){&model_playertorso, "playertorso", 0.1F};
				cat->models[SKIN_PLAYER_TORSO_CROUCH] = (struct skin_model_def){&model_playertorsoc, "playertorsoc", 0.1F};
				cat->models[SKIN_PLAYER_ARMS] = (struct skin_model_def){&model_playerarms, "playerarms", 0.1F};
				cat->models[SKIN_PLAYER_LEG] = (struct skin_model_def){&model_playerleg, "playerleg", 0.1F};
				cat->models[SKIN_PLAYER_LEG_CROUCH] = (struct skin_model_def){&model_playerlegc, "playerlegc", 0.1F};
				cat->models[SKIN_PLAYER_DEAD] = (struct skin_model_def){&model_playerdead, "playerdead", 0.1F};
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

		/* Capacity is per scan; callbacks grow it as needed. */
		int capacity = 8;
		cat->entries = malloc(sizeof(struct skin_entry) * capacity);
		if(!cat->entries) {
			log_warn("Could not allocate skin category %d", c);
			continue;
		}
		strcpy(cat->entries[0].name, "Default");
		cat->entries[0].suffix[0] = 0;
		cat->entries[0].previews = calloc(cat->model_count, sizeof(struct kv6_t));
		if(!cat->entries[0].previews) {
			log_warn("Could not allocate default skin previews for category %d", c);
			free(cat->entries);
			cat->entries = NULL;
			continue;
		}
		for(int m = 0; m < cat->model_count; m++) {
			char mp[256];
			snprintf(mp, sizeof(mp), "kv6/%s.kv6", cat->models[m].basename);
			skin_reload_model((enum skin_category_type)c, cat, m, &cat->entries[0].previews[m], mp);
			cat->entries[0].previews[m].flip_yaw = skins_weapon_category(c) && flip_find(c, "") >= 0;
		}
		cat->count = 1;

		struct skin_scan_context context = {cat, (enum skin_category_type)c, capacity};
		file_dir_list("kv6", skin_scan_file, &context);

		if(cat->count > 1)
			qsort(cat->entries + 1, cat->count - 1, sizeof(struct skin_entry), entry_compare);
	}
}

/* A failed decode must not destroy the previous usable sight. */
static void skin_load_scope(struct texture* zoom_tex, char* path, const char* default_png) {
	struct texture replacement = {0};
	int loaded = texture_create(&replacement, path);
	if(!loaded && strcmp(path, default_png)) {
		log_warn("Could not load skin PNG %s, falling back to %s", path, default_png);
		loaded = texture_create(&replacement, (char*)default_png);
	}
	if(loaded) {
		texture_delete(zoom_tex);
		*zoom_tex = replacement;
	} else {
		log_warn("Could not load scope PNG %s; keeping previous sight", default_png);
	}
}

static bool sound_uses_skin_suffix(enum skin_category_type category, int sound_index, const char* suffix) {
	return suffix[0] && (category != SKIN_RIFLE || sound_index == 0 || legacy_mod_reload_suffix(suffix));
}

int skins_apply(enum skin_category_type category, int entry, int reload_models) {
	if(category < 0 || category >= SKIN_CATEGORIES)
		return -1;
	struct skin_category* cat = &skin_categories[category];
	if(entry < 0 || entry >= cat->count)
		return -1;

	struct skin_entry* e = &cat->entries[entry];
	int ok = 0;

	if(reload_models) {
		for(int m = 0; m < cat->model_count; m++) {
			if(!cat->models[m].ptr) continue;
			char path[256];
			if(e->suffix[0])
				snprintf(path, sizeof(path), "kv6/%s(%s).kv6",
					cat->models[m].basename, e->suffix);
			else
				snprintf(path, sizeof(path), "kv6/%s.kv6",
					cat->models[m].basename);

			if(skin_reload_model(category, cat, m, cat->models[m].ptr, path) == 0) {
				cat->models[m].ptr->flip_yaw = skins_weapon_category(category) && flip_find(category, e->suffix) >= 0;
				ok = 1;
			}
		}
	}

	/* If the model disappeared or is invalid, keep the previous skin's
	   tracer, sound and sight instead of applying a partial selection. */
	if(reload_models && !ok)
		return -1;

	/* Tracers follow the selected weapon skin. Unspecified models reset to
	   the base tracer instead of retaining the previous skin's projectile. */
	if(reload_models && skins_weapon_category(category)) {
		struct kv6_t* tracer = category == SKIN_RIFLE ? &model_semi_tracer :
		                      category == SKIN_SMG ? &model_smg_tracer : &model_shotgun_tracer;
		char tracer_path[256];
		if(e->suffix[0])
			snprintf(tracer_path, sizeof(tracer_path), "kv6/%stracer(%s).kv6", cat->models[0].basename, e->suffix);
		else tracer_path[0] = 0;
		int loaded = tracer_path[0] && file_exists(tracer_path) && kv6_reload(tracer, tracer_path, 0.05F) == 0;
		if(!loaded) {
			snprintf(tracer_path, sizeof(tracer_path), "kv6/%stracer.kv6", cat->models[0].basename);
			if(kv6_reload(tracer, tracer_path, 0.05F) != 0)
				log_warn("Could not load tracer: %s", tracer_path);
		}
	}

	for(int s = 0; s < cat->sound_count; s++) {
		char path[256];
		if(sound_uses_skin_suffix(category, s, e->suffix)) {
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
		skin_load_scope(zoom_tex, path, default_png);
	}

	return ok ? 0 : -1;
}

void skins_apply_all(int reload_models) {
	skins_apply(SKIN_SPADE, settings.skin_spade, reload_models);
	skins_apply(SKIN_GRENADE, settings.skin_grenade, reload_models);
	skins_apply(SKIN_RIFLE, settings.skin_rifle, reload_models);
	skins_apply(SKIN_SMG, settings.skin_smg, reload_models);
	skins_apply(SKIN_SHOTGUN, settings.skin_shotgun, reload_models);
	skins_apply(SKIN_PLAYER, settings.skin_player, reload_models);
	skins_apply(SKIN_INTEL, settings.skin_intel, reload_models);
	skins_apply(SKIN_TENT, settings.skin_tent, reload_models);
}

void skins_render_preview(enum skin_category_type category, int entry, float cx, float cy, float size) {
	if(category < 0 || category >= SKIN_CATEGORIES)
		return;
	struct skin_category* cat = &skin_categories[category];
	if(entry < 0 || entry >= cat->count)
		return;

	mat4 saved_proj, saved_view;
	memcpy(saved_view, matrix_view, sizeof(mat4));
	matrix_identity(matrix_view);
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
		if(category == SKIN_PLAYER && (m == SKIN_PLAYER_TORSO_CROUCH ||
		    m == SKIN_PLAYER_LEG_CROUCH || m == SKIN_PLAYER_DEAD))
			continue;

		struct kv6_t* model = &cat->entries[entry].previews[m];

		matrix_push(matrix_model);

		if(category == SKIN_PLAYER) {
			static const float offsets[SKIN_PLAYER_MODEL_COUNT][3] = {
				[SKIN_PLAYER_HEAD] = {0.0F, 0.9F, 0.0F},
				[SKIN_PLAYER_LEG] = {0.0F, -0.8F, 0.0F},
			};
			if(m < SKIN_PLAYER_MODEL_COUNT)
				matrix_translate(matrix_model, offsets[m][0], offsets[m][1], offsets[m][2]);
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
	memcpy(matrix_view, saved_view, sizeof(mat4));
	memcpy(matrix_projection, saved_proj, sizeof(mat4));
	matrix_upload();
	matrix_upload_p();
}

void skins_init(void) {
	flip_load();
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
