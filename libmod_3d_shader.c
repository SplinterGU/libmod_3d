/*
 * libmod_3d_shader.c - Shader Compilation and Management Implementation
 *
 * Compiles GLSL shaders, links programs, and manages uniforms
 */

#include "libmod_3d_shader.h"
#include "libmod_3d_cloud_glsl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef VITA
#include "libmod_ray_vita_gl.h"
#else
#ifdef _WIN32
#include <GL/glew.h>
#elif defined(__ANDROID__)
#include <GLES2/gl2.h>
#else
/* GL_GLEXT_PROTOTYPES gives real prototypes for GL 2.0+ entry points
   (glUniform3f, etc). Without them the compiler assumes implicit int(...)
   declarations and promotes float args to double -> wrong uniform values. */
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#endif

/* ============================================================================
   SHADER COMPILATION
   ============================================================================
 */

static int g3d_shader_compile(G3DShader *shader) {
    /* Compile individual shader (vertex or fragment) */
    const char *source = shader->source;

    if (!source) {
        snprintf(shader->error_log, sizeof(shader->error_log),
                 "No shader source provided");
        return 0;
    }

#ifndef VITA
    shader->id = glCreateShader(shader->type);
    if (!shader->id) {
        snprintf(shader->error_log, sizeof(shader->error_log),
                 "Failed to create shader object");
        return 0;
    }

    glShaderSource(shader->id, 1, &source, NULL);
    glCompileShader(shader->id);

    /* Check compilation status */
    int compiled;
    glGetShaderiv(shader->id, GL_COMPILE_STATUS, &compiled);

    if (!compiled) {
        int log_length = 0;
        glGetShaderiv(shader->id, GL_INFO_LOG_LENGTH, &log_length);

        if (log_length > 0) {
            char *error = (char *)malloc(log_length);
            glGetShaderInfoLog(shader->id, log_length, NULL, error);
            snprintf(shader->error_log, sizeof(shader->error_log),
                     "Shader compilation failed:\n%s", error);
            free(error);
        }

        glDeleteShader(shader->id);
        shader->id = 0;
        return 0;
    }

    shader->compiled = 1;
    return 1;
#else
    /* VITA: Stub for now (VitaGL shader handling) */
    shader->compiled = 1;
    shader->id = 1;
    return 1;
#endif
}

G3DShaderProgram *g3d_shader_create(const char *vert_source,
                                     const char *frag_source) {
    G3DShaderProgram *program =
        (G3DShaderProgram *)malloc(sizeof(G3DShaderProgram));
    if (!program)
        return NULL;

    memset(program, 0, sizeof(G3DShaderProgram));

    /* Compile vertex shader */
    program->vertex_shader.type = GL_VERTEX_SHADER;
    program->vertex_shader.source = vert_source;
    if (!g3d_shader_compile(&program->vertex_shader)) {
        fprintf(stderr, "G3D: Vertex shader compilation failed\n");
        fprintf(stderr, "%s\n", program->vertex_shader.error_log);
        free(program);
        return NULL;
    }

    /* Compile fragment shader */
    program->fragment_shader.type = GL_FRAGMENT_SHADER;
    program->fragment_shader.source = frag_source;
    if (!g3d_shader_compile(&program->fragment_shader)) {
        fprintf(stderr, "G3D: Fragment shader compilation failed\n");
        fprintf(stderr, "%s\n", program->fragment_shader.error_log);
        glDeleteShader(program->vertex_shader.id);
        free(program);
        return NULL;
    }

#ifndef VITA
    /* Link program */
    program->id = glCreateProgram();
    if (!program->id) {
        fprintf(stderr, "G3D: Failed to create program\n");
        glDeleteShader(program->vertex_shader.id);
        glDeleteShader(program->fragment_shader.id);
        free(program);
        return NULL;
    }

    glAttachShader(program->id, program->vertex_shader.id);
    glAttachShader(program->id, program->fragment_shader.id);
    glLinkProgram(program->id);

    /* Check link status */
    int linked;
    glGetProgramiv(program->id, GL_LINK_STATUS, &linked);

    if (!linked) {
        int log_length = 0;
        glGetProgramiv(program->id, GL_INFO_LOG_LENGTH, &log_length);

        if (log_length > 0) {
            char *error = (char *)malloc(log_length);
            glGetProgramInfoLog(program->id, log_length, NULL, error);
            snprintf(program->error_log, sizeof(program->error_log),
                     "Program linking failed:\n%s", error);
            free(error);
        }

        fprintf(stderr, "G3D: Program linking failed\n");
        fprintf(stderr, "%s\n", program->error_log);

        glDeleteProgram(program->id);
        glDeleteShader(program->vertex_shader.id);
        glDeleteShader(program->fragment_shader.id);
        free(program);
        return NULL;
    }

    program->linked = 1;
    printf("G3D: Shader program linked successfully (id=%d)\n", program->id);
#else
    program->linked = 1;
    program->id = 1;
#endif

    return program;
}

