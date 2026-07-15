/*
 * libmod_3d_occlusion.h - Hardware occlusion culling (GL occlusion queries)
 *
 * Frustum culling can only reject what falls outside the view cone. Looking
 * down a street almost everything is in front of you, so most of the geometry
 * is drawn even though the façades hide it. This rejects what is BEHIND other
 * geometry.
 *
 * After the opaque pass the depth buffer holds the occluders. Each entity's
 * bounding box is then drawn (colour and depth writes off) wrapped in an
 * occlusion query: if not a single sample passes the depth test, the box is
 * entirely behind something and the entity is skipped NEXT frame.
 *
 * Results are read without blocking, so visibility is one frame old — the usual
 * trade for not stalling the GPU. Anything unknown, frustum-culled or wrapped
 * around the camera counts as visible, so the failure mode is drawing too much,
 * never popping something out of view.
 *
 * Needs the geometry to be spatially local to pay off: a submesh whose AABB
 * spans half the map always has something visible. See the chunking in
 * libmod_3d_gltf.c.
 */

#ifndef __LIBMOD_3D_OCCLUSION_H
#define __LIBMOD_3D_OCCLUSION_H

#include "libmod_3d_camera.h"
#include "libmod_3d_entity.h"

#ifdef __cplusplus
extern "C" {
#endif

void g3d_occlusion_set_enabled(int enable);
int  g3d_occlusion_enabled(void);

/* 1 = the entity should be drawn. Always 1 when occlusion culling is off. */
int  g3d_occlusion_visible(G3DEntity *entity);

/* Draw every entity's bounding box into the (already populated) depth buffer
   inside an occlusion query, and collect the previous frame's results. Call
   after the opaque pass, before anything that changes the depth buffer. */
void g3d_occlusion_pass(G3DCamera *camera, int *entities, int entity_count, int flip_y);

/* How many entities the last frame's occlusion test rejected (for stats). */
unsigned int g3d_occlusion_culled(void);

void g3d_occlusion_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_OCCLUSION_H */
