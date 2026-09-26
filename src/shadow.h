/* Directional terrain shadows for the strict OpenGL Core renderer.
 * Legacy desktop, GLES, and unavailable GPU paths retain baked block shadows. */
#ifndef SHADOW_H
#define SHADOW_H

#include <stdbool.h>

/* Allocate/release the Core depth-map resources. Unsupported renderers expose
 * the same no-op API so callers do not need renderer-specific branches. */
bool shadow_init(void);
void shadow_deinit(void);
bool shadow_gpu_supported(void);

/* True when chunk meshing must bake directional shadows into vertex colours.
 * The Core renderer uses this fallback whenever its live shadow path is not
 * available (including when forward terrain lighting is disabled). */
bool shadow_baked_enabled(void);

/* Render nearby terrain into the sun's depth map when its cached coverage is
 * stale. Terrain geometry changes must invalidate the cache. */
void shadow_render(void);
void shadow_invalidate(void);

/* Bind/unbind the shared shadow map and upload its shader contract. Calls are
 * scoped and preserve the caller's active texture unit and unit-3 binding. */
void shadow_apply_program(unsigned int program);
void shadow_finish_program(void);

#endif
