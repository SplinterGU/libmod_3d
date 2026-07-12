/*
 * libmod_3d_renderer.h - Forward Rendering Pipeline
 *
 * Core rendering pipeline:
 * 1. Frustum culling
 * 2. Shadow pass (depth-only)
 * 3. Forward rendering pass (lighting)
 * 4. Post-processing (fog, effects)
 */

#ifndef __LIBMOD_3D_RENDERER_H
#define __LIBMOD_3D_RENDERER_H

#include "libmod_3d_camera.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   RENDERER STATE
   ============================================================================
 */

typedef struct {
    int initialized;

    /* Display */
    uint32_t display_width;
    uint32_t display_height;
    uint32_t framebuffer;       /* GL framebuffer object */
    int flip_y;                 /* flip projection Y (BennuGD GRAPH); editor=0 */
    uint32_t vp_x, vp_y, vp_w, vp_h; /* physical viewport */

    /* Camera */
    G3DCamera *active_camera;

    /* Shaders */
    void *phong_shader;         /* G3DShaderProgram* */
    void *shadow_shader;        /* G3DShaderProgram* */

    /* Shadow mapping */
    uint32_t shadow_map_width;
    uint32_t shadow_map_height;
    uint32_t shadow_framebuffer;
    uint32_t shadow_texture;
    int shadow_enabled;

    /* Rendering state */
    int wireframe_mode;
    int show_normals;
    int show_bounding_boxes;
    int frustum_culling_enabled;
    float clear_color[4];

    /* 1x1 white fallback texture (bound when a material has no albedo map) */
    uint32_t default_white_tex;

    /* Ambient light (applied every frame in render_mesh) */
    float ambient_color[3];
    float ambient_intensity;

    /* Linear fog (applied in the Phong fragment shader) */
    int fog_enabled;
    float fog_color[3];
    float fog_start;
    float fog_end;

    /* Shadow mapping: light-space view-projection from the last shadow pass */
    Mat4 light_space_matrix;

    /* Dynamic shadow maps for up to 4 spot/point lights (torches/flashlight) */
    uint32_t spot_shadow_framebuffer[4];
    uint32_t spot_shadow_texture[4];
    Mat4 spot_light_space_matrix[4];
    int shadow_caster_id[4];  /* light id casting each dynamic shadow */
    int num_shadow_casters;   /* how many are active this frame (0..4) */

    /* Planar reflection (for water): scene rendered mirrored across a plane */
    uint32_t refl_framebuffer;
    uint32_t refl_texture;
    uint32_t refl_depth;

    /* Scene colour copy (for water refraction): the opaque frame grabbed just
       before the transparent water pass. */
    uint32_t scene_texture;
    uint32_t scene_tex_w, scene_tex_h;

    /* Scene depth copy (for screen-space reflections in the water shader), grabbed
       with the colour copy just before the transparent water pass. */
    uint32_t scene_depth_tex, scene_depth_w, scene_depth_h;

    /* Underwater post-process (camera below a water surface). */
    int underwater;
    float underwater_tint[3], underwater_strength;
    void *post_shader;            /* G3DShaderProgram* (fullscreen) */
    uint32_t post_vao, post_vbo;
    Mat4 reflection_view;     /* used by render_mesh when use_reflection_view */
    int use_reflection_view;
    int clip_enabled;         /* discard fragments behind clip_plane */
    float clip_plane[4];      /* (nx,ny,nz,d): keep dot(pos,n)+d >= 0 */

    /* HDR / post pipeline (GL3.3): the scene renders into a RGBA16F buffer, bright areas
       bloom, then a resolve pass tonemaps + composites into the host target_fbo. */
    uint32_t target_fbo;                 /* host FBO (0=screen or GRAPH) */
    uint32_t hdr_fbo, hdr_color, hdr_depth, hdr_w, hdr_h;
    uint32_t bloom_fbo[2], bloom_tex[2], bloom_w, bloom_h;
    void *bright_shader, *blur_shader, *tonemap_shader;
    float exposure, bloom_threshold, bloom_amount;
    int bloom_enabled, hdr_ready, hdr_active, tonemap_mode;

    /* Half-res volumetric cloud target (the raymarch runs at 1/4 the pixels,
       then upscales — clouds are low frequency so quality loss is negligible) */
    uint32_t cloud_fbo, cloud_tex, cloud_w, cloud_h;

    /* SSAO (screen-space ambient occlusion), reconstructed from the depth texture */
    uint32_t ssao_fbo, ssao_tex, ssao_blur_fbo, ssao_blur_tex, ssao_noise_tex, ssao_w, ssao_h;
    void *ssao_shader, *ssao_blur_shader;
    int ssao_enabled;
    float ssao_radius, ssao_power;

    /* Stats */
    uint32_t draw_calls;
    uint32_t triangles_rendered;
    uint32_t entities_culled;

} G3DRenderer;

/* ============================================================================
   RENDERER API
   ============================================================================
 */

/* Initialize renderer */
int g3d_renderer_init(uint32_t width, uint32_t height);

