"""Compile the actual Player Outfit state helpers and exercise their lifecycle."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / "src/player.c").read_text()
start = source.index("bool player_team_is_zombie(unsigned char team) {")
end = source.index("static float os_sprint_state", start)
helpers = source[start:end]

stub = r'''
#include <assert.h>
#include <stdbool.h>
#include <ctype.h>
#include <string.h>
#define PLAYERS_MAX 256
#define TEAM_1 0
#define TEAM_2 1
struct Team { char name[11]; };
struct GameState { struct Team team_1, team_2; };
struct Player { unsigned char team, outfit, connected; };
struct GameState gamestate;
struct Player players[PLAYERS_MAX];
''' + helpers + r'''
int main(void) {
    strcpy(gamestate.team_1.name, "Zombie");
    strcpy(gamestate.team_2.name, "Survivors");
    player_outfit_ext_set_negotiated(false);
    player_outfit_ext_reset_map();

    /* Old servers still get the team-name fallback. */
    players[0].connected = 1;
    players[0].team = TEAM_1;
    player_outfit_ext_apply_player(0);
    assert(players[0].outfit == 1);

    /* Explicit Soldier overrides a legacy Zombie team; Set Outfit can also
       arrive before the player's create/existing-player packet. */
    player_outfit_ext_set_player(0, 0);
    assert(players[0].outfit == 0);
    player_outfit_ext_set_player(1, 1);
    players[1].connected = 1;
    players[1].team = TEAM_2;
    player_outfit_ext_apply_player(1);
    assert(players[1].outfit == 1);

    /* Team defaults, reserved IDs, and explicit team-inherit sentinel. */
    const unsigned char team_outfits[] = { TEAM_1, 0, TEAM_2, 1 };
    player_outfit_ext_set_negotiated(true);
    player_outfit_ext_set_teams(team_outfits, sizeof(team_outfits));
    players[2].connected = 1;
    players[2].team = TEAM_1;
    player_outfit_ext_apply_player(2);
    assert(players[2].outfit == 0);
    players[3].connected = 1;
    players[3].team = TEAM_2;
    player_outfit_ext_apply_player(3);
    assert(players[3].outfit == 1);
    player_outfit_ext_set_player(3, 255);
    assert(players[3].outfit == 1);
    player_outfit_ext_set_player(3, 200); /* no local art: Soldier fallback */
    assert(players[3].outfit == 0);

    /* Team outfits last for the connection; map start resets per-player IDs. */
    player_outfit_ext_reset_map();
    players[3].connected = 1;
    players[3].team = TEAM_2;
    player_outfit_ext_apply_player(3);
    assert(players[3].outfit == 1);

    /* A new connection clears negotiated team defaults and restores legacy
       name detection until the next server sends its Team Outfits packet. */
    player_outfit_ext_set_negotiated(false);
    player_outfit_ext_reset_map();
    players[4].connected = 1;
    players[4].team = TEAM_1;
    player_outfit_ext_apply_player(4);
    assert(players[4].outfit == 1);
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    c_file = Path(directory) / "player_outfit_state.c"
    executable = Path(directory) / "player_outfit_state"
    c_file.write_text(stub)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    str(c_file), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("Player Outfit state, team fallback, and map lifecycle passed")
