// ============================================================================
//  TEST_CHARACTER.PRG - Personaje jugable en 3a persona con animacion FBX.
//  WASD = andar,  SHIFT = correr,  ESPACIO = saltar,  raton = orbitar camara.
//  El personaje mira hacia donde anda y mezcla idle<->andar<->correr (blending).
//  Valida la animacion esqueletal FBX + el character controller de fisica.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene;
    int camera;
END

PROCESS main()
PRIVATE
    int fsmaa; int k5;
    int light; int jmodel; int player; int ch; int i; int nanim;
    int aidle; int awalk; int ajump; int grounded;
    float dt; float t; float msens;
    float camyaw; float campitch; float camdist; float eyeh;
    float cfx; float cfy; float cfz; float tx; float ty; float tz;
    float mfx; float mfz; float mrx; float mrz;
    float wx; float wz; float wl; float spdwalk; float spdrun; float spd;
    float facing; float yawoff;
    float loco; float locotgt; int seg; float bw; int a0; int a1;
    float sx; float sz; float gy; float ty2; float tz2;
    float px; float py; float pz;
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
    g3d_sky_set_low_clouds(0.5, 60.0, 26.0, 1.0);
    g3d_sky_enable(1);

    g3d_set_hdr(1);
    fsmaa = 1; g3d_set_smaa(1);   // 5 = on/off, para juzgar el antialias en la silueta
    g3d_set_tonemap(1);
    g3d_set_exposure(1.0);
    g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);
    g3d_water_set_ssr(1, 0.6);
    g3d_water_set_ocean(0.0, 1.0, 1.2);

    // tierra firme para aparecer
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz2 = 110.0 - i * 3.0;
        ty2 = g3d_scene_terrain_height(0.0, tz2);
        IF (ty2 > 4.0) sz = tz2; gy = ty2; BREAK; END
    END

    // --- Personaje: modelo + esqueleto (crash.glb -> Idle/Jump/Walk) ---
    jmodel = g3d_load_gltf("assets/crash.glb");
    nanim = g3d_model_anim_count(jmodel);
    aidle = 0; ajump = 1; awalk = 2;                // orden real de crash.glb
    say("crash.glb  submallas=" + itoa(g3d_model_submesh_count(jmodel)) + "  animaciones=" + itoa(nanim));
    g3d_model_lock_root(jmodel, 1);                 // anima en el sitio; la fisica lo mueve
    player = g3d_model_spawn(scene, jmodel, sx, gy, sz, 3.0, 0.0);

    g3d_physics_set_gravity(30.0);
    ch = g3d_char_create(sx, gy, sz, 0.6, 3.0);
    g3d_char_set_tuning(ch, 0.8, 46.0);

    camyaw = 180.0; campitch = -12.0; camdist = 14.0; eyeh = 1.9;
    msens = 0.18; dt = 1.0 / 60.0; t = 0.0;
    spdwalk = 12.0; spdrun = 26.0;
    facing = 180000.0; yawoff = 0.0;   // si mira al lado, ajusta yawoff (90000 / 180000 / -90000)
    loco = 0.0;

    x = 480; y = 270;
    write(0, 10, 10, 0, "WASD andar - SHIFT correr - ESPACIO saltar - raton camara - ESC salir");
    write(0, 10, 42, 0, "5 = SMAA (antialias):"); write_var(0, 200, 42, 0, fsmaa);
    write(0, 10, 26, 0, "animaciones:"); write_var(0, 150, 26, 0, nanim);
    write(0, 820, 10, 0, "FPS:"); write_var(0, 860, 10, 0, frame_info.fps);

    g3d_mouse_capture(1);

    LOOP
        // ---- camara orbital ----
        g3d_mouse_update();
        camyaw = camyaw + g3d_mouse_dx() * msens;
        campitch = campitch - g3d_mouse_dy() * msens;
        IF (campitch > 55.0) campitch = 55.0; END
        IF (campitch < -35.0) campitch = -35.0; END

        // ---- intencion de movimiento relativa a la camara ----
        mfx = sin(camyaw * 1000); mfz = cos(camyaw * 1000);
        mrx = cos(camyaw * 1000); mrz = -sin(camyaw * 1000);
        wx = 0.0; wz = 0.0;
        IF (key(_W)) wx = wx + mfx; wz = wz + mfz; END
        IF (key(_S)) wx = wx - mfx; wz = wz - mfz; END
        IF (key(_D)) wx = wx + mrx; wz = wz + mrz; END
        IF (key(_A)) wx = wx - mrx; wz = wz - mrz; END

        spd = spdwalk;
        IF (key(_L_SHIFT)) spd = spdrun; END

        wl = sqrt(wx * wx + wz * wz);
        IF (wl > 0.001)
            wx = wx / wl * spd; wz = wz / wl * spd;
            facing = atan2(wx, wz);    // ya en mili-grados
        END
        g3d_char_move(ch, wx, wz);
        IF (key(_SPACE)) g3d_char_jump(ch, 13.0); END
        g3d_char_update(ch, dt);

        px = g3d_char_x(ch); py = g3d_char_y(ch); pz = g3d_char_z(ch);

        // ---- colocar + orientar el modelo ----
        g3d_entity_set_position(player, px, py, pz);
        g3d_entity_set_rotation(player, 0.0, facing + yawoff, 0.0);

        // ---- animacion: salto en el aire, si no idle<->andar mezclado ----
        grounded = g3d_char_grounded(ch);
        t = t + dt;
        IF (nanim > 0)
            IF (grounded == 0)
                g3d_model_animate(jmodel, ajump, t, 1);        // en el aire
            ELSE
                locotgt = 0.0; IF (wl > 0.001) locotgt = 1.0; END
                loco = loco + (locotgt - loco) * 10.0 * dt;    // 0=idle .. 1=andar
                g3d_model_animate_blend(jmodel, aidle, t, awalk, t, loco, 1);
            END
        END

        // ---- camara en 3a persona ----
        cfx = sin(camyaw * 1000) * cos(campitch * 1000);
        cfy = sin(campitch * 1000);
        cfz = cos(camyaw * 1000) * cos(campitch * 1000);
        tx = px; ty = py + eyeh; tz = pz;
        g3d_camera_set_position(camera, tx - cfx * camdist, ty - cfy * camdist, tz - cfz * camdist);
        g3d_camera_look_at(camera, tx, ty, tz, 0.0, 1.0, 0.0);

        
        

        IF (key(_5) AND k5 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k5 = key(_5);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    
END
