/*
 * libmod_3d_zone.h - Painted movement zones (per-layer barrier mask).
 *
 * A single global grid over the world (same convention as the terrain
 * heightfield: centered on the origin, spanning `world_size` on X/Z). Each cell
 * holds a bitmask of up to 8 "layers". The editor paints layers; the game asks
 * `g3d_zone_blocked(x,z,layer)` and confines whichever objects respect that
 * layer (e.g. a boat blocked by the shoreline layer while a character isn't).
 */
#ifndef __LIBMOD_3D_ZONE_H
#define __LIBMOD_3D_ZONE_H

#ifdef __cplusplus
extern "C" {
#endif

/* (Re)allocate the mask as side*side cells, all clear. side = grid+1 to match a
   terrain of `grid` cells. Safe to call again (keeps data if size unchanged). */
void g3d_zone_init(int side, float world_size);

/* Paint (on=1) or erase (on=0) bit `layer` (0..7) in every cell within `radius`
   world units of (wx,wz). No-op if the mask isn't initialized. */
void g3d_zone_paint(float wx, float wz, float radius, int layer, int on);

/* 1 if the cell nearest (wx,wz) has bit `layer` set. layer<0 -> any bit set. */
int  g3d_zone_blocked(float wx, float wz, int layer);

/* Raw bitmask byte at (wx,wz), or 0 if out of range / not initialized. For the
   editor overlay (color by layer). */
int  g3d_zone_value(float wx, float wz);

int  g3d_zone_side(void);        /* grid side (0 if uninitialized) */
float g3d_zone_worldsize(void);
void g3d_zone_clear(void);       /* clear all layers (keep allocation) */

/* Binary persistence: uint32 side, float world_size, side*side bytes. */
int  g3d_zone_save(const char *path);
int  g3d_zone_load(const char *path);   /* (re)allocates from the file */

#ifdef __cplusplus
}
#endif
#endif
