/*
 * libmod_3d_water.c - Animated water surface implementation
 */

#include "libmod_3d_water.h"
#include "libmod_3d_shader.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_light.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_particles.h"
#include <stdio.h>
#include <math.h>

#include <SDL.h>

#ifndef VITA
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glext.h>
#endif

/* ---- shaders ----------------------------------------------------------- */

static const char *water_vert =
    "#version 330 core\n"
    "layout(location = 0) in vec3 position;\n"
    "layout(location = 1) in vec3 anormal;\n"    /* rivers pack flow dir in .xz (0 = still) */
    "layout(location = 2) in vec2 texcoord;\n"   /* lakes/rivers pack shore depth in .x */
    "uniform mat4 uModel;\n"
    "uniform mat4 uView;\n"
    "uniform mat4 uProjection;\n"
    "uniform float uTime;\n"
    "uniform float uWaveAmp;\n"
    "uniform float uWaveLen;\n"
    "uniform float uWaveSpeed;\n"
    "uniform float uLava;\n"     /* 1 = lava: viscous, bubbling instead of waves */
    "uniform float uSwellAmp;\n"    /* ocean/beach directional swell (0 = off) */
    "uniform float uSwellDirX;\n"
    "uniform float uSwellDirZ;\n"
    "uniform float uSwellLen;\n"
    "uniform float uSwellSpeed;\n"
    "out vec3 vWorldPos;\n"
    "out vec3 vNormal;\n"
    "out vec4 vClip;\n"
    "out float vWaveNorm;\n"     /* normalized wave height (~ -1..1), crest near 1 */
    "out float vShoreDepth;\n"   /* water depth at this vertex (lakes) */
    "out vec2 vFlowDir;\n"       /* river flow direction in XZ (0 = still water) */
    "out float vAlong;\n"        /* rivers: distance along the course (surface UV) */
    "out float vLavaHot;\n"      /* lava: 0..1 bubble intensity at this vertex */
    // Cellular bubbles tiled across the whole surface: one bubble per ~4u cell,
    // each with its own random position and swell/pop lifecycle. Returns the
    // hottest swell at world point p (0..1).
    "float lavaBubbles(vec2 p, float t) {\n"
    "    vec2 cell = p / 4.0;\n"
    "    vec2 id = floor(cell);\n"
    "    vec2 f = fract(cell);\n"
    "    float hot = 0.0;\n"
    "    for (int j = -1; j <= 1; j++) {\n"
    "        for (int i = -1; i <= 1; i++) {\n"
    "            vec2 g = vec2(float(i), float(j));\n"
    "            vec2 nid = id + g;\n"
    "            float r1 = fract(sin(dot(nid, vec2(127.1, 311.7))) * 43758.5453);\n"
    "            float r2 = fract(sin(dot(nid, vec2(269.5, 183.3))) * 43758.5453);\n"
    "            vec2 c = g + vec2(r1, r2);\n"            // bubble centre in this cell
    "            float ph = fract(t * 0.25 + r1);\n"     // per-bubble lifecycle 0..1
    "            float swell = sin(ph * 3.14159);\n"     // 0 -> 1 -> 0 (rise then pop)
    "            float rad = 0.28 + 0.42 * swell;\n"
    "            float b = max(0.0, 1.0 - length(f - c) / rad);\n"
    "            hot = max(hot, b * b * swell);\n"
    "        }\n"
    "    }\n"
    "    return hot;\n"
    "}\n"
    "void main() {\n"
    "    vFlowDir = anormal.xz;\n"
    "    vec3 wp = vec3(uModel * vec4(position, 1.0));\n"
    "    vec2 base = wp.xz;\n"
    "    vec2 dirs[4] = vec2[](normalize(vec2(1.0, 0.35)),\n"
    "                          normalize(vec2(-0.45, 1.0)),\n"
    "                          normalize(vec2(0.7, -0.6)),\n"
    "                          normalize(vec2(-0.8, -0.3)));\n"
    "    float amp = uWaveAmp;\n"
    "    float len = uWaveLen;\n"
    "    float h = 0.0;\n"
    "    vec3 disp = vec3(0.0);\n"
    "    vec3 nrm = vec3(0.0, 1.0, 0.0);\n"
    "    for (int i = 0; i < 4; i++) {\n"
    "        float k = 6.2831853 / len;\n"
    "        float Q = 0.55 / (k * amp * 4.0 + 0.001);\n"   // steepness, bounded (no loops)
    "        float ph = dot(dirs[i], base) * k + uTime * uWaveSpeed * (1.0 + float(i) * 0.3);\n"
    "        float c = cos(ph), s = sin(ph);\n"
    "        disp.x += Q * amp * dirs[i].x * c;\n"          // Gerstner horizontal motion
    "        disp.z += Q * amp * dirs[i].y * c;\n"
    "        disp.y += amp * s;\n"
    "        h += amp * s;\n"
    "        nrm.x -= dirs[i].x * (k * amp * c);\n"          // analytic Gerstner normal
    "        nrm.z -= dirs[i].y * (k * amp * c);\n"
    "        nrm.y -= Q * (k * amp * s);\n"
    "        amp *= 0.55;\n"
    "        len *= 0.62;\n"
    "    }\n"
    "    wp += disp;\n"
    // Ocean/beach SWELL: a dominant directional wave that SHOALS (grows + steepens) as
    // the water gets shallow (small shore depth) -> waves pile up toward the beach.
    "    if (uSwellAmp > 0.0) {\n"
    "        float shoal = clamp(1.0 - texcoord.x / 6.0, 0.0, 1.0);\n"   // 1 shallow -> 0 deep
    "        vec2 swd = normalize(vec2(uSwellDirX, uSwellDirZ) + vec2(1e-4, 0.0));\n"
    "        float sk = 6.2831853 / max(uSwellLen, 0.5);\n"
    "        float sw = sin(dot(swd, base) * sk - uTime * uSwellSpeed);\n"
    "        float samp = uSwellAmp * (0.5 + 0.8 * shoal);\n"            // a bit taller near shore
    "        wp.y += samp * sw;\n"
    "        h += samp * sw;\n"                                          // feeds crest foam
    "    }\n"
    "    vLavaHot = 0.0;\n"
    "    if (uLava > 0.5) {\n"
    "        wp -= disp * 0.7;\n"               // viscous: kill most of the watery sway
    "        float hot = lavaBubbles(base, uTime);\n"
    "        wp.y += hot * 0.7;\n"              // bubbles swell the surface
    "        vLavaHot = hot;\n"
    "    }\n"
    "    vWaveNorm = h / (uWaveAmp * 1.8 + 0.001);\n"
    "    vShoreDepth = texcoord.x;\n"
    "    vAlong = texcoord.y;\n"
    "    vNormal = normalize(nrm);\n"
    "    vWorldPos = wp;\n"
    "    gl_Position = uProjection * uView * vec4(wp, 1.0);\n"
    "    vClip = gl_Position;\n"
    "}\n";

