#ifndef CLIENT_KEY_H
#define CLIENT_KEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Client Key (aosprotocol extension 2, packet 66). The server sends one
   Challenge with a 32-byte nonce; the client answers once with its public key
   and an Ed25519 signature over the 16 ASCII bytes "AoS client key 1" followed
   by that nonce. One key per server address, so two servers cannot match the
   same player by comparing public keys. */
#define EXT_CLIENT_KEY 0x02
#define PACKET_CLIENT_KEY_ID 0x42 /* PACKET_EXT_BASE + EXT_CLIENT_KEY */
#define CLIENT_KEY_SUB_CHALLENGE 0
#define CLIENT_KEY_SUB_RESPONSE 1
#define CLIENT_KEY_NONCE_LEN 32
#define CLIENT_KEY_PUBLIC_LEN 32
#define CLIENT_KEY_SEED_LEN 32
#define CLIENT_KEY_ADDRESS_LEN 64
#define CLIENT_KEY_SIGNATURE_LEN 64
#define CLIENT_KEY_RESPONSE_LEN (1 + CLIENT_KEY_PUBLIC_LEN + CLIENT_KEY_SIGNATURE_LEN)

/* Ed25519 key pair from a 32-byte seed (RFC 8032 section 5.1.5). Exposed so
   tests can reproduce the RFC vectors and the store only keeps the seed. */
void client_key_derive(const uint8_t seed[CLIENT_KEY_SEED_LEN],
                       uint8_t public_key[CLIENT_KEY_PUBLIC_LEN],
                       uint8_t secret_key[CLIENT_KEY_SEED_LEN + CLIENT_KEY_PUBLIC_LEN]);

void client_key_reset_connection(void);
/* Sets the address the current connection talks to. Keys are per address, so
   this must be called before any Challenge arrives. */
void client_key_set_server(const char* address);
void client_key_set_negotiated(bool negotiated);
bool client_key_negotiated(void);
/* Whether the player allows this client to identify itself. */
bool client_key_enabled(void);
/* Builds the Response payload (sub-id, public key, signature) into out. Returns
   the payload length, or 0 when no reply may be sent: the player turned the
   extension off, the server did not offer it, a Challenge was answered on this
   connection already, no key can be created, or the buffer is too small. */
int client_key_build_response(const uint8_t* nonce, uint8_t* out, size_t out_size);
/* Deletes the key stored for the current server address. Returns true when one
   was removed. */
bool client_key_forget_server(void);
/* Number of stored server keys, for the settings screen and tests. */
int client_key_count(void);

#endif