#ifndef VITA
/* Create a program with tessellation stages: vertex + TCS + TES + fragment. Needs a
   GL 4.0 context (GLEW provides the entry points). Returns NULL on failure. */
G3DShaderProgram *g3d_shader_create_tess(const char *vert_source, const char *tcs_source,
                                         const char *tes_source, const char *frag_source) {
    G3DShaderProgram *program = (G3DShaderProgram *)malloc(sizeof(G3DShaderProgram));
    if (!program) return NULL;
    memset(program, 0, sizeof(G3DShaderProgram));

    program->vertex_shader.type = GL_VERTEX_SHADER;
    program->vertex_shader.source = vert_source;
    program->fragment_shader.type = GL_FRAGMENT_SHADER;
    program->fragment_shader.source = frag_source;
    G3DShader tcs; memset(&tcs, 0, sizeof(tcs)); tcs.type = GL_TESS_CONTROL_SHADER; tcs.source = tcs_source;
    G3DShader tes; memset(&tes, 0, sizeof(tes)); tes.type = GL_TESS_EVALUATION_SHADER; tes.source = tes_source;

    if (!g3d_shader_compile(&program->vertex_shader) || !g3d_shader_compile(&tcs) ||
        !g3d_shader_compile(&tes) || !g3d_shader_compile(&program->fragment_shader)) {
        fprintf(stderr, "G3D: tess shader compile failed\n");
        fprintf(stderr, "vert: %s\ntcs: %s\ntes: %s\nfrag: %s\n",
                program->vertex_shader.error_log, tcs.error_log, tes.error_log,
                program->fragment_shader.error_log);
        free(program); return NULL;
    }

    program->id = glCreateProgram();
    glAttachShader(program->id, program->vertex_shader.id);
    glAttachShader(program->id, tcs.id);
    glAttachShader(program->id, tes.id);
    glAttachShader(program->id, program->fragment_shader.id);
    glLinkProgram(program->id);
    int linked; glGetProgramiv(program->id, GL_LINK_STATUS, &linked);
    glDeleteShader(tcs.id); glDeleteShader(tes.id);
    if (!linked) {
        char log[1024]; glGetProgramInfoLog(program->id, sizeof(log), NULL, log);
        fprintf(stderr, "G3D: tess program link failed:\n%s\n", log);
        glDeleteProgram(program->id); free(program); return NULL;
    }
    program->linked = 1;
    printf("G3D: Tess shader program linked (id=%d)\n", program->id);
    return program;
}
#endif

void g3d_shader_free(G3DShaderProgram *program) {
    if (!program)
        return;

#ifndef VITA
    if (program->vertex_shader.id)
        glDeleteShader(program->vertex_shader.id);
    if (program->fragment_shader.id)
        glDeleteShader(program->fragment_shader.id);
    if (program->id)
        glDeleteProgram(program->id);
#endif

    free(program);
}

void g3d_shader_use(G3DShaderProgram *program) {
    if (!program || !program->linked)
        return;

#ifndef VITA
    glUseProgram(program->id);
#endif
}

/* ============================================================================
   UNIFORM MANAGEMENT
   ============================================================================
 */

