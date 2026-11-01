#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client_key.h"
#include "config.h"
#include "file.h"
#include "log.h"
#include "../deps/tweetnacl/tweetnacl.h"

#if defined(_WIN32)
#include <windows.h>
#endif

#define CLIENT_KEY_FILE "client_key.dat"
#define CLIENT_KEY_MAX 64
#define CLIENT_KEY_SECRET_LEN (CLIENT_KEY_SEED_LEN + CLIENT_KEY_PUBLIC_LEN)
/* The exact bytes a Response signs, before the nonce. */
static const char client_key_prefix[16] = "AoS client key 1";

struct client_key_entry {
    char address[CLIENT_KEY_ADDRESS_LEN];
    uint8_t seed[CLIENT_KEY_SEED_LEN];
};

static struct client_key_entry entries[CLIENT_KEY_MAX];
static int entry_count;
static bool entries_loaded;
static char current_address[CLIENT_KEY_ADDRESS_LEN];
static bool negotiated;
static bool answered;

/* TweetNaCl takes the private seed from randombytes(). Making that call
   deterministic is the only way to derive a key from a known seed with the
   unmodified library, so client_key_derive parks the seed here for the one
   crypto_sign_keypair call it makes. Client key use is single threaded, like
   the rest of the client's connection handling. */
static const uint8_t* pending_seed;

void randombytes(unsigned char* out, unsigned long long len) {
    if(pending_seed) {
        memcpy(out, pending_seed, (size_t)len);
        pending_seed = NULL;
        return;
    }
    memset(out, 0, (size_t)len);
    log_error("No entropy source for client key generation");
}

void client_key_derive(const uint8_t seed[CLIENT_KEY_SEED_LEN],
                       uint8_t public_key[CLIENT_KEY_PUBLIC_LEN],
                       uint8_t secret_key[CLIENT_KEY_SEED_LEN + CLIENT_KEY_PUBLIC_LEN]) {
    pending_seed = seed;
    if(crypto_sign_keypair(public_key, secret_key) != 0) {
        log_error("Client key generation failed");
        memset(public_key, 0, CLIENT_KEY_PUBLIC_LEN);
        memset(secret_key, 0, CLIENT_KEY_SEED_LEN + CLIENT_KEY_PUBLIC_LEN);
    }
    pending_seed = NULL;
}

