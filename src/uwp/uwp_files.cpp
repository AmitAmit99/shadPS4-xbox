// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// File access for the UWP app on the Xbox (XBOX_PORT.md). The Xbox lets a UWP app read its USB
// drive only through the *FromApp functions, and shadPS4 reaches files from everywhere through
// the C and C++ runtimes (fopen, std::filesystem) and Win32. So the Win32 file functions those
// modules import are redirected here: the ordinary call first, and where it is refused (access
// denied: the USB drive), the *FromApp call.
//
// From KytyPS5's Xbox build (sysWindowsFileIO.cpp, MIT), which found that
// - the *FromApp calls go through the file broker, over COM and RPC, which needs more stack than
//   a game's threads have (one overflowed in GetFileAttributesExFromAppW): they run on a thread
//   of their own with a large stack, and the caller waits;
// - the Xbox's USB drive refuses an app now and then for a moment (access denied, not ready, a
//   sharing violation) where asked again a moment later it works: asked again up to 10 times.

#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>

#include <windows.h>
#include <fileapifromapp.h>

#include <fmt/format.h>

#include "common/types.h"
#include "uwp/uwp_host.h"

namespace Uwp {

namespace {

class BrokerThread {
public:
    template <typename Fn>
    auto Run(Fn&& fn) -> decltype(fn()) {
        using Result = decltype(fn());
        if (GetCurrentThreadId() == thread_id) {
            return fn();
        }
        std::optional<Result> result;
        DWORD error = 0;
        Call([&] {
            result = fn();
            error = GetLastError();
        });
        SetLastError(error);
        return *result;
    }

private:
    void Call(std::function<void()> job) {
        std::unique_lock lock{mutex};
        if (thread == nullptr) {
            thread = CreateThread(
                nullptr, StackSize,
                [](void* self) -> DWORD {
                    static_cast<BrokerThread*>(self)->Loop();
                    return 0;
                },
                this, STACK_SIZE_PARAM_IS_A_RESERVATION, &thread_id);
        }
        const auto ticket = ++submitted;
        jobs.push_back(std::move(job));
        changed.notify_all();
        changed.wait(lock, [&] { return finished >= ticket; });
    }

    void Loop() {
        std::unique_lock lock{mutex};
        for (;;) {
            changed.wait(lock, [&] { return !jobs.empty(); });
            auto job = std::move(jobs.front());
            jobs.pop_front();
            lock.unlock();
            job();
            lock.lock();
            finished++;
            changed.notify_all();
        }
    }

