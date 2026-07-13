/*
 * libmod_3d_material.c - Material Management Implementation
 */

#include "libmod_3d_material.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G3D_MAX_MATERIALS 4096
static G3DMaterial g_materials[G3D_MAX_MATERIALS];
static int g_material_count = 0;

int g3d_material_impl_create(void) {
    /* Reuse a freed slot first so long-running games that spawn/despawn a lot
       (particles, fracture chunks, projectiles) don't exhaust the pool. */
    int material_id = -1;
    for (int i = 0; i < g_material_count; i++) {
        if (!g_materials[i].active) { material_id = i; break; }
    }
    if (material_id < 0) {
        if (g_material_count >= G3D_MAX_MATERIALS) {
            fprintf(stderr, "G3D: Max materials reached\n");
            return -1;
        }
        material_id = g_material_count++;
    }
    G3DMaterial *mat = &g_materials[material_id];

    memset(mat, 0, sizeof(G3DMaterial));
    mat->id = material_id;
    mat->active = 1;

    /* Default values */
    mat->color[0] = 1.0f;  /* R */
    mat->color[1] = 1.0f;  /* G */
    mat->color[2] = 1.0f;  /* B */
    mat->color[3] = 1.0f;  /* A */
    mat->metallic = 0.0f;
    mat->roughness = 0.5f;
    mat->albedo_texture_id = -1;
    mat->normal_texture_id = -1;
    mat->metallic_roughness_texture_id = -1;

    snprintf(mat->name, 63, "Material_%d", material_id);

    return material_id;
}

int g3d_material_impl_destroy(int material_id) {
    if (material_id < 0 || material_id >= g_material_count)
        return 0;

    G3DMaterial *mat = &g_materials[material_id];

    if (!mat->active)
        return 0;

    mat->active = 0;
    return 1;
}

G3DMaterial *g3d_material_impl_get(int material_id) {
    if (material_id < 0 || material_id >= g_material_count)
        return NULL;

    G3DMaterial *mat = &g_materials[material_id];
    if (!mat->active)
        return NULL;

    return mat;
}

int g3d_material_impl_set_color(int material_id, float r, float g, float b, float a) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat)
        return 0;

    mat->color[0] = r;
    mat->color[1] = g;
    mat->color[2] = b;
    mat->color[3] = a;

    return 1;
}

int g3d_material_impl_set_metallic(int material_id, float value) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat)
        return 0;

    mat->metallic = (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;
    return 1;
}

int g3d_material_impl_set_roughness(int material_id, float value) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat)
        return 0;

    mat->roughness = (value < 0.0f) ? 0.0f : (value > 1.0f) ? 1.0f : value;
    return 1;
}

int g3d_material_impl_set_triplanar(int material_id, int on) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat) return 0;
    mat->triplanar = on ? 1 : 0;
    return 1;
}

int g3d_material_impl_set_biome(int material_id, int on, float amplitude, float sea_level) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat) return 0;
    mat->biome = on ? 1 : 0;
    mat->biome_amp = (amplitude > 0.0f) ? amplitude : 60.0f;
    mat->biome_sea = sea_level;
    return 1;
}

int g3d_material_impl_set_biome_textures(int material_id, void *sand, void *grass, void *rock, void *snow, float scale) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat) return 0;
    mat->biome_tex[0] = sand;
    mat->biome_tex[1] = grass;
    mat->biome_tex[2] = rock;
    mat->biome_tex[3] = snow;
    mat->biome_tex_scale = (scale > 0.0f) ? scale : 0.03f;
    return 1;
}

int g3d_material_impl_set_map(int material_id, int type, void *texture) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat) return 0;
    if (type == 1) mat->normal_texture = texture;
    else if (type == 2) mat->metallic_texture = texture;
    else if (type == 3) mat->roughness_texture = texture;
    else return 0;
    return 1;
}

int g3d_material_impl_set_texture(int material_id, int texture_type, int texture_id) {
    G3DMaterial *mat = g3d_material_impl_get(material_id);
    if (!mat)
        return 0;

    switch (texture_type) {
        case 0:  /* Albedo */
            mat->albedo_texture_id = texture_id;
            break;
        case 1:  /* Normal */
            mat->normal_texture_id = texture_id;
            break;
        case 2:  /* Metallic/Roughness */
            mat->metallic_roughness_texture_id = texture_id;
            break;
        default:
            return 0;
    }

    return 1;
}

void g3d_material_impl_shutdown(void) {
    for (int i = 0; i < g_material_count; i++) {
        g_materials[i].active = 0;
    }
    g_material_count = 0;
    printf("G3D: Material system shut down\n");
}
