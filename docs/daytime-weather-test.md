# Daytime and Weather (extension `0x33`) — what to test against a server

This client implements [zerospades/aosprotocol
`extensions/daytime-weather.md`](https://github.com/zerospades/aosprotocol/blob/master/extensions/daytime-weather.md)
version 1, as matched by ZeroSpades' PR #76. It is a client only: a server must
offer the extension and send the packet.

## What the server has to do

1. Advertise `{0x33, 1}` in Extension Info (packet `0x3c`).
2. Accept the client's `{0x33, 1}` reply. A client that does not answer does not
   want the feature, and sending it a `Sky` is pointless but harmless.
3. Send packet `0x73` once the negotiation is known, and again whenever the time
   changes:

   | Offset | Size | Field | Notes |
   | --- | --- | --- | --- |
   | 0 | 1 | packet id | `0x73` |
   | 1 | 1 | sub-packet | `0` = `Sky` |
   | 2 | 2 | `Time` | little endian; **`0` = night**, `720` = noon; any nonzero value is day |
   | 4 | 2 | `Reserved` | `0` in v1; a later version puts the speed of the day here |
   | 6 | 2 | `Weather` | `[2]` bytes, `0` in v1 (ignored by this client) |

4. The Time is per **connection** and survives Map Start: send it again after a
   map change only if it changed. Do not expect the client to keep sending
   anything back — the extension is one-way after the handshake.

## What the client will do

* Hold the map loading screen until the first `Sky` arrives, for at most 3 s.
  If the server never sends one, the log says so once and the world is drawn as
  day. A demo never waits.
* At night (`Time == 0`): no sun light, no sun shadows, black fog and sky;
  dynamic lights (flashlight, glowing blocks, torches, grenades) keep their full
  brightness. On the legacy baked renderer the chunk light is re-baked once when
  the time flips.
* Ignore truncated packets (fewer than 7 bytes), unknown sub-packet ids, and any
  `0x73` when the extension was not negotiated.

## Checks that run without a server

```
python3 tests/daytime_weather_test.py      # wire layout + the day/night state machine
python3 tests/teamplay_negotiation_test.py # the ExtInfo reply, 0x33 included
make -C tools/bench check                  # -Wall -Wextra type check of the module
```

The test compiles `src/daytime_weather.c` against stubs, so it fails on a wire
change before any of this reaches a server.

## Manual pass

1. Connect to a server that advertises `0x33`. The log shows the offer and the
   v1 reply; the map stays on "Waiting for the server's time of day" until the
   `Sky` lands.
2. Have the server flip to night: the sun and its shadows go out, the fog and
   sky go black, and your flashlight is clearly the main light. Flip back to day
   and everything returns.
3. Change map while it is night: the map comes back at night with no flash of
   daylight.
4. Record a demo, seek into the middle of it: the time of day is whatever the
   recording says, not whatever the live session last had.
