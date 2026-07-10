// Mapa procedural con una GRAN PLAYA y mar: las olas ruedan y ROMPEN en la orilla.
//  - terreno de un heightmap procedural (demo_beach.g3dh): tierra alta -> playa -> mar
//  - WATERSIM con mar global a nivel 0 (rellena todo lo que este bajo 0)
//  - swell direccional que crece hacia la orilla (shoaling) + surf rompiente
//  - pipeline: HDR + filmic + bloom + SSAO + reflejos (SSR) del agua
// Camara libre: WASD mover, raton mirar, SPACE/LSHIFT subir/bajar. ESC salir.
// Compile: bgdc TEST_BEACH.PRG   Run: bgdi TEST_BEACH.dcb

import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int light;
    // puntos de salpicadura (rocas dentro del agua)
    float splx[20]; float splz[20]; int spltrigger[20]; int nspl = 0;
END

// Coloca un modelo (todos sus submeshes) en (px,py,pz), escalado a 'th' de alto y girado.
FUNCTION place_model(int model, float px, float py, float pz, float th, float roty)
PRIVATE
    int n; int j; int sm; int st; int mat; int ent; float mh; float s;
    int snorm; int smetal; int srough;
BEGIN
    IF (model < 0) RETURN; END
    mh = g3d_model_height(model);
    IF (mh <= 0.0) mh = 1.0; END
    s = th / mh;
    n = g3d_model_submesh_count(model);
    FOR (j = 0; j < n; j = j + 1)
        sm = g3d_model_submesh(model, j);
        st = g3d_model_submesh_texture(model, j);
        mat = g3d_material_create();
        g3d_material_set_color(mat, 1.0, 1.0, 1.0, 1.0);
        IF (st >= 0) g3d_material_set_texture(mat, 0, st); END
        // Mapas PBR (normal/metallic/roughness) si el modelo los trae (p.ej. FBX)
        snorm = g3d_model_submesh_map(model, j, 1);
        smetal = g3d_model_submesh_map(model, j, 2);
        srough = g3d_model_submesh_map(model, j, 3);
        IF (snorm >= 0) g3d_material_set_map(mat, 1, snorm); END
        IF (smetal >= 0) g3d_material_set_map(mat, 2, smetal); END
        IF (srough >= 0) g3d_material_set_map(mat, 3, srough); END
        ent = g3d_entity_spawn(scene, 0, 0, 0, 0);
        g3d_entity_set_mesh(ent, sm);
        g3d_entity_set_material(ent, mat);
        g3d_entity_set_scale(ent, s, s, s);
        g3d_entity_set_position(ent, px, py, pz);
        g3d_entity_set_rotation(ent, 0.0, roty * 1000, 0.0);
    END
END

PROCESS barco_pirata(int mship, float base_x, float base_y, float base_z, float scale, float yaw_inicial, float offset_tiempo)
PRIVATE
    float tiempo = 0.0;
    float t_local;
    float onda_x;
    float onda_z;
    float onda_y;
    float altura_bamboleo;
END
BEGIN
    ctype = C_3D;
    csubtype = C3D_ENTITY;
    // Spawneamos la entidad 3D específica para este proceso
    entity = G3D_MODEL_SPAWN(scene, mship, base_x, base_y, base_z, scale, yaw_inicial);

    angle_y = yaw_inicial;

    x = base_x;
    z = base_z;

    LOOP
        // El tiempo avanza independiente dentro de este proceso
        tiempo += 1.0;
        t_local = tiempo + offset_tiempo;

        // Calculamos las ondas usando la escala de milésimas de Bennu
        onda_x = sin(t_local * 1000);
        onda_z = cos((t_local / 2.0) * 1000);
        onda_y = sin(t_local * 1500);

        angle_x = onda_x * 4000.0;   // 4 grados de cabeceo
        angle_z = onda_z * 6000.0;   // 6 grados de alabeo
        altura_bamboleo = onda_y * 0.3; // Subida/bajada vertical

        y = base_y + altura_bamboleo;

        FRAME;
    END
