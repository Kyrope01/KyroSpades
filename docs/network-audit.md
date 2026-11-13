# network.c review (33 points)

This is a targeted review, not a proof that all packet handling is safe. Numbering below matches the submitted review.

## Fixed

1. Outgoing packets now reject negative/oversized lengths, invalid IDs and null nonempty payloads before copying or inspecting a block action. The handshake independently rejects bodies over 511 bytes. ENet allocation and queue failures are handled with correct packet ownership. (Limits also apply to future callers.)
7. Killfeed weapon lookup has a fallback. In fact, the report's assumption that all weapon writes were validated was wrong: ExistingPlayer, CreatePlayer and ChangeWeapon accepted arbitrary weapon IDs. These ingress paths now reject unsupported weapons; ExistingPlayer also validates its held tool.
8, 26, 31. No null winner name or empty capture/drop chat entry for non-team players. Note that point 26 describes captures/drops, not KillAction.
9, 20. MapStart validates the complete 075 or 076 header before any map resets or reads. A 076 packet may include trailing bytes; intermediate lengths are rejected. The 64-byte name is copied and terminated for both logs.
10, 33. Host replacement destroys the previous host, checks creation failure, clears peer references, and disconnect cleans up on both graceful and failed paths. A host left after a remote disconnect is cleaned on the next connect or explicit disconnect. This changes lifetime handling and needs a real ENet reconnect test.
11, 18. A failed decompressor allocation is not used; unexpected return codes terminate the loop. Guard growth against signed overflow. Allocation macro failures elsewhere in this path still use the project's existing fatal allocation policy. No hard decompression-size budget was introduced.
13, 24. A const platform string has a generic fallback and mutually exclusive platform branches. Point 24's reference to programmable shaders is unrelated to this function.
19. During the synchronous connection wait the early-poll configuration now services events explicitly and tests the connection state before calling `network_update` (which otherwise returns success when already disconnected). The other configuration continues to poll through `network_update`.
25. Decimal addresses require a complete, nonnegative 32-bit number and no overflow; dotted host handling is unchanged.
27. Removed the dead unsigned-byte packet-ID check; packets too large for handler `int` lengths are rejected. Traffic counters saturate rather than overflow.

Additional verified problems: popup chat previously passed an unterminated network string to UI functions; popup and kick reason now use bounded payload lengths. Unknown chat types no longer use an uninitialized output buffer. TC StateData now rejects territory counts above the 16-element array, which was used by multiple handlers and rendering. Block action/line coordinates are checked before neighbor math and line generation, preventing integer overflow on hostile endpoints. Grenades reject nonfinite and extreme position/velocity/fuse values before integer conversions in simulation (the generous limits are compatibility policy and may need adjustment for custom servers). Orientation rejects nonfinite magnitude. TC capture labels validate coordinates before float-to-int conversion.

## Claims qualified or not applicable to this checkout

2, 3, 4a/b, 5, 6, 14, 15. `PLAYER_ID_SERVER` is 255; `PLAYER_COUNT_MAX` is 255 and `PLAYERS_MAX` is 256. Packet IDs and `local_player_id` are unsigned bytes; the latter starts at 0, not -1. Thus all 0..255 indices fit `players[256]` in this build; 200 is supported, not out of bounds. Slot 255 is reserved for server actions and must not be accepted as the local player: StateData now rejects it for live sessions, and local gameplay sync excludes it. ExistingPlayer/CreatePlayer deliberately use the smaller real-player count. Demo playback uses slot 255 for a spectator, so don't globally ban that slot from packet handlers. No claim is made about a future build with differently sized arrays.
4c, 30. `len` describes the actual ENet buffer, so reading a longer received payload through the trailing message address is not automatically reading outside the allocation. The decoder is capacity-bounded. Nonetheless its field and protocol size are now capped and length is validated before unsigned subtraction. Server pseudo-player messages and pre-login chat are preserved rather than rejected.
12. Wire team names are 10 bytes and destination team names are 11, so the explicit terminator is in bounds.
16. Already fixed: `file_load_sized` is checked, nonempty data required, and cache response falls back to server transfer on load failure. A nonempty but malformed cache can still be reported as cached because `map_vxl_load` does not return success; this is deferred.
17. `player_init` resets all players, scores, interpolation and corpses on map change, so it is retained and documented. MapStart is not merely a local-player reset.
21. Damage Marker logs intentionally use wire length (body plus packet ID); other extension parsers do likewise. Cosmetic consistency is deferred.
22, 23. Extension negotiation and world-update scans are connection/packet-scale work; no performance gain is asserted. Preserved to avoid changing negotiation semantics without profiling.
28. Tools are SPADE=0, BLOCK=1, GUN=2, GRENADE=3, so `<= TOOL_GUN` would incorrectly reject grenades. Current `< 4` accepts the defined protocol tools.
29. VXL load currently hardcodes 512x512x64; 63 is a protocol-wide convention used in physics and many other modules, not just the quoted handlers. A partial replacement would be inconsistent. Supporting taller maps needs an end-to-end protocol/physics migration.
32. Standard C `rand()` guarantees 0..RAND_MAX; defensive handling of a broken implementation is not warranted.

## Remaining security / verification limits

Other network-supplied float coordinates and physics inputs (including player spawn/position/world updates and objective positions) have not all been exhaustively range-validated; extreme or nonfinite values may still propagate to rendering or physics. TC progress and resource budgets also warrant a separate end-to-end audit. Do not treat this patch as a complete security boundary hardening.

Tests use extracted production functions, fake ENet/map/UI and UBSan, plus stub-only syntax checks. They cannot establish actual network interoperability, cache integrity, visual behavior, or disconnect/reconnect behavior with a real server; no full native build or live server was available.
