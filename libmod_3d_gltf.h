/*
 * libmod_3d_gltf.h - glTF 2.0 Model Loader
 *
 * Parses and loads glTF (.gltf, .glb) files using cgltf
 * Converts glTF data to G3DModel with meshes and textures
 */

#ifndef __LIBMOD_3D_GLTF_H
#define __LIBMOD_3D_GLTF_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   glTF LOADER API
   ============================================================================
 */

/**
 * Load glTF model from file
 *
 * Supports:
 * - .gltf (JSON + external bins)
 * - .glb (binary self-contained)
 *
 * Returns: G3DModel* on success, NULL on failure
 * Loaded model includes:
 * - All meshes (triangulated)
 * - Vertex normals (calculated if missing)
 * - Texture coordinates
 * - Materials with textures
 *
 * Example:
 *   G3DModel *model = g3d_gltf_load("models/character.glb");
 *   if (model) {
 *       for (int i = 0; i < model->mesh_count; i++) {
 *           g3d_mesh_upload_gpu(&model->meshes[i]);
 *       }
 *   }
 */
G3DModel *g3d_gltf_load(const char *filepath);

/* Toggle X/Z re-centering of loaded models (default on). Turn OFF before loading
   world/map sectors whose geometry is baked at absolute world coordinates. */
void g3d_gltf_set_recenter(int on);

/* Fracture loader: one submesh per NODE (not per material). A pre-fractured
   model whose chunks share one material comes in as N separate submeshes, each
   keeping its chunk's world position (submesh AABB centre = chunk centroid).
   Static geometry only. */
G3DModel *g3d_gltf_load_fractured(const char *filepath);

/* Bake an orientation correction (Euler radians) into a model and re-ground it.
   For models whose authored bind pose comes tilted. */
void g3d_model_orient(G3DModel *model, float rx, float ry, float rz);

/**
 * Free glTF model and all associated resources
 */
void g3d_gltf_free(G3DModel *model);

/* ============================================================================
   glTF FEATURES SUPPORTED
   ============================================================================
 */

/*
 * Mesh Data:
 * ✓ Geometry (positions, normals, texcoords)
 * ✓ Indices (uint16 and uint32)
 * ✓ Multiple primitives per mesh
 * ✓ Automatic normal calculation if missing
 *
 * Materials:
 * ✓ Base color (albedo)
 * ✓ Metallic-Roughness workflow
 * ✓ Texture references
 * ✓ Normal maps
 *
 * Textures:
 * ✓ Embedded (base64 in .gltf)
 * ✓ Embedded (.glb)
 * ✓ External files
 * ✓ PNG, JPG formats
 *
 * Animations:
 * ✗ Not yet (planned Phase 4)
 *
 * Skeletal Rigging:
 * ✗ Not yet (planned Phase 4)
 *
 * Extensions:
 * ✗ Not yet (KHR_* extensions)
 */

/* ============================================================================
   INTERNAL STRUCTURES (Don't use directly)
   ============================================================================
 */

/* Opaque glTF parsing context */
typedef struct G3DGltfContext G3DGltfContext;

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_GLTF_H */
