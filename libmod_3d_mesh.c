/*
 * libmod_3d_mesh.c - Mesh Structures and GPU Buffer Management
 *
 * Manages vertex/index data and GPU buffer objects (VAO, VBO, EBO)
 */

#include "libmod_3d_mesh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <math.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include "libmod_ray_vita_gl.h"
#endif

/* ============================================================================
   MESH CREATION
   ============================================================================
 */

G3DMesh *g3d_mesh_create(const char *name, G3DVertex *vertices,
                         uint32_t vertex_count, uint32_t *indices,
                         uint32_t index_count) {
    G3DMesh *mesh = (G3DMesh *)malloc(sizeof(G3DMesh));
    if (!mesh)
        return NULL;

    memset(mesh, 0, sizeof(G3DMesh));

    mesh->id = 1;  /* TODO: Use proper mesh ID pool */
    strncpy(mesh->name, name, 63);
    mesh->name[63] = '\0';

    /* Copy vertex data */
    mesh->vertices = (G3DVertex *)malloc(vertex_count * sizeof(G3DVertex));
    if (!mesh->vertices) {
        free(mesh);
        return NULL;
    }
    memcpy(mesh->vertices, vertices, vertex_count * sizeof(G3DVertex));
    mesh->vertex_count = vertex_count;

    /* Copy index data */
    mesh->indices = (uint32_t *)malloc(index_count * sizeof(uint32_t));
    if (!mesh->indices) {
        free(mesh->vertices);
        free(mesh);
        return NULL;
    }
    memcpy(mesh->indices, indices, index_count * sizeof(uint32_t));
    mesh->index_count = index_count;

    /* Initialize material (will be set later) */
    mesh->material_id = -1;
    mesh->anim_node = -1;   /* static by default (not a node-animated submesh) */

    /* Calculate bounds */
    g3d_mesh_calculate_bounds(mesh);

    printf("G3D: Mesh created: %s (%u vertices, %u indices)\n", name,
           vertex_count, index_count);

    return mesh;
}

/* ============================================================================
   BOUNDS CALCULATION
   ============================================================================
 */

void g3d_mesh_calculate_bounds(G3DMesh *mesh) {
    if (!mesh || mesh->vertex_count == 0) {
        return;
    }

    /* Initialize bounds */
    mesh->aabb_min[0] = mesh->vertices[0].position[0];
    mesh->aabb_min[1] = mesh->vertices[0].position[1];
    mesh->aabb_min[2] = mesh->vertices[0].position[2];

    mesh->aabb_max[0] = mesh->vertices[0].position[0];
    mesh->aabb_max[1] = mesh->vertices[0].position[1];
    mesh->aabb_max[2] = mesh->vertices[0].position[2];

    /* Find min/max for each axis */
    for (uint32_t i = 1; i < mesh->vertex_count; i++) {
        for (int axis = 0; axis < 3; axis++) {
            float pos = mesh->vertices[i].position[axis];

            if (pos < mesh->aabb_min[axis])
                mesh->aabb_min[axis] = pos;
            if (pos > mesh->aabb_max[axis])
                mesh->aabb_max[axis] = pos;
        }
    }
}

/* ============================================================================
   GPU UPLOAD (VAO/VBO/EBO)
   ============================================================================
 */

int g3d_mesh_upload_gpu(G3DMesh *mesh) {
    if (!mesh || mesh->gpu_uploaded)
        return 1;  /* Already uploaded */

#ifndef VITA
    /* Create VAO */
    glGenVertexArrays(1, &mesh->vao);
    glBindVertexArray(mesh->vao);

    /* Create VBO (vertex buffer) */
    glGenBuffers(1, &mesh->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh->vertex_count * sizeof(G3DVertex),
                 mesh->vertices, GL_STATIC_DRAW);

    /* Vertex attributes */
    /* Position (location 0) */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(G3DVertex),
                          (void *)offsetof(G3DVertex, position));

    /* Normal (location 1) */
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(G3DVertex),
                          (void *)offsetof(G3DVertex, normal));

    /* Texcoord (location 2) */
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(G3DVertex),
                          (void *)offsetof(G3DVertex, texcoord));

    /* Create EBO (element buffer) */
    glGenBuffers(1, &mesh->ebo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh->index_count * sizeof(uint32_t),
                 mesh->indices, GL_STATIC_DRAW);

    /* Unbind VAO */
    glBindVertexArray(0);

    mesh->gpu_uploaded = 1;

    printf("G3D: Mesh uploaded to GPU (VAO=%u, VBO=%u, EBO=%u)\n", mesh->vao,
           mesh->vbo, mesh->ebo);

    return 1;
#else
    mesh->gpu_uploaded = 1;
    return 1;
#endif
}

/* ============================================================================
   RENDERING
   ============================================================================
 */

void g3d_mesh_render(G3DMesh *mesh) {
    if (!mesh || !mesh->gpu_uploaded || mesh->index_count == 0)
        return;

#ifndef VITA
    glBindVertexArray(mesh->vao);
    glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
#endif
}

/* Re-upload vertex data to an already-uploaded mesh (after editing positions/
   normals on the CPU). g3d_mesh_upload_gpu would no-op once uploaded. */
