/*
 * libmod_3d_renderer.c - Forward Rendering Pipeline Implementation
 *
 * Core rendering: frustum culling, shadow pass, forward rendering
 */

#include "libmod_3d_renderer.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_material.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_light.h"
#include "libmod_3d_water.h"
#include "libmod_3d_watersim.h"
#include "libmod_3d_voxterrain.h"
#include "libmod_3d_flow.h"
#include <SDL.h>
#include "libmod_3d_particles.h"
#include "libmod_3d_fire.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_ibl.h"
#include "libmod_3d_cloud_glsl.h"
#include "libmod_3d_mirror.h"
#include "libmod_3d_instance.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef VITA
/* GL_GLEXT_PROTOTYPES: real prototypes for GL 2.0+ calls (glUniform*, VAO/FBO
   functions). Without them float args to glUniform3f/1f get promoted to double
   and the uniforms receive garbage. */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#else
#include "libmod_ray_vita_gl.h"
#endif

/* ============================================================================
   GLOBAL RENDERER STATE
   ============================================================================
 */

static G3DRenderer g_renderer = {0};

/* ============================================================================
   INITIALIZATION
   ============================================================================
 */

int g3d_renderer_init(uint32_t width, uint32_t height) {
    if (g_renderer.initialized) {
        fprintf(stderr, "G3D: Renderer already initialized\n");
        return 0;
    }

    printf("G3D: Initializing renderer: %ux%u\n", width, height);

    memset(&g_renderer, 0, sizeof(G3DRenderer));

    g_renderer.display_width = width;
    g_renderer.display_height = height;

    /* Load shaders */
    g_renderer.phong_shader =
        (void *)g3d_shader_load_builtin(G3D_SHADER_PHONG);
    g_renderer.shadow_shader =
        (void *)g3d_shader_load_builtin(G3D_SHADER_SHADOW);

    if (!g_renderer.phong_shader || !g_renderer.shadow_shader) {
        fprintf(stderr, "G3D: Failed to load shaders\n");
        return 0;
    }

    /* Shadow mapping setup (1024² is plenty and much cheaper than 2048²) */
    g_renderer.shadow_enabled = 0;
    g_renderer.shadow_map_width = 1024;
    g_renderer.shadow_map_height = 1024;

#ifndef VITA
    /* Create shadow framebuffer */
    glGenFramebuffers(1, &g_renderer.shadow_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.shadow_framebuffer);

    /* Create shadow depth texture */
    glGenTextures(1, &g_renderer.shadow_texture);
    glBindTexture(GL_TEXTURE_2D, g_renderer.shadow_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                 g_renderer.shadow_map_width, g_renderer.shadow_map_height, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    /* White border so samples outside the light frustum read depth 1.0 (lit) */
    {
        float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    }

    /* Attach to framebuffer */
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                           g_renderer.shadow_texture, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "G3D: Shadow framebuffer incomplete: 0x%x\n", status);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Dynamic shadow maps (up to 4 torches/spotlights), same size as the directional one */
    for (int sc = 0; sc < 4; sc++) {
        glGenFramebuffers(1, &g_renderer.spot_shadow_framebuffer[sc]);
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.spot_shadow_framebuffer[sc]);
        glGenTextures(1, &g_renderer.spot_shadow_texture[sc]);
        glBindTexture(GL_TEXTURE_2D, g_renderer.spot_shadow_texture[sc]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT,
                     g_renderer.shadow_map_width, g_renderer.shadow_map_height, 0,
                     GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                               g_renderer.spot_shadow_texture[sc], 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "G3D: Spot shadow framebuffer %d incomplete\n", sc);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* Planar reflection colour target (display-sized) */
    glGenFramebuffers(1, &g_renderer.refl_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.refl_framebuffer);
    glGenTextures(1, &g_renderer.refl_texture);
    glBindTexture(GL_TEXTURE_2D, g_renderer.refl_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_renderer.refl_texture, 0);
    glGenRenderbuffers(1, &g_renderer.refl_depth);
    glBindRenderbuffer(GL_RENDERBUFFER, g_renderer.refl_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, g_renderer.refl_depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fprintf(stderr, "G3D: Reflection framebuffer incomplete\n");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    /* 1x1 white fallback albedo texture */
    {
        unsigned char white[4] = {255, 255, 255, 255};
        glGenTextures(1, &g_renderer.default_white_tex);
        glBindTexture(GL_TEXTURE_2D, g_renderer.default_white_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
#endif

    /* Default settings */
    g_renderer.frustum_culling_enabled = 1;
    g_renderer.ambient_color[0] = 0.2f;
    g_renderer.ambient_color[1] = 0.2f;
    g_renderer.ambient_color[2] = 0.2f;
    g_renderer.ambient_intensity = 1.0f;
    g_renderer.fog_enabled = 0;
    g_renderer.fog_color[0] = 0.6f;
    g_renderer.fog_color[1] = 0.7f;
    g_renderer.fog_color[2] = 0.8f;
    g_renderer.fog_start = 30.0f;
    g_renderer.fog_end = 120.0f;
    g_renderer.wireframe_mode = 0;
    g_renderer.show_normals = 0;
    g_renderer.show_bounding_boxes = 0;

    /* HDR post pipeline defaults (neutral tonemap so the base look is unchanged; the
       visible win is bloom). */
    g_renderer.hdr_active = 1;
    g_renderer.exposure = 1.0f;
    g_renderer.bloom_enabled = 1;
    g_renderer.bloom_amount = 0.5f;
    g_renderer.bloom_threshold = 0.8f;
    g_renderer.tonemap_mode = 0;
    g_renderer.ssao_enabled = 1;
    g_renderer.ssao_radius = 0.5f;
    g_renderer.ssao_power = 1.2f;

    g_renderer.initialized = 1;

#ifndef VITA
    {
        const char *ver = (const char *)glGetString(GL_VERSION);
        const char *ren = (const char *)glGetString(GL_RENDERER);
        GLint major = 0; glGetIntegerv(GL_MAJOR_VERSION, &major);
        printf("G3D: GL_VERSION = %s  |  GL_RENDERER = %s\n", ver ? ver : "?", ren ? ren : "?");
        printf("G3D: Tessellation (GL4) %s\n", major >= 4 ? "AVAILABLE" : "NOT available (needs GL 4.0)");
    }
#endif
    printf("G3D: Renderer initialized\n");

    return 1;
}

void g3d_renderer_shutdown(void) {
    if (!g_renderer.initialized)
        return;

    printf("G3D: Shutting down renderer\n");

#ifndef VITA
    if (g_renderer.shadow_framebuffer) {
        glDeleteFramebuffers(1, &g_renderer.shadow_framebuffer);
    }
    if (g_renderer.shadow_texture) {
        glDeleteTextures(1, &g_renderer.shadow_texture);
    }
#endif

    if (g_renderer.phong_shader) {
        g3d_shader_free((G3DShaderProgram *)g_renderer.phong_shader);
    }
    if (g_renderer.shadow_shader) {
        g3d_shader_free((G3DShaderProgram *)g_renderer.shadow_shader);
    }

    memset(&g_renderer, 0, sizeof(G3DRenderer));
}

/* ============================================================================
   CONFIGURATION
   ============================================================================
 */

void g3d_renderer_set_camera(G3DCamera *camera) {
    if (!camera)
        return;

    g_renderer.active_camera = camera;
}

void g3d_renderer_set_target(uint32_t fbo) {
    g_renderer.target_fbo = fbo;
    /* With HDR off the scene renders straight into the host FBO (legacy path). With HDR
       on, begin_frame points framebuffer at the internal HDR buffer and end resolves here. */
    if (!g_renderer.hdr_active) g_renderer.framebuffer = fbo;
    /* Default: flip Y when rendering into a non-zero FBO (the BennuGD GRAPH,
       sampled top-left by SDL_gpu). An external host (e.g. the Qt editor) can
       override with g3d_renderer_set_flip() after setting its own FBO target. */
    g_renderer.flip_y = (fbo != 0) ? 1 : 0;
}

void g3d_renderer_set_flip(int flip) {
    g_renderer.flip_y = flip ? 1 : 0;
}

void g3d_renderer_set_viewport_size(uint32_t w, uint32_t h) {
    if (w > 0 && h > 0) {
        g_renderer.display_width = w;
        g_renderer.display_height = h;
    }
}

void g3d_renderer_get_display_size(uint32_t *width, uint32_t *height) {
    if (width)
        *width = g_renderer.display_width;
    if (height)
        *height = g_renderer.display_height;
}

void g3d_renderer_set_viewport_physical(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    g_renderer.vp_x = x;
    g_renderer.vp_y = y;
    g_renderer.vp_w = w;
    g_renderer.vp_h = h;
}

void g3d_renderer_enable_shadows(int enabled, uint32_t resolution) {
    g_renderer.shadow_enabled = enabled;
    g_renderer.shadow_map_width = resolution;
    g_renderer.shadow_map_height = resolution;
}

void g3d_renderer_set_shadows(int enabled) {
    g_renderer.shadow_enabled = enabled;
}

void g3d_renderer_set_clear_color(float r, float g, float b, float a) {
    g_renderer.clear_color[0] = r;
    g_renderer.clear_color[1] = g;
    g_renderer.clear_color[2] = b;
    g_renderer.clear_color[3] = a;
#ifndef VITA
    glClearColor(r, g, b, a);
#endif
}

void g3d_renderer_set_wireframe_mode(int enabled) {
    g_renderer.wireframe_mode = enabled;

#ifndef VITA
    if (enabled) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    } else {
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    }
#endif
}

void g3d_renderer_set_frustum_culling(int enabled) {
    g_renderer.frustum_culling_enabled = enabled;
}

static int g_backface_cull = 0;
void g3d_renderer_set_backface_cull(int enabled) { g_backface_cull = enabled ? 1 : 0; }

/* Opacity + RGB tint for the mesh currently being drawn (set per entity by the
   forward pass; read by render_mesh -> uOpacity / uEntityColor). */
static float g_draw_opacity = 1.0f;
static float g_draw_tint[3] = { 1.0f, 1.0f, 1.0f };

/* A real blend mode (anything except NONE/NORMAL/DISABLED) must go through the
   transparent pass even at full alpha (e.g. additive glow). */
static int blend_is_special(int mode) {
    return !(mode == G3D_BLEND_NORMAL || mode == G3D_BLEND_NONE ||
             mode == G3D_BLEND_DISABLED);
}
#ifndef VITA
/* Set the GL blend state for an entity, replicating BennuGD's 2D blend modes
   (same src/dst factors + equation as gr_set_blend), incl. CUSTOM. */
static void apply_entity_blend(const G3DEntity *e) {
    int srgb, drgb, salpha, dalpha, ergb = GL_FUNC_ADD, ealpha = GL_FUNC_ADD;
    switch (e->blend_mode) {
        case G3D_BLEND_PREMULTIPLIED_ALPHA:
            srgb = GL_ONE;       drgb = GL_ONE_MINUS_SRC_ALPHA;
            salpha = GL_ONE;     dalpha = GL_ONE_MINUS_SRC_ALPHA; break;
        case G3D_BLEND_MULTIPLY:
            srgb = GL_DST_COLOR; drgb = GL_ZERO;
            salpha = GL_SRC_ALPHA; dalpha = GL_ONE_MINUS_SRC_ALPHA; break;
        case G3D_BLEND_ADD:
            srgb = GL_SRC_ALPHA; drgb = GL_ONE;
            salpha = GL_SRC_ALPHA; dalpha = GL_ONE; break;
        case G3D_BLEND_SUBTRACT:
            srgb = GL_ONE;       drgb = GL_ONE;   ergb = GL_FUNC_SUBTRACT;
            salpha = GL_ONE;     dalpha = GL_ONE; ealpha = GL_FUNC_SUBTRACT; break;
        case G3D_BLEND_MOD_ALPHA:
            srgb = GL_ZERO;      drgb = GL_ONE;
            salpha = GL_ZERO;    dalpha = GL_SRC_ALPHA; break;
        case G3D_BLEND_SET_ALPHA:
            srgb = GL_ZERO;      drgb = GL_ONE;
            salpha = GL_ONE;     dalpha = GL_ZERO; break;
        case G3D_BLEND_SET:
            srgb = GL_ONE;       drgb = GL_ZERO;
            salpha = GL_ONE;     dalpha = GL_ZERO; break;
        case G3D_BLEND_NORMAL_KEEP_ALPHA:
            srgb = GL_SRC_ALPHA; drgb = GL_ONE_MINUS_SRC_ALPHA;
            salpha = GL_ZERO;    dalpha = GL_ONE; break;
        case G3D_BLEND_NORMAL_ADD_ALPHA:
            srgb = GL_SRC_ALPHA; drgb = GL_ONE_MINUS_SRC_ALPHA;
            salpha = GL_ONE;     dalpha = GL_ONE; break;
        case G3D_BLEND_NORMAL_FACTOR_ALPHA:
            srgb = GL_SRC_ALPHA; drgb = GL_ONE_MINUS_SRC_ALPHA;
            salpha = GL_ONE_MINUS_DST_ALPHA; dalpha = GL_ONE; break;
        case G3D_BLEND_ALPHA_MASK:
            srgb = GL_ZERO;      drgb = GL_ONE;
            salpha = GL_ZERO;    dalpha = GL_ONE_MINUS_SRC_ALPHA; break;
        case G3D_BLEND_CUSTOM:  /* raw GL enums from the process' custom_blendmode */
            srgb = e->blend_custom[0]; drgb = e->blend_custom[1];
            salpha = e->blend_custom[2]; dalpha = e->blend_custom[3];
            ergb = e->blend_custom[4];   ealpha = e->blend_custom[5]; break;
        default: /* NORMAL / NONE / DISABLED -> standard alpha */
            srgb = GL_SRC_ALPHA; drgb = GL_ONE_MINUS_SRC_ALPHA;
            salpha = GL_SRC_ALPHA; dalpha = GL_ONE_MINUS_SRC_ALPHA; break;
    }
    glBlendEquationSeparate(ergb, ealpha);
    glBlendFuncSeparate(srgb, drgb, salpha, dalpha);
}
#endif

/* ============================================================================
   FRAME RENDERING
   ============================================================================
 */

#ifndef VITA
static void ensure_hdr_targets(uint32_t w, uint32_t h);
#endif

void g3d_renderer_begin_frame(void) {
    if (!g_renderer.initialized)
        return;

#ifndef VITA
    /* Refresh the IBL environment before binding our targets (it captures the
       sky into its own FBO). No-op unless the sky changed. */
    g3d_ibl_update();

    /* HDR on: the scene renders into the internal RGBA16F buffer; end resolves to the host
       FBO. HDR off: render straight into the host FBO (legacy path). */
    if (g_renderer.hdr_active) {
        ensure_hdr_targets(g_renderer.display_width, g_renderer.display_height);
        g_renderer.framebuffer = g_renderer.hdr_ready ? g_renderer.hdr_fbo : g_renderer.target_fbo;
    } else {
        g_renderer.framebuffer = g_renderer.target_fbo;
    }
    /* Bind target framebuffer (0 = screen, or a GRAPH-backed FBO) */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.framebuffer);
    if (g_renderer.framebuffer == g_renderer.target_fbo) {
        glViewport(g_renderer.vp_x, g_renderer.vp_y, g_renderer.vp_w, g_renderer.vp_h);
    } else {
        glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    }

    /* SDL_gpu leaves GL state that breaks raw rendering: scissor test may be
       enabled (clipping everything), clear color/masks may be changed. Reset
       the state we depend on every frame. */
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    /* Backface culling is OFF by default (everything is two-sided). Maps with
       single-sided quads (GTA-style signs) enable it so you don't see mirrored
       backs. flip_y (GRAPH render) inverts winding, so pick the front face to match. */
    if (g_backface_cull) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(g_renderer.flip_y ? GL_CW : GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }

    /* Re-apply our clear color (SDL_gpu may have reset it) and clear */
    glClearColor(g_renderer.clear_color[0], g_renderer.clear_color[1],
                 g_renderer.clear_color[2], g_renderer.clear_color[3]);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* Enable depth testing */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
#endif

    /* Reset stats */
    g_renderer.draw_calls = 0;
    g_renderer.triangles_rendered = 0;
    g_renderer.entities_culled = 0;
}

void g3d_renderer_shadow_pass(void) {
    if (!g_renderer.initialized || !g_renderer.shadow_enabled)
        return;

#ifndef VITA
    /* Find the main directional light to cast from */
    int light_count = 0;
    int *light_ids = g3d_scene_impl_get_lights(&light_count);
    G3DLight *main_light = NULL;
    for (int i = 0; i < light_count; i++) {
        G3DLight *l = g3d_light_impl_get(light_ids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            main_light = l;
            break;
        }
    }
    if (!main_light)
        return;

    /* Build the light-space view-projection: an orthographic frustum looking
       along the light direction, covering a fixed area around the origin. */
    Vec3 dir = vec3_normalize(main_light->direction);
    /* Center the shadow region on the camera (ground level) so the player's
       surroundings cast shadows in a big open world, not just the origin. */
    Vec3 center = vec3_make(0.0f, 0.0f, 0.0f);
    if (g_renderer.active_camera) {
        center.x = g_renderer.active_camera->position.x;
        center.z = g_renderer.active_camera->position.z;
    }
    float extent = 60.0f;    /* half-size of the covered area */
    float distance = 120.0f; /* how far back the light "eye" sits */
    Vec3 eye = vec3_sub(center, vec3_scale(dir, distance));
    Vec3 up = (fabsf(dir.y) > 0.99f) ? vec3_make(0, 0, 1) : vec3_make(0, 1, 0);

    Mat4 light_view = mat4_look_at(eye, center, up);
    Mat4 light_proj =
        mat4_ortho(-extent, extent, -extent, extent, 1.0f, distance * 2.0f);
    g_renderer.light_space_matrix = mat4_multiply(light_proj, light_view);

    /* Render scene depth into the shadow framebuffer */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.shadow_framebuffer);
    glViewport(0, 0, g_renderer.shadow_map_width, g_renderer.shadow_map_height);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    /* Depth bias to avoid shadow acne */
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    G3DShaderProgram *sh = (G3DShaderProgram *)g_renderer.shadow_shader;
    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uLightSpaceMatrix", g_renderer.light_space_matrix);

    int entity_count = 0;
    int *entities = g3d_scene_impl_get_entities(&entity_count);
    for (int i = 0; i < entity_count; i++) {
        G3DEntity *e = g3d_entity_impl_get(entities[i]);
        if (!e || !e->active || !e->mesh)
            continue;
        G3DMesh *m = (G3DMesh *)e->mesh;
        Mat4 world = g3d_entity_impl_get_world_matrix(entities[i]);
        g3d_shader_set_mat4(sh, "uModel", world);
        glBindVertexArray(m->vao);
        glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);

    /* Instanced vegetation/props also cast shadows */
    g3d_instances_render_depth(g_renderer.light_space_matrix);

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
#endif
}

