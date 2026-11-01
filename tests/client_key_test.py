"""Checks for the Client Key extension (aosprotocol 2, packet 66).

The real client_key.c is compiled against the vendored TweetNaCl and the actual
config/file headers, so these checks cover the crypto, the store and the reply
rules rather than a copy of them:

  * RFC 8032 Ed25519 test vectors, through the same seed -> key derivation the
    client uses;
  * the Response is 97 bytes (sub-id 1, 32-byte key, 64-byte signature) and the
    signature verifies over "AoS client key 1" || nonce;
  * nothing is sent while the player has the extension off, when the server did
    not offer it, or on a second Challenge in the same connection;
  * one key per server address, stable across runs (the store is read back),
    and forgettable.

Run: python3 tests/client_key_test.py
"""
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]


network = (root / "src/network.c").read_text()
network_header = (root / "src/network.h").read_text()
client_key_header = (root / "src/client_key.h").read_text()
config_header = (root / "src/config.h").read_text()
config_source = (root / "src/config.c").read_text()

# The extension id and its packet number follow the registry (64 + id).
assert "#define EXT_CLIENT_KEY 0x02" in client_key_header
assert "#define PACKET_CLIENT_KEY_ID 0x42" in client_key_header
assert "void read_PacketClientKey(void* data, int len);" in network_header
assert "packets[PACKET_CLIENT_KEY_ID] = read_PacketClientKey;" in network
assert "void read_PacketClientKey(void* data, int len) {" in network

# A second Challenge in a connection gets no answer, and neither does a server
# that was not offered the extension or a player who turned it off.
assert "if(packet[0] != CLIENT_KEY_SUB_CHALLENGE) return;" in network
assert "if(!client_key_enabled() || !negotiated || answered) return 0;" in (
    root / "src/client_key.c").read_text()

# Negotiation only claims the extension when the player allows it, and the
# per-connection state is cleared on both ends of a session.
assert "supports_client_key = client_key_enabled();" in network
assert "client_key_set_negotiated(supports_client_key);" in network
assert ("reply.entries[next_entry++] = (struct PacketExtInfoEntry){ EXT_CLIENT_KEY, 1 };"
        in network)
assert ("(supports_damage_markers ? 1 : 0) + (supports_client_key ? 1 : 0)"
        "\n                        + (supports_daytime_weather ? 1 : 0);") in network
assert network.count("client_key_reset_connection();") == 2
assert "client_key_set_server(client_key_address);" in network

# The setting exists in all four places a setting has to exist.
assert "int client_key_login;" in config_header
assert 'config_seti("client", "client_key_login", settings.client_key_login);' in config_source
assert "IMPORT_SETTING(settings.client_key_login, client_key_login, atoi(value) ? 1 : 0);" in config_source
assert ".value = &settings_tmp.client_key_login," in config_source
assert "client_key_login               = 1" in (root / "resources/config.ini").read_text()

# Build wiring.
cmake = (root / "src/CMakeLists.txt").read_text()
assert "client_key.c" in cmake
assert "deps/tweetnacl/tweetnacl.c" in cmake
assert "client_key.c" in (root / "tools/bench/Makefile").read_text()

