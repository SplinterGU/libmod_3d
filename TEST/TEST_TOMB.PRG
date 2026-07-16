// ============================================================================
//  TEST_TOMB.PRG - Visor de un nivel de Tomb Raider (assets/tomb.glb).
//
//  El nivel entero es UN glb de 4 MB: 391 materiales sobre 7 texturas
//  compartidas, ~80 x 20 x 90 unidades. Carga instantanea, sin streaming.
//  (El mismo nivel exportado por sectores ocupaba 383 MB porque cada uno de
//   los 1968 .glb reembebia sus texturas.)
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//  1 = sombras   2 = niebla   3 = SMAA   4 = mas/menos luz ambiente
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light; int model; int ent;
    float camx; float camy; float camz;
    float mh; float amb;
    int fshadow; int ffog; int fsmaa;
    int k1; int k2; int k3; int k4;
END

PROCESS main()
PRIVATE
    float msens; float spd; float yaw; float pitch; float dt;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    int tprev; int tnow; int nsub;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(0, 0);

    scene = g3d_scene_create("tomb");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    // Interior de tumba: los niveles de TR llevan la luz pintada en la textura
    // (vertex/lightmap del 96), asi que aqui interesa AMBIENTE alto y un sol
    // suave. Con poca luz ambiente no se ve nada dentro.
    light = g3d_light_create(0, 1.0, 0.96, 0.88);
    g3d_light_set_direction(light, -0.4, -0.75, -0.3);
    g3d_light_set_intensity(light, 0.6);
    amb = 0.75;
    g3d_set_ambient_light(amb, amb, amb, 1.0);
    g3d_sky_set_gradient(0.08, 0.09, 0.13,  0.22, 0.20, 0.18); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.15);
    fsmaa = 1; g3d_set_smaa(1);
    fshadow = 0; ffog = 0;
    g3d_set_shadows(0);
    g3d_set_culling(1);

    write(0, 500, 340, 4, "Cargando tomb.glb...");
    frame;

    model = g3d_load_gltf("assets/tomb.glb");
    IF (model == -1)
        write(0, 400, 380, 4, "ERROR: no se pudo cargar assets/tomb.glb");
        LOOP IF (key(_ESC)) BREAK; END FRAME; END
        RETURN;
    END
    ent  = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);   // escala nativa
    mh   = g3d_model_size(model);
    IF (mh < 1.0) mh = 1.0; END
    nsub = g3d_model_submesh_count(model);
    g3d_set_lod(mh * 4.0);

    // El loader centra en X/Z y apoya en Y=0, asi que el nivel queda alrededor
    // del origen. Arrancamos en el centro a la altura de Lara.
    camx = 0.0; camy = mh * 0.10; camz = 0.0;
    yaw = 0.0; pitch = 0.0; msens = 0.18;
    x = 640; y = 360;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "TOMB RAIDER - WASD+raton volar - SHIFT rapido - CTRL turbo - ESC salir");
    write(0, 10, 30, 0, "1 sombras  2 niebla  3 SMAA  4 ambiente");
    write(0, 10, 52, 0, "submeshes:"); write_var(0, 100, 52, 0, nsub);
    write(0, 200, 52, 0, "ambiente:"); write_var(0, 290, 52, 0, amb);
    write(0, 400, 52, 0, "SMAA:"); write_var(0, 450, 52, 0, fsmaa);
    write(0, 10, 74, 0, "cam X:"); write_var(0,  60, 74, 0, camx);
    write(0, 180, 74, 0, "Y:");    write_var(0, 205, 74, 0, camy);
    write(0, 320, 74, 0, "Z:");    write_var(0, 345, 74, 0, camz);
    write(0, 1160, 690, 0, "FPS:"); write_var(0, 1210, 690, 0, frame_info.fps);

    tprev = timer[0];

    LOOP
        // Velocidad por SEGUNDO (no por frame): si no, a mas FPS vuelas mas rapido
        tnow = timer[0];
        dt = (tnow - tprev) / 100.0;
        tprev = tnow;
        IF (dt > 0.1) dt = 0.1; END
        IF (dt <= 0.0) dt = 0.001; END

        spd = mh * 0.15 * dt;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = mh * 0.5 * dt; END
        IF (key(_CONTROL)) spd = mh * 1.5 * dt; END

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

        IF (key(_1) AND k1 == 0) fshadow = 1 - fshadow; g3d_set_shadows(fshadow); END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) ffog = 1 - ffog; g3d_set_fog(ffog, 0.10, 0.10, 0.12, mh * 0.3, mh * 0.9); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k3 = key(_3);
        // 4 cicla el ambiente: los niveles de TR pueden quedar muy oscuros
        IF (key(_4) AND k4 == 0)
            amb = amb + 0.25; IF (amb > 1.25) amb = 0.25; END
            g3d_set_ambient_light(amb, amb, amb, 1.0);
        END
        k4 = key(_4);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