void g3d_mesh_update_gpu(G3DMesh *mesh) {
    if (!mesh || !mesh->gpu_uploaded || !mesh->vertices)
        return;
#ifndef VITA
    glBindBuffer(GL_ARRAY_BUFFER, mesh->vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0,
                    (long)(mesh->vertex_count * sizeof(G3DVertex)),
                    mesh->vertices);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
#endif
}

void g3d_mesh_update_indices_gpu(G3DMesh *mesh) {
    if (!mesh || !mesh->gpu_uploaded || !mesh->indices)
        return;
#ifndef VITA
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh->ebo);
    glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0,
                    (long)(mesh->index_count * sizeof(uint32_t)),
                    mesh->indices);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
#endif
}

/* ============================================================================
   LOD - MESH SIMPLIFICATION (vertex clustering)
   ============================================================================
   Overlay a uniform grid on the mesh AABB, collapse every vertex in a cell to
   one representative (the average), drop triangles that degenerate. Simple,
   deterministic, dependency-free; quality is fine for distant LOD. `grid` = the
   number of cells along the longest axis (smaller = more aggressive). Returns a
   new GPU-uploaded mesh, or NULL. */
G3DMesh *g3d_mesh_simplify(const G3DMesh *src, int grid) {
    if (!src || !src->vertices || src->vertex_count == 0 || src->index_count < 3)
        return NULL;
    if (grid < 2)  grid = 2;
    if (grid > 48) grid = 48;

    float mn[3], mx[3];
    for (int k = 0; k < 3; k++) { mn[k] = src->aabb_min[k]; mx[k] = src->aabb_max[k]; }
    float ext[3];
    float longest = 1e-6f;
    for (int k = 0; k < 3; k++) { ext[k] = mx[k] - mn[k]; if (ext[k] > longest) longest = ext[k]; }
    float cell = longest / (float)grid;
    if (cell < 1e-6f) cell = 1e-6f;

    int gx = (int)(ext[0] / cell) + 1;
    int gy = (int)(ext[1] / cell) + 1;
    int gz = (int)(ext[2] / cell) + 1;
    long ncells = (long)gx * gy * gz;

    int *cellmap = (int *)malloc(sizeof(int) * ncells);           /* cell -> new index */
    if (!cellmap) return NULL;
    for (long i = 0; i < ncells; i++) cellmap[i] = -1;

    uint32_t vc = src->vertex_count;
    uint32_t *remap = (uint32_t *)malloc(sizeof(uint32_t) * vc);   /* old vert -> new index */
    /* accumulators, at most vc new vertices */
    double *ax = (double *)calloc(vc, sizeof(double));
    double *ay = (double *)calloc(vc, sizeof(double));
    double *az = (double *)calloc(vc, sizeof(double));
    double *anx = (double *)calloc(vc, sizeof(double));
    double *any = (double *)calloc(vc, sizeof(double));
    double *anz = (double *)calloc(vc, sizeof(double));
    double *au = (double *)calloc(vc, sizeof(double));
    double *av = (double *)calloc(vc, sizeof(double));
    int *acnt = (int *)calloc(vc, sizeof(int));
    if (!remap || !ax || !ay || !az || !anx || !any || !anz || !au || !av || !acnt) {
        free(cellmap); free(remap); free(ax); free(ay); free(az);
        free(anx); free(any); free(anz); free(au); free(av); free(acnt); return NULL;
    }

    uint32_t newv = 0;
    for (uint32_t v = 0; v < vc; v++) {
        const G3DVertex *s = &src->vertices[v];
        int cx = (int)((s->position[0] - mn[0]) / cell); if (cx < 0) cx = 0; if (cx >= gx) cx = gx - 1;
        int cy = (int)((s->position[1] - mn[1]) / cell); if (cy < 0) cy = 0; if (cy >= gy) cy = gy - 1;
        int cz = (int)((s->position[2] - mn[2]) / cell); if (cz < 0) cz = 0; if (cz >= gz) cz = gz - 1;
        long ci = ((long)cz * gy + cy) * gx + cx;
        int ni = cellmap[ci];
        if (ni < 0) { ni = (int)newv++; cellmap[ci] = ni; }
        remap[v] = (uint32_t)ni;
        ax[ni] += s->position[0]; ay[ni] += s->position[1]; az[ni] += s->position[2];
        anx[ni] += s->normal[0]; any[ni] += s->normal[1]; anz[ni] += s->normal[2];
        au[ni] += s->texcoord[0]; av[ni] += s->texcoord[1];
        acnt[ni]++;
    }

    G3DVertex *nv = (G3DVertex *)malloc(sizeof(G3DVertex) * newv);
    for (uint32_t i = 0; i < newv; i++) {
        float inv = acnt[i] ? 1.0f / acnt[i] : 1.0f;
        nv[i].position[0] = (float)(ax[i] * inv); nv[i].position[1] = (float)(ay[i] * inv); nv[i].position[2] = (float)(az[i] * inv);
        float nx = (float)anx[i], ny = (float)any[i], nz = (float)anz[i];
        float nl = sqrtf(nx*nx + ny*ny + nz*nz); if (nl < 1e-6f) nl = 1.0f;
        nv[i].normal[0] = nx/nl; nv[i].normal[1] = ny/nl; nv[i].normal[2] = nz/nl;
        nv[i].texcoord[0] = (float)(au[i] * inv); nv[i].texcoord[1] = (float)(av[i] * inv);
    }

    uint32_t *ni_idx = (uint32_t *)malloc(sizeof(uint32_t) * src->index_count);
    uint32_t nic = 0;
    for (uint32_t i = 0; i + 2 < src->index_count; i += 3) {
        uint32_t a = remap[src->indices[i]], b = remap[src->indices[i+1]], c = remap[src->indices[i+2]];
        if (a != b && b != c && a != c) { ni_idx[nic++] = a; ni_idx[nic++] = b; ni_idx[nic++] = c; }
    }

    free(cellmap); free(remap);
    free(ax); free(ay); free(az); free(anx); free(any); free(anz); free(au); free(av); free(acnt);

    G3DMesh *out = NULL;
    if (newv > 0 && nic >= 3)
        out = g3d_mesh_create("lod", nv, newv, ni_idx, nic);
    free(nv); free(ni_idx);
    if (out) g3d_mesh_upload_gpu(out);
    return out;
}

