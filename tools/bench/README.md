# Banco de pruebas y scripts de compilación (Windows)

Herramientas locales con las que se mide el rendimiento del fork. Las rutas están fijadas al PC
de desarrollo (`E:\lsrc`, `E:\lbuild`, `E:\lbench`, `E:\lxllvm`); cámbialas al principio de cada
archivo si las usas en otro equipo.

| Archivo | Para qué |
|---|---|
| `configure.bat`, `build.bat` | Configurar y compilar con MSVC y Ninja. |
| `configure_clang.bat`, `build_clang.bat` | Lo mismo con clang-cl; `LX_CFLAGS` añade flags (PGO, LTO). |
| `bench_config.py` | Adapta una copia de `qt-config.ini` al banco: rutas propias, sin sonido ni diálogos. |
| `bench.py` | Arranca el juego, carga la partida del banco, mide FPS y cierra. |
| `sample_summary.py` | Resume un informe `samples-*.txt` del muestreador por hilo. |

## Cómo se mide

1. El banco es una copia aislada del emulador con su propio `user/` (nunca la instalación real).
2. `bench.py` lanza el emulador con `EDEN_NX2_DEBUG_SERVER`, espera al título, pulsa A con el
   inyector de entrada del servidor de depuración hasta detectar la pantalla de carga y espera a
   que la escena se estabilice. No se simulan teclas ni se captura la pantalla.
3. Los FPS salen del contador de frames presentados (`status.presents`) en ventanas de 0,5 s.
4. Una variante se compara con otra solo tras repetirla: entre sesiones hay un ±5 % de ruido.

```
python bench.py --name base --exe E:/lbuild/bin --seconds 30 --repeat 2
python bench.py --name variante --exe E:/lbclang/bin --env EDEN_NX2_X=1 --sample
```

## PGO con clang

1. Árbol instrumentado: `LX_CFLAGS=-fprofile-generate` y `configure_clang.bat E:\lbgen`.
2. Entrenamiento: `bench.py --name pgo_train --exe E:/lbgen/bin --train 240` con
   `LLVM_PROFILE_FILE=E:/lbench/pgo/oot-%p.profraw`.
3. `llvm-profdata merge -o oot.profdata oot-*.profraw`.
4. Árbol optimizado: `LX_CFLAGS=-fprofile-use=<oot.profdata> -flto=thin` y `configure_clang.bat E:\lbclang`.
