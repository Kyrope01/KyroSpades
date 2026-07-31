
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

#include <enet/enet.h>
#include <math.h>
#include <string.h>
#include <stddef.h>

#include "libdeflate.h"
#include "texture.h"
#include "common.h"
#include "sound.h"
#include "weapon.h"
#include "grenade.h"
#include "camera.h"
#include "cameracontroller.h"
#include "file.h"
#include "hud.h"
#include "map.h"
#include "player.h"
#include "network.h"
#include "particle.h"
#include "texture.h"
#include "chunk.h"
#include "skins.h"
#include "config.h"
#include "demo.h"
#include "window.h"
#include "bloodmarks.h"
#include "damagenumbers.h"

void (*packets[256])(void* data, int len) = {NULL};

int network_connected = 0;
int network_logged_in = 0;
int network_map_transfer = 0;
int network_map_transfer_end = 0;
int network_map_cached = 0;
int network_received_packets = 0;
char network_current_ip[64] = {0};
int  network_current_port = 0;

float network_pos_update = 0.0F;
struct Position network_pos_last;
float network_orient_update = 0.0F;
struct Orientation network_orient_last;
unsigned char network_keys_last = 0;
unsigned char network_buttons_last = 0;
unsigned char network_tool_last = 255;

void* compressed_chunk_data;
int compressed_chunk_data_size;
int compressed_chunk_data_offset = 0;
int compressed_chunk_data_estimate = 0;

struct network_stat network_stats[40];
float network_stats_last = 0.0F;

ENetHost* client;
ENetPeer* peer;

char network_custom_reason[17];

void network_init_host() {
        client = enet_host_create(NULL, 1, 1, 0, 0); // limit bandwidth here if you want to
        enet_host_compress_with_range_coder(client);
}

static inline char team_color_char(int team) {
        switch(team) {
                case TEAM_1: return '\1'; break;
                case TEAM_2: return '\2'; break;
                case TEAM_SPECTATOR: return '\3'; break;
                default: return '\6'; break;
        }
} 

const char* network_reason_disconnect(int code) {
        if(*network_custom_reason)
                return network_custom_reason;
        switch(code) {
                case 1: return "Banned";
                case 2: return "Connection limit";
                case 3: return "Wrong protocol";
                case 4: return "Server full";
                case 5: return "Server shutdown";
                case 10: return "Kicked";
                case 20: return "Invalid name";
                default: return "Unknown";
        }
}

static void printJoinMsg(int team, char* name) {
        /* Default labels when StateData hasn't populated team names yet (some
           pyspades configs send empty team names, others arrive with the
           join packet ahead of the state packet on weird networks). Without
           the fallback, the format string collapses "%c%s\6 team" into
           "<color>\6 team" and the user sees "joined the   team" with two
           spaces and no name in between - which is the actual bug report. */
        char* t;
        switch(team) {
                case TEAM_1:
                        t = (gamestate.team_1.name[0] != 0) ? gamestate.team_1.name : "Blue";
                        break;
                case TEAM_2:
                        t = (gamestate.team_2.name[0] != 0) ? gamestate.team_2.name : "Green";
                        break;
                default:
                case TEAM_SPECTATOR: t = "Spectator"; break;
        }
        /* Buffer was previously 64 - tight against 16-char names + 10-char
           team names + the format scaffolding. Bumped to 128 so we don't
           silently truncate the closing "\6 team" off the end. */
        char s[128];
        char team_color = team_color_char(team);
        snprintf(s, sizeof(s), "%c%s\6 joined the %c%s\6 team",
                         team_color, name, team_color, t);
        chat_add(0, hud_accent_color(), s);
}

void read_PacketMapChunk(void* data, int len) {
        if(demo_is_seeking()) return;
        // increase allocated memory if it is not enough to store the next chunk
        if(compressed_chunk_data_offset + len > compressed_chunk_data_size) {
                compressed_chunk_data_size += 1024 * 1024;
                compressed_chunk_data = realloc(compressed_chunk_data, compressed_chunk_data_size);
                CHECK_ALLOCATION_ERROR(compressed_chunk_data)
        }
        // accept any chunk length for "superior" performance, as pointed out by github/NotAFile
        memcpy(compressed_chunk_data + compressed_chunk_data_offset, data, len);
        compressed_chunk_data_offset += len;
}

void read_PacketChatMessage(void* data, int len) {
        struct PacketChatMessage* p = (struct PacketChatMessage*)data;
        char n[32] = {0};
        char m[256];
        switch(p->chat_type) {
                case CHAT_ERROR: sound_create(SOUND_LOCAL, &sound_beep2, 0.0F, 0.0F, 0.0F);
                case CHAT_BIG: chat_showpopup(p->message, 5.0F, rgb(255, 0, 0)); return;
                case CHAT_INFO: chat_showpopup(p->message, 5.0F, rgb(255, 255, 255)); return;
                case CHAT_WARNING:
                        sound_create(SOUND_LOCAL, &sound_beep1, 0.0F, 0.0F, 0.0F);
                        chat_showpopup(p->message, 5.0F, rgb(255, 255, 0));
                        return;
                case CHAT_SYSTEM:
                        // try to determine gamemode

                        if(p->player_id == 255) {
                                strncpy(network_custom_reason, p->message, 16);
                                network_custom_reason[16] = 0;
                                return; // dont add message to chat
                        }
                        m[0] = 0;
                        break;
                case CHAT_ALL:
                case CHAT_TEAM:
                        if(p->player_id < PLAYERS_MAX && players[p->player_id].connected) {
                                strncpy(n, players[p->player_id].name, sizeof(n) - 1);
                                n[sizeof(n) - 1] = 0;

                                char color = team_color_char(players[p->player_id].team);

                                if(p->chat_type == CHAT_ALL) {
                                        sprintf(m, "[Global] %c%s\6: ", color, n);
                                } else {
                                        sprintf(m, "%c%s\6: ", color, n);
                                }
                        } else {
                                m[0] = 0;
                        }
                        break;
        }

        size_t m_remaining = sizeof(m) - 1 - strlen(m);
        size_t body_len = len - offsetof(struct PacketChatMessage, message);
        if(body_len > m_remaining) {
                body_len = m_remaining;
        }
        strncat(m, p->message, body_len);

        unsigned int color;
        switch(p->chat_type) {
                case CHAT_ALL:
                case CHAT_TEAM:
                        switch(players[p->player_id].connected ? players[p->player_id].team : players[local_player_id].team) {
                                case TEAM_1: color = rgb(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue); break;
                                case TEAM_2: color = rgb(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue); break;
                                case TEAM_SPECTATOR: color = rgb(255, 255, 255); break;
                                default: color = rgb(0, 0, 0); break;
                        }
                        break;
                default: color = 0xFFFFFF; break;
        }

        chat_add(0, color, m);
}