static int hex_value(char c) {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static void hex_encode(const uint8_t* data, int len, char* out) {
    static const char digits[] = "0123456789abcdef";
    for(int i = 0; i < len; ++i) {
        out[i * 2] = digits[data[i] >> 4];
        out[i * 2 + 1] = digits[data[i] & 0xF];
    }
    out[len * 2] = 0;
}

static bool hex_decode(const char* text, uint8_t* out, int len) {
    if((int)strlen(text) != len * 2) return false;
    for(int i = 0; i < len; ++i) {
        int high = hex_value(text[i * 2]);
        int low = hex_value(text[i * 2 + 1]);
        if(high < 0 || low < 0) return false;
        out[i] = (uint8_t)(high << 4 | low);
    }
    return true;
}

static void client_key_entries_load(void) {
    if(entries_loaded) return;
    entries_loaded = true;
    char* data = (char*)file_load(CLIENT_KEY_FILE);
    if(!data) return;
    char* line = data;
    while(*line && entry_count < CLIENT_KEY_MAX) {
        char* end = strchr(line, '\n');
        if(end) *end = 0;
        if(*line && *line != '#') {
            char address[CLIENT_KEY_ADDRESS_LEN];
            char secret[CLIENT_KEY_SEED_LEN * 2 + 1];
            if(sscanf(line, "%63s %64s", address, secret) == 2
               && hex_decode(secret, entries[entry_count].seed, CLIENT_KEY_SEED_LEN)) {
                snprintf(entries[entry_count].address, sizeof entries[entry_count].address, "%s", address);
                entry_count++;
            } else {
                log_warn("Ignoring malformed line in %s", CLIENT_KEY_FILE);
            }
        }
        if(!end) break;
        line = end + 1;
    }
    free(data);
}

static void client_key_entries_save(void) {
    void* file = file_open(CLIENT_KEY_FILE, "w");
    if(!file) {
        log_error("Could not write %s, client keys are not saved", CLIENT_KEY_FILE);
        return;
    }
    file_printf(file, "# KyroSpades client keys, one Ed25519 seed per server address.\n");
    file_printf(file, "# Delete a line (or the whole file) to forget that key.\n");
    for(int i = 0; i < entry_count; ++i) {
        char secret[CLIENT_KEY_SEED_LEN * 2 + 1];
        hex_encode(entries[i].seed, CLIENT_KEY_SEED_LEN, secret);
        file_printf(file, "%s %s\n", entries[i].address, secret);
    }
    file_close(file);
}

/* OS entropy. There is no fallback: a key built from a weak source would look
   valid to the server and quietly identify the player worse than not answering
   at all. */
static bool client_key_random(uint8_t* out, size_t len) {
#if defined(_WIN32)
    typedef long(__stdcall * bcrypt_gen_random)(void*, unsigned char*, unsigned long, unsigned long);
    static bcrypt_gen_random generate;
    static bool looked_up;
    if(!looked_up) {
        looked_up = true;
        HMODULE library = LoadLibraryA("bcrypt.dll");
        if(library)
            generate = (bcrypt_gen_random)(void*)GetProcAddress(library, "BCryptGenRandom");
    }
    /* BCRYPT_USE_SYSTEM_PREFERRED_RNG, NULL algorithm handle. */
    return generate && generate(NULL, out, (unsigned long)len, 2) == 0;
#else
    FILE* file = fopen("/dev/urandom", "rb");
    if(!file) return false;
    size_t read = fread(out, 1, len, file);
    fclose(file);
    return read == len;
#endif
}

static struct client_key_entry* client_key_entry_for(const char* address, bool create) {
    if(!address || !*address) return NULL;
    client_key_entries_load();
    for(int i = 0; i < entry_count; ++i)
        if(strcmp(entries[i].address, address) == 0) return &entries[i];
    if(!create) return NULL;
    if(entry_count >= CLIENT_KEY_MAX) {
        log_warn("Client key store is full, not answering the Challenge");
        return NULL;
    }
    struct client_key_entry* entry = &entries[entry_count];
    snprintf(entry->address, sizeof entry->address, "%s", address);
    if(!client_key_random(entry->seed, sizeof entry->seed)) {
        log_warn("No system entropy available, not answering the client key Challenge");
        return NULL;
    }
    entry_count++;
    client_key_entries_save();
    return entry;
}

void client_key_reset_connection(void) {
    negotiated = false;
    answered = false;
    current_address[0] = 0;
}

void client_key_set_server(const char* address) {
    answered = false;
    snprintf(current_address, sizeof current_address, "%s", address ? address : "");
}

void client_key_set_negotiated(bool enabled) {
    negotiated = enabled;
    answered = false;
}

bool client_key_negotiated(void) {
    return negotiated;
}

bool client_key_enabled(void) {
    return settings.client_key_login != 0;
}

int client_key_count(void) {
    client_key_entries_load();
    return entry_count;
}

bool client_key_forget_server(void) {
    if(!current_address[0]) return false;
    client_key_entries_load();
    for(int i = 0; i < entry_count; ++i) {
        if(strcmp(entries[i].address, current_address) != 0) continue;
        memmove(&entries[i], &entries[i + 1], (size_t)(entry_count - i - 1) * sizeof entries[0]);
        entry_count--;
        memset(&entries[entry_count], 0, sizeof entries[0]);
        client_key_entries_save();
        answered = false;
        return true;
    }
    return false;
}

int client_key_build_response(const uint8_t* nonce, uint8_t* out, size_t out_size) {
    if(!nonce || !out || out_size < CLIENT_KEY_RESPONSE_LEN) return 0;
    if(!client_key_enabled() || !negotiated || answered) return 0;
    struct client_key_entry* entry = client_key_entry_for(current_address, true);
    if(!entry) return 0;

    uint8_t public_key[CLIENT_KEY_PUBLIC_LEN];
    uint8_t secret_key[CLIENT_KEY_SECRET_LEN];
    client_key_derive(entry->seed, public_key, secret_key);

    uint8_t message[sizeof client_key_prefix + CLIENT_KEY_NONCE_LEN];
    memcpy(message, client_key_prefix, sizeof client_key_prefix);
    memcpy(message + sizeof client_key_prefix, nonce, CLIENT_KEY_NONCE_LEN);

    /* crypto_sign writes the 64-byte signature in front of the message. */
    uint8_t signed_message[CLIENT_KEY_SIGNATURE_LEN + sizeof message];
    unsigned long long signed_length = 0;
    int result = crypto_sign(signed_message, &signed_length, message, sizeof message, secret_key);
    memset(secret_key, 0, sizeof secret_key);
    if(result != 0 || signed_length != sizeof signed_message) {
        log_error("Client key signature failed, not answering the Challenge");
        return 0;
    }

    out[0] = CLIENT_KEY_SUB_RESPONSE;
    memcpy(out + 1, public_key, CLIENT_KEY_PUBLIC_LEN);
    memcpy(out + 1 + CLIENT_KEY_PUBLIC_LEN, signed_message, CLIENT_KEY_SIGNATURE_LEN);
    answered = true;
    log_info("Answered the client key Challenge for %s", current_address);
    return CLIENT_KEY_RESPONSE_LEN;
}
