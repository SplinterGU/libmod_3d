/*
 * libmod_3d_instance.c - GPU instancing for open-world vegetation/props
 */

#include "libmod_3d_instance.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_light.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define MAX_GROUPS 256

/* Instanced Phong-ish shader: per-instance model matrix in attribs 3..6,
   directional + ambient light, optional wind sway by object height. */
static const char *inst_vert =
    "#version 330 core\n"
    "layout(location=0) in vec3 position;\n"
    "layout(location=1) in vec3 normal;\n"
    "layout(location=2) in vec2 texcoord;\n"
    "layout(location=3) in vec4 iM0;\n"
    "layout(location=4) in vec4 iM1;\n"
    "layout(location=5) in vec4 iM2;\n"
    "layout(location=6) in vec4 iM3;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "uniform float uTime;\n"
    "uniform float uWind;\n"
    "out vec3 vN;\n"
    "out vec2 vUV;\n"
    "out vec3 vWP;\n"
    "void main() {\n"
    "    mat4 M = mat4(iM0, iM1, iM2, iM3);\n"
    "    vec4 wp = M * vec4(position, 1.0);\n"
    "    float sway = sin(uTime * 1.6 + wp.x * 0.25 + wp.z * 0.25)\n"
    "               * uWind * max(position.y, 0.0) * 0.04;\n"
    "    wp.x += sway;\n"
    "    wp.z += sway * 0.5;\n"
    "    gl_Position = uProjection * uView * wp;\n"
    "    vN = normalize(mat3(M) * normal);\n"
    "    vUV = texcoord;\n"
    "    vWP = wp.xyz;\n"
    "}\n";

static const char *inst_frag =
    "#version 330 core\n"
    "in vec3 vN;\n"
    "in vec2 vUV;\n"
    "in vec3 vWP;\n"
    "uniform sampler2D uTex;\n"
    "uniform int uHasTex;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uLightColor;\n"
    "uniform vec3 uAmbient;\n"
    "uniform sampler2D uShadowMap;\n"
    "uniform mat4 uLightSpace;\n"
    "uniform int uHasShadow;\n"
    "out vec4 FragColor;\n"
    "float shadowAt() {\n"
    "    if (uHasShadow == 0) return 0.0;\n"
    "    vec4 ls = uLightSpace * vec4(vWP, 1.0);\n"
    "    vec3 p = ls.xyz / ls.w * 0.5 + 0.5;\n"
    "    if (p.z > 1.0 || p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0) return 0.0;\n"
    "    float cur = p.z;\n"
    "    float sh = 0.0;\n"
    "    vec2 ts = 1.0 / textureSize(uShadowMap, 0);\n"
    "    for (int x = -1; x <= 1; x++)\n"
    "      for (int y = -1; y <= 1; y++)\n"
    "        sh += cur - 0.003 > texture(uShadowMap, p.xy + vec2(x,y)*ts).r ? 1.0 : 0.0;\n"
    "    return sh / 9.0;\n"
    "}\n"
    "void main() {\n"
    "    vec3 albedo = uHasTex == 1 ? texture(uTex, vUV).rgb : vec3(0.4, 0.6, 0.3);\n"
    "    if (uHasTex == 1 && texture(uTex, vUV).a < 0.5) discard;\n"  /* alpha-cut leaves */
    "    float d = max(dot(normalize(vN), normalize(-uLightDir)), 0.0);\n"
    "    float sh = shadowAt();\n"
    "    vec3 col = albedo * (uAmbient + uLightColor * d * (1.0 - sh * 0.7));\n"
    "    FragColor = vec4(col, 1.0);\n"
    "}\n";

/* Depth-only instanced shader for the shadow pass (same wind sway). */
static const char *inst_depth_vert =
    "#version 330 core\n"
    "layout(location=0) in vec3 position;\n"
    "layout(location=3) in vec4 iM0;\n"
    "layout(location=4) in vec4 iM1;\n"
    "layout(location=5) in vec4 iM2;\n"
    "layout(location=6) in vec4 iM3;\n"
    "uniform mat4 uLightSpace;\n"
    "uniform float uTime;\n"
    "uniform float uWind;\n"
    "void main() {\n"
    "    mat4 M = mat4(iM0, iM1, iM2, iM3);\n"
    "    vec4 wp = M * vec4(position, 1.0);\n"
    "    float sway = sin(uTime * 1.6 + wp.x * 0.25 + wp.z * 0.25)\n"
    "               * uWind * max(position.y, 0.0) * 0.04;\n"
    "    wp.x += sway; wp.z += sway * 0.5;\n"
    "    gl_Position = uLightSpace * wp;\n"
    "}\n";
