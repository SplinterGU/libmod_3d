/*
 * libmod_3d_stream.h - Tile streaming manager for large / open worlds.
 *
 * The engine only owns the SPATIAL logic: it divides the world into a grid of
 * square tiles and, as the camera moves, tracks which tiles are inside a load
 * radius. Each g3d_stream_update() reports the tiles that just ENTERED the ring
 * (to populate) and the ones that just LEFT it (to free). The game decides what
 * lives in each tile (objects, props, spawns) and does the actual spawn/despawn
 * -> no C->script callbacks needed, fits BennuGD cleanly.
 *
 * This first version is SYNCHRONOUS (the game populates on the main thread when
 * a tile enters). Async loading (a job system inside libmod_3d, Jolt-style) is
 * a later layer that does not change this API.
 */
#ifndef __LIBMOD_3D_STREAM_H
#define __LIBMOD_3D_STREAM_H

#ifdef __cplusplus
extern "C" {
#endif

/* tile_size = world units per tile; radius = tiles loaded around the camera
   (Chebyshev, so a (2*radius+1)^2 square). */
void g3d_stream_init(float tile_size, int radius);

/* Recompute the loaded ring for the camera at (camx,camz). Fills the load /
   unload query lists with the tiles that changed this call. Cheap: the lists
   only change when the camera crosses a tile boundary. */
void g3d_stream_update(float camx, float camz);

/* Tiles that ENTERED the ring this update (the game should populate them). */
int g3d_stream_load_count(void);
int g3d_stream_load_x(int i);
int g3d_stream_load_z(int i);

/* Tiles that LEFT the ring this update (the game should free them). */
int g3d_stream_unload_count(void);
int g3d_stream_unload_x(int i);
int g3d_stream_unload_z(int i);

int g3d_stream_loaded_count(void);   /* tiles currently in the ring */
void g3d_stream_shutdown(void);      /* clears state (does not touch game objects) */

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_STREAM_H */