/* Render scene depth from the first spot light's point of view, so its lit cone
   can be tested for occlusion (moving shadows that follow the flashlight). */
void g3d_renderer_spot_shadow_pass(void) {
    g_renderer.num_shadow_casters = 0;
    if (!g_renderer.initialized || !g_renderer.shadow_enabled)
        return;

#ifndef VITA
    int light_count = 0;
    int *light_ids = g3d_scene_impl_get_lights(&light_count);

    /* Pick up to 4 shadow casters: any spotlight, then the nearest point lights (torches)
       to the camera, so several torches cast shadows at once (no sudden pop-in). */
    Vec3 cam = g_renderer.active_camera ? g_renderer.active_camera->position : vec3_make(0, 0, 0);
    int cand_id[4]; float cand_d[4]; int ncand = 0;
    for (int i = 0; i < light_count; i++) {
        G3DLight *l = g3d_light_impl_get(light_ids[i]);
        if (!l || !l->active || l->intensity <= 0.0f) continue;
        if (l->type != G3D_LIGHT_TYPE_SPOT && l->type != G3D_LIGHT_TYPE_POINT) continue;
        float dd;
        if (l->type == G3D_LIGHT_TYPE_SPOT) dd = -1.0f;   /* spots always included */
        else { Vec3 v = vec3_sub(l->position, cam); dd = v.x*v.x + v.y*v.y + v.z*v.z; }
        if (ncand < 4) { cand_id[ncand] = light_ids[i]; cand_d[ncand] = dd; ncand++; }
        else {
            int worst = 0; for (int k = 1; k < 4; k++) if (cand_d[k] > cand_d[worst]) worst = k;
            if (dd < cand_d[worst]) { cand_id[worst] = light_ids[i]; cand_d[worst] = dd; }
        }
    }
    if (ncand == 0) return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    G3DShaderProgram *sh = (G3DShaderProgram *)g_renderer.shadow_shader;
    g3d_shader_use(sh);
    int entity_count = 0;
    int *entities = g3d_scene_impl_get_entities(&entity_count);

    for (int c = 0; c < ncand; c++) {
        G3DLight *l = g3d_light_impl_get(cand_id[c]);
        if (!l) continue;
        int is_point = (l->type == G3D_LIGHT_TYPE_POINT);
        Vec3 eye = l->position;
        Vec3 dir = is_point ? vec3_make(0.0f, -1.0f, 0.0f) : vec3_normalize(l->direction);
        float fov = is_point ? 150.0f : (l->cone_angle + 8.0f);
        if (fov > 175.0f) fov = 175.0f;
        float range = (l->range > 1.0f) ? l->range : 30.0f;
        Vec3 target = vec3_add(eye, dir);
        Vec3 up = (fabsf(dir.y) > 0.99f) ? vec3_make(0, 0, 1) : vec3_make(0, 1, 0);
        Mat4 view = mat4_look_at(eye, target, up);
        Mat4 proj = mat4_perspective(fov, 1.0f, 0.5f, range);
        g_renderer.spot_light_space_matrix[c] = mat4_multiply(proj, view);
        g_renderer.shadow_caster_id[c] = cand_id[c];

        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.spot_shadow_framebuffer[c]);
        glViewport(0, 0, g_renderer.shadow_map_width, g_renderer.shadow_map_height);
        glClear(GL_DEPTH_BUFFER_BIT);
        g3d_shader_set_mat4(sh, "uLightSpaceMatrix", g_renderer.spot_light_space_matrix[c]);
        for (int i = 0; i < entity_count; i++) {
            G3DEntity *e = g3d_entity_impl_get(entities[i]);
            if (!e || !e->active || !e->mesh) continue;
            G3DMesh *m = (G3DMesh *)e->mesh;
            Mat4 world = g3d_entity_impl_get_world_matrix(entities[i]);
            if (is_point) {
                float dx = world.m[12] - eye.x, dz = world.m[14] - eye.z;
                if (dx * dx + dz * dz < 9.0f) continue;   /* the torch that owns this light */
            }
            g3d_shader_set_mat4(sh, "uModel", world);
            glBindVertexArray(m->vao);
            glDrawElements(GL_TRIANGLES, m->index_count, GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
    }

    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    g_renderer.num_shadow_casters = ncand;
#endif
}

uint32_t g3d_renderer_reflection_texture(void) { return g_renderer.refl_texture; }
uint32_t g3d_renderer_scene_texture(void) { return g_renderer.scene_texture; }

uint32_t g3d_renderer_capture_scene(void) {
#ifndef VITA
    uint32_t w = g_renderer.display_width, h = g_renderer.display_height;
    if (w == 0 || h == 0) return 0;
    if (!g_renderer.scene_texture) {
        glGenTextures(1, &g_renderer.scene_texture);
        glBindTexture(GL_TEXTURE_2D, g_renderer.scene_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_renderer.scene_tex_w = 0; g_renderer.scene_tex_h = 0;
    }
    glBindTexture(GL_TEXTURE_2D, g_renderer.scene_texture);
    /* Copy the current colour buffer (the opaque scene) into the texture. */
    if (w != g_renderer.scene_tex_w || h != g_renderer.scene_tex_h) {
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 0, 0, (GLsizei)w, (GLsizei)h, 0);
        g_renderer.scene_tex_w = w; g_renderer.scene_tex_h = h;
    } else {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)w, (GLsizei)h);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return g_renderer.scene_texture;
#else
    return 0;
#endif
}