static const char *inst_depth_frag =
    "#version 330 core\n"
    "void main() {}\n";

typedef struct {
    int active;
    G3DMesh *mesh;
    unsigned int tex;       /* gl handle, 0 = none */
    float *mats;            /* count*16 instance model matrices */
    int count, cap;
    float wind;
    float max_dist;
    unsigned int vao, inst_vbo;       /* camera pass: VAO + visible matrices */
    unsigned int vao_shadow, all_vbo; /* shadow pass: VAO + ALL matrices */
    int gpu_cap;            /* allocated visible VBO capacity (matrices) */
    int all_uploaded;       /* matrices currently in all_vbo */
    int dirty;              /* mats changed since last all_vbo upload */
    float *visible;         /* temp visible matrices, cap*16 */
} Group;

static struct {
    int initialized;
    G3DShaderProgram *shader;
    G3DShaderProgram *depth_shader;
    Group g[MAX_GROUPS];
} g_inst = {0};

static int inst_init(void) {
    if (g_inst.initialized) return 1;
#ifndef VITA
    g_inst.shader = g3d_shader_create(inst_vert, inst_frag);
    if (!g_inst.shader) { fprintf(stderr, "G3D: instance shader failed\n"); return 0; }
    g_inst.depth_shader = g3d_shader_create(inst_depth_vert, inst_depth_frag);
    if (!g_inst.depth_shader) { fprintf(stderr, "G3D: instance depth shader failed\n"); return 0; }
#endif
    g_inst.initialized = 1;
    return 1;
}

#ifndef VITA
/* Build the group VAO: mesh vertex buffer (attribs 0-2) + per-instance matrix
   buffer (attribs 3-6, divisor 1) + mesh index buffer. */
static void build_vao(Group *gr) {
    glGenVertexArrays(1, &gr->vao);
    glGenBuffers(1, &gr->inst_vbo);
    glBindVertexArray(gr->vao);

    glBindBuffer(GL_ARRAY_BUFFER, gr->mesh->vbo);   /* G3DVertex: 32-byte stride */
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 32, (void *)12);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 32, (void *)24);

    glBindBuffer(GL_ARRAY_BUFFER, gr->inst_vbo);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, 64, (void *)(intptr_t)(i * 16));
        glVertexAttribDivisor(3 + i, 1);
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gr->mesh->ebo);
    glBindVertexArray(0);

    /* Shadow VAO: same mesh + ALL instance matrices buffer */
    glGenVertexArrays(1, &gr->vao_shadow);
    glGenBuffers(1, &gr->all_vbo);
    glBindVertexArray(gr->vao_shadow);
    glBindBuffer(GL_ARRAY_BUFFER, gr->mesh->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 32, (void *)0);
    glBindBuffer(GL_ARRAY_BUFFER, gr->all_vbo);
    for (int i = 0; i < 4; i++) {
        glEnableVertexAttribArray(3 + i);
        glVertexAttribPointer(3 + i, 4, GL_FLOAT, GL_FALSE, 64, (void *)(intptr_t)(i * 16));
        glVertexAttribDivisor(3 + i, 1);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gr->mesh->ebo);
    glBindVertexArray(0);
}

/* Upload all instance matrices to all_vbo if they changed. */
static void ensure_all_uploaded(Group *gr) {
    if (!gr->dirty && gr->all_uploaded == gr->count) return;
    glBindBuffer(GL_ARRAY_BUFFER, gr->all_vbo);
    glBufferData(GL_ARRAY_BUFFER, (size_t)gr->count * 64, gr->mats, GL_STATIC_DRAW);
    gr->all_uploaded = gr->count;
    gr->dirty = 0;
}
#endif

