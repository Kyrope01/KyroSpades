"""Compile the scoreboard layout helper and verify HUD scroll/row integration."""
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "src/scoreboard.h"
HUD = (ROOT / "src/hud.c").read_text()
PLAYER = (ROOT / "src/player.h").read_text()
NETWORK = (ROOT / "src/network.c").read_text()
SCOREBOARD = HUD.split("int scoreboard_open = scoreboard_is_open();", 1)[1].split(
    "int is_local =", 1
)[0]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "scoreboard.h"

static void assert_fits(struct scoreboard_layout layout, float top, float bottom,
                        int spectator_count) {
        float lowest = top - layout.team_height;
        if(spectator_count > 0)
                lowest = top - layout.spectator_offset - layout.spectator_height;
        assert(lowest >= bottom - 0.001F);
}

int main(void) {
        /* A full 255-player team must become a bounded, scrollable list. */
        assert(SCOREBOARD_ROW_HEIGHT == 24.0F * 0.8F);
        assert(SCOREBOARD_BORDER_WIDTH == 1.5F);
        assert(scoreboard_center_offset(SCOREBOARD_HEADER_HEIGHT, SCOREBOARD_TEXT_HEIGHT) == 4.0F);
        assert(scoreboard_center_offset(SCOREBOARD_ROW_HEIGHT, SCOREBOARD_TEXT_HEIGHT) > 1.59F);
        assert(scoreboard_center_offset(SCOREBOARD_ROW_HEIGHT, SCOREBOARD_TEXT_HEIGHT) < 1.61F);
        struct scoreboard_layout full = scoreboard_layout_calculate(255, 0, 0, 450.0F, 24.0F);
        assert(full.team_rows == 20);
        assert(full.team_rows < 255);
        assert(full.team_height == SCOREBOARD_HEADER_HEIGHT + 20 * SCOREBOARD_ROW_HEIGHT);
        assert(full.spectator_height == 0);
        assert_fits(full, 450.0F, 24.0F, 0);
        assert(scoreboard_scroll_limit(255, full.team_rows) == 235);
        assert(scoreboard_scroll_start(999, 255, full.team_rows) == 235);
        assert(scoreboard_scroll_start(-10, 255, full.team_rows) == 0);
        assert(scoreboard_scroll_start(17, 255, full.team_rows) == 17);

        /* Large teams and spectators share the available height and both stay
           reachable. The section gap and all headers are included in the budget. */
        struct scoreboard_layout mixed = scoreboard_layout_calculate(160, 40, 55, 450.0F, 24.0F);
        assert(mixed.team_rows == 12);
        assert(mixed.spectator_rows == 6);
        assert(mixed.team_rows + mixed.spectator_rows == 18);
        assert_fits(mixed, 450.0F, 24.0F, 55);
        assert(scoreboard_scroll_limit(160, mixed.team_rows) == 148);
        assert(scoreboard_scroll_limit(55, mixed.spectator_rows) == 49);

        /* Unused team space is returned to spectators instead of leaving blank
           rows while spectator names remain inaccessible. */
        struct scoreboard_layout small_teams = scoreboard_layout_calculate(2, 1, 100, 450.0F, 24.0F);
        assert(small_teams.team_rows == 2);
        assert(small_teams.spectator_rows == 16);
        assert_fits(small_teams, 450.0F, 24.0F, 100);

        struct scoreboard_layout spectators = scoreboard_layout_calculate(0, 0, 255, 450.0F, 24.0F);
        assert(spectators.team_rows == 0);
        assert(spectators.spectator_rows == 18);
        assert_fits(spectators, 450.0F, 24.0F, 255);

        /* High-DPI/large windows gain rows without running off-screen when
           all 255 real players are split across the two teams. */
        struct scoreboard_layout large = scoreboard_layout_calculate(128, 127, 0, 810.0F, 43.2F);
        assert(large.team_rows == 38);
        assert_fits(large, 810.0F, 43.2F, 0);
        assert(scoreboard_scroll_limit(128, large.team_rows) == 90);

        struct scoreboard_layout compact = scoreboard_layout_calculate(40, 30, 20, 270.0F, 14.4F);
        assert(compact.team_rows > 0);
        assert(compact.spectator_rows > 0);
        assert_fits(compact, 270.0F, 14.4F, 20);

        struct scoreboard_layout empty = scoreboard_layout_calculate(0, 0, 0, 450.0F, 24.0F);
        assert(empty.team_rows == 0 && empty.spectator_rows == 0);
        assert(scoreboard_scroll_limit(0, 0) == 0);
        assert(scoreboard_scroll_start(50, 0, 0) == 0);

        puts("Scoreboard 255-player layout and scroll bounds passed");
        return 0;
}
'''

with tempfile.TemporaryDirectory(prefix="ks-scoreboard-") as tmp:
    source = Path(tmp) / "scoreboard_test.c"
    binary = Path(tmp) / "scoreboard_test"
    source.write_text(HARNESS)
    subprocess.run(
        ["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", f"-I{ROOT / 'src'}",
         str(source), "-o", str(binary)],
        check=True,
    )
    subprocess.run([str(binary)], check=True)

assert '#include "scoreboard.h"' in HUD
assert "scoreboard_layout_calculate(" in HUD
assert "scoreboard_scroll_start(" in HUD
assert "scoreboard_scroll_limit(" in HUD
assert SCOREBOARD.count("for(int k = 0; k < PLAYER_COUNT_MAX; k++)") == 2
assert "struct player_table pt[PLAYER_COUNT_MAX];" in SCOREBOARD
assert "for(int k = 0; k < PLAYERS_MAX; k++)" not in SCOREBOARD
assert "row < section_start[section]" in HUD
assert "row >= section_start[section] + section_visible[section]" in HUD
assert "((section_start[i] + row) & 1) ? 0.45F : 0.75F" in HUD
assert "row_top - scoreboard_center_offset(" in HUD
assert "SCOREBOARD_ROW_HEIGHT, SCOREBOARD_TEXT_HEIGHT" in HUD
assert "font_render(x_offset + 8.0F, row_text_y," in HUD
assert "font_centered(x_offset + 150.0F, row_text_y," in HUD
assert "- font_length(SCOREBOARD_TEXT_HEIGHT, id_str) - 8.0F," in HUD
assert 'snprintf(header_str, sizeof(header_str), "%s (%s)", team.name, score_str);' in HUD
assert "font_centered(x_offset + 150.0F, header_text_y," in HUD
assert "row_top - scoreboard_center_offset(SCOREBOARD_ROW_HEIGHT, 18.0F)" in HUD
assert "panel_top - SCOREBOARD_HEADER_HEIGHT" in HUD
assert "scoreboard_scroll_accum -= yoffset * 3.0F;" in HUD
assert "return; /* Do not switch weapons while inspecting the scoreboard. */" in HUD
assert HUD.index("static int scoreboard_is_open(void)") < HUD.index("static void hud_ingame_scroll")
assert HUD.index("if(scoreboard_is_open())", HUD.index("static void hud_ingame_scroll")) < HUD.index(
    "if(camera_mode == CAMERAMODE_FPS && yoffset != 0.0F)", HUD.index("static void hud_ingame_scroll")
)
assert "thumb_height" in HUD and "thumb_top" in HUD
assert "#define SCOREBOARD_BORDER_WIDTH 1.5F" in HEADER.read_text()
assert "static void scoreboard_draw_box_outline" in HUD
assert "static void scoreboard_draw_solid_rect" in HUD
solid_rect = HUD.split("static void scoreboard_draw_solid_rect", 1)[1].split(
    "static void scoreboard_draw_box_outline", 1
)[0]
assert "if(width <= 0.0F || height <= 0.0F)" in solid_rect
assert "glDisable(GL_TEXTURE_2D);" in solid_rect
assert "600.0F, layout.team_height);" in HUD
assert HUD.count("300.0F, SCOREBOARD_HEADER_HEIGHT);") >= 3
assert "settings.window_width / 2.0F - SCOREBOARD_BORDER_WIDTH * 0.5F" in HUD
assert "if(spectator_panel_drawn)" in HUD
assert "if(i == 2 && count_spec == 0)" in HUD
row_backgrounds = HUD.split("for(int row = 0; row < section_visible[i]; row++)", 1)[1].split(
    "glDisable(GL_BLEND);", 1
)[0]
assert "scoreboard_draw_box_outline" not in row_backgrounds

# ID 255 remains allocated for server actions, but is not a real player.
assert "#define PLAYER_ID_SERVER 255" in PLAYER
assert "#define PLAYER_COUNT_MAX PLAYER_ID_SERVER" in PLAYER
assert "#define PLAYERS_MAX (PLAYER_ID_SERVER + 1)" in PLAYER
assert "p->player_id == PLAYER_ID_SERVER" in NETWORK
existing_player = NETWORK.split("void read_PacketExistingPlayer", 1)[1].split(
    "void read_PacketCreatePlayer", 1
)[0]
create_player = NETWORK.split("void read_PacketCreatePlayer", 1)[1].split(
    "void read_PacketPlayerLeft", 1
)[0]
assert "if(p->player_id < PLAYER_COUNT_MAX)" in existing_player
assert "if(p->player_id < PLAYER_COUNT_MAX)" in create_player

print("Scoreboard HUD alignment, 255-player limit and server-ID handling verified")
