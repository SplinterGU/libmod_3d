// ============================================================================
//  TEST_ALPHA.PRG - Semitransparencia real en modelos 3D usando el local `alpha`
//  de BennuGD (0..255). El modelo de delante se desvanece con su propio `alpha`;
//  detras hay uno opaco para ver el blending. ESC = salir.
// ============================================================================
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";

GLOBAL
    int scene; int camera; int light;
    int gcr; int gcg; int gcb;   // color actual (ciclado con ESPACIO), 0..255
    int cidx;
END

// Cada objeto es un PROCESO con su local `alpha` (filosofia BennuGD).
// Orbita alrededor de (cx,cy,cz) con el radio dado mientras cambia su alpha.
PROCESS ghost(float cx, float cy, float cz, float radius, int model, int blendm, float phase0)
PRIVATE int ent; float orbit; float fade; float ox; float oz;
BEGIN
    ent = g3d_model_spawn(scene, model, cx + radius, cy, cz, 2.5, 0.0);
    orbit = phase0; fade = 0.0;
    blendmode = blendm;   // local blendmode de BennuGD (BLEND_NORMAL / BLEND_ADD...)
    LOOP
        // color actual (ciclado con ESPACIO en main) via los locales color_r/g/b
        color_r = gcr; color_g = gcg; color_b = gcb;
        // desvanecido SUAVE con seno sobre el LOCAL `alpha` de BennuGD (0..255).
        fade = fade + 1.4;                          // velocidad del desvanecido
        alpha = 143 + 112 * sin(fade * 1000);
        // el modelo 3D obedece los locales del proceso (alpha, color_r/g/b, blendmode) solo:
        g3d_entity_use_locals(ent);
        // orbitar alrededor del centro (sin/cos toman milideg -> *1000)
        orbit = orbit + 2.0;
        ox = cx + radius * cos(orbit * 1000);
        oz = cz + radius * sin(orbit * 1000);
        g3d_entity_set_position(ent, ox, cy, oz);
        // que el coco gire tambien sobre si mismo
        g3d_entity_set_rotation(ent, 0, angle, 0); angle = angle + 500;
        FRAME;
    END
END

// paleta de colores (0..255) seleccionada por indice
FUNCTION set_palette(int i)
BEGIN
    SWITCH (i)
        CASE 0: gcr = 255; gcg = 255; gcb = 255; END   // blanco (sin tinte)
        CASE 1: gcr = 255; gcg =  60; gcb =  60; END   // rojo
        CASE 2: gcr =  60; gcg = 255; gcb =  90; END   // verde
        CASE 3: gcr =  80; gcg = 140; gcb = 255; END   // azul
        CASE 4: gcr = 255; gcg = 220; gcb =  60; END   // amarillo
        CASE 5: gcr = 255; gcg =  80; gcb = 220; END   // magenta
        CASE 6: gcr =  60; gcg = 230; gcb = 255; END   // cyan
    END
END

PROCESS main()
PRIVATE int rock; int coco; float t; int kspace;
BEGIN
    set_mode(1280, 720, MODE_WINDOW);
    set_fps(60, 0);

    scene = g3d_scene_create("alpha"); g3d_scene_set_active(scene);
    camera = g3d_camera_create(); g3d_camera_set_active(camera);
    g3d_camera_set_position(camera, -1.5, 8.0, 14.0);
    g3d_camera_look_at(camera, -1.5, 1.0, -2.0, 0.0, 1.0, 0.0);

    light = g3d_light_create(0, 1.0, 0.98, 0.92);
    g3d_light_set_direction(light, -0.4, -0.6, -0.5);
    g3d_light_set_intensity(light, 1.0);
    g3d_set_ambient_light(0.5, 0.52, 0.58, 1.0);
    g3d_sky_set_gradient(0.35, 0.55, 0.85,  0.85, 0.90, 0.97); g3d_sky_enable(1);
    g3d_set_hdr(1); g3d_set_tonemap(1);

    rock = g3d_load_gltf("assets/rock.glb");
    coco = g3d_load_gltf("assets/coconut.glb");

    cidx = 0; set_palette(0);   // color inicial: blanco

    // roca opaca en el centro (referencia para ver el blending)
    g3d_model_spawn(scene, rock, -1.5, 0.0, -2.0, 2.5, 0.0);
    // coco 1: alpha normal (se funde) ; coco 2: BLEND aditivo (glow), fase opuesta
    ghost(-1.5, 1.5, -2.0, 6.0, coco, BLEND_NORMAL, 0.0);
    ghost(-1.5, 1.5, -2.0, 6.0, coco, BLEND_ADD,    180.0);

    write(0, 10, 10, 0, "Alpha + blend modes (locales BennuGD) - ESPACIO cambia color - ESC salir");
    write(0, 10, 30, 0, "color idx:"); write_var(0, 100, 30, 0, cidx);

    LOOP
        // ESPACIO cicla el color (los cocos lo leen por sus locales color_r/g/b)
        IF (key(_SPACE) AND kspace == 0)
            cidx = cidx + 1; IF (cidx > 6) cidx = 0; END
            set_palette(cidx);
        END
        kspace = key(_SPACE);
        IF (key(_ESC)) BREAK; END
        FRAME;
    END
    exit();
END