uint32_t g3d_renderer_capture_depth(void) {
#ifndef VITA
    uint32_t w = g_renderer.display_width, h = g_renderer.display_height;
    if (w == 0 || h == 0) return 0;
    if (!g_renderer.scene_depth_tex) {
        glGenTextures(1, &g_renderer.scene_depth_tex);
        glBindTexture(GL_TEXTURE_2D, g_renderer.scene_depth_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        g_renderer.scene_depth_w = 0; g_renderer.scene_depth_h = 0;
    }
    glBindTexture(GL_TEXTURE_2D, g_renderer.scene_depth_tex);
    if (w != g_renderer.scene_depth_w || h != g_renderer.scene_depth_h) {
        glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 0, 0, (GLsizei)w, (GLsizei)h, 0);
        g_renderer.scene_depth_w = w; g_renderer.scene_depth_h = h;
    } else {
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, (GLsizei)w, (GLsizei)h);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return g_renderer.scene_depth_tex;
#else
    return 0;
#endif
}
uint32_t g3d_renderer_scene_depth_texture(void) { return g_renderer.scene_depth_tex; }

void g3d_renderer_set_underwater(int on, float r, float g, float b, float strength) {
    g_renderer.underwater = on ? 1 : 0;
    g_renderer.underwater_tint[0] = r;
    g_renderer.underwater_tint[1] = g;
    g_renderer.underwater_tint[2] = b;
    g_renderer.underwater_strength = strength > 0.0f ? strength : 1.0f;
}

#ifndef VITA
static const char *post_vert =
    "#version 330 core\n"
    "layout(location=0) in vec2 p;\n"
    "out vec2 vUV;\n"
    "void main(){ vUV = p * 0.5 + 0.5; gl_Position = vec4(p, 0.0, 1.0); }\n";
static const char *post_frag_underwater =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uScene; uniform float uTime; uniform vec3 uTint; uniform float uStr;\n"
    "void main(){\n"
    "  vec2 uv = vUV;\n"
    "  uv.x += sin(uv.y * 26.0 + uTime * 2.0) * 0.006 * uStr;\n"
    "  uv.y += cos(uv.x * 30.0 + uTime * 1.7) * 0.006 * uStr;\n"
    "  vec3 c = texture(uScene, clamp(uv, 0.001, 0.999)).rgb;\n"
    "  c = mix(c, uTint, 0.38 * uStr);\n"            // water tint
    "  float d = distance(vUV, vec2(0.5));\n"
    "  c *= 1.0 - d * 0.7 * uStr;\n"                 // darken the edges
    "  F = vec4(c, 1.0);\n"
    "}\n";

/* Shared fullscreen triangle (post passes). */
static void ensure_fs_quad(void) {
    if (g_renderer.post_vao) return;
    float quad[] = { -1, -1,  3, -1,  -1, 3 };   /* one big triangle covers the screen */
    glGenVertexArrays(1, &g_renderer.post_vao);
    glGenBuffers(1, &g_renderer.post_vbo);
    glBindVertexArray(g_renderer.post_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_renderer.post_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);
}

/* Fullscreen underwater pass: grab the frame and redraw it tinted + wobbling. */
void g3d_renderer_underwater_pass(void) {
    if (!g_renderer.underwater) return;
    if (!g_renderer.post_shader)
        g_renderer.post_shader = g3d_shader_create(post_vert, post_frag_underwater);
    ensure_fs_quad();
    if (!g_renderer.post_shader) return;
    /* Draw into the scene framebuffer (HDR fbo when HDR is on); a prior pass may
       have left another FBO bound, which would send the tint nowhere visible. */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.framebuffer);
    glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    g3d_renderer_capture_scene();   /* grab the current frame */
    G3DShaderProgram *sh = (G3DShaderProgram *)g_renderer.post_shader;
    g3d_shader_use(sh);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g_renderer.scene_texture);
    g3d_shader_set_int(sh, "uScene", 0);
    g3d_shader_set_float(sh, "uTime", (float)SDL_GetTicks() / 1000.0f);
    g3d_shader_set_float(sh, "uStr", g_renderer.underwater_strength);
    g3d_shader_set_vec3(sh, "uTint", vec3_make(g_renderer.underwater_tint[0],
                                               g_renderer.underwater_tint[1],
                                               g_renderer.underwater_tint[2]));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glBindVertexArray(g_renderer.post_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}
/* ---- HDR + bloom post pipeline (GL3.3) ---------------------------------- */
static const char *hdr_bright_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F; uniform sampler2D uHDR; uniform float uThresh;\n"
    "void main(){ vec3 c = texture(uHDR, vUV).rgb;\n"
    "  float l = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
    "  float k = max(l - uThresh, 0.0);\n"
    "  F = vec4(c * (k / max(l, 1e-4)), 1.0); }\n";
static const char *hdr_blur_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F; uniform sampler2D uTex; uniform float uDirX; uniform float uDirY;\n"
    "void main(){ float w[5] = float[](0.227027, 0.194595, 0.121622, 0.054054, 0.016216);\n"
    "  vec2 d = vec2(uDirX, uDirY);\n"
    "  vec3 c = texture(uTex, vUV).rgb * w[0];\n"
    "  for (int i = 1; i < 5; i++) {\n"
    "    c += texture(uTex, vUV + d * float(i)).rgb * w[i];\n"
    "    c += texture(uTex, vUV - d * float(i)).rgb * w[i]; }\n"
    "  F = vec4(c, 1.0); }\n";
static const char *hdr_resolve_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uHDR; uniform sampler2D uBloom; uniform sampler2D uAO;\n"
    "uniform float uExposure; uniform float uBloomAmt; uniform int uTonemap; uniform int uUseAO;\n"
    "vec3 aces(vec3 x){ return clamp((x*(2.51*x+0.03))/(x*(2.43*x+0.59)+0.14), 0.0, 1.0); }\n"
    "void main(){\n"
    "  float ao = (uUseAO == 1) ? mix(1.0, texture(uAO, vUV).r, 0.75) : 1.0;\n"   /* 75% strength: darken crevices, keep open areas clean */
    "  vec3 hdr = texelFetch(uHDR, ivec2(gl_FragCoord.xy), 0).rgb;\n"   /* 1:1, no bilinear softening */
    "  vec3 c = hdr * uExposure * ao + texture(uBloom, vUV).rgb * uBloomAmt;\n"
    "  c = (uTonemap == 1) ? aces(c) : min(c, vec3(1.0));\n"
    "  F = vec4(c, 1.0); }\n";

static void ensure_hdr_targets(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    if (!g_renderer.hdr_fbo) {
        glGenFramebuffers(1, &g_renderer.hdr_fbo);
        glGenTextures(1, &g_renderer.hdr_color);
        glGenTextures(1, &g_renderer.hdr_depth);
        glGenFramebuffers(2, g_renderer.bloom_fbo);
        glGenTextures(2, g_renderer.bloom_tex);
    }
    if (w != g_renderer.hdr_w || h != g_renderer.hdr_h) {
        glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        /* depth as a sampleable texture (SSAO reads it) */
        glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_depth);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, w, h, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.hdr_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_renderer.hdr_color, 0);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, g_renderer.hdr_depth, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            fprintf(stderr, "G3D: HDR framebuffer incomplete\n");
        uint32_t bw = w / 2 ? w / 2 : 1, bh = h / 2 ? h / 2 : 1;
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, g_renderer.bloom_tex[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bw, bh, 0, GL_RGBA, GL_FLOAT, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.bloom_fbo[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_renderer.bloom_tex[i], 0);
        }
        g_renderer.hdr_w = w; g_renderer.hdr_h = h; g_renderer.bloom_w = bw; g_renderer.bloom_h = bh;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    g_renderer.hdr_ready = 1;
}

