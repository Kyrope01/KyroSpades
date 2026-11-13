# Main branch merge resolution

Merged `main` commit 6c5476a into the audit branch, preserving changes from both sides:

- The main loop keeps main's pace-then-poll-then-update-then-present order and its optional low-latency present setting. It also retains the audit's nonnegative frame delta and guarded FPS calculation; the old end-of-frame software pacing is removed to avoid pacing twice.
- GLFW retains both the focus-gain cursor recapture from main and logical window-size tracking from the audit. SDL keeps the split poll/present and window-size handling.
- Sound and camera use the existing audited range-limited terrain-only ray, including the actual distance for each of the 27 source samples. `camera_block_hit` remains available as a compatibility wrapper for main's API, using the same validated traversal. Both approaches avoid player hitbox tests and long terrain scans.
- Both branches' terrain, sound, window and input tests were retained and updated to exercise the integrated code rather than the superseded names or loop structure.

Verification: all 94 Python regression scripts passed. GLFW, SDL and SDL+touch window source syntax checks used actual backend headers and stub GL; camera syntax was checked for GLFW and SDL. No real GPU, window manager, audio driver or live multiplayer session was available. The optional real-FFmpeg recording timing test needs a configured FFmpeg build and is skipped in the general run.
