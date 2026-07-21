// ============================================================================
//  TEST_SNAKE.PRG - Personaje jugable con TODAS las animaciones de Old_snake.
//
//  Base = TEST_CHARACTER (escena de playa + character controller + camara 3a
//  persona). Old_snake.fbx fusionado con 14 animaciones Mixamo (fbxanim).
//
//  ---- CONTROLES ----
//  WASD  andar      SHIFT  correr      ESPACIO  saltar      raton  camara
//  TAB   INSPECTOR de animaciones (LEFT/RIGHT recorren las 15, se ven en bucle)
//  R     dispara (Firing Rifle) y saca el RIFLE a la mano
//  ---- AJUSTE EN VIVO DEL RIFLE (para colocarlo en la mano) ----
//  I/K adelante-atras   J/L izq-der   U/O arriba-abajo   +/- escala   N/M giro
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    // Indices de animacion de old_snake_anim.fbx (0 = T-pose base, muerta):
    //  1 Crouching Idle  2 Falling Back Death  3 Firing Rifle  4 Idle
    //  5 Jump To Hang  6 Jump  7 Kick  8 Prone Forward  9 Punching_1
    //  10 Punching_2  11 Running  12 Shooting  13 Swimming To Edge  14 Walking
    A_IDLE = 4;
    A_WALK = 14;
    A_RUN  = 11;
    A_JUMP = 6;
    A_FIRE = 3;      // Firing Rifle -> saca el rifle
END

GLOBAL
    int scene; int camera;
END

