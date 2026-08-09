
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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef USE_RPC

#ifdef _WIN32
        #define WIN32_LEAN_AND_MEAN
        #include <windows.h>
#else
        #include <stddef.h>
        #include <unistd.h>
        #include <errno.h>
        #include <fcntl.h>
        #include <sys/socket.h>
        #include <sys/types.h>
        #include <sys/stat.h>
        #include <sys/un.h>
#endif

#include "log.h"
#include "parson.h"
#include "discord_ipc.h"

#define IPC_OP_HANDSHAKE 0
#define IPC_OP_FRAME     1
#define IPC_OP_CLOSE     2
#define IPC_OP_PING      3
#define IPC_OP_PONG      4

/* Sanity cap for a single frame coming from Discord. The READY dispatch is
   a few hundred bytes; anything above this is treated as stream corruption. */
#define IPC_MAX_PAYLOAD (256 * 1024)

/* Discord tries socket indices 0..9. */
#define IPC_PIPE_COUNT 10

/* Staging buffer per read operation; frames get pieced together by the
   incremental parser below, independent of how the OS chunks the reads. */
#define IPC_READ_CHUNK 4096

/* Seconds between connection attempts while Discord is unreachable. */
#define IPC_RECONNECT_DELAY 4

/* Bound on queued unsent frames (presence updates are rare; 8 is plenty).
   SET_ACTIVITY enforces freshness by construction in rpc.c, so dropping the
   oldest frame under pressure is harmless. */
#define IPC_QUEUE_MAX 8

enum {
        IPC_DISCONNECTED,
        IPC_HANDSHAKE_SENT, /* connected, waiting for the READY dispatch */
        IPC_READY,
};

/* One queued outgoing frame: 8-byte header followed by the JSON payload.
   `sent` tracks progress for non-blocking partial writes. */
struct ipc_frame {
        struct ipc_frame* next;
        uint32_t op;
        uint32_t length;   /* payload length, excluding header */
        uint32_t sent;     /* bytes of header+payload written so far */
        unsigned char data[]; /* header (8) + payload (length) */
};

static int ipc_state = IPC_DISCONNECTED;
static char ipc_client_id[32];
static time_t ipc_next_attempt = 0;
static uint32_t ipc_nonce = 0;

static struct ipc_frame* ipc_tx_head = NULL;
static struct ipc_frame* ipc_tx_tail = NULL;
static int ipc_tx_count = 0;

/* Incremental incoming-frame parser state. */
static unsigned char ipc_rx_header[8];
static uint32_t ipc_rx_have_header = 0;
static unsigned char* ipc_rx_payload = NULL;
static uint32_t ipc_rx_want_payload = 0;
static uint32_t ipc_rx_have_payload = 0;

#ifdef _WIN32
static HANDLE ipc_handle = NULL;
static OVERLAPPED ipc_read_ovl;
static OVERLAPPED ipc_write_ovl;
static int ipc_read_pending = 0;
static int ipc_write_pending = 0;
static unsigned char ipc_read_buf[IPC_READ_CHUNK];
#else
static int ipc_socket = -1;
#endif

/* ── helpers ───────────────────────────────────────────────────────────────*/

static void ipc_store_u32le(unsigned char* dst, uint32_t v) {
        dst[0] = (unsigned char)(v & 0xFF);
        dst[1] = (unsigned char)((v >> 8) & 0xFF);
        dst[2] = (unsigned char)((v >> 16) & 0xFF);
        dst[3] = (unsigned char)((v >> 24) & 0xFF);
}

static uint32_t ipc_load_u32le(const unsigned char* src) {
        return (uint32_t)src[0] | ((uint32_t)src[1] << 8) | ((uint32_t)src[2] << 16) |
                ((uint32_t)src[3] << 24);
}

static long ipc_get_pid(void) {
#ifdef _WIN32
        return (long)GetCurrentProcessId();
#else
        return (long)getpid();
#endif
}

/* ── frame queue ───────────────────────────────────────────────────────────*/

static void ipc_queue_clear(void) {
        struct ipc_frame* f = ipc_tx_head;
        while(f) {
                struct ipc_frame* next = f->next;
                free(f);
                f = next;
        }
        ipc_tx_head = NULL;
        ipc_tx_tail = NULL;
        ipc_tx_count = 0;
}

