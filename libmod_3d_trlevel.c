/*
 * libmod_3d_trlevel.c - Native Tomb Raider level loader (TR1 first)
 *
 * See libmod_3d_trlevel.h for the format map. Everything here was verified
 * against real level files rather than assumed; the notes below are the places
 * where a wrong guess fails SILENTLY, which is what makes them worth writing
 * down:
 *
 *  - TR1's palette is 6-bit VGA (0..63), not 0..255. Without the <<2 the whole
 *    level renders four times too dark and nothing errors.
 *  - TR1 keeps its palette at the END of the file; TR2/TR3 put it right after
 *    the version. Assuming TR1's layout for TR2 desynchronises the whole parse.
 *  - anim_dispatch is 8 bytes, not 2. Getting it wrong throws every offset
 *    after the animations out.
 *  - TR's Y axis points DOWN, and room vertices are local to their room.
 *  - TR1 embeds its audio samples after the palette, so the parse legitimately
 *    stops well before EOF.
 */

#include "libmod_3d_trlevel.h"
#include "libmod_3d_texture.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- little-endian reader that can't run off the end ---------------------- */

typedef struct {
    const unsigned char *d;
    size_t size, at;
    int overrun;
} TRReader;

static int tr_need(TRReader *r, size_t n) {
    if (r->overrun || r->at + n > r->size) { r->overrun = 1; return 0; }
    return 1;
}
static unsigned int tr_u32(TRReader *r) {
    if (!tr_need(r, 4)) return 0;
    const unsigned char *p = r->d + r->at; r->at += 4;
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
           ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}
static int tr_i32(TRReader *r) { return (int)tr_u32(r); }
static unsigned short tr_u16(TRReader *r) {
    if (!tr_need(r, 2)) return 0;
    const unsigned char *p = r->d + r->at; r->at += 2;
    return (unsigned short)(p[0] | (p[1] << 8));
}
static short tr_i16(TRReader *r) { return (short)tr_u16(r); }
static void tr_skip(TRReader *r, size_t n) {
    if (tr_need(r, n)) r->at += n;
}

/* ---- version ------------------------------------------------------------- */

static int ext_is(const char *path, const char *ext) {
    size_t lp = strlen(path), le = strlen(ext);
    if (lp < le) return 0;
    const char *p = path + lp - le;
    for (size_t i = 0; i < le; i++) {
        char a = p[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a += 32;
        if (b >= 'A' && b <= 'Z') b += 32;
        if (a != b) return 0;
    }
    return 1;
}

static int tr_version_of(unsigned int magic, const char *path) {
    switch (magic) {
        case 0x00000020: return G3D_TR1;
        case 0x0000002D: return G3D_TR2;
        case 0xFF180038:
        case 0xFF080038: return G3D_TR3;
        case 0x00345254:            /* "TR4\0" - shared by TR4 AND TR5 */
            return ext_is(path, ".trc") ? G3D_TR5 : G3D_TR4;
        default: return G3D_TR_UNKNOWN;
    }
}

int g3d_tr_probe(const char *filepath) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return G3D_TR_UNKNOWN;
    unsigned char h[4] = {0, 0, 0, 0};
    size_t n = fread(h, 1, 4, f);
    fclose(f);
    if (n != 4) return G3D_TR_UNKNOWN;
    unsigned int magic = (unsigned int)h[0] | ((unsigned int)h[1] << 8) |
                         ((unsigned int)h[2] << 16) | ((unsigned int)h[3] << 24);
    return tr_version_of(magic, filepath);
}

/* ---- TR1 ----------------------------------------------------------------- */

#define TEXTILE_W 256
#define TEXTILE_H 256

/* One TR room, kept only while building the model. */
typedef struct {
    int x, z;                 /* world offset; vertices are room-local */
    G3DVertex *verts;
    uint32_t nverts;
    uint32_t *idx;
    uint32_t nidx, idxcap;
} TRRoom;

/* An object texture: which textile, and the UVs of its corners. */
typedef struct {
    unsigned short tile;
    float u[4], v[4];
} TRObjTex;

