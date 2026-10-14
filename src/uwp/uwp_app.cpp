// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// The UWP app for Xbox Dev Mode (XBOX_PORT.md): a CoreWindow app that starts the emulator on a
// thread of its own and reads the Xbox controller. No renderer yet (phase 2), so the window
// stays empty; the game runs, plays its sound and logs to LocalState\user\log\shad_log.txt.
//
// The game: the first line of LocalState\game.txt (an eboot.bin or a game folder, e.g. on the
// USB drive), else the first folder with an eboot.bin under LocalState\Games.

#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

// C++/WinRT needs the Windows headers' GUID and IUnknown first.
#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.ApplicationModel.Activation.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Core.h>

#include <fmt/format.h>

#include "common/config.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/emulator_state.h"
#include "core/ipc/ipc.h"
#include "emulator.h"
#include "uwp/uwp_host.h"

namespace fs = std::filesystem;
using namespace winrt;
using namespace winrt::Windows::ApplicationModel::Core;
using namespace winrt::Windows::ApplicationModel::Activation;
using namespace winrt::Windows::UI::Core;

namespace {

std::string ToUtf8(const fs::path& path) {
    return Common::FS::PathToUTF8String(path);
}

// An eboot.bin, or a game folder holding one.
std::optional<fs::path> EbootOf(const fs::path& path) {
    std::error_code ec;
    if (fs::is_regular_file(path, ec)) {
        return path;
    }
    if (fs::is_regular_file(path / "eboot.bin", ec)) {
        return path / "eboot.bin";
    }
    return std::nullopt;
}

std::optional<fs::path> FindGame() {
    const fs::path& data = Uwp::DataFolder();

    std::ifstream choice{data / "game.txt"};
    std::string line;
    if (choice && std::getline(choice, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        const fs::path chosen{std::u8string{line.begin(), line.end()}};
        if (auto eboot = EbootOf(chosen)) {
            return eboot;
        }
        Uwp::AppLog(fmt::format("game.txt names {}, which is not an eboot.bin or a game folder "
                                "this app can read",
                                line));
    }

    std::error_code ec;
    const fs::path games = data / "Games";
    fs::create_directories(games, ec);
    for (const auto& entry : fs::directory_iterator(games, ec)) {
        if (auto eboot = EbootOf(entry.path())) {
            return eboot;
        }
    }
    return std::nullopt;
}

// What main.cpp does before Emulator::Run, minus the command line.
void RunEmulator() {
    try {
        Uwp::AppLog("Emulator thread started");
        IPC::Instance().Init();
        EmulatorState::SetInstance(std::make_shared<EmulatorState>());

        Common::FS::SetUwpDataRoot(Uwp::DataFolder());
        Common::FS::InitializeUserPaths(Common::FS::PathInitState::Portable);
        const auto user_dir = Common::FS::GetUserPath(Common::FS::PathType::UserDir);
        Config::load(user_dir / "config.toml");
        Uwp::AppLog("User folder: " + ToUtf8(user_dir));

        const auto eboot = FindGame();
        if (!eboot) {
            Uwp::AppLog("No game. Put a game folder (with eboot.bin) in " +
                        ToUtf8(Uwp::DataFolder() / "Games") +
                        ", or its path on the first line of " +
                        ToUtf8(Uwp::DataFolder() / "game.txt"));
            return;
        }
        Uwp::AppLog("Game: " + ToUtf8(*eboot));

        Uwp::InitControllers();

        auto* emulator = Common::Singleton<Core::Emulator>::Instance();
        emulator->executableName = "shadps4.exe";
        emulator->Run(*eboot);
        Uwp::AppLog("Emulator returned");
    } catch (const winrt::hresult_error& e) {
        Uwp::AppLog(fmt::format("Emulator thread stopped: {:#010x} {}",
                                static_cast<u32>(e.code()), winrt::to_string(e.message())));
    } catch (const std::exception& e) {
        Uwp::AppLog(std::string{"Emulator thread stopped: "} + e.what());
    }
}

struct App : implements<App, IFrameworkViewSource, IFrameworkView> {
    IFrameworkView CreateView() {
        return *this;
    }

    void Initialize(const CoreApplicationView& view) {
        view.Activated([](const CoreApplicationView&, const IActivatedEventArgs&) {
            CoreWindow::GetForCurrentThread().Activate();
        });
    }

    void SetWindow(const CoreWindow& window) {
        window.Closed([this](const CoreWindow&, const CoreWindowEventArgs&) { closed = true; });
    }

    void Load(const hstring&) {}

    void Run() {
        Uwp::AppLog("---- shadPS4 UWP app started ----");
        std::thread{RunEmulator}.detach();

        const auto dispatcher = CoreWindow::GetForCurrentThread().Dispatcher();
        while (!closed) {
            dispatcher.ProcessEvents(CoreProcessEventsOption::ProcessAllIfPresent);
            Uwp::PollControllers();
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        Uwp::AppLog("Window closed");
        Uwp::RequestExit();
    }

    void Uninitialize() {}

    bool closed = false;
};

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    winrt::init_apartment();
    Uwp::RedirectFileApis();
    CoreApplication::Run(make<App>());
    return 0;
}