void read_PacketBlockAction(void* data, int len) {
        if(len < (int)sizeof(struct PacketBlockAction)) {
                log_warn("Short PacketBlockAction: %d < %zu", len, sizeof(struct PacketBlockAction));
                return;
        }
        struct PacketBlockAction* p = (struct PacketBlockAction*)data;
        switch(p->action_type) {
                case ACTION_DESTROY:
                        if((63 - p->z) > 0) {
                                int col = map_get(p->x, 63 - p->z, p->y);
                                map_set(p->x, 63 - p->z, p->y, 0xFFFFFFFF);
                                map_update_physics(p->x, 63 - p->z, p->y);
                                if(!demo_mute_effects()) particle_create(col, p->x + 0.5F, 63 - p->z + 0.5F, p->y + 0.5F, 2.5F, 1.0F, 8, 0.1F, 0.25F);
                        }
                        break;
                case ACTION_GRENADE:
                        for(int y = (63 - (p->z)) - 1; y <= (63 - (p->z)) + 1; y++) {
                                for(int z = (p->y) - 1; z <= (p->y) + 1; z++) {
                                        for(int x = (p->x) - 1; x <= (p->x) + 1; x++) {
                                                if(y > 1) {
                                                        map_set(x, y, z, 0xFFFFFFFF);
                                                        map_update_physics(x, y, z);
                                                }
                                        }
                                }
                        }
                        break;
                case ACTION_SPADE:
                        if((63 - p->z - 1) > 1) {
                                map_set(p->x, 63 - p->z - 1, p->y, 0xFFFFFFFF);
                                map_update_physics(p->x, 63 - p->z - 1, p->y);
                        }
                        if((63 - p->z + 0) > 1) {
                                int col = map_get(p->x, 63 - p->z, p->y);
                                map_set(p->x, 63 - p->z + 0, p->y, 0xFFFFFFFF);
                                map_update_physics(p->x, 63 - p->z + 0, p->y);
                                if(!demo_mute_effects()) particle_create(col, p->x + 0.5F, 63 - p->z + 0.5F, p->y + 0.5F, 2.5F, 1.0F, 8, 0.1F, 0.25F);
                        }
                        if((63 - p->z + 1) > 1) {
                                map_set(p->x, 63 - p->z + 1, p->y, 0xFFFFFFFF);
                                map_update_physics(p->x, 63 - p->z + 1, p->y);
                        }
                        break;
                case ACTION_BUILD:
                        if(p->player_id < PLAYERS_MAX) {
                                if(settings.player_stats && p->player_id == local_player_id) {
                                        player_stats_blocks_placed++;
                                }
                                bool play_sound = map_isair(p->x, 63 - p->z, p->y);

                                map_set(p->x, 63 - p->z, p->y,
                                                players[p->player_id].block.red | (players[p->player_id].block.green << 8)
                                                        | (players[p->player_id].block.blue << 16));

                                if(play_sound)
                                        if(!demo_mute_effects()) sound_create(SOUND_WORLD, &sound_build, p->x + 0.5F, 63 - p->z + 0.5F, p->y + 0.5F);
                        }
                        break;
        }
}

void read_PacketBlockLine(void* data, int len) {
        if(len < (int)sizeof(struct PacketBlockLine)) {
                log_warn("Short PacketBlockLine: %d < %zu", len, sizeof(struct PacketBlockLine));
                return;
        }
        struct PacketBlockLine* p = (struct PacketBlockLine*)data;
        if(p->player_id >= PLAYERS_MAX) {
                return;
        }
        if(settings.player_stats && p->player_id == local_player_id) {
                if(p->sx == p->ex && p->sy == p->ey && p->sz == p->ez) {
                        player_stats_blocks_placed++;
                } else {
                        struct Point blocks[64];
                        int blen = map_cube_line(p->sx, p->sy, p->sz, p->ex, p->ey, p->ez, blocks);
                        player_stats_blocks_placed += blen;
                }
        }
        if(p->sx == p->ex && p->sy == p->ey && p->sz == p->ez) {
                map_set(p->sx, 63 - p->sz, p->sy,
                                players[p->player_id].block.red | (players[p->player_id].block.green << 8)
                                        | (players[p->player_id].block.blue << 16));
        } else {
                struct Point blocks[64];
                int len = map_cube_line(p->sx, p->sy, p->sz, p->ex, p->ey, p->ez, blocks);
                while(len > 0) {
                        if(map_isair(blocks[len - 1].x, 63 - blocks[len - 1].z, blocks[len - 1].y)) {
                                map_set(blocks[len - 1].x, 63 - blocks[len - 1].z, blocks[len - 1].y,
                                                players[p->player_id].block.red | (players[p->player_id].block.green << 8)
                                                        | (players[p->player_id].block.blue << 16));
                        }
                        len--;
                }
        }
        if(!demo_mute_effects()) sound_create(SOUND_WORLD, &sound_build, (p->sx + p->ex) * 0.5F + 0.5F, (63 - p->sz + 63 - p->ez) * 0.5F + 0.5F,
                                 (p->sy + p->ey) * 0.5F + 0.5F);
}

void read_PacketStateData(void* data, int len) {
        /* PacketStateData has a variable on-wire size: it ends with a union
           that is sizeof(GM_CTF)=52 bytes for CTF servers or sizeof(GM_TC)=209
           bytes for TC servers (with #pragma pack(1) from network.h). The C
           struct always pads the union to the larger member (209), so
           sizeof(struct PacketStateData)=240 — but a CTF server only sends
           ~83 bytes. A naive "len < sizeof(struct)" check would reject every
           CTF server and leave the client stuck on the loading screen with
           random sounds playing.

           Validate against the *header* size (everything before the gamemode
           union) first, then re-validate against the gamemode-specific size
           once we know which one it is. */
        const size_t header_size = offsetof(struct PacketStateData, gamemode_data);
        if(len < (int)header_size) {
                log_warn("Short PacketStateData: %d < %zu (header)", len, header_size);
                return;
        }
        struct PacketStateData* p = (struct PacketStateData*)data;

        /* Now peek at p->gamemode to determine the expected full size. */
        size_t expected_size;
        switch(p->gamemode) {
                case GAMEMODE_CTF: expected_size = header_size + sizeof(struct GM_CTF); break;
                case GAMEMODE_TC:  expected_size = header_size + sizeof(struct GM_TC);  break;
                default:
                        log_warn("PacketStateData: unknown gamemode %d", p->gamemode);
                        return;
        }
        if((size_t)len < expected_size) {
                log_warn("Short PacketStateData for gamemode %d: %d < %zu",
                                 p->gamemode, len, expected_size);
                return;
        }

        memcpy(gamestate.team_1.name, p->team_1_name, sizeof(p->team_1_name));
        gamestate.team_1.name[sizeof(p->team_1_name)] = 0;

        gamestate.team_1.red = p->team_1_red;
        gamestate.team_1.green = p->team_1_green;
        gamestate.team_1.blue = p->team_1_blue;

        memcpy(gamestate.team_2.name, p->team_2_name, sizeof(p->team_1_name));
        gamestate.team_2.name[sizeof(p->team_1_name)] = 0;

        gamestate.team_2.red = p->team_2_red;
        gamestate.team_2.green = p->team_2_green;
        gamestate.team_2.blue = p->team_2_blue;

        gamestate.gamemode_type = p->gamemode;

        switch(p->gamemode) {
                case GAMEMODE_CTF: memcpy(&gamestate.gamemode, &p->gamemode_data, sizeof(struct GM_CTF)); break;
                case GAMEMODE_TC: memcpy(&gamestate.gamemode, &p->gamemode_data, sizeof(struct GM_TC)); break;
                default: log_error("Unknown gamemode!");
        }

        if(!demo_mute_effects())
                sound_create(SOUND_LOCAL, &sound_intro, 0.0F, 0.0F, 0.0F);

        fog_color[0] = p->fog_red / 255.0F;
        fog_color[1] = p->fog_green / 255.0F;
        fog_color[2] = p->fog_blue / 255.0F;

        if(demo_is_playing()) {
                if(!demo_is_seeking()) {
                        local_player_id = PLAYERS_MAX - 1;
                        player_reset(&players[local_player_id]);
                        players[local_player_id].connected = 0;
                        players[local_player_id].alive = 0;
                        players[local_player_id].team = TEAM_SPECTATOR;
                        local_player_health = 100;
                        local_player_blocks = 50;
                        local_player_grenades = 3;
                        weapon_set(false);
                        camera_mode = CAMERAMODE_SPECTATOR;
                        screen_current = SCREEN_NONE;
                        cameracontroller_reset_spectator_velocity();
                }
        } else {
                local_player_id = p->player_id;
                local_player_health = 100;
                local_player_blocks = 50;
                local_player_grenades = 3;
                weapon_set(false);

                players[local_player_id].block.red = 111;
                players[local_player_id].block.green = 111;
                players[local_player_id].block.blue = 111;

                camera_mode = CAMERAMODE_SELECTION;
                screen_current = SCREEN_TEAM_SELECT;
        }
        network_map_transfer_end = 1;
        chat_popup_duration = 0;

        log_info("map data was %i bytes", compressed_chunk_data_offset);
        if(!network_map_cached && compressed_chunk_data && compressed_chunk_data_offset > 0) {
                int avail_size = 1024 * 1024;
                void* decompressed = malloc(avail_size);
                CHECK_ALLOCATION_ERROR(decompressed)
                size_t decompressed_size;
                struct libdeflate_decompressor* d = libdeflate_alloc_decompressor();
                while(1) {
                        int r = libdeflate_zlib_decompress(d, compressed_chunk_data, compressed_chunk_data_offset, decompressed,
                                                                                           avail_size, &decompressed_size);
                        // switch not fancy enough here, breaking out of the loop is not aesthetic
                        if(r == LIBDEFLATE_INSUFFICIENT_SPACE) {
                                avail_size += 1024 * 1024;
                                decompressed = realloc(decompressed, avail_size);
                                CHECK_ALLOCATION_ERROR(decompressed)
                        }
                        if(r == LIBDEFLATE_SUCCESS) {
                                log_debug("Map decompressed: %zu bytes", decompressed_size);
                                demo_playback_save_initial_map(decompressed, decompressed_size);
                                if(!demo_is_seeking())
                                        map_vxl_load(decompressed, decompressed_size);
/*#ifndef USE_TOUCH
                                char filename[128];
                                sprintf(filename, "cache/%08X.vxl", libdeflate_crc32(0, decompressed, decompressed_size));
                                log_info("%s", filename);
                                FILE* f = fopen(filename, "wb");
                                fwrite(decompressed, 1, decompressed_size, f);
                                fclose(f);
#endif*/
                                chunk_rebuild_all();
                                break;
                        }
                        if(r == LIBDEFLATE_BAD_DATA || r == LIBDEFLATE_SHORT_OUTPUT) {
                                log_debug("Map decompression failed: r=%i", r);
                                break;
                        }
                }
free(decompressed);
                libdeflate_free_decompressor(d);
        }
        compressed_chunk_data_offset = 0;
        skins_apply_all(0);
}
void read_PacketFogColor(void* data, int len) {
        if(len < (int)sizeof(struct PacketFogColor)) {
                log_warn("Short PacketFogColor: %d < %zu", len, sizeof(struct PacketFogColor));
                return;
        }
        struct PacketFogColor* p = (struct PacketFogColor*)data;
        fog_color[0] = p->red / 255.0F;
        fog_color[1] = p->green / 255.0F;
        fog_color[2] = p->blue / 255.0F;
}

