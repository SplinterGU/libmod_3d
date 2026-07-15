/*
 * libmod_3d_ibl.h - Image Based Lighting (real, from the scene's own sky)
 *
 * The PBR shader is Cook-Torrance GGX, but its environment used to be a
 * two-colour analytic gradient: metals and polished surfaces reflected a sky
 * that did not exist. This builds the real thing from the sky actually being
 * rendered (gradient + sun + clouds, or the panorama texture):
 *
 *   sky -> environment cubemap -> irradiance cubemap   (diffuse IBL)
 *                             \-> prefiltered mip chain (specular IBL, by roughness)
 *          + BRDF integration LUT                       (split-sum approximation)
 *
 * The capture is lazy: it only re-runs when the sky changes (or on demand), so
 * the per-frame cost is zero.
 */

#ifndef __LIBMOD_3D_IBL_H
#define __LIBMOD_3D_IBL_H

#ifdef __cplusplus
extern "C" {
#endif

/* Enable/disable image based lighting. When off, the shader falls back to the
   old flat-ambient term, so existing scenes keep their previous look. */
void g3d_ibl_set_enabled(int enable);
int  g3d_ibl_enabled(void);

/* Scales the IBL contribution (1.0 = the sky's own radiance). */
void g3d_ibl_set_intensity(float intensity);
float g3d_ibl_intensity(void);

/* Mark the environment stale: the next g3d_ibl_update re-captures it. Call when
   the sky, sun or clouds change. */
void g3d_ibl_invalidate(void);

/* Re-capture the environment from the sky if it is stale. Cheap no-op when the
   cubemaps are current. Must run with a GL context bound and NOT inside another
   framebuffer pass. */
void g3d_ibl_update(void);

/* Bind irradiance / prefiltered / BRDF-LUT to the given texture units.
   Returns 1 when IBL is available and bound, 0 otherwise. */
int g3d_ibl_bind(int unit_irradiance, int unit_prefilter, int unit_brdf);

/* Number of mip levels in the prefiltered cubemap (roughness -> lod). */
float g3d_ibl_prefilter_mips(void);

void g3d_ibl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_IBL_H */