void g3d_renderer_resolve_hdr(void) {
    if (!g_renderer.hdr_active || !g_renderer.hdr_ready) return;
    if (!g_renderer.tonemap_shader) {
        g_renderer.tonemap_shader = g3d_shader_create(post_vert, hdr_resolve_frag);
        g_renderer.bright_shader  = g3d_shader_create(post_vert, hdr_bright_frag);
        g_renderer.blur_shader    = g3d_shader_create(post_vert, hdr_blur_frag);
        ensure_fs_quad();
    }
    if (!g_renderer.tonemap_shader) return;
    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glBindVertexArray(g_renderer.post_vao);

    int bloom_src = 0;
    if (g_renderer.bloom_enabled && g_renderer.bright_shader && g_renderer.blur_shader) {
        /* bright pass: HDR -> bloom_tex[0] at half res */
        glViewport(0, 0, g_renderer.bloom_w, g_renderer.bloom_h);
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.bloom_fbo[0]);
        G3DShaderProgram *bs = (G3DShaderProgram *)g_renderer.bright_shader;
        g3d_shader_use(bs);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_color);
        g3d_shader_set_int(bs, "uHDR", 0);
        g3d_shader_set_float(bs, "uThresh", g_renderer.bloom_threshold);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        /* separable Gaussian blur, ping-pong */
        G3DShaderProgram *bl = (G3DShaderProgram *)g_renderer.blur_shader;
        g3d_shader_use(bl);
        int src = 0, dst = 1;
        for (int i = 0; i < 6; i++) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.bloom_fbo[dst]);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.bloom_tex[src]);
            g3d_shader_set_int(bl, "uTex", 0);
            g3d_shader_set_float(bl, "uDirX", (i & 1) ? 0.0f : 1.0f / (float)g_renderer.bloom_w);
            g3d_shader_set_float(bl, "uDirY", (i & 1) ? 1.0f / (float)g_renderer.bloom_h : 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            int t = src; src = dst; dst = t;
        }
        bloom_src = src;
    }

    /* resolve: HDR + bloom -> host target */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.target_fbo);
    if (g_renderer.target_fbo == 0) {
        glViewport(g_renderer.vp_x, g_renderer.vp_y, g_renderer.vp_w, g_renderer.vp_h);
    } else {
        glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    }
    G3DShaderProgram *ts = (G3DShaderProgram *)g_renderer.tonemap_shader;
    g3d_shader_use(ts);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_color);
    g3d_shader_set_int(ts, "uHDR", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_renderer.bloom_tex[bloom_src]);
    g3d_shader_set_int(ts, "uBloom", 1);
    int use_ao = (g_renderer.ssao_enabled && g_renderer.ssao_blur_tex) ? 1 : 0;
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, use_ao ? g_renderer.ssao_blur_tex : g_renderer.default_white_tex);
    g3d_shader_set_int(ts, "uAO", 2);
    g3d_shader_set_int(ts, "uUseAO", use_ao);
    g3d_shader_set_float(ts, "uExposure", g_renderer.exposure);
    g3d_shader_set_float(ts, "uBloomAmt", g_renderer.bloom_enabled ? g_renderer.bloom_amount : 0.0f);
    g3d_shader_set_int(ts, "uTonemap", g_renderer.tonemap_mode);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

/* ---- SSAO (from the depth texture; normals reconstructed via derivatives) ---- */
#define SSAO_KERNEL 32
static const char *ssao_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uDepth; uniform sampler2D uNoise;\n"
    "uniform mat4 uProj; uniform mat4 uInvProj;\n"
    "uniform vec3 uKernel[32]; uniform float uNoiseScaleX; uniform float uNoiseScaleY;\n"
    "uniform float uRadius; uniform float uPower;\n"
    "vec3 viewpos(vec2 uv){ float d = texture(uDepth, uv).r;\n"
    "  vec4 ndc = vec4(uv*2.0-1.0, d*2.0-1.0, 1.0); vec4 v = uInvProj*ndc; return v.xyz/v.w; }\n"
    "void main(){\n"
    "  float d = texture(uDepth, vUV).r;\n"
    "  if (d >= 1.0) { F = vec4(1.0); return; }\n"           /* background: no AO */
    "  vec3 pos = viewpos(vUV);\n"
    "  vec3 n = normalize(cross(dFdx(pos), dFdy(pos)));\n"
    "  if (dot(n, pos) > 0.0) n = -n;\n"
    "  vec3 rnd = normalize(texture(uNoise, vUV*vec2(uNoiseScaleX, uNoiseScaleY)).xyz);\n"
    "  vec3 tangent = normalize(rnd - n*dot(rnd,n));\n"
    "  mat3 TBN = mat3(tangent, cross(n, tangent), n);\n"
    "  float occ = 0.0;\n"
    "  for (int i = 0; i < 32; i++) {\n"
    "    vec3 sp = pos + (TBN * uKernel[i]) * uRadius;\n"
    "    vec4 off = uProj * vec4(sp, 1.0); off.xyz /= off.w; off.xyz = off.xyz*0.5+0.5;\n"
    "    if (off.x<0.0||off.x>1.0||off.y<0.0||off.y>1.0) continue;\n"
    "    vec4 sndc = vec4(off.xy*2.0-1.0, texture(uDepth, off.xy).r*2.0-1.0, 1.0);\n"
    "    vec4 sv = uInvProj*sndc; float sz = sv.z/sv.w;\n"
    "    float rc = smoothstep(0.0, 1.0, uRadius / max(abs(pos.z - sz), 1e-4));\n"
    "    occ += (sz >= sp.z + 0.045 ? 1.0 : 0.0) * rc;\n"   /* larger bias: flat bumpy ground doesn't self-occlude */
    "  }\n"
    "  occ = 1.0 - occ/32.0;\n"
    "  F = vec4(vec3(pow(clamp(occ,0.0,1.0), uPower)), 1.0); }\n";
static const char *ssao_blur_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F; uniform sampler2D uTex; uniform float uTexelX; uniform float uTexelY;\n"
    "void main(){ float r = 0.0; vec2 tx = vec2(uTexelX, uTexelY);\n"
    "  for (int x=-2;x<2;x++) for (int y=-2;y<2;y++) r += texture(uTex, vUV + vec2(float(x),float(y))*tx).r;\n"
    "  F = vec4(vec3(r/16.0), 1.0); }\n";

void g3d_renderer_ssao_pass(void) {
    if (!g_renderer.hdr_active || !g_renderer.ssao_enabled || !g_renderer.hdr_ready) return;
    if (!g_renderer.active_camera) return;
    uint32_t sw = g_renderer.display_width, sh = g_renderer.display_height;   /* full res = sharp AO */
    if (sw == 0 || sh == 0) return;

    static float kernel[SSAO_KERNEL * 3]; static int kernel_built = 0;
    if (!g_renderer.ssao_shader) {
        g_renderer.ssao_shader = g3d_shader_create(post_vert, ssao_frag);
        g_renderer.ssao_blur_shader = g3d_shader_create(post_vert, ssao_blur_frag);
        ensure_fs_quad();
        float noise[16 * 3];
        for (int i = 0; i < 16; i++) {
            noise[i*3+0] = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            noise[i*3+1] = (float)rand() / RAND_MAX * 2.0f - 1.0f;
            noise[i*3+2] = 0.0f;
        }
        glGenTextures(1, &g_renderer.ssao_noise_tex);
        glBindTexture(GL_TEXTURE_2D, g_renderer.ssao_noise_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, 4, 4, 0, GL_RGB, GL_FLOAT, noise);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }
    if (!g_renderer.ssao_shader) return;
    if (!kernel_built) {
        for (int i = 0; i < SSAO_KERNEL; i++) {
            float x = (float)rand()/RAND_MAX*2.0f-1.0f, y = (float)rand()/RAND_MAX*2.0f-1.0f, z = (float)rand()/RAND_MAX;
            float len = sqrtf(x*x+y*y+z*z); if (len < 1e-4f) len = 1.0f;
            float t = (float)i / SSAO_KERNEL, scale = 0.1f + (1.0f-0.1f)*t*t;   /* cluster near origin */
            scale *= (float)rand()/RAND_MAX;
            kernel[i*3+0] = x/len*scale; kernel[i*3+1] = y/len*scale; kernel[i*3+2] = z/len*scale;
        }
        kernel_built = 1;
    }
    if (!g_renderer.ssao_fbo || sw != g_renderer.ssao_w || sh != g_renderer.ssao_h) {
        if (!g_renderer.ssao_fbo) {
            glGenFramebuffers(1, &g_renderer.ssao_fbo); glGenTextures(1, &g_renderer.ssao_tex);
            glGenFramebuffers(1, &g_renderer.ssao_blur_fbo); glGenTextures(1, &g_renderer.ssao_blur_tex);
        }
        uint32_t fb[2] = { g_renderer.ssao_fbo, g_renderer.ssao_blur_fbo };
        uint32_t tx[2] = { g_renderer.ssao_tex, g_renderer.ssao_blur_tex };
        for (int i = 0; i < 2; i++) {
            glBindTexture(GL_TEXTURE_2D, tx[i]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, sw, sh, 0, GL_RED, GL_UNSIGNED_BYTE, NULL);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindFramebuffer(GL_FRAMEBUFFER, fb[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tx[i], 0);
        }
        g_renderer.ssao_w = sw; g_renderer.ssao_h = sh;
    }

    Mat4 proj = g3d_camera_get_projection(g_renderer.active_camera);
    if (g_renderer.flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 invproj = mat4_inverse(proj);

    glDisable(GL_DEPTH_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glBindVertexArray(g_renderer.post_vao);
    glViewport(0, 0, sw, sh);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.ssao_fbo);
    G3DShaderProgram *sh_ao = (G3DShaderProgram *)g_renderer.ssao_shader;
    g3d_shader_use(sh_ao);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_depth); g3d_shader_set_int(sh_ao, "uDepth", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g_renderer.ssao_noise_tex); g3d_shader_set_int(sh_ao, "uNoise", 1);
    g3d_shader_set_mat4(sh_ao, "uProj", proj);
    g3d_shader_set_mat4(sh_ao, "uInvProj", invproj);
    for (int i = 0; i < SSAO_KERNEL; i++) {
        char nm[24]; snprintf(nm, sizeof(nm), "uKernel[%d]", i);
        g3d_shader_set_vec3(sh_ao, nm, vec3_make(kernel[i*3], kernel[i*3+1], kernel[i*3+2]));
    }
    g3d_shader_set_float(sh_ao, "uRadius", g_renderer.ssao_radius);
    g3d_shader_set_float(sh_ao, "uPower", g_renderer.ssao_power);
    g3d_shader_set_float(sh_ao, "uNoiseScaleX", (float)sw / 4.0f);
    g3d_shader_set_float(sh_ao, "uNoiseScaleY", (float)sh / 4.0f);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* blur */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.ssao_blur_fbo);
    G3DShaderProgram *sh_bl = (G3DShaderProgram *)g_renderer.ssao_blur_shader;
    g3d_shader_use(sh_bl);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.ssao_tex); g3d_shader_set_int(sh_bl, "uTex", 0);
    g3d_shader_set_float(sh_bl, "uTexelX", 1.0f / (float)sw);
    g3d_shader_set_float(sh_bl, "uTexelY", 1.0f / (float)sh);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.framebuffer);
    glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    glEnable(GL_DEPTH_TEST);
}