static unsigned int uniform_hash(const char *s) {
    unsigned int h = 2166136261u;          /* FNV-1a */
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

int g3d_shader_get_uniform(G3DShaderProgram *program, const char *name) {
    if (!program || !program->linked)
        return -1;

    /* Compare hashes, not strings: this runs ~100 times per draw call and a
       strcmp scan over the whole cache was a measurable slice of frame time. */
    unsigned int h = uniform_hash(name);
    for (int i = 0; i < program->uniform_count; i++) {
        if (program->uniform_hashes[i] == h &&
            strcmp(program->uniform_names[i], name) == 0) {
            return program->uniform_locations[i];
        }
    }

    /* Not in cache, ask GL */
#ifndef VITA
    int location = glGetUniformLocation(program->id, name);
#else
    int location = -1;
#endif

    /* Cache the answer even when it's -1: a uniform the shader doesn't have
       (or that got optimised out) is asked for on every draw too, and each miss
       is a driver-side string lookup. */
    if (program->uniform_count < G3D_MAX_UNIFORMS) {
        program->uniform_locations[program->uniform_count] = location;
        program->uniform_hashes[program->uniform_count] = h;
        strncpy(program->uniform_names[program->uniform_count], name, 63);
        program->uniform_names[program->uniform_count][63] = '\0';
        program->uniform_count++;
    }

    return location;
}

void g3d_shader_set_mat4(G3DShaderProgram *program, const char *name,
                         Mat4 mat) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniformMatrix4fv(loc, 1, GL_FALSE, mat.m);
#endif
}

/* Upload an array of Mat4 (contiguous, column-major) to a uniform array, e.g.
   skeleton bone matrices for GPU skinning. */
void g3d_shader_set_mat4_array(G3DShaderProgram *program, const char *name,
                               const Mat4 *mats, int count) {
    if (!program || !mats || count <= 0)
        return;
    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;
#ifndef VITA
    glUniformMatrix4fv(loc, count, GL_FALSE, (const float *)mats);
#endif
}

void g3d_shader_set_mat3(G3DShaderProgram *program, const char *name,
                         Mat4 mat) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

    /* Extract 3x3 (for normal matrix) */
    float mat3[9] = {mat.m[0], mat.m[1], mat.m[2],   mat.m[4], mat.m[5],
                     mat.m[6], mat.m[8], mat.m[9],   mat.m[10]};

#ifndef VITA
    glUniformMatrix3fv(loc, 1, GL_FALSE, mat3);
#endif
}

void g3d_shader_set_vec3(G3DShaderProgram *program, const char *name,
                         Vec3 vec) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniform3f(loc, vec.x, vec.y, vec.z);
#endif
}

void g3d_shader_set_vec4(G3DShaderProgram *program, const char *name,
                         Vec4 vec) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniform4f(loc, vec.x, vec.y, vec.z, vec.w);
#endif
}

void g3d_shader_set_float(G3DShaderProgram *program, const char *name,
                          float value) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniform1f(loc, value);
#endif
}

void g3d_shader_set_int(G3DShaderProgram *program, const char *name,
                        int value) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniform1i(loc, value);
#endif
}

void g3d_shader_set_sampler2d(G3DShaderProgram *program, const char *name,
                              int texture_unit) {
    if (!program)
        return;

    int loc = g3d_shader_get_uniform(program, name);
    if (loc < 0)
        return;

#ifndef VITA
    glUniform1i(loc, texture_unit);
#endif
}

/* ============================================================================
   BUILT-IN SHADER SOURCES (Embedded)
   ============================================================================
 */

/* Phong Lighting Vertex Shader (OpenGL 3.3+) */
const char *g3d_shader_phong_vert_gl33 = R"glsl(
#version 330 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texcoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out VS_OUT {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
} vs_out;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(position, 1.0);

    vs_out.position = vec3(uModel * vec4(position, 1.0));
    vs_out.normal = normalize(uNormalMatrix * normal);
    vs_out.texcoord = texcoord;
}
)glsl";

/* Phong Lighting Fragment Shader (OpenGL 3.3+) */
const char *g3d_shader_phong_frag_gl33 = R"glsl(
#version 330 core

in VS_OUT {
    vec3 position;
    vec3 normal;
    vec2 texcoord;
} fs_in;

uniform sampler2D uAlbedoTexture;
uniform sampler2D uNormalTexture;
uniform sampler2D uNormalMap; uniform int uHasNormalMap;
uniform sampler2D uMetalMap;  uniform int uHasMetalMap;
uniform sampler2D uRoughMap;  uniform int uHasRoughMap;
uniform sampler2D uShadowMap;

uniform vec3 uAlbedoColor;
uniform float uMetallic;
uniform float uRoughness;

