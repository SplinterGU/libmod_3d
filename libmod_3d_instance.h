/*
 * libmod_3d_instance.h - GPU instancing for open-world vegetation/props
 *
 * An instance group draws one mesh many times in a single draw call, with a
 * per-instance transform. Per frame it culls instances by distance + frustum
 * (cheap LOD) and uploads only the visible ones. Optional vertex-shader wind
 * sway lets a forest move without per-tree skeletal animation.
 */

#ifndef __LIBMOD_3D_INSTANCE_H
#define __LIBMOD_3D_INSTANCE_H

#include "libmod_3d_camera.h"
#include "libmod_3d_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Create an instance group from a mesh (G3DMesh*) and albedo texture
   (G3DTexture*, may be NULL). Returns a group id, or -1. */
int g3d_instances_create(void *mesh, void *texture);

/* Add one instance at (x,y,z), yaw degrees, uniform scale. */
int g3d_instances_add(int group, float x, float y, float z,
                      float yaw_deg, float scale);

/* Update instance `index` in place (own a stable slot per process). */
void g3d_instances_set(int group, int index, float x, float y, float z,
                       float yaw_deg, float scale);

void g3d_instances_set_wind(int group, float strength);
void g3d_instances_set_distance(int group, float dist);
void g3d_instances_clear(int group);
int g3d_instances_count(int group);

/* Data-driven population (what an editor configures): scatter `count` copies of
   a mesh/model across a square of half-size `area` on the terrain, with random
   yaw and scale in [smin,smax] (mesh) or base*(1±scale_var) (model). `seed`
   makes it reproducible. Returns the (first) instance group id. */
int g3d_scatter_mesh(void *mesh, void *texture, void *terrain, int count,
                     float area, float smin, float smax, float wind,
                     unsigned int seed);
int g3d_scatter_model(void *model, void *terrain, int count, float area,
                      float target_h, float scale_var, float wind,
                      unsigned int seed);

/* Draw all instance groups (called by the renderer, opaque pass). */
void g3d_instances_render_all(G3DCamera *camera, int flip_y);

/* Render all instances' depth into the current shadow FBO (so they cast). */
void g3d_instances_render_depth(Mat4 light_space);

void g3d_instances_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_INSTANCE_H */
