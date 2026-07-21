/*
 * libmod_3d_terrain.c - Runtime terrain editing (sculpting brushes)
 */

#include "libmod_3d_terrain.h"
#include "libmod_3d_primitives.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* ---- helpers ----------------------------------------------------------- */

static int terrain_valid(G3DMesh *m) {
    return m && m->terrain_side > 1 && m->vertices &&
           m->vertex_count == (uint32_t)(m->terrain_side * m->terrain_side);
}

/* World X/Z -> fractional grid coordinate (0 .. side-1) */
static void world_to_grid(G3DMesh *m, float wx, float wz, float *gx, float *gz) {
    int grid = m->terrain_side - 1;
    float ws = m->terrain_world_size;
    *gx = (wx / ws + 0.5f) * (float)grid;
    *gz = (wz / ws + 0.5f) * (float)grid;
}

/* ---- height query ------------------------------------------------------ */

float g3d_terrain_get_height(G3DMesh *m, float wx, float wz) {
    if (!terrain_valid(m))
        return 0.0f;

    int side = m->terrain_side;
    float gx, gz;
    world_to_grid(m, wx, wz, &gx, &gz);

    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > side - 1) gx = side - 1;
    if (gz > side - 1) gz = side - 1;

    int ix = (int)floorf(gx);
    int iz = (int)floorf(gz);
    int ix1 = (ix < side - 1) ? ix + 1 : ix;
    int iz1 = (iz < side - 1) ? iz + 1 : iz;
    float fx = gx - (float)ix;
    float fz = gz - (float)iz;

    float h00 = m->vertices[iz * side + ix].position[1];
    float h10 = m->vertices[iz * side + ix1].position[1];
    float h01 = m->vertices[iz1 * side + ix].position[1];
    float h11 = m->vertices[iz1 * side + ix1].position[1];

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fz;
}

/* ============================================================================
   TERRAIN HOLES  (drop grid cells from the EBO so you can see through the
   ground -- e.g. into a voxel cave below). The vertices stay; only the index
   buffer changes, so it's cheap and reversible.
   ============================================================================ */

/* Rebuild the EBO from the grid, skipping cells flagged as holes. */
static void terrain_rebuild_indices(G3DMesh *m) {
    int side = m->terrain_side, grid = side - 1;
    uint32_t k = 0;
    for (int j = 0; j < grid; j++)
        for (int i = 0; i < grid; i++) {
            if (m->terrain_holes && m->terrain_holes[j * grid + i]) continue;
            uint32_t a = (uint32_t)(j * side + i);
            uint32_t b = (uint32_t)(j * side + i + 1);
            uint32_t c = (uint32_t)((j + 1) * side + i);
            uint32_t d = (uint32_t)((j + 1) * side + i + 1);
            m->indices[k++] = a; m->indices[k++] = c; m->indices[k++] = b;
            m->indices[k++] = b; m->indices[k++] = c; m->indices[k++] = d;
        }
    m->index_count = k;
    g3d_mesh_update_indices_gpu(m);
}

int g3d_terrain_set_hole(G3DMesh *m, float wx, float wz, float radius, int on) {
    if (!terrain_valid(m)) return 0;
    int side = m->terrain_side, grid = side - 1;
    if (grid < 1) return 0;
    if (!m->terrain_holes) {
        m->terrain_holes = (unsigned char *)calloc((size_t)grid * grid, 1);
        if (!m->terrain_holes) return 0;
    }
    float W = m->terrain_world_size, cell = W / (float)grid;
    float gx, gz; world_to_grid(m, wx, wz, &gx, &gz);
    int rc = (int)(radius / cell) + 1;
    int ci = (int)floorf(gx), cj = (int)floorf(gz);
    int changed = 0;
    for (int j = cj - rc; j <= cj + rc; j++)
        for (int i = ci - rc; i <= ci + rc; i++) {
            if (i < 0 || i >= grid || j < 0 || j >= grid) continue;
            float cxw = (((float)i + 0.5f) / grid - 0.5f) * W;   /* centro de celda */
            float czw = (((float)j + 0.5f) / grid - 0.5f) * W;
            float dx = cxw - wx, dz = czw - wz;
            if (dx*dx + dz*dz <= radius * radius) {
                unsigned char v = on ? 1 : 0;
                if (m->terrain_holes[j * grid + i] != v) { m->terrain_holes[j * grid + i] = v; changed = 1; }
            }
        }
    if (changed) terrain_rebuild_indices(m);
    return changed;
}

