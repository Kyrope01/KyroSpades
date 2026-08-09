
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

#ifndef DISCORD_IPC_H
#define DISCORD_IPC_H

#ifdef USE_RPC

/* Minimal client for Discord's local IPC transport ("discord-ipc-N"), the
   same channel the retired discord-rpc SDK spoke: 8-byte message header
   (little-endian uint32 opcode + uint32 payload length) followed by a JSON
   payload. Desktop only — Windows (named pipe) and Linux/macOS (unix domain
   socket). Mobile builds don't define USE_RPC and compile this file out:
   the Discord mobile app exposes no such socket.

   All operations are non-blocking and cheap; discord_ipc_process() is meant
   to be called once per frame from the main loop while nothing was
   connected — every few seconds it retries, so Discord can be started after
   the game and is picked up automatically. */

/* Starts the client with the given Discord application (client) ID. From this
   point on it keeps (re)connecting to a running Discord desktop app in the
   background. An empty/NULL client_id disables the client entirely. Safe to
   call again while already running (no-op). */
void discord_ipc_start(const char* client_id);

/* Flushes queued outgoing frames, reads and handles incoming ones
   (READY dispatch, ping/pong keepalive, close frames) and reconnects when the
   connection dropped. Call once per frame; returns immediately. */
void discord_ipc_process(void);

/* Non-zero once the handshake completed and SET_ACTIVITY frames are
   accepted. */
int discord_ipc_ready(void);

/* Sends a Rich Presence activity. activity_json must be a serialized JSON
   object (as produced by json_serialize_to_string), or NULL to clear the
   current activity. The payload is validated/re-encoded locally and wrapped
   in the SET_ACTIVITY command envelope alongside this process' pid.
   Returns 0 when the frame was queued for sending, -1 on error. */
int discord_ipc_set_activity(const char* activity_json);

/* Closes the connection. Discord clears the shown activity by itself once
   the pipe is gone, so no explicit clear is needed on shutdown. */
void discord_ipc_stop(void);

#endif /* USE_RPC */

#endif
