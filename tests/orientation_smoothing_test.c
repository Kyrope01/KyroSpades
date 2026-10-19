/* Drives the real player_update() from src/player.c to verify that remote
 * player orientation smoothing settles at a rate independent of the call rate.
 *
 * Ported from zerospades/zerospades PR #56.
 *
 * Build (needs the project's include paths; sysinc supplies GLES/SDL/enet/...):
 *   gcc -std=gnu99 -I src -I deps -I deps/libvxl -I <sysinc> <defines> \
 *       tests/orientation_smoothing_test.c src/player.c <stubs.c> -lm \
 *       -o /tmp/smoothing_test
 */
#include <math.h>
#include <stdio.h>

#include "config.h"
#include "player.h"

/* Symbols player.c imports from other translation units. Only these two are
 * actually reached by player_update(dt, 0); everything else is left to the
 * generated weak stubs. local_player_id is tentatively defined by player.c. */
struct RENDER_OPTIONS settings;

float network_correction_rate(void) {
	return 12.0F;
}

static float angle_between(struct Orientation a, struct Orientation b) {
	float d = a.x * b.x + a.y * b.y + a.z * b.z;
	if(d > 1.0F) d = 1.0F;
	if(d < -1.0F) d = -1.0F;
	return acosf(d) * 180.0F / (float)M_PI;
}

/* Relax the real filter over `seconds` of wall clock, in `steps` equal calls. */
static float settle_degrees(int steps, float seconds, struct Orientation start,
                            struct Orientation target) {
	player_reset(&players[0]);
	players[0].connected = 1;
	players[0].alive = 1;
	players[0].has_network_orientation = 1; /* pretend the server already spoke */
	players[0].orientation = target;
	players[0].orientation_smooth = start;
	players[0].net_offset.x = players[0].net_offset.y = players[0].net_offset.z = 0.0F;

	float dt = seconds / (float)steps;
	for(int i = 0; i < steps; i++)
		player_update(dt, 0);

	return angle_between(players[0].orientation_smooth, target);
}

/* The filter this replaces, reproduced here only to characterise how bad the
 * framerate dependence was. This is the old src/player.c code, not a stand-in
 * for the new one -- the new one is exercised through player_update() above. */
static float old_settle_degrees(int steps, float seconds, struct Orientation start,
                                struct Orientation target) {
	struct Orientation s = start;
	float dt = seconds / (float)steps;
	for(int i = 0; i < steps; i++) {
		float d = powf(0.9F, dt * 60.0F), g = powf(0.1F, dt * 60.0F);
		struct Orientation n = {s.x * d + target.x * g, s.y * d + target.y * g,
		                        s.z * d + target.z * g};
		float l = sqrtf(n.x * n.x + n.y * n.y + n.z * n.z);
		s.x = n.x / l; s.y = n.y / l; s.z = n.z / l;
	}
	return angle_between(s, target);
}

static int failures = 0;

static void check(const char* what, int ok) {
	printf("  %-52s %s\n", what, ok ? "ok" : "FAIL");
	if(!ok)
		failures++;
}

static void report(const char* what, float got, float want, float tol) {
	int ok = fabsf(got - want) <= tol;
	printf("  %-52s got %9.4f  want %9.4f  %s\n", what, got, want, ok ? "ok" : "FAIL");
	if(!ok)
		failures++;
}

