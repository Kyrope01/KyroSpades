
/*
 * Asynchronous loader for previous-session chat replayed from log files.
 * Reads up to 30 days of history per call, filtered to the currently
 * connected server (by ip:port), exposing results to chatlog.c for
 * rendering with day-grouped separators.
 */
#ifndef CHATHISTORY_H
#define CHATHISTORY_H

#include <time.h>

struct chathistory_line {
	char raw[256];   /* contains color codes, same shape as chat[][] */
	time_t when;     /* line timestamp parsed from log header */
	int is_session_start; /* 1 if this is the first chat line of a
	                         (re)connection session for the target server,
	                         used by the chatlog renderer to draw a
	                         session separator before this line. */
};

void chathistory_reset(void);

/* Spawns the worker if idle. Returns 1 if a load was kicked off, 0 if a
   load is already running or no more data is available. */
int  chathistory_request_load(void);

/* Call each frame; integrates worker results into the public buffer. */
void chathistory_poll(void);

int  chathistory_loading(void);
int  chathistory_no_more(void);

int  chathistory_count(void);
const struct chathistory_line* chathistory_get(int i);

#endif
