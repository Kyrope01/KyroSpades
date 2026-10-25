# Zombie player models

These KV6 models are from [ZeroSpades](https://github.com/zerospades/zerospades),
`Resources/Models/Player/Zombie/`, upstream revision
`b3415494b984a80b2adecda16814e79caca5c842` (GPL-3.0-or-later).

KyroSpades loads `Dead.kv6`, `Head.kv6`, `Torso.kv6`, `TorsoCrouch.kv6`,
`Arms.kv6`, `Leg.kv6`, `LegCrouch.kv6`, `LegRight.kv6` and
`LegRightCrouch.kv6`. The separate arm pieces are kept with the model set for
the original segmented-arm renderer.

Players on a team named `Zombie` or `Zombies` (case-insensitive, surrounding
spaces ignored) use these models on servers that do not negotiate the Player
Outfit extension. KyroSpades also supports the current draft v1 Player Outfit
extension (`0x35`): outfit `1` (`Undead`) selects these models, and explicit
outfit packets take precedence over team-name detection. Other outfit IDs without
local art fall back to the regular player models. See the [extension proposal](https://github.com/zerospades/aosprotocol/pull/14).
