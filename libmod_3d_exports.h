/*
 * libmod_3d_exports.h - Function Exports for BennuGD2
 *
 * Defines the public API available to BennuGD scripts
 * Pattern follows libmod_ray_exports.h
 */

#ifndef __LIBMOD_3D_EXPORTS
#define __LIBMOD_3D_EXPORTS

#include "bgddl.h"

#if defined(__BGDC__) || !defined(__STATIC__)

#include "libmod_3d.h"

/* Constants exported */
DLCONSTANT __bgdexport(libmod_3d, constants_def)[] = {
    /* 3D */
    { "C_3D",                   TYPE_QWORD, C_3D},

    /* Light types */
    {"G3D_LIGHT_DIRECTIONAL",   TYPE_INT,   0},
    {"G3D_LIGHT_POINT",         TYPE_INT,   1},
    {"G3D_LIGHT_SPOT",          TYPE_INT,   2},

    /* CSUBTYPE values for C_3D processes */
    {"C3D_ENTITY",              TYPE_QWORD, 1},
    {"C3D_LIGHT",               TYPE_QWORD, 2},
    {"C3D_CAMERA",              TYPE_QWORD, 3},

    {NULL, 0, 0}
};

/* --------------------------------------------------------------------------- */
/* Local variables added to every process when libmod_3d is loaded             */

char * __bgdexport(libmod_3d, locals_def) =
    "DOUBLE angle_x=0.0;\n"
    "DOUBLE angle_y=0.0;\n"
    "DOUBLE angle_z=0.0;\n"
//    "DOUBLE size_x=100.0;\n"
//    "DOUBLE size_y=100.0;\n"
    "DOUBLE size_z=100.0;\n"
    "INT entity=0;\n"
    "DOUBLE target_x=0.0;\n"
    "DOUBLE target_y=0.0;\n"
    "DOUBLE target_z=0.0;\n"
    "DOUBLE fov=60.0;\n"
    "DOUBLE intensity=1.0;\n"
    "DOUBLE range=100.0;\n"
    "DOUBLE cone_angle=45.0;\n"
    ;

#endif

/* Forward declarations of BGD wrapper functions */
int64_t g3d_init_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_shutdown_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_render_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_destroy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_set_active_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_spawn_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_destroy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_despawn_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_position_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_rotation_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_scale_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_get_position_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_parent_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_material_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_alpha_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_blend_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_set_active_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_set_position_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_look_at_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_set_projection_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_camera_set_fov_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_load_gltf_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_gltf_set_recenter_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_load_gltf_fractured_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_load_obj_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_load_fbx_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_mesh_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_height_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_size_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_orient_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_anim_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_anim_duration_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_animate_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_animate_blend_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_rest_pose_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_lock_root_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_collider_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collide_move_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collide_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collide_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collide_floor_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_raycast_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_ray_hit_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_ray_hit_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_ray_hit_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_ray_entity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_cx_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_cy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_cz_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_hx_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_hy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_hz_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_set_model_offset_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_spawn_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_load_md3_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_texture_load_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_position_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_direction_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_intensity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_sun_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_range_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_cone_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_enable_shadow_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_light_set_shadow_quality_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_set_metallic_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_set_roughness_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_physics_body_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_physics_body_set_velocity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_physics_step_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_clear_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_ambient_light_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_fog_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_sky_set_gradient_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_sky_set_clouds_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_sky_set_low_clouds_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_sky_set_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_sky_set_enabled_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mouse_capture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mouse_update_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mouse_dx_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mouse_dy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_wireframe_mode_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_shadows_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_hdr_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_exposure_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_bloom_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_tonemap_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_ssao_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_ssr_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_underwater_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_ocean_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_tessellation_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_fire_add_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_fire_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_waves_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_enabled_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_reflection_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_set_reflection_flip_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_ripple_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_water_splash_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mirror_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mirror_set_flip_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mirror_set_tint_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mirror_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_mirror_set_distance_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_add_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_set_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_create_skinned_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_set_gpu_skin_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_lod_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_set_wind_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_set_alpha_cut_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_set_lod_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_world_rebase_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_init_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_update_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_load_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_load_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_load_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_unload_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_unload_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_unload_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_stream_loaded_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_worldgen_set_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_worldgen_height_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_worldgen_tile_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_worldgen_tile_free_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_set_distance_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_instances_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scatter_model_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scatter_mesh_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_add_box_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_piece_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_save_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_load_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_instantiate_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_prefab_count_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_load_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_terrain_height_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_water_level_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_floor_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_solid_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_blocked_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_worldsize_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_walk_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_walkx_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_walky_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_voxterrain_walkz_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_has_spawn_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_spawn_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_spawn_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_spawn_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_player_radius_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_player_height_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_scene_player_climb_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_pick_terrain_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_pick_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_pick_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_pick_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_paint_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_paint_fill_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_terrain_paint_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_paint_get_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_paint_save_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_pin_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_set_wind_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_set_collider_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_clear_collider_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_set_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_cloth_update_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_add_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_add_river_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_set_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_flow_set_clip_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_particles_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_particles_set_color_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_particles_set_floor_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_particles_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_cube_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_sphere_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_plane_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_terrain_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_terrain_get_height_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_terrain_raise_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_terrain_smooth_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_terrain_flatten_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_cliffs_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_primitive_mountain_bgd(INSTANCE *my, int64_t *params);
/* physics: collision + character controller */
int64_t g3d_char_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_destroy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_move_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_jump_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_update_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_set_position_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_set_tuning_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_char_grounded_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_physics_set_gravity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collider_add_box_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collider_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_destroy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_update_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_set_geometry_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_set_tuning_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_yaw_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_pitch_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_roll_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_vehicle_speed_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_create_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_create_sphere_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_create_capsule_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_create_cylinder_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_create_convex_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_set_ccd_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_collider_add_mesh_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_destroy_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_clear_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_step_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_apply_impulse_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_set_velocity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_set_bounce_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_grounded_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_set_angular_velocity_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_angle_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_angle_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_angle_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_render_x_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_render_y_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_rigidbody_render_z_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_entity_set_mesh_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_set_texture_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_material_set_map_bgd(INSTANCE *my, int64_t *params);
int64_t g3d_model_submesh_map_bgd(INSTANCE *my, int64_t *params);

