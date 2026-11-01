# Client Key (aosprotocol 2, packet 66)

Server-side login convenience: the server learns a stable public key for this
player and can recognise them again, without the player trusting it with a
password. The spec is
[PR #24](https://github.com/zerospades/aosprotocol/pull/24); this page records
what KyroSpades does with it.

## Wire

| Direction | Payload | Size |
|-----------|---------|------|
| Server → client | sub-id `0` (Challenge), nonce | 33 bytes |
| Client → server | sub-id `1` (Response), public key, signature | 97 bytes |

The signature is Ed25519 (RFC 8032) over the 16 ASCII bytes `AoS client key 1`
followed by the 32-byte nonce — 48 bytes in total. `packet 66` is `64 + 2`, the
extension packet id rule.

## Rules this client follows

* **Negotiated, not assumed.** Extension 2 is named in the `ExtInfo` reply only
  when the server offered it *and* the player allows it. If the player has it
  off, a Challenge that arrives anyway is ignored.
* **One answer per connection.** `client_key_build_response()` returns 0 on a
  second Challenge, and the per-connection state is cleared on connect and on
  disconnect, so a map change or reconnect starts clean.
* **One key per server address.** The store is keyed by `ip:port`; the same
  install presents different public keys to different servers, so two servers
  cannot join their logs into one identity.
* **No weak fallback.** If the OS entropy source cannot be opened, no key is
  created and no Response is sent. A key derived from a guessable seed would
  look valid to the server and be worse than staying anonymous.

## Where the key lives

`client_key.dat`, next to `config.ini`. One line per server:

```
# KyroSpades client keys, one Ed25519 seed per server address.
# Delete a line (or the whole file) to forget that key.
127.0.0.1:10666 3f0ac1...b7           (64 hex characters = 32 bytes)
```

The stored value is the 32-byte Ed25519 seed; the public key is derived from it
on demand, so the file never needs to be rewritten when a server changes.
Deleting a line — or the file — forgets that key; the next Challenge to that
address creates a new one.

`client_key_login` in `config.ini` (settings screen: **Client Key Login**)
turns the whole feature off. Off means the extension is not advertised and no
Challenge is answered; keys already on disk are left alone.

## Checks

`tests/client_key_test.py` compiles the real `src/client_key.c` against the
vendored TweetNaCl and checks:

* RFC 8032 test vectors 1 and 2 through the same `seed → key pair` derivation
  the client uses (this is what catches a broken crypto build);
* the Response is 97 bytes, starts with sub-id 1, and its signature opens over
  `AoS client key 1` + nonce under the public key in the packet;
* nothing is sent with the setting off, with no negotiation, or with a buffer
  that is one byte short;
* a second Challenge in the same connection is not answered;
* the key is stable for an address across processes (the store is read back)
  and different for another address;
* forgetting a key removes it and the next Challenge is answered with a new one;
* the wiring: the packet is registered, the `ExtInfo` line is conditional on
  the setting, and the per-connection reset runs on both connect and disconnect.

`tests/teamplay_negotiation_test.py` covers the `ExtInfo` reply itself: the
extension is answered while the setting is on and left out when it is off.

`make -C tools/bench check` type-checks `src/client_key.c` with `-Wall -Wextra`.