static void room_push(TRRoom *rm, uint32_t a, uint32_t b, uint32_t c) {
    if (rm->nidx + 3 > rm->idxcap) {
        rm->idxcap = rm->idxcap ? rm->idxcap * 2 : 256;
        rm->idx = (uint32_t *)realloc(rm->idx, rm->idxcap * sizeof(uint32_t));
    }
    rm->idx[rm->nidx++] = a;
    rm->idx[rm->nidx++] = b;
    rm->idx[rm->nidx++] = c;
}

/* Build one RGBA texture from all the 8-bit textiles stacked vertically, so a
   room's faces can all share a single atlas and one draw call. */
static G3DTexture *tr1_build_atlas(const unsigned char *tiles, int ntiles,
                                   const unsigned char *pal) {
    if (ntiles <= 0) return NULL;
    int w = TEXTILE_W, h = TEXTILE_H * ntiles;
    unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!rgba) return NULL;

    for (int t = 0; t < ntiles; t++) {
        const unsigned char *src = tiles + (size_t)t * TEXTILE_W * TEXTILE_H;
        for (int i = 0; i < TEXTILE_W * TEXTILE_H; i++) {
            unsigned char c = src[i];
            unsigned char *o = rgba + (((size_t)t * TEXTILE_W * TEXTILE_H) + i) * 4;
            /* Palette index 0 is the transparent colour in TR. */
            if (c == 0) { o[0] = o[1] = o[2] = 0; o[3] = 0; continue; }
            /* 6-bit VGA palette: scale to 8-bit or everything is 4x too dark. */
            o[0] = (unsigned char)(pal[c * 3 + 0] << 2);
            o[1] = (unsigned char)(pal[c * 3 + 1] << 2);
            o[2] = (unsigned char)(pal[c * 3 + 2] << 2);
            o[3] = 255;
        }
    }

    G3DTexture *tex = (G3DTexture *)calloc(1, sizeof(G3DTexture));
    if (!tex) { free(rgba); return NULL; }
    tex->width = w;
    tex->height = h;
    tex->channels = 4;
    tex->data = rgba;
    tex->data_size = (size_t)w * h * 4;
    g3d_texture_upload_gpu(tex);
    return tex;
}