    static constexpr SIZE_T StackSize = SIZE_T{4} << 20;

    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::function<void()>> jobs;
    u64 submitted = 0;
    u64 finished = 0;
    HANDLE thread = nullptr;
    DWORD thread_id = 0;
};

BrokerThread& Broker() {
    static BrokerThread broker;
    return broker;
}

bool IsTransient(DWORD error) {
    return error == ERROR_ACCESS_DENIED || error == ERROR_NOT_READY ||
           error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION;
}

constexpr DWORD RetryIntervalMs = 50;
constexpr int RetryLimit = 10;

// The ordinary call; where it is refused, the *FromApp call on the broker thread, asked again on
// transient errors.
template <typename Direct, typename FromApp, typename Failed>
auto Redirected(Direct&& direct, FromApp&& from_app, Failed&& failed) -> decltype(direct()) {
    auto result = direct();
    if (!failed(result) || GetLastError() != ERROR_ACCESS_DENIED) {
        return result;
    }
    for (int attempt = 0; attempt <= RetryLimit; attempt++) {
        if (attempt > 0) {
            Sleep(RetryIntervalMs);
        }
        result = Broker().Run(from_app);
        if (!failed(result) || !IsTransient(GetLastError())) {
            break;
        }
    }
    return result;
}

// The functions the modules imported before redirection (KERNELBASE's).
struct Originals {
    decltype(&CreateFileW) create_file;
    decltype(&CreateFile2) create_file2;
    decltype(&GetFileAttributesExW) get_attributes_ex;
    decltype(&GetFileAttributesW) get_attributes;
    decltype(&FindFirstFileExW) find_first;
    decltype(&CreateDirectoryW) create_directory;
    decltype(&RemoveDirectoryW) remove_directory;
    decltype(&DeleteFileW) delete_file;
    decltype(&MoveFileExW) move_file;
    decltype(&SetFileAttributesW) set_attributes;
} original{};

constexpr auto InvalidHandle = [](HANDLE h) { return h == INVALID_HANDLE_VALUE; };
constexpr auto False = [](BOOL ok) { return !ok; };

HANDLE WINAPI HookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                              DWORD disposition, DWORD flags, HANDLE templ) {
    return Redirected(
        [&] { return original.create_file(name, access, share, sa, disposition, flags, templ); },
        [&] { return CreateFileFromAppW(name, access, share, sa, disposition, flags, templ); },
        InvalidHandle);
}

HANDLE WINAPI HookCreateFile2(LPCWSTR name, DWORD access, DWORD share, DWORD disposition,
                              LPCREATEFILE2_EXTENDED_PARAMETERS params) {
    return Redirected(
        [&] { return original.create_file2(name, access, share, disposition, params); },
        [&] { return CreateFile2FromAppW(name, access, share, disposition, params); },
        InvalidHandle);
}

BOOL WINAPI HookGetFileAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID info) {
    return Redirected([&] { return original.get_attributes_ex(name, level, info); },
                      [&] { return GetFileAttributesExFromAppW(name, level, info); }, False);
}

DWORD WINAPI HookGetFileAttributesW(LPCWSTR name) {
    return Redirected(
        [&] { return original.get_attributes(name); },
        [&] {
            WIN32_FILE_ATTRIBUTE_DATA data{};
            return GetFileAttributesExFromAppW(name, GetFileExInfoStandard, &data)
                       ? data.dwFileAttributes
                       : INVALID_FILE_ATTRIBUTES;
        },
        [](DWORD attributes) { return attributes == INVALID_FILE_ATTRIBUTES; });
}

HANDLE WINAPI HookFindFirstFileExW(LPCWSTR name, FINDEX_INFO_LEVELS level, LPVOID data,
                                   FINDEX_SEARCH_OPS op, LPVOID filter, DWORD flags) {
    return Redirected(
        [&] { return original.find_first(name, level, data, op, filter, flags); },
        [&] { return FindFirstFileExFromAppW(name, level, data, op, filter, flags); },
        InvalidHandle);
}

BOOL WINAPI HookCreateDirectoryW(LPCWSTR name, LPSECURITY_ATTRIBUTES sa) {
    return Redirected([&] { return original.create_directory(name, sa); },
                      [&] { return CreateDirectoryFromAppW(name, sa); }, False);
}

BOOL WINAPI HookRemoveDirectoryW(LPCWSTR name) {
    return Redirected([&] { return original.remove_directory(name); },
                      [&] { return RemoveDirectoryFromAppW(name); }, False);
}

BOOL WINAPI HookDeleteFileW(LPCWSTR name) {
    return Redirected([&] { return original.delete_file(name); },
                      [&] { return DeleteFileFromAppW(name); }, False);
}

BOOL WINAPI HookMoveFileExW(LPCWSTR from, LPCWSTR to, DWORD flags) {
    return Redirected(
        [&] { return original.move_file(from, to, flags); },
        [&] {
            // MoveFileFromAppW has no flags: replacing means deleting the target first.
            if ((flags & MOVEFILE_REPLACE_EXISTING) != 0) {
                DeleteFileFromAppW(to);
            }
            return MoveFileFromAppW(from, to);
        },
        False);
}

BOOL WINAPI HookSetFileAttributesW(LPCWSTR name, DWORD attributes) {
    return Redirected([&] { return original.set_attributes(name, attributes); },
                      [&] { return SetFileAttributesFromAppW(name, attributes); }, False);
}

