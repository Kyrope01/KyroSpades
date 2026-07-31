
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>

#include "chathistory.h"
#include "log.h"
#include "network.h"

#define HIST_MAX_DAYS 30

struct hist_state {
	struct chathistory_line* lines;
	int count;
	int cap;
};

static struct hist_state public_state;
static pthread_mutex_t public_lock = PTHREAD_MUTEX_INITIALIZER;
static int loading_flag   = 0;
static int no_more_flag   = 0;
static int next_day_offset = 0;          /* days back from today for next scan */
static char target_ip[64]   = {0};
static int  target_port     = 0;

static struct hist_state worker_result;
static int worker_advance = 0;           /* days the worker scanned */
static int worker_done    = 0;
static int worker_no_more = 0;
static pthread_t worker_thread;

static void hist_state_clear(struct hist_state* s) {
	free(s->lines);
	s->lines = NULL;
	s->count = s->cap = 0;
}

static int hist_state_push(struct hist_state* s, const struct chathistory_line* line) {
	if(s->count >= s->cap) {
		int nc = s->cap ? s->cap * 2 : 64;
		struct chathistory_line* nl = realloc(s->lines, nc * sizeof(*nl));
		if(!nl) return 0;
		s->lines = nl;
		s->cap = nc;
	}
	s->lines[s->count++] = *line;
	return 1;
}

/* Prepend `add` lines onto the public buffer. The `add` block is the
   chronologically older batch the worker just produced. */
static int hist_state_prepend(struct hist_state* dst, const struct hist_state* add) {
	if(add->count == 0) return 1;
	int total = dst->count + add->count;
	struct chathistory_line* nl = realloc(dst->lines, total * sizeof(*nl));
	if(!nl) return 0;
	memmove(nl + add->count, nl, dst->count * sizeof(*nl));
	memcpy(nl, add->lines, add->count * sizeof(*nl));
	dst->lines = nl;
	dst->count = total;
	dst->cap = total;
	return 1;
}

void chathistory_reset(void) {
	pthread_mutex_lock(&public_lock);
	hist_state_clear(&public_state);
	hist_state_clear(&worker_result);
	loading_flag = 0;
	no_more_flag = 0;
	worker_done = 0;
	worker_no_more = 0;
	next_day_offset = 0;
	target_ip[0] = 0;
	target_port = 0;
	pthread_mutex_unlock(&public_lock);
}

int chathistory_loading(void) { return loading_flag; }
int chathistory_no_more(void) { return no_more_flag; }
int chathistory_count(void)   { return public_state.count; }

const struct chathistory_line* chathistory_get(int i) {
	if(i < 0 || i >= public_state.count) return NULL;
	return &public_state.lines[i];
}

/* Build "logs/MM-DD-YYYY.log" path for today minus `days_back`. */
static void build_log_path(int days_back, char* out, size_t out_size) {
	time_t t = time(NULL) - (time_t)days_back * 86400;
	struct tm* tm = localtime(&t);
	strftime(out, out_size, "logs/%m-%d-%Y.log", tm);
}

/* Parse ISO-ish "YYYY-MM-DD HH:MM:SS" into time_t (local time). Returns 0
   on failure. */
static time_t parse_log_timestamp(const char* line) {
	int y, mo, d, h, mi, s;
	if(sscanf(line, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &s) != 6)
		return 0;
	struct tm tm = {0};
	tm.tm_year = y - 1900;
	tm.tm_mon  = mo - 1;
	tm.tm_mday = d;
	tm.tm_hour = h;
	tm.tm_min  = mi;
	tm.tm_sec  = s;
	tm.tm_isdst = -1;
	return mktime(&tm);
}

/* Locate "main.c:N: " inside line. Returns N on hit (>0), 0 on miss.
   On hit, *payload_out points at the byte after ": ". */
static int detect_main_c_tag(const char* line, const char** payload_out) {
	const char* p = strstr(line, "main.c:");
	if(!p) return 0;
	p += 7;
	int n = 0;
	while(*p >= '0' && *p <= '9') { n = n * 10 + (*p - '0'); p++; }
	if(n == 0 || *p != ':') return 0;
	if(*(p + 1) != ' ') return 0;
	if(payload_out) *payload_out = p + 2;
	return n;
}

/* Locate "network.c:N: Connecting to <ip> at port <port>" data. Returns 1
   if matched and fills out_ip/out_port. */
static int parse_connecting_line(const char* line, char* out_ip, int out_ip_size, int* out_port) {
	const char* p = strstr(line, "network.c:");
	if(!p) return 0;
	const char* connecting = strstr(p, "Connecting to ");
	if(!connecting) return 0;
	connecting += strlen("Connecting to ");
	const char* port_kw = strstr(connecting, " at port ");
	if(!port_kw) return 0;
	int n = (int)(port_kw - connecting);
	if(n <= 0 || n >= out_ip_size) return 0;
	memcpy(out_ip, connecting, n);
	out_ip[n] = 0;
	*out_port = atoi(port_kw + strlen(" at port "));
	return 1;
}

/* Heuristic: a payload containing both "joined the " and " team" is
   produced exclusively by the "joined the X team" log line, which
   originates from the same main.c line as live chat output (chat_add ->
   log_info). Whichever main.c:N tag we see this on is the chat tag. */
