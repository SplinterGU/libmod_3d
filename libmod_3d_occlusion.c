/*
 * libmod_3d_occlusion.c - Hardware occlusion culling via GL occlusion queries
 *
 * See libmod_3d_occlusion.h for the why. Every rule here errs toward drawing:
 * an entity is only skipped when a query positively proved that not one sample
 * of its bounding box survived the depth test.
 */

#include "libmod_3d_occlusion.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

static int g_enabled = 1;
static unsigned int g_culled = 0;

int g3d_occlusion_enabled(void) { return g_enabled; }
void g3d_occlusion_set_enabled(int enable) { g_enabled = enable ? 1 : 0; }
unsigned int g3d_occlusion_culled(void) { return g_culled; }

int g3d_occlusion_visible(G3DEntity *entity) {
    if (!g_enabled || !entity)
        return 1;
    return entity->occ_visible;
}

#ifndef VITA

static G3DShaderProgram *g_sh = NULL;
static unsigned int g_vao = 0, g_vbo = 0, g_ebo = 0;
static int g_init = 0;

static const char *VS_BOX =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"uniform mat4 uMVP;\n"
"void main() { gl_Position = uMVP * vec4(aPos, 1.0); }\n";

static const char *FS_BOX =
"#version 330 core\n"
"out vec4 FragColor;\n"
"void main() { FragColor = vec4(1.0); }\n";

/* Unit cube [0,1]^3: the box model matrix maps it onto the mesh AABB. */
static const float CUBE_V[] = {
    0,0,0,  1,0,0,  1,1,0,  0,1,0,
    0,0,1,  1,0,1,  1,1,1,  0,1,1,
};
static const unsigned int CUBE_I[] = {
    0,1,2, 2,3,0,   4,5,6, 6,7,4,     /* -Z, +Z */
    0,4,7, 7,3,0,   1,5,6, 6,2,1,     /* -X, +X */
    0,1,5, 5,4,0,   3,2,6, 6,7,3,     /* -Y, +Y */
};

static int occ_init(void) {
    if (g_init)
        return 1;
    g_sh = g3d_shader_create(VS_BOX, FS_BOX);
    if (!g_sh) {
        fprintf(stderr, "G3D: occlusion shader failed\n");
        g_enabled = 0;
        return 0;
    }
    glGenVertexArrays(1, &g_vao);
    glGenBuffers(1, &g_vbo);
    glGenBuffers(1, &g_ebo);
    glBindVertexArray(g_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_V), CUBE_V, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(CUBE_I), CUBE_I, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glBindVertexArray(0);
    g_init = 1;
    printf("G3D: occlusion culling ready (GL occlusion queries, 1-frame latency)\n");
    return 1;
}

void g3d_occlusion_pass(G3DCamera *camera, int *entities, int entity_count, int flip_y) {
    g_culled = 0;
    if (!g_enabled || !camera || !entities || entity_count <= 0)
        return;
    if (!occ_init())
        return;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }
    Mat4 viewproj = mat4_multiply(proj, view);

    /* Test against the opaque depth without touching colour or depth. Culling
       off so the box still rasterises when seen from inside. */
    g3d_shader_use(g_sh);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glBindVertexArray(g_vao);

    /* A box closer than this to the camera can be clipped by the near plane,
       which would return zero samples and wrongly hide it. */
    float margin = camera->near_plane * 4.0f + 0.5f;

    for (int i = 0; i < entity_count; i++) {
        G3DEntity *e = g3d_entity_impl_get(entities[i]);
        if (!e || !e->active || !e->mesh)
            continue;
        G3DMesh *m = (G3DMesh *)e->mesh;

        /* Collect the previous frame's answer. Never block: if the GPU hasn't
           finished, keep the old state and try again next frame. */
        if (e->occ_pending) {
            GLuint ready = 0;
            glGetQueryObjectuiv(e->occ_query, GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready) {
                GLuint samples = 0;
                glGetQueryObjectuiv(e->occ_query, GL_QUERY_RESULT, &samples);
                e->occ_visible = samples ? 1 : 0;
                e->occ_pending = 0;
            }
        }
        if (!e->occ_visible)
            g_culled++;

        /* World AABB of the mesh box (8 corners through the world matrix). */
        Vec3 wmin, wmax;
        for (int c = 0; c < 8; c++) {
            Vec3 corner = vec3_make((c & 1) ? m->aabb_max[0] : m->aabb_min[0],
                                    (c & 2) ? m->aabb_max[1] : m->aabb_min[1],
                                    (c & 4) ? m->aabb_max[2] : m->aabb_min[2]);
            Vec3 w = mat4_transform_point(e->world_matrix, corner);
            if (c == 0) { wmin = w; wmax = w; }
            else {
                if (w.x < wmin.x) wmin.x = w.x; if (w.x > wmax.x) wmax.x = w.x;
                if (w.y < wmin.y) wmin.y = w.y; if (w.y > wmax.y) wmax.y = w.y;
                if (w.z < wmin.z) wmin.z = w.z; if (w.z > wmax.z) wmax.z = w.z;
            }
        }

        /* Outside the frustum: the box would rasterise nothing and the query
           would call it hidden, so it would stay hidden for a frame after
           coming back. Frustum culling already skips it - just mark it visible
           and don't waste a query. */
        if (!g3d_camera_frustum_contains_aabb(camera, wmin, wmax)) {
            e->occ_visible = 1;
            continue;
        }

        /* Camera inside (or almost inside) the box: the near plane would clip
           the box away and the query would wrongly report it hidden. */
        Vec3 cp = camera->position;
        if (cp.x >= wmin.x - margin && cp.x <= wmax.x + margin &&
            cp.y >= wmin.y - margin && cp.y <= wmax.y + margin &&
            cp.z >= wmin.z - margin && cp.z <= wmax.z + margin) {
            e->occ_visible = 1;
            continue;
        }

        if (e->occ_pending)
            continue;                     /* still waiting: don't stack queries */
        if (!e->occ_query)
            glGenQueries(1, &e->occ_query);

        /* Draw the box: unit cube -> world AABB, slightly grown so a surface
           exactly on the box face doesn't self-occlude it. */
        Vec3 size = vec3_make(wmax.x - wmin.x, wmax.y - wmin.y, wmax.z - wmin.z);
        float grow = 0.01f;
        Mat4 model = mat4_multiply(
            mat4_translate(wmin.x - grow, wmin.y - grow, wmin.z - grow),
            mat4_scale(size.x + grow * 2.0f, size.y + grow * 2.0f, size.z + grow * 2.0f));
        g3d_shader_set_mat4(g_sh, "uMVP", mat4_multiply(viewproj, model));

        glBeginQuery(GL_ANY_SAMPLES_PASSED, e->occ_query);
        glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_INT, 0);
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        e->occ_pending = 1;
    }

    glBindVertexArray(0);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
}

void g3d_occlusion_shutdown(void) {
    if (!g_init)
        return;
    if (g_sh) g3d_shader_free(g_sh);
    glDeleteVertexArrays(1, &g_vao);
    glDeleteBuffers(1, &g_vbo);
    glDeleteBuffers(1, &g_ebo);
    g_sh = NULL;
    g_init = 0;
}

#else /* VITA: no occlusion culling */

void g3d_occlusion_pass(G3DCamera *c, int *e, int n, int f) {
    (void)c; (void)e; (void)n; (void)f;
}
void g3d_occlusion_shutdown(void) {}

#endif
