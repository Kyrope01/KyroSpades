
#ifndef SKINS_H
#define SKINS_H

#include "model.h"
#include "sound.h"

#define SKIN_NAME_MAX 48
#define SKIN_CATEGORIES 8
#define SKIN_MODELS_MAX 8
#define SKIN_SOUNDS_MAX 4

enum skin_category_type {
	SKIN_SPADE,
	SKIN_GRENADE,
	SKIN_RIFLE,
	SKIN_SMG,
	SKIN_SHOTGUN,
	SKIN_PLAYER,
	SKIN_INTEL,
	SKIN_TENT,
};

struct skin_entry {
	char name[SKIN_NAME_MAX];
	char suffix[SKIN_NAME_MAX];
	struct kv6_t* previews;
};

struct skin_model_def {
	struct kv6_t* ptr;
	const char* basename;
	float scale;
};

struct skin_sound_def {
	struct Sound_wav* ptr;
	const char* basename;
};

struct skin_category {
	char label[32];
	struct skin_model_def models[SKIN_MODELS_MAX];
	int model_count;
	struct skin_sound_def sounds[SKIN_SOUNDS_MAX];
	int sound_count;
	struct skin_entry* entries;
	int count;
};

extern struct skin_category skin_categories[SKIN_CATEGORIES];

void skins_init(void);
void skins_scan(void);
int skins_apply(enum skin_category_type category, int entry, int models);
void skins_apply_all(int models);
void skins_render_preview(enum skin_category_type category, int entry, float cx, float cy, float size);

#endif