int main(void) {
	struct Orientation start = {1.0F, 0.0F, 0.0F};
	struct Orientation target = {0.0F, 1.0F, 0.0F}; /* 90 degrees away */

	/* player.c tentatively defines local_player_id as 0, and the smoothing
	 * branch skips the local player. Park it in the sentinel slot the engine
	 * itself uses for "no local player" so slot 0 counts as remote. */
	local_player_id = PLAYERS_MAX - 1;

	const float H = 0.05F; /* horizon; 1 s is ~6 time constants, all settled */

	printf("same wall clock (%.3f s), different call rates\n", H);
	float lo = 1e9F, hi = -1e9F, old_lo = 1e9F, old_hi = -1e9F;
	static const int rates[] = {60, 120, 144, 240, 360, 1000};
	for(unsigned r = 0; r < sizeof(rates) / sizeof(rates[0]); r++) {
		int fps = rates[r];
		int steps = (int)(H * fps);
		if(steps < 1) steps = 1;
		float n = settle_degrees(steps, H, start, target);
		float o = old_settle_degrees(steps, H, start, target);
		printf("  %5d fps:  new %9.4f deg      old %9.4f deg\n", fps, n, o);
		if(n < lo) lo = n;
		if(n > hi) hi = n;
		if(o < old_lo) old_lo = o;
		if(o > old_hi) old_hi = o;
	}
	printf("  new spread %.4f deg   |   old spread %.4f deg\n", hi - lo, old_hi - old_lo);

	/* The new filter still nlerps rather than slerps, so composition is
	 * approximate and a small residual spread is expected -- PR #56 calls
	 * this out explicitly ("a true fix would slerp. The residual is well
	 * below anything visible"). Measured spread is ~0.75 deg across
	 * 60..1000 fps; assert it stays under a degree and vastly better than
	 * the ~71 deg the old weights produced. */
	check("new filter spread < 1.0 deg across 60..1000 fps", hi - lo < 1.0F);
	check("old filter spread was gross (> 30 deg)", old_hi - old_lo > 30.0F);
	check("new spread is <1/20th of the old spread", (hi - lo) * 20.0F < (old_hi - old_lo));

	printf("\n60 fps tuning is preserved\n");
	/* tau = (1/60) / -ln(0.9) was chosen so that a 1/60 s step keeps 0.9 of
	 * the error, i.e. exactly the old weights. The constant is the rounded
	 * 0.158F rather than the exact 0.158178F so that this client smooths
	 * identically to ZeroSpades, which matters because the result feeds hit
	 * detection -- so allow the small discrepancy that rounding introduces. */
	{
		float n = settle_degrees((int)(60.0F * H), H, start, target);
		float o = old_settle_degrees((int)(60.0F * H), H, start, target);
		printf("  new %9.4f deg   old %9.4f deg\n", n, o);
		report("new matches old at 60 fps", n, o, 0.05F);
	}

	printf("\nguards\n");
	/* dt == 0 must be a no-op. The old form did a 50% blend here, since
	 * powf(x, 0) is 1 for both weights. */
	report("dt == 0 leaves the orientation untouched",
	       settle_degrees(1, 0.0F, start, target), 90.0F, 0.001F);

	/* Near-antipodal orientations snap instead of nlerping through the origin. */
	report("antipodal orientation snaps to the target",
	       settle_degrees(1, 1.0F / 60.0F, (struct Orientation){-1.0F, 0.0F, 0.0F},
	                      start),
	       0.0F, 0.001F);

	/* The first reported orientation is adopted, not smoothed towards. */
	{
		player_reset(&players[1]);
		players[1].connected = 1;
		players[1].orientation = players[1].orientation_smooth
			= (struct Orientation){1.0F, 0.0F, 0.0F}; /* team default spawn facing */
		check("player_reset clears has_network_orientation",
		      players[1].has_network_orientation == 0);

		player_set_orientation(1, 0.0F, 1.0F, 0.0F);
		report("first reported orientation is adopted",
		       angle_between(players[1].orientation_smooth,
		                     (struct Orientation){0.0F, 1.0F, 0.0F}),
		       0.0F, 0.001F);

		player_set_orientation(1, 1.0F, 0.0F, 0.0F);
		report("later reported orientations are smoothed, not snapped",
		       angle_between(players[1].orientation_smooth,
		                     (struct Orientation){1.0F, 0.0F, 0.0F}),
		       90.0F, 0.001F);
	}

	/* Out-of-range ids must not write through the players array. */
	{
		struct Orientation before = players[0].orientation;
		player_set_orientation(-1, 5.0F, 5.0F, 5.0F);
		player_set_orientation(PLAYERS_MAX, 5.0F, 5.0F, 5.0F);
		check("player_set_orientation rejects out-of-range ids",
		      players[0].orientation.x == before.x && players[0].orientation.y == before.y
			      && players[0].orientation.z == before.z);
	}

	printf("\n%s\n", failures ? "FAILURES" : "all checks passed");
	return failures != 0;
}
