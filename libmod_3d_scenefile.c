/*
 * libmod_3d_scenefile.c - Declarative scene/map loader
 *
 * Text format (one command per line, '#' = comment):
 *   SCENE <name>
 *   TERRAIN <grid> <size> <height> <tiling> <seed> <texture|_>
 *   SKY_GRADIENT <tr> <tg> <tb> <hr> <hg> <hb>
 *   SKY_TEXTURE <path>
 *   FOG <0|1> <r> <g> <b> <start> <end>
 *   AMBIENT <r> <g> <b> <intensity>
 *   SHADOWS <0|1>
 *   LIGHT <type> <r> <g> <b> <intensity> <dx> <dy> <dz> <px> <py> <pz> <range> <cone>
 *   SCATTER <model> <count> <area> <target_h> <scale_var> <wind> <seed>
 *   MODEL <model> <x> <y> <z> <yaw> <scale>
 *   PREFAB_FILE <path>
 *   PREFAB_AT <x> <y> <z> <yaw>
 */

#include "libmod_3d_scenefile.h"
#include "libmod_3d_scene.h"
#include "libmod_3d_entity.h"
#include "libmod_3d_material.h"
#include "libmod_3d_texture.h"
#include "libmod_3d_mesh.h"
#include "libmod_3d_primitives.h"
#include "libmod_3d_terrain.h"
#include "libmod_3d_chunkterrain.h"
#include "libmod_3d_gltf.h"
#include "libmod_3d_light.h"
#include "libmod_3d_renderer.h"
#include "libmod_3d_sky.h"
#include "libmod_3d_water.h"
#include "libmod_3d_watersim.h"
#include "libmod_3d_voxterrain.h"
#include "libmod_3d_flow.h"
#include "libmod_3d_instance.h"
#include "libmod_3d_prefab.h"
#include "libmod_3d_math.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define DEG2RAD 0.01745329252f

/* Retained terrain heightfield + water level from the last loaded scene, so a
   game can query the ground height (to walk on it) and the water surface (to
   trigger splashes) without rebuilding the world itself. */
static float *g_scene_H = NULL;
static int    g_scene_side = 0;
static float  g_scene_ws = 0.0f;
static float  g_scene_water = -1e30f;
static int    g_scene_has_water = 0;

/* Bilinear ground height at world (x,z) on the last-loaded scene's heightmap. */
float g3d_scene_terrain_height(float x, float z) {
    /* on voxel terrain, use the live voxel surface (so characters walk the sculpted
       relief); fall back to the heightmap otherwise */
    if (g3d_voxterrain_active()) {
        float s = g3d_voxterrain_surface(x, z);
        if (s > -1e29f) return s;
    }
    if (!g_scene_H || g_scene_side <= 0) return 0.0f;
    return g3d_heightfield_height(g_scene_H, g_scene_side, g_scene_ws, x, z);
}

int g3d_scene_heightfield(const float **H, int *side, float *world_size) {
    if (!g_scene_H || g_scene_side <= 1) return 0;
    if (H) *H = g_scene_H;
    if (side) *side = g_scene_side;
    if (world_size) *world_size = g_scene_ws;
    return 1;
}

/* Register a runtime-built terrain mesh (g3d_primitive_terrain + g3d_terrain_load)
   as the collision heightfield, so rigid bodies and characters rest on the sculpted
   relief. Without this only a scene loaded from disk (g3d_scene_load) had collision.
   Copies the mesh's per-vertex Y (row-major iz*side+ix, matching g3d_heightfield_height).
   Returns 1 on success. */
int g3d_scene_set_terrain_collider(G3DMesh *m) {
    if (!m || m->terrain_side < 2) return 0;
    int side = m->terrain_side;
    long n = (long)side * side;
    if ((long)m->vertex_count < n) return 0;
    float *H = (float *)malloc(n * sizeof(float));
    if (!H) return 0;
    for (long i = 0; i < n; i++) H[i] = m->vertices[i].position[1];
    if (g_scene_H) free(g_scene_H);
    g_scene_H = H;
    g_scene_side = side;
    g_scene_ws = m->terrain_world_size > 0.0f ? m->terrain_world_size : 400.0f;
    return 1;
}

/* Water surface level of the last-loaded scene (first lake / global water), or a
   very negative value if the scene has no water. */