uniform vec3 uCameraPosition;
uniform vec3 uAmbientLight;
uniform float uAmbientIntensity;
uniform int uFlipWinding;   // 1 when the GRAPH render flips Y (inverts winding)

// Image based lighting captured from the sky (see libmod_3d_ibl.c). uHasIBL = 0
// falls back to flat ambient + the analytic skyEnv gradient.
uniform int uHasIBL;
uniform float uIBLIntensity;
uniform float uPrefilterMips;
uniform samplerCube uIrradiance;
uniform samplerCube uPrefilter;
uniform sampler2D uBRDFLUT;
uniform float uOpacity;     // entity opacity 0..1 (1 = opaque); < 1 -> transparent pass
uniform vec3  uEntityColor; // per-entity RGB tint (1,1,1 = none), multiplies albedo

// Directional Light
uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uLightIntensity;
uniform mat4 uLightSpaceMatrix;
uniform int uCastShadow;

// Fog
uniform int uFogEnabled;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

// Extra point / spot lights (no shadows; directional above carries the shadow)
#define MAX_PL 8
uniform int   uNumPL;
uniform int   uPLType[MAX_PL];    // 1 = point, 2 = spot
uniform vec3  uPLPos[MAX_PL];
uniform vec3  uPLDir[MAX_PL];     // spot forward direction
uniform vec3  uPLCol[MAX_PL];
uniform float uPLInt[MAX_PL];
uniform float uPLRange[MAX_PL];
uniform float uPLCosIn[MAX_PL];   // cos(inner cone half-angle)
uniform float uPLCosOut[MAX_PL];  // cos(outer cone half-angle)

// Dynamic shadow maps for up to 4 spot/point lights (torches). Each shadows the point
// light in uPL slot uCasterSlot[k].
#define MAX_SC 4
uniform sampler2D uSpotShadowMap[MAX_SC];
uniform mat4 uSpotLightSpaceMatrix[MAX_SC];
uniform int uCasterSlot[MAX_SC];
uniform int uNumShadowCasters;
uniform int uSpotShadowEnabled;

// Clip plane (reflection pass discards geometry behind the mirror plane):
// uClipPlane = (nx,ny,nz,d); discard where dot(pos,n)+d < 0.
uniform int uClipEnable;
uniform vec4 uClipPlane;

// Voxel terrain: paint grass on flats and rock/wall texture on slopes / cave walls
// (so the painted top texture doesn't stretch down vertical surfaces).
uniform int uTriplanar;
uniform sampler2D uWallTexture;
uniform int uHasWallTex;

// Procedural terrain biomes: colour by world height + slope (sand -> grass ->
// rock -> snow, dark basalt + glowing lava on volcano peaks). No textures, no
// per-vertex data: the streamed terrain sets uBiome=1 and this paints itself.
uniform int   uBiome;
uniform float uBiomeAmp;   // world height scale (band thresholds are fractions of it)
uniform float uBiomeSea;   // sea level (world Y) subtracted from the height
// optional real textures per band (each band independently textured or flat)
uniform int       uHasBiomeTex;
uniform float     uBiomeTexScale;
uniform vec4      uBiomeTexMask;    // per band: 1 = use texture, 0 = flat colour
uniform sampler2D uBiomeSand;
uniform sampler2D uBiomeGrass;
uniform sampler2D uBiomeRock;
uniform sampler2D uBiomeSnow;

out vec4 FragColor;

// cheap value noise for breaking up the flat colour bands
float biomeHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float biomeNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = biomeHash(i), b = biomeHash(i + vec2(1,0));
    float c = biomeHash(i + vec2(0,1)), d = biomeHash(i + vec2(1,1));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float calculateShadow() {
    if (uCastShadow == 0) return 0.0;

    vec4 fragPosLightSpace = uLightSpaceMatrix * vec4(fs_in.position, 1.0);
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float closestDepth = texture(uShadowMap, projCoords.xy).r;
    float currentDepth = projCoords.z;

    // PCF: Percentage Closer Filtering
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(uShadowMap, 0);

    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(uShadowMap,
                projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - 0.005 > pcfDepth ? 1.0 : 0.0;
        }
    }

    shadow /= 9.0;
    return shadow;
}

