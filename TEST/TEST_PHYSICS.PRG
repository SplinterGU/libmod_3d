// ============================================================================
//  TEST_PHYSICS.PRG - Rigid bodies al estilo BennuGD2: UN PROCESS por caja.
//  Cada caja es un proceso con su cuerpo fisico + modelo en locals y su bucle
//  FRAME. main() solo avanza el mundo (g3d_rigidbody_step) y renderiza.
//  WASD+raton = camara libre,  ESPACIO = lanzar una caja,  ESC = salir.
//  El C solo simula; el juego usa la filosofia de procesos de BennuGD.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
    int mbarrel;
    int quit;
END

// --- Una caja = un proceso autonomo (idiom BennuGD) ---
PROCESS crate(float px, float py, float pz, float ivx, float ivy, float ivz)
PRIVATE
    int body; int mdl;
BEGIN
    mdl  = g3d_model_spawn(scene, mbarrel, px, py, pz, 3.0, rand(0, 359) * 1000);
    body = g3d_rigidbody_create(px, py, pz, 1.2, 1.5, 1.2, 6.0);   // caja AABB, masa 6
    g3d_rigidbody_set_velocity(body, ivx, ivy, ivz);
    // tumbo al lanzar: eje perpendicular a la direccion (las que caen rectas no giran)
    g3d_rigidbody_set_angular_velocity(body, -ivz * 0.12, 0.0, ivx * 0.12);

    LOOP
        // sincronizar posicion Y orientacion con mi cuerpo (el step lo hace main)
        g3d_entity_set_position(mdl, g3d_rigidbody_render_x(body),
                                     g3d_rigidbody_render_y(body),
                                     g3d_rigidbody_render_z(body));
        g3d_entity_set_rotation(mdl, g3d_rigidbody_angle_x(body),
                                     g3d_rigidbody_angle_y(body),
                                     g3d_rigidbody_angle_z(body));
        IF (quit) BREAK; END
        FRAME;
    END
    g3d_rigidbody_destroy(body);
END

PROCESS main()
PRIVATE
    int light; int gid; int i; int ksp;
    float dt; float msens;
    float camx; float camy; float camz; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz; float spd;
    float sx; float sz; float gy; float ty; float tz;
BEGIN
    set_mode(960, 540);
    set_fps(60, 0);

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END

    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.93, 0.82);
    g3d_light_set_direction(light, -0.5, -0.7, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.30, 0.34, 0.42, 1.0);

    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.75, 0.80, 0.85);
    g3d_sky_set_clouds(0.5, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);

    mbarrel = g3d_load_gltf("assets/barrel.glb");

    // tierra firme
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz = 110.0 - i * 3.0;
        ty = g3d_scene_terrain_height(0.0, tz);
        IF (ty > 4.0) sz = tz; gy = ty; BREAK; END
    END

    g3d_physics_set_gravity(24.0);

    // torre de cajas que cae y se apila (cada una es un proceso)
    FOR (i = 0; i < 7; i = i + 1)
        crate(sx, gy + 6.0 + i * 3.4, sz, 0.0, 0.0, 0.0);
    END

    quit = 0;
    camx = sx - 22.0; camy = gy + 12.0; camz = sz - 22.0;
    yaw = 45.0; pitch = -18.0; msens = 0.18; dt = 1.0 / 60.0; spd = 1.2;

    x = 480; y = 270;
    write(0, 10, 10, 0, "WASD+raton mover camara - ESPACIO lanzar caja - ESC salir");
    write(0, 820, 10, 0, "FPS:"); write_var(0, 860, 10, 0, frame_info.fps);
    g3d_mouse_capture(1);

    LOOP
        // ---- avanzar la simulacion (una vez por frame) ----
        g3d_rigidbody_step(dt);

        // ---- camara libre ----
        g3d_mouse_update();
        yaw = yaw + g3d_mouse_dx() * msens;
        pitch = pitch - g3d_mouse_dy() * msens;
        IF (pitch > 85.0) pitch = 85.0; END
        IF (pitch < -85.0) pitch = -85.0; END
        fwx = sin(yaw * 1000) * cos(pitch * 1000);
        fwy = sin(pitch * 1000);
        fwz = cos(yaw * 1000) * cos(pitch * 1000);
        rgx = cos(yaw * 1000); rgz = -sin(yaw * 1000);
        IF (key(_W)) camx += fwx * spd; camy += fwy * spd; camz += fwz * spd; END
        IF (key(_S)) camx -= fwx * spd; camy -= fwy * spd; camz -= fwz * spd; END
        IF (key(_D)) camx += rgx * spd; camz += rgz * spd; END
        IF (key(_A)) camx -= rgx * spd; camz -= rgz * spd; END
        g3d_camera_set_position(camera, camx, camy, camz);
        g3d_camera_look_at(camera, camx + fwx, camy + fwy, camz + fwz, 0.0, 1.0, 0.0);

        // ---- lanzar una caja (nuevo proceso) con impulso hacia delante ----
        IF (key(_SPACE) AND ksp == 0)
            crate(camx + fwx * 3.0, camy + fwy * 3.0, camz + fwz * 3.0,
                  fwx * 26.0, fwy * 26.0 + 4.0, fwz * 26.0);
        END
        ksp = key(_SPACE);


        IF (key(_ESC)) quit = 1; BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
END
