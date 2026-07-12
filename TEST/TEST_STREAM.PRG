// ============================================================================
//  TEST_STREAM.PRG - Streaming de mundo abierto (esqueleto sincrono).
//
//  El MOTOR gestiona la rejilla de tiles y los anillos (g3d_stream_*). El JUEGO
//  decide que hay en cada tile: aqui, 3 rocas por tile en posiciones
//  deterministas. Al volar, el motor dice que tiles ENTRAN (poblar) y SALEN
//  (liberar) del radio -> mundo "infinito" con memoria constante.
//
//  WASD + raton = volar,  SHIFT = rapido,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    TILE = 100;     // unidades de mundo por tile
    RAD  = 9;       // radio de carga -> (2*9+1)^2 = 361 tiles, ~900u de alcance
    NPT  = 3;       // rocas por tile
END

GLOBAL
    int scene; int camera; int quit;
    int mrock;
    // registro de tiles cargados: (tx,tz) + sus NPT entidades
    int t_used[899]; int t_tx[899]; int t_tz[899]; int t_ent[2699];
    int nrocks; int loaded;
    float wox; float woz;   // FLOATING ORIGIN: offset acumulado del mundo
END

FUNCTION int find_free()
PRIVATE int i;
BEGIN
    FOR (i = 0; i < 900; i = i + 1) IF (t_used[i] == 0) RETURN i; END END
    RETURN -1;
END

FUNCTION int find_tile(int tx, int tz)
PRIVATE int i;
BEGIN
    FOR (i = 0; i < 900; i = i + 1) IF (t_used[i] AND t_tx[i] == tx AND t_tz[i] == tz) RETURN i; END END
    RETURN -1;
END

// Puebla un tile: NPT rocas en posiciones deterministas (mismas al recargar).
FUNCTION load_tile(int tx, int tz)
PRIVATE int i; int k; float rx; float rz; float ry; int seed;
BEGIN
    i = find_free();
    IF (i < 0) RETURN; END
    t_used[i] = 1; t_tx[i] = tx; t_tz[i] = tz;
    FOR (k = 0; k < NPT; k = k + 1)
        seed = tx * 73856093 + tz * 19349663 + k * 83492791;
        // posicion RENDERIZADA = mundo real (tile) - offset del floating origin
        rx = tx * TILE + (abs(seed) % TILE) - wox;
        rz = tz * TILE + (abs(seed / 7) % TILE) - woz;
        ry = g3d_scene_terrain_height(rx, rz);
        t_ent[i * NPT + k] = g3d_model_spawn(scene, mrock, rx, ry, rz, 4.0 + (abs(seed / 13) % 4), (abs(seed / 3) % 360) * 1000.0);
        nrocks = nrocks + 1;
    END
END

FUNCTION unload_tile(int tx, int tz)
PRIVATE int i; int k;
BEGIN
    i = find_tile(tx, tz);
    IF (i < 0) RETURN; END
    FOR (k = 0; k < NPT; k = k + 1) g3d_model_despawn(t_ent[i * NPT + k]); nrocks = nrocks - 1; END
    t_used[i] = 0;
END

PROCESS main()
PRIVATE
    int light; int i; int n;
    float dt; float msens; float spd;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
BEGIN
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(0, 0);

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.96, 0.88);
    g3d_light_set_direction(light, -0.45, -0.75, -0.35);
    g3d_light_set_intensity(light, 1.5); g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1); g3d_set_ambient_light(0.34, 0.37, 0.44, 1.0);
    g3d_sky_set_gradient(0.32, 0.52, 0.85,  0.80, 0.86, 0.94); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.1);
    g3d_set_lod(250.0);   // el LOD automatico tambien aplica a las rocas streameadas
    // niebla al color del cielo: funde las rocas cerca del borde (~900u) -> sin pop-out feo
    g3d_set_fog(1, 0.78, 0.84, 0.92, 550.0, 850.0);

    mrock = g3d_load_gltf("assets/rock.glb");

    // ---- arrancar el streaming ----
    g3d_stream_init(TILE, RAD);

    camx = 0.0; camy = 45.0; camz = 0.0;
    yaw = 0.0; pitch = -20.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "STREAMING - WASD+raton volar (vuela lejos: las rocas cargan/descargan solas) - ESC salir");
    write(0, 10, 30, 0, "tiles cargados:"); write_var(0, 150, 30, 0, loaded);
    write(0, 10, 48, 0, "rocas vivas:");    write_var(0, 130, 48, 0, nrocks);
    write(0, 10, 66, 0, "offset mundo X (rebase):"); write_var(0, 220, 66, 0, wox);
    write(0, 10, 84, 0, "cam X (renderizada, pequena):"); write_var(0, 270, 84, 0, camx);
    write(0, 1770, 1052, 0, "FPS:"); write_var(0, 1815, 1052, 0, frame_info.fps);

    LOOP
        // 0) FLOATING ORIGIN: si la camara se aleja del origen, rebase -> el motor
        //    desplaza todo (rocas) de vuelta y acumulamos el offset. Coords siempre pequenas.
        IF (camx > 2000.0 OR camx < -2000.0 OR camz > 2000.0 OR camz < -2000.0)
            g3d_world_rebase(camx, 0.0, camz);   // mueve todas las entidades -camx,-camz
            wox = wox + camx; woz = woz + camz;
            camx = 0.0; camz = 0.0;
        END

        // 1) streaming con la posicion REAL del mundo (offset + camara renderizada)
        g3d_stream_update(camx + wox, camz + woz);
        // 2) poblar tiles que entran
        n = g3d_stream_load_count();
        FOR (i = 0; i < n; i = i + 1) load_tile(g3d_stream_load_x(i), g3d_stream_load_z(i)); END
        // 3) liberar tiles que salen
        n = g3d_stream_unload_count();
        FOR (i = 0; i < n; i = i + 1) unload_tile(g3d_stream_unload_x(i), g3d_stream_unload_z(i)); END
        loaded = g3d_stream_loaded_count();

        // ---- camara libre ----
        spd = 2.0; IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 8.0; END
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens; pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000); fwy = sin(pitch * 1000); fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