PROGRAM = r"""
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client_key.h"
#include "config.h"
#include "../deps/tweetnacl/tweetnacl.h"

struct RENDER_OPTIONS settings;

void* file_open(const char* name, const char* mode) { return fopen(name, mode); }
void file_printf(void* file, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vfprintf((FILE*)file, fmt, args);
    va_end(args);
}
void file_close(void* file) { fclose((FILE*)file); }
unsigned char* file_load(const char* name) {
    FILE* file = fopen(name, "rb");
    if(!file) return NULL;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    unsigned char* data = malloc((size_t)size + 1);
    size_t read = fread(data, 1, (size_t)size, file);
    data[read] = 0;
    fclose(file);
    return data;
}
void log_log(int level, const char* file, int line, const char* fmt, ...) {
    (void)level; (void)file; (void)line; (void)fmt;
}

static void hex_print(const char* label, const uint8_t* data, size_t len) {
    printf("%s ", label);
    for(size_t i = 0; i < len; ++i) printf("%02x", data[i]);
    printf("\n");
}

static int hex_nibble(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    return (c | 0x20) - 'a' + 10;
}

static void hex_read(const char* text, uint8_t* out, size_t len) {
    for(size_t i = 0; i < len; ++i)
        out[i] = (uint8_t)(hex_nibble(text[i * 2]) << 4 | hex_nibble(text[i * 2 + 1]));
}

/* Verifies a Response payload the way the server does: the signature in the
   packet must open over the prefixed nonce under the public key next to it. */
static int response_verifies(const uint8_t* nonce, const uint8_t* response) {
    if(response[0] != CLIENT_KEY_SUB_RESPONSE) return 0;
    uint8_t message[16 + CLIENT_KEY_NONCE_LEN];
    memcpy(message, "AoS client key 1", 16);
    memcpy(message + 16, nonce, CLIENT_KEY_NONCE_LEN);
    uint8_t signed_message[CLIENT_KEY_SIGNATURE_LEN + sizeof message];
    memcpy(signed_message, response + 1 + CLIENT_KEY_PUBLIC_LEN, CLIENT_KEY_SIGNATURE_LEN);
    memcpy(signed_message + CLIENT_KEY_SIGNATURE_LEN, message, sizeof message);
    /* crypto_sign_open copies all of sm into its first argument, so that
       buffer has to be the full signed length. */
    uint8_t opened[sizeof signed_message];
    unsigned long long opened_length = 0;
    if(crypto_sign_open(opened, &opened_length, signed_message, sizeof signed_message,
                        response + 1) != 0)
        return 0;
    return opened_length == sizeof message
           && memcmp(opened + CLIENT_KEY_SIGNATURE_LEN, message, sizeof message) == 0;
}

/* RFC 8032 section 7.1, with the message signed directly. */
static int rfc8032(const char* seed_hex, const char* public_hex, const char* message_hex,
                   const char* signature_hex) {
    uint8_t seed[CLIENT_KEY_SEED_LEN], expected_public[CLIENT_KEY_PUBLIC_LEN];
    uint8_t public_key[CLIENT_KEY_PUBLIC_LEN], secret_key[CLIENT_KEY_SEED_LEN + CLIENT_KEY_PUBLIC_LEN];
    uint8_t expected_signature[CLIENT_KEY_SIGNATURE_LEN];
    hex_read(seed_hex, seed, sizeof seed);
    hex_read(public_hex, expected_public, sizeof expected_public);
    hex_read(signature_hex, expected_signature, sizeof expected_signature);
    client_key_derive(seed, public_key, secret_key);
    if(memcmp(public_key, expected_public, sizeof public_key) != 0) return 0;

    size_t message_length = strlen(message_hex) / 2;
    uint8_t message[512];
    hex_read(message_hex, message, message_length);
    uint8_t signed_message[CLIENT_KEY_SIGNATURE_LEN + sizeof message];
    unsigned long long signed_length = 0;
    if(crypto_sign(signed_message, &signed_length, message, message_length, secret_key) != 0)
        return 0;
    if(signed_length != CLIENT_KEY_SIGNATURE_LEN + message_length) return 0;
    if(memcmp(signed_message, expected_signature, CLIENT_KEY_SIGNATURE_LEN) != 0) return 0;
    return memcmp(signed_message + CLIENT_KEY_SIGNATURE_LEN, message, message_length) == 0;
}

static int answer(uint8_t* out, int* length) {
    uint8_t nonce[CLIENT_KEY_NONCE_LEN];
    for(int i = 0; i < CLIENT_KEY_NONCE_LEN; ++i) nonce[i] = (uint8_t)(i * 7 + 3);
    *length = client_key_build_response(nonce, out, CLIENT_KEY_RESPONSE_LEN);
    return *length == CLIENT_KEY_RESPONSE_LEN && response_verifies(nonce, out);
}

int main(int argc, char** argv) {
    const char* scenario = argc > 1 ? argv[1] : "";
    settings.client_key_login = 1;

    if(!strcmp(scenario, "rfc8032")) {
        /* 1: empty message, 2: one byte 0x72. */
        printf("vector1 %d\n", rfc8032(
            "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
            "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
            "",
            "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"));
        printf("vector2 %d\n", rfc8032(
            "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
            "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
            "72",
            "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"));
        return 0;
    }

    if(!strcmp(scenario, "answer")) {
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(true);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        int length = 0;
        printf("first %d\n", answer(response, &length));
        hex_print("public", response + 1, CLIENT_KEY_PUBLIC_LEN);
        int second = client_key_build_response((const uint8_t*)"0123456789abcdef0123456789abcdef", response,
                                               CLIENT_KEY_RESPONSE_LEN);
        printf("second %d\n", second);
        return 0;
    }

    if(!strcmp(scenario, "ask-again")) {
        /* A later run of the same address must reuse the stored key. */
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(true);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        int length = 0;
        printf("first %d\n", answer(response, &length));
        hex_print("public", response + 1, CLIENT_KEY_PUBLIC_LEN);
        return 0;
    }

    if(!strcmp(scenario, "other-server")) {
        client_key_set_server("10.0.0.2:10666");
        client_key_set_negotiated(true);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        int length = 0;
        printf("first %d\n", answer(response, &length));
        hex_print("public", response + 1, CLIENT_KEY_PUBLIC_LEN);
        printf("count %d\n", client_key_count());
        return 0;
    }

    if(!strcmp(scenario, "disabled")) {
        settings.client_key_login = 0;
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(true);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        int length = 0;
        printf("answered %d\n", answer(response, &length));
        (void)length;
        return 0;
    }

    if(!strcmp(scenario, "not-negotiated")) {
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(false);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        printf("length %d\n", client_key_build_response(
                   (const uint8_t*)"0123456789abcdef0123456789abcdef", response,
                   CLIENT_KEY_RESPONSE_LEN));
        return 0;
    }

    if(!strcmp(scenario, "small-buffer")) {
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(true);
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        printf("length %d\n", client_key_build_response(
                   (const uint8_t*)"0123456789abcdef0123456789abcdef", response,
                   CLIENT_KEY_RESPONSE_LEN - 1));
        return 0;
    }

    if(!strcmp(scenario, "forget")) {
        client_key_set_server("127.0.0.1:10666");
        client_key_set_negotiated(true);
        printf("removed %d\n", client_key_forget_server());
        printf("count_after_forget %d\n", client_key_count());
        uint8_t response[CLIENT_KEY_RESPONSE_LEN];
        int length = 0;
        printf("first %d\n", answer(response, &length));
        hex_print("public", response + 1, CLIENT_KEY_PUBLIC_LEN);
        printf("count_after_answer %d\n", client_key_count());
        return 0;
    }

    printf("unknown scenario\n");
    return 1;
}
"""

