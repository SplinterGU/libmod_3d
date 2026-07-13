/*
 * libmod_3d_worldgen.h - Procedural terrain for streamed open worlds.
 *
 * A deterministic height function (fBm value noise) defines an endless world;
 * the streaming layer asks for one terrain-tile mesh per tile as the camera
 * moves. Same tile coords -> identical terrain, and adjacent tiles match at the
 * seams (the noise is sampled in world space). The game queries the height to
 * scatter objects (palms, rocks) onto the ground.
 */
#ifndef __LIBMOD_3D_WORLDGEN_H
#define __LIBMOD_3D_WORLDGEN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Tune the world: seed, vertical amplitude, horizontal frequency (smaller =
   larger features), and sea level (heights below it are underwater). */
void  g3d_worldgen_set(int seed, float amplitude, float frequency, float sea_level);

/* How deep the ocean floor sinks below sea level (1 = default, >1 = deeper seas,
   so the water looks deeper). Only affects terrain that is already underwater. */
void  g3d_worldgen_set_water_depth(float depth);

/* Real textures per biome band, blended by height/slope in the shader. Pass
   G3DTexture* handles for sand, grass, rock, snow; scale is the world-space UV
   tiling (e.g. 0.03). Pass 0 for all to go back to flat procedural colours. */
void  g3d_worldgen_set_biome_textures(void *sand, void *grass, void *rock, void *snow, float scale);

/* World height at (wx,wz). Sea is where this is < sea_level. */
float g3d_worldgen_height(float wx, float wz);

/* Build one terrain tile (res x res grid) for world tile (tx,tz) of size tsize,
   placed at render origin (rox,0,roz) in scene `scene_id`, textured with `tex`
   (a G3DTexture* handle, or 0). Returns the entity id (or -1). */
int   g3d_worldgen_tile(int scene_id, int tx, int tz, float tsize, int res,
                        float rox, float roz, void *tex);

/* Destroy a terrain tile made above and free its unique mesh. */
void  g3d_worldgen_tile_free(int entity_id);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_WORLDGEN_H */
