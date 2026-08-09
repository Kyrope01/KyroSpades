
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

#include <string.h>
#include <time.h>

#include "common.h"
#include "log.h"
#include "player.h"
#include "config.h"
#include "parson.h"
#include "rpc.h"

#ifdef USE_RPC
#include "discord_ipc.h"
#endif

#ifndef DISCORD_APP_ID
        #define DISCORD_APP_ID ""
#endif

/* Discord rate-limits Rich Presence updates to roughly one per 15 seconds.
   State changes in between are coalesced and only the newest is shipped.
   (Overridable from the compiler command line for tests.) */
#ifndef RPC_UPDATE_INTERVAL
        #define RPC_UPDATE_INTERVAL 15
#endif

/* Large enough for the fixed activity skeleton plus a fully escaped server
   name (~64 bytes * 6 for \uXXXX worst case). */
#define RPC_ACTIVITY_CAP 1024

static struct rpc {
        int needs_update;
        int players;
        int slots;
        char server_url[64];
        char server_name[64];
        time_t start_time;      /* set when entering a server, for "elapsed" */
        time_t last_sent;       /* throttle bookkeeping */
        int running;            /* ipc client currently active */
        int was_ready;          /* ready state of the previous tick (edge detect) */
        char pending[RPC_ACTIVITY_CAP];  /* newest activity, awaiting send */
} rpc_state;

/* Attaches a fresh JSON object as `parent.name` and returns it (as object),
   for the empty-attach-then-fill building pattern: ownership transfers
   immediately, so a failure halfway through can never orphan a subtree.
*on_fail is invoked by the caller to free the unattached value. */
#ifdef USE_RPC
static JSON_Object* rpc_attach_object(JSON_Object* parent, const char* name, int* ok) {
        JSON_Value* v = json_value_init_object();
        if(!v) {
                *ok = 0;
                return NULL;
        }
        if(json_object_set_value(parent, name, v) != JSONSuccess) {
                json_value_free(v);
                *ok = 0;
                return NULL;
        }
        return json_object_get_object(parent, name);
}

static JSON_Array* rpc_attach_array(JSON_Object* parent, const char* name, int* ok) {
        JSON_Value* v = json_value_init_array();
        if(!v) {
                *ok = 0;
                return NULL;
        }
        if(json_object_set_value(parent, name, v) != JSONSuccess) {
                json_value_free(v);
                *ok = 0;
                return NULL;
        }
        return json_object_get_array(parent, name);
}

/* Serializes the current state into a Discord activity JSON object. Only
   compiled for RPC builds; kept out of discord_ipc.c so the transport stays
   UI/game-agnostic. */
static void rpc_build_activity(char* out, size_t out_size) {
        if(!out || out_size == 0)
                return;
        out[0] = '\0';

        JSON_Value* activity_val = json_value_init_object();
        if(!activity_val)
                return;
        JSON_Object* activity = json_object(activity_val);
        int ok = 1;

        JSON_Object* assets = rpc_attach_object(activity, "assets", &ok);
        if(ok)
                ok = json_object_set_string(assets, "large_image", "pic03") == JSONSuccess &&
                json_object_set_string(assets, "large_text", "KyroSpades") == JSONSuccess &&
                json_object_set_string(assets, "small_image", "logo") == JSONSuccess &&
                json_object_set_string(assets, "small_text", KYROSPADES_VERSION) == JSONSuccess;

        if(ok)
                ok = json_object_set_string(activity, "state",
                                            rpc_state.slots > 0 ? "Playing" : "Waiting") == JSONSuccess;

        if(rpc_state.slots > 0) {
                /* Ingame: server name as headline, party counter (X of Y) and
                   an elapsed-time timer since joining. */
                if(ok)
                        ok = json_object_set_string(activity, "details", rpc_state.server_name) ==
                                JSONSuccess;

                JSON_Object* party = ok ? rpc_attach_object(activity, "party", &ok) : NULL;
                JSON_Array* size = (ok && party) ? rpc_attach_array(party, "size", &ok) : NULL;
                if(ok && size)
                        ok = json_array_append_number(size, (double)max(rpc_state.players, 1)) ==
                                JSONSuccess &&
                        json_array_append_number(size, (double)rpc_state.slots) == JSONSuccess;

                if(ok && rpc_state.start_time > 0) {
                        JSON_Object* ts = rpc_attach_object(activity, "timestamps", &ok);
                        if(ok && ts)
                                ok = json_object_set_number(ts, "start",
                                                            (double)rpc_state.start_time) == JSONSuccess;
                }
        }

        if(ok)
                ok = json_object_set_boolean(activity, "instance", 1) == JSONSuccess;

        if(ok) {
                char* serialized = json_serialize_to_string(activity_val);
                if(serialized) {
                        size_t len = strlen(serialized);
                        if(len < out_size)
                                memcpy(out, serialized, len + 1);
                        json_free_serialized_string(serialized);
                }
        }

        json_value_free(activity_val);

        /* On any allocation failure the output stays empty; the caller simply
           retries at the next update interval with the previous payload (if
           any) still pending. */
}
#endif

