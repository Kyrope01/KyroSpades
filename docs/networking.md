# Networking: what KyroSpades implements, against the spec

The wire protocol is the one documented in
[zerospades/aosprotocol](https://github.com/zerospades/aosprotocol): the 0.75
base protocol plus the registered extensions. KyroSpades is a client only, so
everywhere below "we" means what this client sends, parses or draws.

This page exists so that a spec change has somewhere to land. When aosprotocol
moves, the table and the notes below are what has to be re-checked.

## Base protocol

`protocol075.md`, all of it: handshake and version, map, world/input/weapon
packets, players, blocks, chat, game state. The packet ids, struct layouts and
field order in `src/network.h` follow the tables there; `PacketStateData`'s
CTF/TC union is the one piece with commentary worth reading (the intel bits are
inverted from this client's naming — see `TEAM_n_INTEL_HELD`).

The 0.76 forms (`Map Start (0.76)`, `World Update (0.76)`, `Map Cached`) are
parsed as well, since servers pick a version per map.

Chat encoding is specified by
[UTF-8 Chat](https://github.com/zerospades/aosprotocol/blob/master/extensions/utf-8-chat.md).
All chat — typed, macros, and a Pie Menu slice's Text — goes through
`network_send_chat()`, which sends printable ASCII bare and anything else behind
the `0xff` prefix. On the way in, the prefix means UTF-8 and its absence means
Code Page 437, which is decoded to the UTF-8 the font takes
(`src/charset.c`).

## Extensions

| ID | Extension | State here |
|----|-----------|------------|
| 0 | Player Properties | Parsed (health, blocks, grenades, ammo, score) |
| `0x20` | [Damage Markers](https://github.com/zerospades/aosprotocol/blob/master/extensions/damage-markers.md) | Parsed; drives the floating numbers. Offered by the server, answered; while it is negotiated the client's own weapon-table estimate is suppressed |
| 2 | [Client Key](https://github.com/zerospades/aosprotocol/pull/24) (open PR) | Challenge answered once per connection with an Ed25519 signature; see `docs/client-key-test.md` |
| 48 | [Teamplay](https://github.com/zerospades/aosprotocol/blob/master/extensions/teamplay.md) | Config, Ping (send and receive) and ESP marks |
| `0x31` | [Pie Menu](https://github.com/zerospades/aosprotocol/blob/master/extensions/pie-menu.md) | Parses a Menu whole or not at all; see `docs/pie-menu-v1.md` |
| `0x32` | [Flashlight](https://github.com/zerospades/aosprotocol/blob/master/extensions/flashlight.md) | Light, Light State and Light Config including Flicker; see `docs/flashlight-v1-test.md` |
| `0x33` | [Daytime and Weather](https://github.com/zerospades/aosprotocol/blob/master/extensions/daytime-weather.md) | The `Sky` sub-packet's day/night flag; night is full darkness. See below |
| `0x35` | [Player Outfit](https://github.com/zerospades/aosprotocol/pull/14) (open PR) | Set Outfit with its Colour byte, and Team Outfits |
| 192 | Player Limit | Advertised; 255 players |
| 193 | Message Types | Advertised; chat types 3-6 are drawn |
| 194 | Kick Reason | Advertised; the reason is taken from a CHAT_SYSTEM line from player 255 |
| none | UTF-8 Chat | Both directions, see above |

Negotiation is the flow in
[Extensions](https://github.com/zerospades/aosprotocol/blob/master/extensions/extension.md):
the server sends `ExtInfo` (60), the client answers with the extensions it
actually implements, and an extension that is not offered is not answered and
not used. `read_PacketExtInfo()` in `src/network.c` is the only place that
decides this.

## Client Key

Extension 2 keeps a login token: the server sends one Challenge with a 32-byte
nonce, the client answers once with the public half of an Ed25519 key for that
server and a signature over `AoS client key 1` followed by the nonce, and the
server remembers the key. Ed25519 comes from the public-domain
[TweetNaCl](https://tweetnacl.cr.yp.to/), vendored unmodified in
`deps/tweetnacl/`.

Two rules matter for interoperability:

* One key **per server address** (`ip:port`). Two servers comparing public keys
  must not be able to tell that they are talking to the same install.
* The reply is sent **once per connection**. A second Challenge is dropped, and
  the whole per-connection state is cleared on connect and disconnect, so a
  re-handshake after a map change starts clean.

The player owns the feature: `client_key_login` in `config.ini` (also "Client
Key Login" on the settings screen) is checked before the extension is named in
the `ExtInfo` reply, so a client with it off does not advertise it and stays
silent if a Challenge arrives anyway. Keys live in `client_key.dat` next to
`config.ini`; deleting a line forgets that server's key, and deleting the file
forgets all of them. `src/client_key.c` holds the store and the signature,
`src/network.c` only routes the packet.

## Daytime and Weather

Extension `0x33`, packet `0x73`, version 1, sub-packet `0` = `Sky`. The body is
`Time` (UShort), `Reserved` (UShort, must be 0), `Weather` (UByte[2], must be 0 in
v1). **`Time == 0` is night**; anything else is day (`720`, noon, is what the
reference client writes). `Reserved` is where a later version puts the speed of
the day, so a v1 client must ignore it rather than reject the packet.

Rules this client keeps, all in `src/daytime_weather.c`:

* It answers `0x33` in `ExtInfo` only when the server offered it at version 1 or
  later, and ignores any `0x73` when it did not negotiate the extension.
* **The world waits for the first Sky.** A client that negotiated the extension
  must not draw the world before the server has said what time it is, so the map
  loading screen stays up until the Sky arrives. The wait is bounded at 3 s with
  a warning, so a server that forgets cannot wedge the client, and a demo never
  waits (its packets carry the Sky).
* The Time belongs to the **connection**: `network_connect()`,
  `network_disconnect()` and a kicked connection clear it, but Map Start does
  not, exactly as the extension requires.
* Night is **no sun light and no shadow, black fog and sky**, with only dynamic
  lights lighting the world (they are added separately in the shaders, so a
  flashlight is as bright at night as by day). One factor,
  `daytime_weather_sunlight()`, multiplies `lighting_sunlight_scale()` and the
  render-time fog colour; because `sky.c`'s `u_Daylight`, the lens flare, the
  water sun term and the model lighting all read that scale, they go dark
  together. The shadow pass is skipped at night — there is no sun to cast one —
  and the baked (non-shader) paths remesh once when the time flips.
* Demos: `demo_start_record()` writes the Sky in force into the recording, so a
  demo that starts mid-match replays the right time, and `demo_reset_world()`
  forgets the Time so a seek replays it from the packet.

This mirrors ZeroSpades' implementation (PR #76), which is still open there; the
wire layout is the registered aosprotocol v1 one, so either client can talk to
either server.

## Not implemented

Nothing here at the moment. Every extension in the table above is parsed.

## Harness

`make -C tools/bench check` type-checks `network.c`, `player.c`, `lighting.c`,
`damagenumbers.c`, `pie_menu*.c`, `teamplay*.c`, `flashlight_ext.c`,
`daytime_weather.c`, `charset.c` and `client_key.c` with `-Wall -Wextra`, so a wire
change that does not compile is caught without a server. The behavioural tests are `tests/chat_utf8_test.py`,
`tests/teamplay_negotiation_test.py`, `tests/teamplay_protocol_test.py`,
`tests/pie_menu_ext_test.py`, `tests/flashlight_protocol_test.py`,
`tests/player_outfit_extension_test.py`, `tests/player_outfit_state_test.py`,
`tests/client_key_test.py` and `tests/daytime_weather_test.py` (which compiles the
module against stubs and drives the state machine, not just the strings).
