// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Desktop-only Windows functions that libraries in the UWP app reference, defined here so the
// executable does not import them: the Xbox has none of the desktop DLLs, and an import it
// cannot resolve stops the app from starting there (Windows still runs it). Defining a function's
// import pointer (__imp_<name>) satisfies a library's dllimport call before the desktop libraries
// are searched; a plain reference is satisfied by defining the function. As KytyPS5's Xbox build
// (src/uwp/src/desktopShims.cpp, MIT). tools/uwp-import-check.ps1 lists what is still imported.
//
// - SDL 3: its Windows core's helper window (for joysticks and raw input, which are off).
// - FFmpeg (prebuilt avutil): the desktop window for a DXVA2 device, which is never created.
// - OpenAL Soft (3D audio): the user's folders for its config file, and the multimedia thread
//   priority of its mixer thread. Without them it reads no config file and its thread keeps a
//   normal priority.

#include <windows.h>
#include <avrt.h>

namespace {

HWND WINAPI NoDesktopWindow() {
    return nullptr;
}

HWND WINAPI NoCreateWindow(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HWND, HMENU,
                           HINSTANCE, LPVOID) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

ATOM WINAPI NoRegisterClass(const WNDCLASSW*) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return 0;
}

BOOL WINAPI NoUnregisterClass(LPCWSTR, HINSTANCE) {
    return FALSE;
}

LRESULT WINAPI NoWindowProc(HWND, UINT, WPARAM, LPARAM) {
    return 0;
}

HANDLE WINAPI NoMmThreadCharacteristics(LPCWSTR, LPDWORD) {
    SetLastError(ERROR_NOT_SUPPORTED);
    return nullptr;
}

BOOL WINAPI NoRevertMmThreadCharacteristics(HANDLE) {
    return FALSE;
}

} // namespace

extern "C" {
// OpenAL Soft calls this without dllimport, so the function itself is defined.
HRESULT WINAPI SHGetKnownFolderPath(const GUID&, DWORD, HANDLE, PWSTR* path) {
    if (path != nullptr) {
        *path = nullptr;
    }
    return E_NOTIMPL;
}

decltype(&GetDesktopWindow) __imp_GetDesktopWindow = NoDesktopWindow;
decltype(&CreateWindowExW) __imp_CreateWindowExW = NoCreateWindow;
decltype(&RegisterClassW) __imp_RegisterClassW = NoRegisterClass;
decltype(&UnregisterClassW) __imp_UnregisterClassW = NoUnregisterClass;
decltype(&DefWindowProcW) __imp_DefWindowProcW = NoWindowProc;
decltype(&AvSetMmThreadCharacteristicsW) __imp_AvSetMmThreadCharacteristicsW =
    NoMmThreadCharacteristics;
decltype(&AvRevertMmThreadCharacteristics) __imp_AvRevertMmThreadCharacteristics =
    NoRevertMmThreadCharacteristics;
}
