// ============================================================================
//  TEST_GTA3_STREAM.PRG - Carga de mapa por SECTORES (estilo GTA).
//
//  El motor NO carga el mapa entero: mantiene un anillo de sectores cargados
//  alrededor de la camara (g3d_stream_*). Al cruzar un limite de sector, entran
//  los nuevos y salen los lejanos. Adios al popping del mapa monolitico.
//
//  TU APORTAS los sectores como GLB en:  assets/sectors/sec_<X>_<Z>.glb
//  (X,Z = indice de sector entero, negativos incluidos). Cada sector se exporta
//  en las MISMAS coordenadas del mapa (mundo compartido) y se spawnea en (0,0,0):
//  su geometria ya cae en su sitio. Ajusta SEC al tamano real de tu rejilla.
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//  R / F = subir / bajar el nivel del mar (afinalo hasta que encaje con las islas).
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    SEC   = 512.0;   // unidades de mundo por sector (== sector_size del index.json)
    RAD   = 4;       // radio del anillo -> (2*4+1)^2 = 81 celdas (carga mas lejos = menos pop)
    SCALE = 1.0;     // escala uniforme aplicada a cada sector (si tu GLB es diminuto, subela)
    MAXS  = 512;     // slots maximos de sectores vivos
    SEA   = 0.0;     // nivel del mar inicial (Y del motor); afinalo en vivo con R/F
    WSIZE = 8000.0;  // tamano del plano de agua (cubre todo el mapa; centrado en origen)
END

GLOBAL
    int scene; int camera; int light;
    int t_used[512]; int t_tx[512]; int t_tz[512]; int t_root[512];
    int c_tx[512]; int c_tz[512]; int c_model[512]; int c_n;   // cache de modelos ya cargados
    int loaded; int misses;
    float camx; float camy; float camz;
    float sealevel; int kr; int kf;
    int fwater; int kg;
    int fcloud; int kc;
    int ffog; int kv;
    int fssao; int kb;
    int fhdr; int kh;
    float sunint; int ki; int kk;
    int fbcull; int kt;
END

FUNCTION int find_free()
PRIVATE int i;
BEGIN
    FOR (i = 0; i < MAXS; i = i + 1) IF (t_used[i] == 0) RETURN i; END END
    RETURN -1;
END

FUNCTION int find_sector(int tx, int tz)
PRIVATE int i;
BEGIN
    FOR (i = 0; i < MAXS; i = i + 1)
        IF (t_used[i] AND t_tx[i] == tx AND t_tz[i] == tz) RETURN i; END
    END
    RETURN -1;
END

// Devuelve el handle del modelo del sector, cargando el GLB de disco UNA sola vez.
// Reentrar un sector reusa el modelo en memoria -> spawn instantaneo, sin tirones.
FUNCTION int cache_model(int tx, int tz)
PRIVATE int i; string path;
BEGIN
    FOR (i = 0; i < c_n; i = i + 1)
        IF (c_tx[i] == tx AND c_tz[i] == tz) RETURN c_model[i]; END
    END
    path = "assets/sectors/sec_" + itoa(tx) + "_" + itoa(tz) + ".glb";
    IF (NOT fexists(path)) RETURN -1; END   // sector inexistente (mar/borde): no se cachea
    IF (c_n >= 512) RETURN -1; END
    i = c_n;
    c_tx[i] = tx; c_tz[i] = tz; c_model[i] = g3d_load_gltf(path); c_n = c_n + 1;
    RETURN c_model[i];
END

FUNCTION load_sector(int tx, int tz)
PRIVATE int i; int model;
BEGIN
    i = find_free();
    IF (i < 0) RETURN; END
    model = cache_model(tx, tz);
    IF (model < 0) misses = misses + 1; RETURN; END
    t_root[i] = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);  // escala nativa
    IF (SCALE <> 1.0) g3d_entity_set_scale(t_root[i], SCALE, SCALE, SCALE); END
    t_used[i] = 1; t_tx[i] = tx; t_tz[i] = tz;
END

