// ============================================================================
//  PERF_BEACH.PRG - Test de ESTRES en la playa con INSTANCING + LOD.
//
//  - Cada criatura es un PROCESS (filosofia BennuGD: su propia IA/estado).
//  - Render por INSTANCING: 1 draw por manada/submesh, no por criatura.
//  - LOD por distancia: cerca = malla completa animada (LOD0); lejos = malla
//    SIMPLIFICADA estatica (LOD1, generada con g3d_model_submesh_lod). Cada
//    frame la criatura se anade SOLO a su nivel actual (clear+refill), asi que
//    los lejanos no procesan la malla cara.
//  - main tiene prioridad alta: limpia los grupos de criaturas al inicio del
//    frame ANTES de que las criaturas se anadan.
//
//  WASD + raton = volar,  SHIFT = rapido,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int quit;
    int N_TRIBU; int N_CRAB; int N_GULL; int N_SHIPS; int N_PALMS;
    int NH_TRIBU; int NH_CRAB; int NH_GULL;

    int   tm[8]; float tt[8]; int tnsub; float tscale;
    int   cm[8]; float ct[8]; int cnsub; float cscale;
    int   gm[8]; float gt[8]; int gnsub; float gscale;
    int tgrp[127]; int cgrp[31]; int ggrp[15];    // LOD0 (skinned, por manada)
    int tlod[15]; int clod[15]; int glod[15];     // LOD1 (simplificado, por tipo)

    float LOD_DIST;                                // distancia de cambio a LOD1
    float gcamx; float gcamy; float gcamz;         // camara (para el test de distancia)

    int mpalm1; int mpalm2; int mpalm3; int mship;
    int pg1[15]; int pg2[15]; int pg3[15]; int pns1; int pns2; int pns3;
    float ph1; float ph2; float ph3;

    // peces (bajo el agua) y avioneta (cielo alto)
    int NH_FISHG; int N_GUPPY; int NH_FISHD; int N_DORADA; int N_PLANES;
    int   gum[8]; float gutt[8]; float guscale;   // guppy (1 submesh)
    int   dom[8]; float dott[8]; float doscale;   // dorada (1 submesh)
    int gugrp[15]; int dogrp[15]; int gulod; int dolod;   // peces LOD0 (skinned) + LOD1 (estatico)
    int mplane; int plgrp[3]; int plnsub; float plscale; float planetime; int plskinned;

    float lx; float lz;
    int spawned;
END

FUNCTION int land_point()
PRIVATE int t; float px; float pz;
BEGIN
    FOR (t = 0; t < 40; t = t + 1)
        px = -160.0 + rand(0, 320); pz = 4.0 + rand(0, 54);
        IF (g3d_scene_terrain_height(px, pz) > 1.5) lx = px; lz = pz; RETURN 1; END
    END
    lx = 0.0; lz = 30.0; RETURN 0;
END

FUNCTION int sea_point()
PRIVATE int t; float px; float pz;
BEGIN
    FOR (t = 0; t < 40; t = t + 1)
        px = -150.0 + rand(0, 300); pz = -160.0 + rand(0, 120);
        IF (g3d_scene_terrain_height(px, pz) < -0.5) lx = px; lz = pz; RETURN 1; END
    END
    lx = 0.0; lz = -90.0; RETURN 0;
END

// distancia^2 criatura-camara
FUNCTION float dist2cam(float px, float py, float pz)
PRIVATE float dx; float dy; float dz;
BEGIN
    dx = px - gcamx; dy = py - gcamy; dz = pz - gcamz;
    RETURN dx*dx + dy*dy + dz*dz;
END

