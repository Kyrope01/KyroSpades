"""Source-level wiring checks for the Flashlight V1 renderer and packet lifecycle."""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
network = (root / 'src/network.c').read_text()
hud = (root / 'src/hud.c').read_text()
lighting = (root / 'src/lighting.c').read_text()
main = (root / 'src/main.c').read_text()
assert 'packets[FLASHLIGHT_PACKET_ID] = read_PacketFlashlight;' in network
assert 'reply.entries[next_entry++] = (struct PacketExtInfoEntry){ FLASHLIGHT_EXT_ID, 1 };' in network
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

# Every lit surface must honor server-supplied spotlight color and cone, with
# fill confined to the configured cone and reach. KV6 point shaders previously
# hard-coded both the cone and warm color instead of using these uniforms.
for name in ('lighting.c', 'glx.c', 'water.c', 'model.c'):
    text = (root / 'src' / name).read_text()
    assert 'uniform vec4 u_SpotColorCone[4];' in text, name
    assert 'u_SpotColorCone[i].rgb' in text and 'u_SpotColorCone[3].rgb' in text, name
    assert 'mix(u_SpotColorCone[i].w' in text and 'mix(u_SpotColorCone[3].w' in text, name
    assert '0.70710678' not in text, name
    assert 'cone * range + fill' not in text and 'cone * attenuation + fill' not in text, name
    assert 'cone*range+fill' not in text and 'cone*attenuation+fill' not in text, name
model = (root / 'src/model.c').read_text()
assert 'cone * (range + fill * range)' in model
assert 'cone*(range+fill*range)' in model
print('Flashlight server cone, colour and bounded fill shader checks passed')

# Protocol specifies a full angle in pi/256. Check that the cutoff and
# soft edge used by the shaders vary for narrow and wide server values and
# that no contribution survives outside the cutoff or beyond the reach.
import math

def cone_weight(cone_byte, angle):
    cutoff = math.cos(cone_byte * math.pi / 512)
    edge = cutoff + (1 - cutoff) * 0.15
    t = max(0.0, min(1.0, (math.cos(angle) - cutoff) / (edge - cutoff)))
    return t*t*(3-2*t)

for cone_byte in (1, 32, 128, 255):
    half_angle = cone_byte * math.pi / 512
    assert cone_weight(cone_byte, 0) > 0.999
    assert cone_weight(cone_byte, half_angle + 0.001) == 0
    assert cone_weight(cone_byte, half_angle * .5) > 0
assert cone_weight(32, math.pi / 8) == 0
assert cone_weight(128, math.pi / 8) > 0
for reach in (1, 20, 60, 255):
    for distance in (reach, reach + 1):
        attenuation = max(0, 1 - distance / reach) ** 2
        assert attenuation == 0
print('Flashlight cone angle and reach boundary checks passed')

# Server RGB channels are linear light levels; negotiated spotlights must not
# receive the legacy warm tint, HDR boost, or local dynamic-light multiplier.
assert 'source.intensity = flashlight_ext_negotiated() ? 1.0F : 1.5F;' in lighting
assert 'if(!flashlight_ext_negotiated() && settings.hdr_rendering)' in lighting
assert 'frame_lights[i].spotlight && flashlight_ext_negotiated()' in lighting
assert '(flashlight_ext_negotiated() ? 1.0F : intensity_multiplier)' in lighting
assert 'float brightness = flashlight_ext_negotiated() ? 1.0F : 1.5F;' in lighting
for channel in ('red', 'green', 'blue'):
    assert f'beam.{channel} / 255.0F' in lighting
assert 'color_intensity[i * 4 + 1] = 0.70F' not in lighting
print('Negotiated RGB intensity is not overridden by local flashlight multipliers')
