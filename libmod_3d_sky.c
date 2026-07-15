/*
 * libmod_3d_sky.c - Skybox / sky dome implementation
 */

#include "libmod_3d_sky.h"
#include "libmod_3d_ibl.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_light.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <math.h>
#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* ---- shaders ----------------------------------------------------------- */

/* The cube is drawn centred on the camera (view translation stripped) and
   pushed to the far plane (gl_Position = clip.xyww => depth 1.0), so it only
   fills background pixels under a LEQUAL depth test. vDir is the world-space
   view ray used to colour the sky. */
static const char *sky_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "uniform mat4 uViewRot;\n"   /* view with translation removed */
    "uniform mat4 uProjection;\n"
    "out vec3 vDir;\n"
    "void main() {\n"
    "    vDir = position;\n"
    "    vec4 clip = uProjection * uViewRot * vec4(position, 1.0);\n"
    "    gl_Position = clip.xyww;\n"
    "}\n";

static const char *sky_frag =
    "#version 330 core\n"
    "in vec3 vDir;\n"
    "uniform vec3 uTopColor;\n"
    "uniform vec3 uHorizonColor;\n"
    "uniform vec3 uSunDir;\n"      /* direction TOWARD the sun/moon (normalized) */
    "uniform vec3 uSunColor;\n"
    "uniform sampler2D uPanorama;\n"
    "uniform int uHasTex;\n"
    "uniform float uTime;\n"
    "uniform float uCloudAmt;\n"   /* 0 = clear sky, 1 = heavy clouds */
    "uniform float uCloudSpeed;\n"
    "out vec4 FragColor;\n"
    "const float PI = 3.14159265359;\n"
    "float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1,311.7))) * 43758.5453); }\n"
    "float vn(vec2 p){ vec2 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(hash(i),hash(i+vec2(1,0)),f.x), mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x), f.y); }\n"
    "float fbm(vec2 p){ float s=0.0,a=0.5; mat2 m=mat2(1.6,1.2,-1.2,1.6);\n"
    "  for(int i=0;i<5;i++){ s+=a*vn(p); p=m*p; a*=0.5; } return s; }\n"
    /* ---- 3D value noise for volumetric clouds ---- */
    "float h13(vec3 p){ p=fract(p*0.3183099+0.1); p*=17.0; return fract(p.x*p.y*p.z*(p.x+p.y+p.z)); }\n"
    "float vn3(vec3 p){ vec3 i=floor(p), f=fract(p); f=f*f*(3.0-2.0*f);\n"
    "  return mix(mix(mix(h13(i+vec3(0,0,0)),h13(i+vec3(1,0,0)),f.x), mix(h13(i+vec3(0,1,0)),h13(i+vec3(1,1,0)),f.x),f.y),\n"
    "             mix(mix(h13(i+vec3(0,0,1)),h13(i+vec3(1,0,1)),f.x), mix(h13(i+vec3(0,1,1)),h13(i+vec3(1,1,1)),f.x),f.y), f.z); }\n"
    "float fbm3(vec3 p){ float s=0.0,a=0.5; for(int i=0;i<3;i++){ s+=a*vn3(p); p*=2.03; a*=0.5; } return s; }\n"
    "uniform float uCloudLow; uniform float uCloudHigh;\n"                       /* cloud slab altitudes */
    "float cloudDensity(vec3 p, float amt, float spd){\n"
    "    vec3 wp = p*0.0016 + vec3(uTime*spd*0.010, 0.0, uTime*spd*0.004);\n"     /* scale + wind */
    "    float base = fbm3(wp);\n"
    "    float det  = fbm3(wp*4.0 + 3.1) * 0.35;\n"                              /* erode edges with detail */
    "    float h = clamp((p.y - uCloudLow)/(uCloudHigh - uCloudLow), 0.0, 1.0);\n"
    "    float grad = smoothstep(0.0,0.18,h) * smoothstep(1.0,0.55,h);\n"        /* fluffy top, flat base */
    "    float d = (base - det - (1.0 - amt*0.85)) * grad;\n"                    /* coverage from amt */
    "    return clamp(d*2.5, 0.0, 1.0);\n"
    "}\n"
    "void main() {\n"
    "    vec3 d = normalize(vDir);\n"
    "    vec3 col;\n"
    "    if (uHasTex == 1) {\n"
    "        vec2 uv = vec2(atan(d.z, d.x) / (2.0 * PI) + 0.5,\n"
    "                       acos(clamp(d.y, -1.0, 1.0)) / PI);\n"
    "        col = texture(uPanorama, uv).rgb;\n"
    "    } else {\n"
    "        float t = clamp(d.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "        t = pow(t, 0.55);\n"
    "        col = mix(uHorizonColor, uTopColor, t);\n"
    "        float s = max(dot(d, normalize(uSunDir)), 0.0);\n"
    "        col += uSunColor * (pow(s, 64.0) * 0.8 + pow(s, 8.0) * 0.15);\n"   /* sun/moon glow */
    "        float night = clamp(1.0 - (uTopColor.r+uTopColor.g+uTopColor.b)*1.2, 0.0, 1.0);\n"
    "        if (night > 0.1 && d.y > 0.15) {\n"                                /* stars: round points, gentle twinkle */
    "            vec2 g = (d.xz/(d.y+0.1)) * 60.0;\n"
    "            vec2 cell = floor(g); vec2 f = fract(g);\n"
    "            float h = hash(cell);\n"
    "            if (h > 0.955) {\n"                                            // ~4% of cells hold a star
    "                vec2 sp = vec2(hash(cell+1.3), hash(cell+2.7));\n"         // star position in cell
    "                float pt = smoothstep(0.11, 0.0, length(f - sp));\n"       // round point
    "                float tw = 0.7 + 0.3*sin(uTime*1.5 + h*50.0);\n"          // never fully off
    "                col += vec3(pt) * tw * night * smoothstep(0.15, 0.5, d.y);\n"
    "            }\n"
    "        }\n"
    "        if (uCloudAmt > 0.0 && d.y > 0.03) {\n"                            /* VOLUMETRIC raymarched clouds */
    "            vec3 sunN = normalize(uSunDir);\n"
    "            float t0 = uCloudLow / d.y, t1 = uCloudHigh / d.y;\n"          // ray crosses the slab
    "            const int N = 28; float dtS = (t1 - t0) / float(N);\n"
    "            float jit = hash(gl_FragCoord.xy) * dtS;\n"                    // dither to hide banding
    "            vec3 ambC = mix(uHorizonColor, uTopColor, 0.6);\n"
    "            vec3 sunC = uSunColor * 3.0;\n"
    "            float T = 1.0; vec3 scat = vec3(0.0);\n"
    "            for (int i = 0; i < N; i++) {\n"
    "                float t = t0 + (float(i) + 0.5) * dtS + jit;\n"
    "                vec3 p = d * t;\n"
    "                float dens = cloudDensity(p, uCloudAmt, uCloudSpeed);\n"
    "                if (dens > 0.01) {\n"
    "                    float ls = 0.0; float lstep = (uCloudHigh-uCloudLow)*0.16;\n"
    "                    for (int j = 0; j < 3; j++)\n"                          // light-march toward the sun
    "                        ls += cloudDensity(p + sunN*lstep*float(j+1), uCloudAmt, uCloudSpeed);\n"
    "                    float light = exp(-ls * 0.9);\n"                        // Beer's law (self-shadow)
    "                    float hg = 0.5 + 0.5*pow(max(dot(d,sunN),0.0), 6.0);\n" // forward scatter toward sun
    "                    vec3  cc = ambC*0.6 + sunC*light*hg;\n"
    "                    float a  = dens * dtS * 0.03;\n"                        // opacity this step
    "                    scat += T * a * cc;\n"
    "                    T *= exp(-dens * dtS * 0.03);\n"
    "                    if (T < 0.02) break;\n"
    "                }\n"
    "            }\n"
    "            float edge = smoothstep(0.03, 0.10, d.y);\n"                    // fade at horizon
    "            col = col * mix(1.0, T, edge) + scat * edge;\n"
    "        }\n"
    "    }\n"
    "    FragColor = vec4(col, 1.0);\n"
    "}\n";

