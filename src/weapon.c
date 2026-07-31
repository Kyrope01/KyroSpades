
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

#include <math.h>

#include "window.h"
#include "weapon.h"
#include "cameracontroller.h"
#include "config.h"
#include "camera.h"
#include "particle.h"
#include "map.h"
#include "tracer.h"
#include "hud.h"
#include "bloodmarks.h"
#include "damagenumbers.h"

float weapon_reload_start, weapon_last_shot;
unsigned char weapon_reload_inprogress = 0;

void weapon_update() {
	float t, delay = weapon_delay(players[local_player_id].weapon);
	int bullets = weapon_can_reload();
	switch(players[local_player_id].weapon) {
		case WEAPON_RIFLE: t = 2.5F; break;
		case WEAPON_SMG: t = 2.5F; break;
		case WEAPON_SHOTGUN: t = 0.5F * bullets; break;
	}

	if(weapon_reload_inprogress) {
		if(players[local_player_id].weapon == WEAPON_SHOTGUN) {
			if(window_time() - weapon_reload_start >= 0.5F) {
				local_player_ammo++;
				local_player_ammo_reserved--;

				struct Sound_wav* snd;
				if(local_player_ammo < 6) {
					weapon_reload_start = window_time();
					snd = &sound_shotgun_reload;
				} else {
					weapon_reload_inprogress = 0;
					snd = &sound_shotgun_cock;
				}
				sound_create(SOUND_LOCAL, snd, 0.0F, 0.0F, 0.0F);
			}
		} else {
			if(window_time() - weapon_reload_start >= t) {
				local_player_ammo += bullets;
				local_player_ammo_reserved -= bullets;
				weapon_reload_inprogress = 0;
			}
		}
		if(players[local_player_id].held_item == TOOL_GUN) {
			players[local_player_id].item_disabled = window_time();
			players[local_player_id].items_show_start = window_time();
			players[local_player_id].items_show = 1;
		}
	} else {
		if(screen_current == SCREEN_NONE && window_time() - players[local_player_id].item_disabled >= 0.5F) {
			if(players[local_player_id].input.buttons.lmb && players[local_player_id].held_item == TOOL_GUN
			   && local_player_ammo > 0 && window_time() - weapon_last_shot >= delay) {
				weapon_shoot();
				local_player_ammo = max(local_player_ammo - 1, 0);
				weapon_last_shot = window_time();
			}
		}
	}
}

float weapon_recoil_anim(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return 0.70F;
		case WEAPON_SMG: return 0.35F;
		case WEAPON_SHOTGUN: return 1.5F;
		default: return 0.0F;
	}
}

int weapon_block_damage(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return 50;
		case WEAPON_SMG: return 34;
		case WEAPON_SHOTGUN: return 20;
		default: return 0;
	}
}

/* Client-side estimate of per-hit body damage, used only to draw the cosmetic
   floating "damage numbers" HUD element. The server remains authoritative for
   actual HP -- this table (classic AoS 0.75 values) exists purely so the
   number shown to the shooter is in the right ballpark. */
int weapon_get_damage(int gun, int hit_type) {
	switch(gun) {
		case WEAPON_RIFLE:
			switch(hit_type) {
				case HITTYPE_HEAD: return 100;
				case HITTYPE_ARMS: return 33;
				case HITTYPE_LEGS: return 33;
				default: return 49; /* torso */
			}
		case WEAPON_SMG:
			switch(hit_type) {
				case HITTYPE_HEAD: return 75;
				case HITTYPE_ARMS: return 18;
				case HITTYPE_LEGS: return 18;
				default: return 29; /* torso */
			}
		case WEAPON_SHOTGUN:
			switch(hit_type) {
				case HITTYPE_HEAD: return 37;
				case HITTYPE_ARMS: return 16;
				case HITTYPE_LEGS: return 16;
				default: return 27; /* torso */
			}
		default: return 0;
	}
}

float weapon_delay(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return 0.5F;
		case WEAPON_SMG: return 0.1F;
		case WEAPON_SHOTGUN: return 1.0F;
		default: return 0.0F;
	}
}

struct Sound_wav* weapon_sound(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return &sound_rifle_shoot;
		case WEAPON_SMG: return &sound_smg_shoot;
		case WEAPON_SHOTGUN: return &sound_shotgun_shoot;
		default: return NULL;
	}
}

struct Sound_wav* weapon_sound_reload(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return &sound_rifle_reload;
		case WEAPON_SMG: return &sound_smg_reload;
		case WEAPON_SHOTGUN: return &sound_shotgun_reload;
		default: return NULL;
	}
}

