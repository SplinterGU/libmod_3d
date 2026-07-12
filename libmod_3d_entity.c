/*
 * libmod_3d_entity.c - Entity Management Implementation
 */

#include "libmod_3d_entity.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_material.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define G3D_MAX_ENTITIES 16384
static G3DEntity g_entities[G3D_MAX_ENTITIES];
static int g_entity_count = 0;

int g3d_entity_impl_spawn(int scene_id, int model_id, float x, float y, float z) {
    /* Reuse a freed slot first (long-running games spawn/despawn constantly:
       projectiles, fracture chunks, particles). */
    int entity_id = -1;
    for (int i = 0; i < g_entity_count; i++) {
        if (!g_entities[i].active) { entity_id = i; break; }
    }
    if (entity_id < 0) {
        if (g_entity_count >= G3D_MAX_ENTITIES) {
            fprintf(stderr, "G3D: Max entities reached\n");
            return -1;
        }
        entity_id = g_entity_count++;
    }
    G3DEntity *entity = &g_entities[entity_id];

    memset(entity, 0, sizeof(G3DEntity));
    entity->id = entity_id;
    entity->scene_id = scene_id;
    entity->model_id = model_id;
    entity->material_id = -1;
    entity->parent_id = -1;
    entity->active = 1;
    entity->world_matrix_dirty = 1;

    /* Default transform */
    entity->position = vec3_make(x, y, z);
    entity->rotation = quat_identity();
    entity->scale = vec3_make(1, 1, 1);

    /* Add to scene */
    g3d_scene_impl_add_entity(scene_id, entity_id);

    snprintf(entity->name, 63, "Entity_%d", entity_id);

    return entity_id;
}

int g3d_entity_impl_destroy(int entity_id) {
    if (entity_id < 0 || entity_id >= g_entity_count)
        return 0;

    G3DEntity *entity = &g_entities[entity_id];

    if (!entity->active)
        return 0;

    /* Remove from scene */
    g3d_scene_impl_remove_entity(entity->scene_id, entity_id);

    entity->active = 0;
    return 1;
}

/* Floating origin: shift every root entity (children move with their parent) by
   (-dx,-dy,-dz). The game calls this when the camera drifts far from the origin
   so world coordinates stay small (float precision). Physics is rebased
   separately (g3d_world_rebase). */
void g3d_entity_impl_rebase(float dx, float dy, float dz) {
    for (int i = 0; i < g_entity_count; i++) {
        G3DEntity *e = &g_entities[i];
        if (!e->active || e->parent_id >= 0) continue;   /* children follow roots */
        e->position.x -= dx; e->position.y -= dy; e->position.z -= dz;
        e->world_matrix_dirty = 1;
    }
}

/* Destroy an entity and everything parented under it (recursively). If
   free_materials != 0 each destroyed entity's material is also released - use
   this for models spawned by g3d_model_spawn, whose per-submesh materials are
   private to that spawn. */
int g3d_entity_impl_destroy_tree(int root_id, int free_materials) {
    if (root_id < 0 || root_id >= g_entity_count)
        return 0;
    if (!g_entities[root_id].active)
        return 0;

    /* Children first (scan the whole live range; parenting is shallow here). */
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i].active && g_entities[i].parent_id == root_id)
            g3d_entity_impl_destroy_tree(i, free_materials);
    }

    G3DEntity *entity = &g_entities[root_id];
    if (free_materials && entity->material_id >= 0)
        g3d_material_impl_destroy(entity->material_id);
    g3d_scene_impl_remove_entity(entity->scene_id, root_id);
    entity->active = 0;
    return 1;
}

G3DEntity *g3d_entity_impl_get(int entity_id) {
    if (entity_id < 0 || entity_id >= g_entity_count)
        return NULL;

    G3DEntity *entity = &g_entities[entity_id];
    if (!entity->active)
        return NULL;

    return entity;
}

int g3d_entity_impl_set_position(int entity_id, float x, float y, float z) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->position = vec3_make(x, y, z);
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_set_rotation(int entity_id, float pitch, float yaw, float roll) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->rotation = quat_from_euler(pitch, yaw, roll);
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_set_scale(int entity_id, float sx, float sy, float sz) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->scale = vec3_make(sx, sy, sz);
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_get_position(int entity_id, float *x, float *y, float *z) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    if (x)
        *x = entity->position.x;
    if (y)
        *y = entity->position.y;
    if (z)
        *z = entity->position.z;

    return 1;
}

int g3d_entity_impl_set_parent(int entity_id, int parent_id) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->parent_id = parent_id;
    entity->world_matrix_dirty = 1;
    return 1;
}

int g3d_entity_impl_set_material(int entity_id, int material_id) {
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    entity->material_id = material_id;
    return 1;
}

Mat4 g3d_entity_impl_get_world_matrix(int entity_id) {
    static Mat4 identity;
    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return mat4_identity();

    /* Parented entities must recompute every frame: a moving parent does not mark
       its children dirty, so the cache is only safe for root (unparented) nodes. */
    if (!entity->world_matrix_dirty && entity->parent_id < 0)
        return entity->world_matrix;

    /* Build TRS matrix */
    Mat4 world = mat4_trs(entity->position, entity->rotation, entity->scale);

    /* Apply parent transform if exists */
    if (entity->parent_id >= 0) {
        Mat4 parent_world = g3d_entity_impl_get_world_matrix(entity->parent_id);
        world = mat4_multiply(parent_world, world);
    }

    entity->world_matrix = world;
    entity->world_matrix_dirty = 0;

    return world;
}

void g3d_entity_impl_shutdown(void) {
    for (int i = 0; i < g_entity_count; i++) {
        g_entities[i].active = 0;
    }
    g_entity_count = 0;
    printf("G3D: Entity system shut down\n");
}