END

PROCESS EnviromentLight()
BEGIN
    ctype = C_3D;
    csubtype = C3D_LIGHT;

    // NOCHE: luna fria y tenue + ambiente oscuro azulado (la escena la iluminan las antorchas)
    // Las coordenadas base (origen)
    x = 0;
    y = 0;
    z = 0;

    // Hacia dónde apunta la luz. El motor hará: dir = target - origen (x, y, z)
    target_x = -0.3;
    target_y = -1.0;
    target_z = -0.4;

    // Intensidad
    intensity = 0.28;

    // Color (debes setearlo con las variables propias de BennuGD de 0 a 255)
    // porque ahora el hook sobreescribirá el color base de la luz cada frame!
    color_r = 255 * 0.5;
    color_g = 255 * 0.6;
    color_b = 255 * 0.9;

    // Creamos la luz (el tipo 0 es direccional)
    // El color inicial que pasamos da igual porque el hook lo sobreescribirá
    entity = g3d_light_create(0, 1.0, 1.0, 1.0);

    // Las funciones extra (sombras y estados globales) se siguen llamando igual
    g3d_light_enable_shadow(entity, 1);
    g3d_set_shadows(1);

    g3d_set_ambient_light(0.05, 0.07, 0.13, 1.0);

    LOOP
        // Cada vez que se llame FRAME, internamente el hook enviará
        // el target, x, y, z, la intensidad, y el color a tu luz 3D.

        // Si haces un cambio, por ejemplo: intensity += 0.1;
        // se aplicará automáticamente a la luz sin llamar a funciones.
        FRAME;
    END
END

// Interpolacion Catmull-Rom de 4 puntos de control (movimiento suave y continuo).
FUNCTION float crv(float p0, float p1, float p2, float p3, float tt)
BEGIN
    RETURN 0.5 * ((2.0*p1) + (-p0+p2)*tt
                 + (2.0*p0 - 5.0*p1 + 4.0*p2 - p3)*tt*tt
                 + (-p0 + 3.0*p1 - 3.0*p2 + p3)*tt*tt*tt);
END

PROCESS CameraController()
PRIVATE
    // camara libre
    DOUBLE yaw = 0.0; DOUBLE pitch = -10.0;
    DOUBLE msens = 0.12; DOUBLE spd = 1.8;
    DOUBLE fwx; DOUBLE fwy; DOUBLE fwz; DOUBLE rgx; DOUBLE rgz;
    // --- camara cinematica (spline Catmull-Rom) ---
    float kx[24]; float ky[24]; float kz[24];    // posiciones clave
    float lx[24]; float ly[24]; float lz[24];    // objetivos (look-at) clave
    int nk = 17;                                 // numero de fotogramas clave
    float cinu = 0.0; int cine = 1; int kcp = 0; // parametro 0..nk-1, activo, edge C
    int a0; int a1; int a2; int a3; float ut;
    float ccx; float ccy; float ccz; float ltx; float lty; float ltz;
