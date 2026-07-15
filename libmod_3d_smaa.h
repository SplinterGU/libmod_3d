/*
 * libmod_3d_smaa.h - SMAA 1x anti-aliasing (Jimenez et al.)
 *
 * The engine had no anti-aliasing of any kind. Beyond looking better, this is
 * the prerequisite for FSR 1: a spatial upscaler needs an anti-aliased source
 * image, otherwise it just magnifies the jaggies.
 *
 * Three passes over the resolved LDR image, after tonemapping:
 *   1. luma edge detection            -> edges (RG8)
 *   2. blending weight calculation    -> weights (RGBA8), using the two
 *                                        precomputed LUTs (area + search)
 *   3. neighborhood blending          -> final image
 *
 * Uses the official SMAA.hlsl through its GLSL_4 path (textureGather + fma),
 * which our GL 4.0 context provides. See smaa/ for the vendored sources.
 */

#ifndef __LIBMOD_3D_SMAA_H
#define __LIBMOD_3D_SMAA_H

#ifdef __cplusplus
extern "C" {
#endif

void g3d_smaa_set_enabled(int enable);
int  g3d_smaa_enabled(void);

/* The framebuffer the scene should be resolved into so SMAA can read it back.
   Returns 0 when SMAA is off or unavailable: resolve straight to the target. */
unsigned int g3d_smaa_scene_fbo(int width, int height);

/* Run the three passes from the scene image into dst_fbo. Only valid after a
   resolve into g3d_smaa_scene_fbo() for the same size. */
void g3d_smaa_apply(unsigned int dst_fbo, int vp_x, int vp_y, int vp_w, int vp_h);

void g3d_smaa_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SMAA_H */
