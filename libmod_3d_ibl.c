/*
 * libmod_3d_ibl.c - Image Based Lighting built from the scene's own sky
 *
 * Pipeline (all lazy, only when the sky changes):
 *   1. sky -> environment cubemap      (the sky shader drawn into 6 faces)
 *   2. env -> irradiance cubemap       (cosine convolution, diffuse IBL)
 *   3. env -> prefiltered mip chain    (GGX importance sampling, specular IBL)
 *   4. BRDF integration LUT            (split-sum, built once)
 *
 * The PBR shader then does the standard split-sum:
 *   diffuse  = irradiance(N) * albedo * kD
 *   specular = prefiltered(R, roughness) * (F * A + B)
 */

#include "libmod_3d_ibl.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

#define ENV_SIZE   128
#define IRR_SIZE   32
#define PREF_SIZE  128
#define PREF_MIPS  5
#define BRDF_SIZE  256

static struct {
    int initialized;
    int dirty;
    int enabled;
    int ready;
    float intensity;
#ifndef VITA
    GLuint env_cube, irr_cube, pref_cube, brdf_lut;
    GLuint fbo;
    GLuint cube_vao, cube_vbo;
    GLuint quad_vao, quad_vbo;
#endif
    G3DShaderProgram *sh_irr, *sh_pref, *sh_brdf;
} g_ibl = { 0, 1, 1, 0, 1.0f };

void g3d_ibl_set_enabled(int enable) {
    if (g_ibl.enabled != (enable ? 1 : 0)) {
        g_ibl.enabled = enable ? 1 : 0;
        g_ibl.dirty = 1;
    }
}
int g3d_ibl_enabled(void) { return g_ibl.enabled; }

void g3d_ibl_set_intensity(float intensity) {
    g_ibl.intensity = (intensity < 0.0f) ? 0.0f : intensity;
}
float g3d_ibl_intensity(void) { return g_ibl.intensity; }

void g3d_ibl_invalidate(void) { g_ibl.dirty = 1; }

float g3d_ibl_prefilter_mips(void) { return (float)PREF_MIPS; }

#ifndef VITA

/* ---- shaders ------------------------------------------------------------- */

/* Draws a unit cube; the fragment stage uses the local position as a direction. */
static const char *VS_CUBE =
"#version 330 core\n"
"layout (location = 0) in vec3 aPos;\n"
"uniform mat4 uViewRot;\n"
"uniform mat4 uProjection;\n"
"out vec3 vLocal;\n"
"void main() {\n"
"    vLocal = aPos;\n"
"    gl_Position = uProjection * uViewRot * vec4(aPos, 1.0);\n"
"}\n";

/* Diffuse irradiance: cosine-weighted hemisphere convolution of the env map. */
static const char *FS_IRRADIANCE =
"#version 330 core\n"
"in vec3 vLocal;\n"
"out vec4 FragColor;\n"
"uniform samplerCube uEnv;\n"
"const float PI = 3.14159265359;\n"
"void main() {\n"
"    vec3 N = normalize(vLocal);\n"
"    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(0.0, 0.0, 1.0);\n"
"    vec3 right = normalize(cross(up, N));\n"
"    up = normalize(cross(N, right));\n"
"    vec3 irradiance = vec3(0.0);\n"
"    float nrSamples = 0.0;\n"
"    const float dPhi = 0.025;\n"
"    const float dTheta = 0.025;\n"
"    for (float phi = 0.0; phi < 2.0 * PI; phi += dPhi) {\n"
"        for (float theta = 0.0; theta < 0.5 * PI; theta += dTheta) {\n"
"            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));\n"
"            vec3 dir = tangent.x * right + tangent.y * up + tangent.z * N;\n"
"            irradiance += texture(uEnv, dir).rgb * cos(theta) * sin(theta);\n"
"            nrSamples += 1.0;\n"
"        }\n"
"    }\n"
"    irradiance = PI * irradiance / max(nrSamples, 1.0);\n"
"    FragColor = vec4(irradiance, 1.0);\n"
"}\n";