static int ipc_queue_send(uint32_t op, const char* payload, uint32_t length) {
        /* A new handshake invalidates everything queued before it (queued
           frames belong to the previous connection attempt). */
        if(op == IPC_OP_HANDSHAKE)
                ipc_queue_clear();

        while(ipc_tx_count >= IPC_QUEUE_MAX) {
                struct ipc_frame* old = ipc_tx_head;
                ipc_tx_head = old->next;
                if(!ipc_tx_head)
                        ipc_tx_tail = NULL;
                ipc_tx_count--;
                free(old);
        }

        struct ipc_frame* f = malloc(sizeof(*f) + 8 + length);
        if(!f)
                return -1;
        f->next = NULL;
        f->op = op;
        f->length = length;
        f->sent = 0;
        ipc_store_u32le(f->data, op);
        ipc_store_u32le(f->data + 4, length);
        if(length > 0)
                memcpy(f->data + 8, payload, length);

        if(ipc_tx_tail)
                ipc_tx_tail->next = f;
        else
                ipc_tx_head = f;
        ipc_tx_tail = f;
        ipc_tx_count++;
        return 0;
}

static void ipc_queue_pop(void) {
        struct ipc_frame* done = ipc_tx_head;
        ipc_tx_head = done->next;
        if(!ipc_tx_head)
                ipc_tx_tail = NULL;
        ipc_tx_count--;
        free(done);
}

/* ── incoming frames ───────────────────────────────────────────────────────*/

static void ipc_rx_reset(void) {
        ipc_rx_have_header = 0;
        ipc_rx_want_payload = 0;
        ipc_rx_have_payload = 0;
        free(ipc_rx_payload);
        ipc_rx_payload = NULL;
}

static void ipc_disconnect(void);

static void ipc_dispatch(uint32_t op, const unsigned char* payload, uint32_t length) {
        if(op == IPC_OP_PING) {
                /* Discord's keepalive: must echo the payload back as PONG,
                   otherwise the connection gets dropped. */
                ipc_queue_send(IPC_OP_PONG, (const char*)payload, length);
                return;
        }
        if(op == IPC_OP_CLOSE) {
                if(payload && length > 0)
                        log_warn("Discord RPC: connection closed by Discord: %.*s", (int)length,
                                 (const char*)payload);
                else
                        log_warn("Discord RPC: connection closed by Discord");
                ipc_disconnect();
                return;
        }
        if(op != IPC_OP_FRAME)
                return;

        if(ipc_state == IPC_HANDSHAKE_SENT) {
                /* The expected answer to our handshake is a FRAME whose JSON
                   body contains "evt":"READY". Substring matching is used
                   instead of full JSON parsing on purpose: Discord only ever
                   sends compact objects here and a parse failure must not
                   break presence detection. */
                const char* txt = (const char*)payload;
                if(length >= 5 && (strstr(txt, "\"evt\":\"READY\"") || strstr(txt, "\"evt\": \"READY\"") ||
                                   strstr(txt, "\"evt\":\"ready\""))) {
                        ipc_state = IPC_READY;
                        log_info("Discord RPC: connection established");
                } else {
                        /* e.g. an error dispatch (unknown client id) */
                        log_warn("Discord RPC: unexpected handshake reply: %.*s", (int)length, txt);
                }
        }
        /* Frames after READY are command responses (nonce answers), which we
           don't need, and occasional dispatches; both are safe to ignore. */
}

/* Feeds raw stream bytes into the incremental frame parser. Returns 0 on
   success, -1 when the stream looks broken and the connection must be
   dropped. */
static int ipc_rx_feed(const unsigned char* bytes, uint32_t count) {
        while(count > 0) {
                if(ipc_rx_have_header < 8) {
                        uint32_t need = 8 - ipc_rx_have_header;
                        uint32_t take = count < need ? count : need;
                        memcpy(ipc_rx_header + ipc_rx_have_header, bytes, take);
                        ipc_rx_have_header += take;
                        bytes += take;
                        count -= take;
                        if(ipc_rx_have_header < 8)
                                break;
                        ipc_rx_want_payload = ipc_load_u32le(ipc_rx_header + 4);
                        if(ipc_rx_want_payload > IPC_MAX_PAYLOAD) {
                                log_warn("Discord RPC: oversized frame (%u bytes), reconnecting",
                                         ipc_rx_want_payload);
                                return -1;
                        }
                        free(ipc_rx_payload);
                        ipc_rx_payload = malloc((size_t)ipc_rx_want_payload + 1);
                        if(!ipc_rx_payload)
                                return -1;
                        ipc_rx_have_payload = 0;
                } else {
                        uint32_t need = ipc_rx_want_payload - ipc_rx_have_payload;
                        uint32_t take = count < need ? count : need;
                        memcpy(ipc_rx_payload + ipc_rx_have_payload, bytes, take);
                        ipc_rx_have_payload += take;
                        bytes += take;
                        count -= take;
                        if(ipc_rx_have_payload >= ipc_rx_want_payload) {
                                ipc_rx_payload[ipc_rx_want_payload] = '\0';
                                ipc_dispatch(ipc_load_u32le(ipc_rx_header), ipc_rx_payload,
                                             ipc_rx_want_payload);
                                ipc_rx_reset();
                        }
                }
        }
        return 0;
}

