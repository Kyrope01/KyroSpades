# GLX review follow-up

The external review identified several real issues. The following changes were made:

- Teardown discards queued 2D batches, disables batching and releases/resets its VBO before changing programs. This fixes GLX's own batch resource lifecycle; it does not establish that every other renderer subsystem supports full context recreation.
- A display list records whether it was created as a legacy list or a VBO and uses that backend for updates, draws and destruction regardless of later setting changes. It is zero-initialized, records its uploaded vertex format, and rejects an incompatible draw. The legacy client-pointer upload unbinds an existing VBO when VBO support is available. This setting applies to the non-programmable desktop backend; existing objects retain their backend until rebuilt.
- Explicit-attribute display-list draws now count their actual draw and pointer/enable/disable calls. Empty/invalid helper calls no longer inflate the affected counters; boundary batch flushes aren't erased by the frame reset. Batch uploads and attribute operations are also counted. The profiler's other counters are still manually instrumented and should not be interpreted as an exhaustive GL trace.
- Default-shader uniform helpers bind their own program before writing. An explicit-attribute draw with no current program binds the default and uploads current matrices on every programmable backend.
- A failing shader compile now aborts without attempting to link, cleaning up shader objects; program creation failure is handled too.
- Batches use neutral team tint for their draw and restore the default shader's tracked tint; byte colours clamp and round. Ring-segment steps are capped and its stack buffer has a fixed bound.
- Uniform lookups use a bounded replacement cache instead of scanning the whole table on every miss. The default fragment shader skips its texture fetch when texturing is disabled.

## Deferred / qualifications

- The alpha channel is deliberately used as the existing glowing-block marker in several shaders and by the mesher. Replacing it with a dedicated attribute/uniform is a cross-renderer format change, not a safe isolated edit. Its interaction with genuinely translucent lit geometry should be tested before redesigning it.
- Fragment precision changes require matching vertex/fragment varyings and actual GLES device/compiler testing. They were not made blindly; mobile light-falloff artifacts remain worth investigating on hardware.
- Shadowing all default uniforms is not currently safe: several other files write them directly. Centralize/invalidate all writes first, then benchmark. Per-list VAOs, staging uploads, streaming arenas and persistent mapping are larger, driver-sensitive changes; benchmark and test them separately. Existing VBO capacity retention intentionally avoids shrinking the allocated buffer, so its two orphaning branches are not literally equivalent.
- `OPENGL_CORE` is already made programmable by `common.h`; an additional error for that combination is unnecessary in the current build configuration.
- No direct `glUseProgram` or `glDeleteProgram` calls were found elsewhere in `src`; future direct calls would invalidate GLX's tracking.

Tests use a fake GL driver to exercise backend changes, cache collisions/deletion, draw accounting, type mismatch, batching tint and teardown. Source was also syntax-checked for Core and legacy configurations against the repository's benchmark GL stubs. These checks are not substitutes for a full native build, real GPU/GLES testing, context-loss testing or FPS benchmarks.

## Additional GLX report

- Desktop colour calls now update both tracked colour and actual fixed-function GL state. Previously direct `glx_set_color4f` did not update desktop GL, and ordinary desktop `glColor*` calls bypassed tracking, causing batched HUD quads to use stale tracked colour. The Core backend still uses tracked shader colour; ES 1 retains its real-GL dispatch.
- An explicitly enabled desktop `GLX_PROGRAMMABLE` build now sets and clears shader fog uniforms. Normal legacy desktop builds use fixed-function fog and Core/ES already had shader paths.
- Desktop fixed-function batch flush saves/restores client vertex-array state, including pointers, enables and buffer bindings, as well as texture enable state. GLES 1 does not have the desktop client attribute stack: its existing client-array state disturbance remains and needs a separate ES-specific save/restore design and device testing. Other immediate helper paths also have their own state conventions.
- Full-screen drawing requires a bound program rather than the unrelated default shader. Failed fullscreen VAO/VBO creation is cleaned up; failed streaming or batch VBO creation no longer issues invalid draw/upload calls. A failed batch allocation drops that batch, and future draws can retry; no GPU allocation recovery is guaranteed.
- Batch VBO cleanup and bounded ring storage were already present. The uniform cache is direct-mapped, not a 512-slot scan. The suggested tombstone early termination would be unsafe after deletion without preserving probe chains. Profiling counters remain partial, and buffer rings, persistent mapping (not a baseline GL 3.0 feature), and wider state shadows need benchmarks and proper invalidation around external GL calls.

The added fake-driver tests exercise desktop colour, failed and successful fullscreen allocations, custom-program fullscreen draws, desktop batch-state restoration and batch allocation failure. Fog branch checks and legacy/Core/explicit-programmable syntax checks are not real GPU coverage. No Android/GLES device, actual desktop context, or full native build was tested.
