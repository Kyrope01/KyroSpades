# Dormant gameplay sources

`gmi.c`/`gmi.h` contain an unfinished text-message-based gamemode detector; no active translation unit includes `gmi.h` and CMake does not compile `gmi.c`. They are retained as reference code, not as a supported gameplay path.

`damagenum.c`/`damagenum.h` are an alternative damage-number implementation. The built client instead uses `damagenumbers.c`/`damagenumbers.h`; no active source includes `damagenum.h`, and CMake does not compile `damagenum.c`. Keep the alternative out of the build unless its API and ownership are deliberately integrated and verified. It does not define the active renderer's global, so its exclusion is not a workaround for a known duplicate symbol.

Neither dormant implementation receives runtime bug fixes while excluded from the build. Do not add them to `CLIENT_SOURCES` without reviewing their call sites, lifetimes and platform builds first.