// --- Una tribu: proceso propio; se anade cada frame a su nivel LOD ---
PROCESS tribu(int herd, float wx, float wz)
PRIVATE int j; int base; float heading; float spd; float wy; float nx; float nz;
BEGIN
    base = herd * tnsub;
    heading = rand(0, 359); spd = 0.06 + rand(0, 50) / 1000.0;
    LOOP
        IF (rand(0, 100) < 2) heading = heading + rand(-50, 50); END
        nx = wx + sin(heading * 1000) * spd; nz = wz + cos(heading * 1000) * spd;
        wy = g3d_scene_terrain_height(nx, nz);
        IF (wy > 1.2 AND nx > -165.0 AND nx < 165.0 AND nz > 2.0 AND nz < 60.0)
            wx = nx; wz = nz;
        ELSE
            heading = heading + 180;
        END
        wy = g3d_scene_terrain_height(wx, wz);
        IF (dist2cam(wx, wy, wz) < LOD_DIST * LOD_DIST)
            FOR (j = 0; j < tnsub; j = j + 1) g3d_instances_add(tgrp[base + j], wx, wy, wz, heading, tscale); END
        ELSE
            FOR (j = 0; j < tnsub; j = j + 1) g3d_instances_add(tlod[j], wx, wy, wz, heading, tscale); END
        END
        IF (quit) BREAK; END
        FRAME;
    END
END

// --- Un cangrejo: anda de lado ---
PROCESS crab(int herd, float wx, float wz)
PRIVATE int j; int base; float heading; float spd; float wy; float nx; float nz;
BEGIN
    base = herd * cnsub;
    heading = rand(0, 359); spd = 0.05 + rand(0, 40) / 1000.0;
    LOOP
        IF (rand(0, 100) < 3) heading = heading + rand(-70, 70); END
        nx = wx + sin(heading * 1000) * spd; nz = wz + cos(heading * 1000) * spd;
        wy = g3d_scene_terrain_height(nx, nz);
        IF (wy > 0.6 AND wy < 4.0 AND nx > -165.0 AND nx < 165.0 AND nz > 0.0 AND nz < 60.0)
            wx = nx; wz = nz;
        ELSE
            heading = heading + 180;
        END
        wy = g3d_scene_terrain_height(wx, wz);
        IF (dist2cam(wx, wy, wz) < LOD_DIST * LOD_DIST)
            FOR (j = 0; j < cnsub; j = j + 1) g3d_instances_add(cgrp[base + j], wx, wy, wz, heading + 90.0, cscale); END
        ELSE
            FOR (j = 0; j < cnsub; j = j + 1) g3d_instances_add(clod[j], wx, wy, wz, heading + 90.0, cscale); END
        END
        IF (quit) BREAK; END
        FRAME;
    END
END

// --- Una gaviota: vuela en circulo ---
PROCESS gull(int herd, float cx, float cz, float radius, float alt)
PRIVATE int j; int base; float a; float spd; float gx; float gz; float gy;
BEGIN
    base = herd * gnsub;
    a = rand(0, 359); spd = 0.4 + rand(0, 40) / 100.0;
    LOOP
        a = a + spd;
        gx = cx + cos(a * 1000) * radius; gz = cz + sin(a * 1000) * radius;
        gy = alt + 4.0 * sin((a * 3.0) * 1000);
        IF (dist2cam(gx, gy, gz) < LOD_DIST * LOD_DIST)
            FOR (j = 0; j < gnsub; j = j + 1) g3d_instances_add(ggrp[base + j], gx, gy, gz, a + 90.0, gscale); END
        ELSE
            FOR (j = 0; j < gnsub; j = j + 1) g3d_instances_add(glod[j], gx, gy, gz, a + 90.0, gscale); END
        END
        IF (quit) BREAK; END
        FRAME;
    END
END

