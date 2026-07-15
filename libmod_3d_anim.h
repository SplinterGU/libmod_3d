/*
 * libmod_3d_anim.h - Skeletal animation playback (glTF), CPU skinning
 */

#ifndef __LIBMOD_3D_ANIM_H
#define __LIBMOD_3D_ANIM_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Number of animations the model carries. */
int g3d_model_animation_count(G3DModel *model);

/* Duration (seconds) of an animation, or 0. */
float g3d_model_animation_duration(G3DModel *model, int anim);

/* Evaluate animation `anim` at `time` seconds (loop wraps it to the duration),
   recompute the joint matrices and re-skin every skinned mesh on the GPU. A
   no-op for models without a skin. */
void g3d_model_animate(G3DModel *model, int anim, float time, int loop);

/* Play ALL of a model's animations at once at `time` seconds and recompute the
   node world transforms. For scenes (Bistro-style) whose many independent parts
   each have their own clip on disjoint nodes: awnings, fans, a swinging basket,
   an orbiting light. Works on non-skinned models (node TRS only); also drives
   skinning when the model has a skin. */
void g3d_model_animate_all(G3DModel *model, float time, int loop);

/* Enable GPU skinning for a model: g3d_model_animate then only computes bone
   matrices (cheap) and the instanced skinned shader does the per-vertex work.
   Use with g3d_instances_create_skinned. */
void g3d_model_set_gpu_skin(G3DModel *model, int enable);

/* Cross-fade two animations (weight 0 = a0, 1 = a1). Blends per bone: lerp
   translation/scale, slerp rotation. For smooth idle<->walk<->run transitions. */
void g3d_model_animate_blend(G3DModel *model, int a0, float t0,
                             int a1, float t1, float weight, int loop);

/* Restore the bind pose (skin all meshes with identity joints). */
void g3d_model_rest_pose(G3DModel *model);

/* Root-motion lock: 1 (default) keeps the character in place, 0 lets the root
   bone translate (e.g. a walk cycle that moves forward). */
void g3d_model_set_lock_root(G3DModel *model, int enable);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_ANIM_H */