FUNCTION unload_sector(int tx, int tz)
PRIVATE int i;
BEGIN
    i = find_sector(tx, tz);
    IF (i < 0) RETURN; END
    g3d_model_despawn(t_root[i]);
    t_used[i] = 0;
END

PROCESS main()
PRIVATE
    int i; int n;
    float dt; float msens; float spd; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
BEGIN
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(60, 0);

    scene = g3d_scene_create("gta3s");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.97, 0.90);
    g3d_light_set_direction(light, -0.62, -0.22, -0.52);  // sol BAJO (tarde): el suelo horizontal
                                                          // recibe poca luz -> sin linea en el horizonte
    sunint = 0.7;
    g3d_light_set_intensity(light, sunint);               // sol de tarde (ajustable en vivo con I/K)
    g3d_set_ambient_light(0.5, 0.53, 0.6, 1.0);           // ambiente medio: da relleno sin aplanar
    g3d_sky_set_gradient(0.45, 0.60, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(0.9);   // el suelo no se satura a blanco
    g3d_set_lod(0.0);        // sin LOD: solo cargamos sectores cercanos -> no hace falta
    g3d_set_culling(1);
    // niebla EXACTAMENTE del color del horizonte del cielo (el 2o triple de
    // sky_set_gradient) -> lo lejano (ciudad y mar) se disuelve en el cielo sin
    // linea de costura. Opaca ya a (RAD-1)*SEC -> el sector entra invisible -> sin pop.
    g3d_set_fog(1, 0.85, 0.90, 0.97, (RAD - 2.4) * SEC, (RAD - 1.0) * SEC);

    // Los sectores traen su geometria en coords de MUNDO absolutas: NO recentrar,
    // o cada sector se centraria en el origen y todos se apilarian.
    g3d_gltf_set_recenter(0);

    // Backface culling OFF por defecto: los arboles de GTA3 son quads cruzados de
    // una cara y con culling se ven a medias. La tecla T lo activa (util para no ver
    // el reverso espejado de las placas de una cara). El texto en si se arregla
    // RE-EXPORTANDO con la U volteada (--flip-u / GUI "Invertir U"), no con culling.
    fbcull = 0;
    g3d_set_backface_cull(0);

    g3d_stream_init(SEC, RAD);

    // Agua: un unico plano al nivel del mar. La tierra tapa el agua donde es mas
    // alta, y el mar asoma alrededor de las islas y en los huecos (como en GTA).
    // Centrado en el origen y enorme para cubrir todo el mapa desde cualquier punto.
    sealevel = SEA;
    fwater = 1; fcloud = 1; ffog = 1; fssao = 1; fhdr = 1;
    g3d_water_create(sealevel, WSIZE, 200);
    g3d_water_set_ocean(1.0, 0.3, 1.2);
    g3d_water_set_color(0.02, 0.10, 0.18,  0.08, 0.30, 0.40);

    // arrancar dentro de la ciudad: centro del sector (1,1), el mas poblado
    camx = 768.0; camy = 120.0; camz = 768.0;
    yaw = 0.0; pitch = -18.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "MAPA POR SECTORES - WASD+raton volar - SHIFT rapido - CTRL turbo - ESC salir");
    write(0, 10, 30, 0, "sectores cargados:"); write_var(0, 180, 30, 0, loaded);
    write(0, 10, 48, 0, "no encontrados:");     write_var(0, 150, 48, 0, misses);
    write(0, 10, 66, 0, "cam X:"); write_var(0, 60, 66, 0, camx);
    write(0, 200, 66, 0, "Z:");    write_var(0, 225, 66, 0, camz);
    write(0, 10, 84, 0, "nivel mar (R sube / F baja):"); write_var(0, 250, 84, 0, sealevel);
    write(0, 360, 84, 0, "agua (G on/off):"); write_var(0, 500, 84, 0, fwater);
    write(0, 600, 84, 0, "nubes (C on/off):"); write_var(0, 745, 84, 0, fcloud);
    write(0, 850, 84, 0, "niebla (V on/off):"); write_var(0, 1000, 84, 0, ffog);
    write(0, 1080, 84, 0, "ssao (B on/off):"); write_var(0, 1210, 84, 0, fssao);
    write(0, 1290, 84, 0, "hdr (H on/off):"); write_var(0, 1410, 84, 0, fhdr);
    write(0, 1500, 84, 0, "sol (I sube / K baja):"); write_var(0, 1660, 84, 0, sunint);
    write(0, 1740, 1050, 4, "FPS:"); write_var(0, 1800, 1050, 4, frame_info.fps);

    LOOP
        // --- STREAMING DE SECTORES ---
        g3d_stream_update(camx, camz);
        n = g3d_stream_load_count();
        FOR (i = 0; i < n; i = i + 1) load_sector(g3d_stream_load_x(i), g3d_stream_load_z(i)); END
        n = g3d_stream_unload_count();
        FOR (i = 0; i < n; i = i + 1) unload_sector(g3d_stream_unload_x(i), g3d_stream_unload_z(i)); END
        loaded = g3d_stream_loaded_count();

        // --- camara libre ---
        spd = SEC * 0.00015;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = SEC * 0.06; END
        IF (key(_CONTROL)) spd = SEC * 0.18; END
        g3d_mouse_update();
        yaw = yaw - g3d_mouse_dx() * msens; pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 89.0) pitch = 89.0; END IF (pitch < -89.0) pitch = -89.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000); fwy = sin(pitch * 1000); fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_A)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_D)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        // afinar el nivel del mar en vivo (recrea el plano al nuevo nivel)
        IF (key(_R) AND kr == 0) sealevel = sealevel + 1.0; g3d_water_create(sealevel, WSIZE, 200); g3d_water_set_ocean(1.0, 0.3, 1.2); END
        kr = key(_R);
        IF (key(_F) AND kf == 0) sealevel = sealevel - 1.0; g3d_water_create(sealevel, WSIZE, 200); g3d_water_set_ocean(1.0, 0.3, 1.2); END
        kf = key(_F);
        // G = enciende/apaga el agua (diagnostico de la linea del horizonte)
        IF (key(_G) AND kg == 0) fwater = 1 - fwater; g3d_water_set_enabled(fwater); END
        kg = key(_G);
        // C = enciende/apaga TODAS las nubes (cielo + pase volumetrico del renderer)
        IF (key(_C) AND kc == 0)
            fcloud = 1 - fcloud;
            IF (fcloud)
                g3d_sky_set_clouds(0.4, 1.0); g3d_sky_set_low_clouds(0.5, 300.0, 120.0, 1.0);
            ELSE
                g3d_sky_set_clouds(0.0, 0.0); g3d_sky_set_low_clouds(0.0, 300.0, 120.0, 1.0);
            END
        END
        kc = key(_C);
        // V = enciende/apaga la niebla
        IF (key(_V) AND kv == 0)
            ffog = 1 - ffog;
            g3d_set_fog(ffog, 0.85, 0.90, 0.97, (RAD - 2.4) * SEC, (RAD - 1.0) * SEC);
        END
        kv = key(_V);
        // B = enciende/apaga SSAO (oclusion ambiental en espacio-pantalla)
        IF (key(_B) AND kb == 0) fssao = 1 - fssao; g3d_set_ssao(fssao, 0.5, 1.2); END
        kb = key(_B);
        // H = HDR on/off (off = render base, sin post-proceso: tonemap/bloom/ssao/nubes)
        IF (key(_H) AND kh == 0) fhdr = 1 - fhdr; g3d_set_hdr(fhdr); END
        kh = key(_H);
        // I/K = sube/baja la intensidad del sol en vivo (baja hasta que la raya se vaya)
        IF (key(_I) AND ki == 0) sunint = sunint + 0.05; g3d_light_set_intensity(light, sunint); END
        ki = key(_I);
        IF (key(_K) AND kk == 0) sunint = sunint - 0.05; IF (sunint < 0.0) sunint = 0.0; END g3d_light_set_intensity(light, sunint); END
        kk = key(_K);
        // T = backface culling on/off (on = carteles solo de frente, sin reverso espejado)
        IF (key(_T) AND kt == 0) fbcull = 1 - fbcull; g3d_set_backface_cull(fbcull); END
        kt = key(_T);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
