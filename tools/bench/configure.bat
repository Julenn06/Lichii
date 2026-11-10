@echo off
set VSLANG=1033
chcp 65001 >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set PATH=C:\Users\deust\AppData\Roaming\Python\Python314\Scripts;%PATH%
rem Uso: configure.bat [dir_build] [flags extra de cmake...]
set BDIR=%1
if "%BDIR%"=="" set BDIR=E:\lbuild
shift
cmake -S E:\lsrc -B %BDIR% -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
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
  -DENABLE_LTO=OFF ^
  -DLICHII_HOST_AVX2=OFF ^
  -DGLSLANGVALIDATOR=C:/Users/deust/AppData/Local/Android/Sdk/emulator/lib64/vulkan/glslangValidator.exe %1 %2 %3 %4 %5 %6