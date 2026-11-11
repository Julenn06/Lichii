@echo off
rem Reconfigura un arbol clang existente con o sin ICF. Uso: reconf_icf.bat <dir_build> <ON|OFF>
set VSLANG=1033
chcp 65001 >nul
set PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%
call "C:\Program Files\Microsoft Visual Studio\18\Insiders\VC\Auxiliary\Build\vcvars64.bat" >nul
set LLVM=E:\lxllvm\clang+llvm-23.1.5-x86_64-pc-windows-msvc
set PATH=%LLVM%\bin;C:\Users\deust\AppData\Roaming\Python\Python314\Scripts;%PATH%
cmake -DLICHII_LINK_ICF=%2 %1
