/*
 * libmod_3d.c - Modern 3D Engine for BennuGD2
 *
 * Core module initialization and lifecycle
 * Individual subsystems (camera, renderer, assets) are in separate modules
 */

#include "libmod_3d.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_material.h"
#include "libmod_3d_light.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_water.h"
#include "libmod_3d_fire.h"
#include "libmod_3d_obj.h"
#include "libmod_3d_fbx.h"
#include "libmod_3d_flow.h"
#include "libmod_3d_particles.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_physics.h"
#include "libmod_3d_anim.h"
#include "libmod_3d_collide.h"
#include "libmod_3d_mirror.h"
#include "libmod_3d_instance.h"
#include "libmod_3d_stream.h"
#include "libmod_3d_worldgen.h"
#include "libmod_3d_prefab.h"
#include "libmod_3d_scenefile.h"
#include "libmod_3d_voxterrain.h"
#include "libmod_3d_pick.h"
#include "libmod_3d_paint.h"
#include "libmod_3d_cloth.h"

#include <SDL.h>
#include "SDL_gpu.h"
#include "xstrings.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

/* ============================================================================
   GLOBAL STATE
   ============================================================================
 */

static int g_3d_initialized = 0;
static int64_t g3d_engine_obj_id = 0;

/* C_3D ctype value — must match the constant exported in libbggfx_exports.h */
#define C_3D_CTYPE 2

/* Indices into libmod_3d locals_fixup[] (same order as DLVARFIXUP in exports) */
enum {
    LOC3D_CSUBTYPE = 0,
    LOC3D_COORDX,
    LOC3D_COORDY,
    LOC3D_COORDZ,
    LOC3D_ANGLE_X,
    LOC3D_ANGLE_Y,
    LOC3D_ANGLE_Z,
    LOC3D_SIZE,
    LOC3D_SIZE_X,
    LOC3D_SIZE_Y,
    LOC3D_SIZE_Z,
    LOC3D_ENTITY,
    LOC3D_TARGET_X,
    LOC3D_TARGET_Y,
    LOC3D_TARGET_Z,
    LOC3D_FOV,
    LOC3D_INTENSITY,
    LOC3D_RANGE,
    LOC3D_CONE_ANGLE,
    LOC3D_ALPHA,
    LOC3D_COLORR,
    LOC3D_COLORG,
    LOC3D_COLORB,
    LOC3D_BLENDMODE,
    LOC3D_BLEND_SRC_RGB,
    LOC3D_BLEND_DST_RGB,
    LOC3D_BLEND_SRC_ALPHA,
    LOC3D_BLEND_DST_ALPHA,
    LOC3D_BLEND_EQ_RGB,
    LOC3D_BLEND_EQ_ALPHA
};

DLVARFIXUP __bgdexport(libmod_3d, locals_fixup)[] = {
    { "csubtype"  , NULL, -1, -1 },
    { "x"         , NULL, -1, -1 },
    { "y"         , NULL, -1, -1 },
    { "z"         , NULL, -1, -1 },
    { "angle_x"   , NULL, -1, -1 },
    { "angle_y"   , NULL, -1, -1 },
    { "angle_z"   , NULL, -1, -1 },
    { "size"      , NULL, -1, -1 },
    { "size_x"    , NULL, -1, -1 },
    { "size_y"    , NULL, -1, -1 },
    { "size_z"    , NULL, -1, -1 },
    { "entity"    , NULL, -1, -1 },
    { "target_x"  , NULL, -1, -1 },
    { "target_y"  , NULL, -1, -1 },
    { "target_z"  , NULL, -1, -1 },
    { "fov"       , NULL, -1, -1 },
    { "intensity" , NULL, -1, -1 },
    { "range"     , NULL, -1, -1 },
    { "cone_angle", NULL, -1, -1 },
    /* BennuGD's per-graphic locals (declared by libbggfx) - resolved by NAME to
       the SAME process storage, so g3d entities can obey the standard alpha /
       color / blend locals with the normal LOCBYTE/LOCINT64 macros. */
    { "alpha"     , NULL, -1, -1 },
    { "color_r"   , NULL, -1, -1 },
    { "color_g"   , NULL, -1, -1 },
    { "color_b"   , NULL, -1, -1 },
    { "blendmode" , NULL, -1, -1 },
    { "custom_blendmode.src_rgb"       , NULL, -1, -1 },
    { "custom_blendmode.dst_rgb"       , NULL, -1, -1 },
    { "custom_blendmode.src_alpha"     , NULL, -1, -1 },
    { "custom_blendmode.dst_alpha"     , NULL, -1, -1 },
    { "custom_blendmode.equation_rgb"  , NULL, -1, -1 },
    { "custom_blendmode.equation_alpha", NULL, -1, -1 },
    { NULL        , NULL, -1, -1 }
};

/* ============================================================================
   CORE INITIALIZATION
   ============================================================================
 */

int g3d_engine_init(int width, int height) {
    if (g_3d_initialized) {
        fprintf(stderr, "G3D: Engine already initialized\n");
        return 0;
    }

    printf("G3D: Initializing 3D engine\n");
    printf("G3D: Display: %dx%d\n", width, height);

    /* Initialize renderer */
    if (!g3d_renderer_init(width, height)) {
        fprintf(stderr, "G3D: Renderer initialization failed\n");
        return 0;
    }

    g_3d_initialized = 1;
    printf("G3D: Engine initialized successfully\n");

    return 1;
}

int g3d_engine_shutdown(void) {
    if (!g_3d_initialized)
        return 0;

    printf("G3D: Shutting down engine\n");

    g3d_cloth_shutdown();
    g3d_sky_shutdown();
    g3d_mirror_shutdown();
    g3d_instances_shutdown();
    g3d_prefab_shutdown();
    g3d_renderer_shutdown();

    g_3d_initialized = 0;
    printf("G3D: Engine shutdown complete\n");

    return 1;
}

int g3d_engine_render(void) {
    if (!g_3d_initialized)
        return 0;

    g3d_renderer_render();
    return 1;
}

/* ============================================================================
   BGD WRAPPERS (Simple delegation to C API)
   ============================================================================
 */



static void g3d_ensure_init(void) {
    if (g_3d_initialized) return;
    if (renderer_scaled_width > 0 && renderer_scaled_height > 0) {
        g3d_engine_init((int)renderer_scaled_width, (int)renderer_scaled_height);
        g3d_renderer_set_viewport_physical((uint32_t)renderer_offset_x, (uint32_t)renderer_offset_y,
                                           (uint32_t)renderer_scaled_width, (uint32_t)renderer_scaled_height);
    }
}

/* Scene wrappers */
int64_t g3d_scene_create_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *name = (const char *)string_get(params[0]);
    int64_t result = g3d_scene_impl_create(name);
    string_discard(params[0]);
    return result;
}

int64_t g3d_scene_destroy_bgd(INSTANCE *my, int64_t *params) {
    int scene_id = (int)params[0];
    return g3d_scene_impl_destroy(scene_id);
}

int64_t g3d_scene_set_active_bgd(INSTANCE *my, int64_t *params) {
    int scene_id = (int)params[0];
    return g3d_scene_impl_set_active(scene_id);
}

/* Entity wrappers */
int64_t g3d_entity_spawn_bgd(INSTANCE *my, int64_t *params) {
    int scene_id = (int)params[0];
    int model_id = (int)params[1];
    float x = *(float *)&params[2];
    float y = *(float *)&params[3];
    float z = *(float *)&params[4];
    return g3d_entity_impl_spawn(scene_id, model_id, x, y, z);
}

int64_t g3d_entity_destroy_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    return g3d_entity_impl_destroy(entity_id);
}

/* Despawn a model spawned with g3d_model_spawn: destroys the root, all its
   submesh children and releases their private materials. Also works on a
   single entity+material (e.g. a fracture chunk). Use this to avoid leaking
   entity/material pool slots when spawning/despawning constantly. */
int64_t g3d_model_despawn_bgd(INSTANCE *my, int64_t *params) {
    int root_id = (int)params[0];
    return g3d_entity_impl_destroy_tree(root_id, 1);
}

int64_t g3d_entity_set_position_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    float x = *(float *)&params[1];
    float y = *(float *)&params[2];
    float z = *(float *)&params[3];
    return g3d_entity_impl_set_position(entity_id, x, y, z);
}

/* BGD angles use BennuGD's unit (thousandths of a degree, like sin/cos/angle);
   the C core uses radians. Convert only at this boundary. */
#define G3D_MD2RAD(md) ((float)((md) * 0.00001745329252))
#define G3D_RAD2MD(r)  ((float)((r) * 57295.7795131))

int64_t g3d_entity_set_rotation_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    float pitch = G3D_MD2RAD(*(float *)&params[1]);
    float yaw   = G3D_MD2RAD(*(float *)&params[2]);
    float roll  = G3D_MD2RAD(*(float *)&params[3]);
    return g3d_entity_impl_set_rotation(entity_id, pitch, yaw, roll);
}

int64_t g3d_entity_set_scale_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    float sx = *(float *)&params[1];
    float sy = *(float *)&params[2];
    float sz = *(float *)&params[3];
    return g3d_entity_impl_set_scale(entity_id, sx, sy, sz);
}

int64_t g3d_entity_get_position_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    float *x = (float *)params[1];
    float *y = (float *)params[2];
    float *z = (float *)params[3];
    return g3d_entity_impl_get_position(entity_id, x, y, z);
}

int64_t g3d_entity_set_material_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    int material_id = (int)params[1];
    return g3d_entity_impl_set_material(entity_id, material_id);
}

/* Opacity from BennuGD's 0..255 `alpha` convention (255 = opaque). Applies to
   the whole model tree. Pass your process' `alpha` local straight in. */
int64_t g3d_entity_set_alpha_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    float a = (float)params[1] / 255.0f;
    return g3d_entity_impl_set_alpha(entity_id, a);
}

/* RGB tint from BennuGD's 0..255 color convention (255,255,255 = no tint). */
int64_t g3d_entity_set_color_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    return g3d_entity_impl_set_color(entity_id, (float)params[1] / 255.0f,
                                     (float)params[2] / 255.0f,
                                     (float)params[3] / 255.0f);
}

/* Blend mode, using BennuGD's blend_mode constants (BLEND_NORMAL, BLEND_ADD,
   BLEND_MULTIPLY, BLEND_SUBTRACT). Applies to the whole model tree. */
int64_t g3d_entity_set_blend_bgd(INSTANCE *my, int64_t *params) {
    return g3d_entity_impl_set_blend((int)params[0], (int)params[1]);
}

int64_t g3d_entity_set_parent_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    int parent_id = (int)params[1];
    return g3d_entity_impl_set_parent(entity_id, parent_id);
}

