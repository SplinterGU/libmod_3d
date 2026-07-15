/*
 * libmod_3d_fsr.c - FSR 1 (EASU + RCAS) via AMD's official ffx_fsr1.h
 *
 * See libmod_3d_fsr.h for the why. Notes:
 *  - Only the float32 path (FSR_EASU_F / FSR_RCAS_F). The packed 16-bit and
 *    wave paths need extensions a GL 4.0 context doesn't have.
 *  - EASU's constants are computed here rather than pulling ffx_a.h into C:
 *    FsrEasuCon is a handful of divisions bitcast to uint, so this reimplements
 *    it directly (kept next to the original's comments for checking).
 *  - EASU reads with textureGather (GL 4.0 core), so the input must be POINT
 *    sampled: it fetches exact texels and does its own filtering.
 */

#include "libmod_3d_fsr.h"
#include "libmod_3d_shader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#include "fsr_shader.h"
#endif

static int g_enabled = 0;          /* opt-in: it changes how the frame looks */
static float g_sharpness = 0.25f;

int g3d_fsr_enabled(void) { return g_enabled; }
void g3d_fsr_set_enabled(int enable) { g_enabled = enable ? 1 : 0; }
void g3d_fsr_set_sharpness(float s) {
    if (s < 0.0f) s = 0.0f;
    if (s > 2.0f) s = 2.0f;
    g_sharpness = s;
}

#ifndef VITA

static struct {
    int init, failed;
    int rw, rh, dw, dh;
    GLuint in_tex, in_fbo;         /* render-res frame: EASU's input */
    GLuint up_tex, up_fbo;         /* display-res upscaled: RCAS's input */
    GLuint vao, vbo;
    G3DShaderProgram *sh_easu, *sh_rcas;
} g;

static const float QUAD[] = { -1,-1,  1,-1,  1,1,  -1,-1,  1,1,  -1,1 };

