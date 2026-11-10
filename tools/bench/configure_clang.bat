@echo off
rem Uso: configure_clang.bat <dir_build> [flags extra de cmake...]
rem Variables opcionales del llamante: LX_CFLAGS (flags de compilacion), LX_LDFLAGS (enlazado).
set VSLANG=1033
chcp 65001 >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set LLVM=E:\lxllvm\clang+llvm-23.1.5-x86_64-pc-windows-msvc
set PATH=%LLVM%\bin;C:\Users\deust\AppData\Roaming\Python\Python314\Scripts;%PATH%
set CFLAGS=%LX_CFLAGS%
set CXXFLAGS=%LX_CFLAGS%
set LDFLAGS=/LIBPATH:%LLVM%\lib\clang\23\lib\windows %LX_LDFLAGS%
set BDIR=%1
cmake -S E:\lsrc -B %BDIR% -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_C_COMPILER=clang-cl ^
  -DCMAKE_CXX_COMPILER=clang-cl ^
  -DCMAKE_LINKER_TYPE=LLD ^
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
  -DGLSLANGVALIDATOR=C:/Users/deust/AppData/Local/Android/Sdk/emulator/lib64/vulkan/glslangValidator.exe %2 %3 %4 %5 %6 %7