void read_PacketExistingPlayer(void* data, int len) {
        /* PacketExistingPlayer has a variable on-wire size: the trailing
           name[17] field is sent as a null-terminated string, so a 5-char
           name produces a 17-byte packet (11-byte header + 6 bytes name)
           instead of the full sizeof(struct)=28. The client itself sends
           this packet variable-length (see hud.c), and pyspades servers
           echo it back the same way. A naive "len < sizeof(struct)" check
           would reject every short-name response and prevent team join. */
        const size_t header_size = offsetof(struct PacketExistingPlayer, name);
        if(len < (int)header_size + 1) {
                log_warn("Short PacketExistingPlayer: %d < %zu", len, header_size + 1);
                return;
        }
        struct PacketExistingPlayer* p = (struct PacketExistingPlayer*)data;
        /* Cap the name at the bytes actually received and force null-term
           so the strcpy below cannot read past the packet buffer. */
        size_t name_bytes = (size_t)len - header_size;
        if(name_bytes > sizeof(p->name)) name_bytes = sizeof(p->name);
        ((char*)p->name)[name_bytes - 1] = 0;
        if(p->player_id < PLAYERS_MAX) {
                if(!players[p->player_id].connected)
                        printJoinMsg(p->team, p->name);
                player_save_corpse(p->player_id);
                player_reset(&players[p->player_id]);
                players[p->player_id].connected = 1;
                players[p->player_id].alive = 1;
                players[p->player_id].team = p->team;
                players[p->player_id].weapon = p->weapon;
                players[p->player_id].held_item = p->held_item;
                player_on_held_item_change(&players[p->player_id]);
                players[p->player_id].score = p->kills;
                players[p->player_id].block.red = p->red;
                players[p->player_id].block.green = p->green;
                players[p->player_id].block.blue = p->blue;
                players[p->player_id].ammo = weapon_ammo(p->weapon);
                players[p->player_id].ammo_reserved = weapon_ammo_reserved(p->weapon);
                strcpy(players[p->player_id].name, p->name);
        }
}

void read_PacketCreatePlayer(void* data, int len) {
        /* Like PacketExistingPlayer, PacketCreatePlayer's trailing name[17]
           is sent variable-length with null termination. Validate against
           the header size + at least 1 byte of name, then cap and
           null-terminate the name within the actual received bounds. */
        const size_t header_size = offsetof(struct PacketCreatePlayer, name);
        if(len < (int)header_size + 1) {
                log_warn("Short PacketCreatePlayer: %d < %zu", len, header_size + 1);
                return;
        }
        struct PacketCreatePlayer* p = (struct PacketCreatePlayer*)data;
        size_t name_bytes = (size_t)len - header_size;
        if(name_bytes > sizeof(p->name)) name_bytes = sizeof(p->name);
        ((char*)p->name)[name_bytes - 1] = 0;
        if(p->player_id < PLAYERS_MAX) {
                if(!players[p->player_id].connected)
                        printJoinMsg(p->team, p->name);
                player_save_corpse(p->player_id);
                log_debug("Player created: id=%i name=%s team=%i", p->player_id, p->name, p->team);
                player_reset(&players[p->player_id]);
                players[p->player_id].connected = 1;
                players[p->player_id].alive = 1;
                players[p->player_id].team = p->team;
                players[p->player_id].held_item = TOOL_GUN;
                player_on_held_item_change(&players[p->player_id]);
                players[p->player_id].weapon = p->weapon;
                players[p->player_id].pos.x = p->x;
                players[p->player_id].pos.y = 63.0F - p->z;
                players[p->player_id].pos.z = p->y;
                strcpy(players[p->player_id].name, p->name);
                players[p->player_id].orientation.x = players[p->player_id].orientation_smooth.x
                        = (p->team == TEAM_1) ? 1.0F : -1.0F;
                players[p->player_id].orientation.y = players[p->player_id].orientation_smooth.y = 0.0F;
                players[p->player_id].orientation.z = players[p->player_id].orientation_smooth.z = 0.0F;

                players[p->player_id].block.red = 111;
                players[p->player_id].block.green = 111;
                players[p->player_id].block.blue = 111;
                players[p->player_id].ammo = weapon_ammo(p->weapon);
                players[p->player_id].ammo_reserved = weapon_ammo_reserved(p->weapon);
                if(p->player_id == local_player_id && !demo_is_playing()) {
                        if(p->team == TEAM_SPECTATOR) {
                                camera_x = p->x;
                                camera_y = 63.0F - p->z;
                                camera_z = p->y;
                        }
                        camera_mode = (p->team == TEAM_SPECTATOR) ? CAMERAMODE_SPECTATOR : CAMERAMODE_FPS;
                        camera_rot_x = (p->team == TEAM_1) ? 0.5F * PI : 1.5F * PI;
                        camera_rot_y = 0.5F * PI;
                        // Reset spectator velocity when entering spectator mode
                        if(camera_mode == CAMERAMODE_SPECTATOR) {
                                cameracontroller_reset_spectator_velocity();
                        }
                        network_logged_in = 1;
                        local_player_health = 100;
                        local_player_blocks = 50;
                        local_player_grenades = 3;
                        local_player_lasttool = TOOL_GUN;
                        weapon_set(false);
                        skins_apply_all(0);
                }
        }
}

void read_PacketPlayerLeft(void* data, int len) {
        if(len < (int)sizeof(struct PacketPlayerLeft)) {
                log_warn("Short PacketPlayerLeft: %d < %zu", len, sizeof(struct PacketPlayerLeft));
                return;
        }
        struct PacketPlayerLeft* p = (struct PacketPlayerLeft*)data;
        if(p->player_id < PLAYERS_MAX) {
                log_debug("Player left: id=%i name=%s", p->player_id, players[p->player_id].name);
                player_save_corpse(p->player_id);
                players[p->player_id].connected = 0;
                players[p->player_id].alive = 0;
                players[p->player_id].score = 0;
                char s[32];
                char team_color = team_color_char(players[p->player_id].team);

                sprintf(s, "%c%s\6 disconnected", team_color, players[p->player_id].name);
                chat_add(0, hud_accent_color(), s);
        }
}

