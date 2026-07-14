/*
 * libmod_3d_gltf.c - glTF 2.0 model loader (cgltf)
 *
 * Loads .gltf/.glb into a single merged G3DMesh plus the base-colour texture.
 * Textures are read whether embedded (.glb buffer view / data URI) or external
 * files, decoded with SDL2_image. Indices are uint16 (models are capped at 65k
 * merged vertices).
 */

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "libmod_3d_gltf.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <math.h>

#include <SDL.h>
#include <SDL_image.h>

#ifndef VITA
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* GL block-compressed formats (DDS). Defined here in case glext.h is old. */
#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_COMPRESSED_RG_RGTC2
#define GL_COMPRESSED_RG_RGTC2 0x8DBD
#endif
#ifndef GL_COMPRESSED_RGBA_BPTC_UNORM
#define GL_COMPRESSED_RGBA_BPTC_UNORM 0x8E8C   /* BC7 */
#endif
#ifndef GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM
#define GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM 0x8E8D
#endif

/* Load a DDS image (DXT1/DXT3/DXT5/ATI2 block compression) straight to a GL
   texture with glCompressedTexImage2D - the GPU reads these natively, so no CPU
   decode. Returns a G3DTexture with its gl_handle set (data stays NULL). */
static G3DTexture *load_dds_from_memory(const uint8_t *d, size_t size) {
#ifdef VITA
    (void)d; (void)size; return NULL;
#else
    if (size < 128 || memcmp(d, "DDS ", 4) != 0) return NULL;
    #define RDU32(o) ((uint32_t)d[o] | ((uint32_t)d[(o)+1]<<8) | ((uint32_t)d[(o)+2]<<16) | ((uint32_t)d[(o)+3]<<24))
    uint32_t height = RDU32(12);
    uint32_t width  = RDU32(16);
    uint32_t mips   = RDU32(28);
    #undef RDU32
    if (mips == 0) mips = 1;
    const uint8_t *fc = d + 84;   /* pixel-format FourCC */

    GLenum glfmt; int block; size_t data_off = 128;
    if      (!memcmp(fc, "DXT1", 4)) { glfmt = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;  block = 8;  }
    else if (!memcmp(fc, "DXT3", 4)) { glfmt = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT; block = 16; }
    else if (!memcmp(fc, "DXT5", 4)) { glfmt = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT; block = 16; }
    else if (!memcmp(fc, "ATI2", 4)) { glfmt = GL_COMPRESSED_RG_RGTC2;           block = 16; }
    else if (!memcmp(fc, "DX10", 4)) {
        /* DDS_HEADER_DXT10 follows (20 bytes) -> pixel data starts at 148. */
        if (size < 148) return NULL;
        uint32_t dxgi = (uint32_t)d[128] | ((uint32_t)d[129]<<8) | ((uint32_t)d[130]<<16) | ((uint32_t)d[131]<<24);
        data_off = 148;
        switch (dxgi) {
            case 98: case 99: glfmt = GL_COMPRESSED_RGBA_BPTC_UNORM;   block = 16; break; /* BC7 (/sRGB) */
            case 71: case 72: glfmt = GL_COMPRESSED_RGB_S3TC_DXT1_EXT; block = 8;  break; /* BC1 */
            case 74: case 75: glfmt = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;block = 16; break; /* BC2 */
            case 77: case 78: glfmt = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;block = 16; break; /* BC3 */
            case 83:          glfmt = GL_COMPRESSED_RG_RGTC2;          block = 16; break; /* BC5 */
            default: return NULL;
        }
    }
    else return NULL;   /* unsupported DDS variant */

    GLuint handle = 0;
    glGenTextures(1, &handle);
    glBindTexture(GL_TEXTURE_2D, handle);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    (mips > 1) ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);

    const uint8_t *p = d + data_off;
    size_t left = size - data_off;
    uint32_t w = width, h = height, level, done = 0;
    for (level = 0; level < mips; level++) {
        if (w == 0) w = 1;
        if (h == 0) h = 1;
        size_t lvl = (size_t)((w + 3) / 4) * ((h + 3) / 4) * block;
        if (lvl > left) break;
        glCompressedTexImage2D(GL_TEXTURE_2D, level, glfmt, w, h, 0, (GLsizei)lvl, p);
        p += lvl; left -= lvl; done++;
        w /= 2; h /= 2;
    }
    if (done == 0) { glDeleteTextures(1, &handle); return NULL; }
    if (done == 1 && mips > 1) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint)done - 1);

    G3DTexture *tex = (G3DTexture *)calloc(1, sizeof(G3DTexture));
    if (!tex) { glDeleteTextures(1, &handle); return NULL; }
    tex->width = width; tex->height = height; tex->channels = 4;
    tex->gl_handle = handle; tex->gpu_uploaded = 1;
    return tex;