/* Camera wrappers - backed by a small camera pool */
#define G3D_MAX_BGD_CAMERAS 16
static G3DCamera *g_bgd_cameras[G3D_MAX_BGD_CAMERAS] = {0};
static int g_bgd_camera_count = 0;

static G3DCamera *g3d_bgd_camera_get(int id) {
    if (id < 0 || id >= g_bgd_camera_count)
        return NULL;
    return g_bgd_cameras[id];
}

int64_t g3d_camera_create_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    if (g_bgd_camera_count >= G3D_MAX_BGD_CAMERAS)
        return -1;

    G3DCamera *cam = g3d_camera_impl_create(G3D_CAMERA_PERSPECTIVE);
    if (!cam)
        return -1;

    /* Sensible default perspective (aspect 16:9, 60 deg FOV) */
    g3d_camera_set_perspective(cam, 60.0f, 16.0f / 9.0f, 0.1f, 1000.0f);
    g3d_camera_set_position_impl(cam, vec3_make(0, 0, 0));

    int id = g_bgd_camera_count++;
    g_bgd_cameras[id] = cam;
    return id;
}

int64_t g3d_camera_set_active_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    if (!cam)
        return 0;
    g3d_renderer_set_camera(cam);
    return 1;
}

int64_t g3d_camera_set_position_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    if (!cam)
        return 0;
    float x = *(float *)&params[1];
    float y = *(float *)&params[2];
    float z = *(float *)&params[3];
    g3d_camera_set_position_impl(cam, vec3_make(x, y, z));
    return 1;
}

int64_t g3d_camera_look_at_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    if (!cam)
        return 0;
    float tx = *(float *)&params[1];
    float ty = *(float *)&params[2];
    float tz = *(float *)&params[3];
    float ux = *(float *)&params[4];
    float uy = *(float *)&params[5];
    float uz = *(float *)&params[6];
    g3d_camera_look_at_impl(cam, vec3_make(tx, ty, tz), vec3_make(ux, uy, uz));
    return 1;
}

int64_t g3d_camera_free_bgd(INSTANCE *my, int64_t *params) { return 1; }
int64_t g3d_camera_update_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    if (!cam)
        return 0;
    g3d_camera_update(cam);
    return 1;
}

/* Entity mesh assignment */
int64_t g3d_entity_set_mesh_bgd(INSTANCE *my, int64_t *params) {
    int entity_id = (int)params[0];
    G3DMesh *mesh = (G3DMesh *)(intptr_t)params[1];

    G3DEntity *entity = g3d_entity_impl_get(entity_id);
    if (!entity)
        return 0;

    /* Store full 64-bit pointer; flag model_id as "has mesh" */
    entity->mesh = (void *)mesh;
    entity->model_id = mesh ? 0 : -1;
    return 1;
}

/* Primitive mesh creation */
int64_t g3d_primitive_cube_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = g3d_primitive_create_cube();
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

int64_t g3d_primitive_sphere_bgd(INSTANCE *my, int64_t *params) {
    int segments = (int)params[0];
    G3DMesh *mesh = g3d_primitive_create_sphere(segments);
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

int64_t g3d_primitive_plane_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = g3d_primitive_create_plane();
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

int64_t g3d_primitive_terrain_bgd(INSTANCE *my, int64_t *params) {
    int grid = (int)params[0];
    float world_size = *(float *)&params[1];
    float height = *(float *)&params[2];
    float tiling = *(float *)&params[3];
    unsigned int seed = (unsigned int)params[4];
    G3DMesh *mesh =
        g3d_primitive_create_terrain(grid, world_size, height, tiling, seed);
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

int64_t g3d_terrain_get_height_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = (G3DMesh *)(intptr_t)params[0];
    float x = *(float *)&params[1];
    float z = *(float *)&params[2];
    float h = g3d_terrain_get_height(mesh, x, z);
    return (int64_t) * (int32_t *)&h;
}

int64_t g3d_terrain_raise_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = (G3DMesh *)(intptr_t)params[0];
    float x = *(float *)&params[1];
    float z = *(float *)&params[2];
    float radius = *(float *)&params[3];
    float strength = *(float *)&params[4];
    return g3d_terrain_raise(mesh, x, z, radius, strength);
}

int64_t g3d_terrain_smooth_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = (G3DMesh *)(intptr_t)params[0];
    float x = *(float *)&params[1];
    float z = *(float *)&params[2];
    float radius = *(float *)&params[3];
    float amount = *(float *)&params[4];
    return g3d_terrain_smooth(mesh, x, z, radius, amount);
}

int64_t g3d_terrain_flatten_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *mesh = (G3DMesh *)(intptr_t)params[0];
    float x = *(float *)&params[1];
    float z = *(float *)&params[2];
    float radius = *(float *)&params[3];
    float target = *(float *)&params[4];
    float amount = *(float *)&params[5];
    return g3d_terrain_flatten(mesh, x, z, radius, target, amount);
}

int64_t g3d_primitive_cliffs_bgd(INSTANCE *my, int64_t *params) {
    int grid = (int)params[0];
    float world_size = *(float *)&params[1];
    float height = *(float *)&params[2];
    float tiling = *(float *)&params[3];
    unsigned int seed = (unsigned int)params[4];
    float steepness = *(float *)&params[5];
    float water_floor = *(float *)&params[6];
    G3DMesh *mesh = g3d_primitive_create_cliffs(grid, world_size, height, tiling,
                                                seed, steepness, water_floor);
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

int64_t g3d_primitive_mountain_bgd(INSTANCE *my, int64_t *params) {
    int grid = (int)params[0];
    float world_size = *(float *)&params[1];
    float peak = *(float *)&params[2];
    float tiling = *(float *)&params[3];
    unsigned int seed = (unsigned int)params[4];
    float channel = *(float *)&params[5];
    G3DMesh *mesh = g3d_primitive_create_mountain(grid, world_size, peak, tiling,
                                                  seed, channel);
    if (!mesh)
        return -1;
    g3d_mesh_upload_gpu(mesh);
    return (int64_t)(intptr_t)mesh;
}

/* Material texture assignment */
int64_t g3d_material_set_texture_bgd(INSTANCE *my, int64_t *params) {
    int material_id = (int)params[0];
    int texture_type = (int)params[1];
    G3DTexture *texture = (G3DTexture *)(intptr_t)params[2];

    G3DMaterial *material = g3d_material_impl_get(material_id);
    if (!material)
        return 0;

    /* Slot 0 = albedo: store the full 64-bit texture pointer */
    if (texture_type == 0) {
        material->albedo_texture = (void *)texture;
        material->albedo_texture_id = texture ? 0 : -1;
    } else {
        g3d_material_impl_set_map(material_id, texture_type, (void *)texture);
    }
    return 1;
}
int64_t g3d_material_set_map_bgd(INSTANCE *my, int64_t *params) {
    /* (material, type 1=normal/2=metallic/3=roughness, texture handle) */
    g3d_material_impl_set_map((int)params[0], (int)params[1], (void *)(intptr_t)params[2]);
    return 1;
}

/* Texture & Model wrappers */
int64_t g3d_texture_load_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *filename = (const char *)string_get(params[0]);
    G3DTexture *tex = g3d_texture_load_impl(filename);
    string_discard(params[0]);
    if (!tex)
        return -1;
    g3d_texture_upload_gpu(tex);
    return (int64_t)(intptr_t)tex;
}

int64_t g3d_model_load_gltf_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *filename = (const char *)string_get(params[0]);
    G3DModel *model = g3d_gltf_load(filename);
    string_discard(params[0]);
    if (!model)
        return -1;
    return (int64_t)(intptr_t)model;
}

int64_t g3d_gltf_set_recenter_bgd(INSTANCE *my, int64_t *params) {
    g3d_gltf_set_recenter((int)params[0]);
    return 1;
}

int64_t g3d_model_load_gltf_fractured_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *filename = (const char *)string_get(params[0]);
    G3DModel *model = g3d_gltf_load_fractured(filename);
    string_discard(params[0]);
    if (!model)
        return -1;
    return (int64_t)(intptr_t)model;
}

int64_t g3d_model_load_obj_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *filename = (const char *)string_get(params[0]);
    G3DModel *model = g3d_obj_load(filename);
    string_discard(params[0]);
    return model ? (int64_t)(intptr_t)model : -1;
}
int64_t g3d_model_load_fbx_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    const char *filename = (const char *)string_get(params[0]);
    G3DModel *model = g3d_fbx_load(filename);
    string_discard(params[0]);
    return model ? (int64_t)(intptr_t)model : -1;
}

int64_t g3d_model_mesh_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    if (!model || model->mesh_count == 0)
        return -1;
    return (int64_t)(intptr_t)&model->meshes[0];
}

int64_t g3d_model_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    if (!model || !model->albedo_texture)
        return -1;
    return (int64_t)(intptr_t)model->albedo_texture;
}

int64_t g3d_model_orient_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    float rx = *(float *)&params[1];
    float ry = *(float *)&params[2];
    float rz = *(float *)&params[3];
    g3d_model_orient(model, rx, ry, rz);
    return 1;
}

int64_t g3d_model_submesh_count_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    return model ? (int64_t)model->mesh_count : 0;
}

/* Build a simplified LOD copy of a submesh; returns a mesh handle usable in
   g3d_instances_create (for a distance LOD level). grid: cells along the longest
   axis (e.g. 10 aggressive .. 24 mild). */
int64_t g3d_model_submesh_lod_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int i = (int)params[1];
    int grid = (int)params[2];
    if (!model || i < 0 || i >= (int)model->mesh_count) return -1;
    G3DMesh *lod = g3d_mesh_simplify(&model->meshes[i], grid);
    return lod ? (int64_t)(intptr_t)lod : -1;
}

/* ---- Skeletal animation ---- */
int64_t g3d_model_anim_count_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    return (int64_t)g3d_model_animation_count(model);
}

int64_t g3d_model_anim_duration_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int anim = (int)params[1];
    float d = g3d_model_animation_duration(model, anim);
    return (int64_t) * (int32_t *)&d;
}

int64_t g3d_model_animate_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int anim = (int)params[1];
    float time = *(float *)&params[2];
    int loop = (int)params[3];
    g3d_model_animate(model, anim, time, loop);
    return 1;
}

int64_t g3d_model_animate_blend_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    g3d_model_animate_blend(model, (int)params[1], *(float *)&params[2],
                            (int)params[3], *(float *)&params[4],
                            *(float *)&params[5], (int)params[6]);
    return 1;
}

int64_t g3d_model_rest_pose_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    g3d_model_rest_pose(model);
    return 1;
}

int64_t g3d_model_lock_root_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    g3d_model_set_lock_root(model, (int)params[1]);
    return 1;
}

