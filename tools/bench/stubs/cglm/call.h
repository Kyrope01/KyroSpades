/* Minimal cglm stub for the headless CPU benchmarks.
 *
 * The engine links the real cglm (FetchContent). The benchmarks only compile
 * CPU-side modules, which need the matrix/vector *types* from cglm headers but
 * never call into the library (matrix work is stubbed out in bench_stubs.c).
 */
#ifndef BENCH_CGLM_STUB_H
#define BENCH_CGLM_STUB_H

typedef float vec2[2];
typedef float vec3[3];
typedef float vec4[4];
typedef float mat4[4][4];

static inline void glmc_mat4_identity(mat4 m) {
        for(int i = 0; i < 4; i++)
                for(int j = 0; j < 4; j++)
                        m[i][j] = (i == j) ? 1.0f : 0.0f;
}
static inline void glmc_mat4_mul(mat4 a, mat4 b, mat4 d) { (void)a; (void)b; (void)d; }
static inline void glmc_mat4_mulv(mat4 m, vec4 v, vec4 d) { (void)m; (void)v; (void)d; }
static inline void glmc_mat4_copy(mat4 src, mat4 dst) { (void)src; (void)dst; }

#endif /* BENCH_CGLM_STUB_H */
