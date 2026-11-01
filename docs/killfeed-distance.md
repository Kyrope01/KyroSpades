# Killfeed distance

Every kill line in the killfeed can say how far the shot came from:

```
Proman killed Zero (Rifle) from 42.5 blocks
Proman killed Zero (Shotgun Headshot) from 6.1 blocks
```

On by default, off with `killfeed_distance = 0` in `config.ini` or **HUD/UI
Settings → Killfeed distance**.

## Which distance, and why only one

ZeroSpades prints a distance in its *hit log* (`cg_hitLog`: `You hit X from: N
blocks [HEAD]`), and that is the only distance measure in either client today.
This feature reuses its formula rather than inventing a second one, so the two
clients report the same number for the same shot:

    distance = |killer.eye - victim.eye|          (3D, straight line)

The alternatives were considered and dropped, which is why there is no setting
to switch between them:

* **Flat (X/Z) ground distance.** Loses the height difference, so a shot from a
  tower reads the same as one at street level, and the number stops meaning
  "how far the bullet travelled".
* **Distance to the block that was hit.** The server's `KillAction` carries no
  hit position, and the client's own predicted hit point only exists for the
  local player's hitscan — it cannot be filled in for someone else's kill.
* **Distance along the map / line of sight.** Needs a raycast per kill, answers
  a different question ("is there a wall between us"), and would disagree with
  the kill's own geometry when the shot curved over cover.

Eye-to-eye is also cheap: both eyes are already simulated every tick and are
interpolated for rendering, so the readout costs one square root per kill.

## Rules

* Applies to **every real kill**, not just the local player's: a spectator
  reading the killfeed gets the same information.
* Not applied to `KillTypeTeamChange` / `KillTypeClassChange` — they arrive as
  kill packets but nothing travelled and the "killer" is the player themselves.
  Suicides and falls (killer == victim) are skipped by the same check.
* If either player has no position yet (their eye still sits at the origin, i.e.
  the server has not reported them since they connected), no distance is
  appended. A wrong number is worse than none.
* The value is the positions the client holds when the kill packet arrives,
  which is the closest thing to "where they were when it happened" that a client
  is given: the server sends no per-kill geometry.

## Colours

The killfeed used to paint everything red except the names. Now:

| Line | Words | Names |
|------|-------|-------|
| you made the kill | green | team colours |
| you were killed | red | team colours |
| someone else's kill | white | team colours |
| team / weapon change | white | team colours |

Green wins over nothing and red wins over green, so a suicide (a fall, say) reads
as your death rather than as a kill. Team and weapon changes arrive as kill
packets but are not kills, so they stay white. The little side bar on each entry
follows the same rule; for other players' kills it keeps the killer's team
colour, and a killer on a team with no gamestate colour (zombies, any custom
team id) now gets a white bar instead of being dropped from the feed.

The codes used are the ones the chat renderer already understands (`\5` green,
`\4` red, `\6` white — the palette is in `hud.c`), and
`tests/killfeed_color_test.py` parses that palette so the module and the renderer
cannot drift apart.

## Code

`src/killfeed.c` holds both the maths and the formatting
(`killfeed_distance()`, `killfeed_append_distance()`), so
`read_PacketKillAction()` in `src/network.c` only supplies the two eye positions
and the setting gate. `tests/killfeed_distance_test.py` compiles the module and
checks the distance maths (including the height term and the missing-position
guard), the `from N.N blocks` formatting, truncation on a short buffer, and the
wiring in `network.c` and the settings.
