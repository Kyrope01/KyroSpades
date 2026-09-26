/*
 * Forward lighting for terrain and programmable models.
 *
 * The renderer supports GLSL 330, desktop GLSL 120, and OpenGL ES 2.0
 * variants. Unsupported legacy contexts keep their established renderer.
 */
#ifndef LIGHTING_H
#define LIGHTING_H

#include <stdbool.h>

/* ES 2.0 only guarantees 16 fragment-uniform vectors.  Four lights use eight
 * vec4 uniforms and leave enough room for fog, texture and material state. */
#define LIGHTING_SHADER_LIGHTS 4

/* Compile/destroy the world-lighting shader. A failed shader compile is not
 * fatal: the renderer keeps its established terrain path. set_enabled() supports
 * applying the user-facing toggle without a restart. */
bool lighting_init(void);
bool lighting_set_enabled(bool enabled);
void lighting_deinit(void);
bool lighting_supported(void);
/* The Core world shader can remain active for live directional shadows even
 * when the point-light master is off. */
bool lighting_world_supported(void);

/* OpenSpades-style local flashlight. It starts off, toggles with the user's
 * control binding, and only emits while update() marks the local view usable. */
bool lighting_flashlight_toggle(void);
bool lighting_flashlight_enabled(void);
void lighting_flashlight_reset(void);
void lighting_flashlight_update(float dt, bool usable,
                                float x, float y, float z,
                                float direction_x, float direction_y, float direction_z);

/* Add a short-lived muzzle/explosion point light. Calls are safe when dynamic
 * or flash lighting is disabled or unsupported; disabled calls are ignored.
 * Color and intensity are linear multipliers, and radius is in world blocks. */
void lighting_add_flash(float x, float y, float z, float red, float green, float blue,
                        float radius, float intensity, float duration);

/* Submit a moving or otherwise transient light for the next frame only. The
 * bounded queue is consumed by lighting_prepare_frame(), so callers can update
 * positions without allocating persistent flash entries or leaving trails. */
void lighting_submit_point(float x, float y, float z, float red, float green, float blue,
                           float radius, float intensity);

/* Select the most relevant active and submitted lights for this frame. */
void lighting_prepare_frame(double now, float camera_x, float camera_y, float camera_z);

/* Upload this frame's lights and sunlight to a programmable model shader that
 * uses the shared u_LightPositionRadius/u_LightColorIntensity contract. */
void lighting_apply_program(unsigned int program);

/* Bind the forward-lighting shader around terrain rendering.  Returns true
 * when a shader was bound.  The caller may invoke update_matrices after every
 * model-matrix change, then must call lighting_world_end(). */
bool lighting_world_begin(bool textured);
void lighting_world_update_matrices(void);
void lighting_world_end(void);

#endif
