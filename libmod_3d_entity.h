/*
 * libmod_3d_entity.h - Entity Management
 *
 * Manages game objects with transforms, meshes, and materials
 */

#ifndef __LIBMOD_3D_ENTITY_H
#define __LIBMOD_3D_ENTITY_H

#include "libmod_3d_math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;
    char name[64];
    int scene_id;
    int model_id;
    int material_id;
    int parent_id;
    int active;

    /* Direct mesh pointer (set via g3d_entity_set_mesh) - avoids storing a
       64-bit pointer in the 32-bit model_id field */
    void *mesh;

    /* Node animation: when this entity's mesh is a node-animated submesh
       (mesh->anim_node >= 0), anim_model points back to its G3DModel so the
       renderer can fetch node_global[anim_node] each frame. NULL for static. */
    void *anim_model;

    /* Transform */
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    /* Cached matrix */
    Mat4 world_matrix;
    int world_matrix_dirty;

    /* Collision: 1 = solid box collider (uses the mesh world AABB) */
    int collider;

    /* Occlusion culling (libmod_3d_occlusion.c). occ_visible is one frame old:
       the query issued after the opaque pass is read back without blocking.
       Defaults to visible, so an untested entity is always drawn. */
    unsigned int occ_query;    /* GL query object, 0 = not created yet */
    int occ_pending;           /* a query is in flight */
    int occ_visible;           /* 0 = fully behind other geometry */

    /* Model far-LOD: on a g3d_model_spawn root, a single merged+decimated mesh
       drawn (with lod_material) when the model is beyond g3d_set_lod distance,
       replacing all its submesh children (61 draws -> 1). lod_far = this frame's
       state, read by the children to skip themselves. */
    void *lod_mesh;
    int lod_material;
    int lod_far;

    /* Opt out of the automatic per-mesh LOD decimation. Terrain tiles set this:
       their borders must stay full-res so neighbouring tiles keep matching
       (decimating them opens cracks along the tile grid at distance). */
    int lod_exempt;

    /* Opacity 0..1 (1 = opaque). < 1 -> drawn in the transparent pass (alpha
       blended). Set from BennuGD's 0..255 `alpha` local via g3d_entity_set_alpha. */
    float opacity;

    /* RGB tint 0..1 multiplied into the albedo (1,1,1 = no tint). Like BennuGD's
       per-graphic colour. Set via g3d_entity_set_color (0..255). */
    float tint[3];

    /* Blend mode (G3D_BLEND_* below, values match BennuGD's blendmode local).
       Non-normal modes force the transparent pass. */
    int blend_mode;
    /* Factors for G3D_BLEND_CUSTOM (raw GL enums, like BennuGD's custom_blendmode):
       [0]src_rgb [1]dst_rgb [2]src_alpha [3]dst_alpha [4]eq_rgb [5]eq_alpha */
    int blend_custom[6];
} G3DEntity;

/* Blend modes - values MATCH BennuGD's g_blit.h so the process `blendmode` local
   maps 1:1 (0/NONE, 1/NORMAL, ... up to CUSTOM which reads custom_blendmode). */
#define G3D_BLEND_CUSTOM              (-2)
#define G3D_BLEND_DISABLED           (-1)
#define G3D_BLEND_NONE                 0
#define G3D_BLEND_NORMAL               1
#define G3D_BLEND_PREMULTIPLIED_ALPHA  2
#define G3D_BLEND_MULTIPLY             3
#define G3D_BLEND_ADD                  4
#define G3D_BLEND_SUBTRACT             5
#define G3D_BLEND_MOD_ALPHA            6
#define G3D_BLEND_SET_ALPHA            7
#define G3D_BLEND_SET                  8
#define G3D_BLEND_NORMAL_KEEP_ALPHA    9
#define G3D_BLEND_NORMAL_ADD_ALPHA    10
#define G3D_BLEND_NORMAL_FACTOR_ALPHA 11
#define G3D_BLEND_ALPHA_MASK          12

/* Entity lifecycle */
int g3d_entity_impl_spawn(int scene_id, int model_id, float x, float y, float z);
int g3d_entity_impl_destroy(int entity_id);
int g3d_entity_impl_destroy_tree(int root_id, int free_materials);
void g3d_entity_impl_rebase(float dx, float dy, float dz);   /* floating origin */
G3DEntity *g3d_entity_impl_get(int entity_id);

/* Transform */
int g3d_entity_impl_set_position(int entity_id, float x, float y, float z);
int g3d_entity_impl_set_rotation(int entity_id, float pitch, float yaw, float roll);
int g3d_entity_impl_set_scale(int entity_id, float sx, float sy, float sz);
int g3d_entity_impl_get_position(int entity_id, float *x, float *y, float *z);
int g3d_entity_impl_set_parent(int entity_id, int parent_id);

/* Material */
int g3d_entity_impl_set_material(int entity_id, int material_id);

/* Opacity 0..1 (applied to the entity and all its children, so it works on a
   whole g3d_model_spawn tree). 1 = opaque, < 1 = alpha blended. */
int g3d_entity_impl_set_alpha(int entity_id, float opacity);

/* RGB tint 0..1 multiplied into the albedo (applied to the whole tree). */
int g3d_entity_impl_set_color(int entity_id, float r, float g, float b);

/* Blend mode (G3D_BLEND_* values), applied to the whole tree. */
int g3d_entity_impl_set_blend(int entity_id, int mode);
/* Custom blend factors (raw GL enums) for G3D_BLEND_CUSTOM, applied to the tree. */
int g3d_entity_impl_set_blend_custom(int entity_id, int src_rgb, int dst_rgb,
                                     int src_alpha, int dst_alpha,
                                     int eq_rgb, int eq_alpha);


/* Matrix */
Mat4 g3d_entity_impl_get_world_matrix(int entity_id);

/* Spawn a whole model (every submesh) grouped under one empty root entity, with
   per-submesh materials (albedo + PBR maps) built automatically. Returns the
   root entity id; move/rotate/scale it to transform the whole model. `height`>0
   scales the model to that height (<=0 keeps 1:1). `roty` is in radians.
   `model` is a G3DModel* (the handle returned by g3d_load_gltf/obj/fbx). */
int g3d_model_spawn(int scene_id, void *model, float x, float y, float z,
                    float height, float roty);

/* Cleanup */
void g3d_entity_impl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_ENTITY_H */
