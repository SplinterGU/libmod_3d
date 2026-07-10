/*
 * libmod_3d_light.c - Lighting Management Implementation
 */

#include "libmod_3d_light.h"
#include "libmod_3d_scene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define G3D_MAX_LIGHTS 32

static G3DLight g_lights[G3D_MAX_LIGHTS];
static int g_light_count = 0;   /* high-water mark for get_all / shutdown */
static int g_light_next  = 0;   /* circular hint: where to start looking next */

int g3d_light_impl_create(int type, float r, float g, float b) {
    /* Circular next-fit: start from g_light_next, wrap around, try at most MAX slots */
    int light_id = -1;
    for (int i = 0; i < G3D_MAX_LIGHTS; i++) {
        int candidate = (g_light_next + i) % G3D_MAX_LIGHTS;
        if (!g_lights[candidate].active) {
            light_id = candidate;
            break;
        }
    }
    if (light_id < 0) {
        fprintf(stderr, "G3D: Max lights reached\n");
        return 0;
    }

    /* Advance hint past the slot we just took */
    g_light_next = (light_id + 1) % G3D_MAX_LIGHTS;

    /* Extend high-water mark if we're using a new slot */
    if (light_id >= g_light_count)
        g_light_count = light_id + 1;

    G3DLight *light = &g_lights[light_id];

    memset(light, 0, sizeof(G3DLight));
    light->id = light_id;
    light->type = type;
    light->active = 1;

    /* Color */
    light->color[0] = (r < 0.0f) ? 0.0f : (r > 1.0f) ? 1.0f : r;
    light->color[1] = (g < 0.0f) ? 0.0f : (g > 1.0f) ? 1.0f : g;
    light->color[2] = (b < 0.0f) ? 0.0f : (b > 1.0f) ? 1.0f : b;

    /* Default values */
    light->intensity = 1.0f;
    light->range = 100.0f;
    light->cone_angle = 45.0f;
    light->cast_shadows = 0;
    light->shadow_resolution = 2048;

    /* Direction defaults to down */
    light->direction = vec3_make(0, -1, 0);

    /* Add to active scene if one exists.
       NOTE: g3d_light_impl_get() expects 1-based IDs (it does g_lights[id-1]),
       so we store light_id+1 in the scene so the renderer can look it up. */
    int active_scene = g3d_scene_impl_get_active();
    if (active_scene >= 0) {
        g3d_scene_impl_add_light(active_scene, light_id + 1);
    }

    printf("G3D: Light created: id=%d, type=%d\n", light_id + 1, type);
    return light_id + 1;
}

int g3d_light_impl_destroy(int light_id) {
    if (light_id < 1 || light_id > g_light_count)
        return 0;

    G3DLight *light = &g_lights[light_id - 1];

    if (!light->active)
        return 0;

    /* Remove from all scenes */
    for (int i = 0; i < 16; i++) {
        G3DScene *scene = g3d_scene_impl_get(i);
        if (scene) {
            g3d_scene_impl_remove_light(i, light_id);
        }
    }

    light->active = 0;

    /* Shrink high-water mark if we freed a tail slot */
    while (g_light_count > 0 && !g_lights[g_light_count - 1].active)
        g_light_count--;

    printf("G3D: Light destroyed: id=%d\n", light_id);
    return 1;
}

G3DLight *g3d_light_impl_get(int light_id) {
    /* light_id is 1-based; valid range is 1..MAX */
    if (light_id < 1 || light_id > G3D_MAX_LIGHTS)
        return NULL;

    G3DLight *light = &g_lights[light_id - 1];
    if (!light->active)
        return NULL;

    return light;
}

int g3d_light_impl_set_position(int light_id, float x, float y, float z) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    light->position = vec3_make(x, y, z);
    return 1;
}

int g3d_light_impl_set_direction(int light_id, float dx, float dy, float dz) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    Vec3 dir = vec3_make(dx, dy, dz);
    light->direction = vec3_normalize(dir);
    return 1;
}

int g3d_light_impl_set_color(int light_id, float r, float g, float b) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    light->color[0] = (r < 0.0f) ? 0.0f : (r > 1.0f) ? 1.0f : r;
    light->color[1] = (g < 0.0f) ? 0.0f : (g > 1.0f) ? 1.0f : g;
    light->color[2] = (b < 0.0f) ? 0.0f : (b > 1.0f) ? 1.0f : b;
    return 1;
}

int g3d_light_impl_set_intensity(int light_id, float intensity) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    light->intensity = (intensity < 0.0f) ? 0.0f : intensity;
    return 1;
}

int g3d_light_impl_set_range(int light_id, float range) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    light->range = (range < 0.0f) ? 0.0f : range;
    return 1;
}

int g3d_light_impl_set_cone(int light_id, float angle_degrees) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    if (angle_degrees < 1.0f) angle_degrees = 1.0f;
    if (angle_degrees > 179.0f) angle_degrees = 179.0f;
    light->cone_angle = angle_degrees;
    return 1;
}

int g3d_light_impl_enable_shadow(int light_id, int enabled) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    light->cast_shadows = (enabled != 0) ? 1 : 0;
    return 1;
}

int g3d_light_impl_set_shadow_quality(int light_id, int resolution) {
    G3DLight *light = g3d_light_impl_get(light_id);
    if (!light)
        return 0;

    if (resolution < 512)
        resolution = 512;
    if (resolution > 4096)
        resolution = 4096;

    light->shadow_resolution = resolution;
    return 1;
}

int *g3d_light_impl_get_all(int *count) {
    static int light_ids[G3D_MAX_LIGHTS];
    int idx = 0;

    for (int i = 0; i < g_light_count; i++) {
        if (g_lights[i].active) {
            light_ids[idx++] = i;
        }
    }

    if (count)
        *count = idx;

    return (idx > 0) ? light_ids : NULL;
}

void g3d_light_impl_shutdown(void) {
    for (int i = 0; i < g_light_count; i++) {
        g_lights[i].active = 0;
    }
    g_light_count = 0;
    printf("G3D: Light system shut down\n");
}