#endif
}

/* Build a G3DTexture (RGBA, uploaded) from an SDL_Surface. */
static G3DTexture *texture_from_surface(SDL_Surface *src) {
    if (!src)
        return NULL;
    SDL_Surface *rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ABGR8888, 0);
    if (!rgba)
        return NULL;

    G3DTexture *tex = (G3DTexture *)calloc(1, sizeof(G3DTexture));
    if (!tex) { SDL_FreeSurface(rgba); return NULL; }
    tex->width = rgba->w;
    tex->height = rgba->h;
    tex->channels = 4;
    tex->data_size = (size_t)rgba->w * rgba->h * 4;
    tex->data = (uint8_t *)malloc(tex->data_size);
    if (!tex->data) { free(tex); SDL_FreeSurface(rgba); return NULL; }
    /* Copy row by row (respect pitch) */
    for (int y = 0; y < rgba->h; y++) {
        memcpy(tex->data + (size_t)y * rgba->w * 4,
               (uint8_t *)rgba->pixels + (size_t)y * rgba->pitch,
               (size_t)rgba->w * 4);
    }
    SDL_FreeSurface(rgba);
    g3d_texture_upload_gpu(tex);
    return tex;
}

/* Pick the albedo texture from a material, trying the common slots so models
   that don't use the standard PBR base-colour slot still get textured. */
static cgltf_texture *pick_albedo_texture(cgltf_material *mat) {
    if (!mat)
        return NULL;
    if (mat->has_pbr_metallic_roughness &&
        mat->pbr_metallic_roughness.base_color_texture.texture)
        return mat->pbr_metallic_roughness.base_color_texture.texture;
    if (mat->has_pbr_specular_glossiness &&
        mat->pbr_specular_glossiness.diffuse_texture.texture)
        return mat->pbr_specular_glossiness.diffuse_texture.texture;
    if (mat->emissive_texture.texture)
        return mat->emissive_texture.texture;
    return NULL;
}

/* Load one texture FILE: DDS (block-compressed, GPU-native) or anything
   SDL2_image can decode (PNG/JPG/...). Returns NULL if the file is missing or
   its format is unsupported. */
static G3DTexture *load_file_texture(const char *path) {
    size_t fsz = 0;
    void *fdata = SDL_LoadFile(path, &fsz);
    if (fdata) {
        G3DTexture *tex = NULL;
        if (fsz >= 4 && memcmp(fdata, "DDS ", 4) == 0)
            tex = load_dds_from_memory((const uint8_t *)fdata, fsz);
        SDL_free(fdata);
        if (tex) return tex;
    }
    SDL_Surface *surf = IMG_Load(path);
    if (!surf) return NULL;
    G3DTexture *tex = texture_from_surface(surf);
    SDL_FreeSurface(surf);
    return tex;
}

/* Load a cgltf image to a G3DTexture, handling DDS (block-compressed, GPU-native)
   and everything SDL2_image can decode (PNG/JPG/...), embedded or external. */
static G3DTexture *load_image_texture(cgltf_image *img, const char *gltf_path) {
    if (!img) return NULL;

    /* Embedded in the .glb buffer: we have the raw bytes directly. */
    if (img->buffer_view) {
        cgltf_buffer_view *bv = img->buffer_view;
        const uint8_t *bytes = (const uint8_t *)bv->buffer->data + bv->offset;
        if (bv->size >= 4 && memcmp(bytes, "DDS ", 4) == 0)
            return load_dds_from_memory(bytes, bv->size);
        SDL_RWops *rw = SDL_RWFromConstMem(bytes, (int)bv->size);
        if (!rw) return NULL;
        SDL_Surface *surf = IMG_Load_RW(rw, 1);
        if (!surf) return NULL;
        G3DTexture *tex = texture_from_surface(surf);
        SDL_FreeSurface(surf);
        return tex;
    }

    /* External file. */
    if (img->uri && strncmp(img->uri, "data:", 5) != 0) {
        char dir[512];
        strncpy(dir, gltf_path ? gltf_path : "", sizeof(dir) - 1);
        dir[sizeof(dir) - 1] = '\0';
        char *slash = strrchr(dir, '/');
        if (slash) slash[1] = '\0'; else dir[0] = '\0';
        char path[800];
        snprintf(path, sizeof(path), "%s%s", dir, img->uri);

        G3DTexture *tex = load_file_texture(path);
        if (tex) return tex;

        /* The URI file is missing or its format unsupported. Many exports
           (MSFT_texture_dds and friends) reference one extension but ship the
           sibling: e.g. cgltf hands us a .png that isn't on disk while the real
           texture next to it is a .dds (BC7/DXT, now supported). Try swapping
           .png/.jpg <-> .dds and load the sibling that actually exists. */
        size_t plen = strlen(path);
        if (plen > 4 && path[plen-4] == '.') {
            const char *ext = path + plen - 3;
            char alt[800];
            alt[0] = '\0';
            if (!strcasecmp(ext, "png") || !strcasecmp(ext, "jpg") || !strcasecmp(ext, "jpeg"))
                snprintf(alt, sizeof(alt), "%.*sdds", (int)(plen - 3), path);
            else if (!strcasecmp(ext, "dds"))
                snprintf(alt, sizeof(alt), "%.*spng", (int)(plen - 3), path);
            if (alt[0]) return load_file_texture(alt);
        }
        return NULL;
    }
    return NULL;
}

