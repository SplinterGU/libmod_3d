/*
 * libmod_3d_trlevel.h - Native Tomb Raider level loader (.PHD/.TR2/.TR4/.TRC)
 *
 * Reads the original level files directly - no conversion step, no exporter.
 * The formats are the classic ones documented by the community (TRosettaStone);
 * this only parses geometry the game already ships, it contains no game data.
 *
 * Version is taken from the 4-byte magic, EXCEPT that TR4 and TR5 share the
 * same "TR4\0" magic and are told apart by file extension:
 *
 *   TR1  .PHD   0x00000020   8-bit textiles, palette at the END of the file
 *   TR2  .TR2   0x0000002D   8+16-bit textiles, palette right after the version
 *   TR3  .TR2   0xFF180038   as TR2 (also 0xFF080038)
 *   TR4  .tr4   "TR4\0"      32-bit textiles, zlib compressed
 *   TR5  .TRC   "TR4\0"      as TR4, different room layout
 *
 * Coordinates: TR has Y pointing DOWN and room vertices are local to their
 * room, so the loader negates Y and adds each room's world offset.
 */

#ifndef __LIBMOD_3D_TRLEVEL_H
#define __LIBMOD_3D_TRLEVEL_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Which game a file turned out to be (also what g3d_tr_version reports). */
#define G3D_TR_UNKNOWN 0
#define G3D_TR1        1
#define G3D_TR2        2
#define G3D_TR3        3
#define G3D_TR4        4
#define G3D_TR5        5

/* Identify a level file without loading it. Returns one of the G3D_TR* above. */
int g3d_tr_probe(const char *filepath);

/* Load a level's room geometry as a G3DModel: one submesh per room, placed at
   its world position, textured from the level's own textiles. Returns NULL if
   the file isn't a level this understands. Free with g3d_gltf_free. */
G3DModel *g3d_tr_load(const char *filepath);

/* Rooms in the level that was loaded last (0 if none). */
int g3d_tr_room_count(G3DModel *model);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_TRLEVEL_H */
