// ============================================================================
//  TEST_TR.PRG - Nivel ORIGINAL de Tomb Raider con LARA jugable.
//
//  REHECHO partiendo de TEST_CHARACTER/TEST_LARA (que funcionan): misma camara,
//  mismo character controller, misma animacion. Lo UNICO que cambia respecto a
//  ellos es el escenario: en vez de una .scene con terreno, se carga un nivel
//  nativo de TR y cada room se anade como colision de malla a Jolt.
//
//  CLAVES aprendidas (no tocar sin motivo):
//   - g3d_fbx_set_recenter(0) ANTES de cargar a Lara: su pose BIND esta TUMBADA,
//     y el recentrado del loader la desplazaba 4.25 uds de su capsula (parecia
//     que atravesaba paredes y que la camara miraba a otro sitio).
//   - La capsula mide LO MISMO que el modelo (LARA_H), y el modelo va DIRECTO a
//     la posicion del char, sin offsets.
//
//  WASD andar - SHIFT correr - ESPACIO saltar - raton camara - N room - ESC salir
//  Ajuste en vivo:  Z/X camara cerca-lejos | C/V altura de mira | Q/E giro de Lara
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    // La casa de Lara (TR2). Otras que tambien valen (el loader detecta la version):
    //   .../Tomb-Raider-1/00-Laras-Home.PHD
    //   .../Tomb-Raider-3/00-Laras-Home.TR2
    //   .../Tomb-Raider-5/01-Streets-of-Rome.TRC
    LEVEL = "/home/ruben/Escritorio/viewer/Tomb-Raider-1-2-3-4-5-Map-viewer-and-levels-master/Tomb-Raider-2/02-Venice.TR2";
    LARA  = "assets/lara_trr.glb";     // Lara de TR Remastered (exportado del .blend)

    // Este modelo NO trae ciclos de animacion, solo POSES (1 frame cada una).
    // Fabricamos el movimiento con ellas: idle = Neutral Rest; correr = alternar
    // Run L <-> Run R con una onda seno; saltar = Jump.
    //  0 Crouch  1 Hanging  2 Hold Guns  3 Jump  4 Neutral Rest  5 Run L  6 Run R
    P_IDLE = 4;
    P_JUMP = 3;
    P_RUNL = 5;
    P_RUNR = 6;

    // Escala del mundo TR: el loader divide por 512, asi que 1 tile = 2 uds y
    // Lara (762 uds de TR) mide ~1.5. Todo lo demas se dimensiona con esto.
    LARA_H = 1.5;
END

GLOBAL
    int scene; int camera;
    // zonas de agua (superficie Y + huella X/Z) para detectar el nado
    int nwz;
    float wzy[64]; float wzx0[64]; float wzx1[64]; float wzz0[64]; float wzz1[64];
END