/* ---- world-space volumetric LOW clouds (fly-through, depth-aware) ---- */
static const char *cloud_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uDepth;\n"
    "uniform mat4 uInvViewProj; uniform vec3 uCamPos;\n"
    "uniform vec3 uSunDir; uniform vec3 uSunColor; uniform vec3 uAmbient; uniform float uTime;\n"
    G3D_CLOUD_GLSL
    "void main(){\n"
    "  vec2 uv = vUV;\n"
    "  vec4 nf = uInvViewProj*vec4(uv*2.0-1.0, 1.0, 1.0); vec3 far = nf.xyz/nf.w;\n"
    "  vec3 rd = normalize(far - uCamPos);\n"
    "  float sd = texture(uDepth, uv).r; float maxT;\n"
    "  if (sd < 1.0){ vec4 ns=uInvViewProj*vec4(uv*2.0-1.0, sd*2.0-1.0, 1.0); vec3 ws=ns.xyz/ns.w; maxT=length(ws-uCamPos); }\n"
    "  else maxT = 8000.0;\n"
    "  float yb=uCloudBase, yt=uCloudBase+uCloudThick;\n"
    "  float T=1.0; vec3 scat=vec3(0.0);\n"
    "  if (abs(rd.y) > 1e-4){\n"
    "    float ta=(yb-uCamPos.y)/rd.y, tb=(yt-uCamPos.y)/rd.y;\n"
    "    float d0=max(min(ta,tb),0.0), d1=min(max(ta,tb),maxT);\n"
    "    if (d1>d0){\n"
    "      int N=26; float dtS=(d1-d0)/float(N);\n"
    "      float jit=h13(vec3(gl_FragCoord.xy,uTime))*dtS;\n"
    "      vec3 sunN=normalize(uSunDir); vec3 sunC=uSunColor*3.0;\n"
    "      for(int i=0;i<N;i++){\n"
    "        float t=d0+(float(i)+0.5)*dtS+jit; vec3 p=uCamPos+rd*t;\n"
    "        float dens=lowCloudDensity(p);\n"
    "        if (dens>0.01){\n"
    "          float ls=0.0; float lstep=uCloudThick*0.24;\n"
    "          for(int j=0;j<3;j++) ls+=lowCloudDensity(p+sunN*lstep*float(j+1));\n"
    "          float light=exp(-ls*0.9);\n"
    "          float hg=0.5+0.5*pow(max(dot(rd,sunN),0.0),6.0);\n"
    "          vec3 cc=uAmbient*0.7 + sunC*light*hg;\n"
    "          float a=dens*dtS*0.05;\n"
    "          scat += T*a*cc; T*=exp(-dens*dtS*0.05);\n"
    "          if (T<0.02) break;\n"
    "        }\n"
    "      }\n"
    "    }\n"
    "  }\n"
    "  F = vec4(scat, T);\n"                 /* blend GL_ONE, GL_SRC_ALPHA -> dst*T + scat */
    "}\n";

/* Upscale composite: samples the half-res (scat,T) target; the GL_ONE,GL_SRC_ALPHA
   blend then does dst*T + scat, identical to the full-res path. */
static const char *cloud_composite_frag =
    "#version 330 core\n"
    "in vec2 vUV; out vec4 F;\n"
    "uniform sampler2D uClouds;\n"
    "void main(){ F = texture(uClouds, vUV); }\n";

void g3d_renderer_cloud_pass(void) {
    if (!g_renderer.hdr_active || !g_renderer.hdr_ready || !g_renderer.active_camera) return;
    float cover, base, thick, speed;
    if (!g3d_sky_low_clouds(&cover, &base, &thick, &speed)) return;   /* off -> skip */

    static G3DShaderProgram *sh = NULL, *csh = NULL;
    if (!sh)  { sh = g3d_shader_create(post_vert, cloud_frag); ensure_fs_quad(); }
    if (!csh) { csh = g3d_shader_create(post_vert, cloud_composite_frag); }
    if (!sh || !csh) return;

    /* Half-res target (1/4 the pixels for the expensive raymarch). */
    uint32_t hw = g_renderer.display_width / 2, hh = g_renderer.display_height / 2;
    if (hw < 1) hw = 1; if (hh < 1) hh = 1;
    if (!g_renderer.cloud_fbo || g_renderer.cloud_w != hw || g_renderer.cloud_h != hh) {
        if (!g_renderer.cloud_fbo) glGenFramebuffers(1, &g_renderer.cloud_fbo);
        if (!g_renderer.cloud_tex) glGenTextures(1, &g_renderer.cloud_tex);
        glBindTexture(GL_TEXTURE_2D, g_renderer.cloud_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, hw, hh, 0, GL_RGBA, GL_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.cloud_fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_renderer.cloud_tex, 0);
        g_renderer.cloud_w = hw; g_renderer.cloud_h = hh;
    }

    Mat4 proj = g3d_camera_get_projection(g_renderer.active_camera);
    if (g_renderer.flip_y) { proj.m[1]=-proj.m[1]; proj.m[5]=-proj.m[5]; proj.m[9]=-proj.m[9]; proj.m[13]=-proj.m[13]; }
    Mat4 view = g3d_camera_get_view(g_renderer.active_camera);
    Mat4 invvp = mat4_inverse(mat4_multiply(proj, view));
    Mat4 invview = mat4_inverse(view);
    Vec3 cam = vec3_make(invview.m[12], invview.m[13], invview.m[14]);   /* world camera pos */
    float sdir[3], scol[3], amb[3];
    g3d_sky_get_sun(sdir, scol); g3d_sky_get_ambient(amb);

    /* Pass 1: raymarch into the half-res target (overwrite, no blend). */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.cloud_fbo);
    glViewport(0, 0, hw, hh);
    glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); glClear(GL_COLOR_BUFFER_BIT);   /* scat=0, T=1 */
    glBindVertexArray(g_renderer.post_vao);
    g3d_shader_use(sh);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.hdr_depth); g3d_shader_set_int(sh, "uDepth", 0);
    g3d_shader_set_mat4(sh, "uInvViewProj", invvp);
    g3d_shader_set_vec3(sh, "uCamPos", cam);
    g3d_shader_set_vec3(sh, "uSunDir", vec3_make(sdir[0], sdir[1], sdir[2]));
    g3d_shader_set_vec3(sh, "uSunColor", vec3_make(scol[0], scol[1], scol[2]));
    g3d_shader_set_vec3(sh, "uAmbient", vec3_make(amb[0], amb[1], amb[2]));
    g3d_shader_set_float(sh, "uTime", (float)SDL_GetTicks() / 1000.0f);
    g3d_shader_set_float(sh, "uCloudCover", cover);
    g3d_shader_set_float(sh, "uCloudSpeed", speed);
    g3d_shader_set_float(sh, "uCloudBase", base);
    g3d_shader_set_float(sh, "uCloudThick", thick);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    /* Pass 2: composite (bilinear upscale) into HDR with the cloud blend. */
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.hdr_fbo);
    glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_SRC_ALPHA);   /* dst*T + scat */
    g3d_shader_use(csh);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g_renderer.cloud_tex); g3d_shader_set_int(csh, "uClouds", 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); glDisable(GL_BLEND);
    glActiveTexture(GL_TEXTURE0); glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.framebuffer);
    glEnable(GL_DEPTH_TEST);
}
#else
void g3d_renderer_underwater_pass(void) {}
void g3d_renderer_resolve_hdr(void) {}
void g3d_renderer_ssao_pass(void) {}
void g3d_renderer_cloud_pass(void) {}
#endif

void g3d_renderer_set_hdr(int enabled) { g_renderer.hdr_active = enabled ? 1 : 0; }
void g3d_renderer_set_exposure(float e) { g_renderer.exposure = e > 0.0f ? e : 0.0f; }
void g3d_renderer_set_bloom(int enabled, float amount, float threshold) {
    g_renderer.bloom_enabled = enabled ? 1 : 0;
    if (amount >= 0.0f) g_renderer.bloom_amount = amount;
    if (threshold >= 0.0f) g_renderer.bloom_threshold = threshold;
}
void g3d_renderer_set_tonemap(int mode) { g_renderer.tonemap_mode = mode; }
void g3d_renderer_set_ssao(int enabled, float radius, float strength) {
    g_renderer.ssao_enabled = enabled ? 1 : 0;
    if (radius > 0.0f) g_renderer.ssao_radius = radius;
    if (strength > 0.0f) g_renderer.ssao_power = strength;
}

uint32_t g3d_renderer_shadow_texture(void) { return g_renderer.shadow_texture; }
Mat4 g3d_renderer_light_space(void) { return g_renderer.light_space_matrix; }
int g3d_renderer_shadows_on(void) { return g_renderer.shadow_enabled; }

/* Render the scene mirrored across the plane (P, N) into the reflection
   texture, clipping away geometry behind the plane, for water/mirrors. */
void g3d_renderer_reflection_pass_plane(float px, float py, float pz,
                                        float nx, float ny, float nz,
                                        unsigned int fbo, int w, int h) {
    if (!g_renderer.initialized || !g_renderer.active_camera)
        return;
#ifndef VITA
    G3DCamera *camera = g_renderer.active_camera;
    g3d_camera_update(camera);

    /* Normalize the plane normal and compute d = -N.P */
    float nl = sqrtf(nx*nx + ny*ny + nz*nz);
    if (nl < 1e-6f) nl = 1.0f;
    nx /= nl; ny /= nl; nz /= nl;
    float d = -(nx*px + ny*py + nz*pz);

    /* Reflection matrix R = I - 2 N Nt (linear) + (-2 d N) (translation),
       column-major. */
    Mat4 Rm = mat4_identity();
    Rm.m[0]  = 1.0f - 2.0f*nx*nx; Rm.m[1]  = -2.0f*nx*ny;     Rm.m[2]  = -2.0f*nx*nz;
    Rm.m[4]  = -2.0f*nx*ny;       Rm.m[5]  = 1.0f - 2.0f*ny*ny;Rm.m[6]  = -2.0f*ny*nz;
    Rm.m[8]  = -2.0f*nx*nz;       Rm.m[9]  = -2.0f*ny*nz;      Rm.m[10] = 1.0f - 2.0f*nz*nz;
    Rm.m[12] = -2.0f*d*nx;        Rm.m[13] = -2.0f*d*ny;       Rm.m[14] = -2.0f*d*nz;

    Mat4 V = g3d_camera_get_view(camera);
    g_renderer.reflection_view = mat4_multiply(V, Rm);

    g_renderer.clip_plane[0] = nx; g_renderer.clip_plane[1] = ny;
    g_renderer.clip_plane[2] = nz; g_renderer.clip_plane[3] = d;

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, w, h);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glClearColor(0.62f, 0.74f, 0.90f, 1.0f); /* sky-ish where nothing reflects */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    g_renderer.use_reflection_view = 1;
    g_renderer.clip_enabled = 1;

    int entity_count = 0;
    int *entities = g3d_scene_impl_get_entities(&entity_count);
    for (int i = 0; i < entity_count; i++) {
        G3DEntity *e = g3d_entity_impl_get(entities[i]);
        if (!e || !e->active || !e->mesh)
            continue;
        G3DMaterial *material =
            (e->material_id >= 0) ? g3d_material_impl_get(e->material_id) : NULL;
        Mat4 world = g3d_entity_impl_get_world_matrix(entities[i]);
        g3d_renderer_render_mesh(e->mesh, material, world, mat4_identity());
    }

    g_renderer.use_reflection_view = 0;
    g_renderer.clip_enabled = 0;
