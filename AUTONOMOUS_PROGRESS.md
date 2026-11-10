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
  `E:\lbclang` (clang + PGO propio + ThinLTO).
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

## Hecho en esta sesión

- Repo: faltaban `src/audio_core/out/*` (los ignoraba `.gitignore: out/`); recuperados del commit
  base de upstream y la regla limitada a `/out/`. El repositorio público no compilaba desde cero.
- Compila con clang-cl 23 + STL de MSVC 14.52: `FindStorageAddr` en
  `shader_recompiler/ir_opt/global_memory_to_storage_buffer_pass.cpp`.
- Perfil PGO propio de Ocarina: `E:\lbench\pgo\oot.profdata` (recorrido de 4 min, 16 vueltas).

## Pendiente (por prioridad)

1. Medir `E:\lbclang` (clang + PGO + ThinLTO) contra 30,6 FPS y pasar sus tests.
2. A/B de los interruptores experimentales ya presentes: `EDEN_NX2_ACQUIRE_PRECHECK=2`,
   `EDEN_NX2_ACQUIRE_EQUAL=1`, `EDEN_NX2_ACQUIRE_LEARN=1`, `EDEN_NX2_PERF_ON=front_sleep`.
3. Requiere permiso del usuario: compilar y medir `upstream/nx2` (árbol en `E:\lup`, configurado
   en `E:\lupbuild`). `nx2` trae #1056 (macros con lectura en el back), #949 (semáforos sin
   drain), #1028 (lecturas asíncronas), #858 (menos trabajo por draw), que atacan el 53 % de
   espera medido. Si rinde más, migrar el fork sobre `nx2` (rama nueva desde nx2 +
   `git merge -s ours main`, sin forzar el push).
4. Qué conservar al migrar: tema `lichii_dark`, `es.ts`, actualizador contra `Julenn06/Lichii`
   (`dist/mejorado_version.txt`), `nx2_switches_dialog`, `bug_report`, «Continuar el último juego»,
   versión del fork en el título. Descartar el troceo de `nx2_gsv.h` (choca con upstream).
5. Cierre durante la carga de un juego: el hilo de emulación no para (upstream #1058 lo trata).

## Limitaciones

- No se puede borrar el enlace `E:\lxsrc` desde la sesión (bloqueado); inofensivo.
- El modo automático no permite ejecutar los binarios precompilados de upstream
  (`E:\lbench\upstream`, build-130) ni compilar el árbol `upstream/nx2`.
