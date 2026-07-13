// ============================================================================
//  TEST_GTA3.PRG - Prueba de RENDIMIENTO cargando un mapa completo en GLB.
//  Vuela libre por el mapa y mira los FPS. Toggles para aislar el coste de
//  cada sistema (sombras / LOD / niebla / culling).
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//  1 sombras   2 niebla   3 LOD   4 culling   0 reset camara
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    MAPH = 350.0;   // altura objetivo del mapa en unidades (el GLB es diminuto: 0.82)
END

GLOBAL
    int scene; int camera; int light; int model; int ent;
    int fshadow; int ffog; int flod; int fcull;
    int k1; int k2; int k3; int k4;
    float camx; float camy; float camz;
END

PROCESS main()
PRIVATE
    float dt; float msens; float spd; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
BEGIN
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(0, 0);

    scene = g3d_scene_create("gta3");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.97, 0.90);
    g3d_light_set_direction(light, -0.4, -0.8, -0.35);
    g3d_light_set_intensity(light, 1.5); g3d_light_enable_shadow(light, 1);
    g3d_set_ambient_light(0.42, 0.46, 0.52, 1.0);
    g3d_sky_set_gradient(0.45, 0.60, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.05);

    // Estado inicial: baseline crudo (sombras OFF, niebla OFF, LOD y culling ON)
    fshadow = 0; ffog = 0; flod = 1; fcull = 1;
    g3d_set_shadows(0);
    g3d_set_lod(MAPH * 2.0);                              // LOD automatico del motor
    g3d_set_culling(1);                                  // frustum culling
    g3d_set_fog(0, 0.70, 0.78, 0.88, MAPH * 4.0, MAPH * 9.0);

    // ---- carga del mapa completo (una sola entidad estatica) ----
    write(0, 640, 520, 4, "Cargando gta3_map.glb (48 MB)...");
    frame;
    model = g3d_load_gltf("assets/gta3_map.glb");
    ent   = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, MAPH, 0.0);

    camx = 0.0; camy = MAPH; camz = 0.0 - MAPH * 1.5;
    yaw = 0.0; pitch = -30.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    // ---- HUD (persistente) ----
    write(0, 10, 10, 0, "MAPA GTA3 - WASD+raton volar - SHIFT rapido - CTRL turbo - ESC salir");
    write(0, 10, 30, 0, "1 sombras  2 niebla  3 LOD  4 culling  0 reset");
    write(0, 10, 54, 0, "sombras:"); write_var(0,  80, 54, 0, fshadow);
    write(0, 150, 54, 0, "niebla:"); write_var(0, 215, 54, 0, ffog);
    write(0, 280, 54, 0, "LOD:");    write_var(0, 325, 54, 0, flod);
    write(0, 390, 54, 0, "cull:");   write_var(0, 440, 54, 0, fcull);
    write(0, 10, 78, 0, "cam X:"); write_var(0,  60, 78, 0, camx);
    write(0, 200, 78, 0, "Y:");    write_var(0, 225, 78, 0, camy);
    write(0, 360, 78, 0, "Z:");    write_var(0, 385, 78, 0, camz);
    write(0, 1740, 1050, 4, "FPS:"); write_var(0, 1800, 1050, 4, frame_info.fps);

    LOOP
        // camara libre (vuelo)
        spd = MAPH * 0.012;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = MAPH * 0.07; END
        IF (key(_CONTROL)) spd = MAPH * 0.28; END
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

        // toggles de diagnostico
        IF (key(_1) AND k1 == 0) fshadow = 1 - fshadow; g3d_set_shadows(fshadow); END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) ffog = 1 - ffog; g3d_set_fog(ffog, 0.70, 0.78, 0.88, 1500.0, 3500.0); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0)
            flod = 1 - flod;
            IF (flod) g3d_set_lod(600.0); ELSE g3d_set_lod(0.0); END
        END
        k3 = key(_3);
        IF (key(_4) AND k4 == 0) fcull = 1 - fcull; g3d_set_culling(fcull); END
        k4 = key(_4);
        IF (key(_0)) camx = 0.0; camy = MAPH; camz = 0.0 - MAPH * 1.5; END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