float g3d_scene_water_level(void) {
    return g_scene_has_water ? g_scene_water : -1e30f;
}

/* Player spawn point placed in the editor (SPAWN directive). */
static float g_scene_spawn[3] = {0, 0, 0};
static int   g_scene_has_spawn = 0;
int   g3d_scene_has_spawn(void) { return g_scene_has_spawn; }
float g3d_scene_spawn_x(void) { return g_scene_spawn[0]; }
float g3d_scene_spawn_y(void) { return g_scene_spawn[1]; }
float g3d_scene_spawn_z(void) { return g_scene_spawn[2]; }

/* Player capsule + climb settings (PLAYER directive; sensible defaults). */
static float g_player_radius = 1.0f, g_player_height = 5.0f, g_player_climb = 1.5f;
float g3d_scene_player_radius(void) { return g_player_radius; }
float g3d_scene_player_height(void) { return g_player_height; }
float g3d_scene_player_climb(void)  { return g_player_climb; }

static int make_mat(const char *tex, float r, float g, float b) {
    int m = g3d_material_impl_create();
    g3d_material_impl_set_color(m, r, g, b, 1.0f);
    if (tex && tex[0] && strcmp(tex, "_") != 0) {
        G3DTexture *t = g3d_texture_load_impl(tex);
        if (t) {
            g3d_texture_upload_gpu(t);
            G3DMaterial *mm = g3d_material_impl_get(m);
            if (mm) mm->albedo_texture = t;
        }
    }
    return m;
}

static void place_model(int scene, const char *path, float x, float y, float z,
                        float rx, float ry, float rz, float sx, float sy, float sz) {
    G3DModel *model = g3d_gltf_load(path);
    if (!model) return;
    for (uint32_t s = 0; s < model->mesh_count; s++) {
        int ent = g3d_entity_impl_spawn(scene, 0, x, y, z);
        G3DEntity *e = g3d_entity_impl_get(ent);
        if (!e) continue;
        e->mesh = &model->meshes[s];
        int mat = g3d_material_impl_create();
        g3d_material_impl_set_color(mat, 1.0f, 1.0f, 1.0f, 1.0f);
        if (model->mesh_textures && model->mesh_textures[s]) {
            G3DMaterial *mm = g3d_material_impl_get(mat);
            if (mm) mm->albedo_texture = model->mesh_textures[s];
        }
        e->material_id = mat;
        g3d_entity_impl_set_scale(ent, sx, sy, sz);
        g3d_entity_impl_set_rotation(ent, rx * DEG2RAD, ry * DEG2RAD, rz * DEG2RAD);
    }
}

