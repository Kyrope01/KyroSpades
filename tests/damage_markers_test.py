"""Checks for the Damage Markers extension (aosprotocol 0x20, packet 0x60).

The length of the packet picks the width of the Hit Amount, and a negative value
is a heal. The real reader in network.c is put through every documented shape,
including the ones it must drop, and the negotiation and estimate-suppression
wiring is checked where the code is not linkable on its own.

Run: python3 tests/damage_markers_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
network = (root / "src/network.c").read_text()
network_header = (root / "src/network.h").read_text()
numbers = (root / "src/damagenumbers.h").read_text()


def extract(source, start_marker, end_marker):
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


assert "#define PACKET_DAMAGEMARKER_ID 0x60" in network_header
assert "EXT_DAMAGE_MARKERS = 0x20" in network_header
assert "packets[PACKET_DAMAGEMARKER_ID] = read_PacketDamageMarker;" in network

# The client answers the offer and stops estimating what it deals itself.
assert "damagenumbers_set_server_authoritative(supports_damage_markers);" in network
assert "reply.entries[next_entry++] = (struct PacketExtInfoEntry){ EXT_DAMAGE_MARKERS, 1 };" in network
assert network.count("damagenumbers_set_server_authoritative(false);") == 3
assert "void damagenumbers_add_server(int victim_id, int amount);" in numbers

# The estimate is suppressed whenever the server is the authority.
add_body = extract((root / "src/damagenumbers.c").read_text(),
                   "void damagenumbers_add(int victim_id",
                   "/* Damage Markers (aosprotocol extension 0x20): the amount")
assert "if(server_authoritative)" in add_body

reader = extract(network, "void read_PacketDamageMarker(", "void read_PacketBlockAction(")

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int damage_amount, damage_victim, damage_calls;
void damagenumbers_add_server(int victim_id, int amount) {
        damage_victim = victim_id;
        damage_amount = amount;
        damage_calls++;
}
#define log_warn(...) ((void)0)

''' + reader + r'''

int main(void) {
        /* 3 bytes carry a UByte, 4 a signed Short and 6 a signed Int. Collected
           lengths include the packet id, which the dispatcher has stripped. */
        const uint8_t byte_form[] = {7, 45};
        read_PacketDamageMarker((void*)byte_form, sizeof byte_form);
        assert(damage_calls == 1 && damage_victim == 7 && damage_amount == 45);

        const uint8_t big_byte[] = {7, 255};
        read_PacketDamageMarker((void*)big_byte, sizeof big_byte);
        assert(damage_calls == 2 && damage_amount == 255);

        const uint8_t short_form[] = {7, 0xfe, 0xff};   /* -2: a heal */
        read_PacketDamageMarker((void*)short_form, sizeof short_form);
        assert(damage_calls == 3 && damage_amount == -2);

        const uint8_t int_form[] = {200, 0x18, 0xfc, 0xff, 0xff}; /* -1000 */
        read_PacketDamageMarker((void*)int_form, sizeof int_form);
        assert(damage_calls == 4 && damage_victim == 200 && damage_amount == -1000);

        const uint8_t big_int[] = {200, 0xff, 0xff, 0xff, 0x7f}; /* INT32_MAX */
        read_PacketDamageMarker((void*)big_int, sizeof big_int);
        assert(damage_calls == 5 && damage_amount == 2147483647);

        /* Every other length is dropped, as is an empty packet. */
        const uint8_t wrong[] = {7, 1, 2, 3};
        read_PacketDamageMarker((void*)wrong, sizeof wrong);
        read_PacketDamageMarker((void*)wrong, 1);
        read_PacketDamageMarker(NULL, 2);
        assert(damage_calls == 5);

        puts("Damage Markers 3/4/6-byte shapes, heals and dropped lengths passed");
        return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    c_file = Path(directory) / "damage_markers_test.c"
    executable = Path(directory) / "damage_markers_test"
    c_file.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror", str(c_file),
                    "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("Damage Markers tests passed")