static G3DTexture *gltf_load_base_color(cgltf_material *mat,
                                        const char *gltf_path) {
    cgltf_texture *t = pick_albedo_texture(mat);
    if (!t || !t->image)
        return NULL;
    G3DTexture *tex = load_image_texture(t->image, gltf_path);
    if (!tex)
        fprintf(stderr, "G3D: glTF base-colour texture could not be decoded\n");
    return tex;
}

/* Build one submesh from every triangle primitive that uses `target` material
   (target == NULL collects primitives with no material). If only_node != NULL
   the material filter is ignored and only that node's primitives are collected
   (used by the fracture loader: one submesh per node/chunk). Returns NULL if
   none. */
static G3DMesh *build_submesh(cgltf_data *data, cgltf_material *target,
                              cgltf_node *only_node) {
    uint32_t cap_v = 4096;   /* grows as needed (uint32 indices, no hard cap) */
    G3DVertex *verts = (G3DVertex *)malloc(cap_v * sizeof(G3DVertex));
    uint32_t *indices = NULL;
    uint32_t vcount = 0, icap = 0, icount = 0;
    int any = 0;
    /* Per-vertex skinning data (filled only for skinned primitives) */
    uint16_t *sj = (uint16_t *)calloc((size_t)cap_v * 4, sizeof(uint16_t));
    float *sw = (float *)calloc((size_t)cap_v * 4, sizeof(float));
    int has_skin = 0;

    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh)
            continue;
        if (only_node && node != only_node)
            continue;

        /* Skinned meshes: the raw vertex positions are already the bind pose
           (jointWorld * inverseBind = identity at bind), so do NOT apply the
           node transform. Static meshes: bake the node's world transform. */
        float M[16];
        if (node->skin) {
            for (int k = 0; k < 16; k++) M[k] = 0.0f;
            M[0] = M[5] = M[10] = M[15] = 1.0f;
        } else {
            cgltf_node_transform_world(node, M); /* column-major 4x4 */
        }

        cgltf_mesh *mesh = node->mesh;
        for (cgltf_size pi = 0; pi < mesh->primitives_count; pi++) {
            cgltf_primitive *prim = &mesh->primitives[pi];
            if (prim->type != cgltf_primitive_type_triangles)
                continue;
            if (!only_node && prim->material != target)
                continue;

            cgltf_accessor *a_pos = NULL, *a_nrm = NULL, *a_uv = NULL, *a_col = NULL;
            cgltf_accessor *a_joint = NULL, *a_weight = NULL;
            for (cgltf_size ai = 0; ai < prim->attributes_count; ai++) {
                cgltf_attribute *at = &prim->attributes[ai];
                if (at->type == cgltf_attribute_type_position) a_pos = at->data;
                else if (at->type == cgltf_attribute_type_normal) a_nrm = at->data;
                else if (at->type == cgltf_attribute_type_texcoord && !a_uv) a_uv = at->data;
                else if (at->type == cgltf_attribute_type_color && !a_col) a_col = at->data;
                else if (at->type == cgltf_attribute_type_joints && !a_joint) a_joint = at->data;
                else if (at->type == cgltf_attribute_type_weights && !a_weight) a_weight = at->data;
            }
            if (!a_pos)
                continue;
            (void)a_col;

            uint32_t base = vcount;
            uint32_t nv = (uint32_t)a_pos->count;
            if (base + nv > cap_v) {
                /* grow vertex / skin buffers (no hard cap -> detailed meshes) */
                while (cap_v < base + nv) cap_v *= 2;
                verts = (G3DVertex *)realloc(verts, cap_v * sizeof(G3DVertex));
                sj = (uint16_t *)realloc(sj, (size_t)cap_v * 4 * sizeof(uint16_t));
                sw = (float *)realloc(sw, (size_t)cap_v * 4 * sizeof(float));
            }

            for (uint32_t i = 0; i < nv; i++) {
                G3DVertex *v = &verts[base + i];
                float p[3] = {0, 0, 0}, n[3] = {0, 1, 0}, uv[2] = {0, 0};
                cgltf_accessor_read_float(a_pos, i, p, 3);
                if (a_nrm) cgltf_accessor_read_float(a_nrm, i, n, 3);
                if (a_uv) cgltf_accessor_read_float(a_uv, i, uv, 2);
                /* Bake the node's world transform into the vertex. cgltf returns
                   M in COLUMN-major order, so x' = M[0]px+M[4]py+M[8]pz+M[12]. */
                v->position[0] = M[0]*p[0] + M[4]*p[1] + M[8]*p[2]  + M[12];
                v->position[1] = M[1]*p[0] + M[5]*p[1] + M[9]*p[2]  + M[13];
                v->position[2] = M[2]*p[0] + M[6]*p[1] + M[10]*p[2] + M[14];
                float nx = M[0]*n[0] + M[4]*n[1] + M[8]*n[2];
                float ny = M[1]*n[0] + M[5]*n[1] + M[9]*n[2];
                float nz = M[2]*n[0] + M[6]*n[1] + M[10]*n[2];
                float nl = sqrtf(nx*nx + ny*ny + nz*nz);
                if (nl < 1e-6f) nl = 1.0f;
                v->normal[0] = nx/nl; v->normal[1] = ny/nl; v->normal[2] = nz/nl;
                /* glTF samples textures with UV origin top-left and NO flip
                   (matches three.js flipY=false); our upload already matches. */
                v->texcoord[0] = uv[0]; v->texcoord[1] = uv[1];

                /* Skinning attributes (joint indices are skin-relative) */
                if (a_joint && a_weight) {
                    cgltf_uint ji[4] = {0, 0, 0, 0};
                    float wj[4] = {0, 0, 0, 0};
                    cgltf_accessor_read_uint(a_joint, i, ji, 4);
                    cgltf_accessor_read_float(a_weight, i, wj, 4);
                    uint16_t *dj = &sj[(base + i) * 4];
                    float *dw = &sw[(base + i) * 4];
                    for (int k = 0; k < 4; k++) {
                        dj[k] = (uint16_t)ji[k];
                        dw[k] = wj[k];
                    }
                    has_skin = 1;
                }
            }
            vcount += nv;

            uint32_t prim_ic = prim->indices ? (uint32_t)prim->indices->count : nv;
            if (icount + prim_ic > icap) {
                icap = (icount + prim_ic) * 2;
                indices = (uint32_t *)realloc(indices, icap * sizeof(uint32_t));
            }
            for (uint32_t i = 0; i < prim_ic; i++) {
                uint32_t idx = prim->indices
                                   ? (uint32_t)cgltf_accessor_read_index(prim->indices, i)
                                   : i;
                indices[icount++] = base + idx;
            }
            any = 1;
        }
    }

    G3DMesh *mesh = NULL;
    if (any && vcount > 0 && icount > 0) {
        /* Created on the CPU only; uploaded after the whole model is recentered */
        mesh = g3d_mesh_create("glTFsub", verts, vcount, indices, icount);
        if (mesh && has_skin) {
            /* Keep the raw bind pose + per-vertex joints/weights for CPU skinning */
            mesh->skinned = 1;
            mesh->bind_pos = (float *)malloc((size_t)vcount * 3 * sizeof(float));
            mesh->bind_nrm = (float *)malloc((size_t)vcount * 3 * sizeof(float));
            mesh->vjoints = (uint16_t *)malloc((size_t)vcount * 4 * sizeof(uint16_t));
            mesh->vweights = (float *)malloc((size_t)vcount * 4 * sizeof(float));
            for (uint32_t v = 0; v < vcount; v++) {
                mesh->bind_pos[v * 3 + 0] = verts[v].position[0];
                mesh->bind_pos[v * 3 + 1] = verts[v].position[1];
                mesh->bind_pos[v * 3 + 2] = verts[v].position[2];
                mesh->bind_nrm[v * 3 + 0] = verts[v].normal[0];
                mesh->bind_nrm[v * 3 + 1] = verts[v].normal[1];
                mesh->bind_nrm[v * 3 + 2] = verts[v].normal[2];
            }
            memcpy(mesh->vjoints, sj, (size_t)vcount * 4 * sizeof(uint16_t));
            memcpy(mesh->vweights, sw, (size_t)vcount * 4 * sizeof(float));
        }
    }
    free(verts);
    free(indices);
    free(sj);
    free(sw);
    return mesh;
}

