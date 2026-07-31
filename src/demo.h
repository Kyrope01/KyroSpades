
/*
    Demo recording and playback for KyroSpades.

    Recording is compatible with aos_replay format.
    Playback adapts ZeroSpades' DemoPlayer/DemoNetClient design to C:
      - All packets pre-loaded into memory for O(log n) seeking
      - Bootstrap-end detection so backward seeks never land before the map
      - demo_seeking flag suppresses sound/particles/chat during fast replay
      - Initial decompressed VXL snapshot enables world reset for backward seeks
*/

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "enet/enet.h"

/* ─── Recording ────────────────────────────────────────────────── */

struct Demo {
    FILE*  fp;
    float  start_time;
};

extern struct Demo CurrentDemo;

FILE* create_demo_file(void);
void  register_demo_packet(ENetPacket* packet);
void  demo_start_record(void);
void  demo_stop_record(void);
bool  demo_is_server_omited_packet(int id);

/* ─── Playback ─────────────────────────────────────────────────── */

/* A single packet entry pre-loaded from the .demo file. */
struct DemoPacketEntry {
    float          timestamp; /* seconds since demo start */
    unsigned short length;    /* total length including the packet-id byte */
    unsigned char* data;      /* heap-allocated; data[0] is the packet type id */
};

/* A decompressed VXL map captured at the moment its StateData was processed
   during normal play, so a backward seek can restore the correct map. */
struct DemoMapSnapshot {
    float  timestamp;     /* StateData timestamp this map became active   */
    int    packet_index;  /* index of that StateData packet in packets[]  */
    void*  data;          /* heap-allocated decompressed VXL bytes        */
    size_t size;
};

struct DemoPlayback {
    bool   active;
    bool   paused;
    bool   finished;
    float  current_time;       /* virtual demo clock (seconds)              */
    float  duration;           /* total demo duration (seconds)             */
    float  speed;              /* playback speed multiplier (default 1.0)   */
    float  bootstrap_end_time; /* timestamp of last bootstrap packet        */
    double last_real_time;     /* window_time() at last update              */

    struct DemoPacketEntry* packets;
    int    packet_count;
    int    packet_capacity;
    int    current_packet_index;

    int    protocol_version;   /* 3 = 0.75, 4 = 0.76 */

    /* Decompressed VXL snapshots, one per map load (StateData), so backward
       seeks can restore the map that was active at the target time without
       re-parsing the demo.  Sorted by timestamp in load order. */
    struct DemoMapSnapshot* maps;
    int    map_count;
    int    map_capacity;
};

extern struct DemoPlayback DemoPlaybackState;

/* True only during a backward reset-replay, when the world has been restored
   from the saved snapshot.  Tells the map packet handlers (MapStart/MapChunk/
   StateData) to skip loading.  Effect-muting is handled separately by
   demo_mute_effects().  Read-only outside demo.c. */
extern bool demo_seeking;

/* Open a .demo file and start playback.  Returns false on error. */
bool  demo_playback_open(const char* filename);

/* Stop playback and free all resources. */
void  demo_playback_close(void);

/* Advance the virtual clock and dispatch any packets that are due.
   Call once per frame from network_update() when demo_is_playing(). */
void  demo_playback_update(void);

/* Seek to an absolute time (seconds).  Backward seeks rebuild world state. */
void  demo_playback_seek(float time);

/* Skip forward by `seconds` from the current position. */
void  demo_playback_fast_forward(float seconds);

void  demo_playback_pause(void);
void  demo_playback_resume(void);
void  demo_playback_toggle_pause(void);

/* Clamp to [0.25, 8.0]. */
void  demo_playback_set_speed(float speed);

bool  demo_is_playing(void);
bool  demo_is_seeking(void);
/* True during a seek: suppress one-shot effects (sounds, particles) but
   still replay chat / join / leave so the chat log stays complete. */
bool  demo_mute_effects(void);
/* Game clock: identical to window_time() except it freezes during demo pause.
   Use in render/animation paths instead of window_time(). */
float game_time(void);
/* True when playing AND paused (includes auto-pause at end of demo).
   Use this to freeze world physics without stopping camera movement. */
bool  demo_is_frozen(void);

/* Fills `out` with a heap-allocated array of heap-allocated filename strings
   found in the demos/ folder, sorted alphabetically.  The caller must free
   each string and the array itself.  Returns the number of entries. */
int   demo_list_files(char*** out);

/* Called from read_PacketStateData() when the map is first decompressed.
   Saves a copy of the VXL bytes so backward seeks can restore world state. */
void  demo_playback_save_initial_map(const void* data, size_t size);


