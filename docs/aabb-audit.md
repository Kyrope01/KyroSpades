# AABB review

## Changes

- Terrain queries clamp their lower y bound to zero even for a negative `miny`; all current callers pass zero. The query rejects non-finite or inverted boxes and clamps finite coordinates before converting to integers. Only in-map voxels are queried. **For hypothetical negative-`miny` callers this deliberately changes behavior:** the old path could treat below-map space as solid.
- Terrain bounds now enumerate exactly the voxels whose closed unit cubes can touch the box. Integer-boundary contacts remain inclusive, including faces, edges and corners. A single map read lock covers the query rather than a lock per voxel, yielding a consistent snapshot and fewer lock operations. For example, an empty 0.8-unit box wholly within one voxel probes one voxel instead of 27; a point on an interior voxel corner still probes eight. The existing x/z/y iteration order is retained; changing it does not universally help and could harm column locality.
- Ray inputs are const-qualified and validated. A zero direction now returns no hit rather than a spurious hit; non-finite/inverted inputs return no hit. Double-precision subtraction and length calculation avoid float underflow/overflow for very small or large finite directions. Zero-length point tangencies are rejected; a ray traveling along a face for a positive interval remains a hit. Box/box contact remains inclusive. The unused empty render stub was removed.

## Review qualifications

- The claimed out-of-bounds crash is not established: `map_isair_nolock` converts the vertical coordinate and `libvxl_map_issolid` handles a vertical coordinate outside map depth without accessing geometry. A negative y could give an unintended solid result, but all current callers use `miny = 0`, and zero already clamps the old lower bound to zero.
- Treating every direction component below a fixed epsilon as zero is not safe for scale-independent rays: a ray with direction `1e-30` can still hit at a finite world distance. The double slab calculation handles such values, and the test checks this case. Sound probes and other callers use non-unit rays, so removing the direction-length factor would change distance semantics.
- Tight `floor(min)`/`ceil(max)` bounds from the report would miss exact face contacts; the new formulas preserve them without an arbitrary epsilon. The original y-inner loop checks a first y immediately, not only after completing the x/z loops. No chunk-level skip or inferred chunk layout was added.
- The terrain query has no acceleration for large boxes, and the longer-held read lock can delay map writers on large scans; production profiling is needed. The code assumes callers don't already hold the map write lock. Invalid boxes return no collision, rather than guessing intended sizes; callers still need valid physics state. Setters retain their existing negative-size behavior.

`tests/aabb_audit_test.py` compares 50,000 seeded terrain queries to a full-map closed-AABB oracle and covers bounds, exact contacts, non-finite/huge values, ray scale and lock cleanup under UBSan. These checks and stub syntax builds are not actual gameplay, GPU, FPS, or concurrency benchmarks.
