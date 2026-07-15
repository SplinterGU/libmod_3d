/*
 * libmod_3d_smaa.c - SMAA 1x, wired to the official SMAA.hlsl (GLSL_4 path)
 *
 * See libmod_3d_smaa.h for the pipeline. Notes worth keeping:
 *
 *  - SMAA_RT_METRICS is a uniform, not a compile-time constant, so a resize
 *    doesn't have to recompile three shader programs.
 *  - The lookup textures upload as-is, NOT flipped. glTexImage2D maps the first
 *    row of data to t=0 and DX10 maps it to v=0, so the bytes land where the
 *    shader's math expects them. The "flip for OpenGL" folklore is about
 *    framebuffers and image loaders, not raw texel uploads.
 *  - areaTex is sampled linearly and searchTex too (SMAA.hlsl reads it through
 *    SMAASampleLevelZero, the LINEAR sampler, despite the docs implying point).
 *  - Whether the scene image is Y-flipped for the GRAPH FBO doesn't matter:
 *    every pass works in the same space, so the result is consistent.
 */

#include "libmod_3d_smaa.h"
#include "libmod_3d_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "smaa/AreaTex.h"
#include "smaa/SearchTex.h"
#include "smaa_shader.h"
#endif

static int g_enabled = 1;

int g3d_smaa_enabled(void) { return g_enabled; }
void g3d_smaa_set_enabled(int enable) { g_enabled = enable ? 1 : 0; }

#ifndef VITA

static struct {
    int init, failed;
    int w, h;
    GLuint area_tex, search_tex;
    GLuint scene_tex, scene_fbo;     /* resolved LDR scene: SMAA's input */
    GLuint edges_tex, edges_fbo;
    GLuint blend_tex, blend_fbo;
    GLuint vao, vbo;
    G3DShaderProgram *sh_edge, *sh_weight, *sh_blend;
} g;

/* Fullscreen triangle-pair in clip space. */
static const float QUAD[] = { -1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1 };

/* Build one SMAA program: the official header plus a small wrapper. Vertex and
   fragment stages include the same source with the opposite half gated out. */
static G3DShaderProgram *make_prog(const char *vs_main, const char *fs_main) {
    static const char *HDR =
        "#version 400 core\n"
        "#define SMAA_GLSL_4 1\n"
        "#define SMAA_PRESET_HIGH 1\n"
        "#define SMAA_RT_METRICS uSMAARTMetrics\n"
        "uniform vec4 uSMAARTMetrics;\n";

    size_t len = strlen(HDR) + strlen(SMAA_SRC_GLSL) + 4096;
    char *vs = (char *)malloc(len);
    char *fs = (char *)malloc(len);
    if (!vs || !fs) { free(vs); free(fs); return NULL; }

    snprintf(vs, len, "%s#define SMAA_INCLUDE_VS 1\n#define SMAA_INCLUDE_PS 0\n%s\n%s",
             HDR, SMAA_SRC_GLSL, vs_main);
    snprintf(fs, len, "%s#define SMAA_INCLUDE_VS 0\n#define SMAA_INCLUDE_PS 1\n%s\n%s",
             HDR, SMAA_SRC_GLSL, fs_main);

    G3DShaderProgram *p = g3d_shader_create(vs, fs);
    free(vs);
    free(fs);
    return p;
}

static GLuint make_rt(GLuint *fbo, int w, int h, GLenum internal, GLenum fmt) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, fmt, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return tex;
}

static void free_targets(void) {
    if (g.scene_tex) { glDeleteTextures(1, &g.scene_tex); glDeleteFramebuffers(1, &g.scene_fbo); }
    if (g.edges_tex) { glDeleteTextures(1, &g.edges_tex); glDeleteFramebuffers(1, &g.edges_fbo); }
    if (g.blend_tex) { glDeleteTextures(1, &g.blend_tex); glDeleteFramebuffers(1, &g.blend_fbo); }
    g.scene_tex = g.edges_tex = g.blend_tex = 0;
}