/* Center the model on X/Z, rest it on Y=0 (feet at origin) and (re)upload all
   submeshes. Shared by load and orient. If out_off != NULL it receives the
   applied offset (used by skinned models, whose bind_pos stays un-offset). */
/* When 0, loaded models keep their ORIGINAL vertex coordinates instead of being
   re-centered on X/Z and dropped to Y=0. Streamed world/map sectors need this:
   their geometry is baked at absolute world positions, so re-centering each
   sector to the origin would pile every sector on top of each other. */
static int g_gltf_recenter = 1;
void g3d_gltf_set_recenter(int on) { g_gltf_recenter = on ? 1 : 0; }

static void model_recenter_upload_off(G3DMesh *meshes, int sub, float *out_off) {
    if (sub <= 0)
        return;
    /* Recompute bounds first (vertices may have just been rotated by orient) */
    for (int s = 0; s < sub; s++)
        g3d_mesh_calculate_bounds(&meshes[s]);
    float mn[3], mx[3];
    for (int k = 0; k < 3; k++) { mn[k] = meshes[0].aabb_min[k]; mx[k] = meshes[0].aabb_max[k]; }
    for (int s = 1; s < sub; s++) {
        for (int k = 0; k < 3; k++) {
            if (meshes[s].aabb_min[k] < mn[k]) mn[k] = meshes[s].aabb_min[k];
            if (meshes[s].aabb_max[k] > mx[k]) mx[k] = meshes[s].aabb_max[k];
        }
    }
    float off[3] = { -0.5f * (mn[0] + mx[0]), -mn[1], -0.5f * (mn[2] + mx[2]) };
    if (!g_gltf_recenter) { off[0] = off[1] = off[2] = 0.0f; }   /* keep absolute coords (sectors) */
    for (int s = 0; s < sub; s++) {
        G3DMesh *m = &meshes[s];
        for (uint32_t v = 0; v < m->vertex_count; v++) {
            m->vertices[v].position[0] += off[0];
            m->vertices[v].position[1] += off[1];
            m->vertices[v].position[2] += off[2];
        }
        g3d_mesh_calculate_bounds(m);
        if (m->gpu_uploaded)
            g3d_mesh_update_gpu(m);   /* orient(): re-upload edited vertices */
        else
            g3d_mesh_upload_gpu(m);
    }
    if (out_off) { out_off[0] = off[0]; out_off[1] = off[1]; out_off[2] = off[2]; }
}