void read_PacketMapStart(void* data, int len) {
        if(demo_is_seeking()) return;
        player_clear_corpses();
        bloodmarks_clear();
        damagenumbers_clear();
        // ffs someone fix the wrong map size of 1.5mb
        if(compressed_chunk_data) {
                free(compressed_chunk_data);
                compressed_chunk_data = NULL;
        }
        compressed_chunk_data_size = 1024 * 1024;
        compressed_chunk_data = malloc(compressed_chunk_data_size);
        CHECK_ALLOCATION_ERROR(compressed_chunk_data)
        compressed_chunk_data_offset = 0;       network_logged_in = 0;
        network_map_transfer = 1;
        network_map_transfer_end = 0;
        network_map_cached = 0;

        if(len == sizeof(struct PacketMapStart075)) {
                struct PacketMapStart075* p = (struct PacketMapStart075*)data;
                compressed_chunk_data_estimate = p->map_size;
        } else {
                struct PacketMapStart076* p = (struct PacketMapStart076*)data;
                compressed_chunk_data_estimate = p->map_size;
                log_info("map name: %s", p->map_name);
                log_info("map crc32: 0x%08X", p->crc32);
                log_debug("Map transfer started: name=%s, crc=0x%08X, size=%i", p->map_name, p->crc32, p->map_size);
                char filename[128];
                sprintf(filename, "cache/%02X%02X%02X%02X.vxl", red(p->crc32), green(p->crc32), blue(p->crc32),
                                alpha(p->crc32));
                log_info("%s", filename);
                if(file_exists(filename)) {
                        network_map_cached = 1;
                        log_debug("Map cache hit: loading from %s", filename);
                        void* mapd = file_load(filename);
                        map_vxl_load(mapd, file_size(filename));
                        free(mapd);
                        chunk_rebuild_all();
                }

                struct PacketMapCached c;
                c.cached = network_map_cached;
                network_send(PACKET_MAPCACHED_ID, &c, sizeof(c));
        }

        player_init();
        camera_mode = CAMERAMODE_SELECTION;
}

void read_PacketWorldUpdate(void* data, int len) {
        if(len > 0) {
                int is_075 = (len % sizeof(struct PacketWorldUpdate075) == 0);
                int is_076 = (len % sizeof(struct PacketWorldUpdate076) == 0);

                if(is_075) {
                        for(int k = 0; k < (len / sizeof(struct PacketWorldUpdate075)); k++) { // supports up to 256 players
                                struct PacketWorldUpdate075* p
                                        = (struct PacketWorldUpdate075*)(data + k * sizeof(struct PacketWorldUpdate075));
                                if(players[k].connected && players[k].alive && k != local_player_id) {
                                        if(distance3D(players[k].pos.x, players[k].pos.y, players[k].pos.z, p->x, 63.0F - p->z, p->y)
                                           > 0.1F * 0.1F) {
                                                players[k].pos.x = p->x;
                                                players[k].pos.y = 63.0F - p->z;
                                                players[k].pos.z = p->y;
                                        }
                                        players[k].orientation.x = p->ox;
                                        players[k].orientation.y = -p->oz;
                                        players[k].orientation.z = p->oy;
                                }
                        }
                } else {
                        if(is_076) {
                                for(int k = 0; k < (len / sizeof(struct PacketWorldUpdate076)); k++) {
                                        struct PacketWorldUpdate076* p
                                                = (struct PacketWorldUpdate076*)(data + k * sizeof(struct PacketWorldUpdate076));
                                        if(players[p->player_id].connected && players[p->player_id].alive
                                           && p->player_id != local_player_id) {
                                                if(distance3D(players[p->player_id].pos.x, players[p->player_id].pos.y, players[p->player_id].pos.z, p->x, 63.0F - p->z, p->y)
                                                   > 0.1F * 0.1F) {
                                                        players[p->player_id].pos.x = p->x;
                                                        players[p->player_id].pos.y = 63.0F - p->z;
                                                        players[p->player_id].pos.z = p->y;
                                                }
                                                players[p->player_id].orientation.x = p->ox;
                                                players[p->player_id].orientation.y = -p->oz;
                                                players[p->player_id].orientation.z = p->oy;
                                        }
                                }
                        }
                }
        }
}

void read_PacketPositionData(void* data, int len) {
        if(len < (int)sizeof(struct PacketPositionData)) {
                log_warn("Short PacketPositionData: %d < %zu", len, sizeof(struct PacketPositionData));
                return;
        }
        struct PacketPositionData* p = (struct PacketPositionData*)data;
        players[local_player_id].pos.x = p->x;
        players[local_player_id].pos.y = 63.0F - p->z;
        players[local_player_id].pos.z = p->y;
}

void read_PacketOrientationData(void* data, int len) {
        if(len < (int)sizeof(struct PacketOrientationData)) {
                log_warn("Short PacketOrientationData: %d < %zu", len, sizeof(struct PacketOrientationData));
                return;
        }
        struct PacketOrientationData* p = (struct PacketOrientationData*)data;
        players[local_player_id].pos.x = p->x;
        players[local_player_id].pos.y = -p->z;
        players[local_player_id].pos.z = p->y;
}

void read_PacketSetColor(void* data, int len) {
        if(len < (int)sizeof(struct PacketSetColor)) {
                log_warn("Short PacketSetColor: %d < %zu", len, sizeof(struct PacketSetColor));
                return;
        }
        struct PacketSetColor* p = (struct PacketSetColor*)data;
        if(p->player_id < PLAYERS_MAX) {
                players[p->player_id].block.red = p->red;
                players[p->player_id].block.green = p->green;
                players[p->player_id].block.blue = p->blue;
        }
}

void read_PacketInputData(void* data, int len) {
        if(len < (int)sizeof(struct PacketInputData)) {
                log_warn("Short PacketInputData: %d < %zu", len, sizeof(struct PacketInputData));
                return;
        }
        struct PacketInputData* p = (struct PacketInputData*)data;
        if(p->player_id < PLAYERS_MAX) {
                if(p->player_id != local_player_id)
                        players[p->player_id].input.keys.packed = p->keys;
                players[p->player_id].physics.jump = (p->keys & 16) > 0;
                // players[p->player_id].input.keys.jump = (p->keys&16)>0;
        }
}

void read_PacketWeaponInput(void* data, int len) {
        if(len < (int)sizeof(struct PacketWeaponInput)) {
                log_warn("Short PacketWeaponInput: %d < %zu", len, sizeof(struct PacketWeaponInput));
                return;
        }
        struct PacketWeaponInput* p = (struct PacketWeaponInput*)data;
        if(p->player_id < PLAYERS_MAX && p->player_id != local_player_id) {
                players[p->player_id].input.buttons.lmb = p->primary;
                players[p->player_id].input.buttons.rmb = p->secondary;
                if(p->primary) {
                        players[p->player_id].input.buttons.lmb_start = window_time();
                        players[p->player_id].weapon_last_shot = window_time();
                }
                if(p->secondary)
                        players[p->player_id].input.buttons.rmb_start = window_time();
        }
}

void read_PacketSetTool(void* data, int len) {
        if(len < (int)sizeof(struct PacketSetTool)) {
                log_warn("Short PacketSetTool: %d < %zu", len, sizeof(struct PacketSetTool));
                return;
        }
        struct PacketSetTool* p = (struct PacketSetTool*)data;
        if(p->player_id < PLAYERS_MAX && p->tool < 4) {
                players[p->player_id].held_item = p->tool;
                player_on_held_item_change(&players[p->player_id]);
        }
}