/* ---- Collision ---- */
int64_t g3d_entity_set_collider_bgd(INSTANCE *my, int64_t *params) {
    G3DEntity *e = g3d_entity_impl_get((int)params[0]);
    if (e) e->collider = (int)params[1] ? 1 : 0;
    return 1;
}

int64_t g3d_collide_move_bgd(INSTANCE *my, int64_t *params) {
    float x = *(float *)&params[0];
    float y = *(float *)&params[1];
    float z = *(float *)&params[2];
    float radius = *(float *)&params[3];
    float height = *(float *)&params[4];
    float dx = *(float *)&params[5];
    float dz = *(float *)&params[6];
    return g3d_collide_move_character(x, y, z, radius, height, dx, dz);
}

int64_t g3d_collide_x_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_collide_result_x();
    return (int64_t) * (int32_t *)&v;
}

int64_t g3d_collide_z_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_collide_result_z();
    return (int64_t) * (int32_t *)&v;
}

int64_t g3d_collide_floor_bgd(INSTANCE *my, int64_t *params) {
    float x = *(float *)&params[0];
    float z = *(float *)&params[1];
    float radius = *(float *)&params[2];
    float feet = *(float *)&params[3];
    float v = g3d_collide_floor(x, z, radius, feet);
    return (int64_t) * (int32_t *)&v;
}

int64_t g3d_raycast_bgd(INSTANCE *my, int64_t *params) {
    float ox = *(float *)&params[0];
    float oy = *(float *)&params[1];
    float oz = *(float *)&params[2];
    float dx = *(float *)&params[3];
    float dy = *(float *)&params[4];
    float dz = *(float *)&params[5];
    float maxd = *(float *)&params[6];
    float v = g3d_collide_raycast(ox, oy, oz, dx, dy, dz, maxd);
    return (int64_t) * (int32_t *)&v;
}

int64_t g3d_ray_hit_x_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_collide_hit_x();
    return (int64_t) * (int32_t *)&v;
}
int64_t g3d_ray_hit_y_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_collide_hit_y();
    return (int64_t) * (int32_t *)&v;
}
int64_t g3d_ray_hit_z_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_collide_hit_z();
    return (int64_t) * (int32_t *)&v;
}
int64_t g3d_ray_entity_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g3d_collide_hit_entity();
}

/* Full model height (max Y extent across all submeshes), as a float */
int64_t g3d_model_height_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    float h = 0.0f;
    if (model && model->mesh_count > 0) {
        float ymin = model->meshes[0].aabb_min[1];
        float ymax = model->meshes[0].aabb_max[1];
        for (uint32_t i = 1; i < model->mesh_count; i++) {
            if (model->meshes[i].aabb_min[1] < ymin) ymin = model->meshes[i].aabb_min[1];
            if (model->meshes[i].aabb_max[1] > ymax) ymax = model->meshes[i].aabb_max[1];
        }
        h = ymax - ymin;
    }
    return (int64_t) * (int32_t *)&h;
}

/* Largest overall extent (max of width/height/depth) across ALL submeshes.
   Use it to frame a whole model with a free camera regardless of its scale. */
int64_t g3d_model_size_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    float s = 0.0f;
    if (model && model->mesh_count > 0) {
        float mn[3], mx[3];
        for (int k = 0; k < 3; k++) { mn[k] = model->meshes[0].aabb_min[k]; mx[k] = model->meshes[0].aabb_max[k]; }
        for (uint32_t i = 1; i < model->mesh_count; i++)
            for (int k = 0; k < 3; k++) {
                if (model->meshes[i].aabb_min[k] < mn[k]) mn[k] = model->meshes[i].aabb_min[k];
                if (model->meshes[i].aabb_max[k] > mx[k]) mx[k] = model->meshes[i].aabb_max[k];
            }
        float ex = mx[0] - mn[0], ey = mx[1] - mn[1], ez = mx[2] - mn[2];
        s = ex; if (ey > s) s = ey; if (ez > s) s = ez;
    }
    return (int64_t) * (int32_t *)&s;
}

int64_t g3d_model_submesh_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int i = (int)params[1];
    if (!model || i < 0 || i >= (int)model->mesh_count)
        return -1;
    return (int64_t)(intptr_t)&model->meshes[i];
}

int64_t g3d_model_submesh_map_bgd(INSTANCE *my, int64_t *params) {
    /* (model, submesh, type 1=normal/2=metallic/3=roughness) -> texture handle or -1 */
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int i = (int)params[1], type = (int)params[2];
    if (!model || i < 0 || i >= (int)model->mesh_count) return -1;
    void **arr = (type == 1) ? model->mesh_normal : (type == 2) ? model->mesh_metallic :
                 (type == 3) ? model->mesh_roughness : NULL;
    if (!arr || !arr[i]) return -1;
    return (int64_t)(intptr_t)arr[i];
}

int64_t g3d_model_submesh_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DModel *model = (G3DModel *)(intptr_t)params[0];
    int i = (int)params[1];
    if (!model || i < 0 || i >= (int)model->mesh_count || !model->mesh_textures ||
        !model->mesh_textures[i])
        return -1;
    return (int64_t)(intptr_t)model->mesh_textures[i];
}

/* Per-submesh AABB in model space (centre + half-extents), for fracture chunks:
   each chunk = one submesh -> its own rigid body sized/placed from these. */
static float g3d_submesh_aabb(void *mp, int i, int comp, int half) {
    G3DModel *m = (G3DModel *)mp;
    if (!m || i < 0 || i >= (int)m->mesh_count) return 0.0f;
    float lo = m->meshes[i].aabb_min[comp], hi = m->meshes[i].aabb_max[comp];
    return half ? (hi - lo) * 0.5f : (hi + lo) * 0.5f;
}
/* Expose a submesh's CPU vertex positions (+ triangle indices) so the physics
   backend can build convex hulls and static mesh colliders. Returns the vertex
   count (0 on failure); *pos -> first position, *stride_floats = floats between
   consecutive vertices. Declared in libmod_3d_physics.h and used by the Jolt
   backend. */
int g3d_physics_submesh_geom(void *mp, int i, const float **pos, int *stride_floats,
                             const unsigned int **indices, int *icount) {
    G3DModel *m = (G3DModel *)mp;
    if (!m || i < 0 || i >= (int)m->mesh_count) return 0;
    G3DMesh *mesh = &m->meshes[i];
    if (!mesh->vertices || mesh->vertex_count == 0) return 0;
    if (pos)           *pos = mesh->vertices[0].position;
    if (stride_floats) *stride_floats = (int)(sizeof(G3DVertex) / sizeof(float));
    if (indices)       *indices = mesh->indices;
    if (icount)        *icount = (int)mesh->index_count;
    return (int)mesh->vertex_count;
}

int64_t g3d_model_submesh_cx_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 0, 0); return (int64_t)*(int32_t*)&v; }
int64_t g3d_model_submesh_cy_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 1, 0); return (int64_t)*(int32_t*)&v; }
int64_t g3d_model_submesh_cz_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 2, 0); return (int64_t)*(int32_t*)&v; }
int64_t g3d_model_submesh_hx_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 0, 1); return (int64_t)*(int32_t*)&v; }
int64_t g3d_model_submesh_hy_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 1, 1); return (int64_t)*(int32_t*)&v; }
int64_t g3d_model_submesh_hz_bgd(INSTANCE *my, int64_t *params) { float v = g3d_submesh_aabb((void*)(intptr_t)params[0], (int)params[1], 2, 1); return (int64_t)*(int32_t*)&v; }

int64_t g3d_rigidbody_set_model_offset_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_set_model_offset((int)params[0], *(float*)&params[1], *(float*)&params[2], *(float*)&params[3]);
    return 1;
}

/* Merge ALL of a model's submeshes into one mesh (positions scaled by s), then
   decimate it -> a single low-poly mesh for the far-LOD of detailed multi-part
   models (e.g. a 61-submesh ship: 61 draws -> 1 far away). One texture only
   (the model albedo); fine at distance. Returns NULL if not worth it. */
static G3DMesh *build_model_merged_lod(G3DModel *model, float s) {
    if (!model || model->mesh_count < 4) return NULL;
    uint32_t tv = 0, ti = 0;
    for (uint32_t i = 0; i < model->mesh_count; i++) { tv += model->meshes[i].vertex_count; ti += model->meshes[i].index_count; }
    if (tv < 48 || ti < 3) return NULL;
    /* Skip environment-scale models (whole maps): merging them would (a) alloc a
       giant temporary buffer and (b) collapse the ENTIRE map to one blob whenever
       the camera is far from the model origin -- which is always, inside a map.
       Such models keep per-submesh culling + per-mesh LOD instead. */
    if (tv > 1000000u || model->mesh_count > 512) return NULL;
    G3DVertex *verts = (G3DVertex *)malloc((size_t)tv * sizeof(G3DVertex));
    uint32_t *idx = (uint32_t *)malloc((size_t)ti * sizeof(uint32_t));
    if (!verts || !idx) { free(verts); free(idx); return NULL; }
    uint32_t vo = 0, io = 0;
    for (uint32_t i = 0; i < model->mesh_count; i++) {
        G3DMesh *sm = &model->meshes[i];
        for (uint32_t v = 0; v < sm->vertex_count; v++) {
            verts[vo + v] = sm->vertices[v];
            verts[vo + v].position[0] *= s; verts[vo + v].position[1] *= s; verts[vo + v].position[2] *= s;
        }
        for (uint32_t k = 0; k < sm->index_count; k++) idx[io + k] = sm->indices[k] + vo;
        vo += sm->vertex_count; io += sm->index_count;
    }
    G3DMesh *merged = g3d_mesh_create("merged", verts, tv, idx, ti);
    free(verts); free(idx);
    if (!merged) return NULL;
    G3DMesh *lod = g3d_mesh_simplify(merged, 16);   /* decimate the whole thing */
    g3d_mesh_free(merged);
    return lod;
}

/* Spawn a whole model (all submeshes) under one empty root entity; returns the
   root id. Move/rotate/scale the root to move the entire model. */
