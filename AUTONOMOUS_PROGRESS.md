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

## Pendiente (por prioridad)

1. AVX2 medido (A/B/A, 3 x 30 s): 38,3 y 38,6 FPS frente a 37,7 sin AVX2 (+2 %, dentro del ruido).
   No se adopta: no compensa dejar fuera las CPU sin AVX2.
2. Probar la generación de frames ya incluida (`frame_gen`, tipo FSR3, x2) como vía a 60 FPS
   presentados; documentar calidad y coste antes de recomendarla.
3. Requiere permiso del usuario: compilar y medir `upstream/nx2` (árbol en `E:\lup`, configurado
   en `E:\lupbuild`). Trae #1056, #949, #1028 y #858, que atacan la espera front/back medida.
   Si rinde más, migrar el fork sobre `nx2` (rama nueva desde nx2 + `git merge -s ours main`).
4. Al migrar conservar: tema `lichii_dark`, `es.ts`, actualizador contra `Julenn06/Lichii`,
   `nx2_switches_dialog`, `bug_report`, `crash_report`, «Continuar el último juego», versión del
   fork en el título, opciones PGO de CMake, `tools/bench`. Descartar el troceo de `nx2_gsv.h`.
5. Un cambio de versión o de `CMakeLists` provocó una recompilación casi completa del árbol
   clang (causa sin identificar; el commit posterior solo recompiló 18 pasos).

## Limitaciones

- No se puede borrar el enlace `E:\lxsrc` desde la sesión (bloqueado); inofensivo.
- El modo automático no permite ejecutar los binarios precompilados de upstream
  (`E:\lbench\upstream`, build-130) ni compilar el árbol `upstream/nx2`.