BEGIN
    ctype = C_3D;
    csubtype = C3D_CAMERA;
    entity = g3d_camera_create();
    g3d_camera_set_active(entity);

    x = 0.0; y = 30.0; z = -120.0;

    // --- Fotogramas clave del plano secuencia (pos -> objetivo) ---
    kx[0] =  25.0; ky[0] = 85.0; kz[0] =-135.0;   lx[0] =  25.0; ly[0] =  5.0; lz[0] =-100.0;
    kx[1] =  10.0; ky[1] = 45.0; kz[1] =-100.0;   lx[1] = -25.0; ly[1] =  8.0; lz[1] = -82.0;
    kx[2] = -48.0; ky[2] =  7.0; kz[2] = -72.0;   lx[2] = -25.0; ly[2] = 10.0; lz[2] = -82.0;
    kx[3] = -25.0; ky[3] =  6.0; kz[3] = -62.0;   lx[3] = -25.0; ly[3] = 12.0; lz[3] = -86.0;
    kx[4] =  20.0; ky[4] =  6.0; kz[4] = -92.0;   lx[4] =  75.0; ly[4] = 10.0; lz[4] =-118.0;
    kx[5] =  52.0; ky[5] =  6.0; kz[5] =-108.0;   lx[5] =  75.0; ly[5] = 12.0; lz[5] =-122.0;
    kx[6] =  98.0; ky[6] =  6.0; kz[6] =-120.0;   lx[6] =  75.0; ly[6] = 12.0; lz[6] =-122.0;
    kx[7] = 140.0; ky[7] = 15.0; kz[7] = -28.0;   lx[7] = 130.0; ly[7] =  3.0; lz[7] =  12.0;
    kx[8] = 130.0; ky[8] = 12.0; kz[8] =  -8.0;   lx[8] = 130.0; ly[8] =  3.0; lz[8] =  14.0;
    kx[9] =  95.0; ky[9] = 12.0; kz[9] =  -8.0;   lx[9] =  95.0; ly[9] =  3.0; lz[9] =  14.0;
    kx[10]=  60.0; ky[10]= 12.0; kz[10]=  -8.0;   lx[10]=  60.0; ly[10]=  3.0; lz[10]=  14.0;
    kx[11]=  25.0; ky[11]= 12.0; kz[11]=  -8.0;   lx[11]=  25.0; ly[11]=  3.0; lz[11]=  14.0;
    kx[12]= -10.0; ky[12]= 12.0; kz[12]=  -8.0;   lx[12]= -10.0; ly[12]=  3.0; lz[12]=  14.0;
    kx[13]= -45.0; ky[13]= 12.0; kz[13]=  -8.0;   lx[13]= -45.0; ly[13]=  3.0; lz[13]=  14.0;
    kx[14]= -80.0; ky[14]= 12.0; kz[14]=  -8.0;   lx[14]= -80.0; ly[14]=  3.0; lz[14]=  14.0;
    kx[15]=-115.0; ky[15]= 12.0; kz[15]=  -8.0;   lx[15]=-115.0; ly[15]=  3.0; lz[15]=  14.0;
    kx[16]=-150.0; ky[16]= 12.0; kz[16]=  -8.0;   lx[16]=-150.0; ly[16]=  3.0; lz[16]=  14.0;

    LOOP
        // Teclas: C = (re)lanzar el plano secuencia, F = camara libre
        IF (key(_C) AND kcp == 0) cine = 1; cinu = 0.0; END
        kcp = key(_C);
        IF (key(_F)) cine = 0; END

        IF (cine == 1)
            // ---- PLANO SECUENCIA: camara sobre el spline ----
            cinu = cinu + 0.005;                  // velocidad del barrido
            IF (cinu > nk - 1) cinu = nk - 1; cine = 0; x = ccx; y = ccy; z = ccz; END
            a1 = cinu; IF (a1 > nk - 2) a1 = nk - 2; END   // segmento (floor)
            ut = cinu - a1;
            a0 = a1 - 1; IF (a0 < 0) a0 = 0; END
            a2 = a1 + 1; IF (a2 > nk - 1) a2 = nk - 1; END
            a3 = a1 + 2; IF (a3 > nk - 1) a3 = nk - 1; END
            ccx = crv(kx[a0], kx[a1], kx[a2], kx[a3], ut);
            ccy = crv(ky[a0], ky[a1], ky[a2], ky[a3], ut);
            ccz = crv(kz[a0], kz[a1], kz[a2], kz[a3], ut);

            ltx = crv(lx[a0], lx[a1], lx[a2], lx[a3], ut);
            lty = crv(ly[a0], ly[a1], ly[a2], ly[a3], ut);
            ltz = crv(lz[a0], lz[a1], lz[a2], lz[a3], ut);

            x = ccx; y = ccy; z = ccz;
            target_x = ltx; target_y = lty; target_z = ltz;
        ELSE
            // ---- CAMARA LIBRE ----
            g3d_mouse_update();
            yaw = yaw - g3d_mouse_dx() * msens;
            pitch = pitch - g3d_mouse_dy() * msens;
            IF (pitch > 85.0) pitch = 85.0; END
            IF (pitch < -85.0) pitch = -85.0; END
            fwx = sin(yaw * 1000) * cos(pitch * 1000);
            fwy = sin(pitch * 1000);
            fwz = cos(yaw * 1000) * cos(pitch * 1000);
            rgx = cos(yaw * 1000);
            rgz = -sin(yaw * 1000);
            IF (key(_W)) x = x + fwx * spd; y = y + fwy * spd; z = z + fwz * spd; END
            IF (key(_S)) x = x - fwx * spd; y = y - fwy * spd; z = z - fwz * spd; END
            IF (key(_A)) x = x + rgx * spd; z = z + rgz * spd; END
            IF (key(_D)) x = x - rgx * spd; z = z - rgz * spd; END
            IF (key(_SPACE)) y = y + spd; END
            IF (key(_L_SHIFT)) y = y - spd; END

            target_x = x + fwx; target_y = y + fwy; target_z = z + fwz;
        END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
