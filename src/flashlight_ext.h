/* aosprotocol Flashlight V1 (zerospades/aosprotocol PR #9, revision flashlight-v1).
 * Packet numbers include their top-level ID only in network_send/dispatch. */
#ifndef FLASHLIGHT_EXT_H
#define FLASHLIGHT_EXT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FLASHLIGHT_EXT_ID 0x32
#define FLASHLIGHT_PACKET_ID 0x72
#define FLASHLIGHT_PLAYERS 256
struct flashlight_beam {
    uint8_t reach, cone, red, green, blue;
};
void flashlight_ext_reset_connection(void);
void flashlight_ext_set_negotiated(bool enabled);
bool flashlight_ext_negotiated(void);
void flashlight_ext_reset_map(void);
void flashlight_ext_reset_player(unsigned int id, bool left);
void flashlight_ext_receive(const void *data, size_t len);
bool flashlight_ext_on(unsigned int id);
struct flashlight_beam flashlight_ext_beam(unsigned int id);
/* Payload for packet 0x72 (network_send prepends the top-level ID). */
void flashlight_ext_request(unsigned int id, uint8_t out[3]);
#endif