static unsigned int f2u(float f) {
    unsigned int u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

/* Reimplementation of FsrEasuCon (ffx_fsr1.h) - see that file for the diagrams. */
static void easu_con(unsigned int c0[4], unsigned int c1[4],
                     unsigned int c2[4], unsigned int c3[4],
                     float in_vw, float in_vh,      /* rendered viewport */
                     float in_w,  float in_h,       /* input resource size */
                     float out_w, float out_h) {    /* display size */
    /* Output integer position to a pixel position in viewport. */
    c0[0] = f2u(in_vw / out_w);
    c0[1] = f2u(in_vh / out_h);
    c0[2] = f2u(0.5f * in_vw / out_w - 0.5f);
    c0[3] = f2u(0.5f * in_vh / out_h - 0.5f);
    /* Viewport pixel position to normalized image space. */
    c1[0] = f2u(1.0f / in_w);
    c1[1] = f2u(1.0f / in_h);
    /* Centers of gather4, first offset from upper-left of 'F'. */
    c1[2] = f2u( 1.0f / in_w);
    c1[3] = f2u(-1.0f / in_h);
    /* These are from (0) instead of 'F'. */
    c2[0] = f2u(-1.0f / in_w);
    c2[1] = f2u( 2.0f / in_h);
    c2[2] = f2u( 1.0f / in_w);
    c2[3] = f2u( 2.0f / in_h);
    c3[0] = f2u( 0.0f / in_w);
    c3[1] = f2u( 4.0f / in_h);
    c3[2] = 0;
    c3[3] = 0;
}

/* FsrRcasCon: sharpness is in stops, the shader wants exp2(-sharpness). */
static void rcas_con(unsigned int c[4], float sharpness) {
    c[0] = f2u(exp2f(-sharpness));
    c[1] = 0;   /* the rest is only used by the packed-16 path */
    c[2] = 0;
    c[3] = 0;
}

static const char *VS_QUAD =
"#version 400 core\n"
"layout (location = 0) in vec2 aPos;\n"
"void main() { gl_Position = vec4(aPos, 0.0, 1.0); }\n";

static G3DShaderProgram *make_prog(const char *defines, const char *body) {
    size_t len = strlen(FFX_A_GLSL) + strlen(FFX_FSR1_GLSL) + strlen(body) + 8192;
    char *fs = (char *)malloc(len);
    if (!fs) return NULL;
    snprintf(fs, len,
        "#version 400 core\n"
        /* ffx_a.h defines its packing helpers with packHalf2x16, which core GLSL
           only has from 420. This extension backports it to our 4.0 context -
           the driver's own error message points at it. The helpers themselves
           are only used by the packed-16 path we don't take, but they still have
           to compile. */
        "#extension GL_ARB_shading_language_packing : enable\n"
        "#define A_GPU 1\n"
        "#define A_GLSL 1\n"
        "%s"                    /* per-pass sampler + callbacks + FSR_*_F */
        "%s",                   /* ffx_a + ffx_fsr1 + main, assembled by caller */
        defines, body);
    G3DShaderProgram *p = g3d_shader_create(VS_QUAD, fs);
    free(fs);
    return p;
}

static int fsr_init(void) {
    if (g.failed) return 0;
    if (g.init)   return 1;

    /* EASU: ffx_a, then the gather callbacks, then ffx_fsr1, then main. */
    {
        size_t len = strlen(FFX_A_GLSL) + strlen(FFX_FSR1_GLSL) + 8192;
        char *body = (char *)malloc(len);
        if (!body) { g.failed = 1; return 0; }
        snprintf(body, len,
            "%s\n"
            "uniform sampler2D uInput;\n"
            "#define FSR_EASU_F 1\n"
            "AF4 FsrEasuRF(AF2 p) { return AF4(textureGather(uInput, p, 0)); }\n"
            "AF4 FsrEasuGF(AF2 p) { return AF4(textureGather(uInput, p, 1)); }\n"
            "AF4 FsrEasuBF(AF2 p) { return AF4(textureGather(uInput, p, 2)); }\n"
            "%s\n"
            "uniform uvec4 uCon0, uCon1, uCon2, uCon3;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    AF3 c;\n"
            "    FsrEasuF(c, AU2(gl_FragCoord.xy), uCon0, uCon1, uCon2, uCon3);\n"
            "    FragColor = vec4(c, 1.0);\n"
            "}\n",
            FFX_A_GLSL, FFX_FSR1_GLSL);
        g.sh_easu = make_prog("", body);
        free(body);
    }

    /* RCAS: same shape, its own callbacks. */
    {
        size_t len = strlen(FFX_A_GLSL) + strlen(FFX_FSR1_GLSL) + 8192;
        char *body = (char *)malloc(len);
        if (!body) { g.failed = 1; return 0; }
        snprintf(body, len,
            "%s\n"
            "uniform sampler2D uInput;\n"
            "#define FSR_RCAS_F 1\n"
            "#define FSR_RCAS_DENOISE 1\n"
            "AF4 FsrRcasLoadF(ASU2 p) { return AF4(texelFetch(uInput, ASU2(p), 0)); }\n"
            "void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}\n"
            "%s\n"
            "uniform uvec4 uConRcas;\n"
            "uniform vec2 uOutOffset;\n"
            "out vec4 FragColor;\n"
            "void main() {\n"
            "    AF3 c;\n"
            /* gl_FragCoord is in FRAMEBUFFER space, but we texelFetch the
               upscaled image, which starts at the viewport origin. With a
               letterboxed/offset viewport (BennuGD scales its physical viewport
               and renders into FBO 0) those differ and the image comes out
               shifted by the offset. */
            "    FsrRcasF(c.r, c.g, c.b, AU2(gl_FragCoord.xy - uOutOffset), uConRcas);\n"
            "    FragColor = vec4(c, 1.0);\n"
            "}\n",
            FFX_A_GLSL, FFX_FSR1_GLSL);
        g.sh_rcas = make_prog("", body);
        free(body);
    }

    if (!g.sh_easu || !g.sh_rcas) {
        fprintf(stderr, "G3D: FSR shaders failed to build - upscaling disabled\n");
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

    g.init = 1;
    printf("G3D: FSR 1 ready (EASU + RCAS, float path)\n");
    return 1;
}

static GLuint make_rt(GLuint *fbo, int w, int h, int point) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    /* EASU gathers exact texels and filters them itself: no hardware filtering. */
    GLint f = point ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenFramebuffers(1, fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, *fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    return tex;
}

static void free_targets(void) {
    if (g.in_tex) { glDeleteTextures(1, &g.in_tex); glDeleteFramebuffers(1, &g.in_fbo); }
    if (g.up_tex) { glDeleteTextures(1, &g.up_tex); glDeleteFramebuffers(1, &g.up_fbo); }
    g.in_tex = g.up_tex = 0;
}

unsigned int g3d_fsr_input_fbo(int render_w, int render_h, int display_w, int display_h) {
    if (!g_enabled || render_w <= 0 || render_h <= 0)
        return 0;
    if (render_w == display_w && render_h == display_h)
        return 0;                       /* nothing to upscale */
    if (!fsr_init())
        return 0;
    if (!g.in_tex || g.rw != render_w || g.rh != render_h ||
        g.dw != display_w || g.dh != display_h) {
        GLint prev = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prev);
        free_targets();
        g.in_tex = make_rt(&g.in_fbo, render_w, render_h, 1);   /* point: EASU gathers */
        g.up_tex = make_rt(&g.up_fbo, display_w, display_h, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prev);
        g.rw = render_w; g.rh = render_h;
        g.dw = display_w; g.dh = display_h;
        printf("G3D: FSR upscaling %dx%d -> %dx%d (%.0f%% of the pixels)\n",
               render_w, render_h, display_w, display_h,
               100.0 * (double)(render_w * render_h) / (double)(display_w * display_h));
    }
    return g.in_fbo;
}

void g3d_fsr_apply(unsigned int dst_fbo, int vp_x, int vp_y, int vp_w, int vp_h) {
    if (!g_enabled || !g.init || !g.in_tex)
        return;

    unsigned int c0[4], c1[4], c2[4], c3[4], cr[4];
    easu_con(c0, c1, c2, c3, (float)g.rw, (float)g.rh,
             (float)g.rw, (float)g.rh, (float)g.dw, (float)g.dh);
    rcas_con(cr, g_sharpness);

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(g.vao);

    /* EASU: render res -> display res */
    glBindFramebuffer(GL_FRAMEBUFFER, g.up_fbo);
    glViewport(0, 0, g.dw, g.dh);
    g3d_shader_use(g.sh_easu);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.in_tex);
    g3d_shader_set_int(g.sh_easu, "uInput", 0);
    glUniform4uiv(g3d_shader_get_uniform(g.sh_easu, "uCon0"), 1, c0);
    glUniform4uiv(g3d_shader_get_uniform(g.sh_easu, "uCon1"), 1, c1);
    glUniform4uiv(g3d_shader_get_uniform(g.sh_easu, "uCon2"), 1, c2);
    glUniform4uiv(g3d_shader_get_uniform(g.sh_easu, "uCon3"), 1, c3);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    /* RCAS: sharpen at display res, straight into the real target */
    glBindFramebuffer(GL_FRAMEBUFFER, dst_fbo);
    glViewport(vp_x, vp_y, vp_w, vp_h);
    g3d_shader_use(g.sh_rcas);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, g.up_tex);
    g3d_shader_set_int(g.sh_rcas, "uInput", 0);
    glUniform2f(g3d_shader_get_uniform(g.sh_rcas, "uOutOffset"), (float)vp_x, (float)vp_y);
    glUniform4uiv(g3d_shader_get_uniform(g.sh_rcas, "uConRcas"), 1, cr);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}

void g3d_fsr_shutdown(void) {
    if (!g.init) return;
    free_targets();
    if (g.vao) glDeleteVertexArrays(1, &g.vao);
    if (g.vbo) glDeleteBuffers(1, &g.vbo);
    if (g.sh_easu) g3d_shader_free(g.sh_easu);
    if (g.sh_rcas) g3d_shader_free(g.sh_rcas);
    memset(&g, 0, sizeof(g));
}

#else /* VITA */

unsigned int g3d_fsr_input_fbo(int a, int b, int c, int d) {
    (void)a; (void)b; (void)c; (void)d; return 0;
}
void g3d_fsr_apply(unsigned int f, int a, int b, int c, int d) {
    (void)f; (void)a; (void)b; (void)c; (void)d;
}
void g3d_fsr_shutdown(void) {}

#endif