// --- Un barco (entidad: necesita pitch/roll) ---
PROCESS boat(int model, float sx, float sz, float dir)
PRIVATE int ent; float ang; float by; float rollv; float pitchv;
BEGIN
    ent = g3d_model_spawn(scene, model, sx, 0.0, sz, 22.0, dir);
    ang = rand(0, 359);
    LOOP
        ang = ang + 1.4;
        sx = sx + sin(dir * 1000) * 0.03; sz = sz + cos(dir * 1000) * 0.03;
        IF (sx < -170.0 OR sx > 170.0 OR sz < -170.0 OR sz > -25.0) dir = dir + 180.0; END
        by = 0.7 * sin(ang * 1000); rollv = 3.5 * sin((ang * 0.7) * 1000); pitchv = 2.5 * sin((ang * 0.5) * 1000);
        g3d_entity_set_position(ent, sx, by, sz);
        g3d_entity_set_rotation(ent, pitchv * 1000.0, dir * 1000.0, rollv * 1000.0);
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_model_despawn(ent);
END

// --- Un pez en banco: circula bajo el agua alrededor del centro del banco ---
PROCESS fish(int ftype, int herd, float cx, float cz, float radius, float depth)
PRIVATE int grp; float a; float spd; float fx; float fz; float fy;
BEGIN
    a = rand(0, 359); spd = 0.5 + rand(0, 40) / 100.0;
    LOOP
        a = a + spd;
        fx = cx + cos(a * 1000) * radius;
        fz = cz + sin(a * 1000) * radius;
        fy = depth + 0.8 * sin((a * 2.0) * 1000);   // ondeo vertical suave
        IF (dist2cam(fx, fy, fz) < LOD_DIST * LOD_DIST)   // cerca = skinned
            IF (ftype == 0) grp = gugrp[herd]; ELSE grp = dogrp[herd]; END
        ELSE                                              // lejos = LOD1 estatico
            IF (ftype == 0) grp = gulod; ELSE grp = dolod; END
        END
        IF (ftype == 0)
            g3d_instances_add(grp, fx, fy, fz, a + 90.0, guscale);
        ELSE
            g3d_instances_add(grp, fx, fy, fz, a + 90.0, doscale);
        END
        IF (quit) BREAK; END
        FRAME;
    END
END

// --- Avioneta: circula alto (por encima de las gaviotas). Instancing SIN
//     skinning (malla estatica en bind pose) para que no se deforme. ---
PROCESS plane(float cx, float cz, float radius, float alt, float speed)
PRIVATE int j; float a; float px; float pz; float py;
BEGIN
    a = rand(0, 359);
    LOOP
        a = a + speed;
        px = cx + cos(a * 1000) * radius;
        pz = cz + sin(a * 1000) * radius;
        py = alt + 4.0 * sin((a * 0.4) * 1000);
        FOR (j = 0; j < plnsub; j = j + 1)
            g3d_instances_add(plgrp[j], px, py, pz, a + 90.0, plscale);
        END
        IF (quit) BREAK; END
        FRAME;
    END
END

PROCESS main()
PRIVATE
    int light; int i; int h; int j;
    float dt; float msens; float spd; float wy; float th;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    int fclouds; int fssao; int fbloom; int fssr; int fshadow; int fanim;
    int k1; int k2; int k3; int k4; int k5; int k6; int k7; int k8;
    int gokuanim; int funder;
