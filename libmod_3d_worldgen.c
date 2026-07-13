/*
 * libmod_3d_worldgen.c - Procedural terrain generation. See the header.
 */
#include "libmod_3d_worldgen.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_material.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_math.h"
#include <stdlib.h>
#include <math.h>

static int   g_seed = 1337;
static float g_amp  = 60.0f;    /* vertical scale */
static float g_freq = 0.0025f;  /* horizontal frequency */
static float g_sea  = 0.0f;     /* sea level (world Y) */
static float g_water_depth = 1.0f;              /* ocean-floor deepening factor */
static void *g_biome_tex[4] = { 0, 0, 0, 0 };   /* sand, grass, rock, snow */
static float g_biome_tex_scale = 0.03f;

void g3d_worldgen_set(int seed, float amplitude, float frequency, float sea_level) {
    g_seed = seed;
    if (amplitude > 0.0f) g_amp = amplitude;
    if (frequency > 0.0f) g_freq = frequency;
    g_sea = sea_level;
}

void g3d_worldgen_set_water_depth(float depth) {
    g_water_depth = (depth > 0.0f) ? depth : 1.0f;
}

void g3d_worldgen_set_biome_textures(void *sand, void *grass, void *rock, void *snow, float scale) {
    g_biome_tex[0] = sand; g_biome_tex[1] = grass;
    g_biome_tex[2] = rock; g_biome_tex[3] = snow;
    if (scale > 0.0f) g_biome_tex_scale = scale;
}

/* Deterministic hash of an integer lattice point -> [0,1). */
static float hash2(int x, int z) {
    unsigned int h = (unsigned int)(x * 374761393) + (unsigned int)(z * 668265263) + (unsigned int)g_seed;
    h = (h ^ (h >> 13)) * 1274126177u;
    h = h ^ (h >> 16);
    return (float)(h & 0x00FFFFFF) / (float)0x01000000;
}
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float valnoise(float x, float z) {
    int xi = (int)floorf(x), zi = (int)floorf(z);
    float xf = x - xi, zf = z - zi;
    float u = xf * xf * (3.0f - 2.0f * xf);
    float v = zf * zf * (3.0f - 2.0f * zf);
    float a = hash2(xi, zi),     b = hash2(xi + 1, zi);
    float c = hash2(xi, zi + 1), d = hash2(xi + 1, zi + 1);
    return lerpf(lerpf(a, b, u), lerpf(c, d, u), v);
}
static float fbm(float x, float z, int oct) {
    float sum = 0.0f, amp = 1.0f, freq = 1.0f, norm = 0.0f;
    for (int i = 0; i < oct; i++) {
        sum += amp * valnoise(x * freq, z * freq);
        norm += amp; amp *= 0.5f; freq *= 2.0f;
    }
    return (norm > 0.0f) ? sum / norm : 0.0f;   /* [0,1] */
}

static float smoothstepf(float e0, float e1, float x) {
    float t = (x - e0) / (e1 - e0);
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    return t * t * (3.0f - 2.0f * t);
}

float g3d_worldgen_height(float wx, float wz) {
    float fx = wx * g_freq, fz = wz * g_freq;

    /* Continent mask (large scale): decides ocean vs land and where mountains
     * cluster. Signed so a good chunk of the world falls below sea level. */
    float cont = fbm(fx * 0.15f + 100.0f, fz * 0.15f + 100.0f, 4) * 2.0f - 1.0f;   /* [-1,1] */

    /* Rolling base hills. */
    float base = fbm(fx, fz, 6) * 2.0f - 1.0f;                                     /* [-1,1] */

    /* Ridged mountains: 1-|2n-1| gives sharp crests; cubing sharpens them into
     * proper ranges instead of round blobs. */
    float rn    = fbm(fx * 0.5f + 50.0f, fz * 0.5f + 50.0f, 5);
    float ridge = 1.0f - fabsf(rn * 2.0f - 1.0f);        /* [0,1], 1 at crests */
    ridge = ridge * ridge * ridge;

    /* Mountains grow inland (where continents are high), not out at sea. */
    float mmask = smoothstepf(0.05f, 0.55f, cont);

    float h = (cont * 0.85f + base * 0.35f) * g_amp;     /* land base + coastline */
    h += ridge * mmask * g_amp * 2.6f;                   /* ranges & volcano peaks */
    if (h < 0.0f) h *= g_water_depth;                    /* deepen ocean basins only */
    return h + g_sea;
}

