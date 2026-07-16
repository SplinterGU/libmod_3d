// ============================================================================
//  TEST_TR_SECTORS.PRG - Visor de un mapa exportado por SECTORES (assets/sectors).
//
//  El manifest declara 1968 sectores de 1.024 unidades sobre una rejilla 47x44,
//  pero el mapa ENTERO son 15.112 triangulos en 48x45 unidades. O sea: cada
//  sector tiene ~8 triangulos y el mapa cabe de sobra en memoria de una vez.
//
//  Por eso el visor tiene DOS modos, y el interesante es la comparacion:
//
//    F1 = CARGA TOTAL   -> carga los 1968 sectores y los deja puestos.
//                          Es lo correcto para un mapa de este tamano.
//    F2 = STREAMING     -> solo carga los sectores cerca de la camara y
//                          descarga los lejanos. Util para ver el sistema
//                          funcionando, pero aqui es matar moscas a canonazos.
//
//  Arranca en carga total. Mira el contador de DRAW CALLS: cada sector es un
//  draw, asi que 1968 sectores = 1968 draws para 15k triangulos. Ese es el
//  problema real del export, no el motor.
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    SECTORS_DIR = "assets/sectors/";
    // Del manifest.json: rejilla y tamano de celda.
    GRID_X   = 47;
    GRID_Z   = 44;
    CELL     = 1.024;
    // Origen del mundo (manifest: min = [16.384, 0, 53.248]).
    ORIGIN_X = 16.384;
    ORIGIN_Z = 53.248;
    // Radio de streaming EN SECTORES (modo F2)
    STREAM_R = 12;
END

GLOBAL
    int scene; int camera; int light;
    float camx; float camy; float camz;
    int mode;                    // 1 = carga total, 2 = streaming
    int loaded_n;                // sectores puestos ahora mismo
    int total_tris;
    // Un slot por celda de la rejilla: id de entidad (-1 = no cargado)
    int ent_of[47*44];
    int mdl_of[47*44];
    int kf1; int kf2;
END

// Carga un sector (sx,sz) si existe y no esta ya puesto. Devuelve 1 si lo puso.
FUNCTION int sector_load(int sx, int sz)
PRIVATE
    int idx; int m; int e;
    string path;
BEGIN
    IF (sx < 0 OR sz < 0 OR sx >= GRID_X OR sz >= GRID_Z) RETURN 0; END
    idx = sz * GRID_X + sx;
    IF (ent_of[idx] != -1) RETURN 0; END         // ya cargado

    path = SECTORS_DIR + "sector_" + sx + "_" + sz + ".glb";
    // Los .glb del export traen su geometria en coordenadas ABSOLUTAS del mundo,
    // asi que hay que desactivar el recentrado: si no, cada sector se apilaria
    // sobre el origen y el mapa saldria hecho un amasijo.
    g3d_gltf_set_recenter(0);
    m = g3d_load_gltf(path);
    // g3d_load_gltf devuelve -1 si falla (NO 0): ese sector no existe en la
    // rejilla. Con 1968 de 2068 celdas, ~100 huecos son normales.
    IF (m == -1 OR m == 0) RETURN 0; END

    e = g3d_model_spawn(scene, m, 0.0, 0.0, 0.0, 0.0, 0.0);
    ent_of[idx] = e;
    mdl_of[idx] = m;
    loaded_n++;
    RETURN 1;
END

FUNCTION int sector_unload(int sx, int sz)
PRIVATE
    int idx;
BEGIN
    IF (sx < 0 OR sz < 0 OR sx >= GRID_X OR sz >= GRID_Z) RETURN 0; END
    idx = sz * GRID_X + sx;
    IF (ent_of[idx] == -1) RETURN 0; END
    g3d_model_despawn(ent_of[idx]);
    ent_of[idx] = -1;
    mdl_of[idx] = -1;
    loaded_n--;
    RETURN 1;
END

// Carga los 1968 de golpe
FUNCTION load_all()
PRIVATE
    int sx; int sz;
BEGIN
    FOR (sz = 0; sz < GRID_Z; sz++)
        FOR (sx = 0; sx < GRID_X; sx++)
            sector_load(sx, sz);
        END
    END
END

FUNCTION unload_all()
PRIVATE
    int sx; int sz;
