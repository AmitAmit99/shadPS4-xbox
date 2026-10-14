@echo off
rem Baseline: this tree built as the ordinary Windows app (SDL frontend, no Qt), same flags as the
rem claude build's build-diag.bat. Proves the toolchain on this commit before the UWP work.
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin;D:\CMake\bin;%PATH%
rem C: is nearly full: temporary files on D:.
if not exist D:\shadps4-xbox-deps\tmp mkdir D:\shadps4-xbox-deps\tmp
set TEMP=D:\shadps4-xbox-deps\tmp
set TMP=D:\shadps4-xbox-deps\tmp
cd /d D:\shadps4-xbox
rem clang 22 turns FLAC's (sdl3_mixer) uint32_t*/unsigned long* mismatch into an error
cmake -S . -B build-desktop -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DENABLE_QT_GUI=OFF -DENABLE_DISCORD_RPC=OFF -DENABLE_UPDATER=OFF "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS -Wno-error=incompatible-pointer-types" || exit /b 2
rem 6 jobs: this PC has little free RAM.
ninja -j 6 -C build-desktop || exit /b 3
echo BUILD OK