#endif
}

/* Draw the scene's entities, filtered by transparency (want_transparent: 0 =
   opaque only, 1 = transparent only). Shared by the opaque forward pass and the
   later transparent pass (which runs AFTER the sky so blended objects aren't
   overwritten by the sky where there's no opaque geometry behind them). */
static void draw_scene_entities(G3DCamera *camera, int *entities, int entity_count,
                                int want_transparent) {
    for (int i = 0; i < entity_count; i++) {
        int entity_id = entities[i];
        G3DEntity *entity = g3d_entity_impl_get(entity_id);

        if (!entity || !entity->active)
            continue;

        /* Route to the matching pass and carry the opacity/tint to the shader.
           Special blend modes (add/multiply/subtract) always need the blend pass. */
        int is_transparent = (entity->opacity < 0.996f) || blend_is_special(entity->blend_mode);
        if (is_transparent != want_transparent) continue;
        g_draw_opacity = entity->opacity;
        g_draw_tint[0] = entity->tint[0];
        g_draw_tint[1] = entity->tint[1];
        g_draw_tint[2] = entity->tint[2];
#ifndef VITA
        if (want_transparent) apply_entity_blend(entity);
#endif

        /* Get entity's world matrix */
        Mat4 world_matrix = g3d_entity_impl_get_world_matrix(entity_id);
        float lodd = g3d_instances_get_lod_distance();

        /* Model far-LOD root: when the whole model is far, draw ONE merged +
           decimated mesh instead of all its submesh children. */
        if (entity->lod_mesh && lodd > 0.0f) {
            float ex = world_matrix.m[12] - camera->position.x;
            float ey = world_matrix.m[13] - camera->position.y;
            float ez = world_matrix.m[14] - camera->position.z;
            entity->lod_far = (ex*ex + ey*ey + ez*ez > lodd * lodd) ? 1 : 0;
            if (entity->lod_far) {
                G3DMaterial *lmat = (entity->lod_material >= 0) ? g3d_material_impl_get(entity->lod_material) : NULL;
                g3d_renderer_render_mesh(entity->lod_mesh, lmat, world_matrix, mat4_identity());
                continue;
            }
        }

        /* Skip entities without a mesh assigned (e.g. a near model root) */
        if (!entity->mesh)
            continue;

        /* Child of a model root that is drawing its merged far-LOD -> skip. */
        if (entity->parent_id >= 0) {
            G3DEntity *par = g3d_entity_impl_get(entity->parent_id);
            if (par && par->lod_far)
                continue;
        }

        /* Frustum culling: transform the mesh AABB's 8 corners to world space,
           build a world AABB and test it against the camera frustum. */
        if (g_renderer.frustum_culling_enabled) {
            G3DMesh *cm = (G3DMesh *)entity->mesh;
            Vec3 wmin, wmax;
            int first = 1;
            for (int c = 0; c < 8; c++) {
                Vec3 corner = vec3_make(
                    (c & 1) ? cm->aabb_max[0] : cm->aabb_min[0],
                    (c & 2) ? cm->aabb_max[1] : cm->aabb_min[1],
                    (c & 4) ? cm->aabb_max[2] : cm->aabb_min[2]);
                Vec3 w = mat4_transform_point(world_matrix, corner);
                if (first) { wmin = w; wmax = w; first = 0; }
                else {
                    if (w.x < wmin.x) wmin.x = w.x; if (w.x > wmax.x) wmax.x = w.x;
                    if (w.y < wmin.y) wmin.y = w.y; if (w.y > wmax.y) wmax.y = w.y;
                    if (w.z < wmin.z) wmin.z = w.z; if (w.z > wmax.z) wmax.z = w.z;
                }
            }
            if (!g3d_camera_frustum_contains_aabb(camera, wmin, wmax)) {
                g_renderer.entities_culled++;
                continue;
            }
        }

        /* Get material (if any) */
        G3DMaterial *material = NULL;
        if (entity->material_id >= 0) {
            material = g3d_material_impl_get(entity->material_id);
        }

        /* Node-animated submesh: its vertices are in the glTF node's LOCAL space,
           so fold the node's animated world transform (+ the model's recenter
           offset) into the model matrix. Keep the full mesh (no LOD swap) so the
           moving part stays crisp. */
        G3DMesh *emesh = (G3DMesh *)entity->mesh;
        Mat4 draw_matrix = world_matrix;
        int node_animated = 0;
        if (emesh && emesh->anim_node >= 0 && entity->anim_model) {
            G3DModel *am = (G3DModel *)entity->anim_model;
            if (am->node_global && emesh->anim_node < am->node_count) {
                Mat4 offm = mat4_translate(am->skin_offset[0], am->skin_offset[1],
                                           am->skin_offset[2]);
                Mat4 nodem = mat4_multiply(offm, am->node_global[emesh->anim_node]);
                draw_matrix = mat4_multiply(world_matrix, nodem);
                node_animated = 1;
            }
        }

        /* Automatic LOD: far entities render their auto-generated low-poly mesh.
           Same global g3d_set_lod distance as instanced objects; no game code. */
        void *drawmesh = entity->mesh;
        if (!node_animated && lodd > 0.0f && !entity->lod_exempt) {
            float ex = world_matrix.m[12] - camera->position.x;
            float ey = world_matrix.m[13] - camera->position.y;
            float ez = world_matrix.m[14] - camera->position.z;
            if (ex*ex + ey*ey + ez*ez > lodd * lodd)
                drawmesh = g3d_mesh_lod((G3DMesh *)entity->mesh);
        }

        /* Render the entity's mesh (render_mesh recomputes the normal matrix
           from the model matrix internally) */
        g3d_renderer_render_mesh(drawmesh, material, draw_matrix,
                                 mat4_identity());
    }
}

static void g3d_renderer_bind_scene_target(void) {
#ifndef VITA
    glBindFramebuffer(GL_FRAMEBUFFER, g_renderer.framebuffer);
    if (g_renderer.framebuffer == g_renderer.target_fbo)
        glViewport(g_renderer.vp_x, g_renderer.vp_y, g_renderer.vp_w, g_renderer.vp_h);
    else
        glViewport(0, 0, g_renderer.display_width, g_renderer.display_height);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
#endif
}

void g3d_renderer_forward_pass(void) {
    if (!g_renderer.initialized || !g_renderer.active_camera)
        return;

    G3DCamera *camera = g_renderer.active_camera;
    g3d_camera_update(camera);
    if (g_renderer.frustum_culling_enabled)
        g3d_camera_update_frustum(camera);

    g3d_renderer_bind_scene_target();

    int entity_count = 0;
    int *entities = g3d_scene_impl_get_entities(&entity_count);
    if (!entities || entity_count == 0)
        return;

    /* Opaque only. Transparent entities are drawn later (after the sky) so the
       sky doesn't repaint them where there's no opaque geometry behind. */
    draw_scene_entities(camera, entities, entity_count, 0);
    g_draw_opacity = 1.0f;
    g_draw_tint[0] = g_draw_tint[1] = g_draw_tint[2] = 1.0f;
}

void g3d_renderer_transparent_pass(void) {
    if (!g_renderer.initialized || !g_renderer.active_camera)
        return;
    G3DCamera *camera = g_renderer.active_camera;

    int entity_count = 0;
    int *entities = g3d_scene_impl_get_entities(&entity_count);
    if (!entities || entity_count == 0)
        return;

    g3d_renderer_bind_scene_target();
#ifndef VITA
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);   /* blended: test depth, don't write it */
#endif

    draw_scene_entities(camera, entities, entity_count, 1);

#ifndef VITA
    glBlendEquation(GL_FUNC_ADD);   /* reset (an entity may have used SUBTRACT) */
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
    g_draw_opacity = 1.0f;
    g_draw_tint[0] = g_draw_tint[1] = g_draw_tint[2] = 1.0f;
}

void g3d_renderer_end_frame(void) {
    if (!g_renderer.initialized)
        return;

#ifndef VITA
    /* Stats */
    printf("G3D: Draw calls: %u, Triangles: %u, Culled: %u\n",
           g_renderer.draw_calls, g_renderer.triangles_rendered,
           g_renderer.entities_culled);

    /* Disable depth testing */
    glDisable(GL_DEPTH_TEST);
#endif
}

