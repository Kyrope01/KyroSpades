"""Source checks for the draft AoS Player Outfit extension integration.

Run with: python3 tests/player_outfit_extension_test.py
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
network = (root / "src/network.c").read_text()
network_header = (root / "src/network.h").read_text()
player = (root / "src/player.c").read_text()
player_header = (root / "src/player.h").read_text()
models = (root / "resources/kv6/zombie/README.md").read_text()

assert "EXT_PLAYER_OUTFIT = 0x35" in network_header
assert "PACKET_EXT_BASE + EXT_PLAYER_OUTFIT" in network
assert "supports_player_outfit" in network
assert "player_outfit_ext_set_negotiated(supports_player_outfit)" in network
# The Set Outfit payload carries a Colour byte (extension PR #14 head); a
# server built before it sends one byte fewer, which means the player's own.
assert "player_outfit_ext_set_player(packet[1], packet[2], len == 4 ? packet[3] : 255)" in network
assert "if(len != 4 && len != 3)" in network
assert "player_outfit_ext_set_teams(packet + 1, len - 1)" in network
assert "player_outfit_ext_reset_map();" in network
assert "player_outfit_ext_reset_player(p->player_id);" in network
assert network.count("player_outfit_ext_apply_player(p->player_id);") == 2
assert "player_outfit_overrides[player_id] = 255" in player
assert "player_team_outfits_received" in player
assert "return outfit == 1 ? 1 : 0;" in player
assert "unsigned char player_outfit_ext_colour(unsigned char player_id)" in player
assert "if(team > 1) /* only teams 0 and 1 exist; the rest are ignored */" in player
# Body parts wear the outfit's colour; tools and weapons keep the team's.
# The helper also takes the model, because HUD previews (team selector) have no
# player slot and must use their own team instead of indexing players[].
assert player.count("player_model_team(p, id)") == 6
assert "static unsigned char player_model_team(const struct Player* p, int id)" in player
assert "if(id < 0 || id >= PLAYERS_MAX)\n\t\treturn p->team;" in player
assert "kv6_render(torso, p->team);" not in player
assert "bool player_uses_undead_outfit(const struct Player* p)" in player
assert "unsigned char outfit;" in player_header
assert "outfit `1` (`Undead`)" in models

print("Player Outfit extension (Colour byte) and Undead model checks passed")
