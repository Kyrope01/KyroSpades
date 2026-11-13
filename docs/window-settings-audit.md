# Window and settings review

## Window size and settings commits

- The stale windowed-size issue was real. The cache now starts with the configured logical window size and is updated from GLFW **window-size** callbacks or SDL window-size events while windowed. It is not populated from `reshape`: that callback receives drawable pixels, which can differ from window points on HiDPI. Pending size edits are not overwritten by unrelated manual resize events. Whenever config is saved it now records the logical windowed size, including while windowed; a manual resize alone does not trigger a disk write.
- `Game width` and `Game height` now use separate logical-point menu values rather than `settings_tmp.window_width/height`. Those struct fields still track the live drawable for layout/rendering. Fullscreen reshape events therefore cannot replace the windowed target, while a valid edit made in fullscreen is saved and restored on exit. Values below 1 are clamped on commit.
- Both backends avoid fullscreen and resize operations when the current state/size already matches. Pending SDL application reads the latest cache, so a manual resize that arrives after an apply was queued is not reverted. GLFW still handles monitor/mode absence, failed transitions and Wayland placement. SDL detects failed fullscreen transitions.
- Settings edits are applied and saved after the left-button drag ends, rather than remeshing and writing config every drag frame. Leaving the settings HUD commits pending changes even if a drag is interrupted. This changes preview behavior: most settings apply on release, not continuously. Keyboard text input can still commit on each edit frame. Tests do not measure actual disk or mesh latency.
- F11 also updates the working fullscreen setting so an open settings menu doesn't undo the toggle.

## Remaining numbered findings

1–3. GLFW joystick names are null-safe, SDL clipboard text is freed on replacement and shutdown, and the previous GLFW gamepad snapshot changes only after a successful poll. A disconnect from an unrelated joystick no longer clears the selected one. Held gamepad actions on actual disconnect are not explicitly released; a dedicated unified physical/gamepad input-state fix is still needed.
4, 6, 7. Addressed by the transition and size handling above. Fullscreen failure settings are corrected in memory; the config written before an unsuccessful deferred transition is not immediately rewritten.
5. `glfwWindowHint(GLFW_SAMPLES, ...)` after window creation did not change the default framebuffer and has been removed from `window_apply`. The review's blanket "zero visible effect until restart" is not true for the off-screen scene: `postprocess_msaa_configure` uses the current setting and can reconfigure an MSAA scene target live when postprocessing and support are available. Default-framebuffer MSAA still needs a restart; the menu help now says so. No context recreation attempted.
8. No touch aim-finger change, as the review itself withdrew this claim.
9. Unknown GLFW and SDL mouse buttons are ignored rather than falling through to the primary button.
10. Android's standard NDK defines `__linux__`, and `common.h` maps it to `OS_LINUX`; with `USE_TOUCH`, it already used `sysconf`, so the claimed unconditional single-core result is not established. The touch/Linux query now uses online cores, handles errors, and bounds the conversion. Android runtime not tested.
11–13. Both key-name implementations guard null/zero-size output; title formatting uses the full buffer; the Wayland keymap guard is commented.
14. Windowed-size tracking and backend apply paths were clarified without a broad cross-platform refactor. Existing backend-specific transitions remain separate.
15. Fractional touch-chat swipe accumulators still persist across sessions; deferred because their gesture state/lifetimes are separate from window resizing.

The hidden-setting string comparisons and `format_comma` were not changed: the former is menu-only and unprofiled, the latter's 32-byte temporary is sufficient for `int`. No claim is made that all `window.c` platform behavior has been exhaustively audited.

## Verification and limits

Fake SDL and GLFW tests exercise a manual resize, queued-apply race, repeated settings applies, fullscreen editing/round-trip, transition failure and 2× drawable scale; fake input tests exercise failed gamepad polls, unknown mouse buttons and clipboard ownership. A settings-commit test covers logical sizes, repeat commits and remesh decisions. Actual SDL/GLFW headers were used for syntax checks (GL is stubbed). No real window manager, GPU, Android/iOS or controller was available; X11, native Wayland, desktop HiDPI and mobile touch transitions still need hands-on testing. Mobile window sizing is platform-controlled; Android continues to use its status-bar path rather than desktop window sizing.