static int smaa_init(void) {
    if (g.failed) return 0;
    if (g.init)   return 1;

    g.sh_edge = make_prog(
        "layout (location = 0) in vec2 aPos;\n"
        "out vec2 vTex;\n"
        "out vec4 vOffset[3];\n"
        "void main() {\n"
        "    vTex = aPos * 0.5 + 0.5;\n"
        "    SMAAEdgeDetectionVS(vTex, vOffset);\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n",
        "in vec2 vTex;\n"
        "in vec4 vOffset[3];\n"
        "uniform sampler2D uColorTex;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = vec4(SMAALumaEdgeDetectionPS(vTex, vOffset, uColorTex), 0.0, 0.0);\n"
        "}\n");

    g.sh_weight = make_prog(
        "layout (location = 0) in vec2 aPos;\n"
        "out vec2 vTex;\n"
        "out vec2 vPixcoord;\n"
        "out vec4 vOffset[3];\n"
        "void main() {\n"
        "    vTex = aPos * 0.5 + 0.5;\n"
        "    SMAABlendingWeightCalculationVS(vTex, vPixcoord, vOffset);\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n",
        "in vec2 vTex;\n"
        "in vec2 vPixcoord;\n"
        "in vec4 vOffset[3];\n"
        "uniform sampler2D uEdgesTex;\n"
        "uniform sampler2D uAreaTex;\n"
        "uniform sampler2D uSearchTex;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = SMAABlendingWeightCalculationPS(vTex, vPixcoord, vOffset,\n"
        "                    uEdgesTex, uAreaTex, uSearchTex, vec4(0.0));\n"
        "}\n");

    g.sh_blend = make_prog(
        "layout (location = 0) in vec2 aPos;\n"
        "out vec2 vTex;\n"
        "out vec4 vOffset;\n"
        "void main() {\n"
        "    vTex = aPos * 0.5 + 0.5;\n"
        "    SMAANeighborhoodBlendingVS(vTex, vOffset);\n"
        "    gl_Position = vec4(aPos, 0.0, 1.0);\n"
        "}\n",
        "in vec2 vTex;\n"
        "in vec4 vOffset;\n"
        "uniform sampler2D uColorTex;\n"
        "uniform sampler2D uBlendTex;\n"
        "out vec4 FragColor;\n"
        "void main() {\n"
        "    FragColor = SMAANeighborhoodBlendingPS(vTex, vOffset, uColorTex, uBlendTex);\n"
        "}\n");

    if (!g.sh_edge || !g.sh_weight || !g.sh_blend) {
        fprintf(stderr, "G3D: SMAA shaders failed to build - anti-aliasing disabled\n");
        g.failed = 1;
        return 0;
    }

    glGenVertexArrays(1, &g.vao);
    glGenBuffers(1, &g.vbo);
    glBindVertexArray(g.vao);
    glBindBuffer(GL_ARRAY_BUFFER, g.vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(QUAD), QUAD, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *)0);
    glBindVertexArray(0);

    /* The two precomputed lookup tables, uploaded exactly as stored. */
    glGenTextures(1, &g.area_tex);
    glBindTexture(GL_TEXTURE_2D, g.area_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT, 0,
                 GL_RG, GL_UNSIGNED_BYTE, areaTexBytes);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &g.search_tex);
    glBindTexture(GL_TEXTURE_2D, g.search_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);          /* 64px of R8 rows */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, 0,
                 GL_RED, GL_UNSIGNED_BYTE, searchTexBytes);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    g.init = 1;
    printf("G3D: SMAA 1x ready (preset HIGH, GLSL_4)\n");
    return 1;
}

static int ensure_targets(int w, int h) {
    if (w <= 0 || h <= 0)
        return 0;
    if (g.scene_tex && g.w == w && g.h == h)
        return 1;
    free_targets();
    GLint prev = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
    g.scene_tex = make_rt(&g.scene_fbo, w, h, GL_RGBA8, GL_RGBA);
    g.edges_tex = make_rt(&g.edges_fbo, w, h, GL_RG8,   GL_RG);
    g.blend_tex = make_rt(&g.blend_fbo, w, h, GL_RGBA8, GL_RGBA);
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
    g.w = w; g.h = h;
    return g.scene_tex && g.edges_tex && g.blend_tex;
}

unsigned int g3d_smaa_scene_fbo(int width, int height) {
    if (!g_enabled || !smaa_init() || !ensure_targets(width, height))
        return 0;
    return g.scene_fbo;
}

void g3d_smaa_apply(unsigned int dst_fbo, int vp_x, int vp_y, int vp_w, int vp_h) {
    if (!g_enabled || !g.init || !g.scene_tex)
        return;

    Vec4 metrics = vec4_make(1.0f / (float)g.w, 1.0f / (float)g.h, (float)g.w, (float)g.h);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(g.vao);

    /* 1. edges */
    glBindFramebuffer(GL_FRAMEBUFFER, g.edges_fbo);
    glViewport(0, 0, g.w, g.h);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    g3d_shader_use(g.sh_edge);
    g3d_shader_set_vec4(g.sh_edge, "uSMAARTMetrics", metrics);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.scene_tex);
    g3d_shader_set_int(g.sh_edge, "uColorTex", 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* 2. blending weights */
    glBindFramebuffer(GL_FRAMEBUFFER, g.blend_fbo);
    glViewport(0, 0, g.w, g.h);
    glClear(GL_COLOR_BUFFER_BIT);
    g3d_shader_use(g.sh_weight);
    g3d_shader_set_vec4(g.sh_weight, "uSMAARTMetrics", metrics);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.edges_tex);
    g3d_shader_set_int(g.sh_weight, "uEdgesTex", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g.area_tex);
    g3d_shader_set_int(g.sh_weight, "uAreaTex", 1);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, g.search_tex);
    g3d_shader_set_int(g.sh_weight, "uSearchTex", 2);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* 3. neighborhood blending -> the real target */
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(vp_x, vp_y, vp_w, vp_h);
    g3d_shader_use(g.sh_blend);
    g3d_shader_set_vec4(g.sh_blend, "uSMAARTMetrics", metrics);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, g.scene_tex);
    g3d_shader_set_int(g.sh_blend, "uColorTex", 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, g.blend_tex);
    g3d_shader_set_int(g.sh_blend, "uBlendTex", 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void g3d_smaa_shutdown(void) {
    if (!g.init)
        return;
    free_targets();
    if (g.area_tex)   glDeleteTextures(1, &g.area_tex);
    if (g.search_tex) glDeleteTextures(1, &g.search_tex);
    if (g.vao) glDeleteVertexArrays(1, &g.vao);
    if (g.vbo) glDeleteBuffers(1, &g.vbo);
    if (g.sh_edge)   g3d_shader_free(g.sh_edge);
    if (g.sh_weight) g3d_shader_free(g.sh_weight);
    if (g.sh_blend)  g3d_shader_free(g.sh_blend);
    memset(&g, 0, sizeof(g));
}

#else /* VITA */

unsigned int g3d_smaa_scene_fbo(int w, int h) { (void)w; (void)h; return 0; }
void g3d_smaa_apply(unsigned int f, int a, int b, int c, int d) {
    (void)f; (void)a; (void)b; (void)c; (void)d;
}
void g3d_smaa_shutdown(void) {}

#endif
