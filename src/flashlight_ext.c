#include <string.h>
#include "flashlight_ext.h"

static uint8_t states[FLASHLIGHT_PLAYERS];
static struct flashlight_beam personal[FLASHLIGHT_PLAYERS];
static bool has_personal[FLASHLIGHT_PLAYERS];
static struct flashlight_beam default_beam = {60, 90, 255, 179, 128, 0};
static bool negotiated;

void flashlight_ext_reset_connection(void) {
    negotiated = false;
    memset(states, 0, sizeof states);
    memset(has_personal, 0, sizeof has_personal);
    default_beam = (struct flashlight_beam){60, 90, 255, 179, 128, 0};
}
void flashlight_ext_set_negotiated(bool enabled) {
    /* Re-handshakes must not leave state from an older server in place. */
    flashlight_ext_reset_connection();
    negotiated = enabled;
}
bool flashlight_ext_negotiated(void) { return negotiated; }
void flashlight_ext_reset_map(void) { memset(states, 0, sizeof states); }
void flashlight_ext_reset_player(unsigned int id, bool left) {
    if(id >= FLASHLIGHT_PLAYERS) return;
    states[id] = 0;
    if(left) has_personal[id] = false;
}
bool flashlight_ext_on(unsigned int id) {
    return negotiated && id < FLASHLIGHT_PLAYERS && states[id] != 0;
}
struct flashlight_beam flashlight_ext_beam(unsigned int id) {
    return id < FLASHLIGHT_PLAYERS && has_personal[id] ? personal[id] : default_beam;
}
void flashlight_ext_request(unsigned int id, uint8_t out[3]) {
    out[0] = 0;
    out[1] = (uint8_t)id;
    out[2] = flashlight_ext_on(id) ? 0 : 1;
}
void flashlight_ext_receive(const void *data, size_t len) {
    if(!negotiated || !data || len < 1) return;
    const uint8_t *p = data;
    switch(p[0]) {
        case 0: /* Light: server owns the state. Reserved states 2..255 are on. */
            if(len != 3) return;
            states[p[1]] = p[2];
            break;
        case 1: /* Bit n of byte n/8, low bit first. Missing IDs are off. */
            memset(states, 0, sizeof states);
            for(size_t id = 0; id < FLASHLIGHT_PLAYERS && id / 8 + 1 < len; ++id)
                states[id] = (p[1 + id / 8] >> (id % 8)) & 1u;
            break;
        case 2: { /* The all-player default never replaces a personal override. */
            /* Sub-packet, player, reach, cone, red, green, blue, flicker. A
               server built before Flicker existed sends one byte fewer; the
               beam is then steady, which is what its absence meant. */
            if(len != 8 && len != 7) return;
            struct flashlight_beam beam = {p[2], p[3], p[4], p[5], p[6], len == 8 ? p[7] : 0};
            if(p[1] == 255) default_beam = beam;
            else { personal[p[1]] = beam; has_personal[p[1]] = true; }
            break;
        }
        default: break;
    }
}

/* A deterministic hash of the player id and the time slot, so that every
   client drops the same beam out at the same moment without any state.
   Changing it changes the pattern for everyone, which is all v1 asks for. */
static uint32_t flicker_hash(uint32_t id, uint32_t slot) {
    uint32_t h = id * 2654435761u ^ slot * 2246822519u;
    h ^= h >> 15;
    h *= 2654435761u;
    h ^= h >> 13;
    return h;
}

bool flashlight_ext_dark(unsigned int id, double time) {
    if(!negotiated || id >= FLASHLIGHT_PLAYERS) return false;
    const struct flashlight_beam beam = flashlight_ext_beam(id);
    if(!beam.flicker) return false;
    /* 12 slots a second: short bursts, and dark Flicker/512 of the time. */
    const uint32_t slot = (uint32_t)(time * 12.0);
    return flicker_hash(id, slot) % 512u < beam.flicker;
}