void read_PacketKillAction(void* data, int len) {
        if(len < (int)sizeof(struct PacketKillAction)) {
                log_warn("Short PacketKillAction: %d < %zu", len, sizeof(struct PacketKillAction));
                return;
        }
        struct PacketKillAction* p = (struct PacketKillAction*)data;
        if(p->player_id < PLAYERS_MAX && p->killer_id < PLAYERS_MAX && p->kill_type >= 0 && p->kill_type < 7) {
                if(p->player_id == local_player_id) {
                        local_player_death_time = window_time();
                        local_player_respawn_time = p->respawn_time;
                        local_player_respawn_cnt_last = 255;
                        if(!demo_mute_effects())
                                sound_create(SOUND_LOCAL, &sound_death, 0.0F, 0.0F, 0.0F);

                        if(p->player_id != p->killer_id) {
                                local_player_last_damage_timer = local_player_death_time;
                                local_player_last_damage_x = players[p->killer_id].pos.x;
                                local_player_last_damage_y = players[p->killer_id].pos.y;
                                local_player_last_damage_z = players[p->killer_id].pos.z;
                                cameracontroller_death_init(local_player_id, players[p->killer_id].pos.x, players[p->killer_id].pos.y,
                                                                                        players[p->killer_id].pos.z);
                        } else {
                                cameracontroller_death_init(local_player_id, 0, 0, 0);
                        }
                }
                players[p->player_id].alive = 0;
                players[p->player_id].input.keys.packed = 0;
                players[p->player_id].input.buttons.packed = 0;
                player_save_corpse(p->player_id);
                log_debug("Player %i killed by %i (type %i)", p->player_id, p->killer_id, p->kill_type);
                if(p->player_id != p->killer_id) {
                        players[p->killer_id].score++;
                }

                if(settings.player_stats) {
                        if(p->killer_id == local_player_id) {
                                player_stats_kills++;
                                if(p->kill_type == KILLTYPE_HEADSHOT) {
                                        player_stats_headshots++;
                                }
                        }
                        if(p->player_id == local_player_id) {
                                player_stats_deaths++;
                        }
                }

                char* gun_name[3] = {"Rifle", "SMG", "Shotgun"};
                char m[256];

                char color_killer = team_color_char(players[p->killer_id].team);
                char color_dead   = team_color_char(players[p->player_id].team);
                char color_kill   = '\4';
                switch(p->kill_type) {
                        case KILLTYPE_WEAPON:
                                sprintf(m, "%c%s%c killed %c%s%c (%s)", color_killer, players[p->killer_id].name, color_kill, color_dead, players[p->player_id].name,
                                                color_kill, gun_name[players[p->killer_id].weapon]);
                                break;
                        case KILLTYPE_HEADSHOT:
                                sprintf(m, "%c%s%c killed %c%s%c (%s Headshot)", color_killer, players[p->killer_id].name, color_kill, color_dead, players[p->player_id].name,
                                        color_kill, gun_name[players[p->killer_id].weapon]);
                                break;
                        case KILLTYPE_MELEE:
                                sprintf(m, "%c%s%c killed %c%s%c (Spade)", color_killer, players[p->killer_id].name, color_kill, color_dead, players[p->player_id].name, color_kill);
                                break;
                        case KILLTYPE_GRENADE:
                                sprintf(m, "%c%s%c killed %c%s%c (Grenade)", color_killer, players[p->killer_id].name, color_kill, color_dead, players[p->player_id].name, color_kill);
                                break;
                        case KILLTYPE_FALL: sprintf(m, "%c%s%c fell too far", color_dead, players[p->player_id].name, color_kill); break;
                        case KILLTYPE_TEAMCHANGE: sprintf(m, "%c%s%c changed teams", color_dead, players[p->player_id].name, color_kill); break;
                        case KILLTYPE_CLASSCHANGE: sprintf(m, "%c%s%c changed weapons", color_dead, players[p->player_id].name, color_kill); break;
                }
                if(p->killer_id == local_player_id || p->player_id == local_player_id) {
                        chat_add(1, 0x0000FF, m);
                } else {
                        switch(players[p->killer_id].team) {
                                case TEAM_1:
                                        chat_add(1, rgb(gamestate.team_1.red, gamestate.team_1.green, gamestate.team_1.blue), m);
                                        break;
                                case TEAM_2:
                                        chat_add(1, rgb(gamestate.team_2.red, gamestate.team_2.green, gamestate.team_2.blue), m);
                                        break;
                        }
                }
        }
}

void read_PacketShortPlayerData(void* data, int len) {
        // should never be received
        struct PacketShortPlayerData* p = (struct PacketShortPlayerData*)data;
        log_warn("Unexpected ShortPlayerDataPacket");
}

void read_PacketGrenade(void* data, int len) {
        if(len < (int)sizeof(struct PacketGrenade)) {
                log_warn("Short PacketGrenade: %d < %zu", len, sizeof(struct PacketGrenade));
                return;
        }
        struct PacketGrenade* p = (struct PacketGrenade*)data;

        grenade_add(&(struct Grenade) {
                .team = players[p->player_id].team,
                .owner_id = p->player_id,
                .fuse_length = p->fuse_length,
                .pos.x = p->x,
                .pos.y = 63.0F - p->z,
                .pos.z = p->y,
                .velocity.x = p->vx,
                .velocity.y = -p->vz,
                .velocity.z = p->vy,
        });
}

void read_PacketSetHP(void* data, int len) {
        if(len < (int)sizeof(struct PacketSetHP)) {
                log_warn("Short PacketSetHP: %d < %zu", len, sizeof(struct PacketSetHP));
                return;
        }
        struct PacketSetHP* p = (struct PacketSetHP*)data;
        local_player_health = p->hp;
        if(p->type == DAMAGE_SOURCE_GUN) {
                local_player_last_damage_timer = window_time();
                if(!demo_mute_effects()) sound_create(SOUND_LOCAL, &sound_hitplayer, 0.0F, 0.0F, 0.0F);
        }
        local_player_last_damage_x = p->x;
        local_player_last_damage_y = 63.0F - p->z;
        local_player_last_damage_z = p->y;
}

void read_PacketRestock(void* data, int len) {
        if(len < (int)sizeof(struct PacketRestock)) {
                log_warn("Short PacketRestock: %d < %zu", len, sizeof(struct PacketRestock));
                return;
        }
        struct PacketRestock* p = (struct PacketRestock*)data;
        local_player_health = 100;
        local_player_blocks = 50;
        local_player_grenades = 3;
        weapon_set(true);
        skins_apply_all(0);
        if(!demo_mute_effects())
                sound_create(SOUND_LOCAL, &sound_switch, 0.0F, 0.0F, 0.0F);
}

void read_PacketChangeWeapon(void* data, int len) {
        if(len < (int)sizeof(struct PacketChangeWeapon)) {
                log_warn("Short PacketChangeWeapon: %d < %zu", len, sizeof(struct PacketChangeWeapon));
                return;
        }
        struct PacketChangeWeapon* p = (struct PacketChangeWeapon*)data;
        if(p->player_id < PLAYERS_MAX) {
                if(p->player_id == local_player_id) {
                        log_warn("Unexpected ChangeWeaponPacket");
                        return;
                }
                players[p->player_id].weapon = p->weapon;
        }
}

void read_PacketWeaponReload(void* data, int len) {
        if(len < (int)sizeof(struct PacketWeaponReload)) {
                log_warn("Short PacketWeaponReload: %d < %zu", len, sizeof(struct PacketWeaponReload));
                return;
        }
        struct PacketWeaponReload* p = (struct PacketWeaponReload*)data;
        if(p->player_id == local_player_id) {
                local_player_ammo = p->ammo;
                local_player_ammo_reserved = p->reserved;
        } else {
                if(!demo_mute_effects()) sound_create_sticky(weapon_sound_reload(players[p->player_id].weapon), players + p->player_id, p->player_id);
                // dont use values from packet which somehow are never correct
                players[p->player_id].ammo = weapon_ammo(players[p->player_id].weapon);
                players[p->player_id].ammo_reserved = weapon_ammo_reserved(players[p->player_id].weapon);
        }
}

void read_PacketMoveObject(void* data, int len) {
        if(len < (int)sizeof(struct PacketMoveObject)) {
                log_warn("Short PacketMoveObject: %d < %zu", len, sizeof(struct PacketMoveObject));
                return;
        }
        struct PacketMoveObject* p = (struct PacketMoveObject*)data;
        if(gamestate.gamemode_type == GAMEMODE_CTF) {
                switch(p->object_id) {
                        case TEAM_1_BASE:
                                gamestate.gamemode.ctf.team_1_base.x = p->x;
                                gamestate.gamemode.ctf.team_1_base.y = p->y;
                                gamestate.gamemode.ctf.team_1_base.z = p->z;
                                break;
                        case TEAM_2_BASE:
                                gamestate.gamemode.ctf.team_2_base.x = p->x;
                                gamestate.gamemode.ctf.team_2_base.y = p->y;
                                gamestate.gamemode.ctf.team_2_base.z = p->z;
                                break;
                        case TEAM_1_FLAG:
                                gamestate.gamemode.ctf.team_1_intel = 0;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.x = p->x;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.y = p->y;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.z = p->z;
                                break;
                        case TEAM_2_FLAG:
                                gamestate.gamemode.ctf.team_2_intel = 0;
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.x = p->x;
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.y = p->y;
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.z = p->z;
                                break;
                }
        }
        if(gamestate.gamemode_type == GAMEMODE_TC && p->object_id < gamestate.gamemode.tc.territory_count) {
                gamestate.gamemode.tc.territory[p->object_id].x = p->x;
                gamestate.gamemode.tc.territory[p->object_id].y = p->y;
                gamestate.gamemode.tc.territory[p->object_id].z = p->z;
                gamestate.gamemode.tc.territory[p->object_id].team = p->team;
        }
}