/* ---- unit cube (36 verts) ---------------------------------------------- */

static const float CUBE[] = {
    -1,-1,-1,  -1,-1, 1,  -1, 1, 1,  -1, 1, 1,  -1, 1,-1,  -1,-1,-1,
     1,-1,-1,   1, 1,-1,   1, 1, 1,   1, 1, 1,   1,-1, 1,   1,-1,-1,
    -1,-1,-1,  -1, 1,-1,   1, 1,-1,   1, 1,-1,   1,-1,-1,  -1,-1,-1,
    -1,-1, 1,   1,-1, 1,   1, 1, 1,   1, 1, 1,  -1, 1, 1,  -1,-1, 1,
    -1, 1,-1,  -1, 1, 1,   1, 1, 1,   1, 1, 1,   1, 1,-1,  -1, 1,-1,
    -1,-1,-1,   1,-1,-1,   1,-1, 1,   1,-1, 1,  -1,-1, 1,  -1,-1,-1
};

/* ---- state ------------------------------------------------------------- */

static struct {
    int enabled;
    int initialized;
    G3DShaderProgram *shader;
    unsigned int vao, vbo;
    float top[3];
    float horizon[3];
    unsigned int tex_handle;  /* equirectangular panorama (0 = gradient) */
    float cloud_amt;          /* 0 = clear, 1 = heavy clouds (distant sky-dome layer) */
    float cloud_speed;
    float low_cover;          /* world-space low layer you can fly through (0 = off) */
    float low_base, low_thick, low_speed;
    float sun_dir[3], sun_col[3];   /* cached from the directional light each frame */
} g_sky = {0};

