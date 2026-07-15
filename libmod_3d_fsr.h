/*
 * libmod_3d_fsr.h - AMD FidelityFX Super Resolution 1 (spatial upscaler)
 *
 * Why FSR 1 and not 2/3/4: FSR 4 needs RDNA4 + DX12; FSR 2 and 3.1 need compute
 * shaders (GL 4.3, we run a 4.0 context) and motion vectors for every object,
 * which the engine doesn't produce. FSR 1 is purely spatial: two fragment
 * passes, no history, no motion vectors.
 *
 * Why it's worth it: a 3-point fit of frame time against resolution put the
 * split at 0.484ms fixed + 0.79ns per pixel, so 77% of a 1080p frame is pixel
 * work. Rendering at 720p and upscaling frees ~0.91ms of 2.13.
 *
 *   EASU (edge adaptive upsample)  render res -> display res
 *   RCAS (contrast adaptive sharpen) at display res
 *
 * FSR 1 needs an anti-aliased input or it just magnifies the jaggies, so it
 * chains after SMAA (see libmod_3d_smaa.c).
 */

#ifndef __LIBMOD_3D_FSR_H
#define __LIBMOD_3D_FSR_H

#ifdef __cplusplus
extern "C" {
#endif

/* Enable/disable. The render scale itself lives on the renderer
   (g3d_renderer_set_render_scale): FSR only upscales what it's given. */
void g3d_fsr_set_enabled(int enable);
int  g3d_fsr_enabled(void);

/* RCAS sharpening, 0 = max sharp .. 2 = soft (AMD's stops; default 0.25). */
void g3d_fsr_set_sharpness(float sharpness);

/* The framebuffer the frame should be resolved into for FSR to upscale.
   Returns 0 when FSR is off, unavailable, or there's nothing to upscale
   (render size == display size). */
unsigned int g3d_fsr_input_fbo(int render_w, int render_h, int display_w, int display_h);

/* EASU + RCAS from that buffer into dst_fbo at the given viewport. */
void g3d_fsr_apply(unsigned int dst_fbo, int vp_x, int vp_y, int vp_w, int vp_h);

void g3d_fsr_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_FSR_H */