/* Specular prefilter: GGX importance sampling per roughness level. */
static const char *FS_PREFILTER =
"#version 330 core\n"
"in vec3 vLocal;\n"
"out vec4 FragColor;\n"
"uniform samplerCube uEnv;\n"
"uniform float uRoughness;\n"
"uniform float uEnvSize;\n"
"const float PI = 3.14159265359;\n"
"float radicalInverse(uint bits) {\n"
"    bits = (bits << 16u) | (bits >> 16u);\n"
"    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
"    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
"    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
"    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
"    return float(bits) * 2.3283064365386963e-10;\n"
"}\n"
"vec2 hammersley(uint i, uint N) { return vec2(float(i) / float(N), radicalInverse(i)); }\n"
"vec3 importanceSampleGGX(vec2 Xi, vec3 N, float rough) {\n"
"    float a = rough * rough;\n"
"    float phi = 2.0 * PI * Xi.x;\n"
"    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));\n"
"    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);\n"
"    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);\n"
"    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
"    vec3 tangent = normalize(cross(up, N));\n"
"    vec3 bitangent = cross(N, tangent);\n"
"    return normalize(tangent * H.x + bitangent * H.y + N * H.z);\n"
"}\n"
"float distGGX(float NdotH, float rough) {\n"
"    float a = rough * rough;\n"
"    float a2 = a * a;\n"
"    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;\n"
"    return a2 / max(PI * d * d, 1e-7);\n"
"}\n"
"void main() {\n"
"    vec3 N = normalize(vLocal);\n"
"    vec3 R = N, V = N;\n"
"    const uint SAMPLES = 128u;\n"
"    vec3 color = vec3(0.0);\n"
"    float weight = 0.0;\n"
"    for (uint i = 0u; i < SAMPLES; ++i) {\n"
"        vec2 Xi = hammersley(i, SAMPLES);\n"
"        vec3 H = importanceSampleGGX(Xi, N, uRoughness);\n"
"        vec3 L = normalize(2.0 * dot(V, H) * H - V);\n"
"        float NdotL = max(dot(N, L), 0.0);\n"
"        if (NdotL <= 0.0) continue;\n"
"        /* Sample a mip chosen from the sample density: kills the fireflies a\n"
"           flat mip-0 fetch produces on bright suns. */\n"
"        float NdotH = max(dot(N, H), 0.0);\n"
"        float HdotV = max(dot(H, V), 0.0);\n"
"        float D = distGGX(NdotH, uRoughness);\n"
"        float pdf = (D * NdotH / (4.0 * max(HdotV, 1e-4))) + 1e-4;\n"
"        float saTexel = 4.0 * PI / (6.0 * uEnvSize * uEnvSize);\n"
"        float saSample = 1.0 / (float(SAMPLES) * pdf + 1e-4);\n"
"        float mip = (uRoughness == 0.0) ? 0.0 : 0.5 * log2(saSample / saTexel);\n"
"        color += textureLod(uEnv, L, max(mip, 0.0)).rgb * NdotL;\n"
"        weight += NdotL;\n"
"    }\n"
"    FragColor = vec4(color / max(weight, 1e-4), 1.0);\n"
"}\n";

/* BRDF integration LUT (split-sum second term): x = NdotV, y = roughness. */
static const char *VS_QUAD =
"#version 330 core\n"
"layout (location = 0) in vec2 aPos;\n"
"out vec2 vUV;\n"
"void main() {\n"
"    vUV = aPos * 0.5 + 0.5;\n"
"    gl_Position = vec4(aPos, 0.0, 1.0);\n"
"}\n";

