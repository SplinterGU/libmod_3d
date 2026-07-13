// ============================================================================
//  TEST_OPENWORLD.PRG - Mundo abierto PROCEDURAL e interminable (streaming de
//  terreno). Cada tile genera su malla de terreno con ruido (g3d_worldgen_*) y
//  se carga/descarga en anillos alrededor de la camara. Floating origin + LOD +
//  niebla para volar sin limites y sin temblores ni pop-out.
//
//  WASD + raton = volar,  SHIFT = rapido,  ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    TILE = 120;     // unidades de mundo por tile de terreno
    RES  = 24;      // resolucion de la malla por tile (24x24)
    RAD  = 8;       // radio de streaming -> (2*8+1)^2 = 289 tiles, ~960u alcance
END

GLOBAL
    int scene; int camera; int quit;
    int terra_tex;
    int t_used[899]; int t_tx[899]; int t_tz[899]; int t_ent[899];
    int loaded;
    float wox; float woz;   // floating origin
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

FUNCTION load_tile(int tx, int tz)
PRIVATE int i;
BEGIN
    i = find_free();
    IF (i < 0) RETURN; END
    t_used[i] = 1; t_tx[i] = tx; t_tz[i] = tz;
    // malla de terreno en la posicion RENDERIZADA (mundo - offset del floating origin)
    t_ent[i] = g3d_worldgen_tile(scene, tx, tz, TILE, RES, tx * TILE - wox, tz * TILE - woz, terra_tex);
END

FUNCTION unload_tile(int tx, int tz)
PRIVATE int i;
BEGIN
    i = find_tile(tx, tz);
    IF (i < 0) RETURN; END
    g3d_worldgen_tile_free(t_ent[i]);
    t_used[i] = 0;
END

PROCESS main()
PRIVATE
    int light; int i; int n;
    float dt; float msens; float spd;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
BEGIN
    set_mode(1920, 1080);
    set_fps(0, 0);

    scene = g3d_scene_create("world");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.96, 0.86);
    g3d_light_set_direction(light, -0.45, -0.75, -0.35);
    g3d_light_set_intensity(light, 1.6); g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1); g3d_set_ambient_light(0.40, 0.44, 0.50, 1.0);
    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.82, 0.88, 0.96); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.1);
    g3d_set_lod(1500.0);   // solo el terreno muy lejano se simplifica
    g3d_set_fog(1, 0.80, 0.86, 0.94, 650.0, 950.0);   // funde el borde de streaming

    // forma del mundo: semilla, amplitud (altura), frecuencia (tamano de features), nivel del mar
    g3d_worldgen_set(2337, 150.0, 0.0025, 0.0);
    g3d_worldgen_set_water_depth(2.2);    // fondo oceanico mas hondo -> agua mas profunda

    // texturas por bioma (arena/hierba/roca/nieve). Pon 0 en una banda para
    // dejarla en color plano procedural (aqui la nieve va en color).
    g3d_worldgen_set_biome_textures(
        g3d_load_texture("assets/Sand.png"),
        g3d_load_texture("assets/grass.png"),
        g3d_load_texture("assets/terrain.png"),
        0, 0.05);

    // mar: plano grande centrado en el origen (con floating origin la camara
    // nunca se aleja mas de 3000, asi que 12000 cubre hasta la niebla)
    g3d_water_create(0.0, 12000.0, 200);
    g3d_water_set_ocean(1.0, 0.3, 1.4);          // swell: sube el 3er valor para mar picado
    g3d_water_set_waves(0.30, 8.0, 1.1);         // amplitud, longitud, velocidad (mar calmado)
    g3d_water_set_color(0.02, 0.11, 0.20,  0.10, 0.34, 0.44);

    g3d_stream_init(TILE, RAD);

    camx = 0.0; camy = 200.0; camz = 0.0;
    yaw = 0.0; pitch = -22.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "MUNDO PROCEDURAL - WASD+raton volar - SHIFT rapido - ESC salir");
    write(0, 10, 30, 0, "tiles de terreno:"); write_var(0, 70, 30, 0, loaded);
    write(0, 10, 48, 0, "pos mundo X:");       write_var(0, 130, 48, 0, wox);
    write(0, 1770, 1052, 0, "FPS:"); write_var(0, 1815, 1052, 0, frame_info.fps);

    LOOP
        // floating origin
        IF (camx > 3000.0 OR camx < -3000.0 OR camz > 3000.0 OR camz < -3000.0)
            g3d_world_rebase(camx, 0.0, camz);
            wox = wox + camx; woz = woz + camz;
            camx = 0.0; camz = 0.0;
        END

        // streaming de terreno con la posicion real del mundo
        g3d_stream_update(camx + wox, camz + woz);
        n = g3d_stream_load_count();
        FOR (i = 0; i < n; i = i + 1) load_tile(g3d_stream_load_x(i), g3d_stream_load_z(i)); END
        n = g3d_stream_unload_count();
        FOR (i = 0; i < n; i = i + 1) unload_tile(g3d_stream_unload_x(i), g3d_stream_unload_z(i)); END
        loaded = g3d_stream_loaded_count();

        // camara libre (vuelo)
        spd = 3.0; IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 12.0; END
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens; pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000); fwy = sin(pitch * 1000); fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        IF (camy < 5.0) camy = 5.0; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
