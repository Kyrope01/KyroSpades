#include <math.h>
#include <stdio.h>
#include <string.h>

#include "killfeed.h"

/* A player sits at the origin until the server reports a position, so an
   all-zero eye means "no data" rather than "corner of the map" (every eye is
   above the ground, so a real one is never all zero). */
static int eye_valid(float x, float y, float z) {
    return x != 0.0F || y != 0.0F || z != 0.0F;
}

float killfeed_distance(float killer_x, float killer_y, float killer_z,
                        float victim_x, float victim_y, float victim_z) {
    if(!eye_valid(killer_x, killer_y, killer_z) || !eye_valid(victim_x, victim_y, victim_z))
        return -1.0F;
    float dx = killer_x - victim_x;
    float dy = killer_y - victim_y;
    float dz = killer_z - victim_z;
    return sqrtf(dx * dx + dy * dy + dz * dz);
}

char killfeed_text_color(int local_made_kill, int local_was_killed, int team_or_weapon_change) {
    if(team_or_weapon_change) return KILLFEED_COLOR_WHITE;
    if(local_was_killed) return KILLFEED_COLOR_RED;
    if(local_made_kill) return KILLFEED_COLOR_GREEN;
    return KILLFEED_COLOR_WHITE;
}

void killfeed_append_distance(char* line, size_t size, float distance) {
    if(!line || size == 0 || !(distance >= 0.0F)) return;
    char suffix[32];
    snprintf(suffix, sizeof suffix, " from %.1f blocks", (double)distance);
    size_t used = strlen(line);
    if(used + 1 >= size) return;
    strncat(line, suffix, size - 1 - used);
}