int g3d_model_spawn(int scene_id, void *model_ptr, float x, float y, float z,
                    float height, float roty) {
    G3DModel *model = (G3DModel *)model_ptr;
    if (!model || model->mesh_count == 0) return -1;

    /* uniform scale so the model stands `height` tall (height<=0 keeps 1:1) */
    float s = 1.0f;
    if (height > 0.0f) {
        float ymin = model->meshes[0].aabb_min[1], ymax = model->meshes[0].aabb_max[1];
        for (uint32_t i = 1; i < model->mesh_count; i++) {
            if (model->meshes[i].aabb_min[1] < ymin) ymin = model->meshes[i].aabb_min[1];
            if (model->meshes[i].aabb_max[1] > ymax) ymax = model->meshes[i].aabb_max[1];
        }
        float mh = ymax - ymin;
        if (mh > 1e-6f) s = height / mh;
    }

    int root = g3d_entity_impl_spawn(scene_id, 0, x, y, z);   /* empty root (no mesh -> not drawn) */
    if (root < 0) return -1;
    g3d_entity_impl_set_rotation(root, 0.0f, roty, 0.0f);

    for (uint32_t j = 0; j < model->mesh_count; j++) {
        int mat = g3d_material_impl_create();
        G3DMaterial *m = g3d_material_impl_get(mat);
        if (m) {
            void *alb = model->mesh_textures ? model->mesh_textures[j] : NULL;
            m->albedo_texture = alb;
            m->albedo_texture_id = alb ? 0 : -1;
            /* Static map/prop geometry is matte: a glossy default (0.5) puts a
               grazing specular sheen on flat ground toward the horizon that reads
               as a bright "seam". High roughness keeps it flat and even. */
            m->roughness = 0.9f;
            m->metallic  = 0.0f;
            if (model->mesh_normal && model->mesh_normal[j])       g3d_material_impl_set_map(mat, 1, model->mesh_normal[j]);
            if (model->mesh_metallic && model->mesh_metallic[j])   g3d_material_impl_set_map(mat, 2, model->mesh_metallic[j]);
            if (model->mesh_roughness && model->mesh_roughness[j]) g3d_material_impl_set_map(mat, 3, model->mesh_roughness[j]);
        }
        int ent = g3d_entity_impl_spawn(scene_id, 0, 0.0f, 0.0f, 0.0f);
        if (ent < 0) continue;
        G3DEntity *e = g3d_entity_impl_get(ent);
        if (e) e->mesh = &model->meshes[j];
        g3d_entity_impl_set_material(ent, mat);
        g3d_entity_impl_set_scale(ent, s, s, s);
        g3d_entity_impl_set_parent(ent, root);               /* child of the root -> moves with it */
    }

    /* Far-LOD: one merged, decimated mesh drawn (with the model albedo) instead
       of all the submesh children when the model is beyond g3d_set_lod distance. */
    G3DMesh *mlod = build_model_merged_lod(model, s);
    if (mlod) {
        G3DEntity *re = g3d_entity_impl_get(root);
        if (re) {
            re->lod_mesh = mlod;
            int lmat = g3d_material_impl_create();
            G3DMaterial *lm = g3d_material_impl_get(lmat);
            if (lm) {
                /* The merged mesh mixes many UV spaces, so a single texture can't
                   map right -> flat-shade it with the AVERAGE colour of the model's
                   main texture (clean coloured low-poly instead of white). */
                lm->albedo_texture = NULL;
                lm->albedo_texture_id = -1;
                unsigned long long r = 0, g = 0, b = 0, n = 0;
                if (model->mesh_textures) {
                    for (uint32_t i = 0; i < model->mesh_count; i++) {
                        G3DTexture *t = (G3DTexture *)model->mesh_textures[i];
                        if (!t || !t->data || t->data_size < 4) continue;
                        int ch = (t->channels >= 3) ? (int)t->channels : 4;
                        size_t step = (size_t)ch * 16;   /* sparse sample: fast + representative */
                        for (size_t p = 0; p + 2 < t->data_size; p += step) { r += t->data[p]; g += t->data[p+1]; b += t->data[p+2]; n++; }
                    }
                }
                if (n) { lm->color[0] = (float)r/n/255.0f; lm->color[1] = (float)g/n/255.0f; lm->color[2] = (float)b/n/255.0f; lm->color[3] = 1.0f; }
            }
            re->lod_material = lmat;
        }
    }
    return root;
}

int64_t g3d_model_spawn_bgd(INSTANCE *my, int64_t *params) {
    int scene_id = (int)params[0];
    void *model = (void *)(intptr_t)params[1];
    return g3d_model_spawn(scene_id, model, *(float *)&params[2], *(float *)&params[3],
                           *(float *)&params[4], *(float *)&params[5], G3D_MD2RAD(*(float *)&params[6]));
}

int64_t g3d_model_load_md3_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    string_discard(params[0]);
    return -1;
}

/* Material wrappers */
int64_t g3d_material_create_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    return g3d_material_impl_create();
}

int64_t g3d_material_set_color_bgd(INSTANCE *my, int64_t *params) {
    int material_id = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    float a = *(float *)&params[4];
    return g3d_material_impl_set_color(material_id, r, g, b, a);
}

int64_t g3d_material_set_metallic_bgd(INSTANCE *my, int64_t *params) {
    int material_id = (int)params[0];
    float value = *(float *)&params[1];
    return g3d_material_impl_set_metallic(material_id, value);
}

int64_t g3d_material_set_roughness_bgd(INSTANCE *my, int64_t *params) {
    int material_id = (int)params[0];
    float value = *(float *)&params[1];
    return g3d_material_impl_set_roughness(material_id, value);
}

/* Physics stubs */
int64_t g3d_physics_body_create_bgd(INSTANCE *my, int64_t *params) { return -1; }
int64_t g3d_physics_body_set_velocity_bgd(INSTANCE *my, int64_t *params) { return 1; }
int64_t g3d_physics_step_bgd(INSTANCE *my, int64_t *params) { return 1; }

/* Renderer wrappers */
int64_t g3d_renderer_set_camera_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = (G3DCamera *)(intptr_t)params[0];
    g3d_renderer_set_camera(cam);
    return 1;
}

int64_t g3d_set_clear_color_bgd(INSTANCE *my, int64_t *params) {
    float r = *(float *)&params[0];
    float g = *(float *)&params[1];
    float b = *(float *)&params[2];
    float a = *(float *)&params[3];
    g3d_renderer_set_clear_color(r, g, b, a);
    return 1;
}

int64_t g3d_set_ambient_light_bgd(INSTANCE *my, int64_t *params) {
    float r = *(float *)&params[0];
    float g = *(float *)&params[1];
    float b = *(float *)&params[2];
    float intensity = *(float *)&params[3];
    g3d_renderer_set_ambient_light(vec3_make(r, g, b), intensity);
    return 1;
}
int64_t g3d_set_fog_bgd(INSTANCE *my, int64_t *params) {
    int enabled = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    float start = *(float *)&params[4];
    float end = *(float *)&params[5];
    g3d_renderer_set_fog(enabled, vec3_make(r, g, b), start, end);
    return 1;
}

/* Sky / skybox */
int64_t g3d_sky_set_gradient_bgd(INSTANCE *my, int64_t *params) {
    float tr = *(float *)&params[0];
    float tg = *(float *)&params[1];
    float tb = *(float *)&params[2];
    float hr = *(float *)&params[3];
    float hg = *(float *)&params[4];
    float hb = *(float *)&params[5];
    g3d_sky_set_gradient(tr, tg, tb, hr, hg, hb);
    return 1;
}

int64_t g3d_sky_set_clouds_bgd(INSTANCE *my, int64_t *params) {
    g3d_sky_set_clouds(*(float *)&params[0], *(float *)&params[1]);
    return 1;
}

int64_t g3d_sky_set_low_clouds_bgd(INSTANCE *my, int64_t *params) {
    g3d_sky_set_low_clouds(*(float *)&params[0], *(float *)&params[1],
                           *(float *)&params[2], *(float *)&params[3]);
    return 1;
}

int64_t g3d_sky_set_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DTexture *tex = (G3DTexture *)(intptr_t)params[0];
    g3d_sky_set_texture(tex ? tex->gl_handle : 0);
    return 1;
}

int64_t g3d_sky_set_enabled_bgd(INSTANCE *my, int64_t *params) {
    g3d_sky_set_enabled((int)params[0]);
    return 1;
}

/* Relative mouse (FPS mouse-look): clean deltas, locked/hidden cursor, no edge
   stop. Call g3d_mouse_capture(1) once, then g3d_mouse_update() each frame and
   read g3d_mouse_dx()/g3d_mouse_dy(). */
static int g_mouse_dx = 0;
static int g_mouse_dy = 0;

int64_t g3d_mouse_capture_bgd(INSTANCE *my, int64_t *params) {
    int enable = (int)params[0];
    SDL_SetRelativeMouseMode(enable ? SDL_TRUE : SDL_FALSE);
    SDL_GetRelativeMouseState(NULL, NULL);  /* flush pending delta */
    g_mouse_dx = 0;
    g_mouse_dy = 0;
    return 1;
}

int64_t g3d_mouse_update_bgd(INSTANCE *my, int64_t *params) {
    SDL_GetRelativeMouseState(&g_mouse_dx, &g_mouse_dy);
    return 1;
}

int64_t g3d_mouse_dx_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g_mouse_dx;
}

int64_t g3d_mouse_dy_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g_mouse_dy;
}
int64_t g3d_set_wireframe_mode_bgd(INSTANCE *my, int64_t *params) {
    int enabled = (int)params[0];
    g3d_renderer_set_wireframe_mode(enabled);
    return 1;
}

int64_t g3d_set_shadows_bgd(INSTANCE *my, int64_t *params) {
    int enabled = (int)params[0];
    g3d_renderer_set_shadows(enabled);
    return 1;
}

/* HDR post pipeline */
int64_t g3d_set_hdr_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_hdr((int)params[0]);
    return 1;
}
int64_t g3d_set_exposure_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_exposure(*(float *)&params[0]);
    return 1;
}
int64_t g3d_set_bloom_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_bloom((int)params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}
int64_t g3d_set_tonemap_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_tonemap((int)params[0]);
    return 1;
}
int64_t g3d_set_ssao_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_ssao((int)params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}
int64_t g3d_water_set_ssr_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_set_ssr((int)params[0], *(float *)&params[1]);
    return 1;
}
int64_t g3d_set_underwater_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_underwater((int)params[0], *(float *)&params[1], *(float *)&params[2],
                                *(float *)&params[3], *(float *)&params[4]);
    return 1;
}
int64_t g3d_water_set_ocean_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_set_ocean(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}
int64_t g3d_water_set_tessellation_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_set_tessellation((int)params[0]);
    return 1;
}
/* Fire */
int64_t g3d_fire_add_bgd(INSTANCE *my, int64_t *params) {
    return g3d_fire_add(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
}
int64_t g3d_fire_clear_bgd(INSTANCE *my, int64_t *params) {
    g3d_fire_clear();
    return 1;
}

/* Water */
int64_t g3d_water_create_bgd(INSTANCE *my, int64_t *params) {
    float level = *(float *)&params[0];
    float size = *(float *)&params[1];
    int subdiv = (int)params[2];
    return g3d_water_create(level, size, subdiv);
}

int64_t g3d_water_set_waves_bgd(INSTANCE *my, int64_t *params) {
    float amp = *(float *)&params[0];
    float len = *(float *)&params[1];
    float speed = *(float *)&params[2];
    g3d_water_set_waves(amp, len, speed);
    return 1;
}

int64_t g3d_water_set_color_bgd(INSTANCE *my, int64_t *params) {
    float dr = *(float *)&params[0];
    float dg = *(float *)&params[1];
    float db = *(float *)&params[2];
    float sr = *(float *)&params[3];
    float sg = *(float *)&params[4];
    float sb = *(float *)&params[5];
    g3d_water_set_color(dr, dg, db, sr, sg, sb);
    return 1;
}

int64_t g3d_water_set_enabled_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_set_enabled((int)params[0]);
    return 1;
}

