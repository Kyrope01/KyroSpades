# Cross-file review follow-up

## Confirmed and fixed

- **Chunk worker lifecycle:** workers are now joinable. Shutdown first stops the map-collapse producer, clears pending chunk work, sends one stop packet per worker, joins them, drains results, and only then destroys chunk queues, GPU lists and shared map/water resources. Worker creation failures are checked; failure to start any worker is fatal rather than leaving queued jobs with no consumer.
- **Chunk rebuild work:** concentric rings now queue each chunk once rather than requeueing corners. Minimap buffers start zeroed, so empty columns cannot contain heap garbage. The existing reverse traversal still deliberately lets the topmost block win.
- **Map concurrency:** the collapse worker really does write/reallocate libvxl block arrays. Public block and height reads now take the read lock; callers already holding it can use the `_nolock` functions. This favors correctness over the prior lock-free Windows optimization and needs profiling. The top-of-map sunlight loop no longer probes an extra row; libvxl itself bounds-checks that case, so it was not the suggested out-of-bounds crash.
- **Batch program state:** a 2D flush restores the previous shader program while its re-entry guard is still active. It also resets GLX attribute-shadow state and requested line width at teardown.
- **KV6 lighting:** mesh KV6 face colors had alpha zero, which the default lit shader interprets as the glowing-block marker. They now carry opaque alpha, allowing normal model lighting instead of the emissive bypass. This revises the earlier GLX audit's assumption that only terrain used the marker. The preview comment was corrected as well.
- **Server browser:** version strings are bounded; malformed/non-array and empty lists no longer cause an allocation failure, and missing or null JSON strings are safely copied as empty strings. The minimap's per-frame diagnostic `glGetError` was removed.
- **Main loop:** both `aos://...` and the documented `-aos://...` connect forms retain the full address; RPC state initializes before the server browser; fixed physics catch-up is capped to avoid an unbounded hitch; frame timing uses one clock read and avoids division by zero when reporting FPS.

## Findings checked but not changed

- The reported lost pointer stars/operators in chunk max-height, falling-block delta and chat message parsing are transcription artifacts; the source has the dereferences.
- Interpolation end is safe without begin: it checks `interp_active` before touching player positions.
- On Core/ES, `matrix_upload()` updates the currently bound world's `u_MVP` and `u_Model`, including wrapped terrain copies. The extra call in chunk.c is needed for the compatibility shader path.
- The report's claim that a full chunk channel blocks is inaccurate: `channel_put` grows the queue. Worker-start failures were still worth handling. The recommended extra `#error` is unnecessary because common.h enables programmable mode on Core.
- Most GLX findings 1, 2, 4–6, 9–10, 13 and 15–18 were already addressed in the preceding GLX follow-up. Compiled legacy OpenGL display lists execute `glDrawArrays` during compilation and record the geometry, rather than retaining the client arrays for subsequent draws; a real compatibility-driver test is still advisable.

## Further work

Falloff formulas differ across shader paths; changing them will change appearance and should be compared on actual Core, compatibility and mobile GPUs. The overloaded emissive marker still deserves a dedicated material representation for genuinely translucent lit geometry. Full render-settings snapshots, bulk-map locking/notification design, cached uniform state, shader precision and the suggested larger performance changes require separate concurrency/visual tests and benchmarks. Other context-owned resources also need an end-to-end context-loss audit; the batch fix alone does not make all renderers context-loss-safe.

Validation includes fake-GL program-state tests, actual pthread/channel queue and shutdown tests, JSON parser checks, source integration checks, and the Python regression suite. It does not replace an in-game multiplayer/server-browser test, a real GPU visual check, a Windows contention benchmark, or a native production build.
