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
struct Player { unsigned char team, outfit, connected; struct { float x, y, z; } pos; };
struct GameState gamestate;
struct Player players[PLAYERS_MAX];
/* The model light: recorded so the preview path can be checked. */
static int light_calls;
static int light[3];
static void kv6_calclight(int x, int y, int z) {
    light_calls++;
    light[0] = x; light[1] = y; light[2] = z;
}
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
    player_outfit_ext_set_player(0, 0, 255);
    assert(players[0].outfit == 0);
    player_outfit_ext_set_player(1, 1, 255);
    players[1].connected = 1;
    players[1].team = TEAM_2;
    player_outfit_ext_apply_player(1);
    assert(players[1].outfit == 1);

    /* Team defaults, reserved IDs, and explicit team-inherit sentinel. */
    /* Teams above 1 do not exist and are ignored, as the extension says. */
    const unsigned char team_outfits[] = { TEAM_1, 0, TEAM_2, 1, 3, 1 };
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
    player_outfit_ext_set_player(3, 255, 255);
    assert(players[3].outfit == 1);
    player_outfit_ext_set_player(3, 200, 255); /* no local art: Soldier fallback */
    assert(players[3].outfit == 0);

    /* The Colour byte names the team whose colour the outfit wears; 255, or a
       value that is not a team, leaves the player in their own team's. */
    players[5].connected = 1;
    players[5].team = TEAM_2;
    player_outfit_ext_set_player(5, 1, TEAM_1);
    player_outfit_ext_apply_player(5);
    assert(players[5].outfit == 1 && player_outfit_ext_colour(5) == TEAM_1);
    assert(player_model_team(&players[5], 5) == TEAM_1);
    player_outfit_ext_set_player(5, 1, 255);
    assert(player_outfit_ext_colour(5) == TEAM_2);
    player_outfit_ext_set_player(5, 1, 200); /* not a team: keep the player's own */
    assert(player_outfit_ext_colour(5) == TEAM_2);

    /* Team outfits last for the connection; map start resets per-player IDs. */
    player_outfit_ext_reset_map();
    assert(player_outfit_ext_colour(5) == TEAM_2);
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

    /* The team-selector previews are rendered with PLAYERS_MAX as their id: they
       are not a player slot. Looking them up in players[] read past the end of
       the array, which painted both preview models in one team's colour. Their
       own team is the only source that may be used, whatever players[] holds. */
    for (int k = 0; k < PLAYERS_MAX; k++)
        players[k].team = TEAM_2; /* poison every real slot */
    struct Player first, second;
    memset(&first, 0, sizeof first);
    memset(&second, 0, sizeof second);
    first.team = TEAM_1;
    second.team = TEAM_2;
    assert(player_model_team(&first, PLAYERS_MAX) == TEAM_1);
    assert(player_model_team(&second, PLAYERS_MAX) == TEAM_2);
    /* Out of range on either side, never an array read. */
    assert(player_model_team(&first, -1) == TEAM_1);
    assert(player_model_team(&second, PLAYERS_MAX + 64) == TEAM_2);
    for (int k = 0; k < PLAYERS_MAX; k++)
        players[k].team = TEAM_1;
    assert(player_model_team(&first, PLAYERS_MAX) == TEAM_1);
    assert(player_model_team(&second, PLAYERS_MAX) == TEAM_2);

    /* And they are lit as previews: the struct's position is meaningless (hud.c
       leaves it at the origin), so sampling the map there shaded both models
       with the corner's sun occlusion and made their colours unreadable.
       Preview ids take the engine's full-sun form instead. */
    light_calls = 0;
    player_model_light(&first, PLAYERS_MAX);
    assert(light_calls == 1 && light[0] == -1 && light[1] == -1 && light[2] == -1);
    player_model_light(&second, PLAYERS_MAX);
    assert(light[0] == -1 && light[1] == -1 && light[2] == -1);
    /* A world player is still shaded by the map where it actually stands. */
    players[7].pos.x = 12.0F;
    players[7].pos.y = 30.0F;
    players[7].pos.z = 44.0F;
    player_model_light(&players[7], 7);
    assert(light[0] == 12 && light[1] == 30 && light[2] == 44);
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

print("Player Outfit state, team fallback, map lifecycle and team-selector previews passed")
