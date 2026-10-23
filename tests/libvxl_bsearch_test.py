"""Compile libvxl with a fortified-style bsearch macro and exercise lookups.

Ubuntu 26.04 exposes bsearch through a function-like macro in optimized
builds. A compound literal passed directly as a macro argument has its commas
parsed as argument separators, so this compiles the real implementation with
an equivalent macro and then checks map lookup/removal behavior.

Run: python3 tests/libvxl_bsearch_test.py
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]

shim = r'''
#ifndef LIBVXL_TEST_BSEARCH_SHIM_H
#define LIBVXL_TEST_BSEARCH_SHIM_H
#include <stdlib.h>
static inline void* libvxl_test_bsearch(
    const void* key, const void* base, size_t nmemb, size_t size,
    int (*compar)(const void*, const void*)) {
    return bsearch(key, base, nmemb, size, compar);
}
#define bsearch(KEY, BASE, NMEMB, SIZE, COMPAR) \
    libvxl_test_bsearch((KEY), (BASE), (NMEMB), (SIZE), (COMPAR))
#endif
'''

harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "libvxl.h"

int main(void) {
    struct libvxl_map map;
    assert(libvxl_create(&map, 4, 4, 8, NULL, 0));

    libvxl_map_set(&map, 1, 2, 7, UINT32_C(0x00112233));
    assert(libvxl_map_get(&map, 1, 2, 7) == UINT32_C(0x00112233));

    libvxl_map_set(&map, 1, 2, 6, UINT32_C(0x00445566));
    assert(libvxl_map_get(&map, 1, 2, 6) == UINT32_C(0x00445566));
    libvxl_map_setair(&map, 1, 2, 6);
    assert(!libvxl_map_issolid(&map, 1, 2, 6));
    assert(libvxl_map_get(&map, 1, 2, 6) == 0);

    libvxl_free(&map);
    puts("libvxl bsearch calls work with fortified-style macro expansion");
    return 0;
}
'''

with tempfile.TemporaryDirectory() as directory:
    build = Path(directory)
    shim_path = build / "bsearch_shim.h"
    harness_path = build / "harness.c"
    object_path = build / "libvxl.o"
    executable = build / "libvxl_bsearch_test"
    shim_path.write_text(shim)
    harness_path.write_text(harness)

    flags = ["-std=c99", "-O2", "-Wall", "-Wextra", "-Werror"]
    subprocess.run([
        "cc", *flags, "-I" + str(root / "deps/libvxl"),
        "-include", str(shim_path), "-c", str(root / "deps/libvxl/libvxl.c"),
        "-o", str(object_path),
    ], check=True)
    subprocess.run([
        "cc", *flags, "-I" + str(root / "deps/libvxl"),
        str(harness_path), str(object_path), "-lm", "-o", str(executable),
    ], check=True)
    subprocess.run([str(executable)], check=True)