DLSYSFUNCS __bgdexport(libmod_3d, functions_exports)[] = {
    // FUNC("G3D_INIT", "II", TYPE_INT, g3d_init_bgd),
    // FUNC("G3D_SHUTDOWN", "", TYPE_INT, g3d_shutdown_bgd),
    // FUNC("G3D_RENDER", "I", TYPE_INT, g3d_render_bgd),
    FUNC("G3D_SCENE_CREATE", "S", TYPE_INT, g3d_scene_create_bgd),
    FUNC("G3D_SCENE_DESTROY", "I", TYPE_INT, g3d_scene_destroy_bgd),
    FUNC("G3D_SCENE_SET_ACTIVE", "I", TYPE_INT, g3d_scene_set_active_bgd),
    FUNC("G3D_ENTITY_SPAWN", "IIFFF", TYPE_INT, g3d_entity_spawn_bgd),
    FUNC("G3D_ENTITY_DESTROY", "I", TYPE_INT, g3d_entity_destroy_bgd),
    FUNC("G3D_MODEL_DESPAWN", "I", TYPE_INT, g3d_model_despawn_bgd),
    FUNC("G3D_ENTITY_SET_POSITION", "IFFF", TYPE_INT, g3d_entity_set_position_bgd),
    FUNC("G3D_ENTITY_SET_ROTATION", "IFFF", TYPE_INT, g3d_entity_set_rotation_bgd),
    FUNC("G3D_ENTITY_SET_SCALE", "IFFF", TYPE_INT, g3d_entity_set_scale_bgd),
    FUNC("G3D_ENTITY_GET_POSITION", "IPPP", TYPE_INT, g3d_entity_get_position_bgd),
    FUNC("G3D_ENTITY_SET_PARENT", "II", TYPE_INT, g3d_entity_set_parent_bgd),
    FUNC("G3D_CAMERA_CREATE", "", TYPE_INT, g3d_camera_create_bgd),
    FUNC("G3D_CAMERA_SET_ACTIVE", "I", TYPE_INT, g3d_camera_set_active_bgd),
    FUNC("G3D_CAMERA_SET_POSITION", "IFFF", TYPE_INT, g3d_camera_set_position_bgd),
    FUNC("G3D_CAMERA_LOOK_AT", "IFFFFFF", TYPE_INT, g3d_camera_look_at_bgd),
    FUNC("G3D_CAMERA_SET_PROJECTION", "II", TYPE_INT, g3d_camera_set_projection_bgd),
    FUNC("G3D_CAMERA_SET_FOV", "IF", TYPE_INT, g3d_camera_set_fov_bgd),
    FUNC("G3D_LOAD_GLTF", "S", TYPE_INT, g3d_model_load_gltf_bgd),
    FUNC("G3D_GLTF_SET_RECENTER", "I", TYPE_INT, g3d_gltf_set_recenter_bgd),
    FUNC("G3D_LOAD_GLTF_FRACTURED", "S", TYPE_INT, g3d_model_load_gltf_fractured_bgd),
    FUNC("G3D_LOAD_OBJ", "S", TYPE_INT, g3d_model_load_obj_bgd),
    FUNC("G3D_LOAD_FBX", "S", TYPE_INT, g3d_model_load_fbx_bgd),
    FUNC("G3D_MODEL_MESH", "I", TYPE_INT, g3d_model_mesh_bgd),
    FUNC("G3D_MODEL_TEXTURE", "I", TYPE_INT, g3d_model_texture_bgd),
    FUNC("G3D_MODEL_SUBMESH_COUNT", "I", TYPE_INT, g3d_model_submesh_count_bgd),
    FUNC("G3D_MODEL_HEIGHT", "I", TYPE_FLOAT, g3d_model_height_bgd),
    FUNC("G3D_MODEL_SIZE", "I", TYPE_FLOAT, g3d_model_size_bgd),
    FUNC("G3D_MODEL_ORIENT", "IFFF", TYPE_INT, g3d_model_orient_bgd),
    FUNC("G3D_MODEL_ANIM_COUNT", "I", TYPE_INT, g3d_model_anim_count_bgd),
    FUNC("G3D_MODEL_ANIM_DURATION", "II", TYPE_FLOAT, g3d_model_anim_duration_bgd),
    FUNC("G3D_MODEL_ANIMATE", "IIFI", TYPE_INT, g3d_model_animate_bgd),
    FUNC("G3D_MODEL_ANIMATE_BLEND", "IIFIFFI", TYPE_INT, g3d_model_animate_blend_bgd),
    FUNC("G3D_MODEL_REST_POSE", "I", TYPE_INT, g3d_model_rest_pose_bgd),
    FUNC("G3D_MODEL_LOCK_ROOT", "II", TYPE_INT, g3d_model_lock_root_bgd),
    FUNC("G3D_ENTITY_SET_COLLIDER", "II", TYPE_INT, g3d_entity_set_collider_bgd),
    FUNC("G3D_COLLIDE_MOVE", "FFFFFFF", TYPE_INT, g3d_collide_move_bgd),
    FUNC("G3D_COLLIDE_X", "", TYPE_FLOAT, g3d_collide_x_bgd),
    FUNC("G3D_COLLIDE_Z", "", TYPE_FLOAT, g3d_collide_z_bgd),
    FUNC("G3D_COLLIDE_FLOOR", "FFFF", TYPE_FLOAT, g3d_collide_floor_bgd),
    FUNC("G3D_RAYCAST", "FFFFFFF", TYPE_FLOAT, g3d_raycast_bgd),
    FUNC("G3D_RAY_HIT_X", "", TYPE_FLOAT, g3d_ray_hit_x_bgd),
    FUNC("G3D_RAY_HIT_Y", "", TYPE_FLOAT, g3d_ray_hit_y_bgd),
    FUNC("G3D_RAY_HIT_Z", "", TYPE_FLOAT, g3d_ray_hit_z_bgd),
    FUNC("G3D_RAY_ENTITY", "", TYPE_INT, g3d_ray_entity_bgd),
    FUNC("G3D_MODEL_SUBMESH", "II", TYPE_INT, g3d_model_submesh_bgd),
    FUNC("G3D_MODEL_SUBMESH_TEXTURE", "II", TYPE_INT, g3d_model_submesh_texture_bgd),
    FUNC("G3D_MODEL_SUBMESH_CX", "II", TYPE_FLOAT, g3d_model_submesh_cx_bgd),
    FUNC("G3D_MODEL_SUBMESH_CY", "II", TYPE_FLOAT, g3d_model_submesh_cy_bgd),
    FUNC("G3D_MODEL_SUBMESH_CZ", "II", TYPE_FLOAT, g3d_model_submesh_cz_bgd),
    FUNC("G3D_MODEL_SUBMESH_HX", "II", TYPE_FLOAT, g3d_model_submesh_hx_bgd),
    FUNC("G3D_MODEL_SUBMESH_HY", "II", TYPE_FLOAT, g3d_model_submesh_hy_bgd),
    FUNC("G3D_MODEL_SUBMESH_HZ", "II", TYPE_FLOAT, g3d_model_submesh_hz_bgd),
    FUNC("G3D_MODEL_SPAWN", "IIFFFFF", TYPE_INT, g3d_model_spawn_bgd),
    FUNC("G3D_LOAD_MD3", "S", TYPE_INT, g3d_model_load_md3_bgd),
    FUNC("G3D_LOAD_TEXTURE", "S", TYPE_INT, g3d_texture_load_bgd),
    FUNC("G3D_LIGHT_CREATE", "IFFF", TYPE_INT, g3d_light_create_bgd),
    FUNC("G3D_LIGHT_SET_POSITION", "IFFF", TYPE_INT, g3d_light_set_position_bgd),
    FUNC("G3D_LIGHT_SET_DIRECTION", "IFFF", TYPE_INT, g3d_light_set_direction_bgd),
    FUNC("G3D_LIGHT_SET_INTENSITY", "IF", TYPE_INT, g3d_light_set_intensity_bgd),
    FUNC("G3D_LIGHT_SET_COLOR", "IFFF", TYPE_INT, g3d_light_set_color_bgd),
    FUNC("G3D_SET_SUN", "F", TYPE_INT, g3d_set_sun_bgd),
    FUNC("G3D_LIGHT_SET_RANGE", "IF", TYPE_INT, g3d_light_set_range_bgd),
    FUNC("G3D_LIGHT_SET_CONE", "IF", TYPE_INT, g3d_light_set_cone_bgd),
    FUNC("G3D_LIGHT_ENABLE_SHADOW", "II", TYPE_INT, g3d_light_enable_shadow_bgd),
    FUNC("G3D_LIGHT_SET_SHADOW_QUALITY", "II", TYPE_INT, g3d_light_set_shadow_quality_bgd),
    FUNC("G3D_MATERIAL_CREATE", "", TYPE_INT, g3d_material_create_bgd),
    FUNC("G3D_MATERIAL_SET_COLOR", "IFFFF", TYPE_INT, g3d_material_set_color_bgd),
    FUNC("G3D_MATERIAL_SET_METALLIC", "IF", TYPE_INT, g3d_material_set_metallic_bgd),
    FUNC("G3D_MATERIAL_SET_ROUGHNESS", "IF", TYPE_INT, g3d_material_set_roughness_bgd),
    FUNC("G3D_PHYSICS_BODY_CREATE", "IIFF", TYPE_INT, g3d_physics_body_create_bgd),
    FUNC("G3D_PHYSICS_BODY_SET_VELOCITY", "IFFF", TYPE_INT, g3d_physics_body_set_velocity_bgd),
    FUNC("G3D_PHYSICS_STEP", "F", TYPE_INT, g3d_physics_step_bgd),
    FUNC("G3D_SET_CLEAR_COLOR", "FFFF", TYPE_INT, g3d_set_clear_color_bgd),
    FUNC("G3D_SET_AMBIENT_LIGHT", "FFFF", TYPE_INT, g3d_set_ambient_light_bgd),
    FUNC("G3D_SET_FOG", "IFFFFF", TYPE_INT, g3d_set_fog_bgd),
    FUNC("G3D_SKY_SET_GRADIENT", "FFFFFF", TYPE_INT, g3d_sky_set_gradient_bgd),
    FUNC("G3D_SKY_SET_CLOUDS", "FF", TYPE_INT, g3d_sky_set_clouds_bgd),
    FUNC("G3D_SKY_SET_LOW_CLOUDS", "FFFF", TYPE_INT, g3d_sky_set_low_clouds_bgd),
    FUNC("G3D_CHAR_CREATE", "FFFFF", TYPE_INT, g3d_char_create_bgd),
    FUNC("G3D_CHAR_DESTROY", "I", TYPE_INT, g3d_char_destroy_bgd),
    FUNC("G3D_CHAR_MOVE", "IFF", TYPE_INT, g3d_char_move_bgd),
    FUNC("G3D_CHAR_JUMP", "IF", TYPE_INT, g3d_char_jump_bgd),
    FUNC("G3D_CHAR_UPDATE", "IF", TYPE_INT, g3d_char_update_bgd),
    FUNC("G3D_CHAR_SET_POSITION", "IFFF", TYPE_INT, g3d_char_set_position_bgd),
    FUNC("G3D_CHAR_SET_TUNING", "IFF", TYPE_INT, g3d_char_set_tuning_bgd),
    FUNC("G3D_CHAR_X", "I", TYPE_FLOAT, g3d_char_x_bgd),
    FUNC("G3D_CHAR_Y", "I", TYPE_FLOAT, g3d_char_y_bgd),
    FUNC("G3D_CHAR_Z", "I", TYPE_FLOAT, g3d_char_z_bgd),
    FUNC("G3D_CHAR_GROUNDED", "I", TYPE_INT, g3d_char_grounded_bgd),
    FUNC("G3D_PHYSICS_SET_GRAVITY", "F", TYPE_INT, g3d_physics_set_gravity_bgd),
    FUNC("G3D_COLLIDER_ADD_BOX", "FFFFFF", TYPE_INT, g3d_collider_add_box_bgd),
    FUNC("G3D_COLLIDER_CLEAR", "", TYPE_INT, g3d_collider_clear_bgd),
    FUNC("G3D_VEHICLE_CREATE", "FFFF", TYPE_INT, g3d_vehicle_create_bgd),
    FUNC("G3D_VEHICLE_DESTROY", "I", TYPE_INT, g3d_vehicle_destroy_bgd),
    FUNC("G3D_VEHICLE_UPDATE", "IFFFF", TYPE_INT, g3d_vehicle_update_bgd),
    FUNC("G3D_VEHICLE_SET_GEOMETRY", "IFFF", TYPE_INT, g3d_vehicle_set_geometry_bgd),
    FUNC("G3D_VEHICLE_SET_TUNING", "IFFFFF", TYPE_INT, g3d_vehicle_set_tuning_bgd),
    FUNC("G3D_VEHICLE_X", "I", TYPE_FLOAT, g3d_vehicle_x_bgd),
    FUNC("G3D_VEHICLE_Y", "I", TYPE_FLOAT, g3d_vehicle_y_bgd),
    FUNC("G3D_VEHICLE_Z", "I", TYPE_FLOAT, g3d_vehicle_z_bgd),
    FUNC("G3D_VEHICLE_YAW", "I", TYPE_FLOAT, g3d_vehicle_yaw_bgd),
    FUNC("G3D_VEHICLE_PITCH", "I", TYPE_FLOAT, g3d_vehicle_pitch_bgd),
    FUNC("G3D_VEHICLE_ROLL", "I", TYPE_FLOAT, g3d_vehicle_roll_bgd),
    FUNC("G3D_VEHICLE_SPEED", "I", TYPE_FLOAT, g3d_vehicle_speed_bgd),
    FUNC("G3D_RIGIDBODY_CREATE", "FFFFFFF", TYPE_INT, g3d_rigidbody_create_bgd),
    FUNC("G3D_RIGIDBODY_CREATE_SPHERE", "FFFFF", TYPE_INT, g3d_rigidbody_create_sphere_bgd),
    FUNC("G3D_RIGIDBODY_CREATE_CAPSULE", "FFFFFF", TYPE_INT, g3d_rigidbody_create_capsule_bgd),
    FUNC("G3D_RIGIDBODY_CREATE_CYLINDER", "FFFFFF", TYPE_INT, g3d_rigidbody_create_cylinder_bgd),
    FUNC("G3D_RIGIDBODY_CREATE_CONVEX", "FFFIIFF", TYPE_INT, g3d_rigidbody_create_convex_bgd),
    FUNC("G3D_RIGIDBODY_SET_CCD", "II", TYPE_INT, g3d_rigidbody_set_ccd_bgd),
    FUNC("G3D_COLLIDER_ADD_MESH", "IIFFFF", TYPE_INT, g3d_collider_add_mesh_bgd),
    FUNC("G3D_RIGIDBODY_DESTROY", "I", TYPE_INT, g3d_rigidbody_destroy_bgd),
    FUNC("G3D_RIGIDBODY_CLEAR", "", TYPE_INT, g3d_rigidbody_clear_bgd),
    FUNC("G3D_RIGIDBODY_STEP", "F", TYPE_INT, g3d_rigidbody_step_bgd),
    FUNC("G3D_RIGIDBODY_APPLY_IMPULSE", "IFFF", TYPE_INT, g3d_rigidbody_apply_impulse_bgd),
    FUNC("G3D_RIGIDBODY_SET_VELOCITY", "IFFF", TYPE_INT, g3d_rigidbody_set_velocity_bgd),
    FUNC("G3D_RIGIDBODY_SET_BOUNCE", "IFF", TYPE_INT, g3d_rigidbody_set_bounce_bgd),
    FUNC("G3D_RIGIDBODY_X", "I", TYPE_FLOAT, g3d_rigidbody_x_bgd),
    FUNC("G3D_RIGIDBODY_Y", "I", TYPE_FLOAT, g3d_rigidbody_y_bgd),
    FUNC("G3D_RIGIDBODY_Z", "I", TYPE_FLOAT, g3d_rigidbody_z_bgd),
    FUNC("G3D_RIGIDBODY_GROUNDED", "I", TYPE_INT, g3d_rigidbody_grounded_bgd),
    FUNC("G3D_RIGIDBODY_SET_ANGULAR_VELOCITY", "IFFF", TYPE_INT, g3d_rigidbody_set_angular_velocity_bgd),
    FUNC("G3D_RIGIDBODY_ANGLE_X", "I", TYPE_FLOAT, g3d_rigidbody_angle_x_bgd),
    FUNC("G3D_RIGIDBODY_ANGLE_Y", "I", TYPE_FLOAT, g3d_rigidbody_angle_y_bgd),
    FUNC("G3D_RIGIDBODY_ANGLE_Z", "I", TYPE_FLOAT, g3d_rigidbody_angle_z_bgd),
    FUNC("G3D_RIGIDBODY_RENDER_X", "I", TYPE_FLOAT, g3d_rigidbody_render_x_bgd),
    FUNC("G3D_RIGIDBODY_RENDER_Y", "I", TYPE_FLOAT, g3d_rigidbody_render_y_bgd),
    FUNC("G3D_RIGIDBODY_RENDER_Z", "I", TYPE_FLOAT, g3d_rigidbody_render_z_bgd),
    FUNC("G3D_RIGIDBODY_SET_MODEL_OFFSET", "IFFF", TYPE_INT, g3d_rigidbody_set_model_offset_bgd),
    FUNC("G3D_SKY_SET_TEXTURE", "I", TYPE_INT, g3d_sky_set_texture_bgd),
    FUNC("G3D_SKY_ENABLE", "I", TYPE_INT, g3d_sky_set_enabled_bgd),
    FUNC("G3D_MOUSE_CAPTURE", "I", TYPE_INT, g3d_mouse_capture_bgd),
    FUNC("G3D_MOUSE_UPDATE", "", TYPE_INT, g3d_mouse_update_bgd),
    FUNC("G3D_MOUSE_DX", "", TYPE_INT, g3d_mouse_dx_bgd),
    FUNC("G3D_MOUSE_DY", "", TYPE_INT, g3d_mouse_dy_bgd),
    FUNC("G3D_SET_WIREFRAME_MODE", "I", TYPE_INT, g3d_set_wireframe_mode_bgd),
    FUNC("G3D_SET_SHADOWS", "I", TYPE_INT, g3d_set_shadows_bgd),
    FUNC("G3D_SET_HDR", "I", TYPE_INT, g3d_set_hdr_bgd),
    FUNC("G3D_SET_EXPOSURE", "F", TYPE_INT, g3d_set_exposure_bgd),
    FUNC("G3D_SET_BLOOM", "IFF", TYPE_INT, g3d_set_bloom_bgd),
    FUNC("G3D_SET_TONEMAP", "I", TYPE_INT, g3d_set_tonemap_bgd),
    FUNC("G3D_SET_SSAO", "IFF", TYPE_INT, g3d_set_ssao_bgd),
    FUNC("G3D_WATER_SET_SSR", "IF", TYPE_INT, g3d_water_set_ssr_bgd),
    FUNC("G3D_SET_UNDERWATER", "IFFFF", TYPE_INT, g3d_set_underwater_bgd),
    FUNC("G3D_WATER_SET_OCEAN", "FFF", TYPE_INT, g3d_water_set_ocean_bgd),
    FUNC("G3D_WATER_SET_TESSELLATION", "I", TYPE_INT, g3d_water_set_tessellation_bgd),
    FUNC("G3D_FIRE_ADD", "FFFF", TYPE_INT, g3d_fire_add_bgd),
    FUNC("G3D_FIRE_CLEAR", "", TYPE_INT, g3d_fire_clear_bgd),
    FUNC("G3D_WATER_CREATE", "FFI", TYPE_INT, g3d_water_create_bgd),
    FUNC("G3D_WATER_SET_WAVES", "FFF", TYPE_INT, g3d_water_set_waves_bgd),
    FUNC("G3D_WATER_SET_COLOR", "FFFFFF", TYPE_INT, g3d_water_set_color_bgd),
    FUNC("G3D_WATER_SET_ENABLED", "I", TYPE_INT, g3d_water_set_enabled_bgd),
    FUNC("G3D_WATER_SET_TEXTURE", "I", TYPE_INT, g3d_water_set_texture_bgd),
    FUNC("G3D_WATER_SET_REFLECTION", "IF", TYPE_INT, g3d_water_set_reflection_bgd),
    FUNC("G3D_WATER_SET_REFLECTION_FLIP", "I", TYPE_INT, g3d_water_set_reflection_flip_bgd),
    FUNC("G3D_WATER_RIPPLE", "FFF", TYPE_INT, g3d_water_ripple_bgd),
    FUNC("G3D_WATER_SPLASH", "FFFF", TYPE_INT, g3d_water_splash_bgd),
    FUNC("G3D_MIRROR_CREATE", "FFFFFFFF", TYPE_INT, g3d_mirror_create_bgd),
    FUNC("G3D_MIRROR_SET_FLIP", "II", TYPE_INT, g3d_mirror_set_flip_bgd),
    FUNC("G3D_MIRROR_SET_TINT", "IFFF", TYPE_INT, g3d_mirror_set_tint_bgd),
    FUNC("G3D_MIRROR_CLEAR", "", TYPE_INT, g3d_mirror_clear_bgd),
    FUNC("G3D_MIRROR_SET_DISTANCE", "F", TYPE_INT, g3d_mirror_set_distance_bgd),
    FUNC("G3D_INSTANCES_CREATE", "II", TYPE_INT, g3d_instances_create_bgd),
    FUNC("G3D_INSTANCES_ADD", "IFFFFF", TYPE_INT, g3d_instances_add_bgd),
    FUNC("G3D_INSTANCES_SET", "IIFFFFF", TYPE_INT, g3d_instances_set_bgd),
    FUNC("G3D_INSTANCES_CREATE_SKINNED", "III", TYPE_INT, g3d_instances_create_skinned_bgd),
    FUNC("G3D_MODEL_SET_GPU_SKIN", "II", TYPE_INT, g3d_model_set_gpu_skin_bgd),
    FUNC("G3D_MODEL_SUBMESH_LOD", "III", TYPE_INT, g3d_model_submesh_lod_bgd),
    FUNC("G3D_INSTANCES_SET_WIND", "IF", TYPE_INT, g3d_instances_set_wind_bgd),
    FUNC("G3D_INSTANCES_SET_ALPHA_CUT", "II", TYPE_INT, g3d_instances_set_alpha_cut_bgd),
    FUNC("G3D_SET_LOD", "F", TYPE_INT, g3d_set_lod_bgd),
    FUNC("G3D_SET_CULLING", "I", TYPE_INT, g3d_set_culling_bgd),
    FUNC("G3D_SET_BACKFACE_CULL", "I", TYPE_INT, g3d_set_backface_cull_bgd),
    FUNC("G3D_WORLD_REBASE", "FFF", TYPE_INT, g3d_world_rebase_bgd),
    FUNC("G3D_STREAM_INIT", "FI", TYPE_INT, g3d_stream_init_bgd),
    FUNC("G3D_STREAM_UPDATE", "FF", TYPE_INT, g3d_stream_update_bgd),
    FUNC("G3D_STREAM_LOAD_COUNT", "", TYPE_INT, g3d_stream_load_count_bgd),
    FUNC("G3D_STREAM_LOAD_X", "I", TYPE_INT, g3d_stream_load_x_bgd),
    FUNC("G3D_STREAM_LOAD_Z", "I", TYPE_INT, g3d_stream_load_z_bgd),
    FUNC("G3D_STREAM_UNLOAD_COUNT", "", TYPE_INT, g3d_stream_unload_count_bgd),
    FUNC("G3D_STREAM_UNLOAD_X", "I", TYPE_INT, g3d_stream_unload_x_bgd),
    FUNC("G3D_STREAM_UNLOAD_Z", "I", TYPE_INT, g3d_stream_unload_z_bgd),
    FUNC("G3D_STREAM_LOADED_COUNT", "", TYPE_INT, g3d_stream_loaded_count_bgd),
    FUNC("G3D_WORLDGEN_SET", "IFFF", TYPE_INT, g3d_worldgen_set_bgd),
    FUNC("G3D_WORLDGEN_HEIGHT", "FF", TYPE_FLOAT, g3d_worldgen_height_bgd),
    FUNC("G3D_WORLDGEN_TILE", "IIIFIFFI", TYPE_INT, g3d_worldgen_tile_bgd),
    FUNC("G3D_WORLDGEN_TILE_FREE", "I", TYPE_INT, g3d_worldgen_tile_free_bgd),
    FUNC("G3D_WORLDGEN_SET_WATER_DEPTH", "F", TYPE_INT, g3d_worldgen_set_water_depth_bgd),
    FUNC("G3D_WORLDGEN_SET_BIOME_TEXTURES", "IIIIF", TYPE_INT, g3d_worldgen_set_biome_textures_bgd),
    FUNC("G3D_INSTANCES_SET_DISTANCE", "IF", TYPE_INT, g3d_instances_set_distance_bgd),
    FUNC("G3D_INSTANCES_CLEAR", "I", TYPE_INT, g3d_instances_clear_bgd),
    FUNC("G3D_INSTANCES_COUNT", "I", TYPE_INT, g3d_instances_count_bgd),
    FUNC("G3D_SCATTER_MODEL", "IIIFFFFI", TYPE_INT, g3d_scatter_model_bgd),
    FUNC("G3D_SCATTER_MESH", "IIIIFFFFI", TYPE_INT, g3d_scatter_mesh_bgd),
    FUNC("G3D_PREFAB_CREATE", "S", TYPE_INT, g3d_prefab_create_bgd),
    FUNC("G3D_PREFAB_ADD_BOX", "IFFFFFFFFFFFFI", TYPE_INT, g3d_prefab_add_box_bgd),
    FUNC("G3D_PREFAB_PIECE_TEXTURE", "IS", TYPE_INT, g3d_prefab_piece_texture_bgd),
    FUNC("G3D_PREFAB_SAVE", "IS", TYPE_INT, g3d_prefab_save_bgd),
    FUNC("G3D_PREFAB_LOAD", "S", TYPE_INT, g3d_prefab_load_bgd),
    FUNC("G3D_PREFAB_INSTANTIATE", "IIFFFF", TYPE_INT, g3d_prefab_instantiate_bgd),
    FUNC("G3D_PREFAB_COUNT", "I", TYPE_INT, g3d_prefab_count_bgd),
    FUNC("G3D_SCENE_LOAD", "S", TYPE_INT, g3d_scene_load_bgd),
    FUNC("G3D_SCENE_TERRAIN_HEIGHT", "FF", TYPE_FLOAT, g3d_scene_terrain_height_bgd),
    FUNC("G3D_SCENE_WATER_LEVEL", "", TYPE_FLOAT, g3d_scene_water_level_bgd),
    FUNC("G3D_VOXTERRAIN_FLOOR", "FFF", TYPE_FLOAT, g3d_voxterrain_floor_bgd),
    FUNC("G3D_VOXTERRAIN_SOLID", "FFF", TYPE_INT, g3d_voxterrain_solid_bgd),
    FUNC("G3D_VOXTERRAIN_BLOCKED", "FFFFF", TYPE_INT, g3d_voxterrain_blocked_bgd),
    FUNC("G3D_VOXTERRAIN_WORLDSIZE", "", TYPE_FLOAT, g3d_voxterrain_worldsize_bgd),
    FUNC("G3D_VOXTERRAIN_WALK", "FFFFFFFF", TYPE_INT, g3d_voxterrain_walk_bgd),
    FUNC("G3D_VOXTERRAIN_WALKX", "", TYPE_FLOAT, g3d_voxterrain_walkx_bgd),
    FUNC("G3D_VOXTERRAIN_WALKY", "", TYPE_FLOAT, g3d_voxterrain_walky_bgd),
    FUNC("G3D_VOXTERRAIN_WALKZ", "", TYPE_FLOAT, g3d_voxterrain_walkz_bgd),
    FUNC("G3D_SCENE_HAS_SPAWN", "", TYPE_INT, g3d_scene_has_spawn_bgd),
    FUNC("G3D_SCENE_SPAWN_X", "", TYPE_FLOAT, g3d_scene_spawn_x_bgd),
    FUNC("G3D_SCENE_SPAWN_Y", "", TYPE_FLOAT, g3d_scene_spawn_y_bgd),
    FUNC("G3D_SCENE_SPAWN_Z", "", TYPE_FLOAT, g3d_scene_spawn_z_bgd),
    FUNC("G3D_SCENE_PLAYER_RADIUS", "", TYPE_FLOAT, g3d_scene_player_radius_bgd),
    FUNC("G3D_SCENE_PLAYER_HEIGHT", "", TYPE_FLOAT, g3d_scene_player_height_bgd),
    FUNC("G3D_SCENE_PLAYER_CLIMB", "", TYPE_FLOAT, g3d_scene_player_climb_bgd),
    FUNC("G3D_PICK_TERRAIN", "IFFFFI", TYPE_INT, g3d_pick_terrain_bgd),
    FUNC("G3D_PICK_X", "", TYPE_FLOAT, g3d_pick_x_bgd),
    FUNC("G3D_PICK_Y", "", TYPE_FLOAT, g3d_pick_y_bgd),
    FUNC("G3D_PICK_Z", "", TYPE_FLOAT, g3d_pick_z_bgd),
    FUNC("G3D_PAINT_CREATE", "II", TYPE_INT, g3d_paint_create_bgd),
    FUNC("G3D_PAINT_FILL", "IIF", TYPE_INT, g3d_paint_fill_bgd),
    FUNC("G3D_TERRAIN_PAINT", "IIIFFFFF", TYPE_INT, g3d_terrain_paint_bgd),
    FUNC("G3D_PAINT_GET_TEXTURE", "I", TYPE_INT, g3d_paint_get_texture_bgd),
    FUNC("G3D_PAINT_SAVE", "IS", TYPE_INT, g3d_paint_save_bgd),
    FUNC("G3D_CLOTH_CREATE", "FFIIFFF", TYPE_INT, g3d_cloth_create_bgd),
    FUNC("G3D_CLOTH_PIN", "II", TYPE_INT, g3d_cloth_pin_bgd),
    FUNC("G3D_CLOTH_SET_WIND", "IFFFF", TYPE_INT, g3d_cloth_set_wind_bgd),
    FUNC("G3D_CLOTH_SET_COLLIDER", "IFFFF", TYPE_INT, g3d_cloth_set_collider_bgd),
    FUNC("G3D_CLOTH_CLEAR_COLLIDER", "I", TYPE_INT, g3d_cloth_clear_collider_bgd),
    FUNC("G3D_CLOTH_SET_TEXTURE", "II", TYPE_INT, g3d_cloth_set_texture_bgd),
    FUNC("G3D_CLOTH_UPDATE", "IF", TYPE_INT, g3d_cloth_update_bgd),
    FUNC("G3D_FLOW_ADD", "FFFFFFFFF", TYPE_INT, g3d_flow_add_bgd),
    FUNC("G3D_FLOW_ADD_RIVER", "IFFFFFFFF", TYPE_INT, g3d_flow_add_river_bgd),
    FUNC("G3D_FLOW_SET_TEXTURE", "I", TYPE_INT, g3d_flow_set_texture_bgd),
    FUNC("G3D_FLOW_SET_COLOR", "FFF", TYPE_INT, g3d_flow_set_color_bgd),
    FUNC("G3D_FLOW_CLEAR", "", TYPE_INT, g3d_flow_clear_bgd),
    FUNC("G3D_FLOW_SET_CLIP", "F", TYPE_INT, g3d_flow_set_clip_bgd),
    FUNC("G3D_PARTICLES_CREATE", "FFFFFFFFFFFFFI", TYPE_INT, g3d_particles_create_bgd),
    FUNC("G3D_PARTICLES_SET_COLOR", "IFFF", TYPE_INT, g3d_particles_set_color_bgd),
    FUNC("G3D_PARTICLES_SET_FLOOR", "IF", TYPE_INT, g3d_particles_set_floor_bgd),
    FUNC("G3D_PARTICLES_CLEAR", "", TYPE_INT, g3d_particles_clear_bgd),
    FUNC("G3D_RAYCAST", "FFFFFFPPP", TYPE_INT, g3d_raycast_bgd),
    FUNC("G3D_PRIMITIVE_CUBE", "", TYPE_INT, g3d_primitive_cube_bgd),
    FUNC("G3D_PRIMITIVE_SPHERE", "I", TYPE_INT, g3d_primitive_sphere_bgd),
    FUNC("G3D_PRIMITIVE_PLANE", "", TYPE_INT, g3d_primitive_plane_bgd),
    FUNC("G3D_PRIMITIVE_TERRAIN", "IFFFI", TYPE_INT, g3d_primitive_terrain_bgd),
    FUNC("G3D_TERRAIN_GET_HEIGHT", "IFF", TYPE_FLOAT, g3d_terrain_get_height_bgd),
    FUNC("G3D_TERRAIN_RAISE", "IFFFF", TYPE_INT, g3d_terrain_raise_bgd),
    FUNC("G3D_TERRAIN_SMOOTH", "IFFFF", TYPE_INT, g3d_terrain_smooth_bgd),
    FUNC("G3D_TERRAIN_FLATTEN", "IFFFFF", TYPE_INT, g3d_terrain_flatten_bgd),
    FUNC("G3D_PRIMITIVE_CLIFFS", "IFFFIFF", TYPE_INT, g3d_primitive_cliffs_bgd),
    FUNC("G3D_PRIMITIVE_MOUNTAIN", "IFFFIF", TYPE_INT, g3d_primitive_mountain_bgd),
    FUNC("G3D_ENTITY_SET_MESH", "II", TYPE_INT, g3d_entity_set_mesh_bgd),
    FUNC("G3D_ENTITY_SET_MATERIAL", "II", TYPE_INT, g3d_entity_set_material_bgd),
    FUNC("G3D_ENTITY_SET_ALPHA", "II", TYPE_INT, g3d_entity_set_alpha_bgd),
    FUNC("G3D_ENTITY_SET_COLOR", "IIII", TYPE_INT, g3d_entity_set_color_bgd),
    FUNC("G3D_ENTITY_SET_BLEND", "II", TYPE_INT, g3d_entity_set_blend_bgd),
    FUNC("G3D_MATERIAL_SET_TEXTURE", "III", TYPE_INT, g3d_material_set_texture_bgd),
    FUNC("G3D_MATERIAL_SET_MAP", "III", TYPE_INT, g3d_material_set_map_bgd),
    FUNC("G3D_MODEL_SUBMESH_MAP", "III", TYPE_INT, g3d_model_submesh_map_bgd),
    FUNC(NULL, NULL, 0, NULL)};

/* Hooks del módulo */
void __bgdexport(libmod_3d, module_initialize)();
void __bgdexport(libmod_3d, module_finalize)();

#endif /* __LIBMOD_3D_EXPORTS */
