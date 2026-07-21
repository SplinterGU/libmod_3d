/*
 * libmod_3d_texture.c - Texture Loading and Management Implementation
 *
 * Loads textures via SDL2_image and manages GPU texture objects
 */

#include "libmod_3d_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "SDL2/SDL_image.h"
#else
#include "libmod_ray_vita_gl.h"
#endif

/* ============================================================================
   TEXTURE LOADING
   ============================================================================
 */

#ifndef VITA
/* Convert an SDL surface to a G3DTexture (RGBA), freeing the surface. Shared by
   the file loader and the in-memory loader (embedded FBX textures). */
static G3DTexture *surface_to_g3d(SDL_Surface *surface, const char *name) {
    if (!surface) return NULL;

    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    if (!texture) {
        SDL_FreeSurface(surface);
        return NULL;
    }

    memset(texture, 0, sizeof(G3DTexture));

    texture->id = 1;  /* TODO: Use proper texture ID pool */
    strncpy(texture->name, name ? name : "tex", 63);
    texture->name[63] = '\0';
    strncpy(texture->filepath, name ? name : "tex", 255);
    texture->filepath[255] = '\0';

    texture->width = surface->w;
    texture->height = surface->h;
    texture->channels = surface->format->BytesPerPixel;

    /* Copy pixel data (convert to RGBA if needed) */
    uint32_t pixel_count = texture->width * texture->height;

    if (texture->channels == 3) {
        /* RGB -> RGBA conversion */
        texture->channels = 4;
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);

        uint8_t *src = (uint8_t *)surface->pixels;
        uint8_t *dst = texture->data;

        for (uint32_t i = 0; i < pixel_count; i++) {
            dst[0] = src[0];  /* R */
            dst[1] = src[1];  /* G */
            dst[2] = src[2];  /* B */
            dst[3] = 255;     /* A */

            src += 3;
            dst += 4;
        }
    } else if (texture->channels == 4) {
        /* RGBA direct copy */
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);
        memcpy(texture->data, surface->pixels, texture->data_size);
    } else if (texture->channels == 1) {
        /* Grayscale -> RGBA */
        texture->channels = 4;
        texture->data_size = pixel_count * 4;
        texture->data = (uint8_t *)malloc(texture->data_size);

        uint8_t *src = (uint8_t *)surface->pixels;
        uint8_t *dst = texture->data;

        for (uint32_t i = 0; i < pixel_count; i++) {
            dst[0] = dst[1] = dst[2] = src[0];  /* Gray → RGB */
            dst[3] = 255;                        /* A */

            src += 1;
            dst += 4;
        }
    } else {
        fprintf(stderr, "G3D: Unsupported texture format (channels=%d)\n",
                texture->channels);
        free(texture->data);
        free(texture);
        SDL_FreeSurface(surface);
        return NULL;
    }

    SDL_FreeSurface(surface);

    printf("G3D: Texture loaded: %dx%d, RGBA\n", texture->width,
           texture->height);

    return texture;
}
#endif

G3DTexture *g3d_texture_load_impl(const char *filepath) {
    if (!filepath) {
        fprintf(stderr, "G3D: No filepath provided for texture\n");
        return NULL;
    }
    printf("G3D: Loading texture: %s\n", filepath);
#ifndef VITA
    SDL_Surface *surface = IMG_Load(filepath);
    if (!surface) {
        fprintf(stderr, "G3D: Failed to load texture: %s\n", IMG_GetError());
        return NULL;
    }
    return surface_to_g3d(surface, filepath);
#else
    /* VITA: Stub */
    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    memset(texture, 0, sizeof(G3DTexture));
    strncpy(texture->filepath, filepath, 255);
    return texture;
#endif
}

/* Load a texture from an in-memory encoded image (PNG/JPG/etc. bytes), e.g. a
   texture embedded inside an FBX. NULL on failure. */
G3DTexture *g3d_texture_load_mem(const char *name, const void *data, unsigned long size) {
#ifndef VITA
    if (!data || size == 0) return NULL;
    SDL_RWops *rw = SDL_RWFromConstMem(data, (int)size);
    if (!rw) return NULL;
    SDL_Surface *surface = IMG_Load_RW(rw, 1);   /* 1 = free the RWops */
    if (!surface) {
        fprintf(stderr, "G3D: embedded texture decode failed: %s\n", IMG_GetError());
        return NULL;
    }
    printf("G3D: embedded texture '%s' %dx%d\n", name ? name : "?", surface->w, surface->h);
    return surface_to_g3d(surface, name);
#else
    (void)name; (void)data; (void)size; return NULL;
#endif
}