/* ── connection teardown & (re)connect scheduling ─────────────────────────*/

static void ipc_disconnect(void) {
        if(ipc_state != IPC_DISCONNECTED)
                log_info("Discord RPC: disconnected");
#ifdef _WIN32
        if(ipc_handle) {
                CancelIo(ipc_handle);
                CloseHandle(ipc_handle);
                ipc_handle = NULL;
        }
        if(ipc_read_ovl.hEvent) {
                CloseHandle(ipc_read_ovl.hEvent);
                ipc_read_ovl.hEvent = NULL;
        }
        if(ipc_write_ovl.hEvent) {
                CloseHandle(ipc_write_ovl.hEvent);
                ipc_write_ovl.hEvent = NULL;
        }
        ipc_read_pending = 0;
        ipc_write_pending = 0;
#else
        if(ipc_socket >= 0) {
                close(ipc_socket);
                ipc_socket = -1;
        }
#endif
        ipc_state = IPC_DISCONNECTED;
        ipc_queue_clear();
        ipc_rx_reset();
        ipc_next_attempt = time(NULL) + IPC_RECONNECT_DELAY;
}

/* Attempts one connect. Returns 0 when a transport handle was acquired and
   the handshake was queued. Silently fails when Discord isn't running. */
static int ipc_try_connect(void) {
#ifdef _WIN32
        for(int k = 0; k < IPC_PIPE_COUNT; k++) {
                char pipe_name[64];
                snprintf(pipe_name, sizeof(pipe_name), "\\\\.\\pipe\\discord-ipc-%d", k);
                HANDLE h = CreateFileA(pipe_name, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                       OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
                if(h == INVALID_HANDLE_VALUE)
                        continue;

                ZeroMemory(&ipc_read_ovl, sizeof(ipc_read_ovl));
                ZeroMemory(&ipc_write_ovl, sizeof(ipc_write_ovl));
                ipc_read_ovl.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
                ipc_write_ovl.hEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
                if(!ipc_read_ovl.hEvent || !ipc_write_ovl.hEvent) {
                        if(ipc_read_ovl.hEvent)
                                CloseHandle(ipc_read_ovl.hEvent);
                        if(ipc_write_ovl.hEvent)
                                CloseHandle(ipc_write_ovl.hEvent);
                        ZeroMemory(&ipc_read_ovl, sizeof(ipc_read_ovl));
                        ZeroMemory(&ipc_write_ovl, sizeof(ipc_write_ovl));
                        CloseHandle(h);
                        return -1;
                }
                ipc_handle = h;
                ipc_read_pending = 0;
                ipc_write_pending = 0;
                return 0;
        }
        return -1;
#else
        /* Candidate base directories, in priority order. The flatpak and snap
           Discord builds hide the socket in their private runtime dirs. */
        char bases[8][64];
        int base_count = 0;
        const char* env;
        if((env = getenv("XDG_RUNTIME_DIR")) && *env) {
                snprintf(bases[base_count++], 64, "%s", env);
                snprintf(bases[base_count++], 64, "%s/app/com.discordapp.Discord", env);
                snprintf(bases[base_count++], 64, "%s/snap.discord", env);
        }
        if((env = getenv("TMPDIR")) && *env && base_count < 8)
                snprintf(bases[base_count++], 64, "%s", env);
        if((env = getenv("TMP")) && *env && base_count < 8)
                snprintf(bases[base_count++], 64, "%s", env);
        if(base_count < 8)
                snprintf(bases[base_count++], 64, "/tmp");

        for(int b = 0; b < base_count; b++) {
                for(int k = 0; k < IPC_PIPE_COUNT; k++) {
                        char path[128];
                        int plen = snprintf(path, sizeof(path), "%s/discord-ipc-%d", bases[b], k);
                        if(plen <= 0 || plen >= (int)sizeof(path) ||
                           plen >= (int)sizeof(((struct sockaddr_un*)0)->sun_path))
                                continue;

                        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
                        if(fd < 0)
                                return -1;
                        int flags = fcntl(fd, F_GETFL, 0);
                        if(flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                                close(fd);
                                return -1;
                        }
#ifdef __APPLE__
                        /* macOS has no MSG_NOSIGNAL; prevent SIGPIPE per-socket. */
                        int one = 1;
                        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
                        struct sockaddr_un addr;
                        memset(&addr, 0, sizeof(addr));
                        addr.sun_family = AF_UNIX;
                        memcpy(addr.sun_path, path, (size_t)plen + 1);
                        socklen_t addr_len =
                                (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (size_t)plen + 1);

                        if(connect(fd, (struct sockaddr*)&addr, addr_len) == 0 || errno == EINPROGRESS) {
                                ipc_socket = fd;
                                return 0;
                        }
                        /* ENOENT (Discord not running under this index/dir) or
                           ECONNREFUSED (stale socket file): keep looking. */
                        close(fd);
                }
        }
        return -1;
#endif
}

/* ── public API ────────────────────────────────────────────────────────────*/

void discord_ipc_start(const char* client_id) {
        if(!client_id || !*client_id)
                return;
        if(ipc_client_id[0])
                return; /* already running */
        snprintf(ipc_client_id, sizeof(ipc_client_id), "%s", client_id);
        ipc_next_attempt = 0; /* connect ASAP */
}

int discord_ipc_ready(void) {
        return ipc_state == IPC_READY;
}

void discord_ipc_process(void) {
        if(!ipc_client_id[0])
                return; /* never started or no client id configured */
        time_t now = time(NULL);

        if(ipc_state == IPC_DISCONNECTED) {
                if(ipc_next_attempt == 0)
                        ipc_next_attempt = now;
                if(now < ipc_next_attempt)
                        return;
                ipc_next_attempt = now + IPC_RECONNECT_DELAY;
                if(ipc_try_connect() != 0)
                        return;
                ipc_rx_reset();
                ipc_state = IPC_HANDSHAKE_SENT;
                char handshake[96];
                int len = snprintf(handshake, sizeof(handshake), "{\"v\":1,\"client_id\":\"%s\"}",
                                   ipc_client_id);
                if(len <= 0 || len >= (int)sizeof(handshake) ||
                   ipc_queue_send(IPC_OP_HANDSHAKE, handshake, (uint32_t)len) != 0) {
                        ipc_disconnect();
                        return;
                }
        }

#ifdef _WIN32
        /* ── writes (single outstanding overlapped write) ── */
        while(ipc_tx_head) {
                if(!ipc_write_pending) {
                        struct ipc_frame* f = ipc_tx_head;
                        uint32_t total = 8 + f->length;
                        uint32_t remaining = total - f->sent;
                        if(WriteFile(ipc_handle, f->data + f->sent, remaining, NULL,
                                     &ipc_write_ovl) ||
                           GetLastError() == ERROR_IO_PENDING) {
                                ipc_write_pending = 1;
                        } else {
                                ipc_disconnect();
                                return;
                        }
                }
                if(WaitForSingleObject(ipc_write_ovl.hEvent, 0) != WAIT_OBJECT_0)
                        break;
                DWORD written = 0;
                if(!GetOverlappedResult(ipc_handle, &ipc_write_ovl, &written, FALSE)) {
                        ipc_disconnect();
                        return;
                }
                ResetEvent(ipc_write_ovl.hEvent);
                ipc_write_pending = 0;
                ipc_tx_head->sent += written;
                if(ipc_tx_head->sent >= 8 + ipc_tx_head->length)
                        ipc_queue_pop();
        }

        /* ── reads (single outstanding overlapped read) ── */
        for(;;) {
                if(!ipc_read_pending) {
                        if(ReadFile(ipc_handle, ipc_read_buf, sizeof(ipc_read_buf), NULL,
                                    &ipc_read_ovl) ||
                           GetLastError() == ERROR_IO_PENDING) {
                                ipc_read_pending = 1;
                        } else {
                                ipc_disconnect();
                                return;
                        }
                }
                if(WaitForSingleObject(ipc_read_ovl.hEvent, 0) != WAIT_OBJECT_0)
                        break;
                DWORD got = 0;
                if(!GetOverlappedResult(ipc_handle, &ipc_read_ovl, &got, FALSE) || got == 0) {
                        /* pipe broken / Discord exited */
                        ipc_disconnect();
                        return;
                }
                ResetEvent(ipc_read_ovl.hEvent);
                ipc_read_pending = 0;
                if(ipc_rx_feed(ipc_read_buf, got) != 0) {
                        ipc_disconnect();
                        return;
                }
                if(ipc_state == IPC_DISCONNECTED) /* CLOSE frame dropped us */
                        return;
        }
#else
        /* ── writes ── */
        while(ipc_tx_head) {
                struct ipc_frame* f = ipc_tx_head;
                uint32_t total = 8 + f->length;
                uint32_t remaining = total - f->sent;
#ifdef MSG_NOSIGNAL
                ssize_t n = send(ipc_socket, f->data + f->sent, remaining, MSG_NOSIGNAL);
#else
                ssize_t n = send(ipc_socket, f->data + f->sent, remaining, 0);
#endif
                if(n < 0) {
                        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                                break;
                        ipc_disconnect();
                        return;
                }
                f->sent += (uint32_t)n;
                if(f->sent >= total)
                        ipc_queue_pop();
        }

        /* ── reads ── */
        unsigned char buf[IPC_READ_CHUNK];
        for(;;) {
                ssize_t n = recv(ipc_socket, buf, sizeof(buf), 0);
                if(n < 0) {
                        if(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                                break;
                        ipc_disconnect();
                        return;
                }
                if(n == 0) { /* orderly shutdown by Discord */
                        ipc_disconnect();
                        return;
                }
                if(ipc_rx_feed(buf, (uint32_t)n) != 0) {
                        ipc_disconnect();
                        return;
                }
                if(ipc_state == IPC_DISCONNECTED) /* CLOSE frame dropped us */
                        return;
        }
#endif
}

int discord_ipc_set_activity(const char* activity_json) {
        if(ipc_state != IPC_READY)
                return -1;

        /* Parse the activity up front: this guarantees we only ever ship
           well-formed, properly escaped JSON regardless of what the caller
           produced. */
        JSON_Value* activity = NULL;
        if(activity_json) {
                activity = json_parse_string(activity_json);
                if(!activity) {
                        log_warn("Discord RPC: malformed activity JSON dropped");
                        return -1;
                }
        }

        /* Build {"cmd":"SET_ACTIVITY","args":{"pid":..,"activity":..},
           "nonce":".."} — empty-attach args immediately so no subtree can be
           orphaned on a failure midway. */
        JSON_Value* cmd_val = json_value_init_object();
        if(!cmd_val) {
                json_value_free(activity);
                return -1;
        }
        JSON_Object* cmd = json_object(cmd_val);

        JSON_Value* args_val = json_value_init_object();
        if(!args_val ||
           json_object_set_value(cmd, "args", args_val) != JSONSuccess) {
                if(args_val && json_object_get_value(cmd, "args") != args_val)
                        json_value_free(args_val);
                json_value_free(cmd_val);
                json_value_free(activity);
                return -1;
        }
        JSON_Object* args = json_object_get_object(cmd, "args");

        char nonce[24];
        snprintf(nonce, sizeof(nonce), "ks-%u", ipc_nonce++);

        int ok = json_object_set_string(cmd, "cmd", "SET_ACTIVITY") == JSONSuccess &&
                json_object_set_number(args, "pid", (double)ipc_get_pid()) == JSONSuccess &&
                json_object_set_string(cmd, "nonce", nonce) == JSONSuccess;
        if(ok) {
                /* Ownership of `activity` moves into the command tree here. */
                ok = activity ? json_object_set_value(args, "activity", activity) == JSONSuccess
                              : json_object_set_null(args, "activity") == JSONSuccess;
        }

        char* serialized = NULL;
        if(ok)
                serialized = json_serialize_to_string(cmd_val);

        /* The frame goes out on the next discord_ipc_process() pump (at most
           one game frame later). It deliberately isn't flushed here: incoming
           data must only ever be handled from the main loop's pump, so a
           CLOSE/error can't land in the middle of this call. */
        if(serialized) {
                int rc = ipc_queue_send(IPC_OP_FRAME, serialized, (uint32_t)strlen(serialized));
                json_free_serialized_string(serialized);
                if(rc != 0) {
                        ok = 0;
                }
        } else {
                ok = 0;
        }

        int attached = json_object_get_value(args, "activity") == activity;
        json_value_free(cmd_val);
        if(activity && !attached)
                json_value_free(activity);

        return ok ? 0 : -1;
}

void discord_ipc_stop(void) {
        ipc_queue_clear();
        ipc_disconnect();
        ipc_client_id[0] = '\0';
        ipc_next_attempt = 0;
}

#endif /* USE_RPC */
