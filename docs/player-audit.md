# Player review

## Changes

- ESP's ray now stops at the sampled target point. Previously, if the ray missed the target's animated hitboxes, terrain behind the target could make it appear obscured. The ray still excludes the local player and still considers other players before terrain; switching to terrain-only would change that behavior. This remains a single-point approximation, not a full visibility test.
- Removed the unreachable tool-translation branch: it tested `render_fpv` inside the `else` of `if(render_fpv)`. This does not restore a third-person animation; doing so would change presentation and needs visual validation. The live speed calculation now squares velocity components directly.

## Findings and deferred changes

- Collision uses fixed 512-by-512-by-64 geometry and the 63 coordinate transform. The allocated map and surrounding movement code use that same convention; the underlying libvxl solid lookup handles vertical bounds and wraps horizontal coordinates. There is no demonstrated out-of-bounds crash from this alone. Supporting arbitrary map dimensions would require a coordinated change, not just changing this function.
- `player_move` computes a fall-damage value, but its caller discards it. Landing sounds and movement effects still occur; authoritative health and damage are server-driven. Applying local health damage or sending a new packet without verifying protocol semantics risks double damage.
- Spade RMB starts a wind-up on the first timer expiration, then acts after another second while held; it does not require a second press. LMB and RMB share a timer, so repeated LMB can prevent RMB from advancing, and resetting the timer on release has early-uptime quirks. Changing this state machine can affect cadence and server compatibility and needs protocol/gameplay testing.
- `camera_hit_fromplayer` already bounds terrain traversal to the requested range. A terrain-only ESP ray would also remove player occlusion; not adopted.
- The dead `esp_active` paths in the regular model renderer are remnants of an older ESP implementation; current boxes are drawn separately. Removing all the old rendering state code is a larger cleanup, not necessary to fix the unreachable translation.
- Casing positions are needed for remote as well as local players. Restricting their calculation to the local player breaks remote ejection.
- `game_time()` returns float; changing only a destination to double cannot recover lost precision. `os_last_time == 0` is a sentinel, not an approximate equality check.
- Other render/style optimizations were not assumed beneficial without profiling. None of the visual or gameplay behavior above was tested on a live server or GPU.

## Verification

The Teamplay extracted-C regression covers a nearer wall, a wall beyond the sampled target, no wall, and local-player exclusion. It uses a fake hit routine, not real geometry or a graphics context. Full Python regression scripts were also run; see the task report for results. Native build and interactive testing require desktop development dependencies and a working game environment.
