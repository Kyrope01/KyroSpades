# Skins review

1. `SKIN_NAME_MAX` is currently 48, so `hex` has 97 bytes and `%96s` fits; the proposed current overflow is not present. It would overflow if the constant were reduced. Parsing now uses a checked token length derived from the actual array, rejects oversized tokens rather than accepting a truncated prefix, and uses `strtol` with range checking for the category. `%s` stops at the terminating NUL; it does not consume it as input. The loader accepts the existing lowercase hex encoding and `-` empty suffix.
2. Both default category allocations are now checked. On failure the category is empty and later categories still scan; selection/preview callers already reject indices outside `count`, and the menu checks `count > 0`. A failed rescan does discard that category's old list; it does not change the already loaded live model.
3. Previews now save the view matrix, render with an identity view and restore view, model and projection. The previous behavior depended on the caller's view being identity.
4. `texture_create` returns **1 on success**, 0 on failure. Scope loading now creates a temporary texture first, tries the default on a failed custom decode, and keeps the previous sight if both fail. This avoids `texture_delete`'s dangling fields and avoids double deletion. GL upload failures are not reported by `texture_create`, so this does not cover every possible driver failure.
5. Flip is applied by the render matrix, not baked into the VBO. The current toggle updates the preview and, only for the selected entry, the live model; reloading reapplies the preference. No behavior change needed.
6–7. The per-scan capacity and `file_load`'s writable, NUL-terminated heap buffer are documented. `file_load` actually allocates `size + 1` and frees normally.
8–9. Extracted the existing sound-suffix policy and renamed the boolean-like models parameter. Rifle reload variants still use the legacy registry and all sounds retain their existing fallback behavior.
10. Named player model indices are now shared by the category table and preview placement; the same four standing parts and offsets are rendered.
11. Empty-category settings clamp to zero and apply safely rejects the entry; the UI does not index it while count is zero.
12. Removal of the last flip preference uses a valid one-past pointer with a zero-length `memmove`; `flip_find` provides a valid index.
13. `file_printf` and `file_close` return void, including on Android; neither reports write or close failure. Atomic saving requires portable checked write/close/rename APIs and is deferred. A disk-full write can still truncate preferences.
14. A custom skin named `Default` remains selectable, but is displayed as `Default (custom)` so it cannot be confused with the built-in entry. Its filename suffix remains unchanged.
15. Live model reload skips a null model pointer. Existing category definitions always provide their model pointers.

## Verification and limits

All 90 Python regression scripts passed. Extracted production C tests cover a 16-byte name limit, long/invalid flip lines, numeric overflow, scan allocation failures and recovery, corrupt custom/default texture fallback with fake textures, sound suffix rules, preview matrix save/restore with a nonidentity view, and custom Default discovery. The Core source syntax check used incomplete GL/cglm stubs, not a full build. No actual GL context, corrupt-PNG driver upload, Android assets, or interactive skins menu was tested.
