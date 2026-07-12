/*
 * libmod_3d_shader.h - Shader Compilation and Management
 *
 * Handles GLSL shader compilation, linking, and uniform management
 * Supports PC (OpenGL 3.3+) and VITA/Android (GLES2) variants
 */

#ifndef __LIBMOD_3D_SHADER_H
#define __LIBMOD_3D_SHADER_H

#include "libmod_3d_math.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
   SHADER TYPES
   ============================================================================
 */

typedef enum {
    G3D_SHADER_PHONG,       /* Phong lighting + shadow mapping */
    G3D_SHADER_SHADOW,      /* Depth-only for shadow passes */
    G3D_SHADER_SKYBOX,      /* Skybox rendering */
    G3D_SHADER_UNLIT,       /* Unlit (color only) */
} G3DShaderType;

typedef enum {
    G3D_UNIFORM_MATRIX4,
    G3D_UNIFORM_MATRIX3,
    G3D_UNIFORM_VEC3,
    G3D_UNIFORM_VEC4,
    G3D_UNIFORM_FLOAT,
    G3D_UNIFORM_INT,
    G3D_UNIFORM_SAMPLER2D,
} G3DUniformType;

/* ============================================================================
   SHADER STRUCTURES
   ============================================================================
 */

typedef struct {
    int id;                     /* GL shader object id */
    uint32_t type;              /* GL_VERTEX_SHADER, GL_FRAGMENT_SHADER */
    const char *source;         /* GLSL source code */
    int compiled;               /* 1 if successfully compiled */
    char error_log[1024];       /* Compilation error message */
} G3DShader;

typedef struct {
    int id;                     /* GL program object id */
    int linked;                 /* 1 if successfully linked */
    char error_log[1024];       /* Linking error message */

    /* Vertex and fragment shaders */
    G3DShader vertex_shader;
    G3DShader fragment_shader;

    /* Uniform cache (for fast lookups) */
    #define G3D_MAX_UNIFORMS 64
    int uniform_locations[G3D_MAX_UNIFORMS];
    char uniform_names[G3D_MAX_UNIFORMS][64];
    int uniform_count;
} G3DShaderProgram;

/* ============================================================================
   SHADER COMPILATION API
   ============================================================================
 */

/* Create shader program from vertex and fragment source */
G3DShaderProgram *g3d_shader_create(const char *vert_source,
                                     const char *frag_source);
G3DShaderProgram *g3d_shader_create_tess(const char *vert_source, const char *tcs_source,
                                         const char *tes_source, const char *frag_source);

/* Load built-in shader (by type) */
G3DShaderProgram *g3d_shader_load_builtin(G3DShaderType type);

/* Free shader program */
void g3d_shader_free(G3DShaderProgram *program);

/* Use shader program for rendering */
void g3d_shader_use(G3DShaderProgram *program);

/* Get uniform location and cache it */
int g3d_shader_get_uniform(G3DShaderProgram *program, const char *name);

/* ============================================================================
   UNIFORM SETTING API
   ============================================================================
 */

void g3d_shader_set_mat4_array(G3DShaderProgram *program, const char *name,
                               const Mat4 *mats, int count);
void g3d_shader_set_mat4(G3DShaderProgram *program, const char *name,
                         Mat4 mat);
void g3d_shader_set_mat3(G3DShaderProgram *program, const char *name,
                         Mat4 mat);  /* Extract 3x3 from 4x4 */

void g3d_shader_set_vec3(G3DShaderProgram *program, const char *name,
                         Vec3 vec);
void g3d_shader_set_vec4(G3DShaderProgram *program, const char *name,
                         Vec4 vec);

void g3d_shader_set_float(G3DShaderProgram *program, const char *name,
                          float value);
void g3d_shader_set_int(G3DShaderProgram *program, const char *name,
                        int value);

void g3d_shader_set_sampler2d(G3DShaderProgram *program, const char *name,
                              int texture_unit);

/* ============================================================================
   BUILT-IN SHADER SOURCES (Embedded in C)
   ============================================================================
 */

/* Phong Lighting Shader (PC - OpenGL 3.3+) */
extern const char *g3d_shader_phong_vert_gl33;
extern const char *g3d_shader_phong_frag_gl33;

/* Shadow Depth Shader (PC - OpenGL 3.3+) */
extern const char *g3d_shader_shadow_vert_gl33;
extern const char *g3d_shader_shadow_frag_gl33;

/* GLES2 Variants (for Android/VITA future) */
extern const char *g3d_shader_phong_vert_gles2;
extern const char *g3d_shader_phong_frag_gles2;

#ifdef __cplusplus
}
#endif

#endif /* __LIBMOD_3D_SHADER_H */