END

PROCESS main()
PRIVATE
    int i; int m; float px; float py; float pz;
    int mpalm1; int mpalm2; int mpalm3; int mship; int mrock; int mtorch;
    int mcrash; int mmario; int mluigi; int mbarrel; int placed_char; int mbuggy;
    float h1; float h2; float h3; float h4;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);


    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    CameraController();

    EnviromentLight();

    // Cielo de noche + nubes volumetricas en movimiento
    g3d_sky_set_gradient(0.02, 0.03, 0.09,  0.06, 0.08, 0.16);
    g3d_sky_set_clouds(0.7, 1.0);      // capa alta lejana (domo del cielo)
    // capa BAJA volumetrica en espacio-mundo: la puedes atravesar subiendo la camara
    // (cobertura, altitud base, grosor, velocidad). Proyecta sombras sobre la escena.
    g3d_sky_set_low_clouds(0.55, 50.0, 28.0, 1.0);
    g3d_sky_enable(1);

    // Pipeline de render
    g3d_set_hdr(1);
    g3d_set_tonemap(1);            // ACES filmic
    g3d_set_exposure(1.15);
    g3d_set_bloom(1, 0.35, 0.9);
    g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    // Oceano: swell hacia la tierra (+Z), olas altas que rompen en la costa
    g3d_water_set_ocean(0.0, 1.0, 1.4);

    // --- Poblar la escena: palmeras en la tierra, barcos en el mar ---
    mpalm1 = g3d_load_gltf("assets/coconut_palm.glb");
    mpalm2 = g3d_load_gltf("assets/curly_palm.glb");
    mpalm3 = g3d_load_gltf("assets/palm.glb");
    mship  = g3d_load_gltf("assets/pirate_ship.glb");

    // Palmeras esparcidas por la tierra junto a la playa (solo en suelo seco)
    FOR (i = 0; i < 16; i = i + 1)
        px = -170.0 + i * 22.0 + 9.0 * sin(i * 61000);
        pz = 6.0 + 52.0 * abs(sin(i * 97000));
        py = g3d_scene_terrain_height(px, pz);
        IF (py > 1.5)
            m = mpalm1;
            IF (i % 3 == 1) m = mpalm2; END
            IF (i % 3 == 2) m = mpalm3; END
            place_model(m, px, py, pz, 11.0 + 5.0 * abs(sin(i * 37000)), i * 40.0);
        END
    END

    // Barco pirata en el mar (a nivel de agua)

    // Barco 1 (En sus coordenadas, escala 24.0, dirección 35.0, sin offset de tiempo)
    barco_pirata(mship, -25.0, -1.2, -80.0, 24.0, 35000.0, 0.0);

    // Barco 2 (En sus coordenadas, escala 28.0, dirección -55.0, y con offset de 45.0 para desincronizar)
    barco_pirata(mship, 75.0, -1.2, -120.0, 28.0, -55000.0, 45.0);

    // Rocas: en la orilla y asomando del agua poco profunda
    mrock = g3d_load_gltf("assets/rock.glb");
    FOR (i = 0; i < 10; i = i + 1)
        px = -150.0 + i * 30.0 + 12.0 * sin(i * 53000);
        pz = -22.0 + 30.0 * sin(i * 79000);
        py = g3d_scene_terrain_height(px, pz);
        IF (py < 0.5)
            py = -0.6;                       // roca marina: asoma al ras del agua
            IF (nspl < 20) splx[nspl] = px; splz[nspl] = pz; spltrigger[nspl] = rand(60,300); nspl = nspl + 1; END
        END
        place_model(mrock, px, py, pz, 3.0 + 4.0 * abs(sin(i * 41000)), i * 63.0);
    END

    // Modelos para poblar junto a las antorchas
    mtorch  = g3d_load_gltf("assets/torch_simple.glb");
    mcrash  = g3d_load_gltf("assets/crash.glb");
    mmario  = g3d_load_gltf("assets/mario.glb");
    mluigi  = g3d_load_gltf("assets/luigi.glb");
    mbarrel = g3d_load_gltf("assets/barrel.glb");

    // Buggy (FBX) en la arena. Muestreo la altura del terreno bajo su huella y uso la
    // MAS ALTA (+ un pelin) para que ninguna rueda se hunda en los baches.
    mbuggy = g3d_load_fbx("assets/source/VHC_SC_Buggy_01.fbx");
    px = 20.0; pz = 40.0;
    h1 = g3d_scene_terrain_height(px - 5.0, pz);
    h2 = g3d_scene_terrain_height(px + 5.0, pz);
    h3 = g3d_scene_terrain_height(px, pz - 3.5);
    h4 = g3d_scene_terrain_height(px, pz + 3.5);
    py = h1;
    IF (h2 > py) py = h2; END
    IF (h3 > py) py = h3; END
    IF (h4 > py) py = h4; END
    py = py + 0.2;
    place_model(mbuggy, px, py, pz, 4.0, 40.0);

    // Antorchas a lo largo de la orilla; JUSTO AL LADO de cada una, un personaje o barril
    FOR (i = 0; i < 9; i = i + 1)
        px = -150.0 + i * 36.0;
        pz = 3.0 + 16.0 * abs(sin(i * 71000));
        py = g3d_scene_terrain_height(px, pz);
        IF (py > 1.0)
            place_model(mtorch, px, py, pz, 4.5, i * 25.0);
            g3d_fire_add(px, py + 4.3, pz, 1.3);      // llama en la punta
            placed_char = 0;
            IF (i == 1) place_model(mcrash, px + 5.0, py, pz + 1.5, 5.0, 200.0); placed_char = 1; END
            IF (i == 4) place_model(mmario, px + 5.0, py, pz + 1.5, 5.0, 200.0); placed_char = 1; END
            IF (i == 7) place_model(mluigi, px + 5.0, py, pz + 1.5, 5.5, 200.0); placed_char = 1; END
            IF (placed_char == 0) place_model(mbarrel, px + 4.5, py, pz + 1.5, 2.5, i * 47.0); END
        END
    END

    g3d_mouse_capture(1);
    x = 480; y = 270;

    write(0, 10, 10, 0, "FPS:");
    write_var(0, 70, 10, 0, frame_info.fps);
    write(0, 10, 28, 0, "PLANO SECUENCIA en marcha  -  C: repetir   F: camara libre (WASD+raton)");

    int next_splash = rand(50, 100);

    LOOP
        // Salpicaduras periódicas donde las olas golpean las rocas del mar
        FOR (i = 0; i < nspl; ++i)
            --spltrigger[i];
            IF (spltrigger[i] == 0)
                g3d_water_splash(splx[i], 0.0, splz[i], 1.3);
                spltrigger[i] = rand(60,300);
            END
        END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);

    let_me_alone();

END
