/*
 * libmod_3d_material.h - Material Management
 *
 * Manages material properties (color, metallic, roughness, textures)
 */

#ifndef __LIBMOD_3D_MATERIAL_H
#define __LIBMOD_3D_MATERIAL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int id;
    char name[64];

    /* Color */
    float color[4];  /* RGBA */

    /* Metallic/Roughness PBR */
    float metallic;
    float roughness;

    /* Texture IDs */
    int albedo_texture_id;
    int normal_texture_id;
    int metallic_roughness_texture_id;

    /* Direct texture pointer (G3DTexture*) for the albedo map - avoids storing
       a 64-bit pointer in the 32-bit albedo_texture_id field */
    void *albedo_texture;
    void *normal_texture;      /* tangent-space normal map (G3DTexture*) */
    void *metallic_texture;    /* metallic map (uses .r) */
    void *roughness_texture;   /* roughness map (uses .r) */
    void *wall_texture;   /* triplanar wall/rock texture for slopes & cave walls (G3DTexture*) */

    int triplanar;   /* 1 = voxel terrain: grass on flats, rock on slopes/walls */

    int   biome;      /* 1 = procedural height/slope terrain biomes (streamed terrain) */
    float biome_amp;  /* world height amplitude (band scale) */
    float biome_sea;  /* sea level (world Y) */
    void *biome_tex[4];    /* optional textures: [0]sand [1]grass [2]rock [3]snow (G3DTexture*) */
    float biome_tex_scale; /* world-space UV tiling for the biome textures */

    int active;
} G3DMaterial;

/* Material lifecycle */
int g3d_material_impl_create(void);
int g3d_material_impl_destroy(int material_id);
G3DMaterial *g3d_material_impl_get(int material_id);

/* Material properties */
int g3d_material_impl_set_color(int material_id, float r, float g, float b, float a);
int g3d_material_impl_set_metallic(int material_id, float value);
int g3d_material_impl_set_roughness(int material_id, float value);
int g3d_material_impl_set_texture(int material_id, int texture_type, int texture_id);
/* Set a PBR map by pointer (type 1=normal, 2=metallic, 3=roughness). */
int g3d_material_impl_set_map(int material_id, int type, void *texture);
int g3d_material_impl_set_triplanar(int material_id, int on);
int g3d_material_impl_set_biome(int material_id, int on, float amplitude, float sea_level);
int g3d_material_impl_set_biome_textures(int material_id, void *sand, void *grass, void *rock, void *snow, float scale);

/* Cleanup */
void g3d_material_impl_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_MATERIAL_H */