static const char *FS_BRDF =
"#version 330 core\n"
"in vec2 vUV;\n"
"out vec2 FragColor;\n"
"const float PI = 3.14159265359;\n"
"float radicalInverse(uint bits) {\n"
"    bits = (bits << 16u) | (bits >> 16u);\n"
"    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);\n"
"    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);\n"
"    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);\n"
"    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);\n"
"    return float(bits) * 2.3283064365386963e-10;\n"
"}\n"
"vec2 hammersley(uint i, uint N) { return vec2(float(i) / float(N), radicalInverse(i)); }\n"
"vec3 importanceSampleGGX(vec2 Xi, vec3 N, float rough) {\n"
"    float a = rough * rough;\n"
"    float phi = 2.0 * PI * Xi.x;\n"
"    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));\n"
"    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);\n"
"    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);\n"
"    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);\n"
"    vec3 tangent = normalize(cross(up, N));\n"
"    vec3 bitangent = cross(N, tangent);\n"
"    return normalize(tangent * H.x + bitangent * H.y + N * H.z);\n"
"}\n"
/* Smith geometry with the IBL k (k = a^2/2), not the direct-light one. */
"float geomSchlickIBL(float NdotX, float rough) {\n"
"    float a = rough * rough;\n"
"    float k = a * a / 2.0;\n"
"    return NdotX / (NdotX * (1.0 - k) + k);\n"
"}\n"
"void main() {\n"
"    float NdotV = max(vUV.x, 1e-3);\n"
"    float rough = vUV.y;\n"
"    vec3 V = vec3(sqrt(1.0 - NdotV * NdotV), 0.0, NdotV);\n"
"    vec3 N = vec3(0.0, 0.0, 1.0);\n"
"    float A = 0.0, B = 0.0;\n"
"    const uint SAMPLES = 512u;\n"
"    for (uint i = 0u; i < SAMPLES; ++i) {\n"
"        vec2 Xi = hammersley(i, SAMPLES);\n"
"        vec3 H = importanceSampleGGX(Xi, N, rough);\n"
"        vec3 L = normalize(2.0 * dot(V, H) * H - V);\n"
"        float NdotL = max(L.z, 0.0);\n"
"        if (NdotL <= 0.0) continue;\n"
"        float NdotH = max(H.z, 0.0);\n"
"        float VdotH = max(dot(V, H), 0.0);\n"
"        float G = geomSchlickIBL(NdotV, rough) * geomSchlickIBL(NdotL, rough);\n"
"        float Gvis = (G * VdotH) / max(NdotH * NdotV, 1e-4);\n"
"        float Fc = pow(1.0 - VdotH, 5.0);\n"
"        A += (1.0 - Fc) * Gvis;\n"
"        B += Fc * Gvis;\n"
"    }\n"
"    FragColor = vec2(A, B) / float(SAMPLES);\n"
"}\n";

/* ---- geometry ------------------------------------------------------------ */

static const float CUBE_VERTS[] = {
    -1,-1,-1,  -1, 1, 1,  -1, 1,-1,   -1,-1,-1,  -1,-1, 1,  -1, 1, 1,
     1,-1,-1,   1, 1,-1,   1, 1, 1,    1,-1,-1,   1, 1, 1,   1,-1, 1,
    -1,-1,-1,   1,-1,-1,   1,-1, 1,   -1,-1,-1,   1,-1, 1,  -1,-1, 1,
    -1, 1,-1,  -1, 1, 1,   1, 1, 1,   -1, 1,-1,   1, 1, 1,   1, 1,-1,
    -1,-1,-1,   1, 1,-1,   1,-1,-1,   -1,-1,-1,  -1, 1,-1,   1, 1,-1,
    -1,-1, 1,   1,-1, 1,   1, 1, 1,   -1,-1, 1,   1, 1, 1,  -1, 1, 1,
};

static const float QUAD_VERTS[] = {
    -1,-1,   1,-1,   1, 1,
    -1,-1,   1, 1,  -1, 1,
};

static void make_cubemap(GLuint *tex, int size, int mips) {
    glGenTextures(1, tex);
    glBindTexture(GL_TEXTURE_CUBE_MAP, *tex);
    for (int f = 0; f < 6; f++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, NULL);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips)
        glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
}

