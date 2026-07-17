/*
 * libmod_3d_fbx.c - FBX loader via ufbx. Fills the same generic G3DModel skeleton /
 * skin / animation structs as the glTF loader, so anim.c plays FBX the same way.
 * Designed for Mixamo-style exports (one skinned mesh + skeleton + animation stacks).
 */
#include "libmod_3d_fbx.h"
#include "libmod_3d_texture.h"
#include "ufbx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>

static Mat4 mat_from_ufbx(ufbx_matrix m) {
    Mat4 r;
    r.m[0]=(float)m.cols[0].x; r.m[1]=(float)m.cols[0].y; r.m[2]=(float)m.cols[0].z; r.m[3]=0.0f;
    r.m[4]=(float)m.cols[1].x; r.m[5]=(float)m.cols[1].y; r.m[6]=(float)m.cols[1].z; r.m[7]=0.0f;
    r.m[8]=(float)m.cols[2].x; r.m[9]=(float)m.cols[2].y; r.m[10]=(float)m.cols[2].z; r.m[11]=0.0f;
    r.m[12]=(float)m.cols[3].x; r.m[13]=(float)m.cols[3].y; r.m[14]=(float)m.cols[3].z; r.m[15]=1.0f;
    return r;
}
static Vec3 v3f(ufbx_vec3 v) { return vec3_make((float)v.x, (float)v.y, (float)v.z); }
static Quat q4f(ufbx_quat q) { Quat r; r.x=(float)q.x; r.y=(float)q.y; r.z=(float)q.z; r.w=(float)q.w; return r; }

/* Recentrado del modelo al cargar (ON = comportamiento de siempre). Desactivalo
   con g3d_fbx_set_recenter(0) para mallas con esqueleto cuya pose BIND no
   representa la pose real: ahi el AABB de la bind desplaza el modelo respecto a
   donde lo coloca el juego. */
static int g_fbx_recenter = 1;
void g3d_fbx_set_recenter(int on) { g_fbx_recenter = on ? 1 : 0; }