#ifndef VITA
int g3d_worldgen_tile(int scene_id, int tx, int tz, float tsize, int res,
                      float rox, float roz, void *tex) {
    if (res < 2) res = 2;
    int grid_nv = res * res;
    int nv = grid_nv + 4 * res;        /* + 4 border rings for skirts */
    G3DVertex *verts = (G3DVertex *)malloc((size_t)nv * sizeof(G3DVertex));
    if (!verts) return -1;
    float step = tsize / (float)(res - 1);
    float inv  = 1.0f / (float)(res - 1);

    for (int j = 0; j < res; j++) {
        for (int i = 0; i < res; i++) {
            /* Fractional coords so shared tile edges sample BIT-IDENTICAL world
             * positions: tile (tx) at i=res-1 -> (tx+1)*tsize, and tile (tx+1)
             * at i=0 -> (tx+1)*tsize. Avoids the (res-1)*step != tsize float
             * drift that opened hairline sky-cracks along every tile seam. */
            float fx = (float)i * inv, fz = (float)j * inv;   /* 0..1 exactly at ends */
            float lx = fx * tsize, lz = fz * tsize;           /* local pos in the tile */
            float wx = ((float)tx + fx) * tsize;              /* world pos (for the noise) */
            float wz = ((float)tz + fz) * tsize;
            float h  = g3d_worldgen_height(wx, wz);
            float hl = g3d_worldgen_height(wx - step, wz), hr = g3d_worldgen_height(wx + step, wz);
            float hd = g3d_worldgen_height(wx, wz - step), hu = g3d_worldgen_height(wx, wz + step);
            float nx = hl - hr, ny = 2.0f * step, nz = hd - hu;
            float nl = sqrtf(nx*nx + ny*ny + nz*nz); if (nl < 1e-6f) nl = 1.0f;
            G3DVertex *v = &verts[j * res + i];
            v->position[0] = lx; v->position[1] = h; v->position[2] = lz;
            v->normal[0] = nx/nl; v->normal[1] = ny/nl; v->normal[2] = nz/nl;
            v->texcoord[0] = wx * 0.03f; v->texcoord[1] = wz * 0.03f;   /* world-space, seamless */
        }
    }
    /* Skirts: duplicate each border ring pushed straight DOWN. Any hairline
     * crack between neighbouring tiles (float rasterization drift, or a LOD tile
     * whose border no longer matches its full-res neighbour) then shows this
     * terrain-coloured wall instead of the sky -> the tile grid disappears. */
    float skirt = fmaxf(g_amp * 0.5f, tsize * 0.15f);
    int sk = grid_nv;
    for (int k = 0; k < res; k++) {
        verts[sk +          k] = verts[k];                    verts[sk +          k].position[1] -= skirt; /* bottom j=0    */
        verts[sk +   res  + k] = verts[(res - 1) * res + k];  verts[sk +   res  + k].position[1] -= skirt; /* top    j=res-1*/
        verts[sk + 2*res  + k] = verts[k * res];              verts[sk + 2*res  + k].position[1] -= skirt; /* left   i=0    */
        verts[sk + 3*res  + k] = verts[k * res + (res - 1)];  verts[sk + 3*res  + k].position[1] -= skirt; /* right  i=res-1*/
    }

    uint32_t cells = (uint32_t)(res - 1) * (res - 1);
    uint32_t *idx = (uint32_t *)malloc(((size_t)cells * 6 + (size_t)4 * (res - 1) * 12) * sizeof(uint32_t));
    uint32_t ic = 0;
    for (int j = 0; j < res - 1; j++) {
        for (int i = 0; i < res - 1; i++) {
            uint32_t a = j * res + i, b = a + 1, c = a + res, d = c + 1;
            idx[ic++] = a; idx[ic++] = c; idx[ic++] = b;
            idx[ic++] = b; idx[ic++] = c; idx[ic++] = d;
        }
    }
    /* Skirt walls (double-sided so they show from any viewing angle). */
    int ebase[4]   = { 0, (res - 1) * res, 0, res - 1 };
    int estride[4] = { 1, 1, res, res };
    for (int e = 0; e < 4; e++) {
        int gb = ebase[e], gs = estride[e], skb = sk + e * res;
        for (int k = 0; k < res - 1; k++) {
            uint32_t A = gb + k * gs, B = gb + (k + 1) * gs, SA = skb + k, SB = skb + k + 1;
            idx[ic++] = A; idx[ic++] = B; idx[ic++] = SB;   idx[ic++] = A; idx[ic++] = SB; idx[ic++] = SA;
            idx[ic++] = A; idx[ic++] = SB; idx[ic++] = B;   idx[ic++] = A; idx[ic++] = SA; idx[ic++] = SB;
        }
    }
    G3DMesh *m = g3d_mesh_create("terrtile", verts, (uint32_t)nv, idx, ic);
    free(verts); free(idx);
    if (!m) return -1;
    g3d_mesh_upload_gpu(m);

    int ent = g3d_entity_impl_spawn(scene_id, 0, rox, 0.0f, roz);
    if (ent < 0) { g3d_mesh_free(m); return -1; }
    G3DEntity *e = g3d_entity_impl_get(ent);
    if (e) { e->mesh = m; e->lod_exempt = 1; }   /* keep borders full-res -> no grid cracks */
    int mat = g3d_material_impl_create();
    G3DMaterial *mm = g3d_material_impl_get(mat);
    if (mm) {
        G3DTexture *t = (G3DTexture *)tex;
        if (t && (intptr_t)t != -1) { mm->albedo_texture = t; mm->albedo_texture_id = 0; }
    }
    /* Procedural biomes: colour by height/slope in the shader (sand/grass/rock/
     * snow + lava on volcano peaks). Uses the same amplitude/sea as the noise. */
    g3d_material_impl_set_biome(mat, 1, g_amp, g_sea);
    if (g_biome_tex[0] || g_biome_tex[1] || g_biome_tex[2] || g_biome_tex[3])
        g3d_material_impl_set_biome_textures(mat, g_biome_tex[0], g_biome_tex[1],
                                             g_biome_tex[2], g_biome_tex[3], g_biome_tex_scale);
    g3d_entity_impl_set_material(ent, mat);
    return ent;
}

void g3d_worldgen_tile_free(int entity_id) {
    G3DEntity *e = g3d_entity_impl_get(entity_id);
    if (!e) return;
    G3DMesh *m   = (G3DMesh *)e->mesh;
    int      mat = e->material_id;      /* free it too, or the pool leaks -> white tiles */
    e->mesh = NULL;
    g3d_entity_impl_destroy(entity_id);
    if (m) g3d_mesh_free(m);
    if (mat >= 0) g3d_material_impl_destroy(mat);
}
#else
int  g3d_worldgen_tile(int s, int tx, int tz, float ts, int r, float rox, float roz, void *tex) { return -1; }
void g3d_worldgen_tile_free(int e) {}
#endif
