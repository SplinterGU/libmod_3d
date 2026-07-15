// ============================================================================
//  TEST_FLYBENCH.PRG - Banco de pruebas REAL: la camara recorre todo el Bistro
//  y guarda los FPS en un .txt desde el propio BennuGD.
//
//  Se ejecuta EN VENTANA, se ve el recorrido, y al final escribe:
//      bench_result.txt
//
//  Hace 2 vueltas al mismo recorrido: occlusion OFF y occlusion ON, para que
//  la comparacion sea exactamente la misma camara.
//  El chunking es de carga (no se puede cambiar en caliente): se controla con
//  la constante CHUNK -> lanzalo dos veces (0.0 y -1.0) y compara los .txt.
//
//  ESC = abortar.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    CHUNK    = -1.0;    // 0.0 = sin trocear (defecto actual),  -1.0 = trocear auto
    RESW     = 1920;   // resolucion: prueba 1280x720 y luego 640x360
    RESH     = 1080;    //   si los FPS apenas cambian -> NO estas limitado por pixeles
    NWP      = 8;      // puntos del recorrido
    SEGFRAMES = 150;   // frames por tramo
END

GLOBAL
    int scene; int camera; int light; int model; int ent;
    float camx; float camy; float camz;
    float wpx[8]; float wpy[8]; float wpz[8];
    int fp;
    float chunkv;
    string outname;
END

// Media de FPS de una vuelta completa al recorrido, escribiendo cada tramo al txt
FUNCTION float run_lap(int occ, int measure)
PRIVATE
    int w; int n; int t0; int ms; int total_frames; int total_ms;
    float ax; float ay; float az; float bx; float by; float bz;
    float t; float fps; float lx; float ly; float lz;
BEGIN
    g3d_set_occlusion(occ);
    total_frames = 0; total_ms = 0;

    FOR (w = 0; w < NWP; w++)
        ax = wpx[w];              ay = wpy[w];              az = wpz[w];
        bx = wpx[(w + 1) % NWP];  by = wpy[(w + 1) % NWP];  bz = wpz[(w + 1) % NWP];

        // Direccion de avance normalizada: se mira SIEMPRE hacia delante a una
        // distancia fija. Mirar al waypoint destino degenera al llegar a el
        // (la camara gira bruscamente en el ultimo frame de cada tramo).
        lx = bx - ax; ly = by - ay; lz = bz - az;
        t = sqrt(lx*lx + ly*ly + lz*lz);
        IF (t < 0.001) t = 1.0; END
        lx /= t; ly /= t; lz /= t;

        t0 = timer[0];
        FOR (n = 0; n < SEGFRAMES; n++)
            t = n / (SEGFRAMES * 1.0);
            camx = ax + (bx - ax) * t;
            camy = ay + (by - ay) * t;
            camz = az + (bz - az) * t;
            g3d_camera_set_position(camera, camx, camy, camz);
            g3d_camera_look_at(camera, camx + lx * 10.0, camy + ly * 10.0,
                                       camz + lz * 10.0, 0.0, 1.0, 0.0);
            FRAME;
            IF (key(_ESC)) RETURN 0.0; END
        END
        ms = timer[0] - t0;
        IF (ms <= 0) ms = 1; END
        fps = SEGFRAMES * 100.0 / ms;
        total_frames += SEGFRAMES;
        total_ms += ms;
        IF (measure AND fp)
            fputs(fp, "  tramo " + w + ": " + SEGFRAMES + " frames, " + ms + " cs, FPS = " + fps);
        END
    END

    IF (total_ms <= 0) total_ms = 1; END
    RETURN (total_frames * 100.0 / total_ms);
END

PROCESS main()
PRIVATE
    float fps_off; float fps_on; float mh;
