#ifndef SCOREBOARD_H
#define SCOREBOARD_H

/* Pure scoreboard layout/scroll arithmetic, shared by the HUD renderer and
   regression tests. Counts are real player counts; headers are added by the
   layout rather than being hidden in the team counts. */
#define SCOREBOARD_HEADER_HEIGHT 24.0F
#define SCOREBOARD_ROW_HEIGHT 19.2F /* 80% of the original 24 px player rows */
#define SCOREBOARD_TEXT_HEIGHT 16.0F
#define SCOREBOARD_SECTION_GAP 32.0F
#define SCOREBOARD_BORDER_WIDTH 1.5F
#define SCOREBOARD_INTEL_RESERVE 24.0F

struct scoreboard_layout {
        int team_rows;
        int spectator_rows;
        float team_height;
        float spectator_offset;
        float spectator_height;
};

static int scoreboard_min_int(int a, int b) {
        return a < b ? a : b;
}

static int scoreboard_max_int(int a, int b) {
        return a > b ? a : b;
}

static float scoreboard_center_offset(float container, float content) {
        return (container - content) * 0.5F;
}

static int scoreboard_scroll_limit(int count, int visible_rows) {
        if(count < 0) count = 0;
        if(visible_rows < 0) visible_rows = 0;
        return scoreboard_max_int(0, count - visible_rows);
}

static int scoreboard_scroll_start(int offset, int count, int visible_rows) {
        int limit = scoreboard_scroll_limit(count, visible_rows);
        if(offset < 0) return 0;
        return scoreboard_min_int(offset, limit);
}

static struct scoreboard_layout scoreboard_layout_calculate(int team1_count, int team2_count,
                                                            int spectator_count, float top, float bottom) {
        struct scoreboard_layout layout = {0};
        if(team1_count < 0) team1_count = 0;
        if(team2_count < 0) team2_count = 0;
        if(spectator_count < 0) spectator_count = 0;

        int team_total = scoreboard_max_int(team1_count, team2_count);
        float available = top - bottom - SCOREBOARD_HEADER_HEIGHT;
        if(spectator_count > 0)
                available -= SCOREBOARD_SECTION_GAP + SCOREBOARD_HEADER_HEIGHT;
        int capacity = available > 0.0F ? (int)(available / SCOREBOARD_ROW_HEIGHT) : 0;

        if(spectator_count == 0) {
                layout.team_rows = scoreboard_min_int(team_total, capacity);
        } else if(team_total == 0) {
                layout.spectator_rows = scoreboard_min_int(spectator_count, capacity);
        } else if(capacity <= 1) {
                /* Extremely short windows cannot show both list types. Keep the
                   primary team list usable rather than drawing off-screen. */
                layout.team_rows = scoreboard_min_int(team_total, capacity);
        } else {
                /* Reserve roughly a third of the rows for spectators, but always
                   leave at least one team row. Unused space is returned to whichever
                   list can use it, so small teams do not waste half the scoreboard. */
                layout.spectator_rows = scoreboard_min_int(
                        spectator_count, scoreboard_max_int(1, capacity / 3));
                layout.spectator_rows = scoreboard_min_int(layout.spectator_rows, capacity - 1);
                layout.team_rows = scoreboard_min_int(
                        team_total, capacity - layout.spectator_rows);

                int remaining = capacity - layout.team_rows - layout.spectator_rows;
                int extra = scoreboard_min_int(team_total - layout.team_rows, remaining);
                layout.team_rows += extra;
                remaining -= extra;
                extra = scoreboard_min_int(spectator_count - layout.spectator_rows, remaining);
                layout.spectator_rows += extra;
        }

        layout.team_height = SCOREBOARD_HEADER_HEIGHT
                + SCOREBOARD_ROW_HEIGHT * layout.team_rows;
        layout.spectator_offset = layout.team_height + SCOREBOARD_SECTION_GAP;
        layout.spectator_height = spectator_count > 0
                ? SCOREBOARD_HEADER_HEIGHT + SCOREBOARD_ROW_HEIGHT * layout.spectator_rows : 0.0F;
        return layout;
}

#endif /* SCOREBOARD_H */
