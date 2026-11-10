@echo off
rem Build de release: clang-cl + PGO (dist/pgo/lichii-oot.profdata) + ThinLTO.
rem Uso: configure_release.bat <dir_build> [ON|OFF AVX2]
set VSLANG=1033
chcp 65001 >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set LLVM=E:\lxllvm\clang+llvm-23.1.5-x86_64-pc-windows-msvc
set PATH=%LLVM%\bin;C:\Users\deust\AppData\Roaming\Python\Python314\Scripts;%PATH%
set LDFLAGS=/LIBPATH:%LLVM%\lib\clang\23\lib\windows
set BDIR=%1
set AVX2=%2
if "%AVX2%"=="" set AVX2=OFF
cmake -S E:\lsrc -B %BDIR% -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_LINKER_TYPE=LLD ^
  -DLICHII_PGO_PROFILE=E:/lsrc/dist/pgo/lichii-oot.profdata ^
  -DLICHII_THIN_LTO=ON ^
  -DLICHII_HOST_AVX2=%AVX2% ^
  -DCMAKE_INTERMEDIATE_DIR_STRATEGY=SHORT ^
  -DBUILD_TESTING=ON ^
  -DYUZU_TESTS=ON ^
  -DYUZU_CMD=OFF ^
  -DYUZU_ROOM=OFF ^
  -DYUZU_ROOM_STANDALONE=OFF ^
  -DYUZU_USE_BUNDLED_QT=OFF ^
  -DCMAKE_PREFIX_PATH=E:/lxqt/6.8.3/msvc2022_64 ^
  -DYUZU_USE_BUNDLED_SDL3=ON ^
  -DENABLE_UPDATE_CHECKER=ON ^
  -DUSE_DISCORD_PRESENCE=OFF ^
  -DENABLE_WERROR=OFF ^
  -DENABLE_QT_TRANSLATION=ON ^
  -DGLSLANGVALIDATOR=C:/Users/deust/AppData/Local/Android/Sdk/emulator/lib64/vulkan/glslangValidator.exe