/* Shutdown renderer */
void g3d_renderer_shutdown(void);

/* Set active camera */
void g3d_renderer_set_camera(G3DCamera *camera);

/* Set render target FBO (0 = screen). Also sets flip_y = (fbo != 0). */
void g3d_renderer_set_target(uint32_t fbo);

/* Override the projection Y-flip (call after set_target). Editor uses 0. */
void g3d_renderer_set_flip(int flip);

/* Update the render viewport size (logical size). */
void g3d_renderer_set_viewport_size(uint32_t w, uint32_t h);

/* Set the physical viewport on the actual target (handles letterbox/overscan) */
void g3d_renderer_set_viewport_physical(uint32_t x, uint32_t y, uint32_t w, uint32_t h);

/* Get the renderer's logical display/viewport size */
void g3d_renderer_get_display_size(uint32_t *width, uint32_t *height);

/* Enable/disable shadow mapping */
void g3d_renderer_enable_shadows(int enabled, uint32_t resolution);

/* Toggle shadow rendering without changing the shadow map resolution */
void g3d_renderer_set_shadows(int enabled);

/* Set clear color */
void g3d_renderer_set_clear_color(float r, float g, float b, float a);

/* HDR post pipeline */
void g3d_renderer_set_hdr(int enabled);                 /* master toggle (default on) */
void g3d_renderer_set_exposure(float exposure);         /* default 1.0 */
void g3d_renderer_set_bloom(int enabled, float amount, float threshold);
void g3d_renderer_set_tonemap(int mode);                /* 0 = neutral clamp, 1 = ACES filmic */
void g3d_renderer_set_ssao(int enabled, float radius, float strength);
void g3d_renderer_ssao_pass(void);                      /* compute AO from the depth texture */
void g3d_renderer_cloud_pass(void);                     /* world-space volumetric low clouds */
void g3d_renderer_resolve_hdr(void);                    /* resolve HDR+bloom (+AO) into the host FBO */

/* Enable/disable features */
void g3d_renderer_set_wireframe_mode(int enabled);
void g3d_renderer_set_frustum_culling(int enabled);

/* Begin frame (clear buffers, reset stats) */
void g3d_renderer_begin_frame(void);

/* Shadow pass (depth-only rendering) */
void g3d_renderer_shadow_pass(void);

/* Spot-light shadow pass (depth from the first spotlight) */
void g3d_renderer_spot_shadow_pass(void);

/* Planar reflection pass: render the scene mirrored across the plane (point P,
   normal N) into the given FBO (viewport w x h). For water/mirrors to sample. */
void g3d_renderer_reflection_pass_plane(float px, float py, float pz,
                                        float nx, float ny, float nz,
                                        unsigned int fbo, int w, int h);

/* GL handle of the reflection colour texture. */
uint32_t g3d_renderer_reflection_texture(void);

/* Copy the current colour buffer into the scene texture (for water refraction)
   and return its GL handle. Call right before the transparent water pass. */
uint32_t g3d_renderer_capture_scene(void);
uint32_t g3d_renderer_scene_texture(void);
uint32_t g3d_renderer_capture_depth(void);         /* copy scene depth (for water SSR) */
uint32_t g3d_renderer_scene_depth_texture(void);

/* Underwater screen effect: when `on`, the frame is tinted toward (r,g,b) and
   wobble-distorted. strength ~1 is normal. */
void g3d_renderer_set_underwater(int on, float r, float g, float b, float strength);

/* Shadow state (for other passes, e.g. instancing, to receive shadows). */
uint32_t g3d_renderer_shadow_texture(void);
Mat4 g3d_renderer_light_space(void);
int g3d_renderer_shadows_on(void);

/* Forward rendering pass (with lighting) */
void g3d_renderer_forward_pass(void);

/* End frame (swap buffers, etc) */
void g3d_renderer_end_frame(void);

/* Full render (begin + shadow + forward + end) */
void g3d_renderer_render(void);

/* Render a single mesh with given matrices and material */
void g3d_renderer_render_mesh(void *mesh,           /* G3DMesh* */
                              void *material,       /* G3DMaterial* */
                              Mat4 model_matrix,
                              Mat4 normal_matrix);

/* Linear fog: blends scene toward color between start and end distance */
void g3d_renderer_set_fog(int enabled, Vec3 color, float start, float end);

/* Set lighting uniforms */
void g3d_renderer_set_ambient_light(Vec3 color, float intensity);
void g3d_renderer_set_directional_light(Vec3 direction, Vec3 color,
                                        float intensity);

/* Get renderer stats */
uint32_t g3d_renderer_get_draw_calls(void);
uint32_t g3d_renderer_get_triangle_count(void);
uint32_t g3d_renderer_get_culled_entities(void);

/* Debug visualization */
void g3d_renderer_draw_bounding_box(Vec3 min, Vec3 max, Vec3 color);
void g3d_renderer_draw_frustum(G3DCamera *camera, Vec3 color);

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_RENDERER_H */
