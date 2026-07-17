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
#include "ufbx.h"          /* reuse ufbx's zlib inflate for TR4/5 (no -lz dep) */
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
    int water;                /* 1 if the room flags mark it as a water room */
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

/* Does `cnt` records of `stride` bytes at `at` look like a real object-texture
   table? Every record must name an existing textile AND the block must not be a
   run of zeros: tile==0 passes "tile < ntiles", so a zero-filled region used to
   be accepted as the table, giving UVs of (0,0) -> the whole level sampled the
   atlas' top-left texel and rendered as one flat colour. */
static int tr_objtex_valid(const unsigned char *d, size_t size, size_t at,
                           unsigned int cnt, int stride, unsigned int ntiles) {
    if (cnt < 1 || cnt > 40000) return 0;
    if (at + (size_t)cnt * (size_t)stride > size) return 0;
    int checks = cnt < 32 ? (int)cnt : 32, nonzero = 0;
    for (int i = 0; i < checks; i++) {
        const unsigned char *e = d + at + (size_t)i * (size_t)stride;
        unsigned short tile = (unsigned short)(e[2] | (e[3] << 8)) & 0x7FFF;
        if (tile >= ntiles) return 0;
        for (int k = 4; k < stride; k++) if (e[k]) { nonzero = 1; break; }
    }
    return nonzero;
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

/* Parse the rooms (the reader must be positioned at numRooms) and build a
   G3DModel. Shared by TR1-3 (reading the level file directly) and TR4 (reading
   the decompressed level-data blob). The atlas is either built here from 8-bit
   textiles + palette (TR1-3), or passed in already built from the 32-bit
   textiles (TR4, prebuilt_atlas). obj_stride is the object-texture record size:
   20 bytes in TR1-3, 38 in TR4. */
static G3DModel *tr_build_rooms(TRReader *r, const char *filepath, int ver,
                                const unsigned char *tiles, unsigned int ntiles,
                                const unsigned char *pal,
                                G3DTexture *prebuilt_atlas, int obj_stride) {
    /* Per-version room layout differences, all verified against real levels. */
    int vert_sz   = (ver == G3D_TR1) ? 8  : 12;
    int light_sz  = (ver == G3D_TR1) ? 18 : ((ver == G3D_TR4) ? 46 : 24);
    int static_sz = (ver == G3D_TR1) ? 18 : 20;
    /* Bytes between the ambient intensity and the light list. */
    int after_amb = (ver == G3D_TR1) ? 0  : ((ver == G3D_TR2) ? 4 : 2);
    int room_tail = (ver == G3D_TR3 || ver == G3D_TR4) ? 3 : 0;

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
        tr_skip(r, 2);                   /* alternate room (i16) */
        unsigned short rflags = tr_u16(r);
        rm->water = (rflags & 0x0001) ? 1 : 0;   /* bit 0 = water room */
        tr_skip(r, (size_t)room_tail);   /* TR3/4: waterScheme, reverbInfo, filler */
        if (r->overrun) break;
    }
    size_t rooms_end = r->at;

    /* Find the object texture array by CONTENT rather than tracking every block
       between here and it: the intermediate structures drift across TR1-5 (TR3
       alone slips an undocumented ~41KB block in). Scan forward from the end of
       the rooms for a count whose entries all reference valid textiles; a wrong
       offset reads garbage tile ids and is rejected, so it's self-correcting.
       obj_stride is the record size (20 in TR1-3, 38 in TR4). */
    unsigned int nobjtex = 0;
    size_t obj_at = 0;
    if (ver == G3D_TR4) {
        /* TR4's mid-file structures changed too much for a content scan (a big
           run of small-valued zone/overlap data passes any loose validator), so
           walk them exactly. The object textures sit behind a "TEX" marker after
           the animated textures. Sizes verified against a real level. */
        r->at = rooms_end;
        unsigned int n;
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* floor data */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* mesh data */
        n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh pointers */
        n = tr_u32(r); tr_skip(r, (size_t)n * 40);     /* animations (TR4: 40 B) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 6);      /* state changes */
        n = tr_u32(r); tr_skip(r, (size_t)n * 8);      /* anim dispatches */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* anim commands */
        n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh trees */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* frames */
        n = tr_u32(r); tr_skip(r, (size_t)n * 18);     /* moveables */
        n = tr_u32(r); tr_skip(r, (size_t)n * 32);     /* static meshes */
        tr_skip(r, 3);                                 /* "SPR" marker */
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* sprite textures */
        n = tr_u32(r); tr_skip(r, (size_t)n * 8);      /* sprite sequences */
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* cameras */
        n = tr_u32(r); tr_skip(r, (size_t)n * 40);     /* flyby cameras (TR4) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 16);     /* sound sources */
        unsigned int nbox = tr_u32(r); tr_skip(r, (size_t)nbox * 8);  /* boxes (TR4: 8) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* overlaps */
        tr_skip(r, (size_t)nbox * 20);                 /* zones (TR4) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* animated textures */
        tr_skip(r, 1);                                 /* animated-texture UV count */
        tr_skip(r, 3);                                 /* "TEX" marker */
        nobjtex = tr_u32(r);
        obj_at = r->at;
        if (r->overrun || nobjtex == 0 || nobjtex > 40000) nobjtex = 0;
    } else {
        /* TR1-3: WALK the blocks exactly (same approach as TR4). El content-scan
           que habia aqui se enganchaba a zonas de CEROS: su unico test era
           tile < ntiles, y tile=0 lo pasa, asi que un bloque de ceros valia como
           tabla de object textures -> UVs a cero -> todas las caras muestreaban
           la esquina (0,0) del atlas y el nivel salia de un color plano (verde en
           Lara's Home). Si el recorrido no cuadra (bloques no documentados), se
           cae al escaneo, pero ya con un validador que rechaza los ceros. */
        r->at = rooms_end;
        unsigned int n;
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* floor data */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* mesh data */
        n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh pointers */
        n = tr_u32(r); tr_skip(r, (size_t)n * 32);     /* animations (TR1-3: 32 B) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 6);      /* state changes */
        n = tr_u32(r); tr_skip(r, (size_t)n * 8);      /* anim dispatches (8 B!) */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* anim commands */
        n = tr_u32(r); tr_skip(r, (size_t)n * 4);      /* mesh trees */
        n = tr_u32(r); tr_skip(r, (size_t)n * 2);      /* frames */
        n = tr_u32(r); tr_skip(r, (size_t)n * 18);     /* models */
        n = tr_u32(r); tr_skip(r, (size_t)n * 32);     /* static meshes */
        unsigned int cnt = tr_u32(r);
        size_t at = r->at;
        if (!r->overrun && tr_objtex_valid(r->d, r->size, at, cnt, obj_stride, ntiles)) {
            nobjtex = cnt; obj_at = at;
        } else {
            for (size_t scan = rooms_end; scan + 4 + (size_t)obj_stride * 32 <= r->size; scan += 2) {
                unsigned int c2 = (unsigned int)r->d[scan] | ((unsigned int)r->d[scan+1] << 8) |
                                  ((unsigned int)r->d[scan+2] << 16) | ((unsigned int)r->d[scan+3] << 24);
                if (c2 < 16) continue;
                if (tr_objtex_valid(r->d, r->size, scan + 4, c2, obj_stride, ntiles)) {
                    nobjtex = c2; obj_at = scan + 4; break;
                }
            }
        }
    }

    /* TR4 object textures put the UVs 4 bytes later (an extra u16 NewFlags after
       attribute+tile) and each coord is a byte pair (whole, fraction). */
    int uv_off = (ver == G3D_TR4) ? 6 : 4;
    TRObjTex *otex = NULL;
    if (nobjtex > 0) {
        otex = (TRObjTex *)calloc(nobjtex, sizeof(TRObjTex));
        for (unsigned int i = 0; i < nobjtex; i++) {
            const unsigned char *e = r->d + obj_at + (size_t)i * obj_stride;
            unsigned short tile = (unsigned short)(e[2] | (e[3] << 8));
            otex[i].tile = tile & 0x7FFF;
            for (int c = 0; c < 4; c++) {
                const unsigned char *uv = e + uv_off + c * 4;
                /* whole part in the high byte, low byte nudges inside the texel */
                otex[i].u[c] = (float)uv[1] / (float)TEXTILE_W;
                otex[i].v[c] = (float)uv[3] / (float)TEXTILE_H;
            }
        }
    }

    /* TR1's palette is at the very END of the file, so keep walking to reach it.
       TR2/3 gave us the palette up top; TR4 uses 32-bit textiles (no palette). */
    if (ver == G3D_TR1) {
        unsigned int n;
        r->at = rooms_end;
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
        n = tr_u32(r); tr_skip(r, (size_t)n * 20);     /* object textures */
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

    /* Need the object textures always; the palette only when we build the atlas
       here (TR1-3). TR4 has no palette - it comes in with its 32-bit atlas. */
    if (!otex || (!pal && !prebuilt_atlas)) {
        fprintf(stderr, "G3D: TR%d parse failed (otex=%p pal=%p): %s\n",
                ver, (void *)otex, (void *)pal, filepath);
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

    /* --- Quitar la LAMINA de agua del nivel (fea y estatica; la sustituye nuestra
       agua animada) y de paso hacer que Lara CAIGA en la piscina. La lamina puede
       estar en el room de agua O en el room SECO de encima, asi que: (1) recogemos
       la superficie de cada room de agua (su Y y su huella X/Z); (2) borramos las
       caras HORIZONTALES a esa Y que caen sobre esa huella, en CUALQUIER room. Al
       no estar ya en la malla, dejan de verse Y dejan de ser colision -> cae. --- */
    {
        float *wy  = (float *)malloc((size_t)nrooms * sizeof(float));
        float *wx0 = (float *)malloc((size_t)nrooms * sizeof(float));
        float *wx1 = (float *)malloc((size_t)nrooms * sizeof(float));
        float *wz0 = (float *)malloc((size_t)nrooms * sizeof(float));
        float *wz1 = (float *)malloc((size_t)nrooms * sizeof(float));
        int nws = 0;
        if (wy && wx0 && wx1 && wz0 && wz1) {
            for (int i = 0; i < nrooms; i++) {
                TRRoom *rm = &rooms[i];
                if (!rm->water || rm->nverts < 3) continue;
                float maxy = rm->verts[0].position[1];
                float mnx = rm->verts[0].position[0], mxx = mnx;
                float mnz = rm->verts[0].position[2], mxz = mnz;
                for (uint32_t v = 1; v < rm->nverts; v++) {
                    float *p = rm->verts[v].position;
                    if (p[1] > maxy) maxy = p[1];
                    if (p[0] < mnx) mnx = p[0]; if (p[0] > mxx) mxx = p[0];
                    if (p[2] < mnz) mnz = p[2]; if (p[2] > mxz) mxz = p[2];
                }
                wy[nws] = maxy; wx0[nws] = mnx; wx1[nws] = mxx;
                wz0[nws] = mnz; wz1[nws] = mxz; nws++;
            }
            for (int i = 0; i < nrooms; i++) {
                TRRoom *rm = &rooms[i];
                if (rm->nverts < 3) continue;
                uint32_t tris = rm->nverts / 3, ow = 0;
                for (uint32_t tt = 0; tt < tris; tt++) {
                    float *a = rm->verts[tt*3+0].position;
                    float *b = rm->verts[tt*3+1].position;
                    float *c = rm->verts[tt*3+2].position;
                    float d01 = a[1]-b[1], d12 = b[1]-c[1];
                    int surface = 0;
                    if (d01 < 8.0f && d01 > -8.0f && d12 < 8.0f && d12 > -8.0f) {   /* horizontal */
                        float fcx = (a[0]+b[0]+c[0]) / 3.0f, fcz = (a[2]+b[2]+c[2]) / 3.0f;
                        for (int w = 0; w < nws; w++) {
                            float dy = a[1] - wy[w];
                            if (dy < 8.0f && dy > -8.0f &&
                                fcx >= wx0[w]-4.0f && fcx <= wx1[w]+4.0f &&
                                fcz >= wz0[w]-4.0f && fcz <= wz1[w]+4.0f) { surface = 1; break; }
                        }
                    }
                    if (surface) continue;
                    for (int k = 0; k < 3; k++) { rm->verts[ow] = rm->verts[tt*3+k]; rm->idx[ow] = ow; ow++; }
                }
                rm->nverts = ow; rm->nidx = ow;
            }
        }
        free(wy); free(wx0); free(wx1); free(wz0); free(wz1);
    }

    /* One submesh per room. */
    G3DMesh *meshes = (G3DMesh *)calloc((size_t)nrooms, sizeof(G3DMesh));
    void **texs = (void **)calloc((size_t)nrooms, sizeof(void *));
    /* TR1-3 build the atlas here from 8-bit textiles + palette; TR4 passes its
       32-bit atlas in already built. */
    G3DTexture *atlas = prebuilt_atlas ? prebuilt_atlas
                                       : tr1_build_atlas(tiles, atlas_tiles, pal);
    unsigned char *swater = (unsigned char *)calloc((size_t)nrooms, 1);
    int sub = 0;
    for (int i = 0; i < nrooms; i++) {
        TRRoom *rm = &rooms[i];
        if (rm->nidx == 0 || rm->nverts == 0) continue;
        G3DMesh *m = g3d_mesh_create("TRroom", rm->verts, rm->nverts, rm->idx, rm->nidx);
        if (m) { meshes[sub] = *m; free(m); texs[sub] = atlas;
                 if (swater) swater[sub] = (unsigned char)rm->water; sub++; }
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
    model->submesh_water = swater;
    model->albedo_texture = atlas;
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);
    g3d_model_calculate_bounds(model);

    printf("G3D: TR%d level loaded: %s (%d rooms, %u textiles, atlas %dx%d)\n",
           ver, filepath, sub, ntiles, TEXTILE_W, TEXTILE_H * atlas_tiles);
    return model;
}

/* ---- TR1-3: read the header, then hand the rooms to the shared builder ---- */

static G3DModel *tr123_load(TRReader *r, const char *filepath, int ver) {
    const unsigned char *pal = NULL;
    if (ver != G3D_TR1) {
        pal = r->d + r->at;              /* TR2/3: palette right after the version */
        tr_skip(r, 768);
        tr_skip(r, 1024);                /* 16-bit palette (unused) */
    }
    unsigned int ntiles = tr_u32(r);
    if (r->overrun || ntiles == 0 || ntiles > 64) return NULL;
    const unsigned char *tiles = r->d + r->at;
    tr_skip(r, (size_t)ntiles * TEXTILE_W * TEXTILE_H);
    if (ver != G3D_TR1)
        tr_skip(r, (size_t)ntiles * TEXTILE_W * TEXTILE_H * 2);   /* 16-bit set */
    tr_skip(r, 4);                       /* unused */
    /* reader now at numRooms */
    return tr_build_rooms(r, filepath, ver, tiles, ntiles, pal, NULL, 20);
}

/* ---- TR4: zlib-compressed textiles + a compressed level-data blob --------- */

/* Inflate a zlib stream with ufbx's decompressor (avoids a libz dependency on
   every platform). Returns a malloc'd buffer of exactly out_size, or NULL. */
static unsigned char *tr_inflate(const unsigned char *src, size_t src_size,
                                 size_t out_size) {
    unsigned char *out = (unsigned char *)malloc(out_size ? out_size : 1);
    if (!out) return NULL;
    ufbx_inflate_input in;
    memset(&in, 0, sizeof(in));
    in.total_size = src_size;
    in.data = src;
    in.data_size = src_size;
    ufbx_inflate_retain retain;
    retain.initialized = false;
    ptrdiff_t got = ufbx_inflate(out, out_size, &in, &retain);
    if (got < 0 || (size_t)got != out_size) { free(out); return NULL; }
    return out;
}

/* Build the 32-bit atlas from TR4's Textile32 (BGRA tiles). */
static G3DTexture *tr4_build_atlas(const unsigned char *t32, int ntiles) {
    if (ntiles <= 0) return NULL;
    int w = TEXTILE_W, h = TEXTILE_H * ntiles;
    unsigned char *rgba = (unsigned char *)malloc((size_t)w * h * 4);
    if (!rgba) return NULL;
    size_t px = (size_t)w * h;
    for (size_t i = 0; i < px; i++) {
        /* Textile32 is BGRA; swap to RGBA. */
        rgba[i*4+0] = t32[i*4+2];
        rgba[i*4+1] = t32[i*4+1];
        rgba[i*4+2] = t32[i*4+0];
        rgba[i*4+3] = t32[i*4+3];
    }
    G3DTexture *tex = (G3DTexture *)calloc(1, sizeof(G3DTexture));
    if (!tex) { free(rgba); return NULL; }
    tex->width = w; tex->height = h; tex->channels = 4;
    tex->data = rgba; tex->data_size = (size_t)w * h * 4;
    g3d_texture_upload_gpu(tex);
    return tex;
}

static G3DModel *tr4_load(TRReader *r, const char *filepath) {
    /* Header: numRoomTextiles, numObjTextiles, numBumpTextiles (u16 x3). */
    unsigned int nroom = tr_u16(r), nobj = tr_u16(r), nbump = tr_u16(r);
    unsigned int ntiles = nroom + nobj + nbump;
    if (r->overrun || ntiles == 0 || ntiles > 4096) return NULL;

    /* Textile32: uncompressed size, compressed size, then the zlib blob. */
    unsigned int t32_unc = tr_u32(r), t32_comp = tr_u32(r);
    const unsigned char *t32_src = r->d + r->at;
    tr_skip(r, t32_comp);
    /* Textile16 and the font/sky misc block: skip (we texture from the 32-bit). */
    unsigned int u, c;
    u = tr_u32(r); c = tr_u32(r); tr_skip(r, c);      /* Textile16 */
    u = tr_u32(r); c = tr_u32(r); tr_skip(r, c);      /* Misc (font+sky) */
    /* Level data: the whole level (rooms, meshes, ...) as one zlib blob. */
    unsigned int lvl_unc = tr_u32(r), lvl_comp = tr_u32(r);
    const unsigned char *lvl_src = r->d + r->at;
    (void)u;
    if (r->overrun) return NULL;

    unsigned char *t32 = tr_inflate(t32_src, t32_comp, t32_unc);
    unsigned char *lvl = tr_inflate(lvl_src, lvl_comp, lvl_unc);
    if (!t32 || !lvl) {
        fprintf(stderr, "G3D: TR4 zlib inflate failed: %s\n", filepath);
        free(t32); free(lvl); return NULL;
    }

    G3DTexture *atlas = tr4_build_atlas(t32, (int)ntiles);
    free(t32);

    /* Parse the rooms out of the decompressed level data. It starts with an
       unused u32, then numRooms. */
    TRReader lr = { lvl, lvl_unc, 4 /* skip unused */, 0 };
    G3DModel *model = tr_build_rooms(&lr, filepath, G3D_TR4, NULL, ntiles, NULL, atlas, 38);
    free(lvl);
    return model;
}

/* ---- TR5: uncompressed level data, XELA-chained rooms, float vertices ------ */

/* Read the object textures from a TR5 level-data buffer: they sit behind a
   "TEX\0" marker, 40 bytes each (TR4's 38 + 2). Returns a malloc'd array. */
static TRObjTex *tr5_object_textures(const unsigned char *lvl, size_t n,
                                     unsigned int ntiles, unsigned int *out_count) {
    *out_count = 0;
    for (size_t i = 0; i + 8 < n; i++) {
        if (lvl[i]=='T' && lvl[i+1]=='E' && lvl[i+2]=='X' && lvl[i+3]==0) {
            size_t off = i + 4;
            unsigned int cnt = (unsigned int)lvl[off] | ((unsigned int)lvl[off+1]<<8) |
                               ((unsigned int)lvl[off+2]<<16) | ((unsigned int)lvl[off+3]<<24);
            if (cnt < 16 || cnt > 40000 || off + 4 + (size_t)cnt * 40 > n) continue;
            /* validate the first entries reference real textiles */
            int ok = 1, checks = cnt < 32 ? (int)cnt : 32;
            for (int k = 0; k < checks; k++) {
                const unsigned char *e = lvl + off + 4 + (size_t)k * 40;
                unsigned short tile = (unsigned short)(e[2] | (e[3]<<8)) & 0x7FFF;
                if (tile >= ntiles) { ok = 0; break; }
            }
            if (!ok) continue;
            TRObjTex *ot = (TRObjTex *)calloc(cnt, sizeof(TRObjTex));
            if (!ot) return NULL;
            for (unsigned int k = 0; k < cnt; k++) {
                const unsigned char *e = lvl + off + 4 + (size_t)k * 40;
                ot[k].tile = (unsigned short)(e[2] | (e[3]<<8)) & 0x7FFF;
                for (int c = 0; c < 4; c++) {
                    const unsigned char *uv = e + 6 + c * 4;   /* attr,tile,newflags then UVs */
                    ot[k].u[c] = (float)uv[1] / (float)TEXTILE_W;
                    ot[k].v[c] = (float)uv[3] / (float)TEXTILE_H;
                }
            }
            *out_count = cnt;
            return ot;
        }
    }
    return NULL;
}

static G3DModel *tr5_load(TRReader *r, const char *filepath) {
    unsigned int nroom = tr_u16(r), nobj = tr_u16(r), nbump = tr_u16(r);
    unsigned int ntiles = nroom + nobj + nbump;
    if (r->overrun || ntiles == 0 || ntiles > 4096) return NULL;

    unsigned int t32_unc = tr_u32(r), t32_comp = tr_u32(r);
    const unsigned char *t32_src = r->d + r->at; tr_skip(r, t32_comp);
    unsigned int u, c;
    u = tr_u32(r); c = tr_u32(r); tr_skip(r, c);      /* Textile16 */
    u = tr_u32(r); c = tr_u32(r); tr_skip(r, c);      /* Misc */
    tr_skip(r, 32);   /* LaraType(u16) + WeatherType(u16) + Filler[28] */
    /* Level data: NOT compressed in TR5 (uncompressed == compressed size). */
    unsigned int lvl_unc = tr_u32(r), lvl_comp = tr_u32(r);
    const unsigned char *lvl = r->d + r->at;
    (void)u; (void)lvl_comp;
    if (r->overrun || lvl_unc < 8 || r->at + lvl_unc > r->size) return NULL;
    size_t N = lvl_unc;

    unsigned char *t32 = tr_inflate(t32_src, t32_comp, t32_unc);
    if (!t32) { fprintf(stderr, "G3D: TR5 textile inflate failed: %s\n", filepath); return NULL; }
    G3DTexture *atlas = tr4_build_atlas(t32, (int)ntiles);
    free(t32);

    unsigned int nobjtex = 0;
    TRObjTex *otex = tr5_object_textures(lvl, N, ntiles, &nobjtex);
    if (!otex) { fprintf(stderr, "G3D: TR5 object textures not found: %s\n", filepath); return NULL; }

    unsigned int nrooms = (unsigned int)lvl[4] | ((unsigned int)lvl[5]<<8) |
                          ((unsigned int)lvl[6]<<16) | ((unsigned int)lvl[7]<<24);
    if (nrooms == 0 || nrooms > 5000) { free(otex); return NULL; }

    G3DMesh *meshes = (G3DMesh *)calloc(nrooms, sizeof(G3DMesh));
    void **texs = (void **)calloc(nrooms, sizeof(void *));
    int sub = 0;
    size_t ro = 8;   /* first room (after unused u32 + numRooms u32) */

    for (unsigned int rm = 0; rm < nrooms && ro + 8 <= N; rm++) {
        if (!(lvl[ro]=='X'&&lvl[ro+1]=='E'&&lvl[ro+2]=='L'&&lvl[ro+3]=='A')) break;
        unsigned int rds = (unsigned int)lvl[ro+4] | ((unsigned int)lvl[ro+5]<<8) |
                           ((unsigned int)lvl[ro+6]<<16) | ((unsigned int)lvl[ro+7]<<24);
        size_t base = ro + 8, rend = base + rds;
        if (rend > N) break;
        int rx = 0, rz = 0;
        if (base + 32 <= N) {
            rx = (int)((unsigned int)lvl[base+20] | ((unsigned int)lvl[base+21]<<8) |
                       ((unsigned int)lvl[base+22]<<16) | ((unsigned int)lvl[base+23]<<24));
            rz = (int)((unsigned int)lvl[base+28] | ((unsigned int)lvl[base+29]<<8) |
                       ((unsigned int)lvl[base+30]<<16) | ((unsigned int)lvl[base+31]<<24));
        }

        /* TR5 room header (204 bytes) tells us exactly where the geometry is,
           via TRosettaStone. The offset fields need +208 to become room-data
           relative (calibrated against real vertex/poly positions - the docs say
           +216 but +208 is what actually lands on the data here). Indices in the
           polygons are LAYER-relative, so each layer's polys index into that
           layer's own vertex block. */
        #define RDU16(x) ((unsigned)(lvl[(x)] | (lvl[(x)+1]<<8)))
        #define RDU32(x) ((unsigned)(lvl[(x)] | (lvl[(x)+1]<<8) | (lvl[(x)+2]<<16) | ((unsigned)lvl[(x)+3]<<24)))
        if (base + 204 > rend) { ro = rend; continue; }
        /* Field offsets measured against the real file (4 past the published
           layout): NumLayers @168, LayerOffset @172, VerticesOffset @176,
           PolyOffset @180. */
        unsigned num_layers = RDU32(base + 168);
        size_t K = 208;
        size_t lay_at  = base + RDU32(base + 172) + K;
        size_t vert_at = base + RDU32(base + 176) + K;
        size_t poly_at = base + RDU32(base + 180) + K;
        if (num_layers == 0 || num_layers > 2048 ||
            lay_at + (size_t)num_layers * 56 > rend || vert_at > rend || poly_at > rend) {
            ro = rend; continue;
        }

        uint32_t fcap = 256, o2 = 0;
        G3DVertex *fv = (G3DVertex *)malloc(fcap * sizeof(G3DVertex));
        #define EMIT(vx, ot, corner) do { \
            if (o2 >= fcap) { fcap *= 2; fv = realloc(fv, fcap * sizeof(G3DVertex)); } \
            G3DVertex _v = (vx); \
            if (ot) { _v.texcoord[0] = (ot)->u[corner]; \
                      _v.texcoord[1] = ((ot)->v[corner] + (float)(ot)->tile) / (float)ntiles; } \
            fv[o2++] = _v; } while (0)

        size_t vcur = vert_at, pcur = poly_at;
        for (unsigned L = 0; L < num_layers; L++) {
            size_t lb = lay_at + (size_t)L * 56;
            unsigned nlv = RDU16(lb + 0);       /* NumLayerVertices */
            unsigned nlr = RDU16(lb + 6);       /* NumLayerRectangles */
            unsigned nlt = RDU16(lb + 8);       /* NumLayerTriangles */
            if (vcur + (size_t)nlv * 28 > rend) break;

            /* This layer's vertices, room-local -> world (TR Y points down). */
            G3DVertex *lv = (G3DVertex *)calloc(nlv ? nlv : 1, sizeof(G3DVertex));
            for (unsigned v = 0; v < nlv; v++) {
                float px, py, pz;
                memcpy(&px, lvl+vcur+(size_t)v*28, 4);
                memcpy(&py, lvl+vcur+(size_t)v*28+4, 4);
                memcpy(&pz, lvl+vcur+(size_t)v*28+8, 4);
                lv[v].position[0] = (float)rx + px;
                lv[v].position[1] = -py;
                lv[v].position[2] = (float)rz + pz;
                lv[v].normal[1] = 1.0f;
            }
            vcur += (size_t)nlv * 28;

            for (unsigned q = 0; q < nlr && pcur + 12 <= rend; q++, pcur += 12) {
                unsigned a=RDU16(pcur), b=RDU16(pcur+2), cc=RDU16(pcur+4), dd=RDU16(pcur+6);
                unsigned t=RDU16(pcur+8)&0x7FFF;
                if (a>=nlv||b>=nlv||cc>=nlv||dd>=nlv) continue;
                TRObjTex *ot = (t < nobjtex) ? &otex[t] : NULL;
                EMIT(lv[a], ot, 0); EMIT(lv[b], ot, 1); EMIT(lv[cc], ot, 2);
                EMIT(lv[a], ot, 0); EMIT(lv[cc], ot, 2); EMIT(lv[dd], ot, 3);
            }
            for (unsigned q = 0; q < nlt && pcur + 10 <= rend; q++, pcur += 10) {
                unsigned a=RDU16(pcur), b=RDU16(pcur+2), cc=RDU16(pcur+4);
                unsigned t=RDU16(pcur+6)&0x7FFF;
                if (a>=nlv||b>=nlv||cc>=nlv) continue;
                TRObjTex *ot = (t < nobjtex) ? &otex[t] : NULL;
                EMIT(lv[a], ot, 0); EMIT(lv[b], ot, 1); EMIT(lv[cc], ot, 2);
            }
            free(lv);
        }
        #undef EMIT
        #undef RDU16
        #undef RDU32
        if (o2 < 3) { free(fv); ro = rend; continue; }

        uint32_t *fi = (uint32_t *)malloc((size_t)o2 * sizeof(uint32_t));
        for (uint32_t v = 0; v < o2; v++) {
            fi[v] = v;
            fv[v].position[0] /= 512.0f; fv[v].position[1] /= 512.0f; fv[v].position[2] /= 512.0f;
        }
        G3DMesh *m = g3d_mesh_create("TR5room", fv, o2, fi, o2);
        free(fv); free(fi);
        if (m) { g3d_mesh_upload_gpu(m); meshes[sub] = *m; free(m); texs[sub] = atlas; sub++; }

        ro = rend;
    }
    free(otex);

    if (sub == 0) { free(meshes); free(texs); return NULL; }
    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));
    if (!model) { free(meshes); free(texs); return NULL; }
    model->meshes = meshes; model->mesh_count = (uint32_t)sub;
    model->mesh_textures = texs; model->albedo_texture = atlas;
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);
    g3d_model_calculate_bounds(model);
    printf("G3D: TR5 level loaded: %s (%d rooms, %u textiles, %u object textures)\n",
           filepath, sub, ntiles, nobjtex);
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
    } else if (ver == G3D_TR4) {
        model = tr4_load(&r, filepath);
    } else if (ver == G3D_TR5) {
        model = tr5_load(&r, filepath);
    } else {
        fprintf(stderr, "G3D: not a Tomb Raider level (magic 0x%08X): %s\n", magic, filepath);
    }
    free(data);
    return model;
}

int g3d_tr_room_count(G3DModel *model) {
    return model ? (int)model->mesh_count : 0;
}