static const char *water_frag =
    "#version 330 core\n"
    "in vec3 vWorldPos;\n"
    "in vec3 vNormal;\n"
    "in vec4 vClip;\n"
    "in float vWaveNorm;\n"
    "in float vShoreDepth;\n"
    "in vec2 vFlowDir;\n"
    "in float vAlong;\n"
    "in float vLavaHot;\n"
    "uniform int uShoreFoam;\n"        /* 1 = use vShoreDepth for shoreline foam */
    "uniform int uSurf;\n"             /* 1 = breaking surf lines rolling onto the shore */
    "uniform float uSurfFreq;\n"       /* surf line spacing (in shore-depth units) */
    "uniform float uSurfSpeed;\n"      /* how fast the surf rolls in */
    "uniform float uBreakDepth;\n"     /* depth below which waves break */
    "uniform vec3 uCameraPos;\n"
    "uniform vec3 uLightDir;\n"
    "uniform vec3 uLightColor;\n"
    "uniform vec3 uWaterDeep;\n"
    "uniform vec3 uWaterShallow;\n"
    "uniform sampler2D uWaterTex;\n"
    "uniform int uHasTex;\n"
    "uniform float uTime;\n"
    "uniform sampler2D uReflTex;\n"
    "uniform int uHasRefl;\n"
    "uniform float uReflStrength;\n"
    "uniform int uReflFlipV;\n"
    "uniform float uDepth;\n"          /* 0 = use only fresnel (global water) */
    "uniform float uOpacity;\n"        /* overall opacity multiplier (0..1) */
    "uniform float uLava;\n"           /* 1 = render as emissive lava */
    "uniform sampler2D uSceneTex;\n"   /* opaque scene colour (for refraction + SSR) */
    "uniform int uHasScene;\n"
    "uniform float uRefract;\n"        /* refraction distortion strength */
    "uniform sampler2D uDepthTex;\n"   /* opaque scene depth copy (SSR + contact foam) */
    "uniform mat4 uViewProj;\n"        /* world -> clip (projects the reflected ray) */
    "uniform mat4 uInvProj;\n"         /* clip -> view (reconstruct depth for contact foam) */
    "uniform int uHasDepth;\n"
    "uniform int uSSR;\n"
    "uniform float uSSRStrength;\n"
    "uniform float uSSRStep;\n"
    "uniform vec4 uRipples[24];\n"     /* expanding ring ripples: xz centre, z age, w strength */
    "uniform int uRippleCount;\n"
    "uniform float uRippleStrength;\n" /* global ripple intensity (0 = off) */
    "out vec4 FragColor;\n"
    "vec3 skyColor(vec3 dir) {\n"      // cheap sky gradient for reflections
    "    float t = clamp(dir.y * 0.5 + 0.5, 0.0, 1.0);\n"
    "    return mix(vec3(0.80, 0.88, 0.96), vec3(0.25, 0.47, 0.85), t);\n"
    "}\n"
    "void main() {\n"
    "    vec3 N = normalize(vNormal);\n"
    "    vec2 p = vWorldPos.xz;\n"        // world coords: ripples, foam noise
    "    float t = uTime;\n"
    // Scroll the surface detail along the flow so ripples run downstream (the water
    // sim's flowing cells carry velocity in vFlowDir); still water has vFlowDir 0.
    "    vec2 fp = p - vFlowDir * (t * 2.5);\n"
    // high-frequency animated ripple detail: this is what stops it looking like
    // a smooth moving plane (fine shimmer over the big Gerstner waves)
    "    vec3 d = vec3(0.0);\n"
    "    d.x += sin(fp.x * 1.7 + t * 1.4) * 0.08 + sin(fp.x * 4.3 - t * 2.2 + fp.y * 2.0) * 0.04 + sin(fp.x * 9.0 + t * 3.1) * 0.02;\n"
    "    d.z += sin(fp.y * 1.9 - t * 1.2) * 0.08 + sin(fp.y * 4.1 + t * 1.9 + fp.x * 2.0) * 0.04 + sin(fp.y * 8.5 - t * 2.7) * 0.02;\n"
    "    vec3 texTint = vec3(1.0);\n"
    "    if (uHasTex == 1) {\n"
    "        vec2 uv1 = fp * 0.06 + vec2(t * 0.015, t * 0.010);\n"
    "        vec2 uv2 = fp * 0.11 - vec2(t * 0.012, t * 0.018);\n"
    "        vec3 c1 = texture(uWaterTex, uv1).rgb;\n"
    "        vec3 c2 = texture(uWaterTex, uv2).rgb;\n"
    "        float b1 = dot(c1, vec3(0.3333));\n"
    "        float b2 = dot(c2, vec3(0.3333));\n"
    "        d.x += (b1 - 0.5) * 0.5; d.z += (b2 - 0.5) * 0.5;\n"   // texture as ripple/dudv
    "        texTint = 0.6 + 0.8 * mix(c1, c2, 0.5);\n"             // and a colour tint
    "    }\n"
    // expanding ring ripples (splashes, river mouths, objects in the water)
    "    float rfoam = 0.0;\n"
    "    for (int i = 0; i < uRippleCount; i++) {\n"
    "        vec2 rc = uRipples[i].xy; float age = uRipples[i].z; float str = uRipples[i].w;\n"
    "        float dist = length(p - rc);\n"
    "        float rad = age * 6.0;\n"                              // ring expands outward
    "        float w = dist - rad;\n"
    "        float env = exp(-w * w * 0.25) * exp(-age * 1.3) * str * uRippleStrength;\n"
    "        float ring = sin(w * 3.0 - uTime * 6.0);\n"
    "        vec2 dir = dist > 0.001 ? (p - rc) / dist : vec2(0.0);\n"
    "        d.x += dir.x * ring * env * 0.9;\n"
    "        d.z += dir.y * ring * env * 0.9;\n"
    "        rfoam = max(rfoam, clamp(env, 0.0, 1.0) * 0.9);\n"
    "    }\n"
    "    N = normalize(N + d);\n"
    // ---- Lava: emissive molten crust (no refraction/reflection) ----
    "    if (uLava > 0.5) {\n"
    "        vec2 q = fp * 0.25;\n"
    // slow molten field (two octaves drifting)
    "        float m = 0.5 + 0.5 * sin(q.x * 1.3 + t * 0.6) * sin(q.y * 1.1 - t * 0.5);\n"
    "        m = 0.5 * (m + 0.5 + 0.5 * sin(q.x * 2.7 - t * 0.4 + q.y * 1.7));\n"
    // bright cracks (veins) between cooler crust plates
    "        float plates = sin(q.x * 3.0 + sin(q.y * 2.0 + t * 0.3))\n"
    "                     * sin(q.y * 3.0 - sin(q.x * 2.0 - t * 0.2));\n"
    "        float crack = smoothstep(0.82, 1.0, 1.0 - abs(plates));\n"
    "        float heat = clamp(m + crack, 0.0, 1.0);\n"
    "        vec3 crust  = uWaterDeep;\n"            // dark cooled rock
    "        vec3 molten = uWaterShallow;\n"         // glowing rock
    "        vec3 core   = vec3(1.0, 0.85, 0.35);\n" // hot yellow core in the cracks
    "        vec3 lc = mix(crust, molten, smoothstep(0.2, 0.85, heat));\n"
    "        lc = mix(lc, core, crack);\n"
    "        lc += molten * heat * 0.6 + core * crack * 0.8;\n"   // emissive glow
    // bubbles: the swelling tops glow hot, with a bright pop highlight
    "        lc += molten * vLavaHot * 1.4;\n"
    "        lc += vec3(1.0, 0.92, 0.6) * pow(vLavaHot, 3.0) * 1.2;\n"
    "        vec3 Vl = normalize(uCameraPos - vWorldPos);\n"
    "        float rim = pow(1.0 - max(dot(N, Vl), 0.0), 3.0);\n"
    "        lc += vec3(0.5, 0.18, 0.05) * rim * 0.4;\n"          // hot rim sheen
    "        FragColor = vec4(lc, 1.0);\n"
    "        return;\n"
    "    }\n"
    "    vec3 V = normalize(uCameraPos - vWorldPos);\n"
    // Schlick Fresnel: water is barely reflective head-on, mirror-like at grazing
    "    float fres = 0.02 + 0.98 * pow(1.0 - max(dot(N, V), 0.0), 5.0);\n"
    "    vec3 L = normalize(-uLightDir);\n"
    // Colour by REAL water depth (shore depth): turquoise in the shallows -> deep blue
    // offshore. (Falls back to the fixed uDepth for the classic plane with no shore data.)
    "    float depthF = clamp((uShoreFoam == 1 ? vShoreDepth : uDepth) / 14.0, 0.0, 1.0);\n"
    "    float deepMix = clamp(depthF * 1.15, 0.0, 1.0);\n"
    "    vec3 base = mix(uWaterShallow, uWaterDeep, deepMix);\n"
    "    if (uHasTex == 1) base = mix(base, base * texTint, 0.4);\n"
    // screen-space position of this fragment (for refraction / planar reflection)
    "    vec2 suv = vClip.xy / vClip.w * 0.5 + 0.5;\n"
    // refraction: sample the scene behind the water, distorted by the surface
    // normal; near the shore (shallow) don't distort so the edge stays soft.
    "    float shore = (uShoreFoam == 1) ? smoothstep(0.0, 1.2, vShoreDepth) : 1.0;\n"
    "    vec3 refr = base;\n"
    "    if (uHasScene == 1) {\n"
    "        vec2 ruv = clamp(suv + N.xz * uRefract * shore, 0.002, 0.998);\n"
    "        vec3 bottom = texture(uSceneTex, ruv).rgb;\n"
    // murk: shallow shows the bottom clearly; even deep keeps some refraction
    // visible (capped) so the effect doesn't vanish in deep water.
    // Turbidity = uOpacity: 0 crystal clear (see the bottom), 1 opaque (water
    // colour only). A touch of depth makes deeper water a bit murkier.
    "        float dturb = (uShoreFoam == 1) ? smoothstep(0.0, 40.0, vShoreDepth) * 0.2 : depthF * 0.2;\n"
    "        float murk = clamp(uOpacity + dturb, 0.0, 1.0);\n"
    "        refr = mix(bottom, base, murk);\n"   // clear shows bottom, murky shows water
    "    }\n"
    // reflection: screen-space march (reflects the real scene), else planar, else sky
    "    vec3 Rv = reflect(-V, N);\n"
    "    vec3 refl = skyColor(Rv);\n"
    // ---- Screen-space reflection: march the reflected ray through the depth copy ----
    "    if (uSSR == 1 && uHasScene == 1) {\n"
    "        vec3 rp = vWorldPos; float st = uSSRStep; float hit = 0.0; vec3 hitc = vec3(0.0);\n"
    "        for (int i = 0; i < 48; i++) {\n"
    "            rp += Rv * st;\n"
    "            vec4 cp = uViewProj * vec4(rp, 1.0);\n"
    "            if (cp.w <= 0.0) break;\n"
    "            vec3 ndc = cp.xyz / cp.w; vec2 ruv = ndc.xy * 0.5 + 0.5;\n"
    "            if (ruv.x < 0.0 || ruv.x > 1.0 || ruv.y < 0.0 || ruv.y > 1.0) break;\n"
    "            float sd = texture(uDepthTex, ruv).r; float rd = ndc.z * 0.5 + 0.5;\n"
    "            if (rd > sd + 0.00035) {\n"                    /* ray went behind geometry -> reflect it */
    "                vec2 edge = abs(ruv - 0.5) * 2.0;\n"
    "                float fade = 1.0 - max(edge.x, edge.y);\n" /* fade near screen borders */
    "                hitc = texture(uSceneTex, ruv).rgb; hit = clamp(fade * 2.0, 0.0, 1.0); break;\n"
    "            }\n"
    "            st *= 1.12;\n"                                 /* grow the step: cheap long march */
    "        }\n"
    "        refl = mix(refl, hitc, hit * uSSRStrength);\n"
    "    }\n"
    "    if (uHasRefl == 1) {\n"
    "        vec2 ruv = suv + N.xz * 0.05;\n"
    "        if (uReflFlipV == 1) ruv.y = 1.0 - ruv.y;\n"
    "        refl = mix(refl, texture(uReflTex, clamp(ruv, 0.0, 1.0)).rgb, uReflStrength);\n"
    "    }\n"
    "    vec3 col = mix(refr, refl, clamp(fres, 0.0, 0.95));\n"
    // sun glint: a sharp highlight + a softer sparkle
    "    vec3 Hh = normalize(L + V);\n"
    "    float spec = pow(max(dot(N, Hh), 0.0), 200.0);\n"
    "    float glint = pow(max(dot(N, Hh), 0.0), 40.0) * 0.25;\n"
    "    col += uLightColor * (spec * 1.3 + glint);\n"
    // with refraction the bottom is already composited, so render (near) opaque,
    // fading only at the very shoreline for a soft edge; without it, blend.
    // With refraction the bottom is composited, so alpha just feathers the very
    // shoreline (independent of turbidity, so opaque water still has a soft edge).
    "    float alpha = (uHasScene == 1) ? mix(0.0, 1.0, shore)\n"
    "                                    : uOpacity * mix(0.8, 1.0, fres);\n"
    // ---- Foam ----
    "    float foam = 0.0;\n"
    // animated breakup pattern so foam shimmers / laps instead of a flat band
    "    float n = 0.5 + 0.5 * sin(vWorldPos.x * 1.3 + uTime * 2.5)\n"
    "                        * sin(vWorldPos.z * 1.1 - uTime * 1.9);\n"
    // Open water stays CLEAN (no whitecaps): the only white is where waves break on the
    // shore. A tiny crest sparkle just on the very tallest swell crests, near invisible.
    "    float crest = smoothstep(0.85, 1.0, vWaveNorm);\n"
    "    foam = max(foam, crest * 0.06 * n);\n"
    // Shoreline break: whitewater ONLY in shallow water (near the waterline), and it PULSES
    // as each swell crest arrives -> the wave "breaks" when it reaches the beach.
    "    if (uShoreFoam == 1) {\n"
    "        float atShore = smoothstep(uBreakDepth, 0.0, vShoreDepth);\n"    // 1 at waterline -> 0 offshore
    "        float edgeFade = smoothstep(0.0, 0.35, vShoreDepth);\n"          // soften the very waterline
    "        float broken = 0.5 + 0.5 * n;\n"
    "        float amt = 0.65;\n"                                             // static lapping
    "        if (uSurf == 1) {\n"
    "            float ph = vShoreDepth * uSurfFreq - uTime * uSurfSpeed;\n"
    "            float arrive = pow(0.5 + 0.5 * sin(ph * 6.2831853), 3.0);\n" // sharp breaking pulse
    "            amt = 0.25 + 0.75 * arrive;\n"
    "        }\n"
    "        foam = max(foam, atShore * edgeFade * broken * amt);\n"
    "    }\n"
    // CONTACT foam: white where the water meets ANY geometry (rocks, ship hulls, the
    // shore) via the depth buffer -> the swell washes and breaks against objects. Pulses
    // so it churns instead of a static ring.
    "    if (uHasDepth == 1) {\n"
    "        float sd = texture(uDepthTex, suv).r;\n"
    "        vec4 sp = uInvProj * vec4(suv * 2.0 - 1.0, sd * 2.0 - 1.0, 1.0);\n"
    "        vec4 wpv = uInvProj * vec4(suv * 2.0 - 1.0, gl_FragCoord.z * 2.0 - 1.0, 1.0);\n"
    "        float gap = abs(sp.z / sp.w - wpv.z / wpv.w);\n"        // eye-space surface->object gap
    "        float contact = 1.0 - smoothstep(0.0, 1.8, gap);\n"
    "        float pulse = 0.55 + 0.45 * sin(uTime * 3.2 + vWorldPos.x * 0.6 - vWorldPos.z * 0.5);\n"
    "        foam = max(foam, contact * pulse * (0.5 + 0.5 * n) * 0.9);\n"
    "    }\n"
    "    foam = max(foam, rfoam);\n"
    "    foam = clamp(foam, 0.0, 1.0);\n"
    "    col = mix(col, vec3(1.0), foam * 0.85);\n"
    "    alpha = max(alpha, foam * 0.95);\n"
    // Atmospheric horizon haze: distant sea fades into the sky (vast-ocean depth cue),
    // softening the hard water/sky seam.
    "    vec3 vd = normalize(vWorldPos - uCameraPos);\n"
    "    float dist = length(vWorldPos - uCameraPos);\n"
    "    float haze = smoothstep(70.0, 380.0, dist);\n"
    "    vec3 hazeCol = vec3(0.74, 0.83, 0.93);\n"   // light atmospheric horizon
    "    col = mix(col, hazeCol, haze * 0.9);\n"
    "    alpha = max(alpha, haze * 0.9);\n"     // distant water stays opaque against the sky
    "    FragColor = vec4(col, clamp(alpha, 0.0, 1.0));\n"
    "}\n";