void read_PacketIntelCapture(void* data, int len) {
        if(len < (int)sizeof(struct PacketIntelCapture)) {
                log_warn("Short PacketIntelCapture: %d < %zu", len, sizeof(struct PacketIntelCapture));
                return;
        }
        struct PacketIntelCapture* p = (struct PacketIntelCapture*)data;
        if(gamestate.gamemode_type == GAMEMODE_CTF && p->player_id < PLAYERS_MAX) {
                char capture_str[128];
                switch(players[p->player_id].team) {
                        case TEAM_1:
                                gamestate.gamemode.ctf.team_1_score++;
                                sprintf(capture_str, "\1%s\6 has captured the \2%s\6 Intel", players[p->player_id].name, gamestate.team_2.name);
                                break;
                        case TEAM_2:
                                gamestate.gamemode.ctf.team_2_score++;
                                sprintf(capture_str, "\2%s\6 has captured the \1%s\6 Intel", players[p->player_id].name, gamestate.team_1.name);
                                break;
                }
                if(!demo_mute_effects()) sound_create(SOUND_LOCAL, p->winning ? &sound_horn : &sound_pickup, 0.0F, 0.0F, 0.0F);
                players[p->player_id].score += 10;
                chat_add(0, hud_accent_color(), capture_str);
                if(p->winning) {
                        char* name = NULL;

                        switch(players[p->player_id].team) {
                                case TEAM_1: name = gamestate.team_1.name; break;
                                case TEAM_2: name = gamestate.team_2.name; break;
                        }

                        sprintf(capture_str, "%s Team Wins!", name);
                        chat_showpopup(capture_str, 5.0F, rgb(255, 0, 0));

                        gamestate.gamemode.ctf.team_1_score = 0;
                        gamestate.gamemode.ctf.team_2_score = 0;
                }
        }
}

void read_PacketIntelDrop(void* data, int len) {
        if(len < (int)sizeof(struct PacketIntelDrop)) {
                log_warn("Short PacketIntelDrop: %d < %zu", len, sizeof(struct PacketIntelDrop));
                return;
        }
        struct PacketIntelDrop* p = (struct PacketIntelDrop*)data;
        if(gamestate.gamemode_type == GAMEMODE_CTF && p->player_id < PLAYERS_MAX) {
                char drop_str[128];
                switch(players[p->player_id].team) {
                        case TEAM_1:
                                gamestate.gamemode.ctf.team_2_intel = 0; // drop opposing team's intel
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.x = p->x;
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.y = p->y;
                                gamestate.gamemode.ctf.team_2_intel_location.dropped.z = p->z;
                                sprintf(drop_str, "\1%s\6 has dropped the \2%s\6 Intel", players[p->player_id].name, gamestate.team_2.name);
                                break;
                        case TEAM_2:
                                gamestate.gamemode.ctf.team_1_intel = 0;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.x = p->x;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.y = p->y;
                                gamestate.gamemode.ctf.team_1_intel_location.dropped.z = p->z;
                                sprintf(drop_str, "\2%s\6 has dropped the \1%s\6 Intel", players[p->player_id].name, gamestate.team_1.name);
                                break;
                }
                chat_add(0, hud_accent_color(), drop_str);
        }
}

void read_PacketIntelPickup(void* data, int len) {
        if(len < (int)sizeof(struct PacketIntelPickup)) {
                log_warn("Short PacketIntelPickup: %d < %zu", len, sizeof(struct PacketIntelPickup));
                return;
        }
        struct PacketIntelPickup* p = (struct PacketIntelPickup*)data;
        if(gamestate.gamemode_type == GAMEMODE_CTF && p->player_id < PLAYERS_MAX) {
                char pickup_str[128];
                switch(players[p->player_id].team) {
                        case TEAM_1:
                                gamestate.gamemode.ctf.team_2_intel = 1; // pickup opposing team's intel
                                gamestate.gamemode.ctf.team_2_intel_location.held.player_id = p->player_id;
                                sprintf(pickup_str, "\1%s\6 has the \2%s\6 Intel", players[p->player_id].name, gamestate.team_2.name);
                                break;
                        case TEAM_2:
                                gamestate.gamemode.ctf.team_1_intel = 1;
                                gamestate.gamemode.ctf.team_1_intel_location.held.player_id = p->player_id;
                                sprintf(pickup_str, "\2%s\6 has the \1%s\6 Intel", players[p->player_id].name, gamestate.team_1.name);
                                break;
                }
                chat_add(0, hud_accent_color(), pickup_str);
                if(!demo_mute_effects()) sound_create(SOUND_LOCAL, &sound_pickup, 0.0F, 0.0F, 0.0F);
        }
}

void read_PacketTerritoryCapture(void* data, int len) {
        if(len < (int)sizeof(struct PacketTerritoryCapture)) {
                log_warn("Short PacketTerritoryCapture: %d < %zu", len, sizeof(struct PacketTerritoryCapture));
                return;
        }
        struct PacketTerritoryCapture* p = (struct PacketTerritoryCapture*)data;
        if(gamestate.gamemode_type == GAMEMODE_TC && p->tent < gamestate.gamemode.tc.territory_count) {
                gamestate.gamemode.tc.territory[p->tent].team = p->team;
                if(!demo_mute_effects()) sound_create(SOUND_LOCAL, p->winning ? &sound_horn : &sound_pickup, 0.0F, 0.0F, 0.0F);
                char x = (int)(gamestate.gamemode.tc.territory[p->tent].x / 64.0F) + 'A';
                char y = (int)(gamestate.gamemode.tc.territory[p->tent].y / 64.0F) + '1';
                char capture_str[128];
                char* team_n;
                switch(p->team) {
                        case TEAM_1: team_n = gamestate.team_1.name; break;
                        case TEAM_2: team_n = gamestate.team_2.name; break;
                }
                if(team_n) {
                        char team_color = team_color_char(p->team);

                        sprintf(capture_str, "%c%s\6 have captured %c%c", team_color, team_n, x, y);
                        chat_add(0, hud_accent_color(), capture_str);
                        if(p->winning) {
                                sprintf(capture_str, "%s Team Wins!", team_n);
                                chat_showpopup(capture_str, 5.0F, rgb(255, 0, 0));
                        }
                }
        }
}

void read_PacketProgressBar(void* data, int len) {
        if(len < (int)sizeof(struct PacketProgressBar)) {
                log_warn("Short PacketProgressBar: %d < %zu", len, sizeof(struct PacketProgressBar));
                return;
        }
        struct PacketProgressBar* p = (struct PacketProgressBar*)data;
        if(gamestate.gamemode_type == GAMEMODE_TC && p->tent < gamestate.gamemode.tc.territory_count) {
                gamestate.progressbar.progress = max(min(p->progress, 1.0F), 0.0F);
                gamestate.progressbar.rate = p->rate;
                gamestate.progressbar.tent = p->tent;
                gamestate.progressbar.team_capturing = p->team_capturing;
                gamestate.progressbar.update = window_time();
        }
}

void read_PacketHandshakeInit(void* data, int len) {
        network_send(PACKET_HANDSHAKERETURN_ID, data, len);
}