int g3d_instances_create(void *mesh, void *texture) {
    if (!mesh || !inst_init()) return -1;
    int idx = -1;
    for (int i = 0; i < MAX_GROUPS; i++)
        if (!g_inst.g[i].active) { idx = i; break; }
    if (idx < 0) return -1;
    Group *gr = &g_inst.g[idx];
    gr->mesh = (G3DMesh *)mesh;
    G3DTexture *t = (G3DTexture *)texture;
    gr->tex = t ? t->gl_handle : 0;
    gr->cap = 256;
    gr->mats = (float *)malloc((size_t)gr->cap * 16 * sizeof(float));
    gr->visible = (float *)malloc((size_t)gr->cap * 16 * sizeof(float));
    gr->count = 0;
    gr->wind = 0.0f;
    gr->max_dist = 250.0f;
    gr->gpu_cap = 0;
#ifndef VITA
    build_vao(gr);
#endif
    gr->active = 1;
    return idx;
}

int g3d_instances_add(int group, float x, float y, float z,
                      float yaw_deg, float scale) {
    if (group < 0 || group >= MAX_GROUPS || !g_inst.g[group].active) return -1;
    Group *gr = &g_inst.g[group];
    if (gr->count >= gr->cap) {
        gr->cap *= 2;
        gr->mats = (float *)realloc(gr->mats, (size_t)gr->cap * 16 * sizeof(float));
        gr->visible = (float *)realloc(gr->visible, (size_t)gr->cap * 16 * sizeof(float));
    }
    Mat4 m = mat4_trs(vec3_make(x, y, z),
                      quat_from_euler(0.0f, yaw_deg * 3.14159265f / 180.0f, 0.0f),
                      vec3_make(scale, scale, scale));
    for (int k = 0; k < 16; k++)
        gr->mats[gr->count * 16 + k] = m.m[k];
    gr->count++;
    gr->dirty = 1;
    return gr->count - 1;
}

/* Update an existing instance in place (no append). Lets one-process-per-object
   code (BennuGD idiom) own a stable slot for its whole life and just refresh its
   transform each frame, while still drawing the whole group in one call. */
void g3d_instances_set(int group, int index, float x, float y, float z,
                       float yaw_deg, float scale) {
    if (group < 0 || group >= MAX_GROUPS || !g_inst.g[group].active) return;
    Group *gr = &g_inst.g[group];
    if (index < 0 || index >= gr->count) return;
    Mat4 m = mat4_trs(vec3_make(x, y, z),
                      quat_from_euler(0.0f, yaw_deg * 3.14159265f / 180.0f, 0.0f),
                      vec3_make(scale, scale, scale));
    for (int k = 0; k < 16; k++)
        gr->mats[index * 16 + k] = m.m[k];
    gr->dirty = 1;
}

void g3d_instances_set_wind(int group, float strength) {
    if (group >= 0 && group < MAX_GROUPS && g_inst.g[group].active)
        g_inst.g[group].wind = strength;
}
void g3d_instances_set_distance(int group, float dist) {
    if (group >= 0 && group < MAX_GROUPS && g_inst.g[group].active)
        g_inst.g[group].max_dist = (dist > 1.0f) ? dist : 1.0f;
}
void g3d_instances_clear(int group) {
    if (group >= 0 && group < MAX_GROUPS && g_inst.g[group].active)
        g_inst.g[group].count = 0;
}
int g3d_instances_count(int group) {
    if (group < 0 || group >= MAX_GROUPS || !g_inst.g[group].active) return 0;
    return g_inst.g[group].count;
}

/* ---- data-driven scatter (editor-facing population) -------------------- */

static unsigned int s_rng = 1;
static float frand(float a, float b) {
    s_rng = s_rng * 1103515245u + 12345u;
    float t = ((s_rng >> 16) & 0x7fff) / 32767.0f;
    return a + (b - a) * t;
}