void weapon_spread(struct Player* p, float* d) {
	float spread = 0.0F;
	switch(p->weapon) {
		case WEAPON_RIFLE: spread = 0.006F; break;
		case WEAPON_SMG: spread = 0.012F; break;
		case WEAPON_SHOTGUN: spread = 0.024F; break;
	}
	d[0] += (ms_rand() - ms_rand()) / 16383.0F * spread * (p->input.buttons.rmb ? 0.5F : 1.0F)
		* ((p->input.keys.crouch && p->weapon != WEAPON_SHOTGUN) ? 0.5F : 1.0F);
	d[1] += (ms_rand() - ms_rand()) / 16383.0F * spread * (p->input.buttons.rmb ? 0.5F : 1.0F)
		* ((p->input.keys.crouch && p->weapon != WEAPON_SHOTGUN) ? 0.5F : 1.0F);
	d[2] += (ms_rand() - ms_rand()) / 16383.0F * spread * (p->input.buttons.rmb ? 0.5F : 1.0F)
		* ((p->input.keys.crouch && p->weapon != WEAPON_SHOTGUN) ? 0.5F : 1.0F);
}

void weapon_recoil(int gun, double* horiz_recoil, double* vert_recoil) {
	switch(gun) {
		case WEAPON_RIFLE:
			*horiz_recoil = 0.0001;
			*vert_recoil = 0.050000001;
			break;
		case WEAPON_SMG:
			*horiz_recoil = 0.00005;
			*vert_recoil = 0.0125;
			break;
		case WEAPON_SHOTGUN:
			*horiz_recoil = 0.0002;
			*vert_recoil = 0.1;
			break;
		default: *horiz_recoil = 0.0F; *vert_recoil = 0.0F;
	}
}

int weapon_ammo(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return 10;
		case WEAPON_SMG: return 30;
		case WEAPON_SHOTGUN: return 6;
		default: return 0;
	}
}

int weapon_ammo_reserved(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return 50;
		case WEAPON_SMG: return 120;
		case WEAPON_SHOTGUN: return 48;
		default: return 0;
	}
}

struct kv6_t* weapon_casing(int gun) {
	switch(gun) {
		case WEAPON_RIFLE: return &model_semi_casing;
		case WEAPON_SMG: return &model_smg_casing;
		case WEAPON_SHOTGUN: return &model_shotgun_casing;
		default: return NULL;
	}
}

void weapon_set(bool restock) {
	if(!restock)
		local_player_ammo = weapon_ammo(players[local_player_id].weapon);

	local_player_ammo_reserved = weapon_ammo_reserved(players[local_player_id].weapon);
	weapon_reload_inprogress = 0;
}

void weapon_reload() {
	if(local_player_ammo_reserved == 0 || weapon_reload_inprogress || !weapon_can_reload())
		return;

	weapon_reload_start = window_time();
	weapon_reload_inprogress = 1;

	sound_create(SOUND_LOCAL, weapon_sound_reload(players[local_player_id].weapon), players[local_player_id].pos.x,
				 players[local_player_id].pos.y, players[local_player_id].pos.z);

	struct PacketWeaponReload reloadp;
	reloadp.player_id = local_player_id;
	reloadp.ammo = local_player_ammo;
	reloadp.reserved = local_player_ammo_reserved;
	network_send(PACKET_WEAPONRELOAD_ID, &reloadp, sizeof(reloadp));
}

void weapon_reload_abort() {
	if(weapon_reload_inprogress && players[local_player_id].weapon == WEAPON_SHOTGUN) {
		weapon_reload_inprogress = 0;
		players[local_player_id].items_show = 0;
		players[local_player_id].item_showup = 0;
		players[local_player_id].item_disabled = 0;
	}
}

int weapon_reloading() {
	return weapon_reload_inprogress;
}

int weapon_can_reload() {
	int mag_size = weapon_ammo(players[local_player_id].weapon);
	return max(min(min(local_player_ammo_reserved, mag_size), mag_size - local_player_ammo), 0);
}