BEGIN
    priority = 100;              // main corre ANTES que las criaturas (limpia grupos)
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(0, 0);

    NH_TRIBU = 8; N_TRIBU = 240;
    NH_CRAB  = 6; N_CRAB  = 120;
    NH_GULL  = 8; N_GULL  = 240;
    N_SHIPS = 4; N_PALMS = 28;
    NH_FISHG = 4; N_GUPPY = 100;   // guppy
    NH_FISHD = 4; N_DORADA = 70;   // dorada
    N_PLANES = 3;
    LOD_DIST = 60.0;   // criaturas lejanas -> LOD1 estatico (sin skinning) = mas FPS

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.95, 0.85);
    g3d_light_set_direction(light, -0.45, -0.75, -0.35);
    g3d_light_set_intensity(light, 1.6); g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1); g3d_set_ambient_light(0.32, 0.36, 0.44, 1.0);

    g3d_sky_set_gradient(0.32, 0.52, 0.85,  0.80, 0.86, 0.94);
    g3d_sky_set_clouds(0.6, 1.0); g3d_sky_set_low_clouds(0.55, 60.0, 30.0, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.15);
    g3d_set_bloom(1, 0.35, 0.9); g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6); g3d_water_set_ocean(0.0, 1.0, 1.4);

    // ---- palmeras INSTANCIADAS (estaticas: add una vez, NO se limpian) ----
    mpalm1 = g3d_load_gltf("assets/coconut_palm.glb");
    mpalm2 = g3d_load_gltf("assets/curly_palm.glb");
    mpalm3 = g3d_load_gltf("assets/palm.glb");
    mship  = g3d_load_gltf("assets/pirate_ship.glb");
    pns1 = g3d_model_submesh_count(mpalm1); ph1 = g3d_model_height(mpalm1);
    pns2 = g3d_model_submesh_count(mpalm2); ph2 = g3d_model_height(mpalm2);
    pns3 = g3d_model_submesh_count(mpalm3); ph3 = g3d_model_height(mpalm3);
    FOR (j = 0; j < pns1; j = j + 1) pg1[j] = g3d_instances_create(g3d_model_submesh(mpalm1, j), g3d_model_submesh_texture(mpalm1, j)); g3d_instances_set_distance(pg1[j], 800.0); END
    FOR (j = 0; j < pns2; j = j + 1) pg2[j] = g3d_instances_create(g3d_model_submesh(mpalm2, j), g3d_model_submesh_texture(mpalm2, j)); g3d_instances_set_distance(pg2[j], 800.0); END
    FOR (j = 0; j < pns3; j = j + 1) pg3[j] = g3d_instances_create(g3d_model_submesh(mpalm3, j), g3d_model_submesh_texture(mpalm3, j)); g3d_instances_set_distance(pg3[j], 800.0); END
    FOR (i = 0; i < N_PALMS; i = i + 1)
        IF (land_point())
            wy = g3d_scene_terrain_height(lx, lz); yaw = rand(0, 359); th = 10.0 + rand(0, 6);
            IF (i % 3 == 0) FOR (j = 0; j < pns1; j = j + 1) g3d_instances_add(pg1[j], lx, wy, lz, yaw, th / ph1); END END
            IF (i % 3 == 1) FOR (j = 0; j < pns2; j = j + 1) g3d_instances_add(pg2[j], lx, wy, lz, yaw, th / ph2); END END
            IF (i % 3 == 2) FOR (j = 0; j < pns3; j = j + 1) g3d_instances_add(pg3[j], lx, wy, lz, yaw, th / ph3); END END
        END
    END
    FOR (i = 0; i < N_SHIPS; i = i + 1) sea_point(); boat(mship, lx, lz, rand(0, 359)); END

    // ---- manadas + grupos LOD0 (skinned) ----
    // gpu_skin = 1 -> animate solo calcula matrices de hueso; el shader skinnea
    FOR (h = 0; h < NH_TRIBU; h = h + 1) tm[h] = g3d_load_gltf("assets/goku_kid.glb"); g3d_model_set_gpu_skin(tm[h], 1); tt[h] = h * 1.6; END
    FOR (h = 0; h < NH_CRAB;  h = h + 1) cm[h] = g3d_load_gltf("assets/crab.glb");  g3d_model_set_gpu_skin(cm[h], 1); ct[h] = h * 0.5; END
    FOR (h = 0; h < NH_GULL;  h = h + 1) gm[h] = g3d_load_gltf("assets/gaviota.glb"); g3d_model_set_gpu_skin(gm[h], 1); gt[h] = h * 1.0; END
    tnsub = g3d_model_submesh_count(tm[0]); tscale = 3.2 / g3d_model_height(tm[0]);
    cnsub = g3d_model_submesh_count(cm[0]); cscale = 1.4 / g3d_model_height(cm[0]);
    gnsub = g3d_model_submesh_count(gm[0]); gscale = 1.4 / g3d_model_height(gm[0]);
    // LOD0 = grupos SKINNED (skinning en GPU)
    FOR (h = 0; h < NH_TRIBU; h = h + 1)
        FOR (j = 0; j < tnsub; j = j + 1) tgrp[h*tnsub+j] = g3d_instances_create_skinned(g3d_model_submesh(tm[h], j), g3d_model_submesh_texture(tm[h], j), tm[h]); g3d_instances_set_distance(tgrp[h*tnsub+j], 600.0); END
    END
    FOR (h = 0; h < NH_CRAB; h = h + 1)
        FOR (j = 0; j < cnsub; j = j + 1) cgrp[h*cnsub+j] = g3d_instances_create_skinned(g3d_model_submesh(cm[h], j), g3d_model_submesh_texture(cm[h], j), cm[h]); g3d_instances_set_distance(cgrp[h*cnsub+j], 600.0); END
    END
    FOR (h = 0; h < NH_GULL; h = h + 1)
        FOR (j = 0; j < gnsub; j = j + 1) ggrp[h*gnsub+j] = g3d_instances_create_skinned(g3d_model_submesh(gm[h], j), g3d_model_submesh_texture(gm[h], j), gm[h]); g3d_instances_set_distance(ggrp[h*gnsub+j], 600.0); END
    END
    // ---- grupos LOD1 (simplificado, compartido por tipo) ----
    FOR (j = 0; j < tnsub; j = j + 1) tlod[j] = g3d_instances_create(g3d_model_submesh_lod(tm[0], j, 10), g3d_model_submesh_texture(tm[0], j)); g3d_instances_set_distance(tlod[j], 600.0); END
    FOR (j = 0; j < cnsub; j = j + 1) clod[j] = g3d_instances_create(g3d_model_submesh_lod(cm[0], j, 10), g3d_model_submesh_texture(cm[0], j)); g3d_instances_set_distance(clod[j], 600.0); END
    FOR (j = 0; j < gnsub; j = j + 1) glod[j] = g3d_instances_create(g3d_model_submesh_lod(gm[0], j, 8),  g3d_model_submesh_texture(gm[0], j)); g3d_instances_set_distance(glod[j], 600.0); END

    // ---- peces (guppy + dorada): skinned instancing, 1 submesh ----
    FOR (h = 0; h < NH_FISHG; h = h + 1) gum[h] = g3d_load_gltf("assets/guppy.glb"); g3d_model_set_gpu_skin(gum[h], 1); END
    FOR (h = 0; h < NH_FISHD; h = h + 1) dom[h] = g3d_load_gltf("assets/dorada.glb"); g3d_model_set_gpu_skin(dom[h], 1); END
    guscale = 1.0 / g3d_model_height(gum[0]);   // guppy ~1u
    doscale = 1.6 / g3d_model_height(dom[0]);   // dorada ~1.6u
    FOR (h = 0; h < NH_FISHG; h = h + 1) gugrp[h] = g3d_instances_create_skinned(g3d_model_submesh(gum[h], 0), g3d_model_submesh_texture(gum[h], 0), gum[h]); g3d_instances_set_distance(gugrp[h], 600.0); END
    FOR (h = 0; h < NH_FISHD; h = h + 1) dogrp[h] = g3d_instances_create_skinned(g3d_model_submesh(dom[h], 0), g3d_model_submesh_texture(dom[h], 0), dom[h]); g3d_instances_set_distance(dogrp[h], 600.0); END
    // LOD1 de peces (simplificado, estatico, compartido) -> peces lejanos sin skinning
    gulod = g3d_instances_create(g3d_model_submesh_lod(gum[0], 0, 8), g3d_model_submesh_texture(gum[0], 0)); g3d_instances_set_distance(gulod, 600.0);
    dolod = g3d_instances_create(g3d_model_submesh_lod(dom[0], 0, 8), g3d_model_submesh_texture(dom[0], 0)); g3d_instances_set_distance(dolod, 600.0);
    // ---- avioneta: ESTATICA (sin skinning: barato y la bind pose ya es correcta).
    //      Solo submesh 0 (cuerpo texturado); ignora outline/blur del modelo toon. ----
    mplane = g3d_load_gltf("assets/avioneta.glb");
    plnsub = g3d_model_submesh_count(mplane);
    IF (plnsub > 1) plnsub = 1; END
    plskinned = 0;
    FOR (j = 0; j < plnsub; j = j + 1) plgrp[j] = g3d_instances_create(g3d_model_submesh(mplane, j), g3d_model_submesh_texture(mplane, j)); g3d_instances_set_distance(plgrp[j], 900.0); g3d_instances_set_alpha_cut(plgrp[j], 0); END
    plscale = 14.0 / g3d_model_height(mplane);
    IF (plscale <= 0.0 OR plscale > 40.0) plscale = 7.0; END

    // ---- spawn: UN PROCESO POR CRIATURA ----
    FOR (i = 0; i < N_TRIBU; i = i + 1) land_point(); tribu(i % NH_TRIBU, lx, lz); END
    FOR (i = 0; i < N_CRAB;  i = i + 1) land_point(); crab(i % NH_CRAB, lx, lz); END
    FOR (i = 0; i < N_GULL;  i = i + 1) gull(i % NH_GULL, -120.0 + rand(0, 240), -120.0 + rand(0, 160), 12.0 + rand(0, 40), 22.0 + rand(0, 45)); END
    // peces: bancos bajo el agua (centro en zona de mar, profundidad negativa)
    FOR (i = 0; i < N_GUPPY; i = i + 1) sea_point(); fish(0, i % NH_FISHG, lx, lz, 4.0 + rand(0, 8), -2.0 - rand(0, 3)); END
    FOR (i = 0; i < N_DORADA; i = i + 1) sea_point(); fish(1, i % NH_FISHD, lx, lz, 5.0 + rand(0, 10), -2.5 - rand(0, 3)); END
    // avionetas: circulan ALTO (gaviotas van a 22-67 -> avioneta a 95-120)
    FOR (i = 0; i < N_PLANES; i = i + 1) plane(-40.0 + rand(0, 80), -60.0 + rand(0, 80), 90.0 + rand(0, 40), 95.0 + rand(0, 25), 0.35 + rand(0, 20) / 100.0); END
    spawned = N_TRIBU + N_CRAB + N_GULL + N_GUPPY + N_DORADA;

    camx = 0.0; camy = 60.0; camz = 70.0;
    yaw = 180.0; pitch = -28.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    fclouds = 1; fssao = 1; fbloom = 1; fssr = 1; fshadow = 1; fanim = 1;
    gokuanim = 2; funder = 0;
    write(0, 10, 10, 0, "PROFILE: 1 nubes 2 SSAO 3 bloom 4 SSR 5 sombras 6 skinning  |  7 = anim goku");
    write(0, 10, 64, 0, "goku anim:"); write_var(0, 100, 64, 0, gokuanim);
    write(0, 10, 28, 0, "Criaturas:"); write_var(0, 110, 28, 0, spawned);
    write(0, 10, 46, 0, "nubes:"); write_var(0, 70, 46, 0, fclouds);
    write(0, 110, 46, 0, "ssao:"); write_var(0, 165, 46, 0, fssao);
    write(0, 210, 46, 0, "bloom:"); write_var(0, 270, 46, 0, fbloom);
    write(0, 320, 46, 0, "ssr:"); write_var(0, 365, 46, 0, fssr);
    write(0, 410, 46, 0, "sombras:"); write_var(0, 490, 46, 0, fshadow);
    write(0, 1770, 1052, 0, "FPS:");   write_var(0, 1815, 1052, 0, frame_info.fps);
    write(0, 1620, 1030, 0, "cam Y (bajo 0 = agua):"); write_var(0, 1820, 1030, 0, camy);

    LOOP
        // limpiar grupos de criaturas (main tiene prioridad alta -> corre antes)
        FOR (i = 0; i < NH_TRIBU * tnsub; i = i + 1) g3d_instances_clear(tgrp[i]); END
        FOR (i = 0; i < NH_CRAB  * cnsub; i = i + 1) g3d_instances_clear(cgrp[i]); END
        FOR (i = 0; i < NH_GULL  * gnsub; i = i + 1) g3d_instances_clear(ggrp[i]); END
        FOR (i = 0; i < tnsub; i = i + 1) g3d_instances_clear(tlod[i]); END
        FOR (i = 0; i < cnsub; i = i + 1) g3d_instances_clear(clod[i]); END
        FOR (i = 0; i < gnsub; i = i + 1) g3d_instances_clear(glod[i]); END
        FOR (i = 0; i < NH_FISHG; i = i + 1) g3d_instances_clear(gugrp[i]); END
        FOR (i = 0; i < NH_FISHD; i = i + 1) g3d_instances_clear(dogrp[i]); END
        g3d_instances_clear(gulod); g3d_instances_clear(dolod);
        FOR (i = 0; i < plnsub; i = i + 1) g3d_instances_clear(plgrp[i]); END

        IF (fanim)
            FOR (h = 0; h < NH_TRIBU; h = h + 1) tt[h] += dt;      g3d_model_animate(tm[h], gokuanim, tt[h], 1); END
            FOR (h = 0; h < NH_CRAB;  h = h + 1) ct[h] += dt;      g3d_model_animate(cm[h], 0, ct[h], 1); END
            FOR (h = 0; h < NH_GULL;  h = h + 1) gt[h] += dt * 1.4; g3d_model_animate(gm[h], 0, gt[h], 1); END
            FOR (h = 0; h < NH_FISHG; h = h + 1) gutt[h] += dt;    g3d_model_animate(gum[h], 0, gutt[h], 1); END
            FOR (h = 0; h < NH_FISHD; h = h + 1) dott[h] += dt;    g3d_model_animate(dom[h], 0, dott[h], 1); END
            IF (plskinned) planetime += dt; g3d_model_animate(mplane, 0, planetime, 1); END
        END

        spd = 1.4; IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 5.0; END
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens; pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000); fwy = sin(pitch * 1000); fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        gcamx = camx; gcamy = camy; gcamz = camz;
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        // distorsion + tinte cuando la camara baja del nivel del mar (Y=0), o forzado con tecla 8
        IF (camy < 0.0 OR funder)
            g3d_set_underwater(1, 0.04, 0.28, 0.42, 1.5);
        ELSE
            g3d_set_underwater(0, 0.0, 0.0, 0.0, 0.0);
        END

        // --- PROFILING: apagar/encender efectos caros y ver que recupera FPS ---
        IF (key(_1) AND k1 == 0)
            fclouds = 1 - fclouds;
            IF (fclouds) g3d_sky_set_low_clouds(0.55, 60.0, 30.0, 1.0); ELSE g3d_sky_set_low_clouds(0.0, 60.0, 30.0, 1.0); END
        END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) fssao = 1 - fssao; g3d_set_ssao(fssao, 0.5, 1.2); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0) fbloom = 1 - fbloom; g3d_set_bloom(fbloom, 0.35, 0.9); END
        k3 = key(_3);
        IF (key(_4) AND k4 == 0) fssr = 1 - fssr; g3d_water_set_ssr(fssr, 0.6); END
        k4 = key(_4);
        IF (key(_5) AND k5 == 0) fshadow = 1 - fshadow; g3d_set_shadows(fshadow); END
        k5 = key(_5);
        IF (key(_6) AND k6 == 0) fanim = 1 - fanim; END   // congela skinning (diagnostico CPU)
        k6 = key(_6);
        IF (key(_7) AND k7 == 0) gokuanim = gokuanim + 1; IF (gokuanim > 8) gokuanim = 0; END END   // cicla animacion de goku (0..8)
        k7 = key(_7);
        IF (key(_8) AND k8 == 0) funder = 1 - funder; END   // FUERZA underwater (test del pase)
        k8 = key(_8);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