int g3d_scatter_mesh(void *mesh, void *texture, void *terrain, int count,
                     float area, float smin, float smax, float wind,
                     unsigned int seed) {
    int g = g3d_instances_create(mesh, texture);
    if (g < 0) return -1;
    g3d_instances_set_wind(g, wind);
    G3DMesh *tm = (G3DMesh *)terrain;
    s_rng = seed ? seed : 1;
    for (int i = 0; i < count; i++) {
        float x = frand(-area, area);
        float z = frand(-area, area);
        float y = tm ? g3d_terrain_get_height(tm, x, z) : 0.0f;
        float yaw = frand(0.0f, 360.0f);
        float sc = frand(smin, smax);
        g3d_instances_add(g, x, y, z, yaw, sc);
    }
    return g;
}

int g3d_scatter_model(void *modelv, void *terrain, int count, float area,
                      float target_h, float scale_var, float wind,
                      unsigned int seed) {
    G3DModel *m = (G3DModel *)modelv;
    if (!m || m->mesh_count == 0) return -1;
    /* model height = max Y extent across submeshes */
    float ymin = m->meshes[0].aabb_min[1], ymax = m->meshes[0].aabb_max[1];
    for (uint32_t s = 1; s < m->mesh_count; s++) {
        if (m->meshes[s].aabb_min[1] < ymin) ymin = m->meshes[s].aabb_min[1];
        if (m->meshes[s].aabb_max[1] > ymax) ymax = m->meshes[s].aabb_max[1];
    }
    float h = ymax - ymin;
    if (h <= 0.0f) h = 1.0f;
    float base = target_h / h;
    float smin = base * (1.0f - scale_var);
    float smax = base * (1.0f + scale_var);
    int first = -1;
    for (uint32_t s = 0; s < m->mesh_count; s++) {
        void *tex = m->mesh_textures ? m->mesh_textures[s] : NULL;
        /* same seed per submesh -> identical placement so parts line up */
        int g = g3d_scatter_mesh(&m->meshes[s], tex, terrain, count, area,
                                 smin, smax, wind, seed);
        if (first < 0) first = g;
    }
    return first;
}

void g3d_instances_render_depth(Mat4 light_space) {
    if (!g_inst.initialized || !g_inst.depth_shader) return;
#ifndef VITA
    int any = 0;
    for (int i = 0; i < MAX_GROUPS; i++) if (g_inst.g[i].active && g_inst.g[i].count) any = 1;
    if (!any) return;

    g3d_shader_use(g_inst.depth_shader);
    g3d_shader_set_mat4(g_inst.depth_shader, "uLightSpace", light_space);
    g3d_shader_set_float(g_inst.depth_shader, "uTime", (float)SDL_GetTicks() / 1000.0f);

    for (int i = 0; i < MAX_GROUPS; i++) {
        Group *gr = &g_inst.g[i];
        if (!gr->active || gr->count == 0) continue;
        ensure_all_uploaded(gr);
        g3d_shader_set_float(g_inst.depth_shader, "uWind", gr->wind);
        glBindVertexArray(gr->vao_shadow);
        glDrawElementsInstanced(GL_TRIANGLES, gr->mesh->index_count,
                                GL_UNSIGNED_INT, 0, gr->count);
    }
    glBindVertexArray(0);
#endif
}