PROCESS main()
PRIVATE
    int light; int lvl; int lent; int jmodel; int player; int ch; int i;
    int nanim; int grounded; int nrooms; int trver; int room;
    int nwater; int wtex; float wx0; float wy0; float wz0; float wsize;
    int inwater; int wi; float wsurf; float hxr; float hzr; float swimpitch;
    int fsmaa; int k5; int kn;
    float dt; float t; float msens;
    float camyaw; float campitch; float camdist; float eyeh;
    float cfx; float cfy; float cfz; float tx; float ty; float tz;
    float mfx; float mfz; float mrx; float mrz;
    float wx; float wz; float wl; float spdwalk; float spdrun; float spd;
    float facing; float yawoff;
    float loco; float locotgt; float runphase; float runw;
    float sx; float sy; float sz;
    float px; float py; float pz;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(60, 0);

    scene = g3d_scene_create("tr");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create();
    g3d_camera_set_active(camera);

    // Los niveles de TR llevan la luz PINTADA en las texturas, asi que aqui manda
    // el ambiente: con poca luz no se ve nada dentro de las rooms.
    light = g3d_light_create(0, 1.0, 0.96, 0.90);
    g3d_light_set_direction(light, -0.4, -0.75, -0.3);
    g3d_light_set_intensity(light, 0.7);
    g3d_set_ambient_light(0.9, 0.9, 0.9, 1.0);
    g3d_sky_set_gradient(0.05, 0.05, 0.08,  0.15, 0.14, 0.12);
    g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.2);
    fsmaa = 1; g3d_set_smaa(1);
    g3d_set_shadows(1);
    g3d_set_culling(1);

    // --- ESCENARIO: nivel nativo de Tomb Raider (esto es lo unico distinto a
    //     TEST_CHARACTER, que carga una .scene con terreno) ---
    trver = g3d_tr_probe(LEVEL);
    say("Cargando nivel de Tomb Raider (TR" + itoa(trver) + ")...");
    frame;
    lvl = g3d_load_tr(LEVEL);
    IF (lvl == -1) say("ERROR: no se pudo cargar el nivel"); RETURN; END
    lent   = g3d_model_spawn(scene, lvl, 0.0, 0.0, 0.0, 0.0, 0.0);  // 1:1 en el origen
    nrooms = g3d_model_submesh_count(lvl);
    g3d_set_lod(g3d_model_size(lvl) * 4.0);

    // Colision del nivel: cada room como malla estatica en Jolt. El char
    // controller consulta ese mundo (ver libmod_3d_physics.c + libmod_3d_jolt.cpp).
    say("Construyendo colision de " + itoa(nrooms) + " rooms...");
    frame;
    FOR (i = 0; i < nrooms; i = i + 1)
        g3d_collider_add_mesh(lvl, i, 0.0, 0.0, 0.0, 1.0);
    END

    // --- AGUA: donde el nivel TR marca una room como agua, ponemos una PISCINA
    //     animada del motor (g3d_fluid_add: zona posicionada con olas/Fresnel).
    //     Superficie en el techo de la room (cima del AABB = cy+hy); cubre el
    //     ancho/fondo de la room; profundidad = alto de la room. Pueden coexistir
    //     varias (el nivel puede tener varias piscinas). ---
    g3d_fluid_clear();
    // agua de PISCINA: olas pequeñas y lentas, y MUY transparente (opacidad baja).
    //  (amp, largo, velocidad, color_profundo rgb, color_orilla rgb, opacidad)
    g3d_fluid_style(0.015, 3.0, 0.35,
                    0.12, 0.32, 0.38,
                    0.22, 0.45, 0.50,
                    0.30);
    nwater = 0; nwz = 0;
    FOR (i = 0; i < nrooms; i = i + 1)
        IF (g3d_model_submesh_is_water(lvl, i))
            wx0 = g3d_model_submesh_cx(lvl, i);
            wz0 = g3d_model_submesh_cz(lvl, i);
            wy0 = g3d_model_submesh_cy(lvl, i) + g3d_model_submesh_hy(lvl, i);
            hxr = g3d_model_submesh_hx(lvl, i);
            hzr = g3d_model_submesh_hz(lvl, i);
            // superficie (cx,cz), tamaño = ancho/fondo de la room, prof = alto room
            g3d_fluid_add(wx0, wz0, hxr * 2.0, hzr * 2.0, wy0,
                          g3d_model_submesh_hy(lvl, i) * 2.0);
            // guardar la zona para detectar el nado
            IF (nwz < 64)
                wzy[nwz]  = wy0;
                wzx0[nwz] = wx0 - hxr; wzx1[nwz] = wx0 + hxr;
                wzz0[nwz] = wz0 - hzr; wzz1[nwz] = wz0 + hzr;
                nwz = nwz + 1;
            END
            nwater = nwater + 1;
        END
    END
    say("rooms de agua detectadas: " + itoa(nwater));

    // --- Lara TRR: .glb con texturas embebidas. Su pose bind esta DE PIE, asi
    //     que el recentrado normal de glTF la posa bien (pies al suelo). ---
    jmodel = g3d_load_gltf(LARA);
    IF (jmodel < 0) say("ERROR: no se pudo cargar " + LARA); RETURN; END
    nanim = g3d_model_anim_count(jmodel);
    say("lara_animada.fbx  submallas=" + itoa(g3d_model_submesh_count(jmodel)) +
        "  animaciones=" + itoa(nanim));
    g3d_model_lock_root(jmodel, 1);     // anima en el sitio; la fisica la mueve

    // Aparecer en el centro de la room 0, algo elevada para caer al suelo real.
    room = 0;
    sx = g3d_model_submesh_cx(lvl, 0);
    sy = g3d_model_submesh_cy(lvl, 0) + LARA_H;
    sz = g3d_model_submesh_cz(lvl, 0);

    player = g3d_model_spawn(scene, jmodel, sx, sy, sz, LARA_H, 0.0);

    // La capsula mide LO MISMO que el modelo (como en TEST_CHARACTER).
    g3d_physics_set_gravity(30.0);
    ch = g3d_char_create(sx, sy, sz, LARA_H * 0.2, LARA_H);
    g3d_char_set_tuning(ch, LARA_H * 0.27, 46.0);

    // Camara: las proporciones de TEST_CHARACTER escaladas a LARA_H (alli es
    // camdist=14 / eyeh=1.9 para un personaje de 3.0). Ajustable con Z/X y C/V.
    camyaw = 180.0; campitch = -12.0;
    camdist = LARA_H * 2.6; eyeh = LARA_H * 0.63;
    msens = 0.18; dt = 1.0 / 60.0; t = 0.0; loco = 0.0;
    spdwalk = LARA_H * 4.0; spdrun = LARA_H * 8.6;
    facing = 180000.0; yawoff = 0.0;    // si mira al lado: Q/E en vivo
    swimpitch = 90000.0;                // inclinacion al nadar (T/G la ajustan)

    x = 640; y = 360;
    // HUD una sola vez (write/write_var son persistentes y se autoactualizan).
    write(0, 10, 10, 0, "WASD andar - SHIFT correr - ESPACIO saltar - raton camara - N room - ESC salir");
    write(0, 10, 30, 0, "EN VIVO: Z/X camara | C/V mira | Q/E giro | T/G inclinacion nado");
    write(0, 10, 54, 0, "camdist:"); write_var(0,  90, 54, 0, camdist);
    write(0, 230, 54, 0, "eyeh:");   write_var(0, 285, 54, 0, eyeh);
    write(0, 420, 54, 0, "giro:");   write_var(0, 475, 54, 0, yawoff);
    write(0, 640, 54, 0, "nado:");   write_var(0, 695, 54, 0, swimpitch);
    write(0, 10, 76, 0, "X:"); write_var(0,  30, 76, 0, px);
    write(0, 180, 76, 0, "Y:"); write_var(0, 200, 76, 0, py);
    write(0, 350, 76, 0, "Z:"); write_var(0, 370, 76, 0, pz);
    write(0, 10, 98, 0, "grounded:"); write_var(0,  95, 98, 0, grounded);
    write(0, 200, 98, 0, "room:");    write_var(0, 255, 98, 0, room);
    write(0, 380, 98, 0, "rooms:");   write_var(0, 445, 98, 0, nrooms);
    write(0, 1160, 700, 0, "FPS:");   write_var(0, 1210, 700, 0, frame_info.fps);

    g3d_mouse_capture(1);

    LOOP
        // ---- camara orbital ----
        g3d_mouse_update();
        camyaw = camyaw + g3d_mouse_dx() * msens;
        campitch = campitch - g3d_mouse_dy() * msens;
        IF (campitch > 55.0) campitch = 55.0; END
        IF (campitch < -35.0) campitch = -35.0; END

        // ---- ajuste en vivo de la camara / giro de Lara ----
        IF (key(_Z)) camdist = camdist - LARA_H * 1.2 * dt; END
        IF (key(_X)) camdist = camdist + LARA_H * 1.2 * dt; END
        IF (key(_C)) eyeh = eyeh - LARA_H * 0.8 * dt; END
        IF (key(_V)) eyeh = eyeh + LARA_H * 0.8 * dt; END
        IF (key(_Q)) yawoff = yawoff - 90000.0 * dt; END
        IF (key(_E)) yawoff = yawoff + 90000.0 * dt; END
        IF (key(_T)) swimpitch = swimpitch - 120000.0 * dt; END   // inclinacion al nadar
        IF (key(_G)) swimpitch = swimpitch + 120000.0 * dt; END
        IF (camdist < 0.3) camdist = 0.3; END

        // ---- intencion de movimiento relativa a la camara ----
        mfx = sin(camyaw * 1000); mfz = cos(camyaw * 1000);
        mrx = cos(camyaw * 1000); mrz = -sin(camyaw * 1000);
        wx = 0.0; wz = 0.0;
        IF (key(_W)) wx = wx + mfx; wz = wz + mfz; END
        IF (key(_S)) wx = wx - mfx; wz = wz - mfz; END
        IF (key(_A)) wx = wx + mrx; wz = wz + mrz; END   // (A/D estaban invertidas)
        IF (key(_D)) wx = wx - mrx; wz = wz - mrz; END

        spd = spdwalk;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = spdrun; END

        wl = sqrt(wx * wx + wz * wz);
        IF (wl > 0.001)
            wx = wx / wl * spd; wz = wz / wl * spd;
            facing = atan2(wx, wz);    // ya en mili-grados
        END
        g3d_char_move(ch, wx, wz);
        IF (key(_SPACE)) g3d_char_jump(ch, LARA_H * 4.3); END

        // ---- ¿Lara dentro de una piscina? (activa el nado) ----
        px = g3d_char_x(ch); py = g3d_char_y(ch); pz = g3d_char_z(ch);
        inwater = 0; wsurf = 0.0;
        FOR (wi = 0; wi < nwz; wi = wi + 1)
            IF (px > wzx0[wi] AND px < wzx1[wi] AND pz > wzz0[wi] AND pz < wzz1[wi] AND py < wzy[wi] + 0.3)
                inwater = 1; wsurf = wzy[wi];
            END
        END
        g3d_char_set_water(ch, inwater, wsurf);

        g3d_char_update(ch, dt);

        px = g3d_char_x(ch); py = g3d_char_y(ch); pz = g3d_char_z(ch);

        // ---- teleport a la siguiente room (para explorar el nivel) ----
        IF (key(_N) AND kn == 0)
            room = room + 1; IF (room >= nrooms) room = 0; END
            sx = g3d_model_submesh_cx(lvl, room);
            sy = g3d_model_submesh_cy(lvl, room) + LARA_H;
            sz = g3d_model_submesh_cz(lvl, room);
            g3d_char_set_position(ch, sx, sy, sz);
        END
        kn = key(_N);

        // ---- colocar + orientar el modelo (SIN offsets, como TEST_CHARACTER) ----
        g3d_entity_set_position(player, px, py, pz);
        IF (inwater)
            g3d_entity_set_rotation(player, swimpitch, facing + yawoff, 0.0);  // tumbada nadando
        ELSE
            g3d_entity_set_rotation(player, 0.0, facing + yawoff, 0.0);
        END

        // ---- animacion FABRICADA con poses (el modelo no trae ciclos) ----
        grounded = g3d_char_grounded(ch);
        t = t + dt;
        IF (nanim > 0)
            IF (inwater)
                // nadando (tumbada): patada suave alternando Run L<->R; si no se
                // mueve, pose neutra (planea). El modelo no trae nado real.
                runphase = runphase + 260.0 * dt;
                runw = 0.5 + 0.5 * sin(runphase * 1000);
                g3d_model_animate_blend(jmodel, P_RUNL, t, P_RUNR, t, runw, 0);
            ELSE
                IF (grounded == 0)
                    g3d_model_animate(jmodel, P_JUMP, t, 0);      // pose de salto
                ELSE
                    IF (wl > 0.001)
                        // correr: Run L<->R con onda seno; mas rapido con SHIFT.
                        IF (key(_L_SHIFT) OR key(_R_SHIFT)) runphase = runphase + 900.0 * dt;
                        ELSE runphase = runphase + 520.0 * dt; END
                        runw = 0.5 + 0.5 * sin(runphase * 1000);
                        g3d_model_animate_blend(jmodel, P_RUNL, t, P_RUNR, t, runw, 0);
                    ELSE
                        g3d_model_animate(jmodel, P_IDLE, t, 0);  // Neutral Rest Pose
                    END
                END
            END
        END

        // ---- camara en 3a persona (identica a TEST_CHARACTER) ----
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