BEGIN
    set_mode(RESW, RESH, MODE_WINDOW);
    set_fps(0, 0);          // sin limite: queremos medir de verdad

    scene = g3d_scene_create("flybench");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.98, 0.92);
    g3d_light_set_direction(light, -0.55, -0.35, -0.5);
    g3d_light_set_intensity(light, 0.7);
    g3d_set_ambient_light(0.55, 0.58, 0.64, 1.0);
    g3d_sky_set_gradient(0.45, 0.60, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(0.95);
    g3d_set_shadows(0);
    g3d_set_culling(1);

    write(0, 340, 340, 4, "Cargando bistro.gltf, paciencia...");
    frame;

    g3d_gltf_set_chunking(CHUNK);
    model = g3d_load_gltf("assets/niagara_bistro-master/bistro.gltf");
    ent   = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);
    mh = g3d_model_size(model);
    g3d_set_lod(mh * 3.0);

    // Recorrido a pie de calle por toda la escena (la escena mide ~158 de diagonal)
    wpx[0] = -20.0; wpy[0] = 6.0; wpz[0] =   1.5;
    wpx[1] =  -8.0; wpy[1] = 8.3; wpz[1] =  10.3;
    wpx[2] =  10.0; wpy[2] = 6.0; wpz[2] =  25.0;
    wpx[3] =  30.0; wpy[3] = 7.0; wpz[3] =  10.0;
    wpx[4] =  35.0; wpy[4] = 6.0; wpz[4] = -20.0;
    wpx[5] =  10.0; wpy[5] = 8.0; wpz[5] = -35.0;
    wpx[6] = -20.0; wpy[6] = 6.0; wpz[6] = -25.0;
    wpx[7] = -35.0; wpy[7] = 7.0; wpz[7] =  -5.0;

    chunkv = CHUNK;
    outname = "bench_result.txt";
    fp = fopen(outname, O_WRITE);

    write(0, 10, 10, 0, "FLYBENCH - recorriendo la escena y midiendo... ESC aborta");
    write(0, 10, 30, 0, "chunking:"); write_var(0, 100, 30, 0, chunkv);
    write(0, 10, 50, 0, "FPS:"); write_var(0, 50, 50, 0, frame_info.fps);

    // Vuelta de CALENTAMIENTO, descartada. La primera pasada paga la compilacion
    // de shaders, la captura del IBL y las subidas de textura: sin esto la
    // vuelta 1 (occlusion OFF) cargaria con ese coste y falsearia la comparacion
    // a favor del occlusion ON (medido: 92 FPS en el primer tramo vs 1500 luego).
    write(0, 10, 70, 0, "calentando (vuelta descartada)...");
    run_lap(1, 0);

    IF (fp)
        fputs(fp, "=== BISTRO FLYBENCH ===");
        fputs(fp, "resolucion: " + RESW + "x" + RESH);
        fputs(fp, "chunking (0=off, -1=auto): " + CHUNK);
        fputs(fp, "submeshes del modelo: " + g3d_model_submesh_count(model));
        fputs(fp, "");
        fputs(fp, "--- vuelta 1: OCCLUSION OFF ---");
    END
    fps_off = run_lap(0, 1);
    IF (fp) fputs(fp, "TOTAL occlusion OFF: FPS medio = " + fps_off); fputs(fp, ""); END

    IF (fp) fputs(fp, "--- vuelta 2: OCCLUSION ON ---"); END
    fps_on = run_lap(1, 1);
    IF (fp) fputs(fp, "TOTAL occlusion ON : FPS medio = " + fps_on); fputs(fp, ""); END

    IF (fp)
        fputs(fp, "=== RESUMEN ===");
        fputs(fp, "chunking     : " + CHUNK);
        fputs(fp, "occlusion OFF: " + fps_off);
        fputs(fp, "occlusion ON : " + fps_on);
        IF (fps_off > 0.0)
            fputs(fp, "ganancia occlusion: " + ((fps_on / fps_off - 1.0) * 100.0) + " %");
        END
        fclose(fp);
    END

    // Deja el resultado en pantalla
    write(0, 10, 100, 0, "LISTO -> bench_result.txt");
    write(0, 10, 120, 0, "occlusion OFF FPS:"); write_var(0, 190, 120, 0, fps_off);
    write(0, 10, 140, 0, "occlusion ON  FPS:"); write_var(0, 190, 140, 0, fps_on);
    write(0, 10, 170, 0, "ESC para salir");
    LOOP
        IF (key(_ESC)) BREAK; END
        FRAME;
    END
END