void weapon_shoot() {
	// see this for reference:
	// https://pastebin.com/raw/TMjKSTXG
	// http://paste.quacknet.org/view/a3ea2743

	for(int i = 0; i < ((players[local_player_id].weapon == WEAPON_SHOTGUN) ? 8 : 1); i++) {
		float o[3] = {players[local_player_id].orientation.x, players[local_player_id].orientation.y,
					  players[local_player_id].orientation.z};

		weapon_spread(&players[local_player_id], o);

		struct Camera_HitType hit;
		camera_hit(&hit, local_player_id, players[local_player_id].physics.eye.x,
				   players[local_player_id].physics.eye.y + player_height(&players[local_player_id]),
				   players[local_player_id].physics.eye.z, o[0], o[1], o[2], 128.0F);

		if(players[local_player_id].input.buttons.packed != network_buttons_last) {
			struct PacketWeaponInput in;
			in.player_id = local_player_id;
			in.primary = players[local_player_id].input.buttons.lmb;
			in.secondary = players[local_player_id].input.buttons.rmb;
			network_send(PACKET_WEAPONINPUT_ID, &in, sizeof(in));

			network_buttons_last = players[local_player_id].input.buttons.packed;
		}

		struct PacketOrientationData orient;
		orient.x = players[local_player_id].orientation.x;
		orient.y = players[local_player_id].orientation.z;
		orient.z = -players[local_player_id].orientation.y;
		network_send(PACKET_ORIENTATIONDATA_ID, &orient, sizeof(orient));

		if(hit.y == 0 && hit.type == CAMERA_HITTYPE_BLOCK)
			hit.type = CAMERA_HITTYPE_NONE;
		switch(hit.type) {
			case CAMERA_HITTYPE_PLAYER: {
				if(hit.player_section == HITTYPE_HEAD) {
					sound_create(SOUND_LOCAL, &sound_headshot, 0.0F, 0.0F, 0.0F);
				} else {
					sound_create(SOUND_LOCAL, &sound_hitbody, 0.0F, 0.0F, 0.0F);
				}
				float hit_x = players[hit.player_id].physics.eye.x;
				float hit_y = players[hit.player_id].physics.eye.y + player_section_height(hit.player_section);
				float hit_z = players[hit.player_id].physics.eye.z;
				particle_create(0x0000FF, hit_x, hit_y, hit_z, 3.5F, 1.0F, 8, 0.1F, 0.4F);

				/* Blood decal: spatter along the bullet's flight direction so
				   it lands on whatever surface is behind the victim. */
				bloodmarks_spatter(hit_x, hit_y, hit_z, o[0] * 12.0F, o[1] * 12.0F, o[2] * 12.0F, true);

				/* Floating damage number: client-side estimate only, purely
				   cosmetic feedback for the shooter. The server remains
				   authoritative for actual HP. */
				int dmg = weapon_get_damage(players[local_player_id].weapon, hit.player_section);
				damagenumbers_add(hit.player_id, dmg, hit_x, hit_y, hit_z);

				struct PacketHit h;
				h.player_id = hit.player_id;
				h.hit_type = hit.player_section;
				network_send(PACKET_HIT_ID, &h, sizeof(h));
				// printf("hit on %s (%i)\n", players[hit.player_id].name, h.hit_type);
				break;
			}
			case CAMERA_HITTYPE_BLOCK:
				map_damage(hit.x, hit.y, hit.z, weapon_block_damage(players[local_player_id].weapon));
				if(map_damage_action(hit.x, hit.y, hit.z) && hit.y > 1) {
					struct PacketBlockAction blk;
					blk.action_type = ACTION_DESTROY;
					blk.player_id = local_player_id;
					blk.x = hit.x;
					blk.y = hit.z;
					blk.z = 63 - hit.y;
					network_send(PACKET_BLOCKACTION_ID, &blk, sizeof(blk));
					// read_PacketBlockAction(&blk,sizeof(blk));
				} else {
					particle_create(map_get(hit.x, hit.y, hit.z), hit.xb + 0.5F, hit.yb + 0.5F, hit.zb + 0.5F, 2.5F,
									1.0F, 4, 0.1F, 0.25F);
				}
				break;
		}

		tracer_pvelocity(o, &players[local_player_id]);
		tracer_add(players[local_player_id].weapon, players[local_player_id].physics.eye.x,
				   players[local_player_id].physics.eye.y + player_height(&players[local_player_id]),
				   players[local_player_id].physics.eye.z, o[0], o[1], o[2]);
	}

	double horiz_recoil, vert_recoil;
	weapon_recoil(players[local_player_id].weapon, &horiz_recoil, &vert_recoil);

	long triangle_wave = (long)(window_time() * 1000) & 511;
	horiz_recoil *= ((double)triangle_wave - 255.5);

	if(((long)(window_time() * 1000) & 1023) < 512) {
		horiz_recoil *= -1.0;
	}

	if((players[local_player_id].input.keys.up || players[local_player_id].input.keys.down
		|| players[local_player_id].input.keys.left || players[local_player_id].input.keys.right)
	   && !players[local_player_id].input.buttons.rmb) {
		vert_recoil *= 2.0;
		horiz_recoil *= 2.0;
	}
	if(players[local_player_id].physics.airborne) {
		vert_recoil *= 2.0;
		horiz_recoil *= 2.0;
	} else {
		if(players[local_player_id].input.keys.crouch) {
			vert_recoil *= 0.5;
			horiz_recoil *= 0.5;
		}
	}

	// converges to 0 for (+/-) 1.0, (only slowly, has no effect on usual orientation.y range)
	horiz_recoil *= sqrt(1.0F
						 - players[local_player_id].orientation.y * players[local_player_id].orientation.y
							 * players[local_player_id].orientation.y * players[local_player_id].orientation.y);

	camera_rot_x += horiz_recoil;
	camera_rot_y -= vert_recoil;

	camera_overflow_adjust();

	sound_create(SOUND_LOCAL, weapon_sound(players[local_player_id].weapon), players[local_player_id].pos.x,
				 players[local_player_id].pos.y, players[local_player_id].pos.z);
	particle_create_casing(&players[local_player_id]);
}