static void model_recenter_upload(G3DMesh *meshes, int sub) {
    model_recenter_upload_off(meshes, sub, NULL);
}

/* Bake an orientation correction (Euler radians) into a model's vertices, then
   re-ground it. Lets a script fix models whose authored bind pose is tilted,
   keeping them standing on the floor and spinnable in place. */
void g3d_model_orient(G3DModel *model, float rx, float ry, float rz) {
    if (!model || model->mesh_count == 0)
        return;
    Mat4 R = mat4_trs(vec3_make(0, 0, 0), quat_from_euler(rx, ry, rz),
                      vec3_make(1, 1, 1));
    for (uint32_t s = 0; s < model->mesh_count; s++) {
        G3DMesh *m = &model->meshes[s];
        for (uint32_t v = 0; v < m->vertex_count; v++) {
            float *p = m->vertices[v].position;
            float *n = m->vertices[v].normal;
            float x = p[0], y = p[1], z = p[2];
            p[0] = R.m[0]*x + R.m[4]*y + R.m[8]*z + R.m[12];
            p[1] = R.m[1]*x + R.m[5]*y + R.m[9]*z + R.m[13];
            p[2] = R.m[2]*x + R.m[6]*y + R.m[10]*z + R.m[14];
            float nx = n[0], ny = n[1], nz = n[2];
            n[0] = R.m[0]*nx + R.m[4]*ny + R.m[8]*nz;
            n[1] = R.m[1]*nx + R.m[5]*ny + R.m[9]*nz;
            n[2] = R.m[2]*nx + R.m[6]*ny + R.m[10]*nz;
        }
    }
    model_recenter_upload(model->meshes, (int)model->mesh_count);
}

