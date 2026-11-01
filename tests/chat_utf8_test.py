"""Checks for the UTF-8 Chat rule (aosprotocol extensions/utf-8-chat.md).

A Chat Message whose body starts with 0xff is UTF-8; one without the prefix is
Code Page 437. Both directions are exercised through the real charset module and
the real send code, lifted out of network.c so no ENet is needed.

Run: python3 tests/chat_utf8_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
network = (root / "src/network.c").read_text()


def extract(source, start_marker, end_marker):
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


# The real function, so a change to it is exercised here.
send_chat = extract(network, "void network_send_chat(", "unsigned int network_ping(")

# Chat no longer builds its packet by hand at any call site.
for path in ("src/hud.c", "src/pie_menu.c"):
    source = (root / path).read_text()
    assert "network_send(PACKET_CHATMESSAGE_ID" not in source, path
assert (root / "src/pie_menu.c").read_text().count(
    "network_send_chat(global ? CHAT_ALL : CHAT_TEAM, text);") == 1
assert (root / "src/hud.c").read_text().count("network_send_chat(CHAT_ALL, line)") == 1

# The receive path knows the prefix and decodes Code Page 437.
chat_reader = extract(network, "void read_PacketChatMessage(", "/* Damage Markers")
assert "CHARSET_UTF8_PREFIX" in chat_reader
assert "charset_cp437_to_utf8" in chat_reader

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "charset.h"

#define PACKET_CHATMESSAGE_ID 17
unsigned char local_player_id = 3;
int sends;
unsigned char last_packet[300];
int last_length;

struct PacketChatMessage {
        unsigned char player_id;
        unsigned char chat_type;
        char message[255];
};

void network_send(int id, void* data, int len) {
        assert(id == PACKET_CHATMESSAGE_ID);
        memcpy(last_packet, data, (size_t)len);
        last_length = len;
        sends++;
}

''' + send_chat + r'''

static void chat(const char* text, int expect_prefix) {
        sends = 0;
        network_send_chat(0, text);
        assert(sends == 1);
        struct PacketChatMessage* m = (struct PacketChatMessage*)last_packet;
        assert(m->player_id == local_player_id && m->chat_type == 0);
        size_t length = strlen(text);
        assert(last_length == 2 + (int)length + 1 + expect_prefix);
        if(expect_prefix) {
                assert((unsigned char)m->message[0] == 0xff);
                assert(!strcmp(m->message + 1, text));
        } else {
                assert(!strcmp(m->message, text));
        }
        assert(m->message[last_length - 3] == 0); /* NUL terminated on the wire */
}

int main(void) {
        /* Printable ASCII reads the same in both encodings, so it goes bare. */
        chat("hello", 0);
        chat("/pingmark 100.00 200.00 43.00 Enemy", 0);
        chat("", 0);

        /* Anything else carries the 0xff prefix of UTF-8 Chat. */
        chat("caf\xc3\xa9", 1);                 /* UTF-8 e-acute */
        chat("\x01\x02red", 1);                 /* control bytes: colours */
        chat("\xc3\xbc" "ber", 1);

        /* The message field holds the prefix, the body and its terminator. */
        char long_text[300];
        memset(long_text, 'a', sizeof(long_text) - 1);
        long_text[sizeof(long_text) - 1] = 0;
        network_send_chat(0, long_text);
        assert(last_length - 3 == 254);

        memset(long_text, 'a', sizeof(long_text) - 1);
        long_text[0] = (char)0xc3;
        network_send_chat(0, long_text);
        assert(((struct PacketChatMessage*)last_packet)->message[0] == (char)0xff);
        assert(last_length - 4 == 253);

        /* Code Page 437 decodes to UTF-8; ASCII and control bytes pass through
           so the inline colour codes still mean colour. */
        char out[64];
        const uint8_t cp[] = {'h', 'i', 0x82, 0x01, 0x07, 0xe1};
        size_t n = charset_cp437_to_utf8(out, sizeof out, cp, sizeof cp);
        assert(n == 2 + 1 + 1 + 1 + 3);
        assert(!memcmp(out, "hi\xc3\xa9\x01\x07\xc3\x9f", n));
        /* A cut never splits a codepoint. */
        char tight[5];
        n = charset_cp437_to_utf8(tight, sizeof tight, cp, sizeof cp);
        assert(n == 4 && !memcmp(tight, "hi\xc3\xa9", 4));

        /* UTF-8 validation: what the extension calls well formed, and what it
           does not. */
        assert(charset_utf8_well_formed((const uint8_t*)"plain", 5));
        assert(charset_utf8_well_formed((const uint8_t*)"caf\xc3\xa9", 5));
        assert(!charset_utf8_well_formed((const uint8_t*)"\xc0\xaf", 2));   /* overlong */
        assert(!charset_utf8_well_formed((const uint8_t*)"\xed\xa0\x80", 3)); /* surrogate */
        assert(!charset_utf8_well_formed((const uint8_t*)"\xf5\x80\x80\x80", 4)); /* > U+10FFFF */
        assert(!charset_utf8_well_formed((const uint8_t*)"\xe2\x82", 2));   /* truncated */

        puts("UTF-8 Chat prefix rule and Code Page 437 decoding passed");
        return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    c_file = Path(directory) / "chat_utf8_test.c"
    executable = Path(directory) / "chat_utf8_test"
    c_file.write_text(harness)
    subprocess.run(["cc", "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-I" + str(root / "src"), str(c_file),
                    str(root / "src/charset.c"), "-o", str(executable)], check=True)
    subprocess.run([str(executable)], check=True)

print("UTF-8 Chat tests passed")
