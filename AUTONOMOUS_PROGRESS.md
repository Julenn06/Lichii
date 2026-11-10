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
  Enlace sin espacios: `E:\lsrc`. Árbol de compilación MSVC: `E:\lbuild` (Ninja, Release).
- Scripts en `E:\lxtools`: `configure.bat [dir] [flags]`, `build.bat <dir> <targets>`.
  Siempre con `VSLANG=1033` (si no, Ninja no detecta cambios en cabeceras).
- `E:\lxsrc` y `E:\lxbuild` son de la carpeta antigua (enlace roto): no usar.

## Banco de pruebas reproducible

- `E:\lbench\app`: copia aislada del emulador con su propio `user/` (configuración sin sonido ni
  diálogos, NAND y caché copiadas; `keys` es un enlace a las del usuario, nunca se leen).
- `python E:\lxtools\bench.py --name <n> [--exe <dir>] [--env K=V] [--seconds 30] [--repeat 2] [--sample]`
  arranca el juego, carga la partida del banco (balcón de la casa de Link, Bosque Kokiri) con el
  inyector de entrada del servidor de depuración, mide FPS por el contador de frames presentados
  y cierra. Resultados en `E:\lbench\runs\<n>\result.json`.
- Sin teclado simulado ni capturas de pantalla: las imágenes salen del propio emulador
  (`E:\lxtools\shot.sh`).
- Ruido entre sesiones: ±5 %. Repetir cada variante antes de sacar conclusiones.

## Diagnóstico de rendimiento (medido 2026-11-10)

- Punto fijo (Kokiri, balcón): **~30,6 FPS** con v3.7.9 (MSVC, interruptores por defecto).
- Perfil por muestreo (10 s): el hilo `GPU` (front) está al 100 %: ~49 % trabajo y ~51 % esperando
  al hilo `GPUBack` (`GpuSplit::Drain` → `Backoff`: semáforos y lecturas DMA de macros).
  `GPUBack` ~75 % ocupado (draws, buffer cache). Los 6 `CPUCore_*` solo ~36 % en código JIT.
  **El límite es el código C++ de emulación de GPU, no la CPU emulada.**
- Referencia upstream en el mismo punto: 48–49,5 FPS (5900X, clang, `PREFLUSH`), comentario en
  `src/video_core/nx2_perf.h`.

## Hecho en esta sesión

- Repo: faltaban `src/audio_core/out/*` (los ignoraba `.gitignore: out/`); recuperados del commit
  base de upstream y la regla limitada a `/out/`. El repositorio público no compilaba desde cero.

## Pendiente (por prioridad)

1. Medir binarios upstream build-130 (MSVC y PGO) en el banco para separar compilador / código.
2. Compilar con clang-cl 23 + LTO + PGO propio de Ocarina; medir.
3. Portar de `nx2` los cambios de solape front/back (#858, #949, #1056, #1028, #910, #1016).
4. Cierre durante la carga de un juego: el hilo de emulación no para y la ventana se cuelga.

## Limitaciones

- No se puede borrar el enlace `E:\lxsrc` desde la sesión (bloqueado); inofensivo.
