"""Source-level wiring checks for the Flashlight V1 renderer and packet lifecycle."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
network = (root / 'src/network.c').read_text()
hud = (root / 'src/hud.c').read_text()
lighting = (root / 'src/lighting.c').read_text()
main = (root / 'src/main.c').read_text()
assert 'packets[FLASHLIGHT_PACKET_ID] = read_PacketFlashlight;' in network
assert 'reply.entries[4 + (supports_teamplay ? 1 : 0)]' in network
assert 'flashlight_ext_reset_player(p->player_id, false);' in network
assert 'flashlight_ext_reset_player(p->player_id, true);' in network
assert network.index('flashlight_ext_reset_map();', network.index('void read_PacketMapStart(')) < network.index('if(demo_is_seeking()) return;', network.index('void read_PacketMapStart('))
assert 'flashlight_ext_request(local_player_id, request)' in hud
assert 'network_send(FLASHLIGHT_PACKET_ID, request, sizeof(request))' in hud
assert 'lighting_flashlight_set_server_state(flashlight_ext_on(local_player_id))' in main
assert 'flashlight_ext_negotiated() ? !flashlight_ext_on(i)' in lighting
assert 'if(!beam.reach || !beam.cone) continue;' in lighting
for name in ('lighting.c', 'glx.c', 'water.c'):
    text = (root / 'src' / name).read_text()
    assert 'uniform vec4 u_SpotColorCone[4];' in text, name
    assert 'u_SpotColorCone[3].rgb' in text, name
    assert 'u_SpotColorCone[i].rgb' in text, name
assert 'glUniform4fv(location, LIGHTING_SHADER_LIGHTS, spot_color_cone)' in lighting
assert 'glUniform4fv(uniform_spot_color_cone, LIGHTING_SHADER_LIGHTS, spot_color_cone)' in lighting
print('Flashlight packet lifecycle and render uniform wiring checks passed')