float calculateFog() {
    if (uFogEnabled == 0) return 1.0;

    float distance = length(fs_in.position - uCameraPosition);
    float fogFactor = (uFogEnd - distance) / (uFogEnd - uFogStart);
    fogFactor = clamp(fogFactor, 0.0, 1.0);

    return fogFactor;
}

float calcCasterShadow(sampler2D map, mat4 lsm) {
    vec4 ls = lsm * vec4(fs_in.position, 1.0);
    vec3 proj = ls.xyz / ls.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    if (proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 0.0;
    float current = proj.z;
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(map, 0);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float pcf = texture(map, proj.xy + vec2(x, y) * texelSize).r;
            shadow += current - 0.0015 > pcf ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

// ---- PBR (Cook-Torrance, metallic/roughness) + procedural-sky IBL ----
const float PBR_PI = 3.14159265;
float distGGX(float NdotH, float rough) {
    float a = rough * rough; float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PBR_PI * d * d);
}
float geomSchlick(float NdotX, float rough) {
    float r = rough + 1.0; float k = (r * r) / 8.0;
    return NdotX / (NdotX * (1.0 - k) + k);
}
vec3 fresnelSchlick(float cosT, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}
vec3 fresnelRough(float cosT, vec3 F0, float rough) {
    return F0 + (max(vec3(1.0 - rough), F0) - F0) * pow(clamp(1.0 - cosT, 0.0, 1.0), 5.0);
}
vec3 skyEnv(vec3 dir) {   // cheap environment for reflections/ambient
    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);
    return mix(vec3(0.55, 0.60, 0.68), vec3(0.30, 0.50, 0.85), t);
}
// Direct-light BRDF. Diffuse is NOT divided by PI (PI folded into the light) so the
// base brightness stays close to the old Phong look.
vec3 pbrDirect(vec3 N, vec3 V, vec3 L, vec3 albedo, float rough, float metal, vec3 F0, vec3 radiance) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 0.0);
    float NdotH = max(dot(N, H), 0.0);
    float HdotV = max(dot(H, V), 0.0);
    float D = distGGX(NdotH, rough);
    float G = geomSchlick(NdotV, rough) * geomSchlick(NdotL, rough);
    vec3  F = fresnelSchlick(HdotV, F0);
    vec3 spec = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metal);
    return (kd * albedo + spec) * radiance * NdotL;
}
)glsl"
/* shared world-space cloud field -> lets the sun be shadowed by the low clouds */
"uniform float uTime;\n"
"uniform vec3 uSunDir;\n"
G3D_CLOUD_GLSL
R"glsl(
void main() {
    if (uClipEnable == 1 &&
        dot(fs_in.position, uClipPlane.xyz) + uClipPlane.w < 0.0) discard;

    // Alpha cutout (foliage / cards): drop near-transparent texels. Opaque
    // textures (alpha=1, and the 1x1 white default) are unaffected.
    vec4 texel = texture(uAlbedoTexture, fs_in.texcoord);
    if (texel.a < 0.5) discard;

    // Albedo = texture * tint color (a white default texture is bound when the
    // material has no albedo map, so this yields the tint color alone)
    vec3 albedo = texel.rgb * uAlbedoColor;

    // Voxel terrain: keep the painted grass on flats, blend to procedural rock on
    // slopes / cave walls (by the geometric up-facing normal) so nothing stretches.
    if (uTriplanar == 1) {
        vec3 gN = normalize(fs_in.normal);
        float slope = 1.0 - clamp(gN.y, 0.0, 1.0);      // 0 flat -> 1 vertical
        vec3 wp = fs_in.position;
        vec3 rock;
        if (uHasWallTex == 1) {
            // triplanar: sample the wall texture on the 3 axis planes, weighted by
            // the normal, so it never stretches on vertical cave walls
            vec3 an = abs(gN); an /= (an.x + an.y + an.z);
            float s = 0.12;
            vec3 tX = texture(uWallTexture, wp.zy * s).rgb;
            vec3 tY = texture(uWallTexture, wp.xz * s).rgb;
            vec3 tZ = texture(uWallTexture, wp.xy * s).rgb;
            rock = tX * an.x + tY * an.y + tZ * an.z;
        } else {
            float n = 0.5 + 0.25 * sin(wp.x * 0.7) * sin(wp.z * 0.6)
                          + 0.25 * sin(wp.y * 0.9 + wp.x * 0.3);
            n = clamp(n, 0.0, 1.0);
            rock = mix(vec3(0.30, 0.27, 0.24), vec3(0.52, 0.48, 0.43), n);
        }
        albedo = mix(albedo, rock, smoothstep(0.35, 0.72, slope));
    }

    // ---- Procedural terrain biomes (height + slope) ----
    float biomeLava = 0.0;
    if (uBiome == 1) {
        vec3 wp = fs_in.position;
        float amp = max(uBiomeAmp, 1.0);
        float r   = (wp.y - uBiomeSea) / amp;          // height in amplitude units
        vec3  gN  = normalize(fs_in.normal);
        float slope = 1.0 - clamp(gN.y, 0.0, 1.0);     // 0 flat -> 1 vertical

        // per-fragment noise so bands aren't perfectly flat
        float nz = biomeNoise(wp.xz * 0.02) * 0.5 + biomeNoise(wp.xz * 0.08) * 0.5;
        r += (nz - 0.5) * 0.12;

        // Per-band base colour: flat procedural, overridden by a texture where one
        // is supplied (each band independent, so you can texture only some).
        vec3 sand  = vec3(0.80, 0.74, 0.53);
        vec3 grass = mix(vec3(0.28, 0.46, 0.20), vec3(0.20, 0.37, 0.15), nz);
        vec3 rock  = mix(vec3(0.46, 0.42, 0.37), vec3(0.36, 0.32, 0.29), nz);
        vec3 snow  = vec3(0.94, 0.95, 0.99);
        if (uHasBiomeTex == 1) {
            vec2 tuv = wp.xz * uBiomeTexScale;
            if (uBiomeTexMask.x > 0.5) sand  = texture(uBiomeSand,  tuv).rgb;
            if (uBiomeTexMask.y > 0.5) grass = texture(uBiomeGrass, tuv).rgb;
            if (uBiomeTexMask.z > 0.5) rock  = texture(uBiomeRock,  tuv).rgb;
            if (uBiomeTexMask.w > 0.5) snow  = texture(uBiomeSnow,  tuv).rgb;
        }

        vec3 col = sand;
        col = mix(col, grass, smoothstep(0.06, 0.16, r));
        col = mix(col, rock,  smoothstep(0.55, 0.95, r));
        col = mix(col, snow,  smoothstep(1.15, 1.45, r));
        // steep faces are always rock (no grass/snow clinging to cliffs)
        col = mix(col, rock, smoothstep(0.50, 0.75, slope));

        // volcanoes: only the ridged peaks climb this high -> dark basalt, then
        // glowing lava right at the summit.
        col = mix(col, vec3(0.12, 0.10, 0.10), smoothstep(1.60, 1.95, r));
        biomeLava = smoothstep(2.30, 2.85, r);
        col = mix(col, vec3(1.00, 0.35, 0.06), biomeLava);

        albedo = col;
    }

    albedo *= uEntityColor;   // per-entity RGB tint (BennuGD-style colour)

    vec3 normal = normalize(fs_in.normal);
    vec3 viewDir = normalize(uCameraPosition - fs_in.position);
    // Two-sided shading: flip the normal on genuine BACK faces, decided by the
    // triangle winding (gl_FrontFacing), NOT the view angle. The old view-angle
    // test flipped grazing FRONT faces too, so a large flat ground seen from near
    // ground level flipped to face-down right at eye level -> a hard dark seam along
    // the horizon. flip_y (GRAPH render) inverts winding, so correct for it.
    bool frontFace = (uFlipWinding == 1) ? (!gl_FrontFacing) : gl_FrontFacing;
    if (!frontFace) normal = -normal;

    // Tangent-space normal map WITHOUT precomputed tangents: build the TBN from the
    // screen-space derivatives of position and UV (Schuler's cotangent frame).
    if (uHasNormalMap == 1) {
        vec3 nT = texture(uNormalMap, fs_in.texcoord).xyz * 2.0 - 1.0;
        vec3 dp1 = dFdx(fs_in.position), dp2 = dFdy(fs_in.position);
        vec2 du1 = dFdx(fs_in.texcoord), du2 = dFdy(fs_in.texcoord);
        vec3 dp2p = cross(dp2, normal), dp1p = cross(normal, dp1);
        vec3 T = dp2p * du1.x + dp1p * du2.x;
        vec3 B = dp2p * du1.y + dp1p * du2.y;
        float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
        normal = normalize(mat3(T * invmax, B * invmax, normal) * nT);
    }

    // ---- PBR lighting ----
    vec3 N = normal;
    vec3 V = viewDir;
    float rough = clamp(uRoughness, 0.045, 1.0);
    float metal = clamp(uMetallic, 0.0, 1.0);
    if (uHasRoughMap == 1) rough = clamp(texture(uRoughMap, fs_in.texcoord).r, 0.045, 1.0);
    if (uHasMetalMap == 1) metal = clamp(texture(uMetalMap, fs_in.texcoord).r, 0.0, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metal);   // dielectrics ~4%, metals tint by albedo

    float shadow = calculateShadow();
    float shadowFactor = 1.0 - (shadow * 0.7);

    // Direct: directional (sun)
    vec3 Lo = vec3(0.0);
    {
        vec3 L = normalize(-uLightDirection);
        vec3 radiance = uLightColor * uLightIntensity * shadowFactor * cloudShadow(fs_in.position);
        Lo += pbrDirect(N, V, L, albedo, rough, metal, F0, radiance);
    }
    // Dynamic shadows: up to 4 caster lights (torches) each with their own shadow map.
    //float casterMul[MAX_SC];
    //for (int k = 0; k < MAX_SC; k++) {
    //    casterMul[k] = 1.0;
    //    if (uSpotShadowEnabled == 1 && k < uNumShadowCasters)
    //        casterMul[k] = 1.0 - calcCasterShadow(uSpotShadowMap[k], uSpotLightSpaceMatrix[k]);
    //}

    // Dynamic shadows: up to 4 caster lights (torches) each with their own shadow map.
    float casterMul[MAX_SC];
    for (int k = 0; k < MAX_SC; k++) {
        casterMul[k] = 1.0;
    }

    if (uSpotShadowEnabled == 1) {
        if (0 < uNumShadowCasters) casterMul[0] = 1.0 - calcCasterShadow(uSpotShadowMap[0], uSpotLightSpaceMatrix[0]);
        if (1 < uNumShadowCasters) casterMul[1] = 1.0 - calcCasterShadow(uSpotShadowMap[1], uSpotLightSpaceMatrix[1]);
        if (2 < uNumShadowCasters) casterMul[2] = 1.0 - calcCasterShadow(uSpotShadowMap[2], uSpotLightSpaceMatrix[2]);
        if (3 < uNumShadowCasters) casterMul[3] = 1.0 - calcCasterShadow(uSpotShadowMap[3], uSpotLightSpaceMatrix[3]);
    }

    // Direct: point / spot lights
    for (int i = 0; i < uNumPL; i++) {
        vec3 toL = uPLPos[i] - fs_in.position;
        float dist = length(toL);
        vec3 L = toL / max(dist, 0.0001);
        float att = clamp(1.0 - dist / max(uPLRange[i], 0.0001), 0.0, 1.0);
        att = att * att;
        float spotF = 1.0;
        if (uPLType[i] == 2) {
            float theta = dot(normalize(uPLDir[i]), -L);
            spotF = clamp((theta - uPLCosOut[i]) /
                          max(uPLCosIn[i] - uPLCosOut[i], 0.0001), 0.0, 1.0);
        }
        // apply the shadow map whose caster slot matches this light
        for (int k = 0; k < MAX_SC; k++)
            if (k < uNumShadowCasters && uCasterSlot[k] == i) spotF *= casterMul[k];
        vec3 radiance = uPLCol[i] * uPLInt[i] * att * spotF;
        Lo += pbrDirect(N, V, L, albedo, rough, metal, F0, radiance);
    }

    // Ambient / IBL. With uHasIBL the environment is the REAL sky, captured into
    // cubemaps: cosine-convolved irradiance for diffuse and a roughness-indexed
    // prefiltered chain + BRDF LUT for specular (split-sum). Without it, fall
    // back to the old flat ambient + analytic gradient.
    float NdotV = max(dot(N, V), 0.0);
    vec3 Fenv = fresnelRough(NdotV, F0, rough);
    vec3 kdA = (vec3(1.0) - Fenv) * (1.0 - metal);
    vec3 R = reflect(-V, N);
    vec3 iblDiff, iblSpec;
    if (uHasIBL == 1) {
        vec3 irradiance = texture(uIrradiance, N).rgb * uIBLIntensity;
        iblDiff = irradiance * albedo * kdA;
        vec3 pref = textureLod(uPrefilter, R, rough * (uPrefilterMips - 1.0)).rgb * uIBLIntensity;
        vec2 ab = texture(uBRDFLUT, vec2(NdotV, rough)).rg;
        iblSpec = pref * (Fenv * ab.x + ab.y);
    } else {
        iblDiff = uAmbientLight * uAmbientIntensity * albedo * kdA;
        iblSpec = skyEnv(R) * Fenv * (1.0 - rough * 0.6);
    }
    vec3 result = iblDiff + iblSpec + Lo;

    // Lava glows (emissive) so volcano summits stay bright even in shadow.
    result += vec3(1.0, 0.32, 0.05) * biomeLava * 2.2;

    // Apply fog
    float fogFactor = calculateFog();
    result = mix(uFogColor, result, fogFactor);

    FragColor = vec4(result, uOpacity);
}
)glsl";

