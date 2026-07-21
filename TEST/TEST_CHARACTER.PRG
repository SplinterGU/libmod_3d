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
    int aidle; int awalk; int ajump; int arun; int afire; int aswim; int grounded; int air;
    int anim; int act; float actt;
    int inwater; int waswet; float waterY; float rippt;
    int rifle; int rmodel; int hand;
    float mscale; float rox; float roy; float roz; float rscale; float ryaw;
    float nx; float ny; float nz; float a2; float wx2; float wz2;
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

    // --- Personaje: modelo + esqueleto (old_snake_anim.fbx) ---
    jmodel = g3d_load_fbx("assets/old_snake_anim.fbx");
    nanim = g3d_model_anim_count(jmodel);
    aidle = 4; ajump = 6; awalk = 14; arun = 11; afire = 3; aswim = 13;   // indices del snake
    say("old_snake_anim.fbx  submallas=" + itoa(g3d_model_submesh_count(jmodel)) + "  animaciones=" + itoa(nanim));
    g3d_model_lock_root(jmodel, 1);                 // anima en el sitio; la fisica lo mueve
    player = g3d_model_spawn(scene, jmodel, sx, gy, sz, 3.6, 0.0);

    // --- Rifle (glb) pegado al hueso de la mano derecha durante el disparo ---
    rmodel = g3d_load_gltf("assets/m16_assault_rifle.glb");
    IF (rmodel >= 0) rifle = g3d_model_spawn(scene, rmodel, 0.0, -9999.0, 0.0, 0.0, 0.0); END
    hand = g3d_model_node_find(jmodel, "RightHand");
    mscale = 3.6 / g3d_model_height(jmodel);         // misma escala que el personaje
    // rifle: la "altura" del modelo (Y) es ~1/3 de su largo, asi que escalar por
    // 1/altura lo dejaba tan largo como el personaje -> lo bajamos bastante.
    rscale = 0.32 / g3d_model_height(rmodel);
    rox = 0.0; roy = 0.0; roz = 0.0; ryaw = 180000.0;
    anim = aidle; act = -1; actt = 0.0;

    // demo_beach: el mar (WATERSIM sea=0) esta a y=0; g3d_scene_water_level() no
    // conoce el watersim y devuelve -1e30, asi que fijamos el nivel a mano.
    waterY = 0.0;
    inwater = 0; waswet = 0; rippt = 0.0;
    say("DBG waterY=" + waterY + "  jH=" + g3d_model_height(jmodel) + "  rH=" + g3d_model_height(rmodel));
    say("DBG hand=" + hand + "  rifle=" + rifle + "  mscale=" + mscale + "  rscale=" + rscale);

    g3d_physics_set_gravity(30.0);
    ch = g3d_char_create(sx, gy, sz, 0.6, 3.0);
    g3d_char_set_tuning(ch, 0.8, 46.0);

    camyaw = 180.0; campitch = -12.0; camdist = 5.0; eyeh = 1.9;
    msens = 0.18; dt = 1.0 / 60.0; t = 0.0;
    spdwalk = 12.0; spdrun = 26.0;
    facing = 180000.0; yawoff = 0.0;   // si mira al lado, ajusta yawoff (90000 / 180000 / -90000)
    loco = 0.0;

    x = 480; y = 270;
    write(0, 10, 10, 0, "WASD andar - SHIFT correr - ESPACIO saltar - raton camara - ESC");
    write(0, 10, 26, 0, "ACCIONES: R disparar T tiro Q/E puños F patada C agachar G tumbar X morir Z gancho (nadar=auto)");
    write(0, 10, 42, 0, "anim:"); write_var(0, 60, 42, 0, anim);
    write(0, 130, 42, 0, "py:"); write_var(0, 170, 42, 0, py);
    write(0, 280, 42, 0, "grounded:"); write_var(0, 370, 42, 0, grounded);
    write(0, 440, 42, 0, "inwater:"); write_var(0, 520, 42, 0, inwater);
    write(0, 10, 500, 0, "rifle I/K J/L U/O mueve  Y/H escala  N/M giro");
    write(0, 350, 500, 0, "rox:"); write_var(0, 395, 500, 0, rox);
    write(0, 490, 500, 0, "roy:"); write_var(0, 535, 500, 0, roy);
    write(0, 630, 500, 0, "roz:"); write_var(0, 675, 500, 0, roz);
    write(0, 770, 500, 0, "rsc:"); write_var(0, 815, 500, 0, rscale);
    write(0, 910, 500, 0, "gir:"); write_var(0, 955, 500, 0, ryaw);
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
        IF (key(_A)) wx = wx + mrx; wz = wz + mrz; END
        IF (key(_D)) wx = wx - mrx; wz = wz - mrz; END

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

        // ---- AGUA: nado automatico por profundidad + fisica de flotacion ----
        inwater = 0;
        IF (py < waterY - 1.2) inwater = 1; END       // cintura bajo el agua -> nadar
        g3d_char_set_water(ch, inwater, waterY);
        IF (inwater AND waswet == 0) g3d_water_splash(px, waterY, pz, 1.0); END  // chapuzon
        waswet = inwater;
        IF (inwater)                                   // ondas mientras nada/se mueve
            rippt = rippt + dt;
            IF (rippt > 0.15) g3d_water_ripple(px, waterY, pz); rippt = 0.0; END
        END

        // ---- colocar + orientar el modelo ----
        g3d_entity_set_position(player, px, py, pz);
        g3d_entity_set_rotation(player, 0.0, facing + yawoff, 0.0);

        // ---- ACCIONES por tecla (como un videojuego): dispara la anim una vez ----
        //   R Firing rifle   T Shooting   Q Punch1   E Punch2   F Kick
        //   C Crouch   G Prone   X Death   Z Jump-to-hang   (nadar = automatico)
        IF (act < 0 AND inwater == 0)
            IF (key(_R)) act = afire; actt = 0.0; END
            IF (key(_T)) act = 12; actt = 0.0; END
            IF (key(_Q)) act = 9;  actt = 0.0; END
            IF (key(_E)) act = 10; actt = 0.0; END
            IF (key(_F)) act = 7;  actt = 0.0; END
            IF (key(_C)) act = 1;  actt = 0.0; END
            IF (key(_G)) act = 8;  actt = 0.0; END
            IF (key(_X)) act = 2;  actt = 0.0; END
            IF (key(_Z)) act = 5;  actt = 0.0; END
        END

        // ---- animacion: nado / accion en curso / locomocion (salto/idle<->andar) ----
        grounded = g3d_char_grounded(ch);
        // anti-rebote: solo contamos "en el aire" tras varios frames sin suelo,
        // asi un microbache en el terreno no dispara la anim de salto al andar.
        IF (grounded == 0) air = air + 1; ELSE air = 0; END
        t = t + dt;
        IF (nanim > 0)
            IF (inwater)
                act = -1; anim = aswim; g3d_model_animate(jmodel, aswim, t, 1);  // nadar en bucle
            ELSE
                IF (act >= 0)
                    anim = act;
                    g3d_model_animate(jmodel, act, actt, 0);       // una vez, sin bucle
                    actt = actt + dt;
                    IF (actt >= g3d_model_anim_duration(jmodel, act)) act = -1; END
                ELSE
                    IF (air > 4)
                        anim = ajump; g3d_model_animate(jmodel, ajump, t, 1);
                    ELSE
                        locotgt = 0.0; IF (wl > 0.001) locotgt = 1.0; END
                        loco = loco + (locotgt - loco) * 10.0 * dt;    // 0=idle .. 1=andar
                        IF (key(_L_SHIFT) AND wl > 0.001)
                            anim = arun; g3d_model_animate_blend(jmodel, awalk, t, arun, t, loco, 1);
                        ELSE
                            anim = awalk; g3d_model_animate_blend(jmodel, aidle, t, awalk, t, loco, 1);
                        END
                    END
                END
            END
        END

        // ---- tuneo en vivo del rifle: I/K J/L U/O mueven, Y/H escala, N/M giro ----
        IF (key(_I)) roz = roz + 2.0 * dt; END
        IF (key(_K)) roz = roz - 2.0 * dt; END
        IF (key(_J)) rox = rox - 2.0 * dt; END
        IF (key(_L)) rox = rox + 2.0 * dt; END
        IF (key(_U)) roy = roy + 2.0 * dt; END
        IF (key(_O)) roy = roy - 2.0 * dt; END
        IF (key(_Y)) rscale = rscale + rscale * dt; END
        IF (key(_H)) rscale = rscale - rscale * dt; END
        IF (key(_N)) ryaw = ryaw - 120000.0 * dt; END
        IF (key(_M)) ryaw = ryaw + 120000.0 * dt; END

        // ---- RIFLE pegado al HUESO de la mano (solo al disparar) ----
        IF (rifle >= 0 AND hand >= 0 AND (anim == afire OR anim == 12))
            nx = g3d_model_node_x(jmodel, hand) * mscale + rox;
            ny = g3d_model_node_y(jmodel, hand) * mscale + roy;
            nz = g3d_model_node_z(jmodel, hand) * mscale + roz;
            a2 = facing + yawoff;
            wx2 =  nx * cos(a2) + nz * sin(a2);
            wz2 = -nx * sin(a2) + nz * cos(a2);
            g3d_entity_set_position(rifle, px + wx2, py + ny, pz + wz2);
            g3d_entity_set_rotation(rifle, 0.0, a2 + ryaw, 0.0);
            g3d_entity_set_scale(rifle, rscale, rscale, rscale);
        END
        IF (rifle >= 0 AND (hand < 0 OR (anim <> afire AND anim <> 12)))
            g3d_entity_set_position(rifle, 0.0, -9999.0, 0.0);
        END

        // ---- camara en 3a persona ----
        cfx = sin(camyaw * 1000) * cos(campitch * 1000);
        cfy = sin(campitch * 1000);
        cfz = cos(camyaw * 1000) * cos(campitch * 1000);
        tx = px; ty = py + eyeh; tz = pz;
        g3d_camera_set_position(camera, tx - cfx * camdist, ty - cfy * camdist, tz - cfz * camdist);
        g3d_camera_look_at(camera, tx, ty, tz, 0.0, 1.0, 0.0);

        // ---- tinte submarino a pantalla completa cuando la CAMARA esta bajo el agua ----
        IF (ty - cfy * camdist < waterY)
            g3d_set_underwater(1, 0.10, 0.32, 0.42, 0.05);
        ELSE
            g3d_set_underwater(0, 0.10, 0.32, 0.42, 0.05);
        END

        
        

        IF (key(_5) AND k5 == 0) fsmaa = 1 - fsmaa; g3d_set_smaa(fsmaa); END
        k5 = key(_5);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END

    g3d_mouse_capture(0);
    
END
