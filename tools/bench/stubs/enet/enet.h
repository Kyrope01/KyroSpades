/* Minimal enet surface so the CPU-only harness can type-check the modules that
 * include <enet/enet.h> (hud.c, network.c, ping.c, demo.c).
 *
 * This is NOT enet: it exists purely so `make -C tools/bench check` can catch
 * typos and type errors in those files on a machine without the real library.
 * Never link against anything built from this header.
 */
#ifndef BENCH_ENET_STUB_H
#define BENCH_ENET_STUB_H

#include <stdint.h>
#include <stddef.h>

typedef struct ENetAddress {
	uint32_t host;
	uint16_t port;
} ENetAddress;

typedef struct ENetPacket {
	size_t referenceCount;
	uint32_t flags;
	uint8_t* data;
	size_t dataLength;
	size_t freeCallback;
	void* userData;
} ENetPacket;

typedef struct ENetPacketFreeCallback { int unused; } ENetPacketFreeCallback;

typedef struct ENetHost { int unused; } ENetHost;
typedef struct ENetPeer {
	void* data;
	uint32_t roundTripTime;
	uint32_t state;
	ENetAddress address;
} ENetPeer;
typedef struct ENetSocket { int unused; } ENetSocket;

typedef struct ENetEvent {
	int type;
	ENetPeer* peer;
	uint8_t channelID;
	uint32_t data;
	ENetPacket* packet;
} ENetEvent;

typedef struct ENetBuffer {
	size_t dataLength;
	void* data;
} ENetBuffer;

typedef uint8_t enet_uint8;
typedef uint16_t enet_uint16;
typedef uint32_t enet_uint32;
typedef int enet_socket_t;

enum {
	ENET_SOCKET_TYPE_STREAM = 1,
	ENET_SOCKET_TYPE_DATAGRAM = 2,
	ENET_SOCKOPT_NONBLOCK = 1,
	ENET_SOCKOPT_BROADCAST = 2,
	ENET_SOCKOPT_RCVBUF = 3,
	ENET_SOCKOPT_SNDBUF = 4,
	ENET_EVENT_TYPE_NONE = 0,
	ENET_EVENT_TYPE_CONNECT = 1,
	ENET_EVENT_TYPE_DISCONNECT = 2,
	ENET_EVENT_TYPE_RECEIVE = 3,
	ENET_PACKET_FLAG_RELIABLE = 1,
	ENET_PACKET_FLAG_UNSEQUENCED = 2,
	ENET_HOST_ANY = 0,
};

extern int enet_socket_receive(ENetSocket, ENetAddress*, ENetBuffer*, size_t);
extern void enet_packet_destroy(ENetPacket*);
extern ENetPacket* enet_packet_create(const void*, size_t, uint32_t);
extern ENetSocket enet_socket_create(int);
extern int enet_socket_set_option(ENetSocket, int, int);
extern int enet_socket_send(ENetSocket, const ENetAddress*, const ENetBuffer*, size_t);
extern int enet_socket_destroy(ENetSocket);



/* Type-check-only declarations. */
extern ENetHost* enet_host_create(const ENetAddress*, size_t, size_t, uint32_t, uint32_t);
extern void enet_host_destroy(ENetHost*);
extern ENetPeer* enet_host_connect(ENetHost*, const ENetAddress*, size_t, uint32_t);
extern int enet_host_service(ENetHost*, ENetEvent*, uint32_t);
extern void enet_host_flush(ENetHost*);
extern int enet_host_compress_with_range_coder(ENetHost*);
extern int enet_initialize(void);
extern void enet_deinitialize(void);
extern int enet_peer_send(ENetPeer*, uint8_t, ENetPacket*);
extern void enet_peer_disconnect(ENetPeer*, uint32_t);
extern void enet_peer_reset(ENetPeer*);
extern int enet_address_set_host(ENetAddress*, const char*);

#endif /* BENCH_ENET_STUB_H */