/* Is there a hole at world (wx,wz)? (for placement/collision to skip holes). */
int g3d_terrain_is_hole(G3DMesh *m, float wx, float wz) {
    if (!terrain_valid(m) || !m->terrain_holes) return 0;
    int side = m->terrain_side, grid = side - 1;
    float gx, gz; world_to_grid(m, wx, wz, &gx, &gz);
    int i = (int)floorf(gx), j = (int)floorf(gz);
    if (i < 0 || i >= grid || j < 0 || j >= grid) return 0;
    return m->terrain_holes[j * grid + i] ? 1 : 0;
}

/* ---- normals + GPU upload ---------------------------------------------- */

static void terrain_recompute_normals(G3DMesh *m) {
    int side = m->terrain_side;
    int grid = side - 1;
    float cell = m->terrain_world_size / (float)grid;

    for (int j = 0; j < side; j++) {
        for (int i = 0; i < side; i++) {
            int il = (i > 0) ? i - 1 : i;
            int ir = (i < grid) ? i + 1 : i;
            int jd = (j > 0) ? j - 1 : j;
            int ju = (j < grid) ? j + 1 : j;
            float hl = m->vertices[j * side + il].position[1];
            float hr = m->vertices[j * side + ir].position[1];
            float hd = m->vertices[jd * side + i].position[1];
            float hu = m->vertices[ju * side + i].position[1];

            float nx = hl - hr;
            float ny = 2.0f * cell;
            float nz = hd - hu;
            float len = sqrtf(nx * nx + ny * ny + nz * nz);
            if (len < 1e-6f)
                len = 1.0f;

            int idx = j * side + i;
            m->vertices[idx].normal[0] = nx / len;
            m->vertices[idx].normal[1] = ny / len;
            m->vertices[idx].normal[2] = nz / len;
        }
    }
}

void g3d_terrain_update(G3DMesh *m) {
    if (!terrain_valid(m))
        return;

    terrain_recompute_normals(m);
    g3d_mesh_calculate_bounds(m);

#ifndef VITA
    if (m->gpu_uploaded && m->vbo) {
        glBindBuffer(GL_ARRAY_BUFFER, m->vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (long)(m->vertex_count * sizeof(G3DVertex)),
                        m->vertices);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
#endif
}

/* Load a per-vertex Y dump (uint32 count + count floats) written by the editor.
   Applies the heights and refreshes normals/GPU. 0 if missing or count mismatch. */
int g3d_terrain_load(G3DMesh *m, const char *path) {
    if (!terrain_valid(m) || !path)
        return 0;
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    unsigned int n = 0;
    if (fread(&n, sizeof(unsigned int), 1, f) != 1 || n != m->vertex_count) {
        fclose(f);
        return 0;
    }
    for (unsigned int i = 0; i < n; i++) {
        float y;
        if (fread(&y, sizeof(float), 1, f) == 1)
            m->vertices[i].position[1] = y;
    }
    fclose(f);
    g3d_terrain_update(m);
    return 1;
}

/* ---- single vertex edit ------------------------------------------------ */

int g3d_terrain_set_vertex_height(G3DMesh *m, int ix, int iz, float h) {
    if (!terrain_valid(m))
        return 0;
    int side = m->terrain_side;
    if (ix < 0 || iz < 0 || ix >= side || iz >= side)
        return 0;
    m->vertices[iz * side + ix].position[1] = h;
    return 1;
}

/* ---- brushes ----------------------------------------------------------- */

/* Smooth falloff in [0,1]: 1 at centre, 0 at the radius edge. */
static float brush_falloff(float dist, float radius) {
    if (dist >= radius)
        return 0.0f;
    float t = dist / radius;
    float k = 1.0f - t * t;
    return k * k;
}

/* Iterate the grid vertices overlapping the brush footprint and call `apply`. */
typedef void (*brush_fn)(G3DMesh *m, int idx, float falloff, void *user);

static int terrain_brush(G3DMesh *m, float wx, float wz, float radius,
                         brush_fn apply, void *user) {
    if (!terrain_valid(m) || radius <= 0.0f)
        return 0;

    int side = m->terrain_side;
    int grid = side - 1;
    float cell = m->terrain_world_size / (float)grid;

    float gcx, gcz;
    world_to_grid(m, wx, wz, &gcx, &gcz);
    int gr = (int)ceilf(radius / cell) + 1;

    int i0 = (int)floorf(gcx) - gr, i1 = (int)ceilf(gcx) + gr;
    int j0 = (int)floorf(gcz) - gr, j1 = (int)ceilf(gcz) + gr;
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    if (i1 > side - 1) i1 = side - 1;
    if (j1 > side - 1) j1 = side - 1;

    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            int idx = j * side + i;
            float vx = m->vertices[idx].position[0];
            float vz = m->vertices[idx].position[2];
            float dx = vx - wx;
            float dz = vz - wz;
            float dist = sqrtf(dx * dx + dz * dz);
            float f = brush_falloff(dist, radius);
            if (f > 0.0f)
                apply(m, idx, f, user);
        }
    }
    return 1;
}