/* Decompose a column-major TRS matrix into translation/rotation/scale. */
static void decompose_matrix(const float *m, Vec3 *t, Quat *r, Vec3 *s) {
    t->x = m[12]; t->y = m[13]; t->z = m[14];
    float sx = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
    float sy = sqrtf(m[4]*m[4] + m[5]*m[5] + m[6]*m[6]);
    float sz = sqrtf(m[8]*m[8] + m[9]*m[9] + m[10]*m[10]);
    s->x = sx; s->y = sy; s->z = sz;
    Mat4 R = mat4_identity();
    if (sx > 1e-8f) { R.m[0]=m[0]/sx; R.m[1]=m[1]/sx; R.m[2]=m[2]/sx; }
    if (sy > 1e-8f) { R.m[4]=m[4]/sy; R.m[5]=m[5]/sy; R.m[6]=m[6]/sy; }
    if (sz > 1e-8f) { R.m[8]=m[8]/sz; R.m[9]=m[9]/sz; R.m[10]=m[10]/sz; }
    *r = mat4_get_rotation(R);
}

/* Read the node hierarchy, the first skin's joints/inverse-binds and all the
   animations into the model. Must run while cgltf `data` (and its buffers) are
   still alive. Returns 1 if the model is skinned. */
static int build_skeleton(cgltf_data *data, G3DModel *model) {
    if (data->skins_count == 0 || data->nodes_count == 0)
        return 0;

    int nc = (int)data->nodes_count;
    model->node_count = nc;
    model->node_parent = (int *)malloc(nc * sizeof(int));
    model->node_base_t = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_base_r = (Quat *)malloc(nc * sizeof(Quat));
    model->node_base_s = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_cur_t = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_cur_r = (Quat *)malloc(nc * sizeof(Quat));
    model->node_cur_s = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_global = (Mat4 *)malloc(nc * sizeof(Mat4));

    for (int i = 0; i < nc; i++) {
        cgltf_node *nd = &data->nodes[i];
        model->node_parent[i] = nd->parent ? (int)(nd->parent - data->nodes) : -1;
        Vec3 t, s; Quat r;
        if (nd->has_matrix) {
            decompose_matrix(nd->matrix, &t, &r, &s);
        } else {
            t = vec3_make(nd->translation[0], nd->translation[1], nd->translation[2]);
            r = quat_make(nd->rotation[0], nd->rotation[1], nd->rotation[2], nd->rotation[3]);
            s = vec3_make(nd->scale[0], nd->scale[1], nd->scale[2]);
        }
        model->node_base_t[i] = t; model->node_cur_t[i] = t;
        model->node_base_r[i] = r; model->node_cur_r[i] = r;
        model->node_base_s[i] = s; model->node_cur_s[i] = s;
    }

    cgltf_skin *skin = &data->skins[0];
    int jc = (int)skin->joints_count;
    model->joint_count = jc;
    model->joint_node = (int *)malloc(jc * sizeof(int));
    model->inverse_bind = (Mat4 *)malloc(jc * sizeof(Mat4));
    model->joint_matrix = (Mat4 *)malloc(jc * sizeof(Mat4));
    for (int j = 0; j < jc; j++) {
        model->joint_node[j] = (int)(skin->joints[j] - data->nodes);
        Mat4 ib = mat4_identity();
        if (skin->inverse_bind_matrices) {
            float f[16];
            cgltf_accessor_read_float(skin->inverse_bind_matrices, j, f, 16);
            for (int k = 0; k < 16; k++) ib.m[k] = f[k];
        }
        model->inverse_bind[j] = ib;
    }

    /* Topmost joint = the joint whose parent is not itself a joint. Used to
       strip root motion (keep the character animating in place). */
    {
        char *is_joint = (char *)calloc(nc, 1);
        for (int j = 0; j < jc; j++) is_joint[model->joint_node[j]] = 1;
        model->root_node = -1;
        for (int j = 0; j < jc; j++) {
            int nd = model->joint_node[j];
            int p = model->node_parent[nd];
            if (p < 0 || !is_joint[p]) { model->root_node = nd; break; }
        }
        free(is_joint);
    }
    model->lock_root = 1;   /* default: in-place preview */

    int ac = (int)data->animations_count;
    model->animation_count = ac;
    model->animations = ac ? (G3DAnimation *)calloc(ac, sizeof(G3DAnimation)) : NULL;
    for (int a = 0; a < ac; a++) {
        cgltf_animation *ga = &data->animations[a];
        G3DAnimation *A = &model->animations[a];
        if (ga->name) strncpy(A->name, ga->name, sizeof(A->name) - 1);
        int cc = (int)ga->channels_count;
        A->channel_count = cc;
        A->channels = (G3DAnimChannel *)calloc(cc, sizeof(G3DAnimChannel));
        float dur = 0.0f;
        for (int c = 0; c < cc; c++) {
            cgltf_animation_channel *gc = &ga->channels[c];
            G3DAnimChannel *C = &A->channels[c];
            if (!gc->target_node ||
                gc->target_path == cgltf_animation_path_type_weights) {
                C->node = -1;
                continue;
            }
            C->node = (int)(gc->target_node - data->nodes);
            C->path = (gc->target_path == cgltf_animation_path_type_translation) ? 0
                    : (gc->target_path == cgltf_animation_path_type_rotation) ? 1 : 2;
            cgltf_animation_sampler *gs = gc->sampler;
            int cubic = (gs->interpolation == cgltf_interpolation_type_cubic_spline);
            C->interp = (gs->interpolation == cgltf_interpolation_type_step) ? 1 : 0;
            int kc = (int)gs->input->count;
            C->key_count = kc;
            C->times = (float *)malloc(kc * sizeof(float));
            for (int k = 0; k < kc; k++)
                cgltf_accessor_read_float(gs->input, k, &C->times[k], 1);
            int comps = (C->path == 1) ? 4 : 3;
            C->values = (float *)malloc((size_t)kc * comps * sizeof(float));
            for (int k = 0; k < kc; k++) {
                /* cubic spline stores [inTangent, value, outTangent] per key;
                   take the value and treat it as linear. */
                cgltf_size oi = cubic ? (cgltf_size)(k * 3 + 1) : (cgltf_size)k;
                cgltf_accessor_read_float(gs->output, oi, &C->values[k * comps], comps);
            }
            if (kc > 0 && C->times[kc - 1] > dur) dur = C->times[kc - 1];
        }
        A->duration = dur;
    }

    model->skinned = 1;
    return 1;
}