BEGIN
    FOR (sz = 0; sz < GRID_Z; sz++)
        FOR (sx = 0; sx < GRID_X; sx++)
            sector_unload(sx, sz);
        END
    END
END

// Streaming: deja puestos solo los sectores dentro de STREAM_R de la camara
FUNCTION stream_update()
PRIVATE
    int cx; int cz; int sx; int sz; int dx; int dz;
BEGIN
    cx = (camx - ORIGIN_X) / CELL;
    cz = (camz - ORIGIN_Z) / CELL;
    FOR (sz = 0; sz < GRID_Z; sz++)
        FOR (sx = 0; sx < GRID_X; sx++)
            dx = sx - cx; IF (dx < 0) dx = -dx; END
            dz = sz - cz; IF (dz < 0) dz = -dz; END
            IF (dx <= STREAM_R AND dz <= STREAM_R)
                sector_load(sx, sz);
            ELSE
                sector_unload(sx, sz);
            END
        END
    END
END

PROCESS main()
PRIVATE
    float msens; float spd; float yaw; float pitch; float dt;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    int tprev; int tnow; int t0; int i;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(0, 0);

    scene = g3d_scene_create("tr");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    // Interiores de cueva/tumba: luz suave y bastante ambiente, si no
    // no se ve nada dentro.
    light = g3d_light_create(0, 1.0, 0.95, 0.85);
    g3d_light_set_direction(light, -0.4, -0.7, -0.3);
    g3d_light_set_intensity(light, 0.8);
    g3d_set_ambient_light(0.45, 0.45, 0.50, 1.0);
    g3d_sky_set_gradient(0.10, 0.10, 0.14,  0.25, 0.22, 0.20); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.1);
    g3d_set_shadows(0);
    g3d_set_culling(1);

    write(0, 480, 340, 4, "Cargando 1968 sectores...");
    frame;

    // La tabla arranca a 0 y 0 ES un id de entidad valido: hay que marcar
    // "vacio" con -1 o el sector 0 se recargaria en bucle.
    FOR (i = 0; i < GRID_X * GRID_Z; i++)
        ent_of[i] = -1; mdl_of[i] = -1;
    END

    t0 = timer[0];
    mode = 1;
    load_all();
    t0 = timer[0] - t0;

    // Centro del mapa, a media altura (manifest: 48x4x45 desde [16.4, 0, 53.2])
    camx = ORIGIN_X + (GRID_X * CELL) / 2.0;
    camy = 3.0;
    camz = ORIGIN_Z + (GRID_Z * CELL) / 2.0;
    yaw = 0.0; pitch = -10.0; msens = 0.18;
    x = 640; y = 360;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "TOMB RAIDER - sectores.  F1 = carga total   F2 = streaming   ESC salir");
    write(0, 10, 30, 0, "modo (1=total 2=stream):"); write_var(0, 220, 30, 0, mode);
    write(0, 10, 50, 0, "sectores cargados:"); write_var(0, 170, 50, 0, loaded_n);
    write(0, 300, 50, 0, "de 1968");
    write(0, 10, 70, 0, "carga inicial (cs):"); write_var(0, 180, 70, 0, t0);
    write(0, 10, 94, 0, "FPS:"); write_var(0, 60, 94, 0, frame_info.fps);
    write(0, 10, 690, 0, "Cada sector = 1 draw call. 1968 draws para 15k triangulos: mira los FPS.");

    tprev = timer[0];

    LOOP
        tnow = timer[0];
        dt = (tnow - tprev) / 100.0;
        tprev = tnow;
        IF (dt > 0.1) dt = 0.1; END
        IF (dt <= 0.0) dt = 0.001; END

        // Velocidad por SEGUNDO. El mapa mide 48 unidades: se cruza en ~6s.
        spd = 8.0 * dt;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 25.0 * dt; END
        IF (key(_CONTROL)) spd = 60.0 * dt; END

        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens; pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 89.0) pitch = 89.0; END IF (pitch < -89.0) pitch = -89.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000); fwy = sin(pitch * 1000); fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        IF (key(_F1) AND kf1 == 0)
            IF (mode != 1) mode = 1; load_all(); END
        END
        kf1 = key(_F1);
        IF (key(_F2) AND kf2 == 0)
            IF (mode != 2) mode = 2; unload_all(); END
        END
        kf2 = key(_F2);

        IF (mode == 2) stream_update(); END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
