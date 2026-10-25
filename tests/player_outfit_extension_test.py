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
assert "player_outfit_ext_set_player(packet[1], packet[2])" in network
assert "player_outfit_ext_set_teams(packet + 1, len - 1)" in network
assert "player_outfit_ext_reset_map();" in network
assert "player_outfit_ext_reset_player(p->player_id);" in network
assert network.count("player_outfit_ext_apply_player(p->player_id);") == 2
assert "player_outfit_overrides[player_id] = 255" in player
assert "player_team_outfits_received" in player
assert "return outfit == 1 ? 1 : 0;" in player
assert "bool player_uses_undead_outfit(const struct Player* p)" in player
assert "unsigned char outfit;" in player_header
assert "outfit `1` (`Undead`)" in models

print("Draft Player Outfit extension and Undead model checks passed")
