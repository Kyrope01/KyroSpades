#ifndef DAYTIME_WEATHER_H
#define DAYTIME_WEATHER_H

#include "common.h"

/* Daytime and Weather (aosprotocol extension 0x33, packet 0x73, version 1).
   The server owns the time of day; this client only reacts to it. See
   docs/networking.md. */

/* The extension id itself lives in network.h's enum as EXT_DAYTIME_WEATHER; a
   macro of the same name here would rewrite that declaration whenever this
   header is included first. */
#define DAYTIME_WEATHER_PACKET_ID 0x73 /* PACKET_EXT_BASE + EXT_DAYTIME_WEATHER */
#define DAYTIME_WEATHER_VERSION 1

enum {
        /* Sub-packet ids. */
        DAYTIME_WEATHER_SUB_SKY = 0,
        /* Sky carries Time (UShort), Reserved (UShort) and Weather (UByte[2]). */
        DAYTIME_WEATHER_SKY_BYTES = 6,
        /* Time values the server sends version 1 with: midnight and noon. Any
           value but 0 means day, so a later version may read the field as
           minutes since midnight without breaking this client. */
        DAYTIME_WEATHER_TIME_NIGHT = 0,
        DAYTIME_WEATHER_TIME_NOON = 720,
};

/* One connection: negotiated on ExtInfo, then the last Sky the server sent. The
   Time belongs to the connection and survives Map Start, exactly like the
   extension requires. */
void daytime_weather_reset_connection(void);
/* Map Start and demo seeks forget the Time but keep the negotiation. */
void daytime_weather_reset_world(void);

void daytime_weather_set_negotiated(int negotiated);
int daytime_weather_negotiated(void);

/* Starts the "wait for the first Sky" clock. A client that negotiated the
   extension must not draw the world before the server has said what time it is,
   so the map is held until that packet arrives (bounded, see
   DAYTIME_WEATHER_SKY_TIMEOUT). */
void daytime_weather_world_entered(void);
int daytime_weather_awaiting_sky(void);

/* 1 when the last Sky said night. Night means no sun light and no shadow,
   black fog and sky, and only dynamic lights lighting the world. */
int daytime_weather_is_night(void);
/* 1.0 by day, or when nothing was negotiated; 0.0 by night. Multiplies the
   sunlight scale and the render-time fog colour. Dynamic lights do not read
   it, so a flashlight is as bright at night as by day. */
float daytime_weather_sunlight(void);

/* Routes a whole packet body (packet id already stripped, as the packet table
   delivers it). Ignores anything that was not negotiated. */
void daytime_weather_receive(const uint8_t* data, int len);

/* Pure wire helpers: a Sky body is the sub-packet id followed by
   DAYTIME_WEATHER_SKY_BYTES. Returns 1 when a Sky was parsed, and writes its
   Time through `time_out` when that is not NULL. */
int daytime_weather_parse_sky(const uint8_t* data, int len, int* time_out);
/* Writes a whole Sky packet, packet id included. Returns the byte count, or 0
   when `cap` is too small. */
size_t daytime_weather_encode_sky(uint8_t* out, size_t cap, int night);

/* Demo support: a recording that starts after the Sky must still replay the
   time of day, so the current state is written into the recording on start. */
void daytime_weather_record_initial(void (*record)(const uint8_t*, size_t));

#endif
