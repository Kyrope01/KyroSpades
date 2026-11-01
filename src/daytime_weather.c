/* Daytime and Weather (aosprotocol extension 0x33, packet 0x73, version 1).
   Mirrors ZeroSpades' implementation (zerospades PR #76): the server sends one
   Sky saying whether it is day or night, the client holds the world until that
   first Sky arrives, and the Time survives Map Start. */
#include "common.h"
#include "chunk.h"
#include "daytime_weather.h"
#include "demo.h"
#include "lighting.h"
#include "window.h"

/* How long a negotiated world may be held waiting for its first Sky. The
   extension says the client waits, but a server that forgets to send one must
   not leave the player staring at a loading screen. */
#define DAYTIME_WEATHER_SKY_TIMEOUT 3.0F

enum {
        TIME_UNKNOWN = -1,
        TIME_DAY = 1,
        TIME_NIGHT = 2,
};

static int negotiated;
static int time_of_day = TIME_UNKNOWN;
static float wait_started = -1.0F;
static bool wait_timed_out;

static uint16_t read_u16(const uint8_t* p) {
        return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void write_u16(uint8_t* p, uint16_t value) {
        p[0] = (uint8_t)(value & 0xFF);
        p[1] = (uint8_t)(value >> 8);
}

void daytime_weather_reset_connection(void) {
        negotiated = 0;
        time_of_day = TIME_UNKNOWN;
        wait_started = -1.0F;
        wait_timed_out = false;
}

void daytime_weather_reset_world(void) {
        /* The negotiation belongs to the connection; only the Time is per map. */
        time_of_day = TIME_UNKNOWN;
        wait_started = -1.0F;
        wait_timed_out = false;
}

void daytime_weather_set_negotiated(int is_negotiated) {
        negotiated = is_negotiated ? 1 : 0;
        if(!negotiated) {
                time_of_day = TIME_UNKNOWN;
                wait_started = -1.0F;
                wait_timed_out = false;
        }
}

int daytime_weather_negotiated(void) {
        return negotiated;
}

void daytime_weather_world_entered(void) {
        wait_started = window_time();
        wait_timed_out = false;
}

int daytime_weather_awaiting_sky(void) {
        if(!negotiated || time_of_day != TIME_UNKNOWN)
                return 0;
        /* A demo may well have been recorded after the Sky was sent, and it
           replays packets rather than negotiating; never hold a playback. */
        if(demo_is_playing())
                return 0;
        if(wait_started < 0.0F) {
                /* Not entered through the map-loading path (a demo seek, an
                   early spawn): hold only if something started the clock. */
                return 0;
        }
        if(window_time() - wait_started >= DAYTIME_WEATHER_SKY_TIMEOUT) {
                if(!wait_timed_out) {
                        log_warn("Server negotiated Daytime and Weather but sent no Sky after %.1fs; "
                                 "drawing the world as day", (double)DAYTIME_WEATHER_SKY_TIMEOUT);
                        wait_timed_out = true;
                }
                return 0;
        }
        return 1;
}

int daytime_weather_is_night(void) {
        return time_of_day == TIME_NIGHT;
}

float daytime_weather_sunlight(void) {
        return daytime_weather_is_night() ? 0.0F : 1.0F;
}

int daytime_weather_parse_sky(const uint8_t* data, int len, int* time_out) {
        if(!data || len < 1 || data[0] != DAYTIME_WEATHER_SUB_SKY)
                return 0;
        if(len < 1 + DAYTIME_WEATHER_SKY_BYTES)
                return 0;
        int time = (int)read_u16(data + 1);
        /* Reserved and Weather are ignored: version 1 requires both to be 0,
           and a later version may put the speed of the day in Reserved. */
        if(time_out)
                *time_out = time;
        return 1;
}

size_t daytime_weather_encode_sky(uint8_t* out, size_t cap, int night) {
        size_t needed = 2 + DAYTIME_WEATHER_SKY_BYTES;
        if(!out || cap < needed)
                return 0;
        out[0] = DAYTIME_WEATHER_PACKET_ID;
        out[1] = DAYTIME_WEATHER_SUB_SKY;
        write_u16(out + 2, (uint16_t)(night ? DAYTIME_WEATHER_TIME_NIGHT : DAYTIME_WEATHER_TIME_NOON));
        write_u16(out + 4, 0); /* reserved */
        out[6] = 0;            /* weather, unimplemented in version 1 */
        out[7] = 0;
        return needed;
}

void daytime_weather_receive(const uint8_t* data, int len) {
        int time;
        if(!negotiated) {
                /* An extension that was not negotiated is not used. */
                return;
        }
        if(!daytime_weather_parse_sky(data, len, &time)) {
                if(data && len >= 1 && data[0] != DAYTIME_WEATHER_SUB_SKY)
                        log_debug("Ignoring an unknown Daytime and Weather sub-packet %u", data[0]);
                return;
        }
        int new_time = (time == DAYTIME_WEATHER_TIME_NIGHT) ? TIME_NIGHT : TIME_DAY;
        if(new_time == time_of_day)
                return;
        time_of_day = new_time;
        wait_started = -1.0F;
        /* The baked paths put the sun into the vertices once per remesh, so a
           flipped sun needs new vertices; the shader paths read the scale every
           frame and are already correct. */
        if(!lighting_world_supported())
                chunk_rebuild_all();
        log_info("Daytime and Weather: it is now %s", daytime_weather_is_night() ? "night" : "day");
}

void daytime_weather_record_initial(void (*record)(const uint8_t*, size_t)) {
        uint8_t packet[2 + DAYTIME_WEATHER_SKY_BYTES];
        if(!record || !negotiated || time_of_day == TIME_UNKNOWN)
                return;
        if(daytime_weather_encode_sky(packet, sizeof packet, daytime_weather_is_night()) == 0)
                return;
        record(packet, sizeof packet);
}
