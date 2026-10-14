@echo off
rem Re-runs CMake for build-uwp in the same environment as build-uwp.bat (vcvars first, then LLVM
rem and CMake in front). A regeneration without the vcvars tools on PATH switches the resource
rem compiler to cmake_llvm_rc + rc.exe, which cannot read clang's line markers and hangs.
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin;D:\CMake\bin;%PATH%
set TEMP=D:\shadps4-xbox-deps\tmp
set TMP=D:\shadps4-xbox-deps\tmp
cd /d D:\shadps4-xbox
cmake -S . -B build-uwp || exit /b 2
ninja -n -C build-uwp