void read_PacketVersionGet(void* data, int len) {
        struct PacketVersionSend ver;
        ver.client = 'K';
        ver.major = KYROSPADES_MAJOR;
        ver.minor = KYROSPADES_MINOR;
        ver.revision = KYROSPADES_PATCH;
#ifndef OPENGL_ES
#ifdef OS_WINDOWS
        char* os = "KyroSpades (Windows) " BS_VER_INFO;
#endif
#ifdef OS_LINUX
        char* os = "KyroSpades (Linux) " BS_VER_INFO;
#endif
#ifdef OS_APPLE
        char* os = "KyroSpades (Apple) " BS_VER_INFO;
#endif
#ifdef OS_HAIKU
        char* os = "KyroSpades (Haiku) " BS_VER_INFO;
#endif
#else
#if defined(OS_IOS)
        char* os = "KyroSpades (iOS) " BS_VER_INFO;
#elif defined(USE_TOUCH)
        char* os = "KyroSpades (Android) " BS_VER_INFO;
#else
        char* os = "KyroSpades (Embedded) " BS_VER_INFO;
#endif
#endif
        strcpy(ver.operatingsystem, os);
        network_send(PACKET_VERSIONSEND_ID, &ver, sizeof(ver) - sizeof(ver.operatingsystem) + strlen(os));
}

void read_PacketExtInfo(void* data, int len) {
        struct PacketExtInfo* p = (struct PacketExtInfo*)data;
        if(len >= p->length * sizeof(struct PacketExtInfoEntry) + 1) {
                if(p->length > 0) {
                        log_info("Server supports the following extensions:");
                        for(int k = 0; k < p->length; k++) {
                                log_info("Extension 0x%02X of version %i", p->entries[k].id, p->entries[k].version);
                                if(p->entries[k].id >= 192)
                                        log_info("(which is packetless)");
                        }
                } else {
                        log_info("Server does not support extensions");
                }

                struct PacketExtInfo reply;
                reply.length = 4;
                reply.entries[0] = (struct PacketExtInfoEntry) {
                        .id = EXT_PLAYER_PROPERTIES,
                        .version = 1,
                };
                reply.entries[1] = (struct PacketExtInfoEntry) {
                        .id = EXT_256PLAYERS,
                        .version = 1,
                };
                reply.entries[2] = (struct PacketExtInfoEntry) {
                        .id = EXT_MESSAGES,
                        .version = 1,
                };
                reply.entries[3] = (struct PacketExtInfoEntry) {
                        .id = EXT_KICKREASON,
                        .version = 1,
                };
                network_send(PACKET_EXTINFO_ID, &reply, reply.length * sizeof(struct PacketExtInfoEntry) + 1);
        }
}

void read_PacketPlayerProperties(void* data, int len) {
        struct PacketPlayerProperties* p = (struct PacketPlayerProperties*)data;

        if(len >= sizeof(struct PacketPlayerProperties) && p->subID == 0) {
                players[p->player_id].ammo = p->ammo_clip;
                players[p->player_id].ammo_reserved = p->ammo_reserved;
                players[p->player_id].score = p->score;

                if(p->player_id == local_player_id) {
                        local_player_health = p->health;
                        local_player_blocks = p->blocks;
                        local_player_grenades = p->grenades;
                        local_player_ammo = p->ammo_clip;
                        local_player_ammo_reserved = p->ammo_reserved;
                }
        }
}

void network_updateColor() {
        struct PacketSetColor c;
        c.player_id = local_player_id;
        c.red = players[local_player_id].block.red;
        c.green = players[local_player_id].block.green;
        c.blue = players[local_player_id].block.blue;
        network_send(PACKET_SETCOLOR_ID, &c, sizeof(c));
}

void network_send(int id, void* data, int len) {
        if(demo_is_playing()) return;
        if(network_connected) {
                network_stats[0].outgoing += len + 1;
                unsigned char tmp[512];
                tmp[0] = id;
                memcpy(tmp + 1, data, len);
                enet_peer_send(peer, 0, enet_packet_create(tmp, len + 1, ENET_PACKET_FLAG_RELIABLE));
        }
}

unsigned int network_ping() {
        return network_connected ? peer->roundTripTime : 0;
}

void network_disconnect() {
        if(demo_is_playing()) {
                demo_playback_close();
                return;
        }
        player_stats_reset();
        if(network_connected) {
                enet_peer_disconnect(peer, 0);
                network_connected = 0;
                network_logged_in = 0;
                network_map_transfer_end = 0;
                player_clear_corpses();
                bloodmarks_clear();
                damagenumbers_clear();
                /* Belt-and-braces: the live chat ring belongs to the just-closed
                   session. server_c also calls chat_clear on the next connect,
                   but clearing here too prevents any UI that runs in the
                   disconnected interval (or a reconnect that bypasses server_c)
                   from showing the previous server's messages. */
                chat_clear(0);
                chat_clear(1);

                ENetEvent event;
                while(enet_host_service(client, &event, 3000) > 0) {
                        switch(event.type) {
                                case ENET_EVENT_TYPE_RECEIVE: enet_packet_destroy(event.packet); break;
                                case ENET_EVENT_TYPE_DISCONNECT:
                                        enet_host_destroy(client);
                                return;
                        }
                }

                enet_peer_reset(peer);
                enet_host_destroy(client);
        }
}

int network_connect_sub(char* ip, int port, int version) {
        ENetAddress address;
        ENetEvent event;
        network_init_host();
        enet_address_set_host(&address, ip);
        address.port = port;
        peer = enet_host_connect(client, &address, 1, version);
        network_logged_in = 0;
        *network_custom_reason = 0;
        memset(network_stats, 0, sizeof(struct network_stat) * 40);
        if(peer == NULL)
                return 0;
        if(enet_host_service(client, &event, 2500) > 0 && event.type == ENET_EVENT_TYPE_CONNECT) {
                network_received_packets = 0;
                network_connected = 1;
                strncpy(network_current_ip, ip, sizeof(network_current_ip) - 1);
                network_current_ip[sizeof(network_current_ip) - 1] = 0;
                network_current_port = port;
                log_debug("Connected to %s:%i (version %i)", ip, port, version);

                float start = window_time();
                while(window_time() - start < 1.0F) { // listen connection for 1s, check if server disconnects
                        if(!network_update()) {
                                enet_peer_reset(peer);
                                return 0;
                        }
                }
                return 1;
        }
        chat_showpopup("No response", 3.0F, rgb(255, 0, 0));
        log_debug("Connection to %s:%i failed: no response from server", ip, port);
        enet_peer_reset(peer);
        return 0;
}

int network_connect(char* ip, int port) {
        log_info("Connecting to %s at port %i", ip, port);
        if(network_connected) {
                network_disconnect();
        }
        if(settings.auto_demo_recording) {
                demo_start_record();
        }
        if(network_connect_sub(ip, port, VERSION_075)) {
                return 1;
        }
        if(network_connect_sub(ip, port, VERSION_076)) {
                return 1;
        }
        network_connected = 0;
        if(settings.auto_demo_recording) {
                demo_stop_record();
        }
        return 0;
}

int network_identifier_split(char* addr, char* ip_out, int* port_out) {
        char* ip_start = strstr(addr, "aos://") + 6;
        if((size_t)ip_start <= 6)
                return 0;
        char* port_start = strchr(ip_start, ':');
        *port_out = port_start ? strtoul(port_start + 1, NULL, 10) : 32887;

        /* Determine the raw IP substring length (without port suffix) so we
           can bound the copy into ip_out. Callers pass a 32-byte buffer; a
           malicious server-list entry with a long dotted string used to
           overflow it via strcpy. */
        size_t ip_len = port_start ? (size_t)(port_start - ip_start) : strlen(ip_start);
        if(ip_len == 0 || ip_len >= 32)
                return 0;

        if(strchr(ip_start, '.')) {
                memcpy(ip_out, ip_start, ip_len);
                ip_out[ip_len] = 0;
        } else {
                unsigned int ip = strtoul(ip_start, NULL, 10);
                sprintf(ip_out, "%i.%i.%i.%i", ip & 255, (ip >> 8) & 255, (ip >> 16) & 255, (ip >> 24) & 255);
        }

        return 1;
}

int network_connect_string(char* addr) {
        char ip[32];
        int port;
        if(!network_identifier_split(addr, ip, &port))
                return 0;
        return network_connect(ip, port);
}

