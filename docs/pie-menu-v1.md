# Testing Pie Menu V1

This client implements [zerospades/aosprotocol `extensions/pie-menu.md`](https://github.com/zerospades/aosprotocol/blob/master/extensions/pie-menu.md) as **merged** in [`7449ddd`](https://github.com/zerospades/aosprotocol/commit/7449ddd) (PR #11). It does not implement a server. A test server must offer extension **0x31, version 1** and send packet **0x71**; the client never sends one. Until a Menu arrives, the built-in pie menu stands; after one arrives it does not come back until the connection is replaced.

1. Connect to a server that advertises `0x31` in Extension Info (packet `0x3c`); the client logs the offer and answers with `0x31` version `1`. Run `python3 tests/pie_menu_ext_test.py`, `python3 tests/pie_menu_test.py` and `python3 tests/teamplay_negotiation_test.py` for repeatable wire, routing and handshake checks without a server.
2. A Menu is `0x71`, sub-packet `0`, a Pie Count, then that many pies. Each pie is a Contexts byte, a reserved Theme Message ID, a NUL-terminated Theme, and exactly six slices of a reserved Message ID, a Flags byte and three NUL-terminated strings (Text, Label, Tooltip). Strings are Code Page 437, or UTF-8 behind the `0xff` prefix of UTF-8 Chat; a blank slice, whose Text and Label are both empty, is five `00` bytes. `tests/pie_menu_ext_test.py` parses every packet below and checks it means what its caption says. Complete packets, top-level ID included:
   * A Pie Count of `0`: there is no pie menu here, and the client's own does not come back.

         71 00 00

   * One pie for `BLOCK` (`0x01`), theme `Social`, slice 0 sending `Hi!` and drawn as `Hi`, then five blank slices.

         71 00 01 01 00 53 6f 63 69 61 6c 00 00 00 48 69
         21 00 48 69 00 00 00 00 00 00 00 00 00 00 00 00
         00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

   * One pie for `DEAD` (`0x10`), slice 0 with `GLOBAL` (`0x01`): offered while the player is dead and waiting to respawn, and sent to the whole server. Its Label is empty, so it draws its Text.

         71 00 01 10 00 44 65 61 64 00 00 01 68 65 6c 6c
         6f 20 61 6c 6c 00 00 00 00 00 00 00 00 00 00 00
         00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
         00

   * One pie for `BLOCK` whose slice 0 sends `/place %r` drawn as `Place` with a tooltip, slice 1 has no Text and is drawn as `Airstrikes: 2`, slice 2 sends `/pingmark %h Enemy` drawn as `Mark`, and three blank slices.

         71 00 01 01 00 42 75 69 6c 64 00 00 00 2f 70 6c
         61 63 65 20 25 72 00 50 6c 61 63 65 00 42 75 69
         6c 64 73 20 77 68 65 72 65 20 79 6f 75 20 6c 6f
         6f 6b 00 00 00 00 41 69 72 73 74 72 69 6b 65 73
         3a 20 32 00 00 00 00 2f 70 69 6e 67 6d 61 72 6b
         20 25 68 20 45 6e 65 6d 79 00 4d 61 72 6b 00 00
         00 00 00 00 00 00 00 00 00 00 00 00 00 00 00

3. Hold **G** (the `pie_menu` binding) to open. The pies offered are the ones whose Contexts bit matches how the menu was opened — what the aim meets first, so `BLOCK`, `TEAMMATE`, `ENEMY` or `SKY`, or `SPECTATOR` and `DEAD` while the player is one — in the order sent; **G** again cycles them, and the pie last used in that context reopens. Slice `0` is at the top and the rest run clockwise. Releasing **G** or clicking with the crosshair in the centre chooses nothing; **LMB** sends the highlighted slice and **RMB** cancels.
4. Check the sending: every slice sends its Text as ordinary chat, `GLOBAL` sending to the whole server, and the client interprets none of it — a Text that starts with `/` is a command for the server to read, on either channel. Marking a place is no action of its own: a slice that wants one sends the [Teamplay](https://github.com/zerospades/aosprotocol/blob/master/extensions/teamplay.md) `/pingmark` command, as in `/pingmark %h Enemy`. A blank slice, whose Text and Label are both empty, draws nothing and cannot be chosen, whatever its Tooltip says.
5. Check the substitutions in a Text: `%p` is `#` and the id of the player the aim meets before any block, `%b` the `x y z` of the first solid block it meets, `%h` the `x y z` of the point the aim first met (a block or a player), `%r` the camera position and view direction, and `%%` a percent sign. An empty token leaves its argument missing, so `/votekick %p` with no player aimed at sends `/votekick `. Only the Text is expanded, once, immediately before sending; Theme, Label and Tooltip are shown as sent.
6. Send a Menu that runs past its own end, leaves bytes after the last pie, or carries a malformed UTF-8 string behind `0xff`: the client logs a refusal and the previous menu stands — it never disconnects and never applies half a menu. A Menu sent while the menu is held changes nothing until it is opened again.
7. Send a pie for `SPECTATOR` (`0x08`) and confirm it opens while spectating, one for `DEAD` while waiting to respawn, and one for `SKY` (`0x20`) aimed at open sky; the client's own menu has no pies for any of them. Confirm **Map Start** and **Player Left** keep the menu (it belongs to the connection), and that reconnecting brings the built-in menu back.