/* Shadow Depth Vertex Shader (OpenGL 3.3+) */
const char *g3d_shader_shadow_vert_gl33 = R"glsl(
#version 330 core

layout(location = 0) in vec3 position;

uniform mat4 uLightSpaceMatrix;
uniform mat4 uModel;

void main() {
    gl_Position = uLightSpaceMatrix * uModel * vec4(position, 1.0);
}
)glsl";

/* Shadow Depth Fragment Shader (OpenGL 3.3+) */
const char *g3d_shader_shadow_frag_gl33 = R"glsl(
#version 330 core

void main() {
    // Depth is automatically written to gl_FragDepth
    // Just output dummy color (not used in shadow pass)
    gl_FragColor = vec4(1.0);
}
)glsl";

/* GLES2 Variants (for Android/VITA) */
const char *g3d_shader_phong_vert_gles2 = R"glsl(
#version 100

attribute vec3 position;
attribute vec3 normal;
attribute vec2 texcoord;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vTexcoord;

void main() {
    gl_Position = uProjection * uView * uModel * vec4(position, 1.0);

    vPosition = vec3(uModel * vec4(position, 1.0));
    vNormal = normalize(uNormalMatrix * normal);
    vTexcoord = texcoord;
}
)glsl";

const char *g3d_shader_phong_frag_gles2 = R"glsl(
#version 100
precision mediump float;