with tempfile.TemporaryDirectory() as tmp:
    workspace = Path(tmp)
    source = workspace / "client_key_test.c"
    source.write_text(PROGRAM)
    binary = workspace / "client_key_test"
    compile_command = [
        "cc", "-std=gnu99", "-Wall", "-Wextra", "-Werror",
        "-I", str(root / "src"), "-I", str(root / "deps"),
        str(source), str(root / "src/client_key.c"),
        str(root / "deps/tweetnacl/tweetnacl.c"), "-o", str(binary),
    ]
    build = subprocess.run(compile_command, capture_output=True, text=True)
    assert build.returncode == 0, build.stderr
    assert build.stderr == "", build.stderr

    def run(scenario):
        result = subprocess.run([str(binary), scenario], capture_output=True, text=True,
                                cwd=workspace)
        assert result.returncode == 0, result.stderr
        values = {}
        for line in result.stdout.splitlines():
            key, _, value = line.partition(" ")
            values[key] = value
        return values

    vectors = run("rfc8032")
    assert vectors["vector1"] == "1", vectors
    assert vectors["vector2"] == "1", vectors

    first = run("answer")
    assert first["first"] == "1", first
    assert first["second"] == "0", "a Challenge may only be answered once per connection"
    public = first["public"]
    assert re.fullmatch(r"[0-9a-f]{64}", public), public

    # The store is written, then read back by a second process: same address,
    # same key.
    reused = run("ask-again")
    assert reused["public"] == public, "the stored key must be reused for the same server"

    other = run("other-server")
    assert other["public"] != public, "each server address gets its own key"
    assert other["count"] == "2", other

    assert run("disabled")["answered"] == "0"
    assert run("not-negotiated")["length"] == "0"
    assert run("small-buffer")["length"] == "0"

    forgotten = run("forget")
    assert forgotten["removed"] == "1", forgotten
    assert forgotten["count_after_forget"] == "1", forgotten
    assert forgotten["first"] == "1", "a Challenge after forgetting must still be answered"
    assert forgotten["public"] != public, "a forgotten key must not come back"
    assert forgotten["count_after_answer"] == "2", forgotten

    stored = (workspace / "client_key.dat").read_text()
    assert "127.0.0.1:10666" in stored and "10.0.0.2:10666" in stored, stored
    assert stored.count("\n") >= 4, stored

print("client key: ok")
