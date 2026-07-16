// ============================================================================
//  TEST_TR.PRG - Carga un nivel ORIGINAL de Tomb Raider, sin conversion.
//
//  g3d_load_tr() lee el .PHD/.TR2/.tr4/.TRC tal cual: rooms, geometria y
//  texturas del propio nivel. De momento solo TR1 (.PHD).
//
//  Pon la ruta de tu nivel en LEVEL (abajo) y compila.
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//  1 = niebla   2 = SMAA   3 = mas/menos luz ambiente
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    LEVEL = "/home/ruben/Escritorio/viewer/Tomb-Raider-1-2-3-4-5-Map-viewer-and-levels-master/Tomb-Raider-2/00-Laras-Home.TR2";
END

GLOBAL
    int scene; int camera; int light; int model; int ent;
    float camx; float camy; float camz;
    float mh; float amb;
    int nrooms; int trver;
    int ffog; int fsmaa;
    int k1; int k2; int k3;
END

PROCESS main()
PRIVATE
    float msens; float spd; float yaw; float pitch; float dt;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    int tprev; int tnow;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(0, 0);

    scene = g3d_scene_create("tr");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    // Los niveles de TR llevan la luz PINTADA en las texturas (el lightmap del
    // 96), asi que aqui manda el ambiente: con poca luz no se ve nada dentro.
    light = g3d_light_create(0, 1.0, 0.96, 0.90);
    g3d_light_set_direction(light, -0.4, -0.75, -0.3);
    g3d_light_set_intensity(light, 0.5);
    amb = 0.9;
    g3d_set_ambient_light(amb, amb, amb, 1.0);
    g3d_sky_set_gradient(0.05, 0.05, 0.08,  0.15, 0.14, 0.12); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.2);
    fsmaa = 1; g3d_set_smaa(1);
    g3d_set_shadows(0);
    g3d_set_culling(1);

    trver = g3d_tr_probe(LEVEL);
    write(0, 480, 340, 4, "Cargando nivel de Tomb Raider...");
    frame;

    model = g3d_load_tr(LEVEL);
    IF (model == -1)
        write(0, 380, 380, 4, "ERROR: no se pudo cargar el nivel (mira la consola)");
        LOOP IF (key(_ESC)) BREAK; END FRAME; END
        RETURN;
    END
    ent    = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);
    nrooms = g3d_model_submesh_count(model);
    mh     = g3d_model_size(model);
    IF (mh < 1.0) mh = 1.0; END
    g3d_set_lod(mh * 4.0);

    // El loader centra el nivel en X/Z y lo apoya en Y=0
    camx = 0.0; camy = mh * 0.08; camz = 0.0;
    yaw = 0.0; pitch = 0.0; msens = 0.18;
    x = 640; y = 360;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "TOMB RAIDER nativo - WASD+raton - SHIFT rapido - CTRL turbo - ESC salir");
    write(0, 10, 30, 0, "1 niebla  2 SMAA  3 ambiente");
    write(0, 10, 52, 0, "TR version:"); write_var(0, 100, 52, 0, trver);
    write(0, 170, 52, 0, "rooms:");     write_var(0, 225, 52, 0, nrooms);
    write(0, 300, 52, 0, "ambiente:");  write_var(0, 390, 52, 0, amb);
    write(0, 10, 74, 0, "cam X:"); write_var(0,  60, 74, 0, camx);
    write(0, 180, 74, 0, "Y:");    write_var(0, 205, 74, 0, camy);
    write(0, 320, 74, 0, "Z:");    write_var(0, 345, 74, 0, camz);
    write(0, 1160, 690, 0, "FPS:"); write_var(0, 1210, 690, 0, frame_info.fps);

    tprev = timer[0];

    LOOP
        tnow = timer[0];
        dt = (tnow - tprev) / 100.0;
        tprev = tnow;
        IF (dt > 0.1) dt = 0.1; END
        IF (dt <= 0.0) dt = 0.001; END

        spd = mh * 0.12 * dt;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = mh * 0.4 * dt; END
        IF (key(_CONTROL)) spd = mh * 1.2 * dt; END

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

        IF (key(_1) AND k1 == 0) ffog = 1 - ffog; g3d_set_fog(ffog, 0.05, 0.05, 0.07, mh * 0.2, mh * 0.7); END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0)
            amb = amb + 0.25; IF (amb > 1.5) amb = 0.25; END
            g3d_set_ambient_light(amb, amb, amb, 1.0);
        END
        k3 = key(_3);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
