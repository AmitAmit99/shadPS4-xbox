# Shadlix (shadPS4) on the Xbox Series X

Goal: Shadlix (`diegolix29/shadPS4`, branch `Shadlix`) as a UWP app in Xbox Dev Mode,
running PS4 games on a Series X. First target game: EA UFC 3 (`CUSA06534`).

| | |
|---|---|
| Base | tag `Shadlix-shadPS4-2026-10-12-11f2e97a74a787f3dc6eb1dc187da96cf4a0c989` |
| Work branch | `xbox` in `D:\shadps4-xbox` |
| Reference (MIT) | KytyPS5 Xbox fork, `D:\xbox ps4 emu\PS5 emu\KytyPS5-xbox-1.1.26286.857 (1)\KytyPS5-xbox-1.1.26286.857` |
| Build deps | `D:\shadps4-xbox-deps` (Python venv with Meson, Mesa) |

The Kyty fork is PS5-only, so it cannot run PS4 games itself. It is the blueprint: its
`XBOX_CHANGES.md`, `docs/d3d12-backend.md` and `src/uwp/README.md` say what the Xbox
needs and how it was solved. Code taken from it keeps its MIT notice.

Other shadPS4 folders on `D:\` (the UFC 3 builds, the `claude` build) are never edited
from this project.

## Why this is work

What a UWP app in Xbox Dev Mode gets, from the Kyty fork's notes:

- **DirectX 12 only.** No Vulkan driver. shadPS4 has only a Vulkan renderer.
- **Series X GPU through D3D12:** feature level 11_0, shader model 6.4, wave64, no
  barycentrics, no mesh shaders, no native 16-bit shader ops, no heap indexing (6.6).
- **Memory and file calls** must be the `*FromApp` variants (`VirtualAlloc2FromApp`,
  `MapViewOfFile3FromApp`, `CreateFileMappingFromApp`, `VirtualProtectFromApp`,
  `CreateFile2FromAppW`, ...).
- **No page writable and executable at once.** shadPS4 maps its whole backing
  section RWX today (`core/address_space.cpp`).
- **One ~7 GB committed section is a problem.** shadPS4 commits
  `ORBIS_KERNEL_TOTAL_MEM_DEV_PRO + FLEXIBLE` up front. Kyty commits 64 KiB windows on
  first touch and pages cold windows to a file (`src/xbox/lazyRegions.h`,
  `kernel/memoryAddressSpace.inc`).
- **Every import must resolve against `WindowsApp.lib`.** A desktop-only import stops
  the app from starting on the Xbox.
- **Games on a USB drive** formatted NTFS, granted to ALL APPLICATION PACKAGES
  (`icacls "<folder>" /grant "*S-1-15-2-1:(OI)(CI)RX"`), chosen as Media in Dev Home.
  The app's type set to **Game** in Dev Home for the larger memory budget.

What is easier than it sounds: the Series X CPU is x86-64 (Zen 2), so PS4 code runs
natively, as on a PC.

## Ruled out: Mesa's Dozen (Vulkan on D3D12) under the Vulkan renderer

Tried 2026-10-14 so that nobody spends time on it again. Dozen built from the same Mesa
(`e24dc5bd` + Kyty's patch, target `vulkan_dzn.dll`), shown to `vulkaninfoSDK` 1.4.357
through `VK_DRIVER_FILES`:

- On this PC's GPUs (Radeon Vega M, Intel HD 630) vulkaninfo **crashes** (0xC0000005)
  while enumerating, right after "dzn is not a conformant Vulkan implementation".
- On WARP (`DZN_DEBUG=warp`, the most capable D3D12 device there is): **Vulkan 1.2**,
  **no `VK_KHR_push_descriptor`, no `VK_EXT_robustness2`**.

shadPS4 asserts all three at startup (`vk_platform.h` `TargetVulkanApiVersion = 1.3`,
`vk_instance.cpp` `ASSERT_MSG(add_extension(...))` for push descriptors and robustness2
with `nullDescriptor`). The Series X's D3D12 offers less than WARP. The set this covers:
that Mesa commit, those devices. A much newer Dozen would need a new look.

## What the D3D12 renderer has to cover

shadPS4's caches are written in Vulkan types: 38 files outside `renderer_vulkan` include
it (`texture_cache/*`, `buffer_cache/*`, `page_manager.cpp`, `amdgpu/liverpool.cpp`,
`core/memory.cpp`, `videoout/driver.cpp`, imgui, devtools). Kyty's first renderer step
was the same split ("Renderer: shared logic": Vulkan calls moved to Vulkan-only files
next to the shared logic). Size: `renderer_vulkan` is 43 files / ~11,000 lines; Kyty's
`host_gpu/d3d12` is 43 files / ~9,700 lines.

## Phases

Each phase ends with a checkpoint the game shows, first on the PC (the UWP app runs on
Windows in Developer Mode), then on the Series X.

### 1. Shell: shadPS4 inside a UWP app, no picture

- CMake option `SHADPS4_BUILD_UWP`: no Qt, no SDL window, a CoreWindow app.
- `core/address_space.cpp`: `*FromApp` calls, no RWX views (switch on write/execute
  faults, as Kyty's `HandleWriteExecuteFault`), lazy commit.
  What `address_space.cpp` (Windows `Impl`) does today, line by line at 11f2e97:
  - L115-125: Windows build via `GetModuleHandleW(L"ntdll.dll")` + `RtlGetVersion`, only
    to cap the user area on builds <= 22621. On UWP: skip (the Xbox OS is newer).
  - L146-181: every free range from 0x400000 to `USER_MAX` reserved as placeholders with
    `VirtualAlloc2` -> `VirtualAlloc2FromApp`.
  - L196-211: `CreateFileMapping2(PAGE_EXECUTE_READWRITE, SEC_COMMIT, BackingSize)` and the
    whole backing mapped RWX at once. `BackingSize` = `ORBIS_KERNEL_TOTAL_MEM_DEV_PRO +
    ORBIS_KERNEL_FLEXIBLE_MEMORY_SIZE` (+ extra dmem). Kyty keeps the section's maximum
    protection RWX (`CreateFileMappingFromApp`) but never maps a view RWX. If the Series X
    refuses the up-front commit, commit on first touch as Kyty does.
  - L233-291 `MapRegion`: `MapViewOfFile3` / `VirtualAlloc2` with the guest's protection,
    which can be RWX -> `*FromApp`, RWX split into W^X ranges switched by faults.
  - L559 `Protect`: `VirtualProtectEx` -> `VirtualProtectFromApp`, same W^X rule.
- `common/ntapi.cpp`: `NtClose`, `NtSetInformationFile`, `NtCreateThread`,
  `NtTerminateThread`, `NtQueueApcThreadEx`, loaded with `GetModuleHandleA` +
  `GetProcAddress` (so they do not stop the app from starting). Replace with documented
  calls where UWP has one (`SetFileInformationByHandle` for the delete disposition,
  `QueueUserAPC2`/`QueueUserAPC` for the thread interrupts).
- `tools/uwp-import-check.ps1 -Binary <exe>` lists every import the Xbox does not have
  (from Kyty's `Test-Imports`); run it on every build.
- `core/signals.cpp`, `core/tls.cpp`, code patching: writes to code go through the
  write/execute switch.
- Window: `Emulator::Run` creates `Frontend::WindowSDL` (`emulator.cpp` L425) and runs its
  event loop (L532-534); 19 files use it (renderer surface, input, imgui, gnmdriver,
  devtools). A `WindowUWP` with the same public interface (`sdl_window.h`: `SetIcon`,
  `WaitEvent`, `InitTimers`, `IsOpen`, keyboard requests, gamepad events,
  `WindowSystemInfo`) over the CoreWindow, chosen by `SHADPS4_BUILD_UWP`. The app's
  `IFrameworkView::Run` owns the CoreWindow loop; the emulator runs on its own thread.
- Package manifest as Kyty's: capabilities `internetClient`, `codeGeneration`,
  `removableStorage` (+ `microphone` later).
- Files through `*FromApp`; game folders on the USB drive.
- Input: `Windows.Gaming.Input` to shadPS4's controller state.
- Audio: SDL audio to XAudio2.

**Checkpoint:** UFC 3 boots, the log shows the game reaching its first GPU
submission, the menu music plays.

### 2. DirectX 12 renderer

- `video_core/renderer_d3d12` beside `renderer_vulkan`; one renderer per build.
- Shaders: shadPS4's recompiler already emits SPIR-V. SPIR-V to DXIL through Mesa's
  `spirv_to_dxil` with Kyty's patch, signed by the SDK's `dxil.dll`.
- Series X limits applied as Kyty does: guest memory reads through a buffer table
  below SM 6.6, 16-bit ops lowered to 32-bit, no barycentrics.

**Checkpoint:** UFC 3's menus draw.

### 3. UFC 3 playable

- Fights draw; the UFC 3 skin and tattoo fixes (Vulkan-side in the claude build)
  redone for D3D12 where still needed.
- Memory pager for the Series X budget; speed.

**Checkpoint:** a fight plays on the Series X.

## Build (this PC, everything on D:)

| tool | where |
|---|---|
| Visual Studio 2022 Community | `D:\Visual Studio` |
| clang-cl 22 | `D:\LLVM\clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin` |
| CMake, Ninja | `D:\CMake\bin`, WinGet |
| Windows SDK | `D:\Windows Kits\10`, 10.0.22621 (Kyty used 26100; install on D: only if something needs it) |
| Meson + Mako | `D:\shadps4-xbox-deps\venv` |
| Mesa | `D:\shadps4-xbox-deps\mesa`, commit `e24dc5bd1e7fe6101bdc866fb16a15a8fcae1aae` + Kyty's `spirv_to_dxil.patch` |

C: has about 6 GB free: point `TEMP`/`TMP` at `D:\` for every build.

## Status

| date | what | checked |
|---|---|---|
| 2026-10-14 | Repo cloned at the Shadlix 12 Oct tag with all submodules, branch `xbox`. Plan written | - |
| 2026-10-14 | `spirv_to_dxil.dll` built (Mesa `e24dc5bd` + Kyty's patch, `/MT`): `D:\shadps4-xbox-deps\spirv_to_dxil\{include,lib,bin}`, script `build-spirv-to-dxil.ps1` | built, not used yet |
| 2026-10-14 | Dozen tried as a shortcut and ruled out (section above) | vulkaninfo on PC |
| 2026-10-14 | Baseline: this tree, unchanged, built as the desktop SDL app (`build-desktop.bat`, `build-desktop\shadps4.exe`, 58 MB). The first run ran out of memory on the embedded font sources at 6 jobs; `build-desktop-resume.bat` builds those alone, then 4 jobs | built, not run |
| 2026-10-14 | Import check of the baseline exe: 404 imports the Xbox lacks, in 16 DLLs (below) | `tools\uwp-import-check.ps1` |

### What the baseline exe imports that the Xbox does not have

| imports | from | fix |
|---|---|---|
| 196 | `MSVCP140`, `VCRUNTIME140`, `VCRUNTIME140_1`, `MSVCP140_ATOMIC_WAIT` | the store C++ runtime (`*_app`) in the UWP build: a link setting, no code |
| ~205 | `USER32` (137), `GDI32` (24), `IMM32`, `SHELL32`, `ole32`, `SETUPAPI`, `HID`, `cfgmgr` (`api-ms-win-devices-config`), `AVRT`, `VERSION` | almost all SDL's window, input, HID, clipboard, IME and WASAPI code: gone with the UWP window, input and audio (Kyty builds SDL with those subsystems off + `desktopShims.cpp`). shadPS4's own: `SHGetKnownFolderPath` / `ShellExecute` and similar, to check one by one |
| 3 | `api-ms-win-core-memory`: `VirtualAlloc2`, `MapViewOfFile3`, `CreateFileMapping2` | `core/address_space.cpp` only -> `*FromApp` |

The NT functions in `common/ntapi.cpp` and `RtlGetVersion` do not show here: they are
looked up at run time with `GetProcAddress`, so they need checking on the console itself.

## Phase 1 as written (2026-10-14): built and packaged, NOT run yet

First package: `xbox-package\1.0.286.928\shadPS4-Xbox_1.0.286.928_x64.msix` (32 MB) +
`Dependencies\Microsoft.VCLibs.x64.14.00.appx`. `tools\uwp-import-check.ps1`: every import is
available to UWP apps. Not started on the PC or the Xbox yet; no picture is expected (phase 2).

Found on the way:
- The 9 imports left after the first link came from SDL's core (helper window), the prebuilt
  FFmpeg (`GetDesktopWindow`), OpenAL Soft (`SHGetKnownFolderPath`, `AvSetMmThread...`) and
  `common/io_file.cpp` (`CreateFileMapping2`). The libraries' ones are defined in
  `src/uwp/uwp_desktop_shims.cpp` (Kyty's `desktopShims.cpp` pattern).
- Re-running CMake WITHOUT the vcvars tools on PATH switches the resource compiler rule to
  `cmake_llvm_rc` + `rc.exe`; rc.exe cannot read clang's `# 1 "file"` markers and its error
  output deadlocks the pipe (looks like a hang). Always regenerate with `regen-uwp.bat`
  (or let `build-uwp.bat` do it).
- The first desktop build ran out of memory on the embedded font sources; they now build one
  at a time first. The builds grew `C:\pagefile.sys` to 27 GB on this PC (C: down to 0.1 GB).

Everything is behind `SHADPS4_BUILD_UWP` (CMake) / `SHADPS4_UWP` (code); the desktop build
is unchanged. Build: `build-uwp.bat` -> `build-uwp\uwp-layout`; package:
`tools\uwp-package.ps1` -> `xbox-package\<version>\` (.msix + `Dependencies\`).

| what | where |
|---|---|
| Build: no Qt/Discord/updater; SDL without video, render, audio, joystick, haptic, HIDAPI, sensor, power, misc, filesystem, locale, DirectX, XInput, WASAPI, Vulkan, OpenGL, camera, dialog, GPU, tray and without its dynamic API (as Kyty); imgui without its Win32 clipboard/IME/shell; store C runtime (`/LIBPATH:...\lib\x64\store`), `WindowsApp.lib` first, `/APPCONTAINER`; layout copied after each link | `CMakeLists.txt`, `externals/CMakeLists.txt` |
| The app: CoreWindow, emulator on its own thread, controller read every 4 ms, app log `LocalState\shadps4-uwp.txt` | `src/uwp/uwp_app.cpp`, `uwp_host.*` |
| Game: first line of `LocalState\game.txt` (eboot.bin or game folder, e.g. on the USB drive), else the first game folder in `LocalState\Games` | `uwp_app.cpp` `FindGame` |
| User folder (config, logs, saves) in `LocalState\user` | `common/path_util.*` `SetUwpDataRoot` |
| Xbox controller -> PS4 pad 0, user 1 logged in (A/B/X/Y = Cross/Circle/Square/Triangle, Menu = Options, View = touchpad) | `uwp_host.cpp` |
| No renderer: presenter not created; flips still complete; every renderer call in `core/memory.cpp`, `videoout/driver.cpp`, `video_out.cpp`, `mouse.cpp` checks for it | those files |
| Memory: `*FromApp` calls; backing 6.25 GB (`HOST_BACKED_TOTAL_MEM` = retail Pro) instead of 8.25 GB; no RWX page: such ranges start RW, the access-violation handler flips 4 KiB pages to RX on execute and back on write | `core/address_space.*`, `core/signals.cpp`, `kernel/memory.h`, `core/memory.cpp` |
| Sound: XAudio2 backend, one source voice per guest port | `audio/xaudio2_audio_out.cpp` |
| Files: the Win32 file functions the app, ucrtbase and msvcp140_app import are redirected; where the ordinary call is refused, the `*FromApp` call on a 4 MB-stack thread, retried on transient USB errors (Kyty's findings) | `src/uwp/uwp_files.cpp` |
| hwinfo (WMI) skipped | `emulator.cpp` |
| Manifest (internetClient, codeGeneration, removableStorage), logos | `src/uwp/AppxManifest.xml`, `src/uwp/Assets` (`tools/uwp-make-assets.py`) |