static G3DModel *tr123_load(TRReader *r, const char *filepath, int ver) {
    /* Per-version room layout differences, all verified against real levels:
         room_vertex : TR1 = 8 bytes (x,y,z,light), TR2/3 = 12 (two lights + attrs)
         after ambient intensity: TR1 +0, TR2 +2, TR3 +2 extra
         room light  : TR1 = 18 bytes, TR2/3 = 24
         room static : TR1 = 18 bytes, TR2/3 = 20
         room tail   : TR3 has 3 extra bytes (waterScheme, reverbInfo, filler) */
    int vert_sz   = (ver == G3D_TR1) ? 8  : 12;
    int light_sz  = (ver == G3D_TR1) ? 18 : 24;
    int static_sz = (ver == G3D_TR1) ? 18 : 20;
    /* Bytes between the ambient intensity and the light list: TR2 has 4 extra,
       TR3 has 2 (measured by brute-force over real levels; getting these two
       swapped desynchronises from the second room on). */
    int after_amb = (ver == G3D_TR1) ? 0  : ((ver == G3D_TR2) ? 4 : 2);
    int room_tail = (ver == G3D_TR3) ? 3  : 0;

    /* Palette: TR1 keeps it at the END of the file, TR2/3 right after the
       version (which we've already read). Grab TR2/3's now. */
    const unsigned char *pal = NULL;
    if (ver != G3D_TR1) {
        pal = r->d + r->at;
        tr_skip(r, 768);                 /* 8-bit palette */
        tr_skip(r, 1024);                /* 16-bit palette (unused here) */
    }

    unsigned int ntiles = tr_u32(r);
    if (r->overrun || ntiles == 0 || ntiles > 64) return NULL;
    const unsigned char *tiles = r->d + r->at;
    tr_skip(r, (size_t)ntiles * TEXTILE_W * TEXTILE_H);
    /* TR2/3 also carry 16-bit textiles right after the 8-bit ones; we texture
       from the 8-bit set + palette, so skip them. */
    if (ver != G3D_TR1)
        tr_skip(r, (size_t)ntiles * TEXTILE_W * TEXTILE_H * 2);
    tr_skip(r, 4);                       /* unused */

    int nrooms = tr_u16(r);
    if (r->overrun || nrooms <= 0) return NULL;

    TRRoom *rooms = (TRRoom *)calloc((size_t)nrooms, sizeof(TRRoom));
    /* Faces reference object textures, which are stored much later in the file,
       so remember each face's texture id and resolve the UVs in a second pass. */
    unsigned short **face_tex = (unsigned short **)calloc((size_t)nrooms, sizeof(unsigned short *));
    uint32_t *face_n = (uint32_t *)calloc((size_t)nrooms, sizeof(uint32_t));
    if (!rooms || !face_tex || !face_n) { free(rooms); free(face_tex); free(face_n); return NULL; }

    for (int i = 0; i < nrooms; i++) {
        TRRoom *rm = &rooms[i];
        rm->x = tr_i32(r);
        rm->z = tr_i32(r);
        tr_skip(r, 8);                   /* yBottom, yTop */

        unsigned int ndw = tr_u32(r);
        size_t data_end = r->at + (size_t)ndw * 2;

        int nv = tr_i16(r);
        if (nv < 0 || r->overrun) break;
        rm->nverts = (uint32_t)nv;
        rm->verts = (G3DVertex *)calloc((size_t)(nv > 0 ? nv : 1), sizeof(G3DVertex));
        for (int v = 0; v < nv; v++) {
            short vx = tr_i16(r), vy = tr_i16(r), vz = tr_i16(r);
            tr_skip(r, (size_t)vert_sz - 6);   /* light(s)/attributes */
            /* Room-local -> world, and TR's Y points DOWN so negate it. */
            rm->verts[v].position[0] = (float)(rm->x + vx);
            rm->verts[v].position[1] = (float)(-vy);
            rm->verts[v].position[2] = (float)(rm->z + vz);
            rm->verts[v].normal[0] = 0.0f;
            rm->verts[v].normal[1] = 1.0f;
            rm->verts[v].normal[2] = 0.0f;
        }

        /* Worst case every face is a quad: 2 triangles, 6 indices. */
        int nq = tr_i16(r);
        unsigned short *ftex = (unsigned short *)calloc((size_t)(nq > 0 ? nq : 1) + 1, sizeof(unsigned short));
        uint32_t fn = 0;
        for (int q = 0; q < nq; q++) {
            unsigned short a = tr_u16(r), b = tr_u16(r), c = tr_u16(r), dd = tr_u16(r);
            unsigned short t = tr_u16(r);
            if (a < rm->nverts && b < rm->nverts && c < rm->nverts && dd < rm->nverts) {
                room_push(rm, a, b, c);
                room_push(rm, a, c, dd);
                ftex[fn++] = t;
            }
        }
        int nt = tr_i16(r);
        ftex = (unsigned short *)realloc(ftex, (size_t)(fn + (nt > 0 ? nt : 1) + 1) * sizeof(unsigned short));
        for (int t3 = 0; t3 < nt; t3++) {
            unsigned short a = tr_u16(r), b = tr_u16(r), c = tr_u16(r);
            unsigned short t = tr_u16(r);
            if (a < rm->nverts && b < rm->nverts && c < rm->nverts) {
                room_push(rm, a, b, c);
                ftex[fn++] = t;
            }
        }
        face_tex[i] = ftex;
        face_n[i] = fn;

        r->at = data_end;                /* skip sprites and any trailing data */
        if (r->at > r->size) { r->overrun = 1; break; }

        int npor = tr_u16(r); tr_skip(r, (size_t)npor * 32);
        int nz = tr_u16(r), nx = tr_u16(r); tr_skip(r, (size_t)nz * nx * 8);
        tr_skip(r, 2);                   /* ambient intensity */
        tr_skip(r, (size_t)after_amb);   /* TR2/3: extra ambient/light-mode */
        int nl = tr_u16(r); tr_skip(r, (size_t)nl * light_sz);
        int nsm = tr_u16(r); tr_skip(r, (size_t)nsm * static_sz);
        tr_skip(r, 4);                   /* alternate room + flags */
        tr_skip(r, (size_t)room_tail);   /* TR3: waterScheme, reverbInfo, filler */
        if (r->overrun) break;
    }

    /* Walk to the object textures. Every count here was checked against a real
       level; anim_dispatch in particular is 8 bytes, not 2. */
    unsigned int n;
    n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* floor data */
    n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* mesh data */
    n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh pointers */
    n = tr_u32(r); tr_skip(r, (size_t)n * 32);     /* animations */
    n = tr_u32(r); tr_skip(r, (size_t)n * 6);      /* state changes */
    n = tr_u32(r); tr_skip(r, (size_t)n * 8);      /* anim dispatches */
    n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* anim commands */
    n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh trees */
    n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* frames */
    n = tr_u32(r); tr_skip(r, (size_t)n * 18);     /* models */
    n = tr_u32(r); tr_skip(r, (size_t)n * 32);     /* static meshes */

    unsigned int nobjtex = tr_u32(r);
    TRObjTex *otex = NULL;
    if (!r->overrun && nobjtex > 0 && nobjtex < 65536) {
        otex = (TRObjTex *)calloc(nobjtex, sizeof(TRObjTex));
        for (unsigned int i = 0; i < nobjtex; i++) {
            tr_skip(r, 2);                          /* attribute (blend mode) */
            unsigned short tile = tr_u16(r);
            otex[i].tile = tile & 0x7FFF;
            for (int c = 0; c < 4; c++) {
                /* Each coord is a 16-bit fixed point: whole part in the high
                   byte, and the low byte pushes the sample inside the texel. */
                unsigned short xc = tr_u16(r);
                unsigned short yc = tr_u16(r);
                otex[i].u[c] = (float)(xc >> 8) / (float)TEXTILE_W;
                otex[i].v[c] = (float)(yc >> 8) / (float)TEXTILE_H;
            }
        }
    }

    /* TR1's palette is at the very END of the file, so keep walking to reach it.
       TR2/3 already gave us the palette up top, so stop here. */
    if (ver == G3D_TR1) {
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* sprite textures */
        n = tr_u32(r); tr_skip(r, (size_t)n * 8);      /* sprite sequences */
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* cameras */
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* sound sources */
        unsigned int nbox = tr_u32(r); tr_skip(r, (size_t)nbox * 20);
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* overlaps */
        tr_skip(r, (size_t)nbox * 2 * 6);              /* zones */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* animated textures */
        n = tr_u32(r); tr_skip(r, (size_t)n * 22);     /* entities */
        tr_skip(r, 8192);                              /* lightmap */
        if (!r->overrun && r->at + 768 <= r->size) pal = r->d + r->at;
    }

    if (!pal || !otex) {
        fprintf(stderr, "G3D: TR%d parse failed before the palette: %s\n", ver, filepath);
        for (int i = 0; i < nrooms; i++) { free(rooms[i].verts); free(rooms[i].idx); free(face_tex[i]); }
        free(rooms); free(face_tex); free(face_n); free(otex);
        return NULL;
    }

    /* Second pass: now that the object textures are known, give every vertex
       its UV. A vertex shared between faces with different textures gets
       duplicated, so the UVs can't fight over it. */
    int atlas_tiles = (int)ntiles;
    for (int i = 0; i < nrooms; i++) {
        TRRoom *rm = &rooms[i];
        uint32_t ntri = rm->nidx / 3;
        if (ntri == 0) continue;
        G3DVertex *nv = (G3DVertex *)malloc((size_t)rm->nidx * sizeof(G3DVertex));
        uint32_t *ni = (uint32_t *)malloc((size_t)rm->nidx * sizeof(uint32_t));
        if (!nv || !ni) { free(nv); free(ni); continue; }
        uint32_t out = 0, fi = 0, tri_in_face = 0;
        for (uint32_t t = 0; t < ntri; t++) {
            unsigned short tid = (fi < face_n[i]) ? face_tex[i][fi] : 0;
            TRObjTex *ot = (tid < nobjtex) ? &otex[tid] : NULL;
            for (int k = 0; k < 3; k++) {
                G3DVertex v = rm->verts[rm->idx[t * 3 + k]];
                if (ot) {
                    /* A quad became two triangles (a,b,c) and (a,c,d): map each
                       index back to the right corner of the object texture. */
                    int corner = k;
                    if (tri_in_face == 1) corner = (k == 0) ? 0 : (k == 1 ? 2 : 3);
                    float tv = (ot->v[corner] + (float)ot->tile) / (float)atlas_tiles;
                    v.texcoord[0] = ot->u[corner];
                    v.texcoord[1] = tv;
                }
                nv[out] = v;
                ni[out] = out;
                out++;
            }
            /* Advance to the next face: a quad spans two triangles. */
            if (tri_in_face == 0 && fi < face_n[i] && t + 1 < ntri &&
                rm->idx[t * 3] == rm->idx[(t + 1) * 3]) {
                tri_in_face = 1;         /* the second half of this quad */
            } else {
                tri_in_face = 0;
                fi++;
            }
        }
        free(rm->verts); free(rm->idx);
        rm->verts = nv; rm->nverts = out;
        rm->idx = ni; rm->nidx = out;
    }

    /* One submesh per room. */
    G3DMesh *meshes = (G3DMesh *)calloc((size_t)nrooms, sizeof(G3DMesh));
    void **texs = (void **)calloc((size_t)nrooms, sizeof(void *));
    G3DTexture *atlas = tr1_build_atlas(tiles, atlas_tiles, pal);
    int sub = 0;
    for (int i = 0; i < nrooms; i++) {
        TRRoom *rm = &rooms[i];
        if (rm->nidx == 0 || rm->nverts == 0) continue;
        G3DMesh *m = g3d_mesh_create("TRroom", rm->verts, rm->nverts, rm->idx, rm->nidx);
        if (m) { meshes[sub] = *m; free(m); texs[sub] = atlas; sub++; }
    }
    for (int i = 0; i < nrooms; i++) { free(rooms[i].verts); free(rooms[i].idx); free(face_tex[i]); }
    free(rooms); free(face_tex); free(face_n); free(otex);

    if (sub == 0) { free(meshes); free(texs); return NULL; }

    /* TR units are huge (1024 per tile): scale down so the level lands in the
       same ballpark as everything else the engine loads. */
    for (int s = 0; s < sub; s++) {
        for (uint32_t v = 0; v < meshes[s].vertex_count; v++) {
            meshes[s].vertices[v].position[0] /= 512.0f;
            meshes[s].vertices[v].position[1] /= 512.0f;
            meshes[s].vertices[v].position[2] /= 512.0f;
        }
        g3d_mesh_calculate_bounds(&meshes[s]);
        g3d_mesh_upload_gpu(&meshes[s]);
    }

    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));
    if (!model) { free(meshes); free(texs); return NULL; }
    model->meshes = meshes;
    model->mesh_count = (uint32_t)sub;
    model->mesh_textures = texs;
    model->albedo_texture = atlas;
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);
    g3d_model_calculate_bounds(model);

    printf("G3D: TR%d level loaded: %s (%d rooms, %u textiles, atlas %dx%d)\n",
           ver, filepath, sub, ntiles, TEXTILE_W, TEXTILE_H * atlas_tiles);
    return model;
}