static int payload_is_join_marker(const char* payload) {
	const char* j = strstr(payload, "joined the ");
	if(!j) return 0;
	if(!strstr(j, " team")) return 0;
	return 1;
}

static int scan_log_file(const char* path, struct hist_state* out, int is_today) {
	FILE* f = fopen(path, "r");
	if(!f) return 0;

	int chat_tag = 0;
	char cur_ip[64] = {0};
	int  cur_port = 0;

	/* First pass: detect chat tag. */
	char line[1024];
	while(fgets(line, sizeof(line), f)) {
		const char* payload;
		int n = detect_main_c_tag(line, &payload);
		if(n > 0 && payload_is_join_marker(payload)) {
			chat_tag = n;
			break;
		}
	}
	if(chat_tag == 0) {
		fclose(f);
		return 1; /* file scanned, nothing to extract */
	}

	/* For today's log, find the byte offset of the last "Connecting to
	   target" line. Lines after that belong to the current live session
	   (already in the in-memory ring) and must be skipped to avoid dupes. */
	long cutoff_off = -1;
	if(is_today) {
		rewind(f);
		long line_start = ftell(f);
		while(fgets(line, sizeof(line), f)) {
			char tmp_ip[64];
			int  tmp_port;
			if(parse_connecting_line(line, tmp_ip, sizeof(tmp_ip), &tmp_port)
			   && strcmp(tmp_ip, target_ip) == 0 && tmp_port == target_port) {
				cutoff_off = line_start;
			}
			line_start = ftell(f);
		}
	}

	rewind(f);
	int produced = 0;
	long line_start = ftell(f);
	/* Set whenever we see a 'Connecting to <target>' line; consumed by
	   the next emitted chat line to mark the session boundary. */
	int next_is_session_start = 0;
	while(fgets(line, sizeof(line), f)) {
		if(cutoff_off >= 0 && line_start >= cutoff_off) break;
		line_start = ftell(f);
		char tmp_ip[64];
		int  tmp_port;
		if(parse_connecting_line(line, tmp_ip, sizeof(tmp_ip), &tmp_port)) {
			strncpy(cur_ip, tmp_ip, sizeof(cur_ip) - 1);
			cur_ip[sizeof(cur_ip) - 1] = 0;
			cur_port = tmp_port;
			/* Only matters when this connection lands on the server
			   we're currently filtering for; lines from other servers
			   never reach the emit branch anyway. */
			if(strcmp(cur_ip, target_ip) == 0 && cur_port == target_port)
				next_is_session_start = 1;
			continue;
		}

		const char* payload;
		int n = detect_main_c_tag(line, &payload);
		if(n != chat_tag) continue;
		if(strcmp(cur_ip, target_ip) != 0 || cur_port != target_port) continue;

		struct chathistory_line hl;
		memset(&hl, 0, sizeof(hl));
		hl.when = parse_log_timestamp(line);
		hl.is_session_start = next_is_session_start;
		next_is_session_start = 0;

		/* Trim trailing newline from payload. */
		size_t pl = strlen(payload);
		while(pl > 0 && (payload[pl - 1] == '\n' || payload[pl - 1] == '\r')) pl--;
		if(pl >= sizeof(hl.raw)) pl = sizeof(hl.raw) - 1;
		memcpy(hl.raw, payload, pl);
		hl.raw[pl] = 0;

		hist_state_push(out, &hl);
		produced++;
	}

	fclose(f);
	return produced;
}

static void* worker_main(void* arg) {
	(void)arg;
	int days = next_day_offset;
	int produced_any = 0;

	while(days <= HIST_MAX_DAYS) {
		char path[64];
		build_log_path(days, path, sizeof(path));
		struct stat st;
		if(stat(path, &st) != 0) { days++; continue; }

		int produced = scan_log_file(path, &worker_result, days == 0);
		if(produced > 0) {
			produced_any = 1;
			days++;
			break;
		}
		days++;
	}

	worker_advance = days;
	worker_no_more = (days >= HIST_MAX_DAYS && !produced_any);
	__sync_synchronize();
	worker_done = 1;
	return NULL;
}

int chathistory_request_load(void) {
	if(loading_flag || no_more_flag) return 0;
	if(!network_connected) return 0;
	if(!network_current_ip[0] || network_current_port == 0) return 0;

	pthread_mutex_lock(&public_lock);
	if(target_ip[0] == 0) {
		strncpy(target_ip, network_current_ip, sizeof(target_ip) - 1);
		target_ip[sizeof(target_ip) - 1] = 0;
		target_port = network_current_port;
	}
	pthread_mutex_unlock(&public_lock);

	hist_state_clear(&worker_result);
	worker_done = 0;
	worker_no_more = 0;
	worker_advance = next_day_offset;
	loading_flag = 1;

	if(pthread_create(&worker_thread, NULL, worker_main, NULL) != 0) {
		log_error("chathistory: pthread_create failed");
		loading_flag = 0;
		return 0;
	}
	pthread_detach(worker_thread);
	return 1;
}

void chathistory_poll(void) {
	if(!loading_flag) return;
	if(!worker_done) return;

	pthread_mutex_lock(&public_lock);
	hist_state_prepend(&public_state, &worker_result);
	hist_state_clear(&worker_result);
	next_day_offset = worker_advance;
	if(worker_no_more) no_more_flag = 1;
	loading_flag = 0;
	worker_done = 0;
	pthread_mutex_unlock(&public_lock);
}