G3DTexture *g3d_texture_create_from_data(const char *name, uint32_t width,
                                          uint32_t height, uint32_t channels,
                                          uint8_t *data) {
    G3DTexture *texture = (G3DTexture *)malloc(sizeof(G3DTexture));
    if (!texture)
        return NULL;

    memset(texture, 0, sizeof(G3DTexture));

    strncpy(texture->name, name, 63);
    texture->name[63] = '\0';

    texture->width = width;
    texture->height = height;
    texture->channels = channels;
    texture->data_size = width * height * channels;

    if (data) {
        texture->data = (uint8_t *)malloc(texture->data_size);
        if (!texture->data) {
            free(texture);
            return NULL;
        }
        memcpy(texture->data, data, texture->data_size);
    }

    return texture;
}

/* ============================================================================
   GPU UPLOAD
   ============================================================================
 */

int g3d_texture_upload_gpu(G3DTexture *texture) {
    if (!texture || !texture->data)
        return 0;

    if (texture->gpu_uploaded)
        return 1;  /* Already uploaded */

#ifndef VITA
    glGenTextures(1, &texture->gl_handle);
    glBindTexture(GL_TEXTURE_2D, texture->gl_handle);

    /* Texture parameters: trilinear with mipmaps to kill minification aliasing
       (the "static/noise" look when a detailed texture is tiled into the
       distance). */
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    /* Upload to GPU */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture->width, texture->height,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, texture->data);
    glGenerateMipmap(GL_TEXTURE_2D);

    texture->gpu_uploaded = 1;

    printf("G3D: Texture uploaded to GPU (handle=%u)\n", texture->gl_handle);

    return 1;
#else
    texture->gpu_uploaded = 1;
    return 1;
#endif
}

void g3d_texture_bind(G3DTexture *texture, int texture_unit) {
    if (!texture || !texture->gpu_uploaded)
        return;

#ifndef VITA
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture->gl_handle);
#endif
}

void g3d_texture_free(G3DTexture *texture) {
    if (!texture)
        return;

#ifndef VITA
    if (texture->gl_handle) {
        glDeleteTextures(1, &texture->gl_handle);
    }
#endif

    if (texture->data) {
        free(texture->data);
    }

    free(texture);
}

/* ============================================================================
   DEFAULT TEXTURES
   ============================================================================
 */

G3DTexture *g3d_texture_create_default_white(void) {
    /* Create 4x4 white texture */
    uint32_t size = 4;
    uint32_t pixel_count = size * size;
    uint8_t *data = (uint8_t *)malloc(pixel_count * 4);

    for (uint32_t i = 0; i < pixel_count; i++) {
        data[i * 4 + 0] = 255;  /* R */
        data[i * 4 + 1] = 255;  /* G */
        data[i * 4 + 2] = 255;  /* B */
        data[i * 4 + 3] = 255;  /* A */
    }

    G3DTexture *texture = g3d_texture_create_from_data("default_white", size,
                                                        size, 4, data);
    free(data);

    if (texture) {
        g3d_texture_upload_gpu(texture);
    }

    return texture;
}

G3DTexture *g3d_texture_create_default_normal(void) {
    /* Create 4x4 normal map (blue = up) */
    uint32_t size = 4;
    uint32_t pixel_count = size * size;
    uint8_t *data = (uint8_t *)malloc(pixel_count * 4);

    for (uint32_t i = 0; i < pixel_count; i++) {
        data[i * 4 + 0] = 128;  /* R (normal X) */
        data[i * 4 + 1] = 128;  /* G (normal Y) */
        data[i * 4 + 2] = 255;  /* B (normal Z, pointing up) */
        data[i * 4 + 3] = 255;  /* A */
    }

    G3DTexture *texture = g3d_texture_create_from_data("default_normal", size,
                                                        size, 4, data);
    free(data);

    if (texture) {
        g3d_texture_upload_gpu(texture);
    }

    return texture;
}
