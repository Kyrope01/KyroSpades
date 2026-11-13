# Chunk and channel review

## Fixed

- Work packets carry the generation captured by the main-thread enqueue operation; workers no longer read a chunk generation concurrently with full rebuilds. Old-generation results cannot be accepted as current merely because the worker started late. Bulk progress is decremented only for current-generation bulk work, not old results or ordinary block updates. Pending block edits get the current generation when drained to the work queue.
- Result draining now uses a mutex-protected nonblocking channel pop. The original result queue has one consumer in the current architecture, so the reported size/pop race was not shown to deadlock today; the nonblocking operation removes that assumption and avoids a separate size check. `channel_size` now reads under the mutex rather than mixing an atomic load with non-atomic writes to the same count.
- Channel capacity growth is checked for overflow and allocation failure. Previously a failed grow could overwrite a full buffer, overrun its logical count, and assert or corrupt the queue. The existing API has no error return, so failure now logs a fatal error and exits instead of silently losing work. Initial capacity multiplication is also checked.
- Block-update coordinates are validated before indexing the fixed chunk array. Two greedy-mesher loop counters now use `int` instead of bytes.

## Review qualifications

- Minimap memory is already zero-initialized with `calloc` and allocation failure is checked using the project's fatal-allocation policy. Zero means transparent black, not uninitialized data. Choosing a different minimap background would be a visual design change.
- Full rebuilds already enqueue each perimeter chunk once. Worker creation is checked, zero-worker startup fails explicitly, and workers are joined during teardown; they are not detached. Work channels dynamically grow, not fixed-capacity. Current VXL maps load as 512×512×64, so the reported >255 loop wrap and >32767 vertex range aren't reachable with normal maps, although the counters were corrected defensively. Taller/larger map support would require an end-to-end format and renderer audit.
- The baked-shadow read lock can delay writers and should be profiled. `map_sun_shadow` can traverse up to 32 cells beyond the current chunk; snapshotting just the current chunk would change shadows at chunk boundaries. A correct snapshot needs an appropriate halo/world view, invalidation and memory/latency measurements. A hidden-water bitmap adds allocation and memory traffic; no speedup is assumed without profiling.
- Full-generation IDs alone do not order multiple updates to the **same** chunk within a generation. Concurrent workers can finish snapshots out of order; per-chunk revisions and a scheduling/coalescing policy are a separate correctness/performance task. Worker reads of mutable settings, map replacement, and GL/device behavior still need integrated stress testing. The 50 ms bulk budget is a responsiveness/throughput tradeoff, not an FPS guarantee.
- The reviewed report ended mid-item 12; anything not included was not assessed.

Tests exercise pthread producers/consumers, nonblocking empty reads, channel overflow/failure behavior, generation accounting, invalid coordinates and repeated worker startup/shutdown. Syntax checks use stub GL headers; there was no real GPU, Android, multiplayer, or FPS benchmark.