/* ---- entry point --------------------------------------------------------- */

G3DModel *g3d_tr_load(const char *filepath) {
    if (!filepath) return NULL;
    FILE *f = fopen(filepath, "rb");
    if (!f) {
        fprintf(stderr, "G3D: TR level not found: %s\n", filepath);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 8) { fclose(f); return NULL; }
    unsigned char *data = (unsigned char *)malloc((size_t)sz);
    if (!data) { fclose(f); return NULL; }
    size_t got = fread(data, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(data); return NULL; }

    TRReader r = { data, (size_t)sz, 0, 0 };
    unsigned int magic = tr_u32(&r);
    int ver = tr_version_of(magic, filepath);

    G3DModel *model = NULL;
    if (ver == G3D_TR1 || ver == G3D_TR2 || ver == G3D_TR3) {
        model = tr123_load(&r, filepath, ver);
    } else if (ver == G3D_TR_UNKNOWN) {
        fprintf(stderr, "G3D: not a Tomb Raider level (magic 0x%08X): %s\n", magic, filepath);
    } else {
        fprintf(stderr, "G3D: TR%d levels aren't supported yet (TR1/2/3 only): %s\n", ver, filepath);
    }
    free(data);
    return model;
}

int g3d_tr_room_count(G3DModel *model) {
    return model ? (int)model->mesh_count : 0;
}
