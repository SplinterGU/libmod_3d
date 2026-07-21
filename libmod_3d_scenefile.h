/*
 * libmod_3d_scenefile.h - Declarative scene/map file (load).
 *
 * A .scene file is a human-readable text map: terrain, sky, fog, lights,
 * scattered vegetation, placed models and prefab instances. The external editor
 * writes this; the runtime loads it with one call and the world appears. Shared
 * format = the bridge between editor and game.
 */

#ifndef __LIBMOD_3D_SCENEFILE_H
#define __LIBMOD_3D_SCENEFILE_H

#include "libmod_3d_mesh.h"   /* G3DMesh (terrain collider registration) */

#ifdef __cplusplus
extern "C" {
#endif

/* Build a whole scene from a .scene file. Creates + activates a scene and
   populates it (terrain, lights, sky, fog, scatter, models, prefabs).
   Returns the new scene id, or -1. */
int g3d_scene_load(const char *file);

/* Query the last-loaded scene's terrain/water (so a game can walk on the loaded
   heightmap and splash on its water without rebuilding the world):
   - terrain_height: bilinear ground height at world (x,z).
   - water_level: surface Y of the first lake / global water (very negative if
     the scene has no water). */
float g3d_scene_terrain_height(float x, float z);
float g3d_scene_water_level(void);

/* Raw heightmap of the loaded scene (for a physics engine to build a collision
   height field). H is row-major H[iz*side+ix], covering world [-ws/2,ws/2] on
   X/Z. Returns 0 if the scene has no heightmap. */
int   g3d_scene_heightfield(const float **H, int *side, float *world_size);

/* Register a runtime-built terrain mesh as the collision heightfield (so rigid
   bodies / characters rest on a g3d_primitive_terrain + g3d_terrain_load relief,
   not just on scenes loaded from disk). Returns 1 on success. */
int   g3d_scene_set_terrain_collider(G3DMesh *mesh);

/* Player spawn point placed in the editor (SPAWN directive). */
int   g3d_scene_has_spawn(void);
float g3d_scene_spawn_x(void);
float g3d_scene_spawn_y(void);
float g3d_scene_spawn_z(void);
float g3d_scene_player_radius(void);
float g3d_scene_player_height(void);
float g3d_scene_player_climb(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SCENEFILE_H */