/* ---- tessellation stages (GL4): give the ocean real geometric wave VOLUME ---- */
static const char *water_tvert =
    "#version 400 core\n"
    "layout(location=0) in vec3 position;\n"
    "layout(location=1) in vec3 anormal;\n"
    "layout(location=2) in vec2 texcoord;\n"
    "uniform mat4 uModel;\n"
    "out vec3 tcWorld; out vec3 tcFlow; out vec2 tcTex;\n"
    "void main(){ tcWorld = vec3(uModel*vec4(position,1.0)); tcFlow = anormal; tcTex = texcoord; }\n";

static const char *water_tcs =
    "#version 400 core\n"
    "layout(vertices=3) out;\n"
    "in vec3 tcWorld[]; in vec3 tcFlow[]; in vec2 tcTex[];\n"
    "out vec3 teWorld[]; out vec3 teFlow[]; out vec2 teTex[];\n"
    "uniform vec3 uCameraPos; uniform float uTessMax;\n"
    "void main(){\n"
    "  teWorld[gl_InvocationID]=tcWorld[gl_InvocationID];\n"
    "  teFlow[gl_InvocationID]=tcFlow[gl_InvocationID];\n"
    "  teTex[gl_InvocationID]=tcTex[gl_InvocationID];\n"
    "  if(gl_InvocationID==0){\n"
    "    vec3 c=(tcWorld[0]+tcWorld[1]+tcWorld[2])/3.0;\n"
    "    float d=distance(c,uCameraPos);\n"
    "    float lvl=clamp(uTessMax*(1.0 - d/220.0), 1.0, uTessMax);\n"   // finer near camera
    "    gl_TessLevelInner[0]=lvl; gl_TessLevelOuter[0]=lvl; gl_TessLevelOuter[1]=lvl; gl_TessLevelOuter[2]=lvl;\n"
    "  }\n}\n";