static int sky_init(void) {
    if (g_sky.initialized)
        return 1;
#ifndef VITA
    g_sky.shader = g3d_shader_create(sky_vert, sky_frag);
    if (!g_sky.shader) {
        fprintf(stderr, "G3D: sky shader failed\n");
        return 0;
    }
    glGenVertexArrays(1, &g_sky.vao);
    glGenBuffers(1, &g_sky.vbo);
    glBindVertexArray(g_sky.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_sky.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE), CUBE, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);
    glBindVertexArray(0);
#endif
    /* sensible default daytime gradient */
    g_sky.top[0] = 0.25f; g_sky.top[1] = 0.45f; g_sky.top[2] = 0.85f;
    g_sky.horizon[0] = 0.75f; g_sky.horizon[1] = 0.85f; g_sky.horizon[2] = 0.95f;
    g_sky.initialized = 1;
    return 1;
}

void g3d_sky_set_gradient(float tr, float tg, float tb,
                          float hr, float hg, float hb) {
    if (!sky_init())
        return;
    g_sky.top[0] = tr; g_sky.top[1] = tg; g_sky.top[2] = tb;
    g_sky.horizon[0] = hr; g_sky.horizon[1] = hg; g_sky.horizon[2] = hb;
    g_sky.tex_handle = 0;
    g_sky.enabled = 1;
    g3d_ibl_invalidate();   /* the sky IS the environment: re-capture it */
}

void g3d_sky_set_clouds(float amount, float speed) {
    if (!sky_init())
        return;
    g_sky.cloud_amt = amount < 0.0f ? 0.0f : (amount > 1.0f ? 1.0f : amount);
    g_sky.cloud_speed = speed > 0.0f ? speed : 1.0f;
    g3d_ibl_invalidate();
}

void g3d_sky_set_low_clouds(float cover, float base, float thick, float speed) {
    if (!sky_init()) return;
    g_sky.low_cover = cover < 0.0f ? 0.0f : (cover > 1.0f ? 1.0f : cover);
    g_sky.low_base  = base;
    g_sky.low_thick = thick > 1.0f ? thick : 1.0f;
    g_sky.low_speed = speed > 0.0f ? speed : 1.0f;
}

/* accessors for the renderer's volumetric cloud pass + the scene shader's cloud shadow */
int  g3d_sky_low_clouds(float *cover, float *base, float *thick, float *speed) {
    if (cover) *cover = g_sky.low_cover; if (base) *base = g_sky.low_base;
    if (thick) *thick = g_sky.low_thick; if (speed) *speed = g_sky.low_speed;
    return g_sky.low_cover > 0.0f;
}
void g3d_sky_get_sun(float dir[3], float col[3]) {
    if (dir) { dir[0]=g_sky.sun_dir[0]; dir[1]=g_sky.sun_dir[1]; dir[2]=g_sky.sun_dir[2]; }
    if (col) { col[0]=g_sky.sun_col[0]; col[1]=g_sky.sun_col[1]; col[2]=g_sky.sun_col[2]; }
}
void g3d_sky_get_ambient(float amb[3]) {
    if (!amb) return;   /* mid of horizon..top, used as cloud ambient */
    amb[0]=(g_sky.horizon[0]+g_sky.top[0])*0.5f;
    amb[1]=(g_sky.horizon[1]+g_sky.top[1])*0.5f;
    amb[2]=(g_sky.horizon[2]+g_sky.top[2])*0.5f;
}

void g3d_sky_set_texture(unsigned int gl_handle) {
    if (!sky_init())
        return;
    g_sky.tex_handle = gl_handle;
    g_sky.enabled = 1;
    g3d_ibl_invalidate();
}

void g3d_sky_set_enabled(int enabled) {
    if (enabled && !sky_init())
        return;
    g_sky.enabled = enabled;
}

