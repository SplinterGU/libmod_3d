/*
 * libmod_3d_sky.h - Skybox / sky dome
 *
 * Renders a background sky: either a procedural vertical gradient (zenith ->
 * horizon, with a soft sun glow taken from the scene's directional light) or an
 * equirectangular panorama texture sampled by view direction.
 */

#ifndef __LIBMOD_3D_SKY_H
#define __LIBMOD_3D_SKY_H

#include "libmod_3d_camera.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Procedural gradient sky: top (zenith) and horizon colours. Disables any
   panorama texture previously set. Also enables the sky. */
void g3d_sky_set_gradient(float tr, float tg, float tb,
                          float hr, float hg, float hb);

/* Use an equirectangular panorama texture (GL handle) instead of the gradient.
   Pass 0 to fall back to the gradient. Enables the sky. */
void g3d_sky_set_texture(unsigned int gl_handle);

/* Animated procedural clouds on the gradient sky. amount 0=clear..1=overcast, speed=wind. */
void g3d_sky_set_clouds(float amount, float speed);

/* World-space LOW cloud layer (fly-through, depth-aware, casts shadows on the scene).
   cover 0=off..1=overcast, base=altitude of the layer bottom, thick=vertical thickness. */
void g3d_sky_set_low_clouds(float cover, float base, float thick, float speed);

/* Accessors used by the renderer's volumetric pass and the scene shader (cloud shadows). */
int  g3d_sky_low_clouds(float *cover, float *base, float *thick, float *speed);
void g3d_sky_get_sun(float dir[3], float col[3]);
void g3d_sky_get_ambient(float amb[3]);

/* Enable / disable sky rendering. */
void g3d_sky_set_enabled(int enabled);

/* Render the sky as the scene background. Called after the opaque forward pass
   so it only fills pixels with no geometry. flip_y matches the GRAPH-FBO flip
   used by the rest of the renderer. */
void g3d_sky_render_pass(G3DCamera *camera, int flip_y);

/* Draw the sky with an explicit orientation/projection into the bound
   framebuffer (no depth state). Used by the IBL capture to render the sky into
   the 6 faces of the environment cubemap. Returns 0 if the sky isn't
   initialised yet (the caller should retry rather than bake a black env). */
int g3d_sky_render_env(Mat4 view_rot, Mat4 proj);

/* Free GPU resources. */
void g3d_sky_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SKY_H */