static const char *water_tes =
    "#version 400 core\n"
    "layout(triangles, fractional_odd_spacing, ccw) in;\n"
    "in vec3 teWorld[]; in vec3 teFlow[]; in vec2 teTex[];\n"
    "uniform mat4 uView; uniform mat4 uProjection;\n"
    "uniform float uTime; uniform float uWaveAmp; uniform float uWaveLen; uniform float uWaveSpeed;\n"
    "uniform float uSwellAmp; uniform float uSwellDirX; uniform float uSwellDirZ; uniform float uSwellLen; uniform float uSwellSpeed;\n"
    "out vec3 vWorldPos; out vec3 vNormal; out vec4 vClip;\n"
    "out float vWaveNorm; out float vShoreDepth; out vec2 vFlowDir; out float vAlong; out float vLavaHot;\n"
    "void main(){\n"
    "  vec3 wp = gl_TessCoord.x*teWorld[0]+gl_TessCoord.y*teWorld[1]+gl_TessCoord.z*teWorld[2];\n"
    "  vec3 flow = gl_TessCoord.x*teFlow[0]+gl_TessCoord.y*teFlow[1]+gl_TessCoord.z*teFlow[2];\n"
    "  vec2 tex = gl_TessCoord.x*teTex[0]+gl_TessCoord.y*teTex[1]+gl_TessCoord.z*teTex[2];\n"
    "  vec2 base = wp.xz;\n"
    "  vec2 dirs[4]=vec2[](normalize(vec2(1.0,0.35)),normalize(vec2(-0.45,1.0)),normalize(vec2(0.7,-0.6)),normalize(vec2(-0.8,-0.3)));\n"
    "  float amp=uWaveAmp; float len=uWaveLen; float h=0.0; vec3 disp=vec3(0.0); vec3 nrm=vec3(0.0,1.0,0.0);\n"
    "  for(int i=0;i<6;i++){\n"                                          // 6 octaves: fine detail (tessellated)
    "    int di=i%4; float k=6.2831853/len; float Q=0.55/(k*amp*4.0+0.001);\n"
    "    float ph=dot(dirs[di],base)*k + uTime*uWaveSpeed*(1.0+float(i)*0.3);\n"
    "    float c=cos(ph), s=sin(ph);\n"
    "    disp.x += Q*amp*dirs[di].x*c; disp.z += Q*amp*dirs[di].y*c; disp.y += amp*s; h+=amp*s;\n"
    "    nrm.x -= dirs[di].x*(k*amp*c); nrm.z -= dirs[di].y*(k*amp*c); nrm.y -= Q*(k*amp*s);\n"
    "    amp*=0.62; len*=0.66;\n"
    "  }\n"
    "  wp += disp;\n"
    "  if(uSwellAmp>0.0){\n"
    "    float shoal=clamp(1.0 - tex.x/6.0,0.0,1.0);\n"
    "    vec2 swd=normalize(vec2(uSwellDirX,uSwellDirZ)+vec2(1e-4,0.0));\n"
    "    float sk=6.2831853/max(uSwellLen,0.5);\n"
    "    float sarg=dot(swd,base)*sk - uTime*uSwellSpeed;\n"
    "    float samp=uSwellAmp*(0.5+0.8*shoal);\n"
    "    wp.y += samp*sin(sarg); h += samp*sin(sarg);\n"
    "    nrm.x -= swd.x*(sk*samp*cos(sarg)); nrm.z -= swd.y*(sk*samp*cos(sarg));\n"
    "  }\n"
    "  vFlowDir=flow.xz; vShoreDepth=tex.x; vAlong=tex.y; vLavaHot=0.0;\n"
    "  vWaveNorm=h/(uWaveAmp*1.8+0.001);\n"
    "  vNormal=normalize(nrm); vWorldPos=wp;\n"
    "  vClip=uProjection*uView*vec4(wp,1.0); gl_Position=vClip;\n"
    "}\n";

/* ---- state ------------------------------------------------------------- */

static struct {
    int enabled;
    int initialized;
    G3DShaderProgram *shader;
    G3DMesh *mesh;
    float level;
    float wave_amp;
    float wave_len;
    float wave_speed;
    float deep[3];
    float shallow[3];
    unsigned int tex_handle;  /* optional surface texture (0 = none) */
    int refl_enabled;
    float refl_strength;
    int refl_flipv;
    int ssr;              /* screen-space reflections on the water (default on) */
    float ssr_strength;
    int ssr_init;
    /* ocean/beach: directional swell that shoals + breaking surf */
    float swell_amp, swell_dirx, swell_dirz, swell_len, swell_speed;
    int   surf;
    float surf_freq, surf_speed, break_depth;
    /* GL4 tessellation (geometric wave volume) */
    int   tess, tess_init, tess_checked, tess_avail;
    void *tess_shader;
} g_water = {0};

void g3d_water_set_texture(unsigned int gl_handle) {
    g_water.tex_handle = gl_handle;
}

/* ---- ripples (expanding rings: splashes, river mouths, swimmers) -------- */

#define MAX_RIPPLES 24
#define RIPPLE_LIFE 3.0f
static struct { float x, z, t0, strength; int active; } g_ripples[MAX_RIPPLES];
static float g_ripple_strength = 1.0f;

