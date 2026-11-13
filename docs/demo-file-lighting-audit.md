# Demo, file and lighting review

## Changes

* Demo recording stops rather than writing a 16-bit packet length followed by a larger payload. It also stops on write errors. Demo loading rejects truncated records, empty payloads, non-finite, negative or decreasing timestamps, and capacity overflow; partial packet allocations are freed on failure. Non-finite seek and speed inputs are ignored. Map snapshots use the timestamp of the StateData being dispatched, not the later frame clock.
* File loading opens once, verifies length and reads, closes handles on all paths and returns NULL on failure. `file_load_sized` couples bytes with their length; binary consumers no longer size a separate open. Empty files are allocated and NUL terminated. Android formatted writes use dynamic allocation and a copied argument list. Buffer reads avoid signed shift and unaligned/aliasing float access. Directory creation checks that EEXIST is actually a directory.

## Review qualifications and remaining risks

* Flashlight IDs are bytes and arrays have 256 entries, so the reported 32-element index overflows do not apply. ID 255 also has a config sentinel role; changing limits needs protocol compatibility testing. Flicker behavior was not redesigned.
* Glow record reallocation already uses the correct element size. The fixed pending-placement table deliberately evicts the oldest entry when full; bursts can lose a glow confirmation. Light collection has a fixed output cap and registry traversal holds a mutex. These need measured workload and a bounded policy or data-structure redesign, not an untested silent change.
* A map snapshot contains only VXL bytes, not player, extension, Teamplay or chat state. Starting replay at its packet index would omit that state. Existing backward replay from the beginning has multi-map correctness limitations: earlier map mutations may be applied to a later restored map. The transfer flag and map-transition behavior need an integrated replay/state checkpoint design and tests; no shortcut was applied.
* The demo container checks do **not** validate the payload schema of every network packet. Network handlers and VXL decoding must be audited/hardened before treating untrusted demos or maps as safe. Demo preloading and snapshot storage remain unbounded for large inputs, and malformed/truncated recordings now fail rather than playing their intact prefix.
* Android JNI asset-manager exception handling, synchronization and global-reference lifecycle still warrant an Android-specific audit. Android I/O and gameplay have not been tested on a device. File APIs remain int-limited to INT_MAX bytes. Writes that fail after a partial record may leave a truncated demo, which the loader rejects.
* No native full build or in-game test was available in this environment. Tests below use desktop source fragments/stubs and cannot verify Android, GPU, or live server behavior.