int64_t g3d_water_set_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DTexture *tex = (G3DTexture *)(intptr_t)params[0];
    g3d_water_set_texture(tex ? tex->gl_handle : 0);
    return 1;
}

int64_t g3d_water_set_reflection_bgd(INSTANCE *my, int64_t *params) {
    int enable = (int)params[0];
    float strength = *(float *)&params[1];
    g3d_water_set_reflection(enable, strength);
    return 1;
}

int64_t g3d_water_set_reflection_flip_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_set_reflection_flip((int)params[0]);
    return 1;
}

int64_t g3d_water_ripple_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_ripple(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}

int64_t g3d_water_splash_bgd(INSTANCE *my, int64_t *params) {
    g3d_water_splash(*(float *)&params[0], *(float *)&params[1],
                     *(float *)&params[2], *(float *)&params[3]);
    return 1;
}

/* Mirror */
int64_t g3d_mirror_create_bgd(INSTANCE *my, int64_t *params) {
    float px = *(float *)&params[0];
    float py = *(float *)&params[1];
    float pz = *(float *)&params[2];
    float nx = *(float *)&params[3];
    float ny = *(float *)&params[4];
    float nz = *(float *)&params[5];
    float w = *(float *)&params[6];
    float h = *(float *)&params[7];
    return (int64_t)g3d_mirror_create(px, py, pz, nx, ny, nz, w, h);
}

int64_t g3d_mirror_set_flip_bgd(INSTANCE *my, int64_t *params) {
    g3d_mirror_set_flip((int)params[0], (int)params[1]);
    return 1;
}

int64_t g3d_mirror_set_tint_bgd(INSTANCE *my, int64_t *params) {
    int idx = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    g3d_mirror_set_tint(idx, r, g, b);
    return 1;
}

int64_t g3d_mirror_clear_bgd(INSTANCE *my, int64_t *params) {
    g3d_mirror_clear();
    return 1;
}

int64_t g3d_mirror_set_distance_bgd(INSTANCE *my, int64_t *params) {
    g3d_mirror_set_max_distance(*(float *)&params[0]);
    return 1;
}

/* Instancing */
int64_t g3d_instances_create_bgd(INSTANCE *my, int64_t *params) {
    void *mesh = (params[0] > 0) ? (void *)(intptr_t)params[0] : NULL;
    void *tex = (params[1] > 0) ? (void *)(intptr_t)params[1] : NULL;
    return (int64_t)g3d_instances_create(mesh, tex);
}

int64_t g3d_instances_add_bgd(INSTANCE *my, int64_t *params) {
    int group = (int)params[0];
    float x = *(float *)&params[1];
    float y = *(float *)&params[2];
    float z = *(float *)&params[3];
    float yaw = *(float *)&params[4];
    float scale = *(float *)&params[5];
    return (int64_t)g3d_instances_add(group, x, y, z, yaw, scale);
}
int64_t g3d_instances_set_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_set((int)params[0], (int)params[1], *(float *)&params[2],
                      *(float *)&params[3], *(float *)&params[4],
                      *(float *)&params[5], *(float *)&params[6]);
    return 1;
}
int64_t g3d_instances_create_skinned_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g3d_instances_create_skinned((void *)(intptr_t)params[0],
                                                 (void *)(intptr_t)params[1],
                                                 (void *)(intptr_t)params[2]);
}
int64_t g3d_model_set_gpu_skin_bgd(INSTANCE *my, int64_t *params) {
    g3d_model_set_gpu_skin((G3DModel *)(intptr_t)params[0], (int)params[1]);
    return 1;
}

int64_t g3d_instances_set_wind_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_set_wind((int)params[0], *(float *)&params[1]);
    return 1;
}
int64_t g3d_instances_set_alpha_cut_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_set_alpha_cut((int)params[0], (int)params[1]);
    return 1;
}
int64_t g3d_set_lod_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_set_lod_distance(*(float *)&params[0]);
    return 1;
}
int64_t g3d_set_culling_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_frustum_culling((int)params[0]);
    return 1;
}
int64_t g3d_set_backface_cull_bgd(INSTANCE *my, int64_t *params) {
    g3d_renderer_set_backface_cull((int)params[0]);
    return 1;
}
/* Floating origin: shift the whole world by (-dx,-dy,-dz) so coordinates stay
   small far from the origin (float precision). The game calls this when the
   camera drifts past a threshold, then offsets its own camera + bookkeeping. */
int64_t g3d_world_rebase_bgd(INSTANCE *my, int64_t *params) {
    g3d_entity_impl_rebase(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}

/* ---- world streaming (tile ring manager) ---- */
int64_t g3d_stream_init_bgd(INSTANCE *my, int64_t *params) {
    g3d_stream_init(*(float *)&params[0], (int)params[1]); return 1;
}
int64_t g3d_stream_update_bgd(INSTANCE *my, int64_t *params) {
    g3d_stream_update(*(float *)&params[0], *(float *)&params[1]); return 1;
}
int64_t g3d_stream_load_count_bgd(INSTANCE *my, int64_t *params)   { return (int64_t)g3d_stream_load_count(); }
int64_t g3d_stream_load_x_bgd(INSTANCE *my, int64_t *params)       { return (int64_t)g3d_stream_load_x((int)params[0]); }
int64_t g3d_stream_load_z_bgd(INSTANCE *my, int64_t *params)       { return (int64_t)g3d_stream_load_z((int)params[0]); }
int64_t g3d_stream_unload_count_bgd(INSTANCE *my, int64_t *params) { return (int64_t)g3d_stream_unload_count(); }
int64_t g3d_stream_unload_x_bgd(INSTANCE *my, int64_t *params)     { return (int64_t)g3d_stream_unload_x((int)params[0]); }
int64_t g3d_stream_unload_z_bgd(INSTANCE *my, int64_t *params)     { return (int64_t)g3d_stream_unload_z((int)params[0]); }
int64_t g3d_stream_loaded_count_bgd(INSTANCE *my, int64_t *params) { return (int64_t)g3d_stream_loaded_count(); }

/* ---- procedural world generation ---- */
int64_t g3d_worldgen_set_bgd(INSTANCE *my, int64_t *params) {
    g3d_worldgen_set((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_worldgen_height_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_worldgen_height(*(float *)&params[0], *(float *)&params[1]);
    return (int64_t) * (int32_t *)&v;
}
int64_t g3d_worldgen_tile_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g3d_worldgen_tile((int)params[0], (int)params[1], (int)params[2],
                                      *(float *)&params[3], (int)params[4],
                                      *(float *)&params[5], *(float *)&params[6],
                                      (void *)(intptr_t)params[7]);
}
int64_t g3d_worldgen_tile_free_bgd(INSTANCE *my, int64_t *params) {
    g3d_worldgen_tile_free((int)params[0]); return 1;
}
int64_t g3d_worldgen_set_water_depth_bgd(INSTANCE *my, int64_t *params) {
    g3d_worldgen_set_water_depth(*(float *)&params[0]); return 1;
}
int64_t g3d_worldgen_set_biome_textures_bgd(INSTANCE *my, int64_t *params) {
    g3d_worldgen_set_biome_textures((void *)(intptr_t)params[0], (void *)(intptr_t)params[1],
                                    (void *)(intptr_t)params[2], (void *)(intptr_t)params[3],
                                    *(float *)&params[4]);
    return 1;
}

int64_t g3d_instances_set_distance_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_set_distance((int)params[0], *(float *)&params[1]);
    return 1;
}

int64_t g3d_instances_clear_bgd(INSTANCE *my, int64_t *params) {
    g3d_instances_clear((int)params[0]);
    return 1;
}

int64_t g3d_instances_count_bgd(INSTANCE *my, int64_t *params) {
    return (int64_t)g3d_instances_count((int)params[0]);
}

int64_t g3d_scatter_model_bgd(INSTANCE *my, int64_t *params) {
    void *model = (params[0] > 0) ? (void *)(intptr_t)params[0] : NULL;
    void *terrain = (params[1] > 0) ? (void *)(intptr_t)params[1] : NULL;
    int count = (int)params[2];
    float area = *(float *)&params[3];
    float target_h = *(float *)&params[4];
    float scale_var = *(float *)&params[5];
    float wind = *(float *)&params[6];
    unsigned int seed = (unsigned int)params[7];
    return (int64_t)g3d_scatter_model(model, terrain, count, area, target_h,
                                      scale_var, wind, seed);
}

/* Prefabs */
int64_t g3d_prefab_create_bgd(INSTANCE *my, int64_t *params) {
    const char *name = (const char *)string_get(params[0]);
    int id = g3d_prefab_create(name);
    string_discard(params[0]);
    return id;
}

int64_t g3d_prefab_add_box_bgd(INSTANCE *my, int64_t *params) {
    int prefab = (int)params[0];
    float px = *(float *)&params[1], py = *(float *)&params[2], pz = *(float *)&params[3];
    float sx = *(float *)&params[4], sy = *(float *)&params[5], sz = *(float *)&params[6];
    float rx = *(float *)&params[7], ry = *(float *)&params[8], rz = *(float *)&params[9];
    float r = *(float *)&params[10], g = *(float *)&params[11], b = *(float *)&params[12];
    int collider = (int)params[13];
    return g3d_prefab_add_box(prefab, px, py, pz, sx, sy, sz, rx, ry, rz, r, g, b, collider);
}

int64_t g3d_prefab_piece_texture_bgd(INSTANCE *my, int64_t *params) {
    int prefab = (int)params[0];
    const char *path = (const char *)string_get(params[1]);
    g3d_prefab_piece_texture(prefab, path);
    string_discard(params[1]);
    return 1;
}

int64_t g3d_prefab_save_bgd(INSTANCE *my, int64_t *params) {
    int prefab = (int)params[0];
    const char *file = (const char *)string_get(params[1]);
    int r = g3d_prefab_save(prefab, file);
    string_discard(params[1]);
    return r;
}

int64_t g3d_prefab_load_bgd(INSTANCE *my, int64_t *params) {
    const char *file = (const char *)string_get(params[0]);
    int id = g3d_prefab_load(file);
    string_discard(params[0]);
    return id;
}

int64_t g3d_prefab_instantiate_bgd(INSTANCE *my, int64_t *params) {
    int prefab = (int)params[0];
    int scene = (int)params[1];
    float x = *(float *)&params[2], y = *(float *)&params[3], z = *(float *)&params[4];
    float yaw = *(float *)&params[5];
    return g3d_prefab_instantiate(prefab, scene, x, y, z, yaw);
}

int64_t g3d_prefab_count_bgd(INSTANCE *my, int64_t *params) {
    return g3d_prefab_count((int)params[0]);
}

int64_t g3d_scene_load_bgd(INSTANCE *my, int64_t *params) {
    const char *file = (const char *)string_get(params[0]);
    int id = g3d_scene_load(file);
    string_discard(params[0]);
    return id;
}

int64_t g3d_scene_terrain_height_bgd(INSTANCE *my, int64_t *params) {
    float h = g3d_scene_terrain_height(*(float *)&params[0], *(float *)&params[1]);
    return (int64_t) * (int32_t *)&h;
}

int64_t g3d_scene_water_level_bgd(INSTANCE *my, int64_t *params) {
    float l = g3d_scene_water_level();
    return (int64_t) * (int32_t *)&l;
}

int64_t g3d_voxterrain_floor_bgd(INSTANCE *my, int64_t *params) {
    float f = g3d_voxterrain_floor(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2]);
    return (int64_t) * (int32_t *)&f;
}
int64_t g3d_voxterrain_solid_bgd(INSTANCE *my, int64_t *params) {
    return g3d_voxterrain_solid(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2]);
}
int64_t g3d_voxterrain_blocked_bgd(INSTANCE *my, int64_t *params) {
    return g3d_voxterrain_blocked(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                  *(float *)&params[3], *(float *)&params[4]);
}
int64_t g3d_voxterrain_worldsize_bgd(INSTANCE *my, int64_t *params) {
    float w = g3d_voxterrain_worldsize();
    return (int64_t) * (int32_t *)&w;
}