void g3d_water_set_ripple_strength(float s) { g_ripple_strength = s < 0.0f ? 0.0f : s; }

void g3d_water_ripple(float x, float z, float strength) {
    float now = (float)SDL_GetTicks() / 1000.0f;
    int slot = -1; float oldest = 1e30f;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!g_ripples[i].active) { slot = i; break; }
        if (g_ripples[i].t0 < oldest) { oldest = g_ripples[i].t0; slot = i; }
    }
    if (slot < 0) slot = 0;
    g_ripples[slot].x = x; g_ripples[slot].z = z;
    g_ripples[slot].t0 = now; g_ripples[slot].strength = strength;
    g_ripples[slot].active = 1;
}

void g3d_water_splash(float x, float y, float z, float strength) {
    if (strength < 0.0f) strength = 0.0f;
    g3d_water_ripple(x, z, strength);
    /* a short burst of whitish droplets that fly up, fall and vanish */
    int n = (int)(8.0f + 18.0f * strength);
    float speed = 1.6f + 2.4f * strength;
    g3d_particles_burst(x, y, z, n, speed, 0.12f, 0.7f, 0.85f, 0.92f, 1.0f);
}

/* Continuous ripple sources (e.g. river mouths): registered once, the engine
   re-emits a ripple at each on a timer so they keep rippling on their own. */
#define MAX_RIPPLE_SOURCES 64
static struct { float x, z, strength; } g_ripple_src[MAX_RIPPLE_SOURCES];
static int g_ripple_src_count = 0;

void g3d_water_clear_ripple_sources(void) { g_ripple_src_count = 0; }

void g3d_water_add_ripple_source(float x, float z, float strength) {
    if (g_ripple_src_count >= MAX_RIPPLE_SOURCES) return;
    g_ripple_src[g_ripple_src_count].x = x;
    g_ripple_src[g_ripple_src_count].z = z;
    g_ripple_src[g_ripple_src_count].strength = strength;
    g_ripple_src_count++;
}

void g3d_water_tick(void) {
    static float last = -1.0f;
    float now = (float)SDL_GetTicks() / 1000.0f;
    if (now - last < 0.35f) return;
    last = now;
    for (int i = 0; i < g_ripple_src_count; i++)
        g3d_water_ripple(g_ripple_src[i].x, g_ripple_src[i].z, g_ripple_src[i].strength);
}

/* Upload the active ripples to whichever water shader is bound. */
static void set_ripple_uniforms(G3DShaderProgram *sh) {
    float now = (float)SDL_GetTicks() / 1000.0f;
    float data[MAX_RIPPLES * 4];
    int n = 0;
    for (int i = 0; i < MAX_RIPPLES; i++) {
        if (!g_ripples[i].active) continue;
        float age = now - g_ripples[i].t0;
        if (age > RIPPLE_LIFE) { g_ripples[i].active = 0; continue; }
        data[n * 4 + 0] = g_ripples[i].x;
        data[n * 4 + 1] = g_ripples[i].z;
        data[n * 4 + 2] = age;
        data[n * 4 + 3] = g_ripples[i].strength;
        n++;
    }
#ifndef VITA
    int loc = g3d_shader_get_uniform(sh, "uRipples");
    if (loc >= 0 && n > 0) glUniform4fv(loc, (GLsizei)n, data);
#endif
    g3d_shader_set_int(sh, "uRippleCount", n);
    g3d_shader_set_float(sh, "uRippleStrength", g_ripple_strength);
}

void g3d_water_set_reflection(int enable, float strength) {
    g_water.refl_enabled = enable ? 1 : 0;
    g_water.refl_strength = strength;
    if (g_water.refl_strength <= 0.0f) g_water.refl_strength = 0.6f;
}

void g3d_water_set_reflection_flip(int flip) { g_water.refl_flipv = flip ? 1 : 0; }

int g3d_water_reflection_enabled(void) {
    return (g_water.enabled && g_water.initialized && g_water.refl_enabled);
}

float g3d_water_get_level(void) { return g_water.level; }

int g3d_water_create(float level, float size, int subdiv) {
    if (subdiv < 2)
        subdiv = 2;
    if (subdiv > 250)
        subdiv = 250;

    if (!g_water.shader) {
        g_water.shader = g3d_shader_create(water_vert, water_frag);
        if (!g_water.shader) {
            fprintf(stderr, "G3D: water shader failed\n");
            return 0;
        }
    }

    if (g_water.mesh) {
        g3d_mesh_free(g_water.mesh);
        g_water.mesh = NULL;
    }
    /* A flat subdivided grid (heights = 0); the shader makes the waves */
    g_water.mesh = g3d_primitive_create_terrain(subdiv, size, 0.0f, 1.0f, 0);
    if (!g_water.mesh)
        return 0;
    g3d_mesh_upload_gpu(g_water.mesh);

    g_water.level = level;
    if (g_water.wave_len <= 0.0f) {
        g_water.wave_amp = 0.35f;
        g_water.wave_len = 7.0f;
        g_water.wave_speed = 1.6f;
        g_water.deep[0] = 0.04f; g_water.deep[1] = 0.22f; g_water.deep[2] = 0.34f;
        g_water.shallow[0] = 0.18f; g_water.shallow[1] = 0.45f; g_water.shallow[2] = 0.55f;
    }
    g_water.enabled = 1;
    g_water.initialized = 1;
    printf("G3D: water created (level=%.1f size=%.1f subdiv=%d)\n", level, size,
           subdiv);
    return 1;
}

void g3d_water_set_waves(float amplitude, float wavelength, float speed) {
    g_water.wave_amp = amplitude;
    g_water.wave_len = (wavelength > 0.1f) ? wavelength : 0.1f;
    g_water.wave_speed = speed;
}

void g3d_water_set_color(float dr, float dg, float db, float sr, float sg,
                         float sb) {
    g_water.deep[0] = dr; g_water.deep[1] = dg; g_water.deep[2] = db;
    g_water.shallow[0] = sr; g_water.shallow[1] = sg; g_water.shallow[2] = sb;
}

void g3d_water_set_enabled(int enabled) { g_water.enabled = enabled; }

int g3d_water_is_enabled(void) { return g_water.enabled && g_water.initialized; }