static void dir_of(const char *path, char *out, size_t n) {
    strncpy(out, path, n - 1); out[n - 1] = 0;
    char *s = strrchr(out, '/'); if (s) s[1] = 0; else out[0] = 0;
}
static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) { fclose(f); return 1; } return 0; }
/* load an image file AND upload it to the GPU (glTF loader does this; otherwise no GL handle). */
static G3DTexture *load_upload(const char *p) { G3DTexture *t = g3d_texture_load_impl(p); if (t) g3d_texture_upload_gpu(t); return t; }
static const char *base_name(const char *p) {
    const char *s = strrchr(p, '/'); const char *b = strrchr(p, '\\');
    if (b > s) s = b;
    return s ? s + 1 : p;
}
/* case-insensitive substring */
static int has_ci(const char *hay, const char *needle) {
    if (!hay || !needle) return 0;
    size_t nl = strlen(needle);
    for (const char *p = hay; *p; p++) {
        size_t i = 0;
        while (i < nl && p[i] && tolower((unsigned char)p[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == nl) return 1;
    }
    return 0;
}
/* FBX texture paths are often wrong on disk; search common spots by base name. */
static G3DTexture *fbx_find_texture(const char *dir, ufbx_texture *tex) {
    if (!tex) return NULL;
    char cand[600];
    if (tex->filename.length && file_exists(tex->filename.data)) return load_upload(tex->filename.data);
    if (tex->relative_filename.length) {
        snprintf(cand, sizeof(cand), "%s%s", dir, tex->relative_filename.data);
        if (file_exists(cand)) return load_upload(cand);
    }
    const char *bn = NULL;
    if (tex->relative_filename.length) bn = base_name(tex->relative_filename.data);
    else if (tex->filename.length) bn = base_name(tex->filename.data);
    if (!bn) return NULL;
    /* name without extension (FBX often says .jpg but the file is .jpeg/.png/etc.) */
    char stem[400]; strncpy(stem, bn, sizeof(stem) - 1); stem[sizeof(stem) - 1] = 0;
    char *dot = strrchr(stem, '.'); if (dot) *dot = 0;
    const char *subs[] = { "", "textures/", "../textures/", "Textures/", "../Textures/", "tex/", "../tex/" };
    const char *exts[] = { ".jpg", ".jpeg", ".png", ".tga", ".bmp", ".tga.png" };
    for (int s = 0; s < 7; s++) {
        snprintf(cand, sizeof(cand), "%s%s%s", dir, subs[s], bn);          /* exact name first */
        if (file_exists(cand)) return load_upload(cand);
        for (int e = 0; e < 6; e++) {                                      /* then swap extension */
            snprintf(cand, sizeof(cand), "%s%s%s%s", dir, subs[s], stem, exts[e]);
            if (file_exists(cand)) return load_upload(cand);
        }
    }
    return NULL;
}
/* Last-resort: some FBX exports drop ALL texture links (materials carry only a
   name like "Head"/"Body"). Scan the asset folder (and its textures/ subdir) for
   a file whose name contains the MATERIAL name and a keyword for this map kind. */
static G3DTexture *fbx_find_by_matname(const char *dir, const char *matname, int kind) {
    if (!matname || !matname[0]) return NULL;
    /* keywords per kind, strongest first */
    const char *kw[4];
    if (kind == 0)      { kw[0]="basecolor"; kw[1]="albedo"; kw[2]="diffuse"; kw[3]="_d"; }
    else if (kind == 1) { kw[0]="normal"; kw[1]="_n"; kw[2]="nrm"; kw[3]="bump"; }
    else if (kind == 2) { kw[0]="metallic"; kw[1]="metalness"; kw[2]="metal"; kw[3]="_m"; }
    else                { kw[0]="roughness"; kw[1]="rough"; kw[2]="gloss"; kw[3]="_r"; }
    const char *subs[] = { "", "textures/", "Textures/", "tex/" };
    char folder[600], path[900];
    for (int s = 0; s < 4; s++) {
        snprintf(folder, sizeof(folder), "%s%s", dir, subs[s]);
        for (int k = 0; k < 4; k++) {                 /* strongest keyword wins */
            DIR *dp = opendir(folder[0] ? folder : ".");
            if (!dp) continue;
            struct dirent *de;
            while ((de = readdir(dp))) {
                if (has_ci(de->d_name, matname) && has_ci(de->d_name, kw[k])) {
                    snprintf(path, sizeof(path), "%s%s", folder, de->d_name);
                    closedir(dp);
                    return load_upload(path);
                }
            }
            closedir(dp);
        }
    }
    return NULL;
}

/* Pick a PBR map of a material by kind (0=base,1=normal,2=metallic,3=roughness):
   direct ufbx field, else scan the texture list by property/filename keyword. */
static G3DTexture *fbx_map(const char *dir, ufbx_material *mat, int kind) {
    if (!mat) return NULL;
    ufbx_texture *tex = NULL;
    const char *k1 = "", *k2 = "", *k3 = "";
    if (kind == 0)      { tex = mat->pbr.base_color.texture; if (!tex) tex = mat->fbx.diffuse_color.texture; k1="base"; k2="albedo"; k3="diffuse"; }
    else if (kind == 1) { tex = mat->pbr.normal_map.texture; if (!tex) tex = mat->fbx.normal_map.texture; if (!tex) tex = mat->fbx.bump.texture; k1="normal"; k2="nrm"; k3="bump"; }
    else if (kind == 2) { tex = mat->pbr.metalness.texture; k1="metal"; k2="metalness"; k3="metallic"; }
    else if (kind == 3) { tex = mat->pbr.roughness.texture; k1="rough"; k2="roughness"; k3="gloss"; }
    if (!tex) {
        for (size_t i = 0; i < mat->textures.count; i++) {
            ufbx_material_texture *mt = &mat->textures.data[i];
            const char *pn = mt->material_prop.data ? mt->material_prop.data : "";
            const char *fn = mt->texture && mt->texture->relative_filename.length ? mt->texture->relative_filename.data : "";
            if (has_ci(pn, k1) || has_ci(pn, k2) || has_ci(fn, k1) || has_ci(fn, k2) || has_ci(fn, k3)) { tex = mt->texture; break; }
        }
        if (kind == 0 && !tex && mat->textures.count) tex = mat->textures.data[0].texture;
    }
    G3DTexture *t = fbx_find_texture(dir, tex);
    /* FBX with no texture links at all: match loose files by material name. */
    if (!t && mat->name.length) t = fbx_find_by_matname(dir, mat->name.data, kind);
    return t;
}

G3DModel *g3d_fbx_load(const char *filepath) {
    if (!filepath) return NULL;

    ufbx_load_opts opts;
    memset(&opts, 0, sizeof(opts));
    opts.target_axes = ufbx_axes_right_handed_y_up;
    opts.target_unit_meters = 1.0f;
    opts.generate_missing_normals = true;
    ufbx_error err;
    ufbx_scene *scene = ufbx_load_file(filepath, &opts, &err);
    if (!scene) { fprintf(stderr, "G3D: FBX load failed: %s (%s)\n", filepath, err.description.data); return NULL; }

    G3DModel *model = (G3DModel *)calloc(1, sizeof(G3DModel));

    /* ---- node hierarchy ---- */
    int nc = (int)scene->nodes.count;
    model->node_count = nc;
    model->node_parent = (int *)malloc(nc * sizeof(int));
    model->node_base_t = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_base_r = (Quat *)malloc(nc * sizeof(Quat));
    model->node_base_s = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_cur_t = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_cur_r = (Quat *)malloc(nc * sizeof(Quat));
    model->node_cur_s = (Vec3 *)malloc(nc * sizeof(Vec3));
    model->node_global = (Mat4 *)malloc(nc * sizeof(Mat4));
    for (int i = 0; i < nc; i++) {
        ufbx_node *n = scene->nodes.data[i];
        model->node_parent[i] = n->parent ? (int)n->parent->typed_id : -1;
        model->node_base_t[i] = v3f(n->local_transform.translation);
        model->node_base_r[i] = q4f(n->local_transform.rotation);
        model->node_base_s[i] = v3f(n->local_transform.scale);
        model->node_cur_t[i] = model->node_base_t[i];
        model->node_cur_r[i] = model->node_base_r[i];
        model->node_cur_s[i] = model->node_base_s[i];
    }

    /* ---- joints: union of all skin clusters' bones (dedup by node) ---- */
    int *node_to_joint = (int *)malloc(nc * sizeof(int));
    for (int i = 0; i < nc; i++) node_to_joint[i] = -1;
    int jcap = 128, jc = 0;
    int *joint_node = (int *)malloc(jcap * sizeof(int));
    Mat4 *inv_bind = (Mat4 *)malloc(jcap * sizeof(Mat4));
    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        ufbx_mesh *mesh = scene->meshes.data[mi];
        for (size_t di = 0; di < mesh->skin_deformers.count; di++) {
            ufbx_skin_deformer *sk = mesh->skin_deformers.data[di];
            for (size_t ci = 0; ci < sk->clusters.count; ci++) {
                ufbx_skin_cluster *cl = sk->clusters.data[ci];
                if (!cl->bone_node) continue;
                int bn = (int)cl->bone_node->typed_id;
                if (node_to_joint[bn] >= 0) continue;
                if (jc >= jcap) { jcap *= 2; joint_node = realloc(joint_node, jcap*sizeof(int)); inv_bind = realloc(inv_bind, jcap*sizeof(Mat4)); }
                node_to_joint[bn] = jc;
                joint_node[jc] = bn;
                inv_bind[jc] = mat_from_ufbx(cl->geometry_to_bone);
                jc++;
            }
        }
    }
    model->joint_count = jc;
    model->joint_node = (int *)malloc((jc > 0 ? jc : 1) * sizeof(int));
    model->inverse_bind = (Mat4 *)malloc((jc > 0 ? jc : 1) * sizeof(Mat4));
    model->joint_matrix = (Mat4 *)malloc((jc > 0 ? jc : 1) * sizeof(Mat4));
    for (int j = 0; j < jc; j++) { model->joint_node[j] = joint_node[j]; model->inverse_bind[j] = inv_bind[j]; }
    free(joint_node); free(inv_bind);
    model->root_node = jc > 0 ? model->joint_node[0] : -1;

    /* ---- meshes -> one submesh per (mesh, material) ---- */
    int scap = 16, scount = 0;
    G3DMesh *meshes = (G3DMesh *)calloc(scap, sizeof(G3DMesh));
    void **textures = (void **)calloc(scap, sizeof(void *));
    void **normals = (void **)calloc(scap, sizeof(void *));
    void **metals = (void **)calloc(scap, sizeof(void *));
    void **roughs = (void **)calloc(scap, sizeof(void *));
    char dir[256]; dir_of(filepath, dir, sizeof(dir));
    int any_skin = 0;

    for (size_t mi = 0; mi < scene->meshes.count; mi++) {
        ufbx_mesh *mesh = scene->meshes.data[mi];
        ufbx_skin_deformer *sk = mesh->skin_deformers.count ? mesh->skin_deformers.data[0] : NULL;
        ufbx_node *inst = mesh->instances.count ? mesh->instances.data[0] : NULL;
        ufbx_matrix g2w = inst ? inst->geometry_to_world : ufbx_identity_matrix;
        ufbx_matrix g2wN = ufbx_matrix_for_normals(&g2w);

        int nmat = (int)mesh->materials.count; if (nmat < 1) nmat = 1;
        for (int matg = 0; matg < nmat; matg++) {
            G3DVertex *verts = NULL; int vn = 0, vcap = 0;
            uint16_t *vj = NULL; float *vw = NULL;   /* skin per emitted vertex */
            uint32_t tri[64];
            for (size_t fi = 0; fi < mesh->faces.count; fi++) {
                if ((int)mesh->materials.count > 0 && (int)mesh->face_material.data[fi] != matg) continue;
                ufbx_face face = mesh->faces.data[fi];
                uint32_t ntri = ufbx_triangulate_face(tri, 64, mesh, face);
                for (uint32_t t = 0; t < ntri * 3; t++) {
                    uint32_t ix = tri[t];   /* corner (index) */
                    ufbx_vec3 p = ufbx_get_vertex_vec3(&mesh->vertex_position, ix);
                    ufbx_vec3 nn = mesh->vertex_normal.exists ? ufbx_get_vertex_vec3(&mesh->vertex_normal, ix) : (ufbx_vec3){0,1,0};
                    ufbx_vec2 uv = mesh->vertex_uv.exists ? ufbx_get_vertex_vec2(&mesh->vertex_uv, ix) : (ufbx_vec2){0,0};
                    G3DVertex v; memset(&v, 0, sizeof(v));
                    if (sk) {  /* skinned: keep geometry space (skin matrices handle placement) */
                        v.position[0]=(float)p.x; v.position[1]=(float)p.y; v.position[2]=(float)p.z;
                        v.normal[0]=(float)nn.x; v.normal[1]=(float)nn.y; v.normal[2]=(float)nn.z;
                    } else {   /* static: bake the node transform into world */
                        ufbx_vec3 pw = ufbx_transform_position(&g2w, p);
                        ufbx_vec3 nw = ufbx_transform_direction(&g2wN, nn);
                        v.position[0]=(float)pw.x; v.position[1]=(float)pw.y; v.position[2]=(float)pw.z;
                        v.normal[0]=(float)nw.x; v.normal[1]=(float)nw.y; v.normal[2]=(float)nw.z;
                    }
                    v.texcoord[0]=(float)uv.x; v.texcoord[1]=1.0f-(float)uv.y;   /* FBX V is bottom-left; engine samples top-left */
                    if (vn >= vcap) { vcap = vcap ? vcap*2 : 512;
                        verts = realloc(verts, (size_t)vcap*sizeof(G3DVertex));
                        if (sk) { vj = realloc(vj, (size_t)vcap*4*sizeof(uint16_t)); vw = realloc(vw, (size_t)vcap*4*sizeof(float)); } }
                    if (sk) {
                        uint32_t vtx = mesh->vertex_indices.data[ix];   /* control point */
                        ufbx_skin_vertex sv = sk->vertices.data[vtx];
                        float w4[4]={0,0,0,0}; int j4[4]={0,0,0,0};
                        for (uint32_t w = 0; w < sv.num_weights && w < 32; w++) {
                            ufbx_skin_weight sw = sk->weights.data[sv.weight_begin + w];
                            ufbx_skin_cluster *cl = sk->clusters.data[sw.cluster_index];
                            int jidx = cl->bone_node ? node_to_joint[cl->bone_node->typed_id] : -1;
                            if (jidx < 0) continue;
                            /* keep the 4 largest weights */
                            int slot = -1; float wmin = (float)sw.weight;
                            for (int s = 0; s < 4; s++) if (w4[s] < wmin) { wmin = w4[s]; slot = s; }
                            if (slot >= 0) { w4[slot]=(float)sw.weight; j4[slot]=jidx; }
                        }
                        float sum = w4[0]+w4[1]+w4[2]+w4[3]; if (sum < 1e-6f) { w4[0]=1.0f; sum=1.0f; }
                        for (int s=0;s<4;s++){ vj[vn*4+s]=(uint16_t)j4[s]; vw[vn*4+s]=w4[s]/sum; }
                    }
                    verts[vn++] = v;
                }
            }
            if (vn < 3) { free(verts); free(vj); free(vw); continue; }
            uint32_t *idx = (uint32_t *)malloc((size_t)vn*sizeof(uint32_t));
            for (int k=0;k<vn;k++) idx[k]=(uint32_t)k;
            G3DMesh *m = g3d_mesh_create("fbxsub", verts, (uint32_t)vn, idx, (uint32_t)vn);
            free(idx); free(verts);
            if (!m) { free(vj); free(vw); continue; }
            if (sk) {
                m->skinned = 1; any_skin = 1;
                m->bind_pos = (float *)malloc((size_t)m->vertex_count*3*sizeof(float));
                m->bind_nrm = (float *)malloc((size_t)m->vertex_count*3*sizeof(float));
                for (uint32_t k=0;k<m->vertex_count;k++){
                    m->bind_pos[k*3]=m->vertices[k].position[0]; m->bind_pos[k*3+1]=m->vertices[k].position[1]; m->bind_pos[k*3+2]=m->vertices[k].position[2];
                    m->bind_nrm[k*3]=m->vertices[k].normal[0]; m->bind_nrm[k*3+1]=m->vertices[k].normal[1]; m->bind_nrm[k*3+2]=m->vertices[k].normal[2];
                }
                m->vjoints = vj; m->vweights = vw;
            } else { free(vj); free(vw); }
            if (scount >= scap) { scap*=2; meshes=realloc(meshes,scap*sizeof(G3DMesh));
                textures=realloc(textures,scap*sizeof(void*)); normals=realloc(normals,scap*sizeof(void*));
                metals=realloc(metals,scap*sizeof(void*)); roughs=realloc(roughs,scap*sizeof(void*)); }
            meshes[scount] = *m; free(m);   /* upload happens after recentering */
            textures[scount] = normals[scount] = metals[scount] = roughs[scount] = NULL;
            if ((int)mesh->materials.count > matg) {
                ufbx_material *mat = mesh->materials.data[matg];
                textures[scount] = fbx_map(dir, mat, 0);
                normals[scount]  = fbx_map(dir, mat, 1);
                metals[scount]   = fbx_map(dir, mat, 2);
                roughs[scount]   = fbx_map(dir, mat, 3);
            }
            scount++;
        }
    }
    free(node_to_joint);

    if (scount == 0) { fprintf(stderr, "G3D: FBX has no geometry: %s\n", filepath); ufbx_free_scene(scene); free(meshes); free(textures); free(model); return NULL; }

    /* Recenter: rest the bottom on Y=0 and centre on X/Z (like the glTF loader), so
       placing at terrain height sits the model ON the ground instead of half-buried.
       CUIDADO con mallas SKINNED: esto mide la pose BIND, que en algunos FBX no es
       la pose real (p.ej. lara_animada.fbx tiene la bind TUMBADA -> "centrarla en Z"
       la desplazaba -4.25 uds del sitio donde la coloca el juego, con la capsula de
       colision en un lado y el modelo dibujado en otro). En un modelo con esqueleto
       la posicion la da el skinning, no el AABB de la bind: por eso
       g3d_fbx_set_recenter(0) permite desactivarlo. */
    if (g_fbx_recenter) {
        float mn[3] = { 1e30f, 1e30f, 1e30f }, mx[3] = { -1e30f, -1e30f, -1e30f };
        for (int s = 0; s < scount; s++)
            for (uint32_t v = 0; v < meshes[s].vertex_count; v++) {
                float *p = meshes[s].vertices[v].position;
                for (int c = 0; c < 3; c++) { if (p[c] < mn[c]) mn[c] = p[c]; if (p[c] > mx[c]) mx[c] = p[c]; }
            }
        float off[3] = { -(mn[0]+mx[0])*0.5f, -mn[1], -(mn[2]+mx[2])*0.5f };
        for (int s = 0; s < scount; s++)
            for (uint32_t v = 0; v < meshes[s].vertex_count; v++) {
                meshes[s].vertices[v].position[0] += off[0];
                meshes[s].vertices[v].position[1] += off[1];
                meshes[s].vertices[v].position[2] += off[2];
            }
        if (any_skin) { model->skin_offset[0]=off[0]; model->skin_offset[1]=off[1]; model->skin_offset[2]=off[2]; }
    }
    for (int s = 0; s < scount; s++) {
        g3d_mesh_calculate_bounds(&meshes[s]);
        g3d_mesh_upload_gpu(&meshes[s]);
    }
    model->meshes = meshes; model->mesh_count = (uint32_t)scount;
    model->mesh_textures = textures; model->albedo_texture = textures[0];
    model->mesh_normal = normals; model->mesh_metallic = metals; model->mesh_roughness = roughs;
    model->skinned = any_skin;
    strncpy(model->filepath, filepath, sizeof(model->filepath) - 1);

    /* ---- bake animations (sample local TRS per node at 30 fps) ---- */
    int ac = (int)scene->anim_stacks.count;
    model->animation_count = ac;
    model->animations = ac ? (G3DAnimation *)calloc(ac, sizeof(G3DAnimation)) : NULL;
    const double FPS = 30.0;
    for (int a = 0; a < ac; a++) {
        ufbx_anim_stack *stk = scene->anim_stacks.data[a];
        G3DAnimation *A = &model->animations[a];
        strncpy(A->name, stk->name.data ? stk->name.data : "anim", 63);
        double dur = stk->time_end - stk->time_begin; if (dur < 0.0) dur = 0.0;
        int nframes = (int)(dur * FPS) + 1; if (nframes < 2) nframes = 2;
        A->duration = (float)dur;
        A->channel_count = nc * 3;    /* T, R, S per node */
        A->channels = (G3DAnimChannel *)calloc(A->channel_count, sizeof(G3DAnimChannel));
        for (int i = 0; i < nc; i++) {
            ufbx_node *node = scene->nodes.data[i];
            for (int pth = 0; pth < 3; pth++) {
                G3DAnimChannel *ch = &A->channels[i*3 + pth];
                ch->node = i; ch->path = pth; ch->interp = 0; ch->key_count = nframes;
                ch->times = (float *)malloc(nframes * sizeof(float));
                int comp = (pth == 1) ? 4 : 3;
                ch->values = (float *)malloc((size_t)nframes * comp * sizeof(float));
                for (int fdx = 0; fdx < nframes; fdx++) {
                    double t = stk->time_begin + (double)fdx / FPS;
                    ufbx_transform tr = ufbx_evaluate_transform(stk->anim, node, t);
                    ch->times[fdx] = (float)((double)fdx / FPS);
                    if (pth == 0) { ch->values[fdx*3]=(float)tr.translation.x; ch->values[fdx*3+1]=(float)tr.translation.y; ch->values[fdx*3+2]=(float)tr.translation.z; }
                    else if (pth == 1) { ch->values[fdx*4]=(float)tr.rotation.x; ch->values[fdx*4+1]=(float)tr.rotation.y; ch->values[fdx*4+2]=(float)tr.rotation.z; ch->values[fdx*4+3]=(float)tr.rotation.w; }
                    else { ch->values[fdx*3]=(float)tr.scale.x; ch->values[fdx*3+1]=(float)tr.scale.y; ch->values[fdx*3+2]=(float)tr.scale.z; }
                }
            }
        }
    }

    g3d_model_calculate_bounds(model);
    printf("G3D: FBX loaded: %s (%d submeshes, %d joints, %d anims, skinned=%d)\n",
           filepath, scount, jc, ac, any_skin);
    ufbx_free_scene(scene);
    return model;
}