void rpc_init() {
        rpc_state.needs_update = 1;
        rpc_state.players = 0;
        rpc_state.slots = 0;
        rpc_state.start_time = 0;
        rpc_state.last_sent = 0;
        *rpc_state.server_url = 0;
        *rpc_state.server_name = 0;
        *rpc_state.pending = 0;
#ifdef USE_RPC
        rpc_state.running = 0;
        rpc_state.was_ready = 0;
#endif
}

void rpc_deinit() {
#ifdef USE_RPC
        discord_ipc_stop();
#endif
}

void rpc_setv(enum RPC_VALUE v, char* x) {
        switch(v) {
                case RPC_VALUE_SERVERNAME:
                        if(strcmp(rpc_state.server_name, x) != 0) {
                                strncpy(rpc_state.server_name, x, sizeof(rpc_state.server_name) - 1);
                                rpc_state.needs_update = 1;
                        }
                        break;
                case RPC_VALUE_SERVERURL:
                        if(strcmp(rpc_state.server_url, x) != 0) {
                                strncpy(rpc_state.server_url, x, sizeof(rpc_state.server_url) - 1);
                                rpc_state.needs_update = 1;
                        }
                        break;
        }
}

void rpc_seti(enum RPC_VALUE v, int x) {
        switch(v) {
                case RPC_VALUE_PLAYERS:
                        if(rpc_state.players != x) {
                                rpc_state.players = x;
                                rpc_state.needs_update = 1;
                        }
                        break;
                case RPC_VALUE_SLOTS:
                        if(rpc_state.slots != x) {
                                if(rpc_state.slots == 0 && x > 0)
                                        rpc_state.start_time = time(NULL);
                                rpc_state.slots = x;
                                rpc_state.needs_update = 1;
                        }
                        break;
        }
}

void rpc_update() {
#ifdef USE_RPC
        /* Honor the settings toggle live: disabling mid-run clears the
           activity by shutting the pipe down, enabling restarts it. */
        if(!settings.discord_rpc) {
                if(rpc_state.running) {
                        discord_ipc_stop();
                        rpc_state.running = 0;
                }
                rpc_state.was_ready = 0;
                return;
        }

        if(!DISCORD_APP_ID[0])
                return; /* built without an application id — stay silent */

        if(!rpc_state.running) {
                discord_ipc_start(DISCORD_APP_ID);
                rpc_state.running = 1;
                rpc_state.needs_update = 1;
        }

        discord_ipc_process();

        int online = 0;
        for(int k = 0; k < PLAYERS_MAX; k++) {
                if(players[k].connected)
                        online++;
        }
        rpc_seti(RPC_VALUE_PLAYERS, online);

        int ready = discord_ipc_ready();
        if(ready && !rpc_state.was_ready) {
                /* Freshly (re)connected: closing the pipe wipes whatever we
                   ever sent on Discord's side, so re-announce the current
                   state right away instead of waiting for a state change. */
                rpc_state.last_sent = 0;
                rpc_state.needs_update = 1;
        }
        rpc_state.was_ready = ready;

        if(ready && rpc_state.needs_update) {
                time_t now = time(NULL);
                if(now - rpc_state.last_sent >= RPC_UPDATE_INTERVAL) {
                        rpc_build_activity(rpc_state.pending, sizeof(rpc_state.pending));
                        if(*rpc_state.pending &&
                           discord_ipc_set_activity(rpc_state.pending) == 0) {
                                rpc_state.last_sent = now;
                                rpc_state.needs_update = 0;
                        }
                }
        }
#endif
}