void g3d_water_render_pass(G3DCamera *camera, int flip_y) {
    if (!g_water.enabled || !g_water.initialized || !g_water.shader ||
        !g_water.mesh || !camera)
        return;

#ifndef VITA
    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1];
        proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9];
        proj.m[13] = -proj.m[13];
    }
    Mat4 model = mat4_translate(0.0f, g_water.level, 0.0f);
    float t = (float)SDL_GetTicks() / 1000.0f;

    g3d_shader_use(g_water.shader);
    g3d_shader_set_mat4(g_water.shader, "uModel", model);
    g3d_shader_set_mat4(g_water.shader, "uView", view);
    g3d_shader_set_mat4(g_water.shader, "uProjection", proj);
    g3d_shader_set_float(g_water.shader, "uTime", t);
    g3d_shader_set_float(g_water.shader, "uWaveAmp", g_water.wave_amp);
    g3d_shader_set_float(g_water.shader, "uWaveLen", g_water.wave_len);
    g3d_shader_set_float(g_water.shader, "uWaveSpeed", g_water.wave_speed);
    g3d_shader_set_float(g_water.shader, "uDepth", 0.0f);  /* global water: fresnel only */
    g3d_shader_set_float(g_water.shader, "uOpacity", 0.9f);
    g3d_shader_set_float(g_water.shader, "uLava", 0.0f);   /* global water is never lava */
    g3d_shader_set_int(g_water.shader, "uShoreFoam", 0);   /* crest foam only */
    set_ripple_uniforms(g_water.shader);
    {
        uint32_t scn = g3d_renderer_scene_texture();
        if (scn) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, scn);
            g3d_shader_set_int(g_water.shader, "uSceneTex", 2);
            g3d_shader_set_int(g_water.shader, "uHasScene", 1);
            g3d_shader_set_float(g_water.shader, "uRefract", 0.14f);
            glActiveTexture(GL_TEXTURE0);
        } else {
            g3d_shader_set_int(g_water.shader, "uHasScene", 0);
        }
    }
    g3d_shader_set_vec3(g_water.shader, "uCameraPos", camera->position);
    g3d_shader_set_vec3(g_water.shader, "uWaterDeep",
                        vec3_make(g_water.deep[0], g_water.deep[1], g_water.deep[2]));
    g3d_shader_set_vec3(g_water.shader, "uWaterShallow",
                        vec3_make(g_water.shallow[0], g_water.shallow[1], g_water.shallow[2]));

    /* Sun direction / colour from the active scene's directional light */
    Vec3 ldir = vec3_make(-0.5f, -1.0f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 1.0f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction;
            lcol = vec3_make(l->color[0], l->color[1], l->color[2]);
            break;
        }
    }
    g3d_shader_set_vec3(g_water.shader, "uLightDir", ldir);
    g3d_shader_set_vec3(g_water.shader, "uLightColor", lcol);

    /* Optional surface texture (ripple detail + tint) */
    if (g_water.tex_handle) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_water.tex_handle);
        g3d_shader_set_int(g_water.shader, "uWaterTex", 0);
        g3d_shader_set_int(g_water.shader, "uHasTex", 1);
    } else {
        g3d_shader_set_int(g_water.shader, "uHasTex", 0);
    }

    /* Planar reflection texture (unit 1) */
    if (g_water.refl_enabled) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, g3d_renderer_reflection_texture());
        g3d_shader_set_int(g_water.shader, "uReflTex", 1);
        g3d_shader_set_int(g_water.shader, "uHasRefl", 1);
        g3d_shader_set_float(g_water.shader, "uReflStrength", g_water.refl_strength);
        g3d_shader_set_int(g_water.shader, "uReflFlipV", g_water.refl_flipv);
        glActiveTexture(GL_TEXTURE0);
    } else {
        g3d_shader_set_int(g_water.shader, "uHasRefl", 0);
    }

    /* Transparent surface: blend, keep depth test, don't write depth */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    glBindVertexArray(g_water.mesh->vao);
    glDrawElements(GL_TRIANGLES, g_water.mesh->index_count, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

/* ---- Fluid zones ------------------------------------------------------- */

#define MAX_FLUIDS 64

typedef struct {
    float cx, cz, level, depth;
    float size_x, size_z;
    G3DMesh *mesh;   /* custom lake surface (world-space). NULL = rectangle (unit_mesh scaled) */
} FluidZone;

static struct {
    FluidZone zones[MAX_FLUIDS];
    int count;
    G3DMesh *unit_mesh;        /* shared 1x1 subdivided grid, scaled per zone */
    float amp, len, speed;
    float deep[3], shallow[3];
    unsigned int tex;
    float opacity;
    int style_set;
    int kind;                  /* 0 = water, 1 = lava (emissive, slow, opaque) */
} g_fluid = {0};

void g3d_fluid_clear(void) {
    for (int i = 0; i < g_fluid.count; i++)
        if (g_fluid.zones[i].mesh) { g3d_mesh_free(g_fluid.zones[i].mesh); g_fluid.zones[i].mesh = NULL; }
    g_fluid.count = 0;
}

int g3d_fluid_count(void) { return g_fluid.count; }

/* Auto-detect whether the camera is under a water surface and drive the
   renderer's underwater post-process. Called by the renderer each frame. */
void g3d_water_update_underwater(G3DCamera *camera) {
    if (!camera) return;
    /* No water plane or fluid zone registered (e.g. ocean/watersim-only scenes):
       don't override manual g3d_set_underwater control. */
    if (!(g_water.enabled && g_water.initialized) && g_fluid.count == 0)
        return;
    Vec3 p = camera->position;
    int under = 0;
    float tint[3] = {0.10f, 0.26f, 0.34f};

    if (g_water.enabled && g_water.initialized && p.y < g_water.level) {
        under = 1;
        tint[0] = g_water.deep[0]; tint[1] = g_water.deep[1]; tint[2] = g_water.deep[2];
    }
    for (int i = 0; !under && i < g_fluid.count; i++) {
        G3DMesh *m = g_fluid.zones[i].mesh;
        if (!m) continue;
        if (p.y < m->aabb_max[1] &&
            p.x >= m->aabb_min[0] && p.x <= m->aabb_max[0] &&
            p.z >= m->aabb_min[2] && p.z <= m->aabb_max[2]) {
            under = 1;
            tint[0] = g_fluid.deep[0]; tint[1] = g_fluid.deep[1]; tint[2] = g_fluid.deep[2];
        }
    }
    g3d_renderer_set_underwater(under, tint[0], tint[1], tint[2], 1.0f);
}

/* Add a fluid zone from a caller-built world-space mesh (e.g. a flood-filled
   lake surface). The engine takes ownership and frees it on clear/shutdown. */
int g3d_fluid_add_mesh(G3DMesh *mesh, float depth) {
    if (!mesh || g_fluid.count >= MAX_FLUIDS) return -1;
    if (!g_water.shader) {
        g_water.shader = g3d_shader_create(water_vert, water_frag);
        if (!g_water.shader) return -1;
    }
    FluidZone *z = &g_fluid.zones[g_fluid.count];
    z->cx = z->cz = z->size_x = z->size_z = 0.0f;
    z->level = 0.0f; z->depth = depth; z->mesh = mesh;
    if (!g_fluid.style_set) {
        g_fluid.amp = 0.25f; g_fluid.len = 6.0f; g_fluid.speed = 1.3f;
        g_fluid.deep[0] = 0.03f; g_fluid.deep[1] = 0.18f; g_fluid.deep[2] = 0.30f;
        g_fluid.shallow[0] = 0.15f; g_fluid.shallow[1] = 0.42f; g_fluid.shallow[2] = 0.55f;
        if (g_fluid.opacity <= 0.0f) g_fluid.opacity = 0.85f;
    }
    return g_fluid.count++;
}

int g3d_fluid_add(float cx, float cz, float size_x, float size_z,
                  float level, float depth) {
    if (g_fluid.count >= MAX_FLUIDS) return -1;
    if (size_x < 0.1f) size_x = 0.1f;
    if (size_z < 0.1f) size_z = 0.1f;
    if (!g_fluid.unit_mesh) {
        /* unit grid spanning [-0.5,0.5] on XZ, flat (waves come from the shader) */
        g_fluid.unit_mesh = g3d_primitive_create_terrain(100, 1.0f, 0.0f, 1.0f, 0);
        if (g_fluid.unit_mesh) g3d_mesh_upload_gpu(g_fluid.unit_mesh);
    }
    if (!g_water.shader) {
        g_water.shader = g3d_shader_create(water_vert, water_frag);
        if (!g_water.shader) return -1;
    }
    FluidZone *z = &g_fluid.zones[g_fluid.count];
    z->cx = cx; z->cz = cz; z->level = level; z->depth = depth;
    z->size_x = size_x; z->size_z = size_z;
    if (!g_fluid.style_set) {
        g_fluid.amp = 0.25f; g_fluid.len = 6.0f; g_fluid.speed = 1.3f;
        g_fluid.deep[0] = 0.03f; g_fluid.deep[1] = 0.18f; g_fluid.deep[2] = 0.30f;
        g_fluid.shallow[0] = 0.15f; g_fluid.shallow[1] = 0.42f; g_fluid.shallow[2] = 0.55f;
        if (g_fluid.opacity <= 0.0f) g_fluid.opacity = 0.85f;
    }
    return g_fluid.count++;
}

void g3d_fluid_set_style(float amp, float len, float speed,
                         float dr, float dg, float db,
                         float sr, float sg, float sb,
                         unsigned int tex_handle, float opacity) {
    g_fluid.amp = amp; g_fluid.len = (len > 0.1f) ? len : 0.1f; g_fluid.speed = speed;
    g_fluid.deep[0] = dr; g_fluid.deep[1] = dg; g_fluid.deep[2] = db;
    g_fluid.shallow[0] = sr; g_fluid.shallow[1] = sg; g_fluid.shallow[2] = sb;
    g_fluid.tex = tex_handle;
    g_fluid.opacity = (opacity > 0.0f) ? opacity : 0.85f;
    g_fluid.style_set = 1;
}

/* Material kind for all fluid zones: 0 = water, 1 = lava (emissive glow, slow
   flowing crust, opaque, no sky reflection). */
void g3d_fluid_set_kind(int kind) {
    g_fluid.kind = kind;
}

int g3d_fluid_get_kind(void) { return g_fluid.kind; }

void g3d_fluid_render_pass(G3DCamera *camera, int flip_y) {
#ifndef VITA
    /* unit_mesh is only needed by rectangle zones; lake zones carry their own
       mesh, so don't gate the whole pass on it. */
    if (g_fluid.count == 0 || !g_water.shader || !camera)
        return;

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }
    float t = (float)SDL_GetTicks() / 1000.0f;

    g3d_shader_use(g_water.shader);
    g3d_shader_set_mat4(g_water.shader, "uView", view);
    g3d_shader_set_mat4(g_water.shader, "uProjection", proj);
    g3d_shader_set_float(g_water.shader, "uTime", t);
    g3d_shader_set_float(g_water.shader, "uWaveAmp", g_fluid.amp);
    g3d_shader_set_float(g_water.shader, "uWaveLen", g_fluid.len);
    g3d_shader_set_float(g_water.shader, "uWaveSpeed", g_fluid.speed);
    g3d_shader_set_vec3(g_water.shader, "uCameraPos", camera->position);
    g3d_shader_set_vec3(g_water.shader, "uWaterDeep",
                        vec3_make(g_fluid.deep[0], g_fluid.deep[1], g_fluid.deep[2]));
    g3d_shader_set_vec3(g_water.shader, "uWaterShallow",
                        vec3_make(g_fluid.shallow[0], g_fluid.shallow[1], g_fluid.shallow[2]));
    g3d_shader_set_float(g_water.shader, "uOpacity", g_fluid.opacity > 0.0f ? g_fluid.opacity : 0.85f);
    g3d_shader_set_float(g_water.shader, "uLava", g_fluid.kind == 1 ? 1.0f : 0.0f);
    g3d_shader_set_int(g_water.shader, "uHasRefl", 0);
    set_ripple_uniforms(g_water.shader);
    {
        uint32_t scn = g3d_renderer_scene_texture();
        if (scn) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, scn);
            g3d_shader_set_int(g_water.shader, "uSceneTex", 2);
            g3d_shader_set_int(g_water.shader, "uHasScene", 1);
            g3d_shader_set_float(g_water.shader, "uRefract", 0.13f);
            glActiveTexture(GL_TEXTURE0);
        } else {
            g3d_shader_set_int(g_water.shader, "uHasScene", 0);
        }
    }

    Vec3 ldir = vec3_make(-0.5f, -1.0f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 1.0f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction; lcol = vec3_make(l->color[0], l->color[1], l->color[2]); break;
        }
    }
    g3d_shader_set_vec3(g_water.shader, "uLightDir", ldir);
    g3d_shader_set_vec3(g_water.shader, "uLightColor", lcol);

    if (g_fluid.tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fluid.tex);
        g3d_shader_set_int(g_water.shader, "uWaterTex", 0);
        g3d_shader_set_int(g_water.shader, "uHasTex", 1);
    } else {
        g3d_shader_set_int(g_water.shader, "uHasTex", 0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    for (int i = 0; i < g_fluid.count; i++) {
        FluidZone *z = &g_fluid.zones[i];
        g3d_shader_set_float(g_water.shader, "uDepth", z->depth);
        if (z->mesh) {
            /* custom lake surface: already in world space at its level; its
               texcoord.x carries the shore depth, so enable shoreline foam */
            g3d_shader_set_int(g_water.shader, "uShoreFoam", 1);
            g3d_shader_set_mat4(g_water.shader, "uModel", mat4_identity());
            glBindVertexArray(z->mesh->vao);
            glDrawElements(GL_TRIANGLES, z->mesh->index_count, GL_UNSIGNED_INT, 0);
        } else {
            g3d_shader_set_int(g_water.shader, "uShoreFoam", 0);
            Mat4 model = mat4_multiply(mat4_translate(z->cx, z->level, z->cz),
                                       mat4_scale(z->size_x, 1.0f, z->size_z));
            g3d_shader_set_mat4(g_water.shader, "uModel", model);
            glBindVertexArray(g_fluid.unit_mesh ? g_fluid.unit_mesh->vao : 0);
            if (g_fluid.unit_mesh)
                glDrawElements(GL_TRIANGLES, g_fluid.unit_mesh->index_count, GL_UNSIGNED_INT, 0);
        }
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#endif
}

/* Draw one caller-owned world-space water mesh with the shared water shader and
   the current fluid style (waves, colour, texture, refraction, foam, flow scroll,
   lava). Used by the live height-field water sim, which rebuilds its mesh each
   frame. The mesh packs flow dir in normal.xz and shore depth in texcoord.x. */
void g3d_water_set_ssr(int enable, float strength) {
    g_water.ssr = enable ? 1 : 0;
    if (strength >= 0.0f) g_water.ssr_strength = strength;
    g_water.ssr_init = 1;
}

/* Ocean/beach: a directional swell (dir + amplitude) that shoals toward shallow water,
   with breaking surf lines on the shore. amp 0 turns it off (lakes/rivers). */
void g3d_water_set_ocean(float dirx, float dirz, float swell_amp) {
    g_water.swell_dirx = dirx; g_water.swell_dirz = dirz;
    g_water.swell_amp = swell_amp > 0.0f ? swell_amp : 0.0f;
    if (g_water.swell_len <= 0.0f) g_water.swell_len = 14.0f;
    if (g_water.swell_speed <= 0.0f) g_water.swell_speed = 1.3f;
    g_water.surf = swell_amp > 0.0f ? 1 : 0;
    if (g_water.surf_freq <= 0.0f) g_water.surf_freq = 0.16f;   // spaced sets, not dense stripes
    if (g_water.surf_speed <= 0.0f) g_water.surf_speed = 0.35f;
    if (g_water.break_depth <= 0.0f) g_water.break_depth = 3.0f;
}

/* GL4 tessellation on/off for the water (geometric wave volume). No-op if GL4 absent. */
void g3d_water_set_tessellation(int enable) {
    g_water.tess = enable ? 1 : 0;
    g_water.tess_init = 1;
}

#ifndef VITA
/* Set the ocean/beach swell + surf uniforms on the water shader (call after use). */
static void set_ocean_uniforms(void *shader) {
    g3d_shader_set_float(shader, "uSwellAmp", g_water.swell_amp);
    g3d_shader_set_float(shader, "uSwellDirX", g_water.swell_dirx);
    g3d_shader_set_float(shader, "uSwellDirZ", g_water.swell_dirz);
    g3d_shader_set_float(shader, "uSwellLen", g_water.swell_len > 0.0f ? g_water.swell_len : 14.0f);
    g3d_shader_set_float(shader, "uSwellSpeed", g_water.swell_speed > 0.0f ? g_water.swell_speed : 1.3f);
    g3d_shader_set_int(shader, "uSurf", g_water.surf);
    g3d_shader_set_float(shader, "uSurfFreq", g_water.surf_freq > 0.0f ? g_water.surf_freq : 0.35f);
    g3d_shader_set_float(shader, "uSurfSpeed", g_water.surf_speed > 0.0f ? g_water.surf_speed : 0.45f);
    g3d_shader_set_float(shader, "uBreakDepth", g_water.break_depth > 0.0f ? g_water.break_depth : 2.5f);
}
#endif

void g3d_water_draw_mesh(G3DMesh *mesh, G3DCamera *camera, int flip_y) {
#ifndef VITA
    if (!mesh || !camera) return;
    if (!g_water.shader) {
        g_water.shader = g3d_shader_create(water_vert, water_frag);
        if (!g_water.shader) return;
    }
    if (!g_water.ssr_init) { g_water.ssr = 1; g_water.ssr_strength = 0.65f; g_water.ssr_init = 1; }

    /* GL4 tessellation: detect once, lazily build the tess program; fall back safely. */
    if (!g_water.tess_checked) {
        GLint major = 0; glGetIntegerv(GL_MAJOR_VERSION, &major);
        g_water.tess_avail = (major >= 4) ? 1 : 0;
        if (!g_water.tess_init) g_water.tess = 1;   /* default on when GL4 is present */
        g_water.tess_checked = 1;
    }
    int use_tess = (g_water.tess && g_water.tess_avail && g_fluid.kind != 1);   /* not lava */
    if (use_tess && !g_water.tess_shader) {
        g_water.tess_shader = g3d_shader_create_tess(water_tvert, water_tcs, water_tes, water_frag);
        if (!g_water.tess_shader) { g_water.tess_avail = 0; use_tess = 0; }     /* compile failed -> fallback */
    }
    G3DShaderProgram *sh = (G3DShaderProgram *)(use_tess ? g_water.tess_shader : g_water.shader);

    Mat4 view = g3d_camera_get_view(camera);
    Mat4 proj = g3d_camera_get_projection(camera);
    if (flip_y) {
        proj.m[1] = -proj.m[1]; proj.m[5] = -proj.m[5];
        proj.m[9] = -proj.m[9]; proj.m[13] = -proj.m[13];
    }
    float t = (float)SDL_GetTicks() / 1000.0f;

    g3d_shader_use(sh);
    g3d_shader_set_mat4(sh, "uView", view);
    g3d_shader_set_mat4(sh, "uProjection", proj);
    g3d_shader_set_mat4(sh, "uModel", mat4_identity());
    g3d_shader_set_float(sh, "uTime", t);
    /* calm waves: the sim water reads its motion from the flow scroll (downstream),
       so big omnidirectional Gerstner waves would fight it and look like it flows
       uphill. Keep the surface gentle. */
    g3d_shader_set_float(sh, "uWaveAmp", g_water.swell_amp > 0.0f ? 0.28f : 0.04f);
    g3d_shader_set_float(sh, "uWaveLen", 8.0f);
    g3d_shader_set_float(sh, "uWaveSpeed", 1.0f);
    g3d_shader_set_vec3(sh, "uCameraPos", camera->position);
    g3d_shader_set_float(sh, "uTessMax", 12.0f);   /* max subdivision near camera */
    g3d_shader_set_vec3(sh, "uWaterDeep",
                        g_fluid.style_set ? vec3_make(g_fluid.deep[0], g_fluid.deep[1], g_fluid.deep[2])
                                          : vec3_make(0.03f, 0.18f, 0.30f));
    g3d_shader_set_vec3(sh, "uWaterShallow",
                        g_fluid.style_set ? vec3_make(g_fluid.shallow[0], g_fluid.shallow[1], g_fluid.shallow[2])
                                          : vec3_make(0.15f, 0.42f, 0.55f));
    g3d_shader_set_float(sh, "uOpacity", g_fluid.opacity > 0.0f ? g_fluid.opacity : 0.6f);
    g3d_shader_set_float(sh, "uLava", g_fluid.kind == 1 ? 1.0f : 0.0f);
    g3d_shader_set_float(sh, "uDepth", 2.0f);
    g3d_shader_set_int(sh, "uShoreFoam", 1);
    g3d_shader_set_int(sh, "uHasRefl", 0);
    set_ripple_uniforms(sh);
    {
        uint32_t scn = g3d_renderer_scene_texture();
        if (scn) {
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, scn);
            g3d_shader_set_int(sh, "uSceneTex", 2);
            g3d_shader_set_int(sh, "uHasScene", 1);
            g3d_shader_set_float(sh, "uRefract", 0.13f);
            glActiveTexture(GL_TEXTURE0);
        } else {
            g3d_shader_set_int(sh, "uHasScene", 0);
        }
        /* Scene depth: feeds both SSR (reflection) and CONTACT FOAM (waves breaking on
           rocks / hulls / the shore). Bind whenever available, not just for SSR. */
        uint32_t dep = g3d_renderer_scene_depth_texture();
        if (scn && dep) {
            g3d_shader_set_mat4(sh, "uViewProj", mat4_multiply(proj, view));
            g3d_shader_set_mat4(sh, "uInvProj", mat4_inverse(proj));
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, dep);
            g3d_shader_set_int(sh, "uDepthTex", 3);
            g3d_shader_set_int(sh, "uHasDepth", 1);
            g3d_shader_set_int(sh, "uSSR", g_water.ssr ? 1 : 0);
            g3d_shader_set_float(sh, "uSSRStrength", g_water.ssr_strength);
            g3d_shader_set_float(sh, "uSSRStep", 0.4f);
            glActiveTexture(GL_TEXTURE0);
        } else {
            g3d_shader_set_int(sh, "uSSR", 0);
            g3d_shader_set_int(sh, "uHasDepth", 0);
        }
    }
    set_ocean_uniforms(sh);   /* swell + breaking surf (ocean/beach) */
    Vec3 ldir = vec3_make(-0.5f, -1.0f, -0.4f);
    Vec3 lcol = vec3_make(1.0f, 1.0f, 1.0f);
    int lc = 0;
    int *lids = g3d_scene_impl_get_lights(&lc);
    for (int i = 0; i < lc; i++) {
        G3DLight *l = g3d_light_impl_get(lids[i]);
        if (l && l->type == G3D_LIGHT_TYPE_DIRECTIONAL) {
            ldir = l->direction; lcol = vec3_make(l->color[0], l->color[1], l->color[2]); break;
        }
    }
    g3d_shader_set_vec3(sh, "uLightDir", ldir);
    g3d_shader_set_vec3(sh, "uLightColor", lcol);
    if (g_fluid.tex) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_fluid.tex);
        g3d_shader_set_int(sh, "uWaterTex", 0);
        g3d_shader_set_int(sh, "uHasTex", 1);
    } else {
        g3d_shader_set_int(sh, "uHasTex", 0);
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(mesh->vao);
    if (use_tess) {
        glPatchParameteri(GL_PATCH_VERTICES, 3);
        glDrawElements(GL_PATCHES, mesh->index_count, GL_UNSIGNED_INT, 0);
    } else {
        glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_INT, 0);
    }
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
#else
    (void)mesh; (void)camera; (void)flip_y;
#endif
}

void g3d_water_shutdown(void) {
    g3d_fluid_clear();   /* frees per-zone lake meshes */
    if (g_fluid.unit_mesh) { g3d_mesh_free(g_fluid.unit_mesh); g_fluid.unit_mesh = NULL; }
    if (g_water.mesh) {
        g3d_mesh_free(g_water.mesh);
        g_water.mesh = NULL;
    }
    if (g_water.shader) {
        g3d_shader_free(g_water.shader);
        g_water.shader = NULL;
    }
    g_water.enabled = 0;
    g_water.initialized = 0;
}