static int ibl_init(void) {
    if (g_ibl.initialized)
        return 1;

    g_ibl.sh_irr  = g3d_shader_create(VS_CUBE, FS_IRRADIANCE);
    g_ibl.sh_pref = g3d_shader_create(VS_CUBE, FS_PREFILTER);
    g_ibl.sh_brdf = g3d_shader_create(VS_QUAD, FS_BRDF);
    if (!g_ibl.sh_irr || !g_ibl.sh_pref || !g_ibl.sh_brdf) {
        fprintf(stderr, "G3D: IBL shader compile failed\n");
        return 0;
    }

    glGenVertexArrays(1, &g_ibl.cube_vao);
    glGenBuffers(1, &g_ibl.cube_vbo);
    glBindVertexArray(g_ibl.cube_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ibl.cube_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_VERTS), CUBE_VERTS, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

    glGenVertexArrays(1, &g_ibl.quad_vao);
    glGenBuffers(1, &g_ibl.quad_vbo);
    glBindVertexArray(g_ibl.quad_vao);
    glBindBuffer(GL_ARRAY_BUFFER, g_ibl.quad_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD_VERTS), QUAD_VERTS, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);

    make_cubemap(&g_ibl.env_cube, ENV_SIZE, 1);
    make_cubemap(&g_ibl.irr_cube, IRR_SIZE, 0);
    make_cubemap(&g_ibl.pref_cube, PREF_SIZE, 1);

    glGenTextures(1, &g_ibl.brdf_lut);
    glBindTexture(GL_TEXTURE_2D, g_ibl.brdf_lut);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, BRDF_SIZE, BRDF_SIZE, 0, GL_RG, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glGenFramebuffers(1, &g_ibl.fbo);

    /* The BRDF LUT depends on nothing but the BRDF: bake it once. */
    GLint prev_fbo = 0, prev_vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);
    glBindFramebuffer(GL_FRAMEBUFFER, g_ibl.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           g_ibl.brdf_lut, 0);
    glViewport(0, 0, BRDF_SIZE, BRDF_SIZE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    g3d_shader_use(g_ibl.sh_brdf);
    glBindVertexArray(g_ibl.quad_vao);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    g_ibl.initialized = 1;
    printf("G3D: IBL initialised (env %d², irradiance %d², prefilter %d² x%d mips, BRDF LUT %d²)\n",
           ENV_SIZE, IRR_SIZE, PREF_SIZE, PREF_MIPS, BRDF_SIZE);
    return 1;
}

/* The 6 cubemap face orientations (OpenGL convention). */
static void face_views(Mat4 out[6]) {
    Vec3 o = vec3_make(0.0f, 0.0f, 0.0f);
    out[0] = mat4_look_at(o, vec3_make( 1,  0,  0), vec3_make(0, -1,  0));
    out[1] = mat4_look_at(o, vec3_make(-1,  0,  0), vec3_make(0, -1,  0));
    out[2] = mat4_look_at(o, vec3_make( 0,  1,  0), vec3_make(0,  0,  1));
    out[3] = mat4_look_at(o, vec3_make( 0, -1,  0), vec3_make(0,  0, -1));
    out[4] = mat4_look_at(o, vec3_make( 0,  0,  1), vec3_make(0, -1,  0));
    out[5] = mat4_look_at(o, vec3_make( 0,  0, -1), vec3_make(0, -1,  0));
}

static void draw_cube(void) {
    glBindVertexArray(g_ibl.cube_vao);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
}

