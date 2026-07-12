/*
 * libmod_3d_stream.c - Tile streaming manager (spatial ring logic only).
 * See libmod_3d_stream.h.
 */
#include "libmod_3d_stream.h"
#include <math.h>
#include <string.h>

#define STREAM_MAX_LOADED 8192   /* supports radius up to ~45 tiles */

static struct {
    int   inited;
    float tsize;
    int   radius;
    int   have_prev;             /* a previous camera tile exists */
    int   cx, cz;                /* camera's current tile */

    /* Currently-loaded tiles (the ring). */
    int   lx[STREAM_MAX_LOADED], lz[STREAM_MAX_LOADED];
    int   ln;

    /* This update's deltas (queried by the game). */
    int   load_x[STREAM_MAX_LOADED], load_z[STREAM_MAX_LOADED], load_n;
    int   unl_x[STREAM_MAX_LOADED], unl_z[STREAM_MAX_LOADED], unl_n;
} S;

void g3d_stream_init(float tile_size, int radius) {
    memset(&S, 0, sizeof(S));
    S.tsize  = (tile_size > 1.0f) ? tile_size : 1.0f;
    S.radius = (radius < 0) ? 0 : radius;
    S.inited = 1;
    S.have_prev = 0;
}

/* Is tile (x,z) currently in the loaded ring? */
static int is_loaded(int x, int z) {
    for (int i = 0; i < S.ln; i++)
        if (S.lx[i] == x && S.lz[i] == z) return 1;
    return 0;
}

/* Is tile (x,z) inside the desired ring around the camera tile? */
static int in_ring(int x, int z) {
    int dx = x - S.cx, dz = z - S.cz;
    if (dx < 0) dx = -dx; if (dz < 0) dz = -dz;      /* Chebyshev square */
    return (dx <= S.radius && dz <= S.radius);
}

void g3d_stream_update(float camx, float camz) {
    if (!S.inited) return;
    int ncx = (int)floorf(camx / S.tsize);
    int ncz = (int)floorf(camz / S.tsize);

    S.load_n = 0;
    S.unl_n  = 0;

    /* No change unless the camera crossed into a new tile (or first update). */
    if (S.have_prev && ncx == S.cx && ncz == S.cz) return;
    S.cx = ncx; S.cz = ncz; S.have_prev = 1;

    /* UNLOAD: loaded tiles now outside the ring. */
    int keep_x[STREAM_MAX_LOADED], keep_z[STREAM_MAX_LOADED], keep_n = 0;
    for (int i = 0; i < S.ln; i++) {
        if (in_ring(S.lx[i], S.lz[i])) {
            keep_x[keep_n] = S.lx[i]; keep_z[keep_n] = S.lz[i]; keep_n++;
        } else if (S.unl_n < STREAM_MAX_LOADED) {
            S.unl_x[S.unl_n] = S.lx[i]; S.unl_z[S.unl_n] = S.lz[i]; S.unl_n++;
        }
    }

    /* LOAD: ring tiles not currently loaded. */
    for (int dz = -S.radius; dz <= S.radius; dz++) {
        for (int dx = -S.radius; dx <= S.radius; dx++) {
            int tx = S.cx + dx, tz = S.cz + dz;
            if (!is_loaded(tx, tz) && S.load_n < STREAM_MAX_LOADED) {
                S.load_x[S.load_n] = tx; S.load_z[S.load_n] = tz; S.load_n++;
            }
        }
    }

    /* New loaded set = kept + newly loaded. */
    memcpy(S.lx, keep_x, keep_n * sizeof(int));
    memcpy(S.lz, keep_z, keep_n * sizeof(int));
    S.ln = keep_n;
    for (int i = 0; i < S.load_n && S.ln < STREAM_MAX_LOADED; i++) {
        S.lx[S.ln] = S.load_x[i]; S.lz[S.ln] = S.load_z[i]; S.ln++;
    }
}

int g3d_stream_load_count(void)   { return S.load_n; }
int g3d_stream_load_x(int i)      { return (i >= 0 && i < S.load_n) ? S.load_x[i] : 0; }
int g3d_stream_load_z(int i)      { return (i >= 0 && i < S.load_n) ? S.load_z[i] : 0; }
int g3d_stream_unload_count(void) { return S.unl_n; }
int g3d_stream_unload_x(int i)    { return (i >= 0 && i < S.unl_n) ? S.unl_x[i] : 0; }
int g3d_stream_unload_z(int i)    { return (i >= 0 && i < S.unl_n) ? S.unl_z[i] : 0; }
int g3d_stream_loaded_count(void) { return S.ln; }
void g3d_stream_shutdown(void)    { memset(&S, 0, sizeof(S)); }