void g3d_renderer_render(void) {
    g3d_renderer_begin_frame();
    g3d_renderer_shadow_pass();
    g3d_renderer_spot_shadow_pass();
    if (g3d_water_reflection_enabled()) {
        g3d_renderer_reflection_pass_plane(0.0f, g3d_water_get_level(), 0.0f,
                                           0.0f, 1.0f, 0.0f,
                                           g_renderer.refl_framebuffer,
                                           g_renderer.display_width,
                                           g_renderer.display_height);
    }
    /* Visible/near mirrors render the scene mirrored into their textures */
    g3d_mirror_render_reflections(g_renderer.active_camera);
    g3d_renderer_forward_pass();
    /* Instanced vegetation/props (opaque, one draw call per group) */
    g3d_instances_render_all(g_renderer.active_camera, g_renderer.flip_y);
    /* Sky fills the background after opaque geometry (depth LEQUAL, far plane) */
    g3d_sky_render_pass(g_renderer.active_camera, g_renderer.flip_y);
    /* Mirror surfaces (opaque) sample their reflection textures */
    g3d_mirror_render_surfaces(g_renderer.active_camera, g_renderer.flip_y);
    /* SSAO from the opaque depth (before transparent water) -> applied in the resolve */
    g3d_renderer_ssao_pass();
    /* World-space volumetric low clouds (fly-through), composited over opaque + sky */
    g3d_renderer_cloud_pass();
    /* Semi-transparent entities (alpha blended) AFTER opaque + sky so they aren't
       repainted by the sky where there's no opaque geometry behind them. */
    g3d_renderer_transparent_pass();
    /* Transparent water + flows after the opaque pass (same Y-flip for GRAPH).
       Grab the opaque scene first so the water can refract it. */
    g3d_water_tick();   /* re-emit ripples at registered sources (river mouths) */
    g3d_water_update_underwater(g_renderer.active_camera); /* camera-submerged? */
    if (g3d_water_is_enabled() || g3d_fluid_count() > 0 || g3d_watersim_active() || g3d_voxwater_active()) {
        g3d_renderer_capture_scene();
        g3d_renderer_capture_depth();   /* for screen-space reflections in the water */
    }
    g3d_water_render_pass(g_renderer.active_camera, g_renderer.flip_y);
    g3d_fluid_render_pass(g_renderer.active_camera, g_renderer.flip_y);
    /* live height-field water sim (unified lakes+rivers): advance + draw.
       On voxel terrain the 3D voxel water owns everything -> don't draw the 2.5D. */
    if (g3d_watersim_active() && !g3d_voxterrain_active()) {
        static unsigned int wsim_last = 0;
        unsigned int now = SDL_GetTicks();
        float dt = wsim_last ? (now - wsim_last) / 1000.0f : 0.016f;
        wsim_last = now;
        if (dt > 0.1f) dt = 0.1f;
        g3d_watersim_step(dt);
        g3d_watersim_render(g_renderer.active_camera, g_renderer.flip_y);
    }
    /* 3D voxel water (fills caves, waterfalls): advance + draw */
    if (g3d_voxwater_active()) {
        static unsigned int vw_last = 0;
        unsigned int now = SDL_GetTicks();
        float dt = vw_last ? (now - vw_last) / 1000.0f : 0.016f;
        vw_last = now;
        if (dt > 0.1f) dt = 0.1f;
        g3d_voxwater_step(dt);
        g3d_voxwater_render(g_renderer.active_camera, g_renderer.flip_y);
    }
    g3d_flow_render_pass(g_renderer.active_camera, g_renderer.flip_y);
    g3d_particles_update_render(g_renderer.active_camera, g_renderer.flip_y);
    g3d_fire_render(g_renderer.active_camera, g_renderer.flip_y);   /* torch/campfire flames */
    g3d_renderer_underwater_pass();   /* fullscreen tint+wobble if camera is submerged */
    g3d_renderer_resolve_hdr();       /* HDR + bloom -> host FBO (no-op if HDR off) */
    g3d_renderer_end_frame();
}

/* ============================================================================
   MESH RENDERING
   ============================================================================
 */

