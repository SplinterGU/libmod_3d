// ============================================================================
//  TEST_BISTRO.PRG - Visor libre de BistroExterior.glb (escena Bistro, ~941 MB).
//  Carga el modelo completo y te deja volar por la escena.
//  La camara/velocidad se ajustan solas al tamano real del modelo.
//
//  WASD + raton = volar,  SHIFT = rapido,  CTRL = turbo,  ESC = salir.
//  1 sombras   2 niebla   3 culling
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light; int model; int ent;
    int fshadow; int ffog; int fcull;
    int k1; int k2; int k3;
    float camx; float camy; float camz;
    float mh;   // altura del modelo (para escalar camara/velocidad)
END

PROCESS main()
PRIVATE
    float dt; float msens; float spd; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    float atime; int fanim; int k4; int nanim; int fsmaa; int k5;
BEGIN
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(60, 0);

    scene = g3d_scene_create("bistro");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    // luz suave y ambiente alto (evita el contraste duro suelo/paredes)
    light = g3d_light_create(0, 1.0, 0.98, 0.92);
    g3d_light_set_direction(light, -0.55, -0.35, -0.5);   // sol bajo (tarde)
    g3d_light_set_intensity(light, 0.7);
    g3d_set_ambient_light(0.55, 0.58, 0.64, 1.0);
    g3d_sky_set_gradient(0.45, 0.60, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(0.95);

    fshadow = 0; ffog = 0; fcull = 1;
    g3d_set_shadows(0);
    g3d_set_culling(1);

    write(0, 620, 520, 4, "Cargando bistro.gltf (95 MB + texturas), paciencia...");
    frame;
    model = g3d_load_gltf("assets/niagara_bistro-master/bistro.gltf");
    ent   = g3d_model_spawn(scene, model, 0.0, 0.0, 0.0, 0.0, 0.0);  // escala nativa

    // EXTENSION real del modelo (ancho/alto/fondo), no solo la altura: Bistro es
    // muy ancho y plano, asi la camara se coloca fuera y encuadra toda la escena.
    mh = g3d_model_size(model);
    IF (mh < 1.0) mh = 1.0; END

    // Animaciones del propio glTF (toldos, ventilador, cesta, luz...): ON por
    // defecto, tecla 4 para pausar. g3d_model_animate_all reproduce TODAS a la vez.
    nanim = g3d_model_anim_count(model);
    fanim = 1; atime = 0.0;
    fsmaa = 1; g3d_set_smaa(1);

    // LOD, niebla y camara/velocidad relativas al tamano real
    g3d_set_lod(mh * 3.0);
    g3d_set_fog(0, 0.80, 0.86, 0.94, mh * 1.5, mh * 3.0);

    // La escena es ANCHA y plana (poca altura): la DISTANCIA se escala al tamano,
    // pero la camara va a media altura mirando HACIA ABAJO a la escena (vista aerea
    // 3/4), si no queda por encima del modelo mirando al cielo.
    camx = 0.0; camy = mh * 0.35; camz = mh * 0.6;
    yaw = 180.0; pitch = -25.0; msens = 0.18; dt = 1.0 / 60.0;
    x = 960; y = 540;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "BISTRO - WASD+raton volar - SHIFT rapido - CTRL turbo - ESC salir");
    write(0, 10, 30, 0, "1 sombras  2 niebla  3 culling  4 animaciones  5 SMAA (antialias)");
    write(0, 520, 30, 0, "animaciones:"); write_var(0, 640, 30, 0, nanim);
    write(0, 700, 30, 0, "SMAA:"); write_var(0, 760, 30, 0, fsmaa);
    write(0, 10, 54, 0, "cam X:"); write_var(0,  60, 54, 0, camx);
    write(0, 200, 54, 0, "Y:");    write_var(0, 225, 54, 0, camy);
    write(0, 360, 54, 0, "Z:");    write_var(0, 385, 54, 0, camz);
    write(0, 1740, 1050, 4, "FPS:"); write_var(0, 1800, 1050, 4, frame_info.fps);

    LOOP
        spd = mh * 0.006;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = mh * 0.03; END
        IF (key(_CONTROL)) spd = mh * 0.1; END
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

        IF (key(_1) AND k1 == 0) fshadow = 1 - fshadow; g3d_set_shadows(fshadow); END
        k1 = key(_1);
        IF (key(_2) AND k2 == 0) ffog = 1 - ffog; g3d_set_fog(ffog, 0.80, 0.86, 0.94, mh * 12.0, mh * 28.0); END
        k2 = key(_2);
        IF (key(_3) AND k3 == 0) fcull = 1 - fcull; g3d_set_culling(fcull); END
        k3 = key(_3);
        IF (key(_4) AND k4 == 0) fanim = 1 - fanim; END
        k4 = key(_4);
        // 5 = SMAA on/off: mira los bordes de las farolas y las rejas
        IF (key(_5) AND k5 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k5 = key(_5);

        // Reproduce TODAS las animaciones del glTF a la vez (toldos, ventilador,
        // cesta que se balancea, luz que orbita). Se para con la tecla 4.
        IF (fanim AND nanim > 0)
            g3d_model_animate_all(model, atime, 1);
            atime = atime + dt;
        END

        IF (key_up(_F1)) set_fps(0,0); END
        IF (key_up(_F2)) set_fps(60,0); END

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
