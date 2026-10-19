"""Exercise the remaining Teamplay packet dispatch without ENet/GL headers.

Run: python3 tests/teamplay_network_dispatch_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'src/network.c').read_text()
handler = source[source.index('static void read_PacketTeamplay(void* data, int len) {'):
                 source.index('void network_init()', source.index('static void read_PacketTeamplay(void* data, int len) {'))]
stubs = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
static int network_map_transfer, network_map_transfer_end, network_logged_in, seeking, playing, calls;
static uint8_t subtype;
static size_t length;
static bool loaded, seeked;
static bool demo_is_seeking(void) { return seeking; }
static bool demo_is_playing(void) { return playing; }
static bool teamplay_receive(const uint8_t* data, size_t len, bool seek, bool loading) {
    calls++; subtype = data[0]; length = len; seeked = seek; loaded = loading;
    return true;
}
'''
checks = r'''
int main(void) {
    uint8_t packet[] = {0, 2, 0, 0};
    read_PacketTeamplay(packet, 0);
    read_PacketTeamplay(NULL, 4);
    assert(calls == 0);
    network_map_transfer = 1;
    read_PacketTeamplay(packet, 4);
    assert(calls == 1 && subtype == 0 && length == 4 && loaded && !seeked);
    packet[0] = 2; network_map_transfer = 0; network_logged_in = 0;
    read_PacketTeamplay(packet, 4);
    assert(calls == 2 && subtype == 2 && loaded && !seeked); /* pre-map mark */
    network_logged_in = 1; seeking = 1; playing = 1;
    read_PacketTeamplay(packet, 4);
    assert(calls == 3 && subtype == 2 && !loaded && seeked); /* seek state */
    seeking = 0; network_map_transfer = 1; network_map_transfer_end = 0;
    read_PacketTeamplay(packet, 4);
    assert(calls == 4 && loaded); /* demo mark before StateData */
    network_map_transfer_end = 1;
    read_PacketTeamplay(packet, 4);
    assert(calls == 5 && !loaded); /* after StateData it ages immediately */
    playing = 0;
    read_PacketTeamplay(packet, 4);
    assert(calls == 6 && loaded); /* live map still loading */
    puts("Teamplay ping/mark dispatch tests passed");
    return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
    c = Path(d) / 'network.c'
    exe = Path(d) / 'network'
    c.write_text(stubs + handler + checks)
    subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(c), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# A mark is server-authored; never expose a local ESP-mark sender or an
# unnegotiated shortcut. The old spectator ESP remains independent.
assert 'settings.esp_in_spec' in (root / 'src/player.c').read_text()
assert 'teamplay_can_overlay()' in (root / 'src/teamplay.c').read_text()
assert 'teamplay_marks[k]' in (root / 'src/player.c').read_text()
assert 'teamplay_apply_pending();' in (root / 'src/main.c').read_text()
state_handler = source[source.index('void read_PacketStateData(void* data, int len) {'):
                       source.index('void read_PacketFogColor(', source.index('void read_PacketStateData(void* data, int len) {'))]
assert 'if(demo_is_playing() && !demo_is_seeking()) teamplay_apply_pending();' in state_handler
assert 'teamplay_send_mark' not in (root / 'src/teamplay.c').read_text()
