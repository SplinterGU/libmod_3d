/*
 * libmod_3d_mesh.h - Mesh Structures and GPU Buffer Management
 *
 * Defines mesh data structures and GPU buffer objects (VAO, VBO, EBO)
 * Handles vertex data, indices, and GPU memory
 */

#ifndef __LIBMOD_3D_MESH_H
#define __LIBMOD_3D_MESH_H

#include <stdint.h>
#include "libmod_3d_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   SKELETAL ANIMATION (glTF) — CPU skinning
   ============================================================================
 */

typedef struct {
    int node;          /* target node index in the model's node array */
    int path;          /* 0 = translation, 1 = rotation, 2 = scale */
    int interp;        /* 0 = linear, 1 = step (cubic falls back to linear) */
    int key_count;
    float *times;      /* key_count timestamps (seconds) */
    float *values;     /* key_count * (3 for T/S, 4 for R) */
} G3DAnimChannel;

typedef struct {
    char name[64];
    float duration;            /* seconds */
    int channel_count;
    G3DAnimChannel *channels;
} G3DAnimation;

/* ============================================================================
   VERTEX STRUCTURE
   ============================================================================
 */

typedef struct {
    float position[3];    /* x, y, z */
    float normal[3];      /* nx, ny, nz */
    float texcoord[2];    /* u, v */
    /* Total: 32 bytes per vertex (tight packing for GPU) */
} G3DVertex;

/* ============================================================================
   MESH STRUCTURES
   ============================================================================
 */

typedef struct {
    uint32_t id;                /* Unique mesh ID */
    char name[64];              /* Mesh name (for debugging) */

    /* Vertex data */
    G3DVertex *vertices;        /* Vertex array */
    uint32_t vertex_count;      /* Number of vertices */

    /* Index data */
    uint32_t *indices;          /* Index array (uint32: large/detailed meshes) */
    uint32_t index_count;       /* Number of indices */

    /* Material */
    int material_id;            /* References G3D_Material by ID */

    /* GPU Objects */
    uint32_t vao;               /* Vertex Array Object */
    uint32_t vbo;               /* Vertex Buffer Object */
    uint32_t ebo;               /* Element Buffer Object */
    int gpu_uploaded;           /* 1 if VAO/VBO/EBO are uploaded to GPU */

    /* Bounds (for culling) */
    float aabb_min[3];          /* Bounding box min */
    float aabb_max[3];          /* Bounding box max */

    /* Terrain grid metadata (terrain_side == 0 means "not a terrain").
       A terrain is a regular (terrain_side x terrain_side) heightmap on XZ,
       centered at origin, spanning terrain_world_size units. Enables editing. */
    int terrain_side;
    float terrain_world_size;

    /* Skinning (CPU). All NULL / 0 when the mesh is not skinned. */
    int skinned;
    float *bind_pos;            /* vertex_count*3 — raw bind positions */
    float *bind_nrm;            /* vertex_count*3 — raw bind normals */
    uint16_t *vjoints;          /* vertex_count*4 — joint indices (skin-relative) */
    float *vweights;            /* vertex_count*4 — joint weights */

    /* Node animation (non-skinned TRS). >=0 means this submesh is one animated
       glTF node kept in LOCAL space: the renderer multiplies the entity matrix
       by model->node_global[anim_node] (+ skin_offset) each frame, so the part
       moves with its node's animation (awnings, fans, doors...). -1 = static. */
    int anim_node;

    void *lod;                  /* auto-generated low-poly copy (lazy); for entity LOD */
} G3DMesh;

typedef struct {
    uint32_t id;                /* Unique model ID */
    char name[64];              /* Model name */
    char filepath[256];         /* Source file path */

    /* Meshes */
    G3DMesh *meshes;            /* Array of meshes */
    uint32_t mesh_count;        /* Number of meshes */

    /* Base-colour (albedo) texture, G3DTexture* (NULL if none). For multi-
       material models, mesh_textures[i] is the albedo for meshes[i]. */
    void *albedo_texture;
    void **mesh_textures;
    void **mesh_normal;      /* per-submesh normal / metallic / roughness maps (NULL if none) */
    void **mesh_metallic;
    void **mesh_roughness;
    unsigned char *submesh_water;  /* per-submesh: 1 if this room is a TR water room (NULL if N/A) */

    /* ---- Skeletal animation (glTF) ---- */
    int skinned;                /* 1 if the model has a skin + skinned meshes */
    int gpu_skin;               /* 1 = skip CPU skinning; skin on the GPU (bone matrices only) */
    float skin_offset[3];       /* center/ground offset applied after skinning */

    /* Node hierarchy (all glTF nodes) */
    int node_count;
    int *node_parent;           /* parent node index, -1 = root */
    Vec3 *node_base_t;          /* base local TRS per node */
    Quat *node_base_r;
    Vec3 *node_base_s;
    Vec3 *node_cur_t;           /* working TRS (animated each frame) */
    Quat *node_cur_r;
    Vec3 *node_cur_s;
    Mat4 *node_global;          /* computed global transform per node */

    /* Skin joints */
    int joint_count;
    int *joint_node;            /* node index for each joint */
    Mat4 *inverse_bind;         /* per joint */
    Mat4 *joint_matrix;         /* per joint, recomputed each frame */
    int root_node;              /* topmost joint node (-1 none); for root-motion lock */
    int lock_root;              /* 1 = animate in place (strip root translation) */

    /* Animations */
    int animation_count;
    G3DAnimation *animations;

    /* Bounds */
    float aabb_min[3];          /* Model bounding box min */
    float aabb_max[3];          /* Model bounding box max */
} G3DModel;

/* ============================================================================
   MESH API
   ============================================================================
 */

/* Create a mesh from vertex/index data */
G3DMesh *g3d_mesh_create(const char *name, G3DVertex *vertices,
                         uint32_t vertex_count, uint32_t *indices,
                         uint32_t index_count);

/* Upload mesh to GPU (create VAO/VBO/EBO) */
int g3d_mesh_upload_gpu(G3DMesh *mesh);

/* Re-upload vertex data to an already-uploaded mesh (after CPU edits) */
void g3d_mesh_update_gpu(G3DMesh *mesh);
void g3d_mesh_update_indices_gpu(G3DMesh *mesh);   /* re-upload the EBO (for terrain holes) */

/* Build a lower-poly copy of a mesh (vertex clustering). grid = cells along the
   longest axis; smaller = more aggressive. GPU-uploaded. For distance LOD. */
G3DMesh *g3d_mesh_simplify(const G3DMesh *src, int grid);

/* Return the mesh's cached low-poly LOD, generating it on first use (or the mesh
   itself if it's too small to simplify). For automatic distance LOD of entities. */
G3DMesh *g3d_mesh_lod(G3DMesh *mesh);

/* Free mesh (GPU and CPU memory) */
void g3d_mesh_free(G3DMesh *mesh);

/* Render a mesh */
void g3d_mesh_render(G3DMesh *mesh);

/* Calculate AABB bounds */
void g3d_mesh_calculate_bounds(G3DMesh *mesh);

/* ============================================================================
   MODEL API
   ============================================================================
 */

/* Create model container */
G3DModel *g3d_model_create(const char *name, const char *filepath);

/* Add mesh to model */
void g3d_model_add_mesh(G3DModel *model, G3DMesh *mesh);

/* Free model (all meshes) */
void g3d_model_free(G3DModel *model);

/* Calculate model bounds from all meshes */
void g3d_model_calculate_bounds(G3DModel *model);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_MESH_H */