int64_t g3d_voxterrain_walk_bgd(INSTANCE *my, int64_t *params) {
    g3d_voxterrain_walk(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                        *(float *)&params[3], *(float *)&params[4], *(float *)&params[5],
                        *(float *)&params[6], *(float *)&params[7]);
    return 1;
}
int64_t g3d_voxterrain_walkx_bgd(INSTANCE *my, int64_t *params) { float v = g3d_voxterrain_walkx(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_voxterrain_walky_bgd(INSTANCE *my, int64_t *params) { float v = g3d_voxterrain_walky(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_voxterrain_walkz_bgd(INSTANCE *my, int64_t *params) { float v = g3d_voxterrain_walkz(); return (int64_t) * (int32_t *)&v; }

int64_t g3d_scene_has_spawn_bgd(INSTANCE *my, int64_t *params) { return g3d_scene_has_spawn(); }
int64_t g3d_scene_spawn_x_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_spawn_x(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_scene_spawn_y_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_spawn_y(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_scene_spawn_z_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_spawn_z(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_scene_player_radius_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_player_radius(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_scene_player_height_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_player_height(); return (int64_t) * (int32_t *)&v; }
int64_t g3d_scene_player_climb_bgd(INSTANCE *my, int64_t *params) { float v = g3d_scene_player_climb(); return (int64_t) * (int32_t *)&v; }

/* Mouse -> world picking */
int64_t g3d_pick_terrain_bgd(INSTANCE *my, int64_t *params) {
    extern G3DCamera *g3d_bgd_camera_get(int id);
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    float sx = *(float *)&params[1];
    float sy = *(float *)&params[2];
    float w = *(float *)&params[3];
    float h = *(float *)&params[4];
    G3DMesh *terrain = (params[5] > 0) ? (G3DMesh *)(intptr_t)params[5] : NULL;
    return g3d_pick_terrain(cam, sx, sy, w, h, terrain);
}
int64_t g3d_pick_x_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_pick_x(); return (int64_t) * (int32_t *)&v;
}
int64_t g3d_pick_y_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_pick_y(); return (int64_t) * (int32_t *)&v;
}
int64_t g3d_pick_z_bgd(INSTANCE *my, int64_t *params) {
    float v = g3d_pick_z(); return (int64_t) * (int32_t *)&v;
}

/* Terrain texture painting */
int64_t g3d_paint_create_bgd(INSTANCE *my, int64_t *params) {
    G3DPaintCanvas *c = g3d_paint_create((int)params[0], (int)params[1]);
    return (int64_t)(intptr_t)c;
}
int64_t g3d_paint_fill_bgd(INSTANCE *my, int64_t *params) {
    G3DPaintCanvas *c = (G3DPaintCanvas *)(intptr_t)params[0];
    G3DTexture *src = (params[1] > 0) ? (G3DTexture *)(intptr_t)params[1] : NULL;
    float tiling = *(float *)&params[2];
    g3d_paint_fill(c, src, tiling);
    g3d_paint_upload(c);
    return 1;
}
int64_t g3d_terrain_paint_bgd(INSTANCE *my, int64_t *params) {
    G3DMesh *terrain = (G3DMesh *)(intptr_t)params[0];
    G3DPaintCanvas *c = (G3DPaintCanvas *)(intptr_t)params[1];
    G3DTexture *src = (params[2] > 0) ? (G3DTexture *)(intptr_t)params[2] : NULL;
    float st = *(float *)&params[3];
    float wx = *(float *)&params[4];
    float wz = *(float *)&params[5];
    float radius = *(float *)&params[6];
    float opacity = *(float *)&params[7];
    return g3d_terrain_paint(terrain, c, src, st, wx, wz, radius, opacity);
}
int64_t g3d_paint_get_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DPaintCanvas *c = (G3DPaintCanvas *)(intptr_t)params[0];
    return (int64_t)(intptr_t)g3d_paint_get_texture(c);
}
int64_t g3d_paint_save_bgd(INSTANCE *my, int64_t *params) {
    G3DPaintCanvas *c = (G3DPaintCanvas *)(intptr_t)params[0];
    const char *file = (const char *)string_get(params[1]);
    int r = g3d_paint_save(c, file);
    string_discard(params[1]);
    return r;
}

/* Cloth */
int64_t g3d_cloth_create_bgd(INSTANCE *my, int64_t *params) {
    float w = *(float *)&params[0], h = *(float *)&params[1];
    int nx = (int)params[2], ny = (int)params[3];
    float px = *(float *)&params[4], py = *(float *)&params[5], pz = *(float *)&params[6];
    return g3d_cloth_create(w, h, nx, ny, px, py, pz);
}
int64_t g3d_cloth_pin_bgd(INSTANCE *my, int64_t *params) {
    g3d_cloth_pin((int)params[0], (int)params[1]); return 1;
}
int64_t g3d_cloth_set_wind_bgd(INSTANCE *my, int64_t *params) {
    g3d_cloth_set_wind((int)params[0], *(float *)&params[1], *(float *)&params[2],
                       *(float *)&params[3], *(float *)&params[4]);
    return 1;
}
int64_t g3d_cloth_set_collider_bgd(INSTANCE *my, int64_t *params) {
    g3d_cloth_set_collider((int)params[0], *(float *)&params[1], *(float *)&params[2],
                           *(float *)&params[3], *(float *)&params[4]);
    return 1;
}
int64_t g3d_cloth_clear_collider_bgd(INSTANCE *my, int64_t *params) {
    g3d_cloth_clear_collider((int)params[0]); return 1;
}
int64_t g3d_cloth_set_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DTexture *t = (params[1] > 0) ? (G3DTexture *)(intptr_t)params[1] : NULL;
    g3d_cloth_set_texture((int)params[0], t ? t->gl_handle : 0);
    return 1;
}
int64_t g3d_cloth_update_bgd(INSTANCE *my, int64_t *params) {
    g3d_cloth_update((int)params[0], *(float *)&params[1]); return 1;
}

int64_t g3d_scatter_mesh_bgd(INSTANCE *my, int64_t *params) {
    void *mesh = (params[0] > 0) ? (void *)(intptr_t)params[0] : NULL;
    void *tex = (params[1] > 0) ? (void *)(intptr_t)params[1] : NULL;
    void *terrain = (params[2] > 0) ? (void *)(intptr_t)params[2] : NULL;
    int count = (int)params[3];
    float area = *(float *)&params[4];
    float smin = *(float *)&params[5];
    float smax = *(float *)&params[6];
    float wind = *(float *)&params[7];
    unsigned int seed = (unsigned int)params[8];
    return (int64_t)g3d_scatter_mesh(mesh, tex, terrain, count, area, smin, smax,
                                     wind, seed);
}

/* Flow (waterfalls / rivers) */
int64_t g3d_flow_add_bgd(INSTANCE *my, int64_t *params) {
    float tx = *(float *)&params[0];
    float ty = *(float *)&params[1];
    float tz = *(float *)&params[2];
    float bx = *(float *)&params[3];
    float by = *(float *)&params[4];
    float bz = *(float *)&params[5];
    float width = *(float *)&params[6];
    float speed = *(float *)&params[7];
    float tiling = *(float *)&params[8];
    return g3d_flow_add(tx, ty, tz, bx, by, bz, width, speed, tiling);
}

int64_t g3d_flow_add_river_bgd(INSTANCE *my, int64_t *params) {
    void *terrain = (void *)(intptr_t)params[0];
    float x0 = *(float *)&params[1];
    float z0 = *(float *)&params[2];
    float x1 = *(float *)&params[3];
    float z1 = *(float *)&params[4];
    float width = *(float *)&params[5];
    float y_offset = *(float *)&params[6];
    float speed = *(float *)&params[7];
    float tiling = *(float *)&params[8];
    return g3d_flow_add_river(terrain, x0, z0, x1, z1, width, y_offset, speed,
                              tiling);
}

int64_t g3d_flow_set_texture_bgd(INSTANCE *my, int64_t *params) {
    G3DTexture *tex = (G3DTexture *)(intptr_t)params[0];
    g3d_flow_set_texture(tex ? tex->gl_handle : 0);
    return 1;
}

int64_t g3d_flow_set_color_bgd(INSTANCE *my, int64_t *params) {
    float r = *(float *)&params[0];
    float g = *(float *)&params[1];
    float b = *(float *)&params[2];
    g3d_flow_set_color(r, g, b);
    return 1;
}

int64_t g3d_flow_clear_bgd(INSTANCE *my, int64_t *params) {
    g3d_flow_clear();
    return 1;
}

int64_t g3d_flow_set_clip_bgd(INSTANCE *my, int64_t *params) {
    float y = *(float *)&params[0];
    g3d_flow_set_clip(y);
    return 1;
}

/* Particles */
int64_t g3d_particles_create_bgd(INSTANCE *my, int64_t *params) {
    float x = *(float *)&params[0];
    float y = *(float *)&params[1];
    float z = *(float *)&params[2];
    float ex = *(float *)&params[3];
    float ey = *(float *)&params[4];
    float ez = *(float *)&params[5];
    float vx = *(float *)&params[6];
    float vy = *(float *)&params[7];
    float vz = *(float *)&params[8];
    float spread = *(float *)&params[9];
    float gravity = *(float *)&params[10];
    float size = *(float *)&params[11];
    float life = *(float *)&params[12];
    int count = (int)params[13];
    return g3d_particles_create(x, y, z, ex, ey, ez, vx, vy, vz, spread, gravity,
                                size, life, count);
}

int64_t g3d_particles_set_color_bgd(INSTANCE *my, int64_t *params) {
    int id = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    g3d_particles_set_color(id, r, g, b);
    return 1;
}

int64_t g3d_particles_set_floor_bgd(INSTANCE *my, int64_t *params) {
    int id = (int)params[0];
    float y = *(float *)&params[1];
    g3d_particles_set_floor(id, y);
    return 1;
}

int64_t g3d_particles_clear_bgd(INSTANCE *my, int64_t *params) {
    g3d_particles_clear();
    return 1;
}

/* Lighting wrappers */
int64_t g3d_light_create_bgd(INSTANCE *my, int64_t *params) {
    g3d_ensure_init();
    int type = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    return g3d_light_impl_create(type, r, g, b);
}

int64_t g3d_light_set_position_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float x = *(float *)&params[1];
    float y = *(float *)&params[2];
    float z = *(float *)&params[3];
    return g3d_light_impl_set_position(light_id, x, y, z);
}

int64_t g3d_light_set_direction_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float dx = *(float *)&params[1];
    float dy = *(float *)&params[2];
    float dz = *(float *)&params[3];
    return g3d_light_impl_set_direction(light_id, dx, dy, dz);
}

int64_t g3d_light_set_intensity_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float intensity = *(float *)&params[1];
    return g3d_light_impl_set_intensity(light_id, intensity);
}