varying vec3 vPosition;
varying vec3 vNormal;
varying vec2 vTexcoord;

uniform sampler2D uAlbedoTexture;
uniform vec3 uAlbedoColor;
uniform vec3 uCameraPosition;
uniform vec3 uAmbientLight;
uniform float uAmbientIntensity;
uniform int uFlipWinding;   // 1 when the GRAPH render flips Y (inverts winding)
uniform float uOpacity;     // entity opacity 0..1 (1 = opaque); < 1 -> transparent pass
uniform vec3  uEntityColor; // per-entity RGB tint (1,1,1 = none), multiplies albedo

uniform vec3 uLightDirection;
uniform vec3 uLightColor;
uniform float uLightIntensity;

void main() {
    vec3 albedo = texture2D(uAlbedoTexture, vTexcoord).rgb * uAlbedoColor;
    vec3 normal = normalize(vNormal);

    vec3 lightDir = normalize(-uLightDirection);
    float diff = max(dot(normal, lightDir), 0.0);

    vec3 ambient = uAmbientLight * uAmbientIntensity;
    vec3 diffuse = uLightColor * diff * uLightIntensity;

    vec3 result = albedo * (ambient + diffuse);

    gl_FragColor = vec4(result, uOpacity);
}
)glsl";

G3DShaderProgram *g3d_shader_load_builtin(G3DShaderType type) {
    switch (type) {
    case G3D_SHADER_PHONG:
        return g3d_shader_create(g3d_shader_phong_vert_gl33,
                                 g3d_shader_phong_frag_gl33);
    case G3D_SHADER_SHADOW:
        return g3d_shader_create(g3d_shader_shadow_vert_gl33,
                                 g3d_shader_shadow_frag_gl33);
    default:
        return NULL;
    }
}
