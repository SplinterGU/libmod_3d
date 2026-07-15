// ============================================================================
//  TEST_FSR.PRG - FSR 1: renderiza a menos resolucion y escala a la pantalla.
//
//  La ventana va a 2K (2560x1440) y puedes cambiar EN CALIENTE a que resolucion
//  se renderiza la escena. Mira los FPS Y la calidad a la vez: ese es el trato.
//
//  Sitios buenos para juzgar la CALIDAD: las rejas del bistro, los cables de las
//  bombillas y las letras del cartel "Le Petit Coin". El detalle fino es donde
//  un upscaler espacial sufre.
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//
//    1 = NATIVO 2K (sin FSR)      <- la referencia de calidad
//    2 = render 1080p -> 2K       (FSR calidad,  ~56% de pixeles)
//    3 = render  960p -> 2K       (FSR balanceado, ~44%)
//    4 = render  720p -> 2K       (FSR rendimiento, 25%)
//    5 = SMAA on/off
//    Q / E = nitidez RCAS (0 = max filo, 2 = suave)
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light; int model; int ent;
    float camx; float camy; float camz;
    float mh;
    int rh;          // altura de render pedida (0 = nativo)
    float sharp;
    int fsmaa;
    int rw_real; int rh_real;
    int k1; int k2; int k3; int k4; int k5; int kq; int ke;
END

// Aplica el modo pedido: rh = 0 -> nativo, si no FSR renderizando a esa altura
FUNCTION apply_mode()
BEGIN
    IF (rh <= 0)
        g3d_set_fsr_height(0, 0, sharp);
    ELSE
        g3d_set_fsr_height(1, rh, sharp);
    END
END

PROCESS main()
PRIVATE
    float dt; float msens; float spd; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    float atime; int nanim; int tprev; int tnow;
BEGIN
    set_mode(2560, 1440, MODE_WINDOW);
    set_fps(0, 0);              // sin limite: queremos ver el coste real

    scene = g3d_scene_create("fsr");
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

    write(0, 1100, 700, 4, "Cargando bistro.gltf, paciencia...");
    frame;
    model = g3d_load_gltf("assets/niagara_bistro-master/bistro.gltf");
    ent   = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);
    mh = g3d_model_size(model);
    IF (mh < 1.0) mh = 1.0; END
    g3d_set_lod(mh * 3.0);
    nanim = g3d_model_anim_count(model);

    sharp = 0.25;               // el valor tipico de AMD
    rh = 0;                     // arranca en NATIVO: primero mira la referencia
    fsmaa = 1; g3d_set_smaa(1);
    apply_mode();

    // A pie de calle, mirando al bistro: aqui estan las rejas y los cables
    camx = -8.0; camy = 8.3; camz = 10.3;
    yaw = 200.0; pitch = -8.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 1280; y = 720;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "FSR - 1 nativo2K  2 render1080  3 render960  4 render720  5 SMAA  Q/E nitidez");
    write(0, 10, 30, 0, "render real:");
    write_var(0, 130, 30, 0, rw_real); write(0, 190, 30, 0, "x"); write_var(0, 205, 30, 0, rh_real);
    write(0, 300, 30, 0, "(pantalla 2560x1440)");
    write(0, 10, 50, 0, "nitidez RCAS:"); write_var(0, 140, 50, 0, sharp);
    write(0, 300, 50, 0, "SMAA:"); write_var(0, 360, 50, 0, fsmaa);
    write(0, 10, 74, 0, "FPS:"); write_var(0, 60, 74, 0, frame_info.fps);
    write(0, 10, 1400, 0, "Compara 1 (nativo) contra 4 (720p): mira las rejas y los cables");

    tprev = timer[0];

    LOOP
        rw_real = g3d_render_width();
        rh_real = g3d_render_height();

        // Velocidad POR SEGUNDO, no por frame. Sin esto, FSR duplica los FPS y
        // duplica la velocidad de vuelo: la escena se te viene encima al doble y
        // parece que "la imagen se amplia" al cambiar de resolucion. El bug no
        // estaba en el upscaler, estaba aqui.
        tnow = timer[0];
        dt = (tnow - tprev) / 100.0;        // timer[] va en centesimas
        tprev = tnow;
        IF (dt > 0.1) dt = 0.1; END         // un paron no debe teleportarte
        IF (dt <= 0.0) dt = 0.001; END

        spd = mh * 0.36 * dt;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = mh * 1.8 * dt; END
        IF (key(_CONTROL)) spd = mh * 6.0 * dt; END
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

        IF (key(_1) AND k1 == 0) rh = 0;    apply_mode(); END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) rh = 1080; apply_mode(); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0) rh = 960;  apply_mode(); END
        k3 = key(_3);
        IF (key(_4) AND k4 == 0) rh = 720;  apply_mode(); END
        k4 = key(_4);
        IF (key(_5) AND k5 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k5 = key(_5);

        // Nitidez del RCAS en caliente
        IF (key(_Q) AND kq == 0) sharp = sharp - 0.25; IF (sharp < 0.0) sharp = 0.0; END apply_mode(); END
        kq = key(_Q);
        IF (key(_E) AND ke == 0) sharp = sharp + 0.25; IF (sharp > 2.0) sharp = 2.0; END apply_mode(); END
        ke = key(_E);

        IF (nanim > 0) g3d_model_animate_all(model, atime, 1); atime = atime + dt; END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