void g3d_instances_render_all(G3DCamera *camera, int flip_y) {
    if (!g_inst.initialized || !g_inst.shader || !camera) return;
#ifndef VITA
    int any = 0;
    for (int i = 0; i < MAX_GROUPS; i++) if (g_inst.g[i].active && g_inst.g[i].count) any = 1;
    if (!any) return;

    g3d_camera_update(camera);
    g3d_camera_update_frustum(camera);
    Vec3 cp = camera->position;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }

    /* Directional light + ambient from the scene */
    Vec3 ldir = vec3_make(-0.4f, -0.8f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 0.95f);
    int lc = 0; int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction;
            lcol = vec3_scale(vec3_make(l->color[0], l->color[1], l->color[2]), l->intensity);
            break;
        }
    }

    g3d_shader_use(g_inst.shader);
    g3d_shader_set_mat4(g_inst.shader, "uView", view);
    g3d_shader_set_mat4(g_inst.shader, "uProjection", proj);
    g3d_shader_set_float(g_inst.shader, "uTime", (float)SDL_GetTicks() / 1000.0f);
    g3d_shader_set_vec3(g_inst.shader, "uLightDir", ldir);
    g3d_shader_set_vec3(g_inst.shader, "uLightColor", lcol);
    g3d_shader_set_vec3(g_inst.shader, "uAmbient", vec3_make(0.40f, 0.42f, 0.45f));

    /* Receive shadows from the directional shadow map (unit 1) */
    if (g3d_renderer_shadows_on()) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g3d_renderer_shadow_texture());
        g3d_shader_set_int(g_inst.shader, "uShadowMap", 1);
        g3d_shader_set_mat4(g_inst.shader, "uLightSpace", g3d_renderer_light_space());
        g3d_shader_set_int(g_inst.shader, "uHasShadow", 1);
        glActiveTexture(GL_TEXTURE0);
    } else {
        g3d_shader_set_int(g_inst.shader, "uHasShadow", 0);
    }

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glActiveTexture(GL_TEXTURE0);

    for (int i = 0; i < MAX_GROUPS; i++) {
        Group *gr = &g_inst.g[i];
        if (!gr->active || gr->count == 0) continue;

        /* CPU cull: keep instances within distance and in the frustum */
        float md2 = gr->max_dist * gr->max_dist;
        Vec3 amn = vec3_make(gr->mesh->aabb_min[0], gr->mesh->aabb_min[1], gr->mesh->aabb_min[2]);
        Vec3 amx = vec3_make(gr->mesh->aabb_max[0], gr->mesh->aabb_max[1], gr->mesh->aabb_max[2]);
        int vis = 0;
        for (int n = 0; n < gr->count; n++) {
            float *m = &gr->mats[n * 16];
            float ix = m[12], iy = m[13], iz = m[14];
            float dx = ix - cp.x, dy = iy - cp.y, dz = iz - cp.z;
            if (dx*dx + dy*dy + dz*dz > md2) continue;
            /* world AABB ~ instance pos + scaled mesh AABB (scale from m[0]) */
            float s = sqrtf(m[0]*m[0] + m[1]*m[1] + m[2]*m[2]);
            Vec3 wmn = vec3_make(ix + amn.x*s, iy + amn.y*s, iz + amn.z*s);
            Vec3 wmx = vec3_make(ix + amx.x*s, iy + amx.y*s, iz + amx.z*s);
            if (!g3d_camera_frustum_contains_aabb(camera, wmn, wmx)) continue;
            for (int k = 0; k < 16; k++) gr->visible[vis * 16 + k] = m[k];
            vis++;
        }
        if (vis == 0) continue;

        glBindVertexArray(gr->vao);
        glBindBuffer(GL_ARRAY_BUFFER, gr->inst_vbo);
        if (vis > gr->gpu_cap) {
            gr->gpu_cap = vis;
            glBufferData(GL_ARRAY_BUFFER, (size_t)gr->gpu_cap * 64, gr->visible, GL_DYNAMIC_DRAW);
        } else {
            glBufferSubData(GL_ARRAY_BUFFER, 0, (size_t)vis * 64, gr->visible);
        }

        if (gr->tex) {
            glBindTexture(GL_TEXTURE_2D, gr->tex);
            g3d_shader_set_int(g_inst.shader, "uTex", 0);
            g3d_shader_set_int(g_inst.shader, "uHasTex", 1);
        } else {
            g3d_shader_set_int(g_inst.shader, "uHasTex", 0);
        }
        g3d_shader_set_float(g_inst.shader, "uWind", gr->wind);

        glDrawElementsInstanced(GL_TRIANGLES, gr->mesh->index_count,
                                GL_UNSIGNED_INT, 0, vis);
    }
    glBindVertexArray(0);
#endif
}

void g3d_instances_shutdown(void) {
#ifndef VITA
    for (int i = 0; i < MAX_GROUPS; i++) {
        Group *gr = &g_inst.g[i];
        if (!gr->active) continue;
        if (gr->inst_vbo) glDeleteBuffers(1, &gr->inst_vbo);
        if (gr->vao) glDeleteVertexArrays(1, &gr->vao);
        free(gr->mats); free(gr->visible);
        gr->active = 0;
    }
    if (g_inst.shader) g3d_shader_free(g_inst.shader);
#endif
    g_inst.shader = NULL;
    g_inst.initialized = 0;
}