int network_update() {
        if(demo_is_playing()) {
                demo_playback_update();
                chunk_queue_blocks();
                return 1;
        }
        if(network_connected) {
                if(window_time() - network_stats_last >= 1.0F) {
                        for(int k = 39; k > 0; k--)
                                network_stats[k] = network_stats[k - 1];
                        network_stats[0].ingoing = 0;
                        network_stats[0].outgoing = 0;
                        network_stats[0].avg_ping = network_ping();
                        network_stats_last = window_time();
                }

                ENetEvent event;
                while(enet_host_service(client, &event, 0) > 0) {
                        switch(event.type) {
                                case ENET_EVENT_TYPE_RECEIVE: {
                                        network_stats[0].ingoing += event.packet->dataLength;
                                        /* Defensive: enet should never deliver a 0-length packet,
                                           but if it does, dataLength-1 underflows to SIZE_MAX
                                           and the handler thinks it has a huge buffer. */
                                        if(event.packet->dataLength < 1) {
                                                enet_packet_destroy(event.packet);
                                                break;
                                        }
                                        int id = event.packet->data[0];
                                        if(id < 0 || id >= 256) {
                                                log_error("Packet id out of range: %i", id);
                                                enet_packet_destroy(event.packet);
                                                break;
                                        }
                                        if(*packets[id]) {
                                                log_debug("Packet id %i", id);
                                                (*packets[id])(event.packet->data + 1, event.packet->dataLength - 1);
                                        } else {
                                                log_error("Invalid packet id %i, length: %i", id, (int)event.packet->dataLength - 1);
                                        }
                                        register_demo_packet(event.packet);
                                        network_received_packets++;
                                        enet_packet_destroy(event.packet);
                                        break;
                                }
                                case ENET_EVENT_TYPE_DISCONNECT:
                                        // In case we're disconnected for using the wrong protocol, don't return to HUD just yet
                                        if(event.data != 3) {
                                                hud_change(&hud_serverlist);
                                                chat_showpopup(network_reason_disconnect(event.data), 10.0F, rgb(255, 0, 0));
                                        }

                                        log_error("server disconnected! reason: %s", network_reason_disconnect(event.data));
                                        event.peer->data = NULL;
                                        network_connected = 0;
                                        network_logged_in = 0;
                                        network_map_transfer_end = 0;
                                        player_clear_corpses();
                                        bloodmarks_clear();
                                        damagenumbers_clear();
                                        return 0;
                        }
                }

                if(network_logged_in && players[local_player_id].team != TEAM_SPECTATOR && players[local_player_id].alive) {
                        if(players[local_player_id].input.keys.packed != network_keys_last) {
                                struct PacketInputData in;
                                in.player_id = local_player_id;
                                in.keys = players[local_player_id].input.keys.packed;
                                network_send(PACKET_INPUTDATA_ID, &in, sizeof(in));

                                network_keys_last = players[local_player_id].input.keys.packed;
                        }
                        if(players[local_player_id].input.buttons.packed != network_buttons_last
                           && players[local_player_id].input.keys.sprint == 0) {
                                struct PacketWeaponInput in;
                                in.player_id = local_player_id;
                                in.primary = players[local_player_id].input.buttons.lmb;
                                in.secondary = players[local_player_id].input.buttons.rmb;
                                network_send(PACKET_WEAPONINPUT_ID, &in, sizeof(in));

                                network_buttons_last = players[local_player_id].input.buttons.packed;
                        }
                        if(players[local_player_id].held_item != network_tool_last) {
                                struct PacketSetTool t;
                                t.player_id = local_player_id;
                                t.tool = players[local_player_id].held_item;
                                network_send(PACKET_SETTOOL_ID, &t, sizeof(t));

                                network_tool_last = players[local_player_id].held_item;
                        }

                        if(window_time() - network_pos_update > 1.0F
                           && distance3D(network_pos_last.x, network_pos_last.y, network_pos_last.z, players[local_player_id].pos.x,
                                                         players[local_player_id].pos.y, players[local_player_id].pos.z)
                                   > 0.01F) {
                                network_pos_update = window_time();
                                memcpy(&network_pos_last, &players[local_player_id].pos, sizeof(struct Position));
                                struct PacketPositionData pos;
                                pos.x = players[local_player_id].pos.x;
                                pos.y = players[local_player_id].pos.z;
                                pos.z = 63.0F - players[local_player_id].pos.y;
                                network_send(PACKET_POSITIONDATA_ID, &pos, sizeof(pos));
                        }
                        if(window_time() - network_orient_update > (1.0F / 120.0F)
                           && angle3D(network_orient_last.x, network_orient_last.y, network_orient_last.z,
                                                  players[local_player_id].orientation.x, players[local_player_id].orientation.y,
                                                  players[local_player_id].orientation.z)
                                   > 0.5F / 180.0F * PI) {
                                network_orient_update = window_time();
                                memcpy(&network_orient_last, &players[local_player_id].orientation, sizeof(struct Orientation));
                                struct PacketOrientationData orient;
                                orient.x = players[local_player_id].orientation.x;
                                orient.y = players[local_player_id].orientation.z;
                                orient.z = -players[local_player_id].orientation.y;
                                network_send(PACKET_ORIENTATIONDATA_ID, &orient, sizeof(orient));
                        }
                }
        }

        chunk_queue_blocks();

        return 1;
}

int network_status() {
        return network_connected;
}

void network_init() {
        enet_initialize();
        client = enet_host_create(NULL, 1, 1, 0, 0); // limit bandwidth here if you want to
        enet_host_compress_with_range_coder(client);

        packets[PACKET_POSITIONDATA_ID] = read_PacketPositionData;
        packets[PACKET_ORIENTATIONDATA_ID] = read_PacketOrientationData;
        packets[PACKET_WORLDUPDATE_ID] = read_PacketWorldUpdate;
        packets[PACKET_INPUTDATA_ID] = read_PacketInputData;
        packets[PACKET_WEAPONINPUT_ID] = read_PacketWeaponInput;
        packets[PACKET_SETHP_ID] = read_PacketSetHP;
        packets[PACKET_GRENADE_ID] = read_PacketGrenade;
        packets[PACKET_SETTOOL_ID] = read_PacketSetTool;
        packets[PACKET_SETCOLOR_ID] = read_PacketSetColor;
        packets[PACKET_EXISTINGPLAYER_ID] = read_PacketExistingPlayer;
        packets[PACKET_SHORTPLAYERDATA_ID] = read_PacketShortPlayerData;
        packets[PACKET_MOVEOBJECT_ID] = read_PacketMoveObject;
        packets[PACKET_CREATEPLAYER_ID] = read_PacketCreatePlayer;
        packets[PACKET_BLOCKACTION_ID] = read_PacketBlockAction;
        packets[PACKET_BLOCKLINE_ID] = read_PacketBlockLine;
        packets[PACKET_STATEDATA_ID] = read_PacketStateData;
        packets[PACKET_KILLACTION_ID] = read_PacketKillAction;
        packets[PACKET_CHATMESSAGE_ID] = read_PacketChatMessage;
        packets[PACKET_MAPSTART_ID] = read_PacketMapStart;
        packets[PACKET_MAPCHUNK_ID] = read_PacketMapChunk;
        packets[PACKET_PLAYERLEFT_ID] = read_PacketPlayerLeft;
        packets[PACKET_TERRITORYCAPTURE_ID] = read_PacketTerritoryCapture;
        packets[PACKET_PROGRESSBAR_ID] = read_PacketProgressBar;
        packets[PACKET_INTELCAPTURE_ID] = read_PacketIntelCapture;
        packets[PACKET_INTELPICKUP_ID] = read_PacketIntelPickup;
        packets[PACKET_INTELDROP_ID] = read_PacketIntelDrop;
        packets[PACKET_RESTOCK_ID] = read_PacketRestock;
        packets[PACKET_FOGCOLOR_ID] = read_PacketFogColor;
        packets[PACKET_WEAPONRELOAD_ID] = read_PacketWeaponReload;
        // 29
        packets[PACKET_CHANGEWEAPON_ID] = read_PacketChangeWeapon;
        packets[PACKET_HANDSHAKEINIT_ID] = read_PacketHandshakeInit;
        packets[PACKET_VERSIONGET_ID] = read_PacketVersionGet;
        packets[PACKET_EXTINFO_ID] = read_PacketExtInfo;
        packets[PACKET_EXT_BASE + EXT_PLAYER_PROPERTIES] = read_PacketPlayerProperties;
}


