"""Checks for the killfeed's text colours.

Player names keep their team colours, and the words around them are white -
green when the local player made the kill, red when the local player was the one
killed. Team and weapon changes are not kills and stay white.

The chat renderer's palette is parsed out of hud.c and compared against the
codes src/killfeed.c returns, so the two cannot drift apart silently.

Run: python3 tests/killfeed_color_test.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

hud = (root / "src/hud.c").read_text()
network = (root / "src/network.c").read_text()

# The codes this feature uses must be the ones the renderer paints.
palette = {}
for code, rgb in re.findall(r"case '\\([0-9])': glColor3ub\(([^)]*)\); break;", hud):
    palette[int(code)] = tuple(int(part.strip()) for part in rgb.split(","))

assert palette[4] == (255, 0, 0), palette.get(4)
assert palette[5] == (0, 255, 0), palette.get(5)
assert palette[6] == (255, 255, 255), palette.get(6)

# Names keep their team colours; the words take the computed colour instead of
# the fixed red the killfeed used to paint everything.
assert "char color_killer = team_color_char(players[p->killer_id].team);" in network
assert "char color_dead   = team_color_char(players[p->player_id].team);" in network
assert "char color_kill   = '\\4';" not in network
assert ("char color_kill = killfeed_text_color(local_made_kill, local_was_killed, "
        "team_or_weapon_change);") in network
assert ("int local_made_kill = p->killer_id == local_player_id "
        "&& p->player_id != local_player_id;") in network
assert "int local_was_killed = p->player_id == local_player_id;" in network
assert ("int team_or_weapon_change = p->kill_type == KILLTYPE_TEAMCHANGE\n"
        "                        || p->kill_type == KILLTYPE_CLASSCHANGE;") in network

# The entry's side bar follows the same rule, and a killer on a team with no
# gamestate colour (zombie or another custom team) still gets a killfeed line.
assert "if(local_made_kill) {\n                        chat_add(1, rgb(0, 255, 0), m);" in network
assert "} else if(local_was_killed) {\n                        chat_add(1, rgb(255, 0, 0), m);" in network
assert "chat_add(1, 0x0000FF, m);" not in network
assert re.search(r"default:\n(?:.*\n)*?\s*chat_add\(1, rgb\(255, 255, 255\), m\);", network)

PROGRAM = r"""
#include <stdio.h>

#include "killfeed.h"

static int failures;

static void expect_color(const char* label, char got, char want) {
    if(got != want) {
        printf("FAIL %s: got \\%d want \\%d\n", label, (int)got, (int)want);
        failures++;
    } else {
        printf("ok %s\n", label);
    }
}

int main(void) {
    /* Ranked: green when the local player got the kill, red when they died,
       white for a kill between two other players. */
    expect_color("local kill", killfeed_text_color(1, 0, 0), KILLFEED_COLOR_GREEN);
    expect_color("local death", killfeed_text_color(0, 1, 0), KILLFEED_COLOR_RED);
    expect_color("someone else", killfeed_text_color(0, 0, 0), KILLFEED_COLOR_WHITE);
    /* A fall or a suicide is still the local player's death. */
    expect_color("local suicide", killfeed_text_color(0, 1, 0), KILLFEED_COLOR_RED);
    /* Team and weapon changes send kill packets but are not kills. */
    expect_color("team change", killfeed_text_color(0, 1, 1), KILLFEED_COLOR_WHITE);
    expect_color("weapon change", killfeed_text_color(0, 0, 1), KILLFEED_COLOR_WHITE);

    return failures ? 1 : 0;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    workspace = Path(tmp)
    source = workspace / "killfeed_color_test.c"
    source.write_text(PROGRAM)
    binary = workspace / "killfeed_color_test"
    build = subprocess.run(
        ["cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
         "-I", str(root / "src"), str(source), str(root / "src/killfeed.c"),
         "-lm", "-o", str(binary)],
        capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    assert build.stderr == "", build.stderr
    run = subprocess.run([str(binary)], capture_output=True, text=True)
    assert run.returncode == 0, run.stdout + run.stderr

print("killfeed colors: ok")