G3DModel *g3d_gltf_load(const char *filepath) {
    if (!filepath)
        return NULL;

    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data *data = NULL;

    if (cgltf_parse_file(&options, filepath, &data) != cgltf_result_success) {
        fprintf(stderr, "G3D: glTF parse failed: %s\n", filepath);
        return NULL;
    }
    if (cgltf_load_buffers(&options, data, filepath) != cgltf_result_success) {
        fprintf(stderr, "G3D: glTF buffer load failed: %s\n", filepath);
        cgltf_free(data);
        return NULL;
    }

    /* One submesh per material (+ one for material-less primitives) so each
       part keeps its own base-colour texture. */
    int max_sub = (int)data->materials_count + 1;
    G3DMesh *meshes = (G3DMesh *)calloc(max_sub, sizeof(G3DMesh));
    void **textures = (void **)calloc(max_sub, sizeof(void *));
    int sub = 0;

    for (cgltf_size m = 0; m < data->materials_count; m++) {
        G3DMesh *sm = build_submesh(data, &data->materials[m], NULL);
        if (!sm)
            continue;
        meshes[sub] = *sm;           /* copy mesh struct into the array */
        free(sm);
        textures[sub] = gltf_load_base_color(&data->materials[m], filepath);
        sub++;
    }
    /* Primitives without a material */
    {
        G3DMesh *sm = build_submesh(data, NULL, NULL);
        if (sm) {
            meshes[sub] = *sm;
            free(sm);
            textures[sub] = NULL;
            sub++;
        }
    }

    if (sub == 0) {
        fprintf(stderr, "G3D: glTF has no triangle geometry: %s\n", filepath);
        free(meshes); free(textures); cgltf_free(data);
        return NULL;
    }

    /* Center on X/Z, rest on Y=0, upload. Capture the offset so skinned models
       (whose bind_pos stays un-offset) can re-apply it after skinning. */
    float off[3] = {0, 0, 0};
    model_recenter_upload_off(meshes, sub, off);

    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));
    if (!model) { free(meshes); free(textures); cgltf_free(data); return NULL; }
    model->meshes = meshes;
    model->mesh_count = (uint32_t)sub;
    model->mesh_textures = textures;
    model->albedo_texture = textures[0];
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);

    /* Skeleton + animations (while cgltf data is still alive) */
    if (build_skeleton(data, model)) {
        model->skin_offset[0] = off[0];
        model->skin_offset[1] = off[1];
        model->skin_offset[2] = off[2];
    }

    printf("G3D: glTF loaded: %s (%d submeshes, images=%d materials=%d, h=%.2f, anims=%d, joints=%d)\n",
           filepath, sub, (int)data->images_count, (int)data->materials_count,
           meshes[0].aabb_max[1] - meshes[0].aabb_min[1],
           model->animation_count, model->joint_count);

    cgltf_free(data);
    return model;
}

