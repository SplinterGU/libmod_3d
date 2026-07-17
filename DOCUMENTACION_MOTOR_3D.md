# libmod_3d — Documentación completa del motor 3D para BennuGD2

> Motor de renderizado 3D moderno (OpenGL 3.3+/4.0) integrado como **módulo nativo**
> de BennuGD2. Se importa desde un `.PRG` con `import "libmod_3d";` y expone ~350
> funciones `G3D_*` para escenas, modelos (glTF/OBJ/FBX/MD3/Tomb Raider), PBR,
> sombras, IBL, agua, físicas (Jolt), partículas, terreno, mundos grandes, etc.
>
> Este documento describe **qué contiene cada fichero fuente** y **qué hace cada
> función exportada**, además de **cómo compilarlo junto a BennuGD2**.

---

## Índice

1. [Arquitectura general](#1-arquitectura-general)
2. [Cómo se integra un módulo en BennuGD2](#2-cómo-se-integra-un-módulo-en-bennugd2)
3. [Compilación junto a BennuGD2](#3-compilación-junto-a-bennugd2)
4. [Mapa de ficheros del código fuente](#4-mapa-de-ficheros-del-código-fuente)
5. [Estructuras de datos principales](#5-estructuras-de-datos-principales)
6. [Referencia de la API por subsistemas](#6-referencia-de-la-api-por-subsistemas)

---

## 1. Arquitectura general

El motor es un **pipeline de renderizado forward** con:

- **Forward+ con sombras**: shadow mapping (CSM), PBR Cook‑Torrance GGX.
- **IBL real** generado desde el propio cielo (cubemap de irradiancia + prefiltrado + BRDF LUT).
- **Post‑proceso**: HDR, tonemapping, bloom, SSAO, SMAA (antialias), FSR 1 (reescalado).
- **Culling**: frustum + occlusion queries por hardware + LOD por distancia.
- **Físicas**: backend **Jolt Physics** (por defecto) o un fallback cinemático propio.
- **Contenido**: cargadores glTF/GLB, OBJ, FBX (ufbx), MD3 y niveles nativos de Tomb Raider (TR1–TR5).
- **Mundos grandes**: instancing GPU, streaming por tiles, generación procedural, *world rebase* (precisión).
- **Herramientas de mundo**: terreno esculpible, pintura de texturas, prefabs, ficheros `.scene`.

Plataforma principal: **PC (Linux/Windows/macOS), OpenGL 3.3+/4.0**. Hay ganchos
previstos para VITA/Android con degradación (renderer alternativo aún no incluido).

Estado global en `g_3d_engine` (ver [`libmod_3d.h`](libmod_3d.h)): límites fijos de
4096 entidades, 16 escenas, 16 cámaras, 32 luces, 256 materiales, 512 modelos, 1024 texturas.

---

## 2. Cómo se integra un módulo en BennuGD2

BennuGD2 carga funciones nativas desde una librería dinámica (`libmod_3d.so` /
`.dll` / `.dylib`). El puente está en [`libmod_3d_exports.h`](libmod_3d_exports.h):

- **`constants_def[]`** — constantes visibles en el `.PRG`: `C_3D`,
  `G3D_LIGHT_DIRECTIONAL/POINT/SPOT`, `C3D_ENTITY/LIGHT/CAMERA`.
- **`locals_def`** — variables locales que se inyectan en **cada proceso** cuando
  se importa el módulo: `angle_x/y/z`, `size_z`, `entity`, `target_x/y/z`, `fov`,
  `intensity`, `range`, `cone_angle`.
- **`functions_exports[]`** — la tabla `FUNC(nombre, firma, tipo_retorno, wrapper)`.
- **`module_initialize` / `module_finalize`** — ganchos de carga/descarga.

### Cómo leer las firmas (importante para este documento)

Cada función se declara con `FUNC("NOMBRE", "firma", TIPO_RETORNO, wrapper_bgd)`.
La cadena de **firma** codifica los parámetros, un carácter por parámetro:

| Letra | Tipo en el `.PRG` | Significado |
|:---:|---|---|
| `I` | `INT` | entero (64 bits) |
| `F` | `FLOAT`/`DOUBLE` | real (en BennuGD2 los `FLOAT` son `double`) |
| `S` | `STRING` | cadena (ruta de fichero, nombre…) |
| `P` | puntero / variable por referencia | **parámetro de salida**: pasas una variable y la función la rellena |
| `""` | (vacío) | sin parámetros |

El tipo de retorno es `TYPE_INT` o `TYPE_FLOAT`. En este documento, junto a cada
función, se indica su firma tal cual aparece en la tabla.

> **Convención de ángulos:** muchas demos multiplican los grados por `1000`
> (p.ej. `sin(yaw * 1000)`) porque las funciones trigonométricas de BennuGD2
> trabajan en **milésimas de grado**. Las funciones que reciben rotaciones en el
> motor esperan **grados** (internamente `g3d_model_spawn` convierte con `MD2RAD`).

---

## 3. Compilación junto a BennuGD2

El módulo vive dentro del árbol de BennuGD2 en `modules/libmod_3d/` y se compila
como parte del build global. Se apoya en librerías del propio BennuGD2:
`bgdrtm` (runtime), `bggfx` (gráficos 2D/ventana) y `sdlhandler`.

### 3.1 Dependencias

- **CMake ≥ 3.5** y un compilador C/C++ (C++17 para Jolt).
- **SDL2**, **SDL2_image**, **OpenGL** (dev packages).
- **Jolt Physics**: viene *vendored* en `jolt/` (MIT). No hace falta submódulo ni
  red; un clon del repo ya compila con físicas. Con `-DUSE_JOLT=OFF` se usa el
  fallback en `libmod_3d_physics.c`.
- **cgltf.h**, **ufbx.c/.h**, **stb**‑style headers: incluidos en el módulo.

### 3.2 Build completo (recomendado)

Desde la raíz del repo BennuGD2, el script `build.sh` configura CMake y compila
todo (compilador `bgdc`, intérprete `bgdi`, y todos los módulos incluido este):

```bash
cd /home/ruben/BennuGD2
./build.sh linux            # target linux-gnu -> salida en build/linux-gnu/bin/
# opciones útiles:  debug   clean   verbose   one-job   static
# otros targets:    windows  windows32  linux32  macosx  switch  android
```

Resultado en `build/linux-gnu/bin/`: `bgdc`, `bgdi`, `libmod_3d.so` y el resto de módulos.

### 3.3 Recompilar SOLO el módulo 3D (iteración rápida)

Cuando ya existe el directorio de build y solo tocas C del motor:

```bash
cd /home/ruben/BennuGD2/build/linux-gnu
make mod_3d                 # reconstruye y enlaza libmod_3d.so en ../../bin/
```

> El *target* de CMake se llama **`mod_3d`** (no `libmod_3d`). El `.so` se deja en
> `build/linux-gnu/bin/`, que es donde `bgdi` busca los módulos.

### 3.4 Compilar y ejecutar un `.PRG`

```bash
cd /home/ruben/BennuGD2/modules/libmod_3d/TEST
bgdc TEST_TR.PRG            # compila  -> TEST_TR.dcb
bgdi TEST_TR.dcb           # ejecuta
```

Un `.PRG` mínimo importa el módulo así:

```c
import "libmod_gfx";
import "libmod_misc";
import "libmod_input";
import "libmod_3d";
```

### 3.5 Notas de plataforma (del `CMakeLists.txt`)

- `-D__LIBMOD_3D` define el módulo; en VITA se añade `-DVITA` y se excluye Jolt.
- Jolt se compila como estático con **PIC** (posición independiente) porque se
  enlaza dentro del `.so`; se desactivan sus tests/samples/viewer.
- `mod_3d` se compila como **C++17** cuando Jolt está activo (por el `.cpp`).

---

## 4. Mapa de ficheros del código fuente

Todos los `.c` listados están en el `CMakeLists.txt` (39 fuentes propias, más
`ufbx.c` y, opcionalmente, `libmod_3d_jolt.cpp`). Cada `.c` tiene su `.h` salvo donde se indica.

### 4.1 Núcleo e infraestructura

| Fichero | Contenido |
|---|---|
| [`libmod_3d.c`](libmod_3d.c) / `.h` | **Corazón del módulo.** Estado global `g_3d_engine`, ganchos `module_initialize/finalize`, y **la mayoría de wrappers `*_bgd`** que convierten los parámetros de BennuGD2 a las llamadas C. Aquí vive `g3d_model_spawn` (instancia un modelo entero con materiales PBR automáticos). Define tipos `G3D_Material/Texture/Mesh/Model/Entity/Light/Camera/Scene/Engine`. |
| [`libmod_3d_exports.h`](libmod_3d_exports.h) | Tabla de exportación a BennuGD2 (constantes, locals y `FUNC(...)`). Es el **índice de toda la API pública**. |
| [`libmod_3d_math.c`](libmod_3d_math.c) / `.h` | Matemáticas 3D sin dependencias: `Vec2/3/4`, `Quat`, `Mat4` (4x4 *column‑major*), transformaciones, proyecciones, look‑at. Base de todo lo demás. |
| [`libmod_3d_shader.c`](libmod_3d_shader.c) / `.h` | Compilación/enlazado de shaders GLSL y gestión de *uniforms*. Variantes PC (GL 3.3+) y GLES2. Contiene el código GLSL del PBR/sombras/post. |
| [`libmod_3d_texture.c`](libmod_3d_texture.c) / `.h` | Carga de imágenes (PNG/JPG/TGA… vía SDL2_image), subida a GPU y caché de texturas. Exporta `G3D_LOAD_TEXTURE`. |
| [`libmod_3d_mesh.c`](libmod_3d_mesh.c) / `.h` | Estructuras de malla y *buffers* GPU (VAO/VBO/EBO). Cálculo de AABB (`g3d_mesh_calculate_bounds`, `g3d_model_calculate_bounds`), submallas, subida de vértices. |
| [`libmod_3d_renderer.c`](libmod_3d_renderer.c) / `.h` | **Pipeline forward.** Orquesta: frustum culling → paso de sombras (depth) → paso forward con iluminación PBR → post (fog, HDR, bloom, SSAO, SMAA, FSR). Contiene casi toda la configuración de render (`g3d_set_*`). Fichero más grande junto a `libmod_3d.c`. |
| [`libmod_3d_camera.c`](libmod_3d_camera.c) / `.h` | Cámaras: matrices vista/proyección, FPS/3ª persona/ortográfica, FOV, look‑at. |
| [`libmod_3d_scene.c`](libmod_3d_scene.c) / `.h` | Gestión de escenas: colecciones de entidades, luces y cámaras. |
| [`libmod_3d_entity.c`](libmod_3d_entity.c) / `.h` | Entidades (objetos con transform + malla + material). `g3d_model_spawn` y helpers de instanciación de submallas. |
| [`libmod_3d_material.c`](libmod_3d_material.c) / `.h` | Materiales: color base, metallic, roughness, texturas y mapas PBR (normal/metal/rough). |
| [`libmod_3d_light.c`](libmod_3d_light.c) / `.h` | Luces direccional/punto/foco, con sombras y calidad de sombra. |

### 4.2 Cargadores de modelos

| Fichero | Contenido |
|---|---|
| [`libmod_3d_gltf.c`](libmod_3d_gltf.c) / `.h` | Cargador **glTF 2.0** (`.gltf`/`.glb`) vía cgltf. Convierte a `G3DModel` con mallas, materiales PBR, esqueleto y animaciones. Opciones de *recenter* y *chunking* espacial. Camino más robusto para personajes. |
| [`libmod_3d_obj.c`](libmod_3d_obj.c) / `.h` | Cargador **Wavefront .obj** (mallas estáticas + texturas base‑color desde `.mtl`). |
| [`libmod_3d_fbx.c`](libmod_3d_fbx.c) / `.h` | Cargador **FBX** vía `ufbx` (estático + *skinned* + animaciones, estilo Mixamo). Búsqueda tolerante de texturas por nombre/ruta (incluye *fallback* por nombre de material cuando el FBX no trae enlaces). |
| `ufbx.c` / `ufbx.h` | Librería *single‑file* de parseo FBX (terceros). |
| [`libmod_3d_trlevel.c`](libmod_3d_trlevel.c) / `.h` | **Cargador nativo de niveles Tomb Raider** (`.PHD`/`.TR2`/`.tr4`/`.TRC`, TR1–TR5). Lee rooms, geometría y texturas del propio nivel; detecta versión por *magic*/extensión. `G3D_LOAD_TR`, `G3D_TR_PROBE`. |
| [`libmod_3d_anim.c`](libmod_3d_anim.c) / `.h` | Reproducción de **animación esqueletal** (glTF/FBX), *skinning* en CPU. Muestreo de poses, *blending*, bloqueo de raíz (`lock_root`). |

### 4.3 Físicas y colisión

| Fichero | Contenido |
|---|---|
| [`libmod_3d_collide.c`](libmod_3d_collide.c) / `.h` | Colisión **sencilla** contra AABB de entidades marcadas como sólidas: *move‑and‑slide* de un cilindro vertical y *raycast* rayo‑vs‑cajas. Suficiente para muros/props y *picking*. |
| [`libmod_3d_physics.c`](libmod_3d_physics.c) / `.h` | Capa física **cinemática ligera** propia: *character controllers* de cápsula (andar/saltar sobre el heightmap o superficie vóxel, deslizar en pendientes, subir escalones) y cajas AABB. También contiene la implementación *fallback* de *rigid bodies* (cuando `USE_JOLT` está apagado). |
| [`libmod_3d_jolt.cpp`](libmod_3d_jolt.cpp) | Backend **Jolt Physics** (opcional, por defecto ON). Implementa la misma API `g3d_rigidbody_*`/colisión de malla que `physics.c`, alimentando el heightmap del terreno como *HeightFieldShape*. Los `.PRG` no cambian según el backend. |

### 4.4 Entorno, cielo y agua

| Fichero | Contenido |
|---|---|
| [`libmod_3d_sky.c`](libmod_3d_sky.c) / `.h` | Cielo: gradiente vertical procedural (cénit→horizonte con *glow* de sol) o panorama equirectangular. Nubes y nubes bajas. |
| [`libmod_3d_ibl.c`](libmod_3d_ibl.c) / `.h` | **Image Based Lighting real** generado desde el cielo que se está renderizando: cubemap de entorno → irradiancia (difuso) + cadena prefiltrada (especular por *roughness*) + BRDF LUT. Captura perezosa (solo al cambiar el cielo). |
| [`libmod_3d_water.c`](libmod_3d_water.c) / `.h` | **Superficie de agua animada**: plano con shader propio (olas por suma de senos, normales analíticas, Fresnel, especular de sol, transparencia, reflexión planar/SSR, océano, ripples/splashes). |
| [`libmod_3d_flow.c`](libmod_3d_flow.c) / `.h` | **Agua en movimiento** (cascadas/ríos): cintas cuya textura hace *scroll* a lo largo; verticales = cascada, inclinadas = río. |
| [`libmod_3d_watersim.c`](libmod_3d_watersim.c) / `.h` | **Simulación de agua unificada** (campo de altura *shallow‑water*, Mei et al.): el agua es un campo de PROFUNDIDAD por celda sobre el terreno; fluye cuesta abajo, y lagos/ríos/cascadas/deltas emergen de una sola superficie. Autoría = fuentes + gravedad. |
| [`libmod_3d_fluidfill.c`](libmod_3d_fluidfill.c) | **Inundación de depresiones** (*flood‑fill*): dado un terreno y una semilla, halla el nivel de desbordamiento y genera una malla plana de lago que se ajusta a la cuenca. (Sin `.h`.) |
| [`libmod_3d_fire.c`](libmod_3d_fire.c) / `.h` | **Fuego procedural** (antorchas/hogueras): *billboard* con shader de llama por ruido (HDR para que haga *bloom*) + luz de punto cálida y parpadeante. |
| [`libmod_3d_particles.c`](libmod_3d_particles.c) / `.h` | **Partículas GPU** (*point sprites*): spray de cascada, salpicaduras, niebla, fuentes. Emisor con pool de partículas actualizado en CPU. |
| [`libmod_3d_mirror.c`](libmod_3d_mirror.c) / `.h` | **Espejos planos** (varios, cada uno con su reflexión): renderiza la escena reflejada en la textura del espejo; sin asset de textura. |

### 4.5 Post‑proceso y culling

| Fichero | Contenido |
|---|---|
| [`libmod_3d_occlusion.c`](libmod_3d_occlusion.c) / `.h` | **Occlusion culling por hardware** (*GL occlusion queries*): tras el paso opaco, prueba la AABB de cada entidad contra el *depth*; si ninguna muestra pasa, se descarta el fotograma siguiente. Visibilidad con 1 frame de retardo (sin *stalls*). |
| [`libmod_3d_smaa.c`](libmod_3d_smaa.c) / `.h` | **SMAA 1x** (antialias, Jimenez et al.): 3 pasos sobre la imagen LDR (detección de bordes → pesos de mezcla con LUTs → *blending*). Prerrequisito de FSR. Fuentes vendored en `smaa/` y `smaa_shader.h`. |
| [`libmod_3d_fsr.c`](libmod_3d_fsr.c) / `.h` | **AMD FSR 1** (reescalado espacial): EASU (upsample adaptativo a bordes) + RCAS (sharpen). Renderiza a menor resolución y escala a la de pantalla. Fuentes en `fsr/` y `fsr_shader.h`. |

### 4.6 Terreno y herramientas de mundo

| Fichero | Contenido |
|---|---|
| [`libmod_3d_primitives.c`](libmod_3d_primitives.c) / `.h` | Generación de **primitivas**: cubo, esfera, plano, terreno (heightmap), acantilados, montañas. |
| [`libmod_3d_terrain.c`](libmod_3d_terrain.c) / `.h` | **Edición de terreno en runtime** (para el editor): pinceles de subir/bajar, suavizar, aplanar; recalcula normales y re‑sube la malla. |
| [`libmod_3d_paint.c`](libmod_3d_paint.c) / `.h` | **Pintura de texturas sobre terreno**: un lienzo RGBA 0..1 estirado sobre el terreno; se rellena con una base y se pintan otras texturas con pincel, horneando un único albedo. |
| [`libmod_3d_chunkterrain.c`](libmod_3d_chunkterrain.c) / `.h` | **Terreno por chunks** (mundos kilométricos): un único array de alturas compartido, renderizado como rejilla de mallas para *frustum‑cull* por chunk; bordes sin costuras (geometría y luz). El estado lo posee el llamante (editor). |
| [`libmod_3d_cave.c`](libmod_3d_cave.c) / `.h` | **Cuevas por vóxeles** (híbrido con heightmap): volúmenes de densidad subterráneos esculpidos con pincel esférico 3D, mallados con *marching cubes* (ver tablas en `libmod_3d_mctables.h`). |
| [`libmod_3d_voxterrain.c`](libmod_3d_voxterrain.c) / `.h` | **Terreno vóxel completo** (marching cubes): todo el terreno es un campo de densidad 3D por chunks; soporta cuevas/salientes/arcos y un pincel único de cavar/añadir. Superficie derivada para agua/colisión. |
| [`libmod_3d_worldgen.c`](libmod_3d_worldgen.c) / `.h` | **Terreno procedural** para mundos *streameados*: función de altura determinista (fBm *value noise*); mismas coords → mismo terreno, tiles adyacentes casan en las costuras. Biomas y texturas por altura. |
| [`libmod_3d_pick.c`](libmod_3d_pick.c) / `.h` | **Picking pantalla→mundo**: desproyecta un píxel y marcha el rayo contra el *heightfield*, dando el punto bajo el cursor (colocar cosas con el ratón). |
| [`libmod_3d_prefab.c`](libmod_3d_prefab.c) / `.h` | **Prefabs**: grupos nombrados de piezas (cajas) con transform/color/textura/collider; se guardan/cargan en texto y se instancian en la escena con posición + yaw. |
| [`libmod_3d_scenefile.c`](libmod_3d_scenefile.c) / `.h` | **Ficheros `.scene`** (mapa declarativo en texto): terreno, cielo, niebla, luces, vegetación esparcida, modelos y prefabs. El editor externo escribe, el runtime carga con una llamada. Incluye punto de aparición del jugador. |

### 4.7 Mundos grandes: instancing y streaming

| Fichero | Contenido |
|---|---|
| [`libmod_3d_instance.c`](libmod_3d_instance.c) / `.h` | **Instancing GPU** para vegetación/props: una malla dibujada muchas veces en una *draw call*, con transform por instancia, *culling* por distancia+frustum, LOD barato y *wind sway* opcional. También *scatter* (esparcir) y skinning en GPU. |
| [`libmod_3d_stream.c`](libmod_3d_stream.c) / `.h` | **Streaming por tiles** (open world): divide el mundo en rejilla; cada `update` reporta tiles que ENTRAN (poblar) y SALEN (liberar) del radio de carga. El juego decide qué vive en cada tile (sin callbacks C→script). Síncrono en esta versión. |

### 4.8 Recursos vendored

| Ruta | Contenido |
|---|---|
| `jolt/` | Jolt Physics (fuente completa, MIT). |
| `smaa/`, `smaa_shader.h` | Shaders/LUTs oficiales de SMAA. |
| `fsr/`, `fsr_shader.h` | Shaders de AMD FidelityFX Super Resolution 1. |
| `cgltf.h` | Parser glTF *single‑file*. |
| `ufbx.c/.h` | Parser FBX *single‑file*. |
| `libmod_3d_mctables.h` | Tablas de *marching cubes* (cuevas/vóxel). |
| `libmod_3d_cloud_glsl.h` | GLSL de nubes para el cielo. |

### 4.9 Cloth (simulación de tela)

| Fichero | Contenido |
|---|---|
| [`libmod_3d_cloth.c`](libmod_3d_cloth.c) / `.h` | **Tela por Verlet** (banderas, telas colgantes): rejilla de partículas con restricciones de distancia; se ancla un borde, se sopla viento y una esfera (el personaje) la empuja. El resultado es una `G3DMesh` normal (texturizada/iluminada/sombreada). |

---

## 5. Estructuras de datos principales

Definidas en [`libmod_3d.h`](libmod_3d.h). Resumen de las más relevantes:

- **`G3D_Material`** — albedo, especular, shininess, *metallic*, *roughness*,
  alpha; IDs de texturas (albedo/normal/rough/metal, −1 si no hay); `blend_mode`
  (0 opaco, 1 alpha, 2 aditivo); `cull_face` (0 back, 1 front, 2 none).
- **`G3D_Texture`** — dimensiones, canales, datos crudos y *handle* GPU.
- **`G3D_Mesh`** — posiciones/normales/texcoords/índices, `material_id`, VAO/VBO/EBO.
- **`G3D_Model`** — array de mallas + animaciones + AABB.
- **`G3D_Transform`** — posición, rotación (cuaternión), escala, *euler* cacheado,
  `model_matrix`, `parent_id`.
- **`G3D_Entity`** — transform + `model_id` + `material_override` + estado de
  animación + `physics_body_id` + visibilidad/sombra.
- **`G3D_Light`** — tipo, color, intensidad, posición/dirección, rango, cono,
  *shadow map* y matriz de espacio de luz.
- **`G3D_Camera`** — posición/target/up, *euler*, proyección (persp/orto), FOV,
  planos near/far, matrices vista/proyección.
- **`G3D_Scene`** — arrays de entidades/luces/cámaras/cuerpos, luz ambiente,
  niebla, color de fondo.
- **`G3D_Engine`** — estado global: dimensiones, escena activa, pools de
  modelos/texturas/materiales, contexto GPU, flags de debug, delta time.

Procesos `C_3D` en BennuGD2: las locales inyectadas (`CSUBTYPE`, `COORDX/Y/Z`,
`ANGLE_X/Y/Z`, `SIZE*`, `ENTITY`) permiten manejar entidades/luces/cámaras como
procesos con `CSUBTYPE` = `C3D_ENTITY/LIGHT/CAMERA`.

---

## 6. Referencia de la API por subsistemas

> Formato: **`NOMBRE_BGD`** `(firma)` → retorno — descripción.
> Recuerda la leyenda de firmas de la [§2](#cómo-leer-las-firmas-importante-para-este-documento).
>
> **Orden de parámetros verificado** contra las firmas C reales y los *wrappers*
> `*_bgd` (segunda pasada). Donde una función recibe varios `float`/`int`, se
> listan sus parámetros en el orden exacto en que hay que pasarlos.

### 6.1 Escena

| Función | Firma | Descripción |
|---|---|---|
| `G3D_SCENE_CREATE` | `S`→I | Crea una escena con nombre; devuelve su id. |
| `G3D_SCENE_DESTROY` | `I`→I | Destruye la escena. |
| `G3D_SCENE_SET_ACTIVE` | `I`→I | Fija la escena activa (la que se renderiza). |

### 6.2 Entidades

| Función | Firma | Descripción |
|---|---|---|
| `G3D_ENTITY_SPAWN` | `IIFFF`→I | Crea una entidad en `(scene, model, x, y, z)`; devuelve id. |
| `G3D_ENTITY_DESTROY` | `I`→I | Destruye una entidad. |
| `G3D_MODEL_DESPAWN` | `I`→I | Elimina un modelo instanciado con `G3D_MODEL_SPAWN` (raíz + submallas). |
| `G3D_ENTITY_SET_POSITION` | `IFFF`→I | Posición mundial de la entidad. |
| `G3D_ENTITY_SET_ROTATION` | `IFFF`→I | Rotación `(pitch, yaw, roll)`. |
| `G3D_ENTITY_SET_SCALE` | `IFFF`→I | Escala `(sx, sy, sz)`. |
| `G3D_ENTITY_GET_POSITION` | `IPPP`→I | Lee la posición en 3 variables por referencia. |
| `G3D_ENTITY_SET_PARENT` | `II`→I | Emparenta la entidad a otra (jerarquía de transforms). |
| `G3D_ENTITY_SET_MESH` | `II`→I | Asigna una malla concreta a la entidad. |
| `G3D_ENTITY_SET_MATERIAL` | `II`→I | Asigna un material (override) a la entidad. |
| `G3D_ENTITY_SET_ALPHA` | `II`→I | Transparencia global de la entidad. |
| `G3D_ENTITY_SET_COLOR` | `IIII`→I | Tinte de color (RGBA como enteros). |
| `G3D_ENTITY_SET_BLEND` | `II`→I | Modo de mezcla (opaco/alpha/aditivo). |

### 6.3 Cámaras

| Función | Firma | Descripción |
|---|---|---|
| `G3D_CAMERA_CREATE` | (—)→I | Crea una cámara; devuelve id. |
| `G3D_CAMERA_SET_ACTIVE` | `I`→I | Cámara activa. |
| `G3D_CAMERA_SET_POSITION` | `IFFF`→I | Posición de la cámara. |
| `G3D_CAMERA_LOOK_AT` | `IFFFFFF`→I | Mira a `(tx,ty,tz)` con *up* `(ux,uy,uz)`. |
| `G3D_CAMERA_SET_PROJECTION` | `II`→I | Proyección: 0 perspectiva, 1 ortográfica. |
| `G3D_CAMERA_SET_FOV` | `IF`→I | Campo de visión vertical (grados). |

### 6.4 Carga y consulta de modelos

| Función | Firma | Descripción |
|---|---|---|
| `G3D_LOAD_GLTF` | `S`→I | Carga `.gltf`/`.glb` (malla+PBR+esqueleto+animaciones). Camino recomendado para personajes. |
| `G3D_LOAD_OBJ` | `S`→I | Carga Wavefront `.obj` (estático). |
| `G3D_LOAD_FBX` | `S`→I | Carga FBX (estático/skinned/animado, vía ufbx). |
| `G3D_LOAD_MD3` | `S`→I | Carga modelos MD3 (Quake III). |
| `G3D_LOAD_TR` | `S`→I | Carga un nivel nativo de Tomb Raider (TR1–TR5); devuelve el modelo del nivel. |
| `G3D_TR_PROBE` | `S`→I | Detecta la versión TR del fichero (1–5) sin cargarlo del todo. |
| `G3D_LOAD_GLTF_FRACTURED` | `S`→I | Carga un glTF pensado para fractura/destrucción. |
| `G3D_GLTF_SET_RECENTER` | `I`→I | Activa/desactiva recentrado del modelo glTF al origen. |
| `G3D_GLTF_SET_CHUNKING` | `F`→I | Tamaño de *chunk* espacial para partir mallas grandes de glTF. |
| `G3D_MODEL_MESH` | `I`→I | Devuelve la malla principal del modelo. |
| `G3D_MODEL_TEXTURE` | `I`→I | Devuelve la textura principal del modelo. |
| `G3D_MODEL_SUBMESH_COUNT` | `I`→I | Nº de submallas del modelo (p.ej. rooms de un nivel TR). |
| `G3D_MODEL_SUBMESH` | `II`→I | Devuelve la submalla `i`. |
| `G3D_MODEL_SUBMESH_TEXTURE` | `II`→I | Textura de la submalla `i` (−1 si no hay). |
| `G3D_MODEL_SUBMESH_MAP` | `III`→I | Mapa PBR de la submalla `i` por tipo (1 normal, 2 metallic, 3 roughness). |
| `G3D_MODEL_SUBMESH_LOD` | `III`→I | Fija/consulta el LOD de una submalla. |
| `G3D_MODEL_SUBMESH_CX/CY/CZ` | `II`→F | Centro (x/y/z) de la AABB de la submalla `i`. |
| `G3D_MODEL_SUBMESH_HX/HY/HZ` | `II`→F | Semiejes (x/y/z) de la AABB de la submalla `i`. |
| `G3D_MODEL_HEIGHT` | `I`→F | Altura del modelo (extensión en Y del bind‑pose). |
| `G3D_MODEL_SIZE` | `I`→F | Tamaño (diagonal/extensión) del modelo. |
| `G3D_MODEL_ORIENT` | `IFFF`→I | Reorienta el modelo (corrige ejes al importar). |
| `G3D_MODEL_SPAWN` | `IIFFFFF`→I | **Instancia un modelo entero** en `(scene, model, x, y, z, height, roty)`: crea una raíz + una entidad por submalla con materiales PBR automáticos. `height>0` escala a esa altura; `roty` en grados. Devuelve la entidad raíz. |
| `G3D_MODEL_SET_GPU_SKIN` | `II`→I | Activa *skinning* en GPU para el modelo. |
| `G3D_LOAD_TEXTURE` | `S`→I | Carga una textura suelta desde fichero; devuelve id. |

### 6.5 Animación esqueletal

| Función | Firma | Descripción |
|---|---|---|
| `G3D_MODEL_ANIM_COUNT` | `I`→I | Nº de animaciones del modelo. |
| `G3D_MODEL_ANIM_DURATION` | `II`→F | Duración (s) de la animación `a`. |
| `G3D_MODEL_ANIMATE` | `IIFI`→I | Reproduce la animación `a` en tiempo `t` (`loop` 0/1). |
| `G3D_MODEL_ANIMATE_BLEND` | `IIFIFFI`→I | Mezcla dos animaciones: `(model, a0, t0, a1, t1, peso, loop)` con `peso` 0..1. |
| `G3D_MODEL_ANIMATE_ALL` | `IFI`→I | Aplica una animación global a todo (`model, t, loop`). |
| `G3D_MODEL_REST_POSE` | `I`→I | Pone el modelo en su pose de reposo (bind). |
| `G3D_MODEL_LOCK_ROOT` | `II`→I | Bloquea la traslación del hueso raíz (anima "en el sitio"; la física mueve al personaje). |

### 6.6 Materiales

| Función | Firma | Descripción |
|---|---|---|
| `G3D_MATERIAL_CREATE` | (—)→I | Crea un material; devuelve id. |
| `G3D_MATERIAL_SET_COLOR` | `IFFFF`→I | Color base RGBA. |
| `G3D_MATERIAL_SET_METALLIC` | `IF`→I | Factor metálico 0..1. |
| `G3D_MATERIAL_SET_ROUGHNESS` | `IF`→I | Rugosidad 0..1. |
| `G3D_MATERIAL_SET_TEXTURE` | `III`→I | Asigna textura a un *slot* `(material, slot, texture)`. |
| `G3D_MATERIAL_SET_MAP` | `III`→I | Asigna mapa PBR `(material, tipo, texture)` — 1 normal, 2 metallic, 3 roughness. |

### 6.7 Luces

| Función | Firma | Descripción |
|---|---|---|
| `G3D_LIGHT_CREATE` | `IFFF`→I | Crea luz `(tipo, r, g, b)`; tipo 0 direccional, 1 punto, 2 foco. |
| `G3D_LIGHT_SET_POSITION` | `IFFF`→I | Posición (punto/foco). |
| `G3D_LIGHT_SET_DIRECTION` | `IFFF`→I | Dirección (direccional/foco). |
| `G3D_LIGHT_SET_INTENSITY` | `IF`→I | Intensidad. |
| `G3D_LIGHT_SET_COLOR` | `IFFF`→I | Color RGB. |
| `G3D_LIGHT_SET_RANGE` | `IF`→I | Rango (punto/foco). |
| `G3D_LIGHT_SET_CONE` | `IF`→I | Ángulo del cono (foco). |
| `G3D_LIGHT_ENABLE_SHADOW` | `II`→I | Activa sombras para esa luz. |
| `G3D_LIGHT_SET_SHADOW_QUALITY` | `II`→I | Resolución del shadow map (1024/2048/4096). |
| `G3D_SET_SUN` | `F`→I | Ajusta el sol (elevación/parámetro global de luz solar). |

### 6.8 Entorno y configuración de render

| Función | Firma | Descripción |
|---|---|---|
| `G3D_SET_CLEAR_COLOR` | `FFFF`→I | Color de borrado del fondo. |
| `G3D_SET_AMBIENT_LIGHT` | `FFFF`→I | Luz ambiente `(r,g,b,intensidad)`. |
| `G3D_SET_FOG` | `IFFFFF`→I | Niebla `(on, r, g, b, inicio, fin)`. |
| `G3D_SET_WIREFRAME_MODE` | `I`→I | Modo alambre on/off. |
| `G3D_SET_SHADOWS` | `I`→I | Sombras globales on/off. |
| `G3D_SET_HDR` | `I`→I | Render HDR on/off. |
| `G3D_SET_TONEMAP` | `I`→I | Tonemapping on/off. |
| `G3D_SET_EXPOSURE` | `F`→I | Exposición HDR. |
| `G3D_SET_BLOOM` | `IFF`→I | Bloom `(on, umbral, intensidad)`. |
| `G3D_SET_SSAO` | `IFF`→I | Oclusión ambiental en pantalla `(on, radio, intensidad)`. |
| `G3D_SET_IBL` | `IF`→I | Image Based Lighting `(on, intensidad)`. |
| `G3D_IBL_REFRESH` | (—)→I | Fuerza recomputar el IBL desde el cielo actual. |
| `G3D_SET_OCCLUSION` | `I`→I | Occlusion culling por hardware on/off. |
| `G3D_SET_SMAA` | `I`→I | Antialias SMAA on/off. |
| `G3D_SET_FSR` | `IFF`→I | FSR 1 `(on, escala_de_render, nitidez)` — reescalado espacial (escala < 1 = renderiza a menos y sube). |
| `G3D_SET_FSR_HEIGHT` | `IIF`→I | Configura FSR por altura de render. |
| `G3D_SET_LOD` | `F`→I | Distancia base de LOD. |
| `G3D_SET_CULLING` | `I`→I | Frustum culling on/off. |
| `G3D_SET_BACKFACE_CULL` | `I`→I | Descarte de caras traseras on/off. |
| `G3D_SET_UNDERWATER` | `IFFFF`→I | Efecto submarino `(on, r, g, b, densidad)`. |
| `G3D_RENDER_WIDTH` | (—)→I | Ancho del render actual. |
| `G3D_RENDER_HEIGHT` | (—)→I | Alto del render actual. |

### 6.9 Cielo

| Función | Firma | Descripción |
|---|---|---|
| `G3D_SKY_SET_GRADIENT` | `FFFFFF`→I | Gradiente `(cénit rgb, horizonte rgb)`. |
| `G3D_SKY_SET_CLOUDS` | `FF`→I | Nubes `(cobertura, velocidad)`. |
| `G3D_SKY_SET_LOW_CLOUDS` | `FFFF`→I | Nubes bajas `(cobertura, altura, tamaño, sombra)`. |
| `G3D_SKY_SET_TEXTURE` | `I`→I | Usa una textura panorámica como cielo. |
| `G3D_SKY_ENABLE` | `I`→I | Activa/desactiva el cielo. |

### 6.10 Agua (superficie y océano)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_WATER_CREATE` | `FFI`→I | Crea una superficie de agua `(nivel, tamaño, resolución)`. |
| `G3D_WATER_SET_WAVES` | `FFF`→I | Parámetros de olas `(amplitud, frecuencia, velocidad)`. |
| `G3D_WATER_SET_COLOR` | `FFFFFF`→I | Color del agua (poco/mucho fondo). |
| `G3D_WATER_SET_ENABLED` | `I`→I | Activa/desactiva el agua. |
| `G3D_WATER_SET_TEXTURE` | `I`→I | Textura del agua. |
| `G3D_WATER_SET_REFLECTION` | `IF`→I | Reflexión planar `(on, intensidad)`. |
| `G3D_WATER_SET_REFLECTION_FLIP` | `I`→I | Invierte la reflexión. |
| `G3D_WATER_SET_SSR` | `IF`→I | Reflexiones en espacio de pantalla `(on, intensidad)`. |
| `G3D_WATER_SET_OCEAN` | `FFF`→I | Modo océano `(dir_x, dir_z, amplitud_marejada)` — marejada + rompiente (0 = off). |
| `G3D_WATER_SET_TESSELLATION` | `I`→I | Teselación de la malla de agua. |
| `G3D_WATER_RIPPLE` | `FFF`→I | Genera una onda en `(x, z, fuerza)`. |
| `G3D_WATER_SPLASH` | `FFFF`→I | Salpicadura `(x, y, z, fuerza)`. |

### 6.11 Ríos y cascadas (flow)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_FLOW_ADD` | `FFFFFFFFF`→I | Cinta de agua `(top x,y,z, bottom x,y,z, ancho, velocidad, tiling)` — vertical = cascada, inclinada = río. |
| `G3D_FLOW_ADD_RIVER` | `IFFFFFFFF`→I | Río sobre terreno `(terrain_mesh, x0, z0, x1, z1, ancho, desfase_y, velocidad, tiling)`. |
| `G3D_FLOW_SET_TEXTURE` | `I`→I | Textura del flujo. |
| `G3D_FLOW_SET_COLOR` | `FFF`→I | Color del flujo. |
| `G3D_FLOW_SET_CLIP` | `F`→I | Plano de recorte. |
| `G3D_FLOW_CLEAR` | (—)→I | Borra todos los flujos. |

### 6.12 Fuego y partículas

| Función | Firma | Descripción |
|---|---|---|
| `G3D_FIRE_ADD` | `FFFF`→I | Añade una llama en `(x, y, z, tamaño)` con luz cálida. |
| `G3D_FIRE_CLEAR` | (—)→I | Borra todos los fuegos. |
| `G3D_PARTICLES_CREATE` | `FFFFFFFFFFFFFI`→I | Emisor: `(x, y, z, ext_x, ext_y, ext_z, vel_x, vel_y, vel_z, dispersión, gravedad, tamaño, vida, nº)`. |
| `G3D_PARTICLES_SET_COLOR` | `IFFF`→I | Color de las partículas. |
| `G3D_PARTICLES_SET_FLOOR` | `IF`→I | Altura del suelo (rebote/parada). |
| `G3D_PARTICLES_CLEAR` | (—)→I | Borra todos los emisores. |

### 6.13 Espejos

| Función | Firma | Descripción |
|---|---|---|
| `G3D_MIRROR_CREATE` | `FFFFFFFF`→I | Espejo plano `(px, py, pz, normal_x, normal_y, normal_z, ancho, alto)`. |
| `G3D_MIRROR_SET_FLIP` | `II`→I | Invierte la reflexión del espejo. |
| `G3D_MIRROR_SET_TINT` | `IFFF`→I | Tinte del espejo. |
| `G3D_MIRROR_SET_DISTANCE` | `F`→I | Distancia máxima de reflexión. |
| `G3D_MIRROR_CLEAR` | (—)→I | Borra los espejos. |

### 6.14 Colisión simple y raycast (`libmod_3d_collide.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_ENTITY_SET_COLLIDER` | `II`→I | Marca la entidad como sólida (aporta su AABB a la colisión). |
| `G3D_COLLIDE_MOVE` | `FFFFFFF`→I | Mueve un cilindro con *collide‑and‑slide* contra las cajas; resultado en `G3D_COLLIDE_X/Z`. |
| `G3D_COLLIDE_X` | (—)→F | X resultante del último `COLLIDE_MOVE`. |
| `G3D_COLLIDE_Z` | (—)→F | Z resultante del último `COLLIDE_MOVE`. |
| `G3D_COLLIDE_FLOOR` | `FFFF`→F | Altura del suelo bajo un punto (vs cajas). |
| `G3D_RAYCAST` | `FFFFFFPPP`→I | Lanza un rayo `(origen, dir, ...)`; hit por referencia y `G3D_RAY_*`. |
| `G3D_RAY_HIT_X/Y/Z` | (—)→F | Punto de impacto del último raycast. |
| `G3D_RAY_ENTITY` | (—)→I | Entidad impactada por el último raycast. |

### 6.15 Character controller y física cinemática (`libmod_3d_physics.c` / Jolt)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_CHAR_CREATE` | `FFFFF`→I | Crea un *character controller* de cápsula `(x, y, z, radio, altura)`. |
| `G3D_CHAR_DESTROY` | `I`→I | Destruye el controller. |
| `G3D_CHAR_MOVE` | `IFF`→I | Fija la intención de movimiento horizontal `(vx, vz)`. |
| `G3D_CHAR_JUMP` | `IF`→I | Salta con impulso dado. |
| `G3D_CHAR_UPDATE` | `IF`→I | Integra la física `dt` (gravedad, deslizamiento, escalones). |
| `G3D_CHAR_SET_POSITION` | `IFFF`→I | Teletransporta el controller. |
| `G3D_CHAR_SET_TUNING` | `IFF`→I | Ajuste `(altura de escalón, pendiente máx en grados)`. |
| `G3D_CHAR_X/Y/Z` | `I`→F | Posición actual del controller. |
| `G3D_CHAR_GROUNDED` | `I`→I | 1 si está en el suelo. |
| `G3D_PHYSICS_SET_GRAVITY` | `F`→I | Gravedad global. |
| `G3D_COLLIDER_ADD_BOX` | `FFFFFF`→I | Añade una caja AABB estática `(min, max)`. |
| `G3D_COLLIDER_ADD_MESH` | `IIFFFF`→I | Añade una submalla como colisión de malla estática `(model, submesh, x, y, z, escala)`. |
| `G3D_COLLIDER_CLEAR` | (—)→I | Borra todos los colliders. |

### 6.16 Rigid bodies (Jolt)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_RIGIDBODY_CREATE` | `FFFFFFF`→I | Crea un cuerpo rígido tipo caja `(pos, semiejes, masa)`. |
| `G3D_RIGIDBODY_CREATE_SPHERE` | `FFFFF`→I | Cuerpo esfera `(pos, radio, masa)`. |
| `G3D_RIGIDBODY_CREATE_CAPSULE` | `FFFFFF`→I | Cuerpo cápsula. |
| `G3D_RIGIDBODY_CREATE_CYLINDER` | `FFFFFF`→I | Cuerpo cilindro. |
| `G3D_RIGIDBODY_CREATE_CONVEX` | `FFFIIFF`→I | Cuerpo convexo desde geometría. |
| `G3D_RIGIDBODY_SET_CCD` | `II`→I | Activa *continuous collision detection*. |
| `G3D_RIGIDBODY_DESTROY` | `I`→I | Destruye el cuerpo. |
| `G3D_RIGIDBODY_CLEAR` | (—)→I | Borra todos los cuerpos. |
| `G3D_RIGIDBODY_STEP` | `F`→I | Avanza la simulación `dt`. |
| `G3D_RIGIDBODY_APPLY_IMPULSE` | `IFFF`→I | Aplica un impulso. |
| `G3D_RIGIDBODY_SET_VELOCITY` | `IFFF`→I | Fija la velocidad lineal. |
| `G3D_RIGIDBODY_SET_ANGULAR_VELOCITY` | `IFFF`→I | Fija la velocidad angular. |
| `G3D_RIGIDBODY_SET_BOUNCE` | `IFF`→I | Rebote/fricción. |
| `G3D_RIGIDBODY_X/Y/Z` | `I`→F | Posición del cuerpo. |
| `G3D_RIGIDBODY_ANGLE_X/Y/Z` | `I`→F | Ángulos del cuerpo. |
| `G3D_RIGIDBODY_RENDER_X/Y/Z` | `I`→F | Posición interpolada para render. |
| `G3D_RIGIDBODY_GROUNDED` | `I`→I | 1 si está apoyado. |
| `G3D_RIGIDBODY_SET_MODEL_OFFSET` | `IFFF`→I | Desfase visual del modelo respecto al cuerpo. |
| `G3D_PHYSICS_BODY_CREATE` | `IIFF`→I | (API antigua) crea cuerpo asociado a entidad `(entity, shape, masa, radio)`. |
| `G3D_PHYSICS_BODY_SET_VELOCITY` | `IFFF`→I | (API antigua) velocidad del cuerpo. |
| `G3D_PHYSICS_STEP` | `F`→I | (API antigua) paso de simulación. |

### 6.17 Vehículos

| Función | Firma | Descripción |
|---|---|---|
| `G3D_VEHICLE_CREATE` | `FFFF`→I | Crea un vehículo `(x, y, z, rumbo)`. |
| `G3D_VEHICLE_DESTROY` | `I`→I | Destruye el vehículo. |
| `G3D_VEHICLE_UPDATE` | `IFFFF`→I | Actualiza `(vehículo, dt, acelerador, giro, freno)`. |
| `G3D_VEHICLE_SET_GEOMETRY` | `IFFF`→I | Dimensiones del chasis. |
| `G3D_VEHICLE_SET_TUNING` | `IFFFFF`→I | Ajuste `(vehículo, motor, vel_máx, giro_máx_grados, freno, resistencia)`. |
| `G3D_VEHICLE_X/Y/Z` | `I`→F | Posición del vehículo. |
| `G3D_VEHICLE_YAW/PITCH/ROLL` | `I`→F | Orientación del vehículo. |
| `G3D_VEHICLE_SPEED` | `I`→F | Velocidad actual. |

### 6.18 Instancing y *scatter* (`libmod_3d_instance.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_INSTANCES_CREATE` | `II`→I | Crea un grupo de instancias `(scene, model)`. |
| `G3D_INSTANCES_CREATE_SKINNED` | `III`→I | Grupo de instancias con animación (skinned). |
| `G3D_INSTANCES_ADD` | `IFFFFF`→I | Añade una instancia `(pos, rot, escala…)`. |
| `G3D_INSTANCES_SET` | `IIFFFFF`→I | Modifica la instancia `i`. |
| `G3D_INSTANCES_SET_WIND` | `IF`→I | Balanceo por viento del grupo. |
| `G3D_INSTANCES_SET_ALPHA_CUT` | `II`→I | *Alpha cutoff* (follaje). |
| `G3D_INSTANCES_SET_DISTANCE` | `IF`→I | Distancia de culling/LOD del grupo. |
| `G3D_INSTANCES_CLEAR` | `I`→I | Vacía el grupo. |
| `G3D_INSTANCES_COUNT` | `I`→I | Nº de instancias. |
| `G3D_SCATTER_MODEL` | `IIIFFFFI`→I | Esparce un modelo `(model, terrain, nº, área, altura_objetivo, var_escala, viento, semilla)`. |
| `G3D_SCATTER_MESH` | `IIIIFFFFI`→I | Esparce una malla `(mesh, textura, terrain, nº, área, escala_mín, escala_máx, viento, semilla)`. |

### 6.19 Mundos grandes: rebase y streaming

| Función | Firma | Descripción |
|---|---|---|
| `G3D_WORLD_REBASE` | `FFF`→I | Reancla el origen del mundo (precisión en mundos enormes). |
| `G3D_STREAM_INIT` | `FI`→I | Inicializa el streaming `(tamaño de tile, radio)`. |
| `G3D_STREAM_UPDATE` | `FF`→I | Actualiza con la posición de cámara `(x, z)`; calcula tiles que entran/salen. |
| `G3D_STREAM_LOAD_COUNT` | (—)→I | Nº de tiles que acaban de entrar (a poblar). |
| `G3D_STREAM_LOAD_X/Z` | `I`→I | Coord del i‑ésimo tile a cargar. |
| `G3D_STREAM_UNLOAD_COUNT` | (—)→I | Nº de tiles que salieron (a liberar). |
| `G3D_STREAM_UNLOAD_X/Z` | `I`→I | Coord del i‑ésimo tile a descargar. |
| `G3D_STREAM_LOADED_COUNT` | (—)→I | Nº de tiles cargados actualmente. |

### 6.20 Generación procedural de terreno (`libmod_3d_worldgen.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_WORLDGEN_SET` | `IFFF`→I | Configura el generador `(semilla, amplitud, frecuencia, nivel_del_mar)`. |
| `G3D_WORLDGEN_HEIGHT` | `FF`→F | Altura procedural en `(x, z)`. |
| `G3D_WORLDGEN_TILE` | `IIIFIFFI`→I | Malla de un tile `(scene, tile_x, tile_z, tamaño, resolución, origen_x, origen_z, textura)`. |
| `G3D_WORLDGEN_TILE_FREE` | `I`→I | Libera una malla de tile. |
| `G3D_WORLDGEN_SET_WATER_DEPTH` | `F`→I | Cuánto se hunde el fondo del océano (1 = def, >1 = más profundo). |
| `G3D_WORLDGEN_SET_BIOME_TEXTURES` | `IIIIF`→I | Texturas por bioma `(arena, hierba, roca, nieve, escala)`. |

### 6.21 Escenas `.scene` y consultas de mundo (`libmod_3d_scenefile.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_SCENE_LOAD` | `S`→I | Carga un mapa declarativo `.scene` (terreno, cielo, luces, vegetación, modelos, prefabs). |
| `G3D_SCENE_TERRAIN_HEIGHT` | `FF`→F | Altura del terreno de la escena en `(x, z)`. |
| `G3D_SCENE_WATER_LEVEL` | (—)→F | Nivel del agua de la escena. |
| `G3D_SCENE_HAS_SPAWN` | (—)→I | 1 si la escena define punto de aparición. |
| `G3D_SCENE_SPAWN_X/Y/Z` | (—)→F | Punto de aparición del jugador. |
| `G3D_SCENE_PLAYER_RADIUS` | (—)→F | Radio sugerido del jugador. |
| `G3D_SCENE_PLAYER_HEIGHT` | (—)→F | Altura sugerida del jugador. |
| `G3D_SCENE_PLAYER_CLIMB` | (—)→F | Altura de escalón sugerida. |

### 6.22 Terreno vóxel (`libmod_3d_voxterrain.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_VOXTERRAIN_FLOOR` | `FFF`→F | Altura de la superficie vóxel bajo `(x, y, z)`. |
| `G3D_VOXTERRAIN_SOLID` | `FFF`→I | 1 si la celda `(x,y,z)` es sólida. |
| `G3D_VOXTERRAIN_BLOCKED` | `FFFFF`→I | 1 si un movimiento choca con vóxel sólido. |
| `G3D_VOXTERRAIN_WORLDSIZE` | (—)→F | Tamaño del mundo vóxel. |
| `G3D_VOXTERRAIN_WALK` | `FFFFFFFF`→I | *Move‑and‑slide* contra la superficie vóxel; resultado en `WALKX/Y/Z`. |
| `G3D_VOXTERRAIN_WALKX/Y/Z` | (—)→F | Posición resultante del último `WALK`. |

### 6.23 Picking (`libmod_3d_pick.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_PICK_TERRAIN` | `IFFFFI`→I | Rayo pantalla→terreno `(cámara, píxel_x, píxel_y, ancho, alto, terrain)`; hit en `PICK_X/Y/Z`. |
| `G3D_PICK_X/Y/Z` | (—)→F | Punto del terreno bajo el cursor. |

### 6.24 Primitivas y edición de terreno (`libmod_3d_primitives.c` / `terrain.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_PRIMITIVE_CUBE` | (—)→I | Crea una malla de cubo. |
| `G3D_PRIMITIVE_SPHERE` | `I`→I | Crea una esfera (subdivisiones). |
| `G3D_PRIMITIVE_PLANE` | (—)→I | Crea un plano. |
| `G3D_PRIMITIVE_TERRAIN` | `IFFFI`→I | Terreno heightmap `(grid, tamaño_mundo, altura, tiling, semilla)`. |
| `G3D_PRIMITIVE_CLIFFS` | `IFFFIFF`→I | Acantilados `(grid, tamaño_mundo, altura, tiling, semilla, pendiente, suelo_agua)`. |
| `G3D_PRIMITIVE_MOUNTAIN` | `IFFFIF`→I | Montaña `(grid, tamaño_mundo, pico, tiling, semilla, canal)`. |
| `G3D_TERRAIN_GET_HEIGHT` | `IFF`→F | Altura del terreno en `(x, z)`. |
| `G3D_TERRAIN_RAISE` | `IFFFF`→I | Pincel subir/bajar `(mesh, wx, wz, radio, fuerza)`. |
| `G3D_TERRAIN_SMOOTH` | `IFFFF`→I | Pincel suavizar `(mesh, wx, wz, radio, cantidad)`. |
| `G3D_TERRAIN_FLATTEN` | `IFFFFF`→I | Pincel aplanar `(mesh, wx, wz, radio, altura_objetivo, cantidad)`. |

### 6.25 Pintura de terreno (`libmod_3d_paint.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_PAINT_CREATE` | `II`→I | Crea un lienzo de pintura para un terreno. |
| `G3D_PAINT_FILL` | `IIF`→I | Rellena con una textura base. |
| `G3D_TERRAIN_PAINT` | `IIIFFFFF`→I | Pinta `(terrain, lienzo, textura, tiling_textura, wx, wz, radio, opacidad)`. |
| `G3D_PAINT_GET_TEXTURE` | `I`→I | Textura horneada resultante. |
| `G3D_PAINT_SAVE` | `IS`→I | Guarda el lienzo a fichero. |

### 6.26 Prefabs (`libmod_3d_prefab.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_PREFAB_CREATE` | `S`→I | Crea un prefab con nombre. |
| `G3D_PREFAB_ADD_BOX` | `IFFFFFFFFFFFFI`→I | Pieza caja `(prefab, px,py,pz, sx,sy,sz, rx,ry,rz, r,g,b, collider)`. |
| `G3D_PREFAB_PIECE_TEXTURE` | `IS`→I | Textura de la última pieza. |
| `G3D_PREFAB_SAVE` | `IS`→I | Guarda el prefab a fichero de texto. |
| `G3D_PREFAB_LOAD` | `S`→I | Carga un prefab de fichero. |
| `G3D_PREFAB_INSTANTIATE` | `IIFFFF`→I | Instancia el prefab en `(scene, x, y, z, yaw)`. |
| `G3D_PREFAB_COUNT` | `I`→I | Nº de piezas del prefab. |

### 6.27 Tela (`libmod_3d_cloth.c`)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_CLOTH_CREATE` | `FFIIFFF`→I | Crea una tela `(ancho, alto, nx, ny, px, py, pz)`. |
| `G3D_CLOTH_PIN` | `II`→I | Ancla un punto de la rejilla. |
| `G3D_CLOTH_SET_WIND` | `IFFFF`→I | Viento sobre la tela. |
| `G3D_CLOTH_SET_COLLIDER` | `IFFFF`→I | Esfera que empuja la tela `(x, y, z, radio)`. |
| `G3D_CLOTH_CLEAR_COLLIDER` | `I`→I | Quita el collider. |
| `G3D_CLOTH_SET_TEXTURE` | `II`→I | Textura de la tela. |
| `G3D_CLOTH_UPDATE` | `IF`→I | Integra la simulación `dt`. |

### 6.28 Ratón (captura relativa)

| Función | Firma | Descripción |
|---|---|---|
| `G3D_MOUSE_CAPTURE` | `I`→I | Captura/libera el ratón (modo relativo para mirar). |
| `G3D_MOUSE_UPDATE` | (—)→I | Lee el delta del ratón este frame. |
| `G3D_MOUSE_DX` | (—)→I | Delta X desde el último `UPDATE`. |
| `G3D_MOUSE_DY` | (—)→I | Delta Y desde el último `UPDATE`. |

---

## Apéndice A — Ejemplos en `TEST/`

Hay ~45 demos `.PRG` en [`TEST/`](TEST/) que ejercitan cada subsistema. Algunas de referencia:

- **`TEST_TR.PRG`** — nivel nativo de Tomb Raider + Lara jugable (FBX animado, colisión por rooms, 3ª persona).
- **`TEST_CHARACTER.PRG`** — personaje jugable 3ª persona con blending idle/walk/run (glTF).
- **`TEST_WALK.PRG`** — character controller 1ª persona sobre terreno con colisión de cajas.
- **`TEST_ANIM.PRG`** — reproductor de animaciones esqueletales (LEFT/RIGHT cambian).
- **`TEST_WATER.PRG` / `TEST_WATERFALL.PRG`** — agua, olas, cascadas.
- **`TEST_PHYSICS.PRG` / `TEST_FRACTURE.PRG` / `TEST_DRIVE.PRG`** — Jolt: rigid bodies, destrucción, vehículos.
- **`TEST_OPENWORLD.PRG` / `TEST_STREAM.PRG` / `TEST_GTA3_STREAM.PRG`** — mundos grandes (instancing + streaming).
- **`TEST_IBL.PRG` / `TEST_SHADOW.PRG` / `TEST_FSR.PRG`** — iluminación, sombras, reescalado.

Compila y ejecuta cualquiera con `bgdc X.PRG && bgdi X.dcb` desde `TEST/`.

---

## Apéndice B — Resumen de cómo compilar (chuleta)

```bash
# 1) Build completo de BennuGD2 + módulo 3D (desde la raíz del repo)
cd /home/ruben/BennuGD2
./build.sh linux                 # salida: build/linux-gnu/bin/ (bgdc, bgdi, libmod_3d.so)

# 2) Recompilar SOLO el módulo tras tocar C del motor
cd /home/ruben/BennuGD2/build/linux-gnu
make mod_3d                      # target CMake = mod_3d  ->  ../../bin/libmod_3d.so

# 3) Compilar y ejecutar una demo
cd /home/ruben/BennuGD2/modules/libmod_3d/TEST
bgdc TEST_TR.PRG && bgdi TEST_TR.dcb

# Físicas: Jolt va ON por defecto (vendored en jolt/). Para el fallback propio:
#   cmake ... -DUSE_JOLT=OFF
```