PROCESS main()
PRIVATE
    int fsmaa; int k5; int light; int jmodel; int player; int ch; int i; int nanim;
    int grounded; int inspect; int ktab; int kl; int kr; int anim; int firing;
    int rifle; int rmodel;
    float dt; float t; float msens;
    float camyaw; float campitch; float camdist; float eyeh;
    float cfx; float cfy; float cfz; float tx; float ty; float tz;
    float mfx; float mfz; float mrx; float mrz;
    float wx; float wz; float wl; float spdwalk; float spdrun; float spd;
    float facing; float yawoff; float loco; float locotgt;
    float sx; float sz; float gy; float ty2; float tz2;
    float px; float py; float pz;
    // rifle pegado al hueso de la mano (API g3d_model_node_*)
    int hand; float mscale; int c7;
    float rox; float roy; float roz; float rscale; float ryaw;
    float nx; float ny; float nz; float a2; float wx2; float wz2;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(60, 0);

    scene = g3d_scene_load("demo_beach.scene");
    IF (scene < 0) say("ERROR cargando demo_beach.scene"); RETURN; END
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    light = g3d_light_create(0, 1.0, 0.93, 0.82);
    g3d_light_set_direction(light, -0.5, -0.7, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_light_enable_shadow(light, 1);
    g3d_set_shadows(1);
    g3d_set_ambient_light(0.30, 0.34, 0.42, 1.0);
    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.75, 0.80, 0.85);
    g3d_sky_set_clouds(0.5, 1.0);
    g3d_sky_enable(1);
    g3d_set_hdr(1); fsmaa = 1; g3d_set_smaa(1);
    g3d_set_tonemap(1); g3d_set_exposure(1.0); g3d_set_bloom(1, 0.30, 1.1);
    g3d_set_ssao(1, 0.5, 1.2);

    // tierra firme para aparecer
    sx = 0.0; sz = 60.0; gy = g3d_scene_terrain_height(0.0, 60.0);
    FOR (i = 0; i < 70; i = i + 1)
        tz2 = 110.0 - i * 3.0; ty2 = g3d_scene_terrain_height(0.0, tz2);
        IF (ty2 > 4.0) sz = tz2; gy = ty2; BREAK; END
    END

    // --- Personaje: Old_snake con 14 animaciones (esta DE PIE -> recenter normal) ---
    jmodel = g3d_load_fbx("assets/old_snake_anim.fbx");
    IF (jmodel < 0) say("ERROR cargando old_snake_anim.fbx"); RETURN; END
    nanim = g3d_model_anim_count(jmodel);
    say("old_snake_anim.fbx  animaciones=" + itoa(nanim));
    say("CHK1 fbx cargado");
    g3d_model_lock_root(jmodel, 1);
    player = g3d_model_spawn(scene, jmodel, sx, gy, sz, 3.0, 0.0);
    say("CHK2 snake spawn OK");

    // --- Rifle (glb con texturas). Se PEGA al hueso de la mano derecha (motor:
    //     g3d_model_node_find/_x/_y/_z). El offset/escala/giro se afinan en vivo. ---
    rmodel = g3d_load_gltf("assets/m16_assault_rifle.glb");
    say("CHK3 rifle cargado rmodel=" + itoa(rmodel));
    IF (rmodel >= 0) rifle = g3d_model_spawn(scene, rmodel, 0.0, -9999.0, 0.0, 0.0, 0.0); END
    say("CHK4 rifle spawn OK");
    hand = g3d_model_node_find(jmodel, "RightHand");     // hueso de la mano
    say("CHK5 hueso mano (RightHand) = " + itoa(hand));
    mscale = 3.0 / g3d_model_height(jmodel);             // misma escala que el spawn del personaje
    rox = 0.0; roy = 0.0; roz = 0.0; rscale = 1.0; ryaw = 0.0;   // ajustables en vivo

    g3d_physics_set_gravity(30.0);
    ch = g3d_char_create(sx, gy, sz, 0.6, 3.0);
    g3d_char_set_tuning(ch, 0.8, 46.0);

    camyaw = 180.0; campitch = -12.0; camdist = 14.0; eyeh = 1.9;
    msens = 0.18; dt = 1.0 / 60.0; t = 0.0; loco = 0.0;
    spdwalk = 12.0; spdrun = 26.0;
    facing = 180000.0; yawoff = 0.0;
    inspect = 0; anim = A_IDLE;

    x = 640; y = 360;
    // HUD (una vez, se autoactualiza)
    write(0, 10, 10, 0, "WASD andar - SHIFT correr - ESPACIO saltar - R disparar - raton camara - ESC");
    write(0, 10, 30, 0, "TAB inspector (LEFT/RIGHT recorren las 15 animaciones)");
    write(0, 10, 52, 0, "anim:");   write_var(0, 60, 52, 0, anim);
    write(0, 150, 52, 0, "de:");    write_var(0, 190, 52, 0, nanim);
    write(0, 300, 52, 0, "inspector:"); write_var(0, 390, 52, 0, inspect);
    write(0, 10, 74, 0, "RIFLE  I/K J/L U/O mueven  +/- escala  N/M giro");
    write(0, 10, 94, 0, "rx:"); write_var(0, 40, 94, 0, rox);
    write(0, 150, 94, 0, "ry:"); write_var(0, 180, 94, 0, roy);
    write(0, 290, 94, 0, "rz:"); write_var(0, 320, 94, 0, roz);
    write(0, 430, 94, 0, "esc:"); write_var(0, 470, 94, 0, rscale);
    write(0, 560, 94, 0, "gir:"); write_var(0, 600, 94, 0, ryaw);
    write(0, 1160, 700, 0, "FPS:"); write_var(0, 1210, 700, 0, frame_info.fps);

    g3d_mouse_capture(1);
    say("CHK6 antes del LOOP");

    LOOP
        // ---- camara orbital ----
        g3d_mouse_update();
        camyaw = camyaw + g3d_mouse_dx() * msens;
        campitch = campitch - g3d_mouse_dy() * msens;
        IF (campitch > 55.0) campitch = 55.0; END
        IF (campitch < -35.0) campitch = -35.0; END

        // ---- INSPECTOR de animaciones ----
        IF (key(_TAB) AND ktab == 0) inspect = 1 - inspect; END
        ktab = key(_TAB);
        IF (inspect)
            IF (key(_RIGHT) AND kr == 0) anim = anim + 1; IF (anim >= nanim) anim = 0; END t = 0.0; END
            kr = key(_RIGHT);
            IF (key(_LEFT) AND kl == 0) anim = anim - 1; IF (anim < 0) anim = nanim - 1; END t = 0.0; END
            kl = key(_LEFT);
        END

        // ---- ajuste en vivo del rifle ----
        IF (key(_I)) roz = roz + 2.0 * dt; END
        IF (key(_K)) roz = roz - 2.0 * dt; END
        IF (key(_J)) rox = rox - 2.0 * dt; END
        IF (key(_L)) rox = rox + 2.0 * dt; END
        IF (key(_U)) roy = roy + 2.0 * dt; END
        IF (key(_O)) roy = roy - 2.0 * dt; END
        IF (key(_Y)) rscale = rscale + 0.8 * dt; END
        IF (key(_H)) rscale = rscale - 0.8 * dt; IF (rscale < 0.02) rscale = 0.02; END
        IF (key(_N)) ryaw = ryaw - 120000.0 * dt; END
        IF (key(_M)) ryaw = ryaw + 120000.0 * dt; END

        // ---- intencion de movimiento relativa a la camara ----
        mfx = sin(camyaw * 1000); mfz = cos(camyaw * 1000);
        mrx = cos(camyaw * 1000); mrz = -sin(camyaw * 1000);
        wx = 0.0; wz = 0.0;
        IF (key(_W)) wx = wx + mfx; wz = wz + mfz; END
        IF (key(_S)) wx = wx - mfx; wz = wz - mfz; END
        IF (key(_A)) wx = wx + mrx; wz = wz + mrz; END
        IF (key(_D)) wx = wx - mrx; wz = wz - mrz; END
        spd = spdwalk; IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = spdrun; END
        wl = sqrt(wx * wx + wz * wz);
        IF (wl > 0.001)
            wx = wx / wl * spd; wz = wz / wl * spd;
            facing = atan2(wx, wz);
        END
        g3d_char_move(ch, wx, wz);
        IF (key(_SPACE)) g3d_char_jump(ch, 13.0); END
        g3d_char_update(ch, dt);
        IF (c7 == 0) say("  A char_update OK"); END

        px = g3d_char_x(ch); py = g3d_char_y(ch); pz = g3d_char_z(ch);

        // ---- colocar + orientar el personaje ----
        g3d_entity_set_position(player, px, py, pz);
        g3d_entity_set_rotation(player, 0.0, facing + yawoff, 0.0);

        // ---- ANIMACION ----
        grounded = g3d_char_grounded(ch);
        t = t + dt;
        firing = key(_R);
        IF (nanim > 0)
            IF (inspect)
                g3d_model_animate(jmodel, anim, t, 1);            // la seleccionada, en bucle
            ELSE
                IF (firing)
                    anim = A_FIRE; g3d_model_animate(jmodel, A_FIRE, t, 1);
                ELSE
                    IF (grounded == 0)
                        anim = A_JUMP; g3d_model_animate(jmodel, A_JUMP, t, 1);
                    ELSE
                        locotgt = 0.0; IF (wl > 0.001) locotgt = 1.0; END
                        loco = loco + (locotgt - loco) * 10.0 * dt;
                        IF (key(_L_SHIFT) OR key(_R_SHIFT))
                            anim = A_RUN; g3d_model_animate_blend(jmodel, A_WALK, t, A_RUN, t, loco, 1);
                        ELSE
                            anim = A_WALK; g3d_model_animate_blend(jmodel, A_IDLE, t, A_WALK, t, loco, 1);
                        END
                    END
                END
            END
        END

        IF (c7 == 0) say("  B animacion OK anim=" + itoa(anim)); END

        // ---- RIFLE pegado al HUESO de la mano (sigue la animacion) ----
        //   pos_mundo = pos_personaje + Ry(facing) * (escala * pos_hueso_modelo + offset)
        IF (rifle >= 0 AND hand >= 0 AND (anim == A_FIRE OR anim == 12))   // Firing Rifle / Shooting
            nx = g3d_model_node_x(jmodel, hand) * mscale + rox;
            ny = g3d_model_node_y(jmodel, hand) * mscale + roy;
            nz = g3d_model_node_z(jmodel, hand) * mscale + roz;
            a2 = facing + yawoff;                        // giro del personaje (milideg)
            wx2 =  nx * cos(a2) + nz * sin(a2);
            wz2 = -nx * sin(a2) + nz * cos(a2);
            g3d_entity_set_position(rifle, px + wx2, py + ny, pz + wz2);
            g3d_entity_set_rotation(rifle, 0.0, a2 + ryaw, 0.0);
            g3d_entity_set_scale(rifle, rscale, rscale, rscale);
        END
        IF (rifle >= 0 AND (hand < 0 OR (anim <> A_FIRE AND anim <> 12)))
            g3d_entity_set_position(rifle, 0.0, -9999.0, 0.0);           // escondido
        END

        IF (c7 == 0) say("  C rifle OK"); END

        // ---- camara 3a persona ----
        cfx = sin(camyaw * 1000) * cos(campitch * 1000);
        cfy = sin(campitch * 1000);
        cfz = cos(camyaw * 1000) * cos(campitch * 1000);
        tx = px; ty = py + eyeh; tz = pz;
        g3d_camera_set_position(camera, tx - cfx * camdist, ty - cfy * camdist, tz - cfz * camdist);
        g3d_camera_look_at(camera, tx, ty, tz, 0.0, 1.0, 0.0);

        IF (key(_5) AND k5 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k5 = key(_5);

        IF (c7 == 0) say("  D camara OK, entrando a FRAME"); END
        IF (key(_ESC)) BREAK; END
        FRAME;
        IF (c7 == 0) say("CHK7 primer frame renderizado OK"); c7 = 1; END
    END
    g3d_mouse_capture(0);
END
