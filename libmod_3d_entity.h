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

    /* Transform */
    Vec3 position;
    Quat rotation;
    Vec3 scale;

    /* Cached matrix */
    Mat4 world_matrix;
    int world_matrix_dirty;

    /* Collision: 1 = solid box collider (uses the mesh world AABB) */
    int collider;

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

    /* Blend mode, using BennuGD's blend_mode values (g_blit.h): 1 NORMAL alpha,
       3 MULTIPLY, 4 ADD, 5 SUBTRACT. Non-normal modes force the transparent pass. */
    int blend_mode;
} G3DEntity;

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

/* Blend mode (BennuGD blend_mode values), applied to the whole tree. */
int g3d_entity_impl_set_blend(int entity_id, int mode);

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
