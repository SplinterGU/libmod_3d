// ============================================================================
//  TEST_IBL.PRG - Image Based Lighting real, capturado del propio cielo.
//
//  Rejilla clasica de esferas: METALICIDAD en un eje, RUGOSIDAD en el otro.
//  Es la prueba de fuego del IBL: los metales solo pueden verse bien si
//  reflejan un entorno de verdad. Antes el entorno era un degradado de 2
//  colores inventado; ahora es el cielo real (degradado + sol + nubes).
//
//  WASD + raton = volar,  SHIFT = rapido,  ESC = salir.
//  5 = IBL ON/OFF (la comparacion importante)
//  6 = mas/menos nubes (el entorno se recaptura solo)
//  7 = cielo dia / atardecer  (idem)
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

CONST
    NROWS = 6;    // rugosidad
    NCOLS = 6;    // metalicidad
END

GLOBAL
    int scene; int camera; int light; int sphere;
    int fibl; int k5; int k6; int k7;
    int fclouds; int fsunset;
    float camx; float camy; float camz;
END

PROCESS main()
PRIVATE
    int i; int j; int ent; int mat;
    float msens; float spd; float yaw; float pitch;
    float fwx; float fwy; float fwz; float rgx; float rgz;
    float metal; float rough;
BEGIN
    set_mode(1920, 1080, MODE_FULLSCREEN);
    set_fps(0, 0);

    scene = g3d_scene_create("ibl");
    g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);

    // Sol suave: con IBL real la mayor parte de la luz ambiente viene del CIELO,
    // no de este ambiente plano.
    light = g3d_light_create(0, 1.0, 0.97, 0.90);
    g3d_light_set_direction(light, -0.45, -0.55, -0.35);
    g3d_light_set_intensity(light, 1.6);
    g3d_set_ambient_light(0.12, 0.13, 0.16, 1.0);

    fsunset = 0; fclouds = 0;
    g3d_sky_set_gradient(0.28, 0.45, 0.80,  0.72, 0.85, 0.98);
    g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1); g3d_set_exposure(1.0);
    g3d_set_shadows(0);

    fibl = 1;
    g3d_set_ibl(1, 1.0);

    // Rejilla de esferas: fila = rugosidad (0..1), columna = metalicidad (0..1)
    sphere = g3d_primitive_sphere(48);
    FOR (i = 0; i < NROWS; i++)
        FOR (j = 0; j < NCOLS; j++)
            ent = g3d_entity_spawn(scene, 0, j * 2.5 - 6.25, i * 2.5 + 1.5, 0.0);
            g3d_entity_set_mesh(ent, sphere);
            g3d_entity_set_scale(ent, 1.0, 1.0, 1.0);
            mat = g3d_material_create();
            g3d_material_set_color(mat, 0.95, 0.75, 0.35, 1.0);   // oro
            metal = j / (NCOLS - 1.0);
            rough = i / (NROWS - 1.0);
            IF (rough < 0.05) rough = 0.05; END      // 0 puro = espejo perfecto
            g3d_material_set_metallic(mat, metal);
            g3d_material_set_roughness(mat, rough);
            g3d_entity_set_material(ent, mat);
        END
    END

    camx = 0.0; camy = 8.0; camz = 16.0;
    yaw = 180.0; pitch = -12.0; msens = 0.18;
    g3d_mouse_capture(1);

    write(0, 10, 10, 0, "IBL REAL - esferas: -> metalicidad,  ^ rugosidad");
    write(0, 10, 30, 0, "5 = IBL ON/OFF (compara!)   6 = nubes   7 = atardecer   ESC salir");
    write(0, 10, 54, 0, "IBL:"); write_var(0, 50, 54, 0, fibl);
    write(0, 1740, 1050, 4, "FPS:"); write_var(0, 1800, 1050, 4, frame_info.fps);

    LOOP
        spd = 0.15;
        IF (key(_L_SHIFT) OR key(_R_SHIFT)) spd = 0.6; END
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

        // IBL ON/OFF: OFF vuelve al ambiente plano + degradado analitico de antes
        IF (key(_5) AND k5 == 0) fibl = 1 - fibl; g3d_set_ibl(fibl, 1.0); END
        k5 = key(_5);

        // Cambiar el cielo recaptura el entorno automaticamente: los metales
        // reflejan el cielo nuevo sin tocar nada mas.
        IF (key(_6) AND k6 == 0)
            fclouds = 1 - fclouds;
            IF (fclouds) g3d_sky_set_clouds(0.75, 1.0); ELSE g3d_sky_set_clouds(0.0, 1.0); END
        END
        k6 = key(_6);
        IF (key(_7) AND k7 == 0)
            fsunset = 1 - fsunset;
            IF (fsunset)
                g3d_sky_set_gradient(0.18, 0.16, 0.35,  0.95, 0.45, 0.18);
                g3d_light_set_direction(light, -0.85, -0.12, -0.25);
                g3d_light_set_color(light, 1.0, 0.62, 0.35);
            ELSE
                g3d_sky_set_gradient(0.28, 0.45, 0.80,  0.72, 0.85, 0.98);
                g3d_light_set_direction(light, -0.45, -0.55, -0.35);
                g3d_light_set_color(light, 1.0, 0.97, 0.90);
            END
            g3d_ibl_refresh();
        END
        k7 = key(_7);

        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    g3d_mouse_capture(0);
END
