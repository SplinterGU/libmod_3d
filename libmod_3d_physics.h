/*
 * libmod_3d_physics.h - collision + character controller
 *
 * A lightweight kinematic physics layer for the 3D engine:
 *   - capsule character controllers that walk/jump on the terrain heightmap
 *     (or voxel surface), slide off steep slopes and step over small ledges.
 *   - axis-aligned box colliders (walls, crates, obstacles) resolved with
 *     collide-and-slide so characters can't pass through them.
 *
 * Ground height comes from g3d_scene_terrain_height(); no BennuGD2 runtime is
 * touched - this all lives inside libmod_3d.
 */
#ifndef __LIBMOD_3D_PHYSICS_H
#define __LIBMOD_3D_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- character controllers (capsule) ---- */

/* Create a capsule character. (x,y,z) = feet position, radius/height = capsule.
   Returns a handle >= 0, or -1 if full. */
int  g3d_char_create(float x, float y, float z, float radius, float height);
void g3d_char_destroy(int id);
void g3d_char_clear_all(void);

/* Desired horizontal velocity this frame (world units/sec). Call every frame
   from input; ignored components decay when airborne. */
void g3d_char_move(int id, float vx, float vz);

/* Jump: if grounded, launch upward at `speed` units/sec. */
void g3d_char_jump(int id, float speed);
void g3d_char_set_water(int id, int in_water, float surface_y);

/* Advance one step: gravity + integrate + collide against terrain and boxes. */
void g3d_char_update(int id, float dt);

/* Teleport (also zeroes velocity). */
void g3d_char_set_position(int id, float x, float y, float z);

/* State read-back (feet position). Eye height = feet + height. */
float g3d_char_x(int id);
float g3d_char_y(int id);
float g3d_char_z(int id);
int   g3d_char_grounded(int id);

/* Tunables (per character). step = max ledge to climb; slope_deg = max walkable
   angle before sliding. */
void g3d_char_set_tuning(int id, float step, float slope_deg);

/* Global gravity (units/sec^2, positive = downward). Default 24. */
void g3d_physics_set_gravity(float g);

/* ---- static box colliders (AABB) ---- */

int  g3d_collider_add_box(float minx, float miny, float minz,
                          float maxx, float maxy, float maxz);
void g3d_collider_clear(void);

/* ---- raycast vehicle (arcade) ---- */

/* Create a vehicle at (x,y,z) facing heading (radians). Returns handle or -1. */
int  g3d_vehicle_create(float x, float y, float z, float heading);
void g3d_vehicle_destroy(int id);

/* Advance one step. throttle -1..1 (reverse..forward), steer -1..1, brake 0..1. */
void g3d_vehicle_update(int id, float dt, float throttle, float steer, float brake);

/* Chassis geometry (wheelbase = front-rear, track = left-right, ride = height
   of the body above the ground). */
void g3d_vehicle_set_geometry(int id, float wheelbase, float track, float ride);

/* Handling: engine accel, top speed, grip-based steer (max steer degrees),
   braking force, drag. Any <=0 keeps the current value. */
void g3d_vehicle_set_tuning(int id, float engine, float top_speed,
                            float max_steer_deg, float brake_force, float drag);

/* State read-back. yaw/pitch/roll in radians (feed to g3d_entity_set_rotation). */
float g3d_vehicle_x(int id);
float g3d_vehicle_y(int id);
float g3d_vehicle_z(int id);
float g3d_vehicle_yaw(int id);
float g3d_vehicle_pitch(int id);
float g3d_vehicle_roll(int id);
float g3d_vehicle_speed(int id);
/* ---- rigid bodies (AABB boxes: fall, stack, collide, get pushed) ----
   Share the same static box colliders, terrain and gravity as the rest. Step
   the whole world once per frame with g3d_rigidbody_step(dt); each body is a
   handle the game reads back (BennuGD idiom: one process per body). */

int  g3d_rigidbody_create(float x, float y, float z,
                          float hx, float hy, float hz, float mass);  /* mass<=0 = static */

/* Extra collision shapes (Jolt backend). Same handle/API as the box body; only
   the shape differs. (x,y,z) is the body position; the default model offset is
   set so a bottom-origin model sits right (override with set_model_offset).
   mass<=0 makes the body static. */
int  g3d_rigidbody_create_sphere(float x, float y, float z,
                                 float radius, float mass);
int  g3d_rigidbody_create_capsule(float x, float y, float z,
                                  float radius, float half_height, float mass);  /* axis = Y */
int  g3d_rigidbody_create_cylinder(float x, float y, float z,
                                   float radius, float half_height, float mass); /* axis = Y */
/* Convex hull from a model submesh. (x,y,z) = world position of the MODEL's
   origin; the hull is built around the submesh centroid and the model offset is
   set automatically, so a fracture chunk drops in with correct rotation pivot. */
int  g3d_rigidbody_create_convex(float x, float y, float z,
                                 void *model, int submesh, float scale, float mass);

/* Continuous collision (LinearCast): stops fast bodies tunnelling through thin
   geometry. Turn on for projectiles. */
void g3d_rigidbody_set_ccd(int id, int enabled);

/* Static triangle-mesh collider from a model submesh (level geometry). Feeds the
   Jolt world as a non-moving body so dynamic bodies collide with it. Returns a
   handle >= 0 or -1. */
int  g3d_collider_add_mesh(void *model, int submesh, float x, float y, float z, float scale);

/* Read a submesh's CPU geometry (implemented in libmod_3d.c). */
int  g3d_physics_submesh_geom(void *model, int submesh, const float **pos,
                              int *stride_floats, const unsigned int **indices, int *icount);
void g3d_rigidbody_destroy(int id);
void g3d_rigidbody_clear(void);
void g3d_rigidbody_step(float dt);                    /* advance the whole world */

void g3d_rigidbody_apply_impulse(int id, float ix, float iy, float iz);
void g3d_rigidbody_set_velocity(int id, float vx, float vy, float vz);
void g3d_rigidbody_set_bounce(int id, float restitution, float friction);

float g3d_rigidbody_x(int id);
float g3d_rigidbody_y(int id);
float g3d_rigidbody_z(int id);
int   g3d_rigidbody_grounded(int id);

/* Angular dynamics: bodies tumble freely in the air and settle flat on the
   ground. Spin is in rad/s; the angle getters return Euler radians. */
void  g3d_rigidbody_set_angular_velocity(int id, float wx, float wy, float wz);
float g3d_rigidbody_angle_x(int id);
float g3d_rigidbody_angle_y(int id);
float g3d_rigidbody_angle_z(int id);
/* Model placement for bottom-origin models: centres the model on the body and
   rotates it about its centre (use these for g3d_entity_set_position). */
float g3d_rigidbody_render_x(int id);
float g3d_rigidbody_render_y(int id);
float g3d_rigidbody_render_z(int id);
/* Offset from the body centre to the model's origin (default (0,hy,0) for a
   bottom-origin model). Set per fracture chunk to its centroid in model space. */
void  g3d_rigidbody_set_model_offset(int id, float ox, float oy, float oz);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_PHYSICS_H */