#ifndef VITA
/* Set every sky uniform and draw the sky cube with the given orientation and
   projection. Shared by the background pass and the IBL cubemap capture, so the
   environment the PBR shader lights with is always the sky actually on screen. */
static void sky_draw(Mat4 view_rot, Mat4 proj) {
    g3d_shader_use(g_sky.shader);
    g3d_shader_set_mat4(g_sky.shader, "uViewRot", view_rot);
    g3d_shader_set_mat4(g_sky.shader, "uProjection", proj);
    g3d_shader_set_vec3(g_sky.shader, "uTopColor",
                        vec3_make(g_sky.top[0], g_sky.top[1], g_sky.top[2]));
    g3d_shader_set_vec3(g_sky.shader, "uHorizonColor",
                        vec3_make(g_sky.horizon[0], g_sky.horizon[1], g_sky.horizon[2]));

    /* Sun glow from the scene's directional light (toward = -direction) */
    Vec3 sun_dir = vec3_make(0.0f, 1.0f, 0.0f);
    Vec3 sun_col = vec3_make(1.0f, 0.95f, 0.8f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            sun_dir = vec3_scale(vec3_normalize(l->direction), -1.0f);
            sun_col = vec3_make(l->color[0], l->color[1], l->color[2]);
            break;
        }
    }
    g3d_shader_set_vec3(g_sky.shader, "uSunDir", sun_dir);
    g3d_shader_set_vec3(g_sky.shader, "uSunColor", sun_col);
    g_sky.sun_dir[0]=sun_dir.x; g_sky.sun_dir[1]=sun_dir.y; g_sky.sun_dir[2]=sun_dir.z;
    g_sky.sun_col[0]=sun_col.x; g_sky.sun_col[1]=sun_col.y; g_sky.sun_col[2]=sun_col.z;
    g3d_shader_set_float(g_sky.shader, "uTime", (float)SDL_GetTicks() / 1000.0f);
    g3d_shader_set_float(g_sky.shader, "uCloudAmt", g_sky.cloud_amt);
    g3d_shader_set_float(g_sky.shader, "uCloudSpeed", g_sky.cloud_speed > 0.0f ? g_sky.cloud_speed : 1.0f);
    g3d_shader_set_float(g_sky.shader, "uCloudLow", 260.0f);
    g3d_shader_set_float(g_sky.shader, "uCloudHigh", 520.0f);

    if (g_sky.tex_handle) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_sky.tex_handle);
        g3d_shader_set_int(g_sky.shader, "uPanorama", 0);
        g3d_shader_set_int(g_sky.shader, "uHasTex", 1);
    } else {
        g3d_shader_set_int(g_sky.shader, "uHasTex", 0);
    }

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    glBindVertexArray(g_sky.vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}
#endif

void g3d_sky_render_pass(G3DCamera *camera, int flip_y) {
    if (!g_sky.enabled || !g_sky.initialized || !g_sky.shader || !camera)
        return;
#ifndef VITA
    /* View with translation removed so the sky stays centred on the camera */
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 view_rot = view;
    view_rot.m[12] = 0.0f;
    view_rot.m[13] = 0.0f;
    view_rot.m[14] = 0.0f;

    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }

    /* Background: depth test LEQUAL so it only fills pixels still at the far
       plane (no opaque geometry), don't write depth, no culling. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    sky_draw(view_rot, proj);

    glDepthMask(GL_TRUE);
#endif
}

/* Draw the sky into whatever framebuffer is bound, for one cubemap face. No
   depth state: the capture target has no depth buffer and the sky fills it.
   Returns 0 when the sky isn't ready yet, so the IBL capture can stay dirty and
   retry instead of baking a black environment for good. */
int g3d_sky_render_env(Mat4 view_rot, Mat4 proj) {
#ifndef VITA
    if (!g_sky.initialized || !g_sky.shader)
        return 0;
    glDisable(GL_DEPTH_TEST);
    sky_draw(view_rot, proj);
    return 1;
#else
    (void)view_rot; (void)proj;
    return 0;
#endif
}

void g3d_sky_shutdown(void) {
#ifndef VITA
    if (g_sky.vbo) glDeleteBuffers(1, &g_sky.vbo);
    if (g_sky.vao) glDeleteVertexArrays(1, &g_sky.vao);
    if (g_sky.shader) g3d_shader_free(g_sky.shader);
#endif
    g_sky.vao = g_sky.vbo = 0;
    g_sky.shader = NULL;
    g_sky.initialized = 0;
    g_sky.enabled = 0;
}
