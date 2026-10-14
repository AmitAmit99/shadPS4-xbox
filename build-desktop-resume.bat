@echo off
rem Resumes build-desktop after an out-of-memory stop: the embedded font sources first, one at a
rem time (each can take several GB in clang), then everything else with 4 jobs.
call "D:\Visual Studio\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin;D:\CMake\bin;%PATH%
set TEMP=D:\shadps4-xbox-deps\tmp
set TMP=D:\shadps4-xbox-deps\tmp
cd /d D:\shadps4-xbox
ninja -j 1 -C build-desktop src/imgui/renderer/CMakeFiles/ImGui_Resources.dir/generated_fonts/imgui_fonts/notosanscjk_regular.ttc.g.cpp.obj CMakeFiles/shadps4.dir/src/imgui/renderer/font_data.cpp.obj || exit /b 3
ninja -j 4 -C build-desktop || exit /b 4
echo BUILD OK