void g3d_ibl_update(void) {
    if (!g_ibl.enabled || !g_ibl.dirty)
        return;
    if (!ibl_init())
        return;

    GLint prev_fbo = 0, prev_vp[4];
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev_fbo);
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    Mat4 views[6];
    face_views(views);
    Mat4 proj = mat4_perspective(90.0f, 1.0f, 0.1f, 10.0f);

    glBindFramebuffer(GL_FRAMEBUFFER, g_ibl.fbo);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);

    /* 1. Capture the sky into the environment cubemap. If the sky isn't up yet
       (no scene has configured it), leave the capture dirty and retry next
       frame rather than baking a black environment permanently. */
    glViewport(0, 0, ENV_SIZE, ENV_SIZE);
    int sky_ok = 1;
    for (int f = 0; f < 6; f++) {
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, g_ibl.env_cube, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        if (!g3d_sky_render_env(views[f], proj))
            sky_ok = 0;
    }
    if (!sky_ok) {
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        glEnable(GL_DEPTH_TEST);
        return;   /* still dirty: retry once the sky exists */
    }
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl.env_cube);
    glGenerateMipmap(GL_TEXTURE_CUBE_MAP);   /* prefilter samples these mips */

    glDisable(GL_DEPTH_TEST);

    /* 2. Diffuse irradiance. */
    g3d_shader_use(g_ibl.sh_irr);
    g3d_shader_set_mat4(g_ibl.sh_irr, "uProjection", proj);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl.env_cube);
    g3d_shader_set_int(g_ibl.sh_irr, "uEnv", 0);
    glViewport(0, 0, IRR_SIZE, IRR_SIZE);
    for (int f = 0; f < 6; f++) {
        g3d_shader_set_mat4(g_ibl.sh_irr, "uViewRot", views[f]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, g_ibl.irr_cube, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        draw_cube();
    }

    /* 3. Specular prefilter: one mip per roughness step. */
    g3d_shader_use(g_ibl.sh_pref);
    g3d_shader_set_mat4(g_ibl.sh_pref, "uProjection", proj);
    g3d_shader_set_float(g_ibl.sh_pref, "uEnvSize", (float)ENV_SIZE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl.env_cube);
    g3d_shader_set_int(g_ibl.sh_pref, "uEnv", 0);
    for (int mip = 0; mip < PREF_MIPS; mip++) {
        int msize = PREF_SIZE >> mip;
        if (msize < 1) msize = 1;
        glViewport(0, 0, msize, msize);
        float rough = (float)mip / (float)(PREF_MIPS - 1);
        g3d_shader_set_float(g_ibl.sh_pref, "uRoughness", rough);
        for (int f = 0; f < 6; f++) {
            g3d_shader_set_mat4(g_ibl.sh_pref, "uViewRot", views[f]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, g_ibl.pref_cube, mip);
            glClear(GL_COLOR_BUFFER_BIT);
            draw_cube();
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev_fbo);
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
    glEnable(GL_DEPTH_TEST);

    g_ibl.dirty = 0;
    g_ibl.ready = 1;

}

int g3d_ibl_bind(int unit_irradiance, int unit_prefilter, int unit_brdf) {
    if (!g_ibl.enabled || !g_ibl.ready)
        return 0;
    glActiveTexture(GL_TEXTURE0 + unit_irradiance);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl.irr_cube);
    glActiveTexture(GL_TEXTURE0 + unit_prefilter);
    glBindTexture(GL_TEXTURE_CUBE_MAP, g_ibl.pref_cube);
    glActiveTexture(GL_TEXTURE0 + unit_brdf);
    glBindTexture(GL_TEXTURE_2D, g_ibl.brdf_lut);
    return 1;
}

void g3d_ibl_shutdown(void) {
    if (!g_ibl.initialized)
        return;
    glDeleteTextures(1, &g_ibl.env_cube);
    glDeleteTextures(1, &g_ibl.irr_cube);
    glDeleteTextures(1, &g_ibl.pref_cube);
    glDeleteTextures(1, &g_ibl.brdf_lut);
    glDeleteFramebuffers(1, &g_ibl.fbo);
    glDeleteVertexArrays(1, &g_ibl.cube_vao);
    glDeleteBuffers(1, &g_ibl.cube_vbo);
    glDeleteVertexArrays(1, &g_ibl.quad_vao);
    glDeleteBuffers(1, &g_ibl.quad_vbo);
    if (g_ibl.sh_irr)  g3d_shader_free(g_ibl.sh_irr);
    if (g_ibl.sh_pref) g3d_shader_free(g_ibl.sh_pref);
    if (g_ibl.sh_brdf) g3d_shader_free(g_ibl.sh_brdf);
    g_ibl.initialized = 0;
    g_ibl.ready = 0;
}

#else  /* VITA: no IBL (kept as the flat-ambient fallback) */

void g3d_ibl_update(void) {}
int  g3d_ibl_bind(int a, int b, int c) { (void)a; (void)b; (void)c; return 0; }
void g3d_ibl_shutdown(void) {}

#endif