/* Cached low-poly LOD, generated on first use. Returns the mesh itself if it is
   too small to bother simplifying, so callers can always use the result. */
G3DMesh *g3d_mesh_lod(G3DMesh *mesh) {
    if (!mesh) return NULL;
    if (mesh->lod) return (G3DMesh *)mesh->lod;
    G3DMesh *l = (mesh->vertex_count >= 48) ? g3d_mesh_simplify(mesh, 12) : NULL;
    mesh->lod = l ? l : mesh;   /* cache (self if not simplifiable) */
    return (G3DMesh *)mesh->lod;
}

/* ============================================================================
   CLEANUP
   ============================================================================
 */

void g3d_mesh_free(G3DMesh *mesh) {
    if (!mesh)
        return;

#ifndef VITA
    if (mesh->vao)
        glDeleteVertexArrays(1, &mesh->vao);
    if (mesh->vbo)
        glDeleteBuffers(1, &mesh->vbo);
    if (mesh->ebo)
        glDeleteBuffers(1, &mesh->ebo);
#endif

    if (mesh->vertices)
        free(mesh->vertices);
    if (mesh->indices)
        free(mesh->indices);
    free(mesh->bind_pos);
    free(mesh->bind_nrm);
    free(mesh->vjoints);
    free(mesh->vweights);

    free(mesh);
}

/* ============================================================================
   MODEL API
   ============================================================================
 */

G3DModel *g3d_model_create(const char *name, const char *filepath) {
    G3DModel *model = (G3DModel *)malloc(sizeof(G3DModel));
    if (!model)
        return NULL;

    memset(model, 0, sizeof(G3DModel));

    model->id = 1;  /* TODO: Use proper model ID pool */
    strncpy(model->name, name, 63);
    model->name[63] = '\0';

    if (filepath) {
        strncpy(model->filepath, filepath, 255);
        model->filepath[255] = '\0';
    }

    return model;
}

void g3d_model_add_mesh(G3DModel *model, G3DMesh *mesh) {
    if (!model || !mesh)
        return;

    /* Allocate or realloc mesh array */
    G3DMesh *new_meshes =
        (G3DMesh *)realloc(model->meshes,
                           (model->mesh_count + 1) * sizeof(G3DMesh));
    if (!new_meshes)
        return;

    model->meshes = new_meshes;
    model->meshes[model->mesh_count] = *mesh;
    model->mesh_count++;
}

void g3d_model_calculate_bounds(G3DModel *model) {
    if (!model || model->mesh_count == 0)
        return;

    /* Initialize from first mesh */
    G3DMesh *first_mesh = &model->meshes[0];
    model->aabb_min[0] = first_mesh->aabb_min[0];
    model->aabb_min[1] = first_mesh->aabb_min[1];
    model->aabb_min[2] = first_mesh->aabb_min[2];

    model->aabb_max[0] = first_mesh->aabb_max[0];
    model->aabb_max[1] = first_mesh->aabb_max[1];
    model->aabb_max[2] = first_mesh->aabb_max[2];

    /* Expand to encompass all meshes */
    for (uint32_t i = 1; i < model->mesh_count; i++) {
        G3DMesh *mesh = &model->meshes[i];

        for (int axis = 0; axis < 3; axis++) {
            if (mesh->aabb_min[axis] < model->aabb_min[axis])
                model->aabb_min[axis] = mesh->aabb_min[axis];
            if (mesh->aabb_max[axis] > model->aabb_max[axis])
                model->aabb_max[axis] = mesh->aabb_max[axis];
        }
    }
}

void g3d_model_free(G3DModel *model) {
    if (!model)
        return;

    for (uint32_t i = 0; i < model->mesh_count; i++) {
        g3d_mesh_free(&model->meshes[i]);
    }

    if (model->meshes)
        free(model->meshes);

    free(model);
}