/* Fracture loader: one submesh PER NODE (not per material), so a pre-fractured
   model whose chunks all share one material still comes in as N separate
   submeshes. Each submesh keeps its chunk's baked world position, so its AABB
   centre is the chunk centroid the physics uses. Static geometry only (no
   skeleton/animation). */
G3DModel *g3d_gltf_load_fractured(const char *filepath) {
    if (!filepath)
        return NULL;

    cgltf_options options;
    memset(&options, 0, sizeof(options));
    cgltf_data *data = NULL;

    if (cgltf_parse_file(&options, filepath, &data) != cgltf_result_success) {
        fprintf(stderr, "G3D: glTF parse failed: %s\n", filepath);
        return NULL;
    }
    if (cgltf_load_buffers(&options, data, filepath) != cgltf_result_success) {
        fprintf(stderr, "G3D: glTF buffer load failed: %s\n", filepath);
        cgltf_free(data);
        return NULL;
    }

    /* Cache one texture per material so shared-material chunks don't re-upload
       (and don't create duplicate GPU handles to free). */
    int mc = (int)data->materials_count;
    void **mat_tex = mc ? (void **)calloc(mc, sizeof(void *)) : NULL;
    for (int m = 0; m < mc; m++)
        mat_tex[m] = gltf_load_base_color(&data->materials[m], filepath);

    int max_sub = (int)data->nodes_count + 1;
    G3DMesh *meshes = (G3DMesh *)calloc(max_sub, sizeof(G3DMesh));
    void **textures = (void **)calloc(max_sub, sizeof(void *));
    int sub = 0;

    for (cgltf_size ni = 0; ni < data->nodes_count; ni++) {
        cgltf_node *node = &data->nodes[ni];
        if (!node->mesh)
            continue;
        G3DMesh *sm = build_submesh(data, NULL, node);
        if (!sm)
            continue;
        meshes[sub] = *sm;
        free(sm);
        /* Texture from this node's first primitive material (cached). */
        void *tex = NULL;
        for (cgltf_size pi = 0; pi < node->mesh->primitives_count; pi++) {
            cgltf_material *pm = node->mesh->primitives[pi].material;
            if (pm) { tex = mat_tex[(int)(pm - data->materials)]; break; }
        }
        textures[sub] = tex;
        sub++;
    }
    free(mat_tex);

    if (sub == 0) {
        fprintf(stderr, "G3D: glTF (fractured) has no triangle geometry: %s\n", filepath);
        free(meshes); free(textures); cgltf_free(data);
        return NULL;
    }

    /* Recenter the whole assembled model as one group (chunks keep their
       relative offsets, so each submesh AABB is the chunk's placement). */
    float off[3] = {0, 0, 0};
    model_recenter_upload_off(meshes, sub, off);

    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));
    if (!model) { free(meshes); free(textures); cgltf_free(data); return NULL; }
    model->meshes = meshes;
    model->mesh_count = (uint32_t)sub;
    model->mesh_textures = textures;
    model->albedo_texture = textures[0];
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);

    printf("G3D: glTF loaded (fractured): %s (%d chunks, materials=%d)\n",
           filepath, sub, mc);

    cgltf_free(data);
    return model;
}

void g3d_gltf_free(G3DModel *model) {
    if (!model)
        return;
    /* Animation channels */
    for (int a = 0; a < model->animation_count; a++) {
        G3DAnimation *A = &model->animations[a];
        for (int c = 0; c < A->channel_count; c++) {
            free(A->channels[c].times);
            free(A->channels[c].values);
        }
        free(A->channels);
    }
    free(model->animations);
    /* Skeleton */
    free(model->node_parent);
    free(model->node_base_t); free(model->node_base_r); free(model->node_base_s);
    free(model->node_cur_t); free(model->node_cur_r); free(model->node_cur_s);
    free(model->node_global);
    free(model->joint_node); free(model->inverse_bind); free(model->joint_matrix);
    if (model->meshes)
        g3d_mesh_free(model->meshes);
    free(model);
}
