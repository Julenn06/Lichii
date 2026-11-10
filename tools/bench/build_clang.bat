@echo off
rem Uso: build_clang.bat <dir_build> <targets...>
set VSLANG=1033
chcp 65001 >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set LLVM=E:\lxllvm\clang+llvm-23.1.5-x86_64-pc-windows-msvc
set PATH=%LLVM%\bin;C:\Users\deust\AppData\Roaming\Python\Python314\Scripts;%PATH%
set BDIR=%1
cmake --build %BDIR% --target %2 %3 %4 %5 %6 -- -k 0
