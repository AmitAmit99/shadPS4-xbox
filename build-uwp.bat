@echo off
rem The UWP app for Xbox Dev Mode (XBOX_PORT.md): build-uwp\uwp-layout is the package layout,
rem tools\uwp-package.ps1 packs it into an .msix.
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin;D:\CMake\bin;%PATH%
rem C: is nearly full: temporary files on D:.
if not exist D:\shadps4-xbox-deps\tmp mkdir D:\shadps4-xbox-deps\tmp
set TEMP=D:\shadps4-xbox-deps\tmp
set TMP=D:\shadps4-xbox-deps\tmp
cd /d D:\shadps4-xbox
if not exist build-uwp\build.ninja (
  rem clang 22 turns FLAC's (sdl3_mixer) uint32_t*/unsigned long* mismatch into an error
  cmake -S . -B build-uwp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DSHADPS4_BUILD_UWP=ON "-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS -Wno-error=incompatible-pointer-types" || exit /b 2
)
rem The embedded font sources take several GB each in clang: one at a time first.
ninja -j 1 -C build-uwp src/imgui/renderer/CMakeFiles/ImGui_Resources.dir/generated_fonts/imgui_fonts/notosanscjk_regular.ttc.g.cpp.obj CMakeFiles/shadps4.dir/src/imgui/renderer/font_data.cpp.obj || exit /b 3
rem 4 jobs: this PC has little free RAM.
ninja -j 4 -C build-uwp || exit /b 4
echo BUILD OK
