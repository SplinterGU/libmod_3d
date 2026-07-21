/*
 * libmod_3d_zone.c - Painted movement zones (see libmod_3d_zone.h).
 */
#include "libmod_3d_zone.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>

static unsigned char *g_zone = NULL;
static int   g_zone_side = 0;
static float g_zone_ws   = 0.0f;

/* world (wx,wz) -> integer cell (ix,iz); returns 0 if outside the grid */
static int zone_cell(float wx, float wz, int *ix, int *iz) {
    if (!g_zone || g_zone_side < 2 || g_zone_ws <= 0.0f) return 0;
    int grid = g_zone_side - 1;
    float gx = (wx / g_zone_ws + 0.5f) * (float)grid;
    float gz = (wz / g_zone_ws + 0.5f) * (float)grid;
    if (gx < 0.0f || gz < 0.0f || gx > grid || gz > grid) return 0;
    int cx = (int)(gx + 0.5f), cz = (int)(gz + 0.5f);
    if (cx < 0) cx = 0; if (cx > grid) cx = grid;
    if (cz < 0) cz = 0; if (cz > grid) cz = grid;
    *ix = cx; *iz = cz;
    return 1;
}

void g3d_zone_init(int side, float world_size) {
    if (side < 2) return;
    if (g_zone && g_zone_side == side) { g_zone_ws = world_size; return; }
    free(g_zone);
    g_zone = (unsigned char *)calloc((size_t)side * side, 1);
    g_zone_side = g_zone ? side : 0;
    g_zone_ws = world_size;
}

void g3d_zone_paint(float wx, float wz, float radius, int layer, int on) {
    if (!g_zone || g_zone_side < 2 || layer < 0 || layer > 7) return;
    int grid = g_zone_side - 1;
    unsigned char bit = (unsigned char)(1u << layer);
    /* cell radius in grid units */
    float cell_w = g_zone_ws / (float)grid;
    int r = (int)ceilf(radius / (cell_w > 1e-5f ? cell_w : 1.0f));
    int cix, ciz;
    if (!zone_cell(wx, wz, &cix, &ciz)) {
        /* still allow painting near the centre even if the exact point is edge */
        cix = (int)((wx / g_zone_ws + 0.5f) * grid + 0.5f);
        ciz = (int)((wz / g_zone_ws + 0.5f) * grid + 0.5f);
    }
    for (int iz = ciz - r; iz <= ciz + r; iz++) {
        if (iz < 0 || iz > grid) continue;
        for (int ix = cix - r; ix <= cix + r; ix++) {
            if (ix < 0 || ix > grid) continue;
            float cwx = ((float)ix / grid - 0.5f) * g_zone_ws;
            float cwz = ((float)iz / grid - 0.5f) * g_zone_ws;
            float dx = cwx - wx, dz = cwz - wz;
            if (dx * dx + dz * dz > radius * radius) continue;
            unsigned char *cell = &g_zone[iz * g_zone_side + ix];
            if (on) *cell |= bit; else *cell &= (unsigned char)~bit;
        }
    }
}

int g3d_zone_blocked(float wx, float wz, int layer) {
    int ix, iz;
    if (!zone_cell(wx, wz, &ix, &iz)) return 0;
    unsigned char v = g_zone[iz * g_zone_side + ix];
    if (layer < 0) return v != 0 ? 1 : 0;
    return (v & (unsigned char)(1u << layer)) ? 1 : 0;
}

int g3d_zone_value(float wx, float wz) {
    int ix, iz;
    if (!zone_cell(wx, wz, &ix, &iz)) return 0;
    return (int)g_zone[iz * g_zone_side + ix];
}

int   g3d_zone_side(void)      { return g_zone_side; }
float g3d_zone_worldsize(void) { return g_zone_ws; }
void  g3d_zone_clear(void) {
    if (g_zone) memset(g_zone, 0, (size_t)g_zone_side * g_zone_side);
}

int g3d_zone_save(const char *path) {
    if (!g_zone || g_zone_side < 2 || !path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t side = (uint32_t)g_zone_side;
    fwrite(&side, sizeof(uint32_t), 1, f);
    fwrite(&g_zone_ws, sizeof(float), 1, f);
    fwrite(g_zone, 1, (size_t)g_zone_side * g_zone_side, f);
    fclose(f);
    return 1;
}

int g3d_zone_load(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    uint32_t side = 0; float ws = 0.0f;
    if (fread(&side, sizeof(uint32_t), 1, f) != 1 ||
        fread(&ws,   sizeof(float),   1, f) != 1 || side < 2) { fclose(f); return 0; }
    unsigned char *buf = (unsigned char *)malloc((size_t)side * side);
    if (!buf) { fclose(f); return 0; }
    if (fread(buf, 1, (size_t)side * side, f) != (size_t)side * side) {
        free(buf); fclose(f); return 0;
    }
    fclose(f);
    free(g_zone);
    g_zone = buf; g_zone_side = (int)side; g_zone_ws = ws;
    return 1;
}
