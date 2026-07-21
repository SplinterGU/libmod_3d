/*
 * libmod_3d_texture.h - Texture Loading and Management
 *
 * Loads PNG, JPG, and other formats via SDL2_image
 * Manages GPU texture objects and texture caching
 */

#ifndef __LIBMOD_3D_TEXTURE_H
#define __LIBMOD_3D_TEXTURE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   TEXTURE STRUCTURES
   ============================================================================
 */

typedef enum {
    G3D_TEXTURE_FORMAT_RGB,      /* 3 channels */
    G3D_TEXTURE_FORMAT_RGBA,     /* 4 channels */
} G3DTextureFormat;

typedef struct {
    uint32_t id;                 /* Unique texture ID */
    char name[64];               /* Texture name (for debugging) */
    char filepath[256];          /* Source file path */

    /* Dimensions */
    uint32_t width;
    uint32_t height;
    uint32_t channels;           /* 3 or 4 */

    /* GPU Object */
    uint32_t gl_handle;          /* OpenGL texture handle */
    int gpu_uploaded;            /* 1 if uploaded to GPU */

    /* Pixel data (CPU-side) */
    uint8_t *data;               /* Raw pixel data (RGBA) */
    size_t data_size;            /* Data size in bytes */
} G3DTexture;

/* ============================================================================
   TEXTURE API
   ============================================================================
 */

/* Load texture from file (PNG, JPG, etc) */
G3DTexture *g3d_texture_load_impl(const char *filepath);

/* Load a texture from an in-memory encoded image (PNG/JPG bytes), e.g. embedded
   in an FBX. NULL on failure. */
G3DTexture *g3d_texture_load_mem(const char *name, const void *data, unsigned long size);

/* Create texture from raw pixel data */
G3DTexture *g3d_texture_create_from_data(const char *name, uint32_t width,
                                          uint32_t height, uint32_t channels,
                                          uint8_t *data);

/* Upload texture to GPU */
int g3d_texture_upload_gpu(G3DTexture *texture);

/* Bind texture for rendering */
void g3d_texture_bind(G3DTexture *texture, int texture_unit);

/* Free texture (GPU and CPU memory) */
void g3d_texture_free(G3DTexture *texture);

/* Generate default white/normal textures */
G3DTexture *g3d_texture_create_default_white(void);
G3DTexture *g3d_texture_create_default_normal(void);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_TEXTURE_H */
