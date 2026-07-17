/*
 * libmod_3d_fbx.h - FBX loader via ufbx (static + skinned + animations, e.g. Mixamo).
 */
#ifndef __LIBMOD_3D_FBX_H
#define __LIBMOD_3D_FBX_H

#include "libmod_3d_mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Load an .fbx into a G3DModel (meshes + textures + skeleton + baked animations).
   NULL on failure. Works with Mixamo character+animation exports. */
G3DModel *g3d_fbx_load(const char *filepath);

/* Recentrado al cargar: centra en X/Z y apoya la base en Y=0 (ON por defecto).
   Desactivalo (0) ANTES de cargar mallas con esqueleto cuya pose BIND no sea la
   pose real (bind tumbada, etc.): en esos casos el AABB de la bind desplaza el
   modelo respecto a la entidad, y el personaje se dibuja lejos de su colision. */
void g3d_fbx_set_recenter(int enabled);

#ifdef __cplusplus
}
#endif

#endif