int64_t g3d_light_set_color_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float r = *(float *)&params[1];
    float g = *(float *)&params[2];
    float b = *(float *)&params[3];
    return g3d_light_impl_set_color(light_id, r, g, b);
}

/* ----------------------------------------------------------------------------
   DAY / NIGHT CYCLE

   g3d_set_sun(t): t in [0,1) is the time of day. Drives the scene's main
   directional light (direction + color + intensity), the ambient light and the
   sky (clear) colour through sunrise -> noon -> sunset -> night. Encapsulated in
   C so a script only has to advance t.
   ---------------------------------------------------------------------------- */

static float g3d_clampf(float v, float a, float b) {
    return v < a ? a : (v > b ? b : v);
}
static float g3d_lerpf(float a, float b, float t) { return a + (b - a) * t; }

int64_t g3d_set_sun_bgd(INSTANCE *my, int64_t *params) {
    float t = *(float *)&params[0];
    float a = t * 6.2831853f; /* full circle */
    float ca = cosf(a);
    float sa = sinf(a); /* sun elevation: >0 day, <0 night */

    /* Light travel direction (from sun toward ground), with a constant Z tilt
       so shadows have some depth. */
    float dx = -ca, dy = -sa, dz = 0.35f;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (len < 1e-6f) len = 1.0f;
    dx /= len; dy /= len; dz /= len;

    float day = g3d_clampf(sa, 0.0f, 1.0f);            /* 0 night .. 1 noon */
    float warmth = 1.0f - g3d_clampf(sa / 0.30f, 0.0f, 1.0f); /* 1 near horizon */

    /* Sun colour: warm orange near the horizon, near-white at noon */
    float lr = 1.0f;
    float lg = g3d_lerpf(0.97f, 0.45f, warmth);
    float lb = g3d_lerpf(0.92f, 0.22f, warmth);
    float intensity = day * 1.6f;

    /* Ambient: dim blue at night, bright at midday */
    float ar = g3d_lerpf(0.05f, 0.40f, day);
    float ag = g3d_lerpf(0.06f, 0.42f, day);
    float ab = g3d_lerpf(0.12f, 0.46f, day);

    /* Sky: dark blue night -> light blue day, with warm horizon at dawn/dusk */
    float skr = g3d_lerpf(0.02f, 0.45f, day);
    float skg = g3d_lerpf(0.03f, 0.65f, day);
    float skb = g3d_lerpf(0.07f, 0.95f, day);
    float wf = warmth * g3d_clampf(sa * 3.0f, 0.0f, 1.0f);
    skr = g3d_lerpf(skr, 0.85f, wf);
    skg = g3d_lerpf(skg, 0.50f, wf);
    skb = g3d_lerpf(skb, 0.32f, wf);

    /* Apply to the active scene's main directional light */
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            g3d_light_impl_set_direction(lids[i], dx, dy, dz);
            g3d_light_impl_set_color(lids[i], lr, lg, lb);
            g3d_light_impl_set_intensity(lids[i], intensity);
            break;
        }
    }
    g3d_renderer_set_ambient_light(vec3_make(ar, ag, ab), 1.0f);
    g3d_renderer_set_clear_color(skr, skg, skb, 1.0f);
    return 1;
}

int64_t g3d_light_set_range_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float range = *(float *)&params[1];
    return g3d_light_impl_set_range(light_id, range);
}

int64_t g3d_light_set_cone_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    float angle = *(float *)&params[1];
    return g3d_light_impl_set_cone(light_id, angle);
}

int64_t g3d_light_enable_shadow_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    int enabled = (int)params[1];
    return g3d_light_impl_enable_shadow(light_id, enabled);
}

int64_t g3d_light_set_shadow_quality_bgd(INSTANCE *my, int64_t *params) {
    int light_id = (int)params[0];
    int resolution = (int)params[1];
    return g3d_light_impl_set_shadow_quality(light_id, resolution);
}

int64_t g3d_camera_set_projection_bgd(INSTANCE *my, int64_t *params) { return 1; }
int64_t g3d_camera_set_fov_bgd(INSTANCE *my, int64_t *params) {
    G3DCamera *cam = g3d_bgd_camera_get((int)params[0]);
    if (!cam)
        return 0;
    float fov = *(float *)&params[1];
    g3d_camera_set_perspective(cam, fov, 16.0f / 9.0f, 0.1f, 1000.0f);
    return 1;
}

