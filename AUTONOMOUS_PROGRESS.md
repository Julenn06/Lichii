# Registro de progreso autónomo

Registro operativo para reanudar el trabajo entre sesiones. Conciso y técnico.

## Estado del proyecto

- **Qué es:** Lichii «Frizti v3 mejorado», fork de `zortos293/Lichii` (a su vez de Eglantine/Eden/yuzu).
  Emulador de Nintendo Switch / Switch 2 en C++20, Qt 6.8.3, Vulkan, JIT dynarmic (ARM64 → x86-64).
- **Base upstream:** commit `00ef0ea6cc` (línea experimental *Frizti*, se separó de `nx2` el 2026-11-05).
  `nx2` upstream lleva 222 commits más (2026-11-09) y porta parte del trabajo de Frizti.
- **Repo:** `Julenn06/Lichii`, rama `main`. Última release: ver `dist/mejorado_version.txt`.
- **Equipo del usuario:** Ryzen 5 5600X (6c/12t), RTX 4060 Ti, 32 GB, monitor 250 Hz, Windows 11.
- **Juego de referencia:** The Legend of Zelda: Ocarina of Time (`0400486024DEC000`, Switch 2).

## Entorno de compilación (Windows, este PC)

- Código: `E:\Emuladores\Nintendo Switch 2\Aplicacion\Lichii-Decompilado` (clon de GitHub).
  Enlace sin espacios: `E:\lsrc`.
- Scripts en `E:\lxtools` (siempre con `VSLANG=1033`; si no, Ninja no ve cambios en cabeceras):
  - MSVC: `configure.bat [dir]`, `build.bat <dir> <targets>` → árbol `E:\lbuild`.
  - clang-cl 23.1.5 (portable en `E:\lxllvm`): `configure_clang.bat <dir>`, `build_clang.bat <dir> <targets>`.
    Flags por las variables `LX_CFLAGS` / `LX_LDFLAGS`. Los argumentos `-DX=Y` no pasan por
    `cmd /c` (el `=` separa argumentos): añadirlos dentro del `.bat`.
- Árboles: `E:\lbuild` (MSVC), `E:\lbgen` (clang instrumentado, `-fprofile-generate`),
  `E:\lrel` (release: clang + PGO + ThinLTO; `configure_release.bat`), `E:\lrelavx2` (igual con AVX2).
- `E:\lxsrc` y `E:\lxbuild` son de la carpeta antigua (enlace roto): no usar.

## Banco de pruebas reproducible

- `E:\lbench\app`: copia aislada del emulador con su propio `user/` (configuración sin sonido ni
  diálogos, NAND y caché copiadas; `keys` es un enlace a las del usuario, nunca se leen).
- `python E:\lxtools\bench.py --name <n> [--exe <dir>] [--env K=V] [--seconds 30] [--repeat 2] [--sample]`
  arranca el juego, carga la partida del banco (balcón de la casa de Link, Bosque Kokiri) con el
  inyector de entrada del servidor de depuración, mide FPS por el contador de frames presentados
  y cierra. Resultados en `E:\lbench\runs\<n>\result.json`. `--train <s>` hace un recorrido para PGO.
- Sin teclado simulado ni capturas de pantalla: las imágenes salen del propio emulador
  (`E:\lxtools\shot.sh <nombre>`).
- Ruido entre sesiones: ±5 %. Repetir cada variante antes de sacar conclusiones. No medir
  mientras compila otro árbol.

## Diagnóstico de rendimiento (medido 2026-11-10)

- Punto fijo (Kokiri, balcón): **~30,6 FPS** con v3.7.9 (MSVC, interruptores por defecto);
  con `async_pipelines` + `PREFLUSH=1`: 31,7 (dentro del ruido, con caídas a 15-19).
- Perfil por muestreo (10 s): el hilo `GPU` (front) está al 100 %: ~47 % trabajo y ~53 % esperando
  al hilo `GPUBack` (`GpuSplit::Drain`): lecturas DMA de macros (`FrontDmaRead`, 31 %) y
  semáforos (`ProcessSemaphoreTriggerMethod`, 25 %). `GPUBack` ~74 % ocupado (draws 41 %,
  compute 11 %). Los 6 `CPUCore_*` solo ~36 % en código JIT.
  **El límite es el código C++ de emulación de GPU, no la CPU emulada.**
- Referencia upstream en el mismo punto: 48–49,5 FPS (5900X, clang, `PREFLUSH`), comentario en
  `src/video_core/nx2_perf.h`.

## Hecho en esta sesión (2026-11-10)

- Repo: faltaban `src/audio_core/out/*` (los ignoraba `.gitignore: out/`); recuperados del commit
  base de upstream y la regla limitada a `/out/`. El repositorio público no compilaba desde cero.
- **v3.8.0 publicada**: motor compilado con clang-cl 23 + PGO propio de Ocarina
  (`dist/pgo/lichii-oot.profdata`) + ThinLTO. Opciones de CMake `LICHII_PGO_PROFILE`,
  `LICHII_PGO_GENERATE`, `LICHII_THIN_LTO`. Medido: MSVC 31,7 FPS → 37,1 FPS (+17 %).