static void apply_raise(G3DMesh *m, int idx, float f, void *user) {
    float strength = *(float *)user;
    m->vertices[idx].position[1] += strength * f;
}

int g3d_terrain_raise(G3DMesh *m, float wx, float wz, float radius,
                      float strength) {
    if (!terrain_brush(m, wx, wz, radius, apply_raise, &strength))
        return 0;
    g3d_terrain_update(m);
    return 1;
}

typedef struct { float target; float amount; } flatten_arg;

static void apply_flatten(G3DMesh *m, int idx, float f, void *user) {
    flatten_arg *a = (flatten_arg *)user;
    float h = m->vertices[idx].position[1];
    float blend = a->amount * f;
    m->vertices[idx].position[1] = h + (a->target - h) * blend;
}

int g3d_terrain_flatten(G3DMesh *m, float wx, float wz, float radius,
                        float target_h, float amount) {
    flatten_arg a = {target_h, amount};
    if (!terrain_brush(m, wx, wz, radius, apply_flatten, &a))
        return 0;
    g3d_terrain_update(m);
    return 1;
}

int g3d_terrain_smooth(G3DMesh *m, float wx, float wz, float radius,
                       float amount) {
    if (!terrain_valid(m) || radius <= 0.0f)
        return 0;

    int side = m->terrain_side;
    int grid = side - 1;
    float cell = m->terrain_world_size / (float)grid;

    float gcx, gcz;
    world_to_grid(m, wx, wz, &gcx, &gcz);
    int gr = (int)ceilf(radius / cell) + 1;

    int i0 = (int)floorf(gcx) - gr, i1 = (int)ceilf(gcx) + gr;
    int j0 = (int)floorf(gcz) - gr, j1 = (int)ceilf(gcz) + gr;
    if (i0 < 0) i0 = 0;
    if (j0 < 0) j0 = 0;
    if (i1 > side - 1) i1 = side - 1;
    if (j1 > side - 1) j1 = side - 1;

    /* Average against neighbours (read original heights, write blended) so the
       brush direction doesn't bias the result. */
    for (int j = j0; j <= j1; j++) {
        for (int i = i0; i <= i1; i++) {
            int idx = j * side + i;
            float dx = m->vertices[idx].position[0] - wx;
            float dz = m->vertices[idx].position[2] - wz;
            float f = brush_falloff(sqrtf(dx * dx + dz * dz), radius);
            if (f <= 0.0f)
                continue;

            int il = (i > 0) ? i - 1 : i;
            int ir = (i < grid) ? i + 1 : i;
            int jd = (j > 0) ? j - 1 : j;
            int ju = (j < grid) ? j + 1 : j;
            float avg = 0.25f * (m->vertices[j * side + il].position[1] +
                                 m->vertices[j * side + ir].position[1] +
                                 m->vertices[jd * side + i].position[1] +
                                 m->vertices[ju * side + i].position[1]);
            float h = m->vertices[idx].position[1];
            m->vertices[idx].position[1] = h + (avg - h) * amount * f;
        }
    }
    g3d_terrain_update(m);
    return 1;
}