/* ---- physics: collision + character controller ---- */
int64_t g3d_char_create_bgd(INSTANCE *my, int64_t *params) {
    return g3d_char_create(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                           *(float *)&params[3], *(float *)&params[4]);
}
int64_t g3d_char_destroy_bgd(INSTANCE *my, int64_t *params) { g3d_char_destroy((int)params[0]); return 1; }
int64_t g3d_char_move_bgd(INSTANCE *my, int64_t *params) {
    g3d_char_move((int)params[0], *(float *)&params[1], *(float *)&params[2]); return 1;
}
int64_t g3d_char_jump_bgd(INSTANCE *my, int64_t *params) {
    g3d_char_jump((int)params[0], *(float *)&params[1]); return 1;
}
int64_t g3d_char_update_bgd(INSTANCE *my, int64_t *params) {
    g3d_char_update((int)params[0], *(float *)&params[1]); return 1;
}
int64_t g3d_char_set_position_bgd(INSTANCE *my, int64_t *params) {
    g3d_char_set_position((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_char_set_tuning_bgd(INSTANCE *my, int64_t *params) {
    g3d_char_set_tuning((int)params[0], *(float *)&params[1], *(float *)&params[2]); return 1;
}
int64_t g3d_char_x_bgd(INSTANCE *my, int64_t *params) { float v = g3d_char_x((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_char_y_bgd(INSTANCE *my, int64_t *params) { float v = g3d_char_y((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_char_z_bgd(INSTANCE *my, int64_t *params) { float v = g3d_char_z((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_char_grounded_bgd(INSTANCE *my, int64_t *params) { return g3d_char_grounded((int)params[0]); }
int64_t g3d_physics_set_gravity_bgd(INSTANCE *my, int64_t *params) { g3d_physics_set_gravity(*(float *)&params[0]); return 1; }
int64_t g3d_collider_add_box_bgd(INSTANCE *my, int64_t *params) {
    return g3d_collider_add_box(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                *(float *)&params[3], *(float *)&params[4], *(float *)&params[5]);
}
int64_t g3d_collider_clear_bgd(INSTANCE *my, int64_t *params) { g3d_collider_clear(); return 1; }

/* ---- raycast vehicle ---- */
int64_t g3d_vehicle_create_bgd(INSTANCE *my, int64_t *params) {
    return g3d_vehicle_create(*(float *)&params[0], *(float *)&params[1],
                              *(float *)&params[2], G3D_MD2RAD(*(float *)&params[3]));
}
int64_t g3d_vehicle_destroy_bgd(INSTANCE *my, int64_t *params) { g3d_vehicle_destroy((int)params[0]); return 1; }
int64_t g3d_vehicle_update_bgd(INSTANCE *my, int64_t *params) {
    g3d_vehicle_update((int)params[0], *(float *)&params[1], *(float *)&params[2],
                       *(float *)&params[3], *(float *)&params[4]);
    return 1;
}
int64_t g3d_vehicle_set_geometry_bgd(INSTANCE *my, int64_t *params) {
    g3d_vehicle_set_geometry((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_vehicle_set_tuning_bgd(INSTANCE *my, int64_t *params) {
    g3d_vehicle_set_tuning((int)params[0], *(float *)&params[1], *(float *)&params[2],
                           *(float *)&params[3], *(float *)&params[4], *(float *)&params[5]);
    return 1;
}
int64_t g3d_vehicle_x_bgd(INSTANCE *my, int64_t *params) { float v = g3d_vehicle_x((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_y_bgd(INSTANCE *my, int64_t *params) { float v = g3d_vehicle_y((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_z_bgd(INSTANCE *my, int64_t *params) { float v = g3d_vehicle_z((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_yaw_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_vehicle_yaw((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_pitch_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_vehicle_pitch((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_roll_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_vehicle_roll((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_vehicle_speed_bgd(INSTANCE *my, int64_t *params) { float v = g3d_vehicle_speed((int)params[0]); return (int64_t) * (int32_t *)&v; }
/* ---- rigid bodies ---- */
int64_t g3d_rigidbody_create_bgd(INSTANCE *my, int64_t *params) {
    return g3d_rigidbody_create(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                *(float *)&params[3], *(float *)&params[4], *(float *)&params[5],
                                *(float *)&params[6]);
}
int64_t g3d_rigidbody_create_sphere_bgd(INSTANCE *my, int64_t *params) {
    return g3d_rigidbody_create_sphere(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                       *(float *)&params[3], *(float *)&params[4]);
}
int64_t g3d_rigidbody_create_capsule_bgd(INSTANCE *my, int64_t *params) {
    return g3d_rigidbody_create_capsule(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                        *(float *)&params[3], *(float *)&params[4], *(float *)&params[5]);
}
int64_t g3d_rigidbody_create_cylinder_bgd(INSTANCE *my, int64_t *params) {
    return g3d_rigidbody_create_cylinder(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                         *(float *)&params[3], *(float *)&params[4], *(float *)&params[5]);
}
int64_t g3d_rigidbody_create_convex_bgd(INSTANCE *my, int64_t *params) {
    return g3d_rigidbody_create_convex(*(float *)&params[0], *(float *)&params[1], *(float *)&params[2],
                                       (void *)(intptr_t)params[3], (int)params[4],
                                       *(float *)&params[5], *(float *)&params[6]);
}
int64_t g3d_rigidbody_set_ccd_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_set_ccd((int)params[0], (int)params[1]); return 1;
}
int64_t g3d_collider_add_mesh_bgd(INSTANCE *my, int64_t *params) {
    return g3d_collider_add_mesh((void *)(intptr_t)params[0], (int)params[1],
                                 *(float *)&params[2], *(float *)&params[3],
                                 *(float *)&params[4], *(float *)&params[5]);
}
int64_t g3d_rigidbody_destroy_bgd(INSTANCE *my, int64_t *params) { g3d_rigidbody_destroy((int)params[0]); return 1; }
int64_t g3d_rigidbody_clear_bgd(INSTANCE *my, int64_t *params) { g3d_rigidbody_clear(); return 1; }
int64_t g3d_rigidbody_step_bgd(INSTANCE *my, int64_t *params) { g3d_rigidbody_step(*(float *)&params[0]); return 1; }
int64_t g3d_rigidbody_apply_impulse_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_apply_impulse((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_rigidbody_set_velocity_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_set_velocity((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_rigidbody_set_bounce_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_set_bounce((int)params[0], *(float *)&params[1], *(float *)&params[2]);
    return 1;
}
int64_t g3d_rigidbody_x_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_x((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_y_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_y((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_z_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_z((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_grounded_bgd(INSTANCE *my, int64_t *params) { return g3d_rigidbody_grounded((int)params[0]); }
int64_t g3d_rigidbody_set_angular_velocity_bgd(INSTANCE *my, int64_t *params) {
    g3d_rigidbody_set_angular_velocity((int)params[0], *(float *)&params[1], *(float *)&params[2], *(float *)&params[3]);
    return 1;
}
int64_t g3d_rigidbody_angle_x_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_rigidbody_angle_x((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_angle_y_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_rigidbody_angle_y((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_angle_z_bgd(INSTANCE *my, int64_t *params) { float v = G3D_RAD2MD(g3d_rigidbody_angle_z((int)params[0])); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_render_x_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_render_x((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_render_y_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_render_y((int)params[0]); return (int64_t) * (int32_t *)&v; }
int64_t g3d_rigidbody_render_z_bgd(INSTANCE *my, int64_t *params) { float v = g3d_rigidbody_render_z((int)params[0]); return (int64_t) * (int32_t *)&v; }


static int g3d_object_info(void *what, REGION *clip, int64_t *key, int64_t *ready) {
    *key = INT64_MAX; /* Highest Z -> draws first / at the bottom */
    *ready = 1;

    if (!renderer_scaled_width || !renderer_scaled_height)
        return 0;

    /* Initialize the engine on the first frame if not done yet */
    if (!g_3d_initialized) {
        g3d_engine_init(renderer_scaled_width, renderer_scaled_height);
    }

    /* Keep the 3D viewport in sync with BGD's scaled physical viewport */
    g3d_renderer_set_viewport_physical((uint32_t)renderer_offset_x, (uint32_t)renderer_offset_y,
                                       (uint32_t)renderer_scaled_width, (uint32_t)renderer_scaled_height);

    return 1;
}

static void g3d_object_draw(void *what, REGION *clip) {
    if (g_3d_initialized) {
        /* Flush any queued 2D rendering (e.g. GPU_Clear or background map) */
        GPU_FlushBlitBuffer();

        /* Render directly into FBO 0 */
        g3d_engine_render();

        /* Reset SDL_gpu state so 2D sprites can draw correctly on top */
        GPU_ResetRendererState();
    }
}

/* ============================================================================
   PROCESS INSTANCE HOOK — called by libbggfx for every C_3D process each frame
   (during the info phase, before draw). Syncs 3D transforms from local vars.
   ============================================================================
 */

static void g3d_process_instance_hook( INSTANCE * i ) {
    int64_t csubtype = LOCQWORD( libmod_3d, i, LOC3D_CSUBTYPE );
    int entity_id = (int) LOCQWORD( libmod_3d, i, LOC3D_ENTITY );

    if ( !entity_id ) return;

    /* World-space position from standard x/y/z BGD locals */
    float px = (float) LOCDOUBLE( libmod_3d, i, LOC3D_COORDX );
    float py = (float) LOCDOUBLE( libmod_3d, i, LOC3D_COORDY );
    float pz = (float) LOCDOUBLE( libmod_3d, i, LOC3D_COORDZ );

    /* Rotation — stored as BGD millidegrees, convert to radians */
    float rx = G3D_MD2RAD( LOCDOUBLE( libmod_3d, i, LOC3D_ANGLE_X ) );
    float ry = G3D_MD2RAD( LOCDOUBLE( libmod_3d, i, LOC3D_ANGLE_Y ) );
    float rz = G3D_MD2RAD( LOCDOUBLE( libmod_3d, i, LOC3D_ANGLE_Z ) );

    /* Scale — same priority logic as 2D: scale_x/y/z > size_x/size_y > size */
    double size = LOCDOUBLE( libmod_3d, i, LOC3D_SIZE );  /* "size"   */
    double sx = LOCDOUBLE( libmod_3d, i, LOC3D_SIZE_X );
    double sy = LOCDOUBLE( libmod_3d, i, LOC3D_SIZE_Y );
    double sz = LOCDOUBLE( libmod_3d, i, LOC3D_SIZE_Z );
    sx = ( sx != 100.0 ) ? sx : size;
    sy = ( sy != 100.0 ) ? sy : size;
    sz = ( sz != 100.0 ) ? sz : size;
    /* Convert percentage to factor (100 = 1.0) */
    float fsx = (float)( sx * 0.01 );
    float fsy = (float)( sy * 0.01 );
    float fsz = (float)( sz * 0.01 );

    /* Target for Lights / Cameras */
    float tx = (float) LOCDOUBLE( libmod_3d, i, LOC3D_TARGET_X );
    float ty = (float) LOCDOUBLE( libmod_3d, i, LOC3D_TARGET_Y );
    float tz = (float) LOCDOUBLE( libmod_3d, i, LOC3D_TARGET_Z );

    switch ( csubtype ) {
        case 1: { /* C3D_ENTITY */
            g3d_entity_impl_set_position( entity_id, px, py, pz );
            g3d_entity_impl_set_rotation( entity_id, rx, ry, rz );
            g3d_entity_impl_set_scale(    entity_id, fsx, fsy, fsz );
            /* Standard BennuGD per-graphic locals -> obeyed automatically. */
            g3d_entity_impl_set_alpha( entity_id, LOCBYTE( libmod_3d, i, LOC3D_ALPHA ) / 255.0f );
            g3d_entity_impl_set_color( entity_id,
                                       LOCBYTE( libmod_3d, i, LOC3D_COLORR ) / 255.0f,
                                       LOCBYTE( libmod_3d, i, LOC3D_COLORG ) / 255.0f,
                                       LOCBYTE( libmod_3d, i, LOC3D_COLORB ) / 255.0f );
            int bmode = (int) LOCINT64( libmod_3d, i, LOC3D_BLENDMODE );
            g3d_entity_impl_set_blend( entity_id, bmode );
            if ( bmode == G3D_BLEND_CUSTOM )
                g3d_entity_impl_set_blend_custom( entity_id,
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_SRC_RGB ),
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_DST_RGB ),
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_SRC_ALPHA ),
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_DST_ALPHA ),
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_EQ_RGB ),
                    (int) LOCINT64( libmod_3d, i, LOC3D_BLEND_EQ_ALPHA ) );
            break;
        }
        case 2: { /* C3D_LIGHT */
            g3d_light_impl_set_position( entity_id, px, py, pz );

            float dx = tx - px;
            float dy = ty - py;
            float dz = tz - pz;
            if ( dx != 0.0f || dy != 0.0f || dz != 0.0f ) {
                g3d_light_impl_set_direction( entity_id, dx, dy, dz );
            }

            float intensity = (float) LOCDOUBLE( libmod_3d, i, LOC3D_INTENSITY );
            g3d_light_impl_set_intensity( entity_id, intensity );

            float range = (float) LOCDOUBLE( libmod_3d, i, LOC3D_RANGE );
            g3d_light_impl_set_range( entity_id, range );

            float cone = (float) LOCDOUBLE( libmod_3d, i, LOC3D_CONE_ANGLE );
            g3d_light_impl_set_cone( entity_id, cone );

            float r = LOCBYTE( libmod_3d, i, LOC3D_COLORR ) / 255.0f;
            float g = LOCBYTE( libmod_3d, i, LOC3D_COLORG ) / 255.0f;
            float b = LOCBYTE( libmod_3d, i, LOC3D_COLORB ) / 255.0f;
            g3d_light_impl_set_color( entity_id, r, g, b );
            break;
        }
        case 3: { /* C3D_CAMERA */
            G3DCamera *cam = g3d_bgd_camera_get( entity_id );
            if ( cam ) {
                g3d_camera_set_position_impl( cam, vec3_make( px, py, pz ) );
                g3d_camera_look_at_impl( cam, vec3_make( tx, ty, tz ), vec3_make( 0.0f, 1.0f, 0.0f ) );

                float fov = (float) LOCDOUBLE( libmod_3d, i, LOC3D_FOV );
                g3d_camera_set_perspective( cam, fov, 16.0f / 9.0f, 0.1f, 1000.0f );
            }
            break;
        }
        default:
            break;
    }

}

void __bgdexport(libmod_3d, module_initialize)() {
    g3d_engine_obj_id = gr_new_object(INT64_MAX, g3d_object_info, g3d_object_draw, NULL);
    /* Register per-process hook for all C_3D instances */
    gr_register_instance_ctype_hook( C_3D_CTYPE, g3d_process_instance_hook );
}

void __bgdexport(libmod_3d, module_finalize)() {
    if (g3d_engine_obj_id) {
        gr_destroy_object(g3d_engine_obj_id);
        g3d_engine_obj_id = 0;
    }
    g3d_engine_shutdown();
}

#include "libmod_3d_exports.h"