- Compila con clang-cl + STL de MSVC 14.52 (`FindStorageAddr` en
  `shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp`).
- Cierre: `GPU::Impl::NotifyShutdown` libera a quien espera una operación de sincronización
  (port de upstream #1058). Si aun así se cuelga, `shutdown_hang_stacks.txt` junto al log.
- `src/yuzu/crash_report.cpp`: informe de fallos en Windows (`.txt` + `.dmp` en `user/crash_dumps`,
  se guardan cinco). Verificado con el método `crash_test` del servidor de depuración.
- `nx2_sampler.cpp`: el recorrido de pilas va protegido con SEH (antes tumbaba el proceso en
  builds de clang).
- `tools/bench`: banco de pruebas, scripts de compilación y `package.ps1`.

## Resultados A/B (build clang + PGO, balcón de Kokiri, 2 x 30 s)

| Variante | FPS |
|---|---|
| por defecto | 38,4 |
| `EDEN_NX2_ACQUIRE_PRECHECK=2` | 38,0 |
| `EDEN_NX2_ACQUIRE_EQUAL=1` | 38,4 |
| `EDEN_NX2_PERF_ON=front_sleep` | 37,7 |
| `EDEN_NX2_PREFLUSH=1` | 37,3 |
| `EDEN_NX2_IDLE_SPIN_US=0` / `300` | 39,0 / 37,7 |

Ninguno sale del ruido (±2 %). Lecturas DMA de macros: ~24.000/s sin drain y ~3.400/s con drain,
el 82 % de estas por «compute/copy enviado desde la instantánea» (`GpuSplit::MayBeWrittenSince`).

## Generación de frames y afinidad (medido 2026-11-10, build de release)

| Variante | Juego | En pantalla (Host pacing) |
|---|---|---|
| por defecto | 37,1 FPS | 38,6 FPS, intervalo medio 26 ms, máx. 34-38 |
| `frame_gen` FSR3 x2 | 35,6 FPS | 72 FPS, medio 13,9 ms, máx. 30-35 |
| `frame_gen` FSR3 x2 + `oot_30fps` | 30,0 FPS | **60,0 FPS**, medio 16,67 ms, máx. 37 |
| `oot_30fps` | 30,0 FPS | 30 FPS, medio 33,3 ms, máx. 40-44 |

De ahí la opción «60 FPS suaves» del diálogo de Ocarina (`src/yuzu/oot_frame_rate.cpp`,
ajuste `oot_smooth_offered`): pone `oot_30fps`, `frame_gen`, tipo FSR3 y x2 antes de crear el
renderizador. `frame_gen` no es modificable en caliente (modo de presentación, almacenamiento de
los frames y número de imágenes se fijan al crear el swapchain).

Afinidad de hilos desde fuera (`bench.py --affinity`), 2 x 30 s: control 37,6; GPU y GPUBack con
núcleo físico propio y el resto en 4 núcleos 38,4; GPU/GPUBack/VulkanWorker en un lógico cada uno
y el resto en 3 núcleos 39,7 (+5 %); núcleos preferentes sin excluir al resto 37,7. No se adopta:
ganancia pequeña y limita los núcleos emulados.

## Pasada 23 (2026-11-10): ports pequeños de upstream nx2, v3.8.2

Portados a mano (leídos enteros) desde `upstream/nx2`: #446/#621 (sondeo de canales suspendidos),
#250 (filtro de páginas en `InnerInvalidation`), #264 (trampas de lectura solo en páginas sin
armar), #481 (`RangeSet` en array ordenado), #580 (caché de rangos virtuales), #700 (bloques
enteros en el acumulador, `IterateCache` corta pronto), #479 (carrera en `synced`), #404, y #1016
(`HostTiming` duerme en el sistema; sin el método `host_timing` del servidor de depuración).

| Variante (Kokiri, 2 x 30 s) | FPS |
|---|---|
| v3.8.1 (controles intercalados) | 37,4 / 38,0 / 37,8 / 39,1 / 38,0 / 38,0 |
| solo #446/#621 | 36,7 / 37,6 (sin cambio) |
| lote sin #1016 | 40,9 / 39,9 / 39,2 |
| lote con #1016 | 40,0 / 40,6 |
| `resolution_setup=1` (1/2X) / `nvidia_dlss=false` | 38,6 / 38,9 (la GPU real no limita) |

`HostTiming` con #1016: ~2.950 esperas/s, CPU del hilo 0-8 %, eventos tarde p50/p99 0,2/0,6 ms.
«60 FPS suaves» sin cambio (60,0 FPS, 16,67 ms de media, máx. 37-40).

Perfil interno (`EDEN_NX2_GPU_PROFILE=1`, ms por segundo a 37 FPS):
- Hilo `GPU` (front): split drain 429 (8.239/s), list GPU wait 168 (116/s, 1,4 ms cada una:
  espera a la GPU real para leer listas escritas por compute), resume pending 191, macro 186,
  pending sleep 4.