struct Hook {
    const char* name;
    void* replacement;
};

const std::array<Hook, 10> hooks{{
    {"CreateFileW", reinterpret_cast<void*>(&HookCreateFileW)},
    {"CreateFile2", reinterpret_cast<void*>(&HookCreateFile2)},
    {"GetFileAttributesExW", reinterpret_cast<void*>(&HookGetFileAttributesExW)},
    {"GetFileAttributesW", reinterpret_cast<void*>(&HookGetFileAttributesW)},
    {"FindFirstFileExW", reinterpret_cast<void*>(&HookFindFirstFileExW)},
    {"CreateDirectoryW", reinterpret_cast<void*>(&HookCreateDirectoryW)},
    {"RemoveDirectoryW", reinterpret_cast<void*>(&HookRemoveDirectoryW)},
    {"DeleteFileW", reinterpret_cast<void*>(&HookDeleteFileW)},
    {"MoveFileExW", reinterpret_cast<void*>(&HookMoveFileExW)},
    {"SetFileAttributesW", reinterpret_cast<void*>(&HookSetFileAttributesW)},
}};

// Points the module's imports of those functions at the hooks; returns how many.
int PatchImports(HMODULE module) {
    auto* base = reinterpret_cast<u8*>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) {
        return 0;
    }
    int patched = 0;
    for (auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         desc->Name != 0; desc++) {
        if (desc->OriginalFirstThunk == 0) {
            continue; // no names to match
        }
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL64(names->u1.Ordinal)) {
                continue;
            }
            const auto* by_name =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            for (const auto& hook : hooks) {
                if (std::strcmp(by_name->Name, hook.name) != 0) {
                    continue;
                }
                ULONG old_prot{};
                if (VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function),
                                          PAGE_READWRITE, &old_prot)) {
                    slots->u1.Function = reinterpret_cast<ULONGLONG>(hook.replacement);
                    VirtualProtectFromApp(&slots->u1.Function, sizeof(slots->u1.Function),
                                          old_prot, &old_prot);
                    patched++;
                }
                break;
            }
        }
    }
    return patched;
}

} // namespace

void RedirectFileApis() {
    const HMODULE kernelbase = GetModuleHandleW(L"kernelbase.dll");
    if (kernelbase == nullptr) {
        AppLog("File redirection: no kernelbase.dll");
        return;
    }
    const auto get = [&](auto& slot, const char* name) {
        slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(
            GetProcAddress(kernelbase, name));
        return slot != nullptr;
    };
    if (!get(original.create_file, "CreateFileW") || !get(original.create_file2, "CreateFile2") ||
        !get(original.get_attributes_ex, "GetFileAttributesExW") ||
        !get(original.get_attributes, "GetFileAttributesW") ||
        !get(original.find_first, "FindFirstFileExW") ||
        !get(original.create_directory, "CreateDirectoryW") ||
        !get(original.remove_directory, "RemoveDirectoryW") ||
        !get(original.delete_file, "DeleteFileW") || !get(original.move_file, "MoveFileExW") ||
        !get(original.set_attributes, "SetFileAttributesW")) {
        AppLog("File redirection: a KERNELBASE function is missing, not redirected");
        return;
    }

    // The app itself (SDL and the rest are linked in) and the runtimes that open files for it.
    const std::array<const wchar_t*, 4> modules{nullptr, L"ucrtbase.dll", L"msvcp140_app.dll",
                                                L"vcruntime140_app.dll"};
    for (const wchar_t* name : modules) {
        const HMODULE module = GetModuleHandleW(name);
        if (module == nullptr) {
            continue;
        }
        const int patched = PatchImports(module);
        std::string module_name = "the app";
        if (name != nullptr) {
            module_name.clear();
            for (const wchar_t* c = name; *c != L'\0'; c++) {
                module_name.push_back(static_cast<char>(*c)); // the names are ASCII
            }
        }
        AppLog(fmt::format("File redirection: {} imports in {}", patched, module_name));
    }
}

} // namespace Uwp