int g3d_scene_load(const char *file) {
    if (!file) return -1;
    FILE *f = fopen(file, "r");
    if (!f) {
        fprintf(stderr, "G3D: scene file not found: %s\n", file);
        return -1;
    }

    int scene = g3d_scene_impl_create("scene");
    g3d_scene_impl_set_active(scene);
    G3DMesh *terrain = NULL;
    int cur_prefab = -1;
    g3d_fluid_clear();
    g3d_flow_clear();
    g3d_water_clear_ripple_sources();
    g3d_watersim_shutdown();   /* a WATERSIM line re-inits the live water sim */
    g3d_voxterrain_shutdown(); /* a VOXTERRAIN line re-loads the voxel terrain */
    if (g_scene_H) { free(g_scene_H); g_scene_H = NULL; }
    g_scene_side = 0; g_scene_ws = 0.0f;
    g_scene_water = -1e30f; g_scene_has_water = 0;
    g_scene_has_spawn = 0;
    /* Keep the loaded heightfield around so LAKE lines can flood-fill it. */
    float *hm_H = NULL; int hm_side = 0; float hm_ws = 0.0f;
    int *hm_ents = NULL; int hm_nents = 0, hm_mat = 0;   /* chunk entities (to hide if voxel) */
    char vox_path[512] = "", vox_wall[256] = "_";         /* VOXTERRAIN volume + wall texture */
    struct { float x, y, z, r; } vw_spring[256]; int vw_nspring = 0;  /* 3D voxel water springs */
    unsigned char *river_mask = NULL;   /* river cells block lake flood-fill */
    /* Water is built at the END (after the file is read) so we can order it like
       the editor: river corridors block the lakes, lakes render over the rivers,
       and rivers are trimmed/flushed at the lake mouths. */
    struct { float sx, sz, fp, surf, depth, radius; } g_lakes[64]; int g_nlake = 0;
    struct { float width; int n; float *pts; } g_rivers[16]; int g_nriver = 0;
    /* live water sim: params + springs, applied at the end (terrain ready) */
    int   ws_has = 0;
    float ws_rain = 0.0f, ws_sea = -1e30f, ws_evap = 0.04f, ws_flow = 1.0f;
    struct { float x, z, r; } ws_spring[256]; int ws_nspring = 0;

    /* Dynamic line buffer: RIVER lines (hundreds of points) blow past any fixed size. */
    char *line = NULL; size_t line_cap = 0;
    while (getline(&line, &line_cap, f) != -1) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;

        if (strncmp(line, "HEIGHTMAP", 9) == 0) {
            /* HEIGHTMAP <file.g3dh> <texture|_>  -> sculpted terrain saved by the
               editor; rendered as chunks (per-chunk frustum culling). */
            char hfile[256] = "_", tex[256] = "_";
            sscanf(line, "HEIGHTMAP %255s %255s", hfile, tex);
            int hs = 0, hc = 1; float hws = 0.0f, ht = 1.0f;
            float *H = g3d_heightmap_read(hfile, &hs, &hws, &ht, &hc);
            if (H) {
                int mat = make_mat(tex, 1, 1, 1);
                int nc = hc * hc;
                G3DMesh **ms = (G3DMesh **)malloc(sizeof(G3DMesh *) * (size_t)nc);
                free(hm_ents);
                hm_ents = (int *)malloc(sizeof(int) * (size_t)nc);
                g3d_chunkterrain_build(H, hs, hws, hc, ht, scene, mat, ms, hm_ents, NULL);
                hm_nents = nc; hm_mat = mat;
                free(ms);
                terrain = NULL;   /* chunked: no single mesh for SCATTER */
                if (hm_H) free(hm_H);
                hm_H = H; hm_side = hs; hm_ws = hws;   /* keep for LAKE fill */
                free(river_mask);
                river_mask = (unsigned char *)calloc((size_t)hs * hs, 1);
            }
        } else if (strncmp(line, "VOXSPRING", 9) == 0) {
            if (vw_nspring < 256 &&
                sscanf(line, "VOXSPRING %f %f %f %f", &vw_spring[vw_nspring].x, &vw_spring[vw_nspring].y,
                       &vw_spring[vw_nspring].z, &vw_spring[vw_nspring].r) == 4)
                vw_nspring++;
        } else if (strncmp(line, "PLAYER", 6) == 0) {
            sscanf(line, "PLAYER %f %f %f", &g_player_radius, &g_player_height, &g_player_climb);
        } else if (strncmp(line, "SPAWN", 5) == 0) {
            if (sscanf(line, "SPAWN %f %f %f", &g_scene_spawn[0], &g_scene_spawn[1], &g_scene_spawn[2]) == 3)
                g_scene_has_spawn = 1;
        } else if (strncmp(line, "VOXTERRAIN", 10) == 0) {
            sscanf(line, "VOXTERRAIN %511s %255s", vox_path, vox_wall);   /* loaded at the end */
        } else if (strncmp(line, "TERRAIN", 7) == 0) {
            int grid, seed; float size, height, tiling; char tex[256] = "_";
            sscanf(line, "TERRAIN %d %f %f %f %d %255s", &grid, &size, &height,
                   &tiling, &seed, tex);
            terrain = g3d_primitive_create_terrain(grid, size, height, tiling,
                                                   (unsigned)seed);
            if (terrain) {
                g3d_mesh_upload_gpu(terrain);
                int ent = g3d_entity_impl_spawn(scene, 0, 0, 0, 0);
                G3DEntity *e = g3d_entity_impl_get(ent);
                if (e) { e->mesh = terrain; e->material_id = make_mat(tex, 1, 1, 1); }
            }
        } else if (strncmp(line, "SKY_GRADIENT", 12) == 0) {
            float tr, tg, tb, hr, hg, hb;
            sscanf(line, "SKY_GRADIENT %f %f %f %f %f %f", &tr, &tg, &tb, &hr, &hg, &hb);
            g3d_sky_set_gradient(tr, tg, tb, hr, hg, hb);
        } else if (strncmp(line, "SKY_TEXTURE", 11) == 0) {
            char path[256];
            if (sscanf(line, "SKY_TEXTURE %255s", path) == 1) {
                G3DTexture *t = g3d_texture_load_impl(path);
                if (t) { g3d_texture_upload_gpu(t); g3d_sky_set_texture(t->gl_handle); }
            }
        } else if (strncmp(line, "FOG", 3) == 0) {
            int en; float r, g, b, s, e;
            sscanf(line, "FOG %d %f %f %f %f %f", &en, &r, &g, &b, &s, &e);
            g3d_renderer_set_fog(en, vec3_make(r, g, b), s, e);
        } else if (strncmp(line, "AMBIENT", 7) == 0) {
            float r, g, b, in;
            sscanf(line, "AMBIENT %f %f %f %f", &r, &g, &b, &in);
            g3d_renderer_set_ambient_light(vec3_make(r, g, b), in);
        } else if (strncmp(line, "WATERSIM", 8) == 0) {
            /* WATERSIM rain seaOn sea evap flow -> live height-field water sim */
            float rain, sea, evap, flow; int seaOn;
            if (sscanf(line, "WATERSIM %f %d %f %f %f", &rain, &seaOn, &sea, &evap, &flow) == 5) {
                ws_has = 1; ws_rain = rain; ws_evap = evap; ws_flow = flow;
                ws_sea = seaOn ? sea : -1e30f;
            }
        } else if (strncmp(line, "WATERSPRING", 11) == 0) {
            /* WATERSPRING x z rate -> a spring for the water sim */
            float x, z, r;
            if (sscanf(line, "WATERSPRING %f %f %f", &x, &z, &r) == 3 && ws_nspring < 256) {
                ws_spring[ws_nspring].x = x; ws_spring[ws_nspring].z = z;
                ws_spring[ws_nspring].r = r; ws_nspring++; ws_has = 1;
            }
        } else if (strncmp(line, "WATER", 5) == 0) {
            /* WATER level size subdiv amp wavelen speed dr dg db sr sg sb refl reflStrength */
            float level, size, amp, wl, sp, dr, dg, db, sr, sg, sb, rstr;
            int subdiv, refl;
            int n = sscanf(line, "WATER %f %f %d %f %f %f %f %f %f %f %f %f %d %f",
                           &level, &size, &subdiv, &amp, &wl, &sp,
                           &dr, &dg, &db, &sr, &sg, &sb, &refl, &rstr);
            if (n >= 6) {
                g3d_water_create(level, size, subdiv);
                g3d_water_set_waves(amp, wl, sp);
                if (n >= 12) g3d_water_set_color(dr, dg, db, sr, sg, sb);
                if (n >= 14) g3d_water_set_reflection(refl, rstr);
                g3d_water_set_enabled(1);
                g_scene_water = level; g_scene_has_water = 1;
            }
        } else if (strncmp(line, "LAKE", 4) == 0) {
            /* LAKE seedX seedZ footprintLevel surfaceY depth -> deferred */
            float sx, sz, fp, surf, depth, radius = 0.0f;
            /* LAKE seedX seedZ footprintLevel surfaceY depth [radius]  (radius>0 caps the fill) */
            if (sscanf(line, "LAKE %f %f %f %f %f %f", &sx, &sz, &fp, &surf, &depth, &radius) >= 5 && g_nlake < 64) {
                g_lakes[g_nlake].sx = sx; g_lakes[g_nlake].sz = sz; g_lakes[g_nlake].fp = fp;
                g_lakes[g_nlake].surf = surf; g_lakes[g_nlake].depth = depth;
                g_lakes[g_nlake].radius = radius; g_nlake++;
            }
        } else if (strncmp(line, "RIVER", 5) == 0) {
            /* RIVER width speed tiling n x0 y0 z0 ... -> deferred */
            float width, speed, tiling; int npts, consumed = 0;
            if (sscanf(line, "RIVER %f %f %f %d%n", &width, &speed, &tiling, &npts, &consumed) == 4
                && npts >= 2 && npts <= 256 && g_nriver < 16) {
                float *pts = (float *)malloc((size_t)npts * 3 * sizeof(float));
                const char *cur = line + consumed;
                int ok = 1;
                for (int i = 0; i < npts * 3; i++) {
                    int adv = 0;
                    if (sscanf(cur, " %f%n", &pts[i], &adv) != 1) { ok = 0; break; }
                    cur += adv;
                }
                if (ok) { g_rivers[g_nriver].width = width; g_rivers[g_nriver].n = npts;
                          g_rivers[g_nriver].pts = pts; g_nriver++; }
                else free(pts);
            }
            (void)speed; (void)tiling;
        } else if (strncmp(line, "FLUID_STYLE", 11) == 0) {
            float amp, len, sp, dr, dg, db, sr, sg, sb, op = 0.6f, rip = 1.0f; char tex[256] = "_";
            int kind = 0;
            /* new: ... sb opacity ripple tex [kind]  (kind 0=water, 1=lava) */
            int n = sscanf(line, "FLUID_STYLE %f %f %f %f %f %f %f %f %f %f %f %255s %d",
                           &amp, &len, &sp, &dr, &dg, &db, &sr, &sg, &sb, &op, &rip, tex, &kind);
            if (n < 12) {   /* ... opacity tex (no ripple) */
                rip = 1.0f; tex[0] = '_'; tex[1] = 0;
                n = sscanf(line, "FLUID_STYLE %f %f %f %f %f %f %f %f %f %f %255s",
                           &amp, &len, &sp, &dr, &dg, &db, &sr, &sg, &sb, &op, tex);
            }
            if (n < 11) {   /* legacy: ... sb tex */
                op = 0.6f; rip = 1.0f; tex[0] = '_'; tex[1] = 0;
                sscanf(line, "FLUID_STYLE %f %f %f %f %f %f %f %f %f %255s",
                       &amp, &len, &sp, &dr, &dg, &db, &sr, &sg, &sb, tex);
            }
            unsigned int h = 0;
            if (strcmp(tex, "_") != 0) {
                G3DTexture *t = g3d_texture_load_impl(tex);
                if (t) { g3d_texture_upload_gpu(t); h = t->gl_handle; }
            }
            g3d_fluid_set_style(amp, len, sp, dr, dg, db, sr, sg, sb, h, op);
            g3d_fluid_set_kind(kind);
            g3d_water_set_ripple_strength(rip);
            /* rivers (flow) share the fluid look */
            g3d_flow_set_texture(h);
            g3d_flow_set_color(dr, dg, db);
        } else if (strncmp(line, "FLUID", 5) == 0) {
            float cx, cz, sx, sz, level, depth;
            if (sscanf(line, "FLUID %f %f %f %f %f %f", &cx, &cz, &sx, &sz, &level, &depth) == 6)
                g3d_fluid_add(cx, cz, sx, sz, level, depth);
        } else if (strncmp(line, "SHADOWS", 7) == 0) {
            int en; sscanf(line, "SHADOWS %d", &en);
            g3d_renderer_set_shadows(en);
        } else if (strncmp(line, "LIGHT", 5) == 0) {
            int type; float r, g, b, in, dx, dy, dz, px, py, pz, range, cone;
            sscanf(line, "LIGHT %d %f %f %f %f %f %f %f %f %f %f %f %f", &type,
                   &r, &g, &b, &in, &dx, &dy, &dz, &px, &py, &pz, &range, &cone);
            int id = g3d_light_impl_create(type, r, g, b);
            g3d_light_impl_set_intensity(id, in);
            g3d_light_impl_set_direction(id, dx, dy, dz);
            g3d_light_impl_set_position(id, px, py, pz);
            g3d_light_impl_set_range(id, range);
            g3d_light_impl_set_cone(id, cone);
        } else if (strncmp(line, "SCATTER", 7) == 0) {
            char path[256]; int count, seed; float area, th, sv, wind;
            sscanf(line, "SCATTER %255s %d %f %f %f %f %d", path, &count, &area,
                   &th, &sv, &wind, &seed);
            G3DModel *m = g3d_gltf_load(path);
            if (m && terrain) g3d_scatter_model(m, terrain, count, area, th, sv, wind, (unsigned)seed);
        } else if (strncmp(line, "MODEL", 5) == 0) {
            char path[256]; float x, y, z, rx = 0, ry = 0, rz = 0, sx = 1, sy = 1, sz = 1;
            int n = sscanf(line, "MODEL %255s %f %f %f %f %f %f %f %f %f", path,
                           &x, &y, &z, &rx, &ry, &rz, &sx, &sy, &sz);
            if (n == 6) { /* legacy: MODEL path x y z yaw scale */
                ry = rx; float sc = ry; (void)sc;
                /* reparse legacy */
                float yaw, scl;
                sscanf(line, "MODEL %255s %f %f %f %f %f", path, &x, &y, &z, &yaw, &scl);
                rx = 0; ry = yaw; rz = 0; sx = sy = sz = scl;
            }
            place_model(scene, path, x, y, z, rx, ry, rz, sx, sy, sz);
        } else if (strncmp(line, "PREFAB_FILE", 11) == 0) {
            char path[256];
            if (sscanf(line, "PREFAB_FILE %255s", path) == 1)
                cur_prefab = g3d_prefab_load(path);
        } else if (strncmp(line, "PREFAB_AT", 9) == 0) {
            float x, y, z, yaw;
            sscanf(line, "PREFAB_AT %f %f %f %f", &x, &y, &z, &yaw);
            if (cur_prefab >= 0) g3d_prefab_instantiate(cur_prefab, scene, x, y, z, yaw);
        }
    }
    free(line);
    /* ---- Build the water in editor order (river corridors block lakes; lakes
       render over rivers; rivers trimmed/flushed at the lake mouths). ---- */
    if (hm_H && (g_nlake > 0 || g_nriver > 0)) {
        int side = hm_side, grid = side - 1, NN = side * side;
        float wsz = hm_ws;
        unsigned char *lmask = (unsigned char *)calloc((size_t)NN, 1);
        float *llevel = (float *)malloc((size_t)NN * sizeof(float));
        for (int k = 0; k < NN; k++) llevel[k] = -1e30f;

        /* lakes flood naturally (up the riverbed to the mouth), hold meshes, mask+level */
        G3DMesh *lakemesh[64]; float lakedepth[64]; int nlm = 0;
        for (int l = 0; l < g_nlake; l++) {
            unsigned char *one = (unsigned char *)calloc((size_t)NN, 1);
            float d = g_lakes[l].depth;
            G3DMesh *lm = g3d_fluid_build_lake(hm_H, side, wsz, g_lakes[l].sx, g_lakes[l].sz,
                                               g_lakes[l].fp, g_lakes[l].surf, NULL, one, &d,
                                               g_lakes[l].radius);
            if (lm) { lakemesh[nlm] = lm; lakedepth[nlm] = d; nlm++; }
            for (int k = 0; k < NN; k++) if (one[k]) { lmask[k] = 1; llevel[k] = g_lakes[l].surf; }
            free(one);
        }
        /* rivers joined to lakes WITHOUT overlapping them (overlapping two transparent
           water surfaces stacks alpha -> ugly patch). Draw only the DRY span; ease each
           end to the touching lake's level so it meets the shore flush. Matches the
           editor's rebuildFluids(). */
        for (int r = 0; r < g_nriver; r++) {
            int npts = g_rivers[r].n; float *rp = g_rivers[r].pts;
            #define LAKE_Y_AT(i) ({ int ci = (int)lrintf((rp[(i) * 3] / wsz + 0.5f) * grid); \
                int cj = (int)lrintf((rp[(i) * 3 + 2] / wsz + 0.5f) * grid); \
                if (ci < 0) ci = 0; if (cj < 0) cj = 0; if (ci > grid) ci = grid; if (cj > grid) cj = grid; \
                lmask[cj * side + ci] ? llevel[cj * side + ci] : -1e30f; })
            int s = 0;        while (s < npts && LAKE_Y_AT(s) > -1e29f) s++;
            int e = npts - 1; while (e >= 0 && LAKE_Y_AT(e) > -1e29f) e--;
            if (s > e) continue;   /* fully under a lake -> the lake covers it */
            int m = e - s + 1;
            float *pts = (float *)malloc((size_t)m * 3 * sizeof(float));
            memcpy(pts, rp + s * 3, (size_t)m * 3 * sizeof(float));
            if (s > 0) {           /* emerges from a lake */
                float lakeY = LAKE_Y_AT(s - 1);
                g3d_water_add_ripple_source(pts[0], pts[2], 0.9f);
                int bn = m < 6 ? m : 6;
                for (int i = 0; i < bn; i++) {
                    float t = (bn > 1) ? (float)i / (bn - 1) : 1.0f;
                    pts[i * 3 + 1] = lakeY * (1.0f - t) + pts[i * 3 + 1] * t;
                }
            }
            if (e < npts - 1) {    /* enters a lake */
                float lakeY = LAKE_Y_AT(e + 1);
                g3d_water_add_ripple_source(pts[(m - 1) * 3], pts[(m - 1) * 3 + 2], 0.9f);
                int bn = m < 6 ? m : 6;
                for (int i = m - bn; i < m; i++) {
                    float t = (bn > 1) ? (float)(i - (m - bn)) / (bn - 1) : 1.0f;
                    pts[i * 3 + 1] = pts[i * 3 + 1] * (1.0f - t) + lakeY * t;
                }
            }
            #undef LAKE_Y_AT
            float d = 0.0f;
            G3DMesh *rm = g3d_fluid_build_river(pts, m, hm_H, side, wsz, g_rivers[r].width, &d);
            if (rm) g3d_fluid_add_mesh(rm, d);
            free(pts);
        }
        /* lakes last -> render over the rivers */
        for (int i = 0; i < nlm; i++) g3d_fluid_add_mesh(lakemesh[i], lakedepth[i]);
        free(lmask); free(llevel);
    }
    /* expose the first lake's surface as the queryable water level (a global
       WATER line, if any, already set it above) */
    if (!g_scene_has_water && g_nlake > 0) {
        g_scene_water = g_lakes[0].surf; g_scene_has_water = 1;
    }
    for (int r = 0; r < g_nriver; r++) free(g_rivers[r].pts);

    /* Full voxel terrain: load the density volume and hide the heightmap chunks so
       the voxel terrain (caves/overhangs) replaces them. Done BEFORE the water so
       the water can run on the voxel surface. */
    if (vox_path[0]) {
        g3d_material_impl_set_triplanar(hm_mat, 1);   // grass on flats, rock on cave walls
        if (strcmp(vox_wall, "_") != 0) {
            G3DTexture *wt = g3d_texture_load_impl(vox_wall);
            if (wt) { g3d_texture_upload_gpu(wt); G3DMaterial *mm = g3d_material_impl_get(hm_mat); if (mm) mm->wall_texture = wt; }
        }
        if (g3d_voxterrain_load(vox_path, scene, hm_mat)) {
            for (int i = 0; i < hm_nents; i++) {
                G3DEntity *e = g3d_entity_impl_get(hm_ents[i]);
                if (e) e->active = 0;
            }
            /* 3D voxel water: add the springs + settle so caves are filled at load */
            for (int i = 0; i < vw_nspring; i++)
                g3d_voxwater_add_source(vw_spring[i].x, vw_spring[i].y, vw_spring[i].z, vw_spring[i].r);
            if (vw_nspring > 0) g3d_voxwater_settle(20.0f);
        }
    }
    free(hm_ents);

    /* Live height-field water sim (unified lakes+rivers): init from the terrain
       (voxel surface if present, else the heightmap), add springs + params, settle
       so the game shows the water already filled. It then flows live. */
    if (ws_has && (hm_H || g3d_voxterrain_active())) {
        float *wh = hm_H; int wside = hm_side; float wws = hm_ws;
        float *vh = NULL;
        if (g3d_voxterrain_active()) {
            wside = g3d_voxterrain_side(); wws = hm_ws;
            vh = (float *)malloc((size_t)wside * wside * sizeof(float));
            g3d_voxterrain_export_heights(vh);
            wh = vh;
        }
        if (wh) {
            g3d_watersim_init(wh, wside, wws);
            g3d_watersim_set_rain(ws_rain);
            g3d_watersim_set_sea_level(ws_sea);
            g3d_watersim_set_evaporation(ws_evap);
            g3d_watersim_set_flow_scale(ws_flow);
            for (int i = 0; i < ws_nspring; i++)
                g3d_watersim_add_source(ws_spring[i].x, ws_spring[i].z, ws_spring[i].r);
            g3d_watersim_settle(30.0f);
        }
        free(vh);
    }

    /* retain the heightfield so the game can sample ground height to walk on it */
    if (hm_H) { g_scene_H = hm_H; g_scene_side = hm_side; g_scene_ws = hm_ws; }
    free(river_mask);
    fclose(f);
    fprintf(stderr, "G3D: scene loaded: %s\n", file);
    return scene;
}