- Hilo `GPUBack`: ocupado ~750 (method 184, bind stage buffers 110, other 80-100, draw 55,
  fence 46, update buffers 45, read handles 35, compute 34, pipeline lookup 31); split wait 251.
- Techo estimado quitando todas las esperas del front: ~49 FPS (el back sería el límite), que
  coincide con lo que upstream mide en este punto con nx2 (48-54 FPS en 5900X / 13900K).

No aplicables a esta base: #910 (no existe `ResolveMemoizedTextures`), HMMA en matrices
cooperativas (#582/#583: aquí la red del juego ya se sustituye por DLSS/FSR 3.1). Con conflictos
en 12-17 archivos cada uno (equivale a migrar): #858, #921, #949, #1028, #1056. El PR #805 de
upstream también está hecho sobre nx2, no sobre la línea Frizti.

Los símbolos del muestreador en la build de release (ICF) salen cambiados de nombre; para
perfiles por función usar una build con `/OPT:NOICF` o las zonas de `EDEN_NX2_GPU_PROFILE`.

## Comparación con upstream nx2 (2026-11-10, con permiso del usuario)

Binarios oficiales de nx2 build 130 (PGO, incluye #858, #949, #1028 y #1056) en el mismo banco:
`bench.py --app E:/lbench/upstream/b130-pgo` (sin `--exe`), datos en `E:/lbench/user-upstream`.

| Build | Ajustes | FPS | ms por frame (su registro «last 600 presents») |
|---|---|---|---|
| v3.8.2 | banco | 38,2 - 40,5 | 24,0 - 25,0 |
| nx2 b130 | por defecto (GPU «Alta»: sin división de hilos) | 28 - 30 | - |
| nx2 b130 | igualados a los nuestros, hilos de GPU fijados (su defecto) | 33,3 / 33,3 | 30,2 - 30,5 |
| nx2 b130 | igualados, `pin_gpu_threads=false` | 25,1 | 39,4 - 40,7 |

- **nx2 es un 15-20 % más lento que este fork en este PC y esta escena. No se migra.**
- Para comparar: la precisión de GPU por defecto de nx2 es «Alta» y con ella `GpuSplit` no arranca.
  La configuración de `user-upstream` fija todos los ajustes de Renderer/Core/Cpu/System a los
  valores efectivos de este fork (copia previa `qt-config.ini.bak_defaults`).
- La caché de shaders de nx2 es otra y solo se guarda al cerrar limpio: calentarla con 2-3
  ejecuciones y descartar las que tengan muchas líneas `async_pipelines`.
- `EDEN_NX2_VSYNC_HZ=240` en nx2: series con paradas y un cuelgue al cerrar; sin dato fiable.
- Este árbol ya fija los hilos de GPU a un núcleo físico cada uno con 6 o más núcleos
  (`Common::PinCurrentThread`, `EDEN_NX2_PIN_THREADS=0` lo quita). Sin medir aún sin fijar.

## Congelación vista una vez en v3.8.2 (2026-11-10)

`E:/lbench/runs/ours382_e`: el juego dejó de presentar hacia el segundo 81 (carga de la partida),
0 FPS y cierre forzado. `shutdown_hang_stacks.txt` (posterior a la orden de cierre): `GPUBack`
esperando un `KLightLock` dentro de `INx2GpuSession::BackSparseWrite` -> `MapPageGroup`
(`src/core/hle/service/nx2_gsv/gpu.h`), `CPUCore_1` en `GPU::OnCPURead` esperando la operación de
sincronización, el resto de núcleos emulados en reposo. Ese código es anterior a los ports de la
pasada 23. No reproducida en 7 arranques seguidos ni en los otros ~27 de las builds de la pasada.
`bench.py` guarda ahora `freeze_samples.txt` (pilas en el momento de la congelación) cuando una
medida da 0 FPS, y no mide con más de un 15 % de CPU ocupada por otros programas.

## Pendiente (por prioridad)

1. AVX2 medido (A/B/A, 3 x 30 s): 38,3 y 38,6 FPS frente a 37,7 sin AVX2 (+2 %, dentro del ruido).
   No se adopta: no compensa dejar fuera las CPU sin AVX2.
2. La pregunta nueva de Ocarina bloquea un banco sin `oot_smooth_offered=true` en su configuración.
3. nx2 medido (ver arriba): no se migra. Seguir portando a mano solo lo pequeño que aplique.
4. Ideas sin medir: `EDEN_NX2_PIN_THREADS=0` y otros repartos de núcleos en el 5600X; enlazar la
   release con `/OPT:NOICF` para que los informes de fallo y de cuelgue tengan símbolos fiables;
   reducir el trabajo por draw del hilo `GPUBack` (bind stage buffers, method).
5. Un cambio de versión o de `CMakeLists` provocó una recompilación casi completa del árbol
   clang (causa sin identificar; el commit posterior solo recompiló 18 pasos).

## Limitaciones

- No se puede borrar el enlace `E:\lxsrc` desde la sesión (bloqueado); inofensivo.
- El usuario usa el PC a ratos (compilaciones propias al 100 % de CPU): el banco se niega a
  medir entonces; esperar a que quede libre, nunca cerrar sus programas.