void g3d_renderer_render_mesh(void *mesh, void *material, Mat4 model_matrix,
                              Mat4 normal_matrix) {
    if (!mesh || !g_renderer.initialized || !g_renderer.active_camera)
        return;

    G3DMesh *g3d_mesh = (G3DMesh *)mesh;
    G3DMaterial *g3d_material = (G3DMaterial *)material;
    G3DShaderProgram *shader = (G3DShaderProgram *)g_renderer.phong_shader;

    if (!shader)
        return;

#ifndef VITA
    /* Bind shader program */
    g3d_shader_use(shader);

    /* Camera matrices */
    Mat4 view = g_renderer.use_reflection_view
                    ? g_renderer.reflection_view
                    : g3d_camera_get_view(g_renderer.active_camera);
    Mat4 proj = g3d_camera_get_projection(g_renderer.active_camera);

    /* When rendering into a GRAPH-backed FBO, flip Y in the projection: GL FBO
       textures have a bottom-left origin while SDL_gpu composites the GRAPH with
       a top-left origin, so the image would otherwise appear upside-down. */
    if (g_renderer.flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }

    g3d_shader_set_mat4(shader, "uModel", model_matrix);
    g3d_shader_set_mat4(shader, "uView", view);
    g3d_shader_set_mat4(shader, "uProjection", proj);

    /* Normal matrix (inverse transpose of model matrix) */
    Mat4 normal_mat = mat4_inverse(mat4_transpose(model_matrix));
    g3d_shader_set_mat3(shader, "uNormalMatrix", normal_mat);

    /* Material properties */
    uint32_t albedo_gl = g_renderer.default_white_tex;
    if (g3d_material) {
        Vec3 albedo = vec3_make(g3d_material->color[0], g3d_material->color[1],
                                g3d_material->color[2]);
        g3d_shader_set_vec3(shader, "uAlbedoColor", albedo);
        g3d_shader_set_float(shader, "uMetallic", g3d_material->metallic);
        g3d_shader_set_float(shader, "uRoughness", g3d_material->roughness);

        /* Use the material's albedo map if assigned */
        G3DTexture *albedo_tex = (G3DTexture *)g3d_material->albedo_texture;
        if (albedo_tex && albedo_tex->gl_handle)
            albedo_gl = albedo_tex->gl_handle;
        g3d_shader_set_int(shader, "uTriplanar", g3d_material->triplanar);
        g3d_shader_set_int(shader, "uBiome", g3d_material->biome);
        g3d_shader_set_float(shader, "uBiomeAmp", g3d_material->biome_amp);
        g3d_shader_set_float(shader, "uBiomeSea", g3d_material->biome_sea);
        /* Optional per-biome textures on units 11..14 (sand/grass/rock/snow). Each
           band is independent: bands without a texture keep the flat colour, and
           their unit gets the default white texture so the sampler stays valid. */
        const char *bn[4] = { "uBiomeSand", "uBiomeGrass", "uBiomeRock", "uBiomeSnow" };
        float bmask[4] = { 0, 0, 0, 0 };
        int any_biome_tex = 0;
        if (g3d_material->biome) {
            for (int b = 0; b < 4; b++) {
                G3DTexture *bt = (G3DTexture *)g3d_material->biome_tex[b];
                GLuint h = (bt && bt->gl_handle) ? bt->gl_handle : g_renderer.default_white_tex;
                if (bt && bt->gl_handle) { bmask[b] = 1.0f; any_biome_tex = 1; }
                glActiveTexture(GL_TEXTURE11 + b);
                glBindTexture(GL_TEXTURE_2D, h);
                g3d_shader_set_sampler2d(shader, bn[b], 11 + b);
            }
            glActiveTexture(GL_TEXTURE0);
        }
        g3d_shader_set_int(shader, "uHasBiomeTex", any_biome_tex);
        if (any_biome_tex) {
            g3d_shader_set_float(shader, "uBiomeTexScale", g3d_material->biome_tex_scale);
            g3d_shader_set_vec4(shader, "uBiomeTexMask",
                                vec4_make(bmask[0], bmask[1], bmask[2], bmask[3]));
        }
        G3DTexture *wall_tex = (G3DTexture *)g3d_material->wall_texture;
        if (g3d_material->triplanar && wall_tex && wall_tex->gl_handle) {
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, wall_tex->gl_handle);
            g3d_shader_set_sampler2d(shader, "uWallTexture", 3);
            g3d_shader_set_int(shader, "uHasWallTex", 1);
            glActiveTexture(GL_TEXTURE0);
        } else {
            g3d_shader_set_int(shader, "uHasWallTex", 0);
        }
        /* PBR maps: normal (unit 8) / metallic (9) / roughness (10) */
        G3DTexture *ntex = (G3DTexture *)g3d_material->normal_texture;
        G3DTexture *mtex = (G3DTexture *)g3d_material->metallic_texture;
        G3DTexture *rtex = (G3DTexture *)g3d_material->roughness_texture;
        if (ntex && ntex->gl_handle) { glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, ntex->gl_handle); g3d_shader_set_sampler2d(shader, "uNormalMap", 8); g3d_shader_set_int(shader, "uHasNormalMap", 1); }
        else g3d_shader_set_int(shader, "uHasNormalMap", 0);
        if (mtex && mtex->gl_handle) { glActiveTexture(GL_TEXTURE9); glBindTexture(GL_TEXTURE_2D, mtex->gl_handle); g3d_shader_set_sampler2d(shader, "uMetalMap", 9); g3d_shader_set_int(shader, "uHasMetalMap", 1); }
        else g3d_shader_set_int(shader, "uHasMetalMap", 0);
        if (rtex && rtex->gl_handle) { glActiveTexture(GL_TEXTURE10); glBindTexture(GL_TEXTURE_2D, rtex->gl_handle); g3d_shader_set_sampler2d(shader, "uRoughMap", 10); g3d_shader_set_int(shader, "uHasRoughMap", 1); }
        else g3d_shader_set_int(shader, "uHasRoughMap", 0);
        glActiveTexture(GL_TEXTURE0);
    } else {
        /* No material: render with white albedo so geometry stays visible */
        g3d_shader_set_vec3(shader, "uAlbedoColor", vec3_make(1, 1, 1));
        g3d_shader_set_float(shader, "uMetallic", 0.0f);
        g3d_shader_set_float(shader, "uRoughness", 0.5f);
        g3d_shader_set_int(shader, "uTriplanar", 0);
        g3d_shader_set_int(shader, "uBiome", 0);
        g3d_shader_set_int(shader, "uHasBiomeTex", 0);
        g3d_shader_set_int(shader, "uHasWallTex", 0);
        g3d_shader_set_int(shader, "uHasNormalMap", 0);
        g3d_shader_set_int(shader, "uHasMetalMap", 0);
        g3d_shader_set_int(shader, "uHasRoughMap", 0);
    }

    /* Ambient light (applied every frame so it never gets lost) */
    g3d_shader_set_vec3(shader, "uAmbientLight",
                        vec3_make(g_renderer.ambient_color[0],
                                  g_renderer.ambient_color[1],
                                  g_renderer.ambient_color[2]));
    g3d_shader_set_float(shader, "uAmbientIntensity", g_renderer.ambient_intensity);

    /* Image based lighting from the sky: irradiance (diffuse) + prefiltered
       chain and BRDF LUT (specular). Units 15..17 — 0..14 are taken by albedo,
       shadow, wall, spot shadows, normal/metal/rough and the biome maps. */
    if (g3d_ibl_bind(15, 16, 17)) {
        g3d_shader_set_int(shader, "uHasIBL", 1);
        g3d_shader_set_int(shader, "uIrradiance", 15);
        g3d_shader_set_int(shader, "uPrefilter", 16);
        g3d_shader_set_sampler2d(shader, "uBRDFLUT", 17);
        g3d_shader_set_float(shader, "uIBLIntensity", g3d_ibl_intensity());
        g3d_shader_set_float(shader, "uPrefilterMips", g3d_ibl_prefilter_mips());
        glActiveTexture(GL_TEXTURE0);
    } else {
        g3d_shader_set_int(shader, "uHasIBL", 0);
    }

    /* Bind albedo texture (real or 1x1 white fallback) to unit 0 */
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, albedo_gl);
    g3d_shader_set_sampler2d(shader, "uAlbedoTexture", 0);

    /* Camera position for specular */
    Vec3 cam_pos = g_renderer.active_camera->position;
    g3d_shader_set_vec3(shader, "uCameraPosition", cam_pos);
    /* GRAPH render (flip_y) inverts triangle winding -> tell the shader so its
       gl_FrontFacing two-sided normal flip stays correct. */
    g3d_shader_set_int(shader, "uFlipWinding", g_renderer.flip_y);
    g3d_shader_set_float(shader, "uOpacity", g_draw_opacity);
    g3d_shader_set_vec3(shader, "uEntityColor",
                        vec3_make(g_draw_tint[0], g_draw_tint[1], g_draw_tint[2]));

    /* Get lights from active scene */
    int light_count = 0;
    int *light_ids = g3d_scene_impl_get_lights(&light_count);

    /* Main directional light (carries the shadow). Default to "off" so a scene
       with only point/spot lights doesn't get lit by stale uniforms. */
    g3d_shader_set_vec3(shader, "uLightDirection", vec3_make(0.0f, -1.0f, 0.0f));
    g3d_shader_set_vec3(shader, "uLightColor", vec3_make(1.0f, 1.0f, 1.0f));
    g3d_shader_set_float(shader, "uLightIntensity", 0.0f);

    Vec3 sun_toward = vec3_make(0.0f, 1.0f, 0.0f);   /* toward-sun dir for cloud shadows */
    if (light_count > 0 && light_ids) {
        for (int i = 0; i < light_count; i++) {
            G3DLight *light = g3d_light_impl_get(light_ids[i]);
            if (light && light->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
                g3d_shader_set_vec3(shader, "uLightDirection", light->direction);
                g3d_shader_set_vec3(shader, "uLightColor",
                                    vec3_make(light->color[0], light->color[1],
                                              light->color[2]));
                g3d_shader_set_float(shader, "uLightIntensity", light->intensity);
                sun_toward = vec3_scale(vec3_normalize(light->direction), -1.0f);
                break;
            }
        }
    }

    /* cloud-shadow field on the sun (identical params to the volumetric cloud pass) */
    {
        float cover = 0.0f, base = 0.0f, thick = 1.0f, speed = 1.0f;
        g3d_sky_low_clouds(&cover, &base, &thick, &speed);
        g3d_shader_set_float(shader, "uTime", (float)SDL_GetTicks() / 1000.0f);
        g3d_shader_set_vec3(shader, "uSunDir", sun_toward);
        g3d_shader_set_float(shader, "uCloudCover", cover);
        g3d_shader_set_float(shader, "uCloudBase", base);
        g3d_shader_set_float(shader, "uCloudThick", thick);
        g3d_shader_set_float(shader, "uCloudSpeed", speed);
    }

    /* Extra point / spot lights (up to MAX_PL=8) */
    int pl_lightid[8];
    int npl_total = 0;
    {
        int npl = 0;
        for (int i = 0; i < light_count && npl < 8; i++) {
            G3DLight *l = g3d_light_impl_get(light_ids[i]);
            if (!l || !l->active)
                continue;
            if (l->type != G3D_LIGHT_TYPE_POINT && l->type != G3D_LIGHT_TYPE_SPOT)
                continue;

            pl_lightid[npl] = light_ids[i];   /* remember which light is in this slot */

            char name[32];
            snprintf(name, sizeof(name), "uPLType[%d]", npl);
            g3d_shader_set_int(shader, name, l->type);
            snprintf(name, sizeof(name), "uPLPos[%d]", npl);
            g3d_shader_set_vec3(shader, name, l->position);
            snprintf(name, sizeof(name), "uPLDir[%d]", npl);
            g3d_shader_set_vec3(shader, name, vec3_normalize(l->direction));
            snprintf(name, sizeof(name), "uPLCol[%d]", npl);
            g3d_shader_set_vec3(shader, name,
                                vec3_make(l->color[0], l->color[1], l->color[2]));
            snprintf(name, sizeof(name), "uPLInt[%d]", npl);
            g3d_shader_set_float(shader, name, l->intensity);
            snprintf(name, sizeof(name), "uPLRange[%d]", npl);
            g3d_shader_set_float(shader, name, (l->range > 0.01f) ? l->range : 20.0f);

            /* Cone: cone_angle is the full angle in degrees; inner = 0.85*outer */
            float outer = (l->cone_angle > 0.5f) ? l->cone_angle : 40.0f;
            float cos_out = cosf(outer * 0.5f * 3.14159265f / 180.0f);
            float cos_in = cosf(outer * 0.5f * 0.75f * 3.14159265f / 180.0f);
            snprintf(name, sizeof(name), "uPLCosOut[%d]", npl);
            g3d_shader_set_float(shader, name, cos_out);
            snprintf(name, sizeof(name), "uPLCosIn[%d]", npl);
            g3d_shader_set_float(shader, name, cos_in);
            npl++;
        }
        g3d_shader_set_int(shader, "uNumPL", npl);
        npl_total = npl;
    }

    /* Shadow mapping: bind the depth map (unit 1) and the light-space matrix */
    if (g_renderer.shadow_enabled) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g_renderer.shadow_texture);
        g3d_shader_set_sampler2d(shader, "uShadowMap", 1);
        g3d_shader_set_mat4(shader, "uLightSpaceMatrix",
                            g_renderer.light_space_matrix);
        g3d_shader_set_int(shader, "uCastShadow", 1);
        glActiveTexture(GL_TEXTURE0);
    } else {
        g3d_shader_set_int(shader, "uCastShadow", 0);
    }

    /* Dynamic shadow maps (units 4..7): up to 4 torches/spotlights, each shadowing the
       point-light slot whose id matches. */
    if (g_renderer.shadow_enabled && g_renderer.num_shadow_casters > 0) {
        int nsc = g_renderer.num_shadow_casters;
        g3d_shader_set_int(shader, "uSpotShadowEnabled", 1);
        g3d_shader_set_int(shader, "uNumShadowCasters", nsc);
        for (int k = 0; k < nsc; k++) {
            /* find this caster's slot in the uPL arrays */
            int slot = -1;
            for (int p = 0; p < npl_total; p++)
                if (pl_lightid[p] == g_renderer.shadow_caster_id[k]) { slot = p; break; }
            char name[40];
            glActiveTexture(GL_TEXTURE4 + k);
            glBindTexture(GL_TEXTURE_2D, g_renderer.spot_shadow_texture[k]);
            snprintf(name, sizeof(name), "uSpotShadowMap[%d]", k);
            g3d_shader_set_sampler2d(shader, name, 4 + k);
            snprintf(name, sizeof(name), "uSpotLightSpaceMatrix[%d]", k);
            g3d_shader_set_mat4(shader, name, g_renderer.spot_light_space_matrix[k]);
            snprintf(name, sizeof(name), "uCasterSlot[%d]", k);
            g3d_shader_set_int(shader, name, slot);
        }
        glActiveTexture(GL_TEXTURE0);
    } else {
        g3d_shader_set_int(shader, "uSpotShadowEnabled", 0);
        g3d_shader_set_int(shader, "uNumShadowCasters", 0);
    }

    /* Clip plane (used by the reflection pass to drop geometry behind a mirror) */
    g3d_shader_set_int(shader, "uClipEnable", g_renderer.clip_enabled);
    g3d_shader_set_vec4(shader, "uClipPlane",
                        vec4_make(g_renderer.clip_plane[0], g_renderer.clip_plane[1],
                                  g_renderer.clip_plane[2], g_renderer.clip_plane[3]));

    /* Linear fog */
    g3d_shader_set_int(shader, "uFogEnabled", g_renderer.fog_enabled);
    if (g_renderer.fog_enabled) {
        g3d_shader_set_vec3(shader, "uFogColor",
                            vec3_make(g_renderer.fog_color[0],
                                      g_renderer.fog_color[1],
                                      g_renderer.fog_color[2]));
        g3d_shader_set_float(shader, "uFogStart", g_renderer.fog_start);
        g3d_shader_set_float(shader, "uFogEnd", g_renderer.fog_end);
    }

    /* Bind and render mesh */
    glBindVertexArray(g3d_mesh->vao);
    glDrawElements(GL_TRIANGLES, g3d_mesh->index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

#endif

    g_renderer.draw_calls++;
    g_renderer.triangles_rendered += g3d_mesh->index_count / 3;
}

/* ============================================================================
   LIGHTING
   ============================================================================
 */

void g3d_renderer_set_fog(int enabled, Vec3 color, float start, float end) {
    g_renderer.fog_enabled = enabled;
    g_renderer.fog_color[0] = color.x;
    g_renderer.fog_color[1] = color.y;
    g_renderer.fog_color[2] = color.z;
    g_renderer.fog_start = start;
    g_renderer.fog_end = end;
}

void g3d_renderer_get_fog(int *enabled, Vec3 *color, float *start, float *end) {
    if (enabled) *enabled = g_renderer.fog_enabled;
    if (color)   *color   = vec3_make(g_renderer.fog_color[0], g_renderer.fog_color[1], g_renderer.fog_color[2]);
    if (start)   *start   = g_renderer.fog_start;
    if (end)     *end     = g_renderer.fog_end;
}

void g3d_renderer_set_ambient_light(Vec3 color, float intensity) {
    /* Store it; render_mesh applies it every frame (robust against the shader
       program state being changed between frames). */
    g_renderer.ambient_color[0] = color.x;
    g_renderer.ambient_color[1] = color.y;
    g_renderer.ambient_color[2] = color.z;
    g_renderer.ambient_intensity = intensity;
}

void g3d_renderer_set_directional_light(Vec3 direction, Vec3 color,
                                        float intensity) {
    if (!g_renderer.phong_shader)
        return;

    G3DShaderProgram *shader = (G3DShaderProgram *)g_renderer.phong_shader;
    g3d_shader_use(shader);
    g3d_shader_set_vec3(shader, "uLightDirection", direction);
    g3d_shader_set_vec3(shader, "uLightColor", color);
    g3d_shader_set_float(shader, "uLightIntensity", intensity);
}

/* ============================================================================
   STATISTICS
   ============================================================================
 */

uint32_t g3d_renderer_get_draw_calls(void) {
    return g_renderer.draw_calls;
}

uint32_t g3d_renderer_get_triangle_count(void) {
    return g_renderer.triangles_rendered;
}

uint32_t g3d_renderer_get_culled_entities(void) {
    return g_renderer.entities_culled;
}

/* ============================================================================
   DEBUG VISUALIZATION
   ============================================================================
 */

void g3d_renderer_draw_bounding_box(Vec3 min, Vec3 max, Vec3 color) {
    /* TODO: Debug visualization of bounding box */
}

void g3d_renderer_draw_frustum(G3DCamera *camera, Vec3 color) {
    /* TODO: Debug visualization of camera frustum */
}
