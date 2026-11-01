#ifndef KILLFEED_H
#define KILLFEED_H

#include <stddef.h>

/* Colour codes the chat renderer understands (see the palette in hud.c). */
#define KILLFEED_COLOR_RED '\4'
#define KILLFEED_COLOR_GREEN '\5'
#define KILLFEED_COLOR_WHITE '\6'

/* How far a kill was made from, in blocks. This is the straight line between
   the two eye positions, which is the path the bullet took: unlike a flat
   ground distance it does not shrink a shot from a tower or a rooftop, and it
   is the same measure ZeroSpades prints in its hit log, so the two clients
   agree on the number.

   Returns -1 when either eye is still at the origin, i.e. the client has no
   position for that player yet. */
float killfeed_distance(float killer_x, float killer_y, float killer_z,
                        float victim_x, float victim_y, float victim_z);

/* Appends " from 42.5 blocks" to a kill line, truncating rather than
   overflowing. A negative distance appends nothing. */
void killfeed_append_distance(char* line, size_t size, float distance);

/* Colour code for the words of a kill line. Player names keep their team
   colours; everything else is white, green for a kill the local player made
   and red when the local player was the one killed. Team and weapon changes
   send kill packets without a kill, so they stay white. */
char killfeed_text_color(int local_made_kill, int local_was_killed, int team_or_weapon_change);

#endif
