// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <array>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <fstream>
#include <mutex>

// C++/WinRT needs the Windows headers' GUID and IUnknown first.
#include <windows.h>
#include <unknwn.h>

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Gaming.Input.h>
#include <winrt/Windows.Storage.h>

#include "common/singleton.h"
#include "core/libraries/pad/pad.h"
#include "core/libraries/system/userservice.h"
#include "input/controller.h"
#include "uwp/uwp_host.h"

namespace Uwp {

namespace WGI = winrt::Windows::Gaming::Input;
using Libraries::Pad::OrbisPadButtonDataOffset;

const std::filesystem::path& DataFolder() {
    static const std::filesystem::path folder{
        std::wstring{winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path()}};
    return folder;
}

void AppLog(std::string_view line) {
    static std::mutex mutex;
    std::scoped_lock lk{mutex};
    std::ofstream file{DataFolder() / "shadps4-uwp.txt", std::ios::app};
    const std::time_t now = std::time(nullptr);
    char stamp[32]{};
    std::tm local{};
    localtime_s(&local, &now);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &local);
    file << stamp << "  " << line << '\n';
}

namespace {
std::mutex exit_mutex;
std::condition_variable exit_cv;
bool exit_requested = false;
} // namespace

void WaitForExitRequest() {
    std::unique_lock lk{exit_mutex};
    exit_cv.wait(lk, [] { return exit_requested; });
}

void RequestExit() {
    {
        std::scoped_lock lk{exit_mutex};
        exit_requested = true;
    }
    exit_cv.notify_all();
}

void InitControllers() {
    auto* controllers = Common::Singleton<Input::GameControllers>::Instance();
    auto* pad = (*controllers)[0];
    pad->user_id = 1;
    Libraries::UserService::AddUserServiceEvent(
        {Libraries::UserService::OrbisUserServiceEventType::Login, 1});
    AppLog("Player 1 logged in on controller 0");
}

namespace {

// The Xbox controller's buttons as the PS4 pad's, as KytyPS5's Xbox build maps the PS5's:
// A/B/X/Y = Cross/Circle/Square/Triangle, Menu = Options, View = touchpad press.
struct ButtonMap {
    WGI::GamepadButtons xbox;
    OrbisPadButtonDataOffset ps4;
};
constexpr std::array<ButtonMap, 14> button_map{{
    {WGI::GamepadButtons::A, OrbisPadButtonDataOffset::Cross},
    {WGI::GamepadButtons::B, OrbisPadButtonDataOffset::Circle},
    {WGI::GamepadButtons::X, OrbisPadButtonDataOffset::Square},
    {WGI::GamepadButtons::Y, OrbisPadButtonDataOffset::Triangle},
    {WGI::GamepadButtons::LeftShoulder, OrbisPadButtonDataOffset::L1},
    {WGI::GamepadButtons::RightShoulder, OrbisPadButtonDataOffset::R1},
    {WGI::GamepadButtons::LeftThumbstick, OrbisPadButtonDataOffset::L3},
    {WGI::GamepadButtons::RightThumbstick, OrbisPadButtonDataOffset::R3},
    {WGI::GamepadButtons::Menu, OrbisPadButtonDataOffset::Options},
    {WGI::GamepadButtons::View, OrbisPadButtonDataOffset::TouchPad},
    {WGI::GamepadButtons::DPadUp, OrbisPadButtonDataOffset::Up},
    {WGI::GamepadButtons::DPadDown, OrbisPadButtonDataOffset::Down},
    {WGI::GamepadButtons::DPadLeft, OrbisPadButtonDataOffset::Left},
    {WGI::GamepadButtons::DPadRight, OrbisPadButtonDataOffset::Right},
}};

// Sticks: -1..1 to the pad's 0..255 (128 centre). The Xbox's up is +1, the PS4's up is 0.
int StickValue(double value, bool invert) {
    const double v = invert ? -value : value;
    return Input::GetAxis(-0x8000, 0x8000, static_cast<int>(v * 0x7FFF));
}

// Triggers: 0..1 to 0..255.
int TriggerValue(double value) {
    return Input::GetAxis(0, 0x8000, static_cast<int>(value * 0x7FFF));
}

struct LastReading {
    bool valid = false;
    WGI::GamepadButtons buttons{};
    std::array<int, 6> axes{};
};

} // namespace

void PollControllers() {
    static LastReading last;
    static bool was_connected = false;

    const auto gamepads = WGI::Gamepad::Gamepads();
    if (gamepads.Size() == 0) {
        if (was_connected) {
            AppLog("Controller disconnected");
            was_connected = false;
            last.valid = false;
        }
        return;
    }
    if (!was_connected) {
        AppLog("Controller connected");
        was_connected = true;
    }

    const auto reading = gamepads.GetAt(0).GetCurrentReading();
    auto* pad = (*Common::Singleton<Input::GameControllers>::Instance())[0];

    for (const auto& map : button_map) {
        const bool pressed = (reading.Buttons & map.xbox) == map.xbox;
        const bool was_pressed = last.valid && (last.buttons & map.xbox) == map.xbox;
        if (pressed != was_pressed || !last.valid) {
            pad->CheckButton(0, map.ps4, pressed);
        }
    }

    const std::array<int, 6> axes{
        StickValue(reading.LeftThumbstickX, false),  StickValue(reading.LeftThumbstickY, true),
        StickValue(reading.RightThumbstickX, false), StickValue(reading.RightThumbstickY, true),
        TriggerValue(reading.LeftTrigger),           TriggerValue(reading.RightTrigger),
    };
    constexpr std::array<Input::Axis, 6> axis_ids{
        Input::Axis::LeftX,  Input::Axis::LeftY,       Input::Axis::RightX,
        Input::Axis::RightY, Input::Axis::TriggerLeft, Input::Axis::TriggerRight,
    };
    for (size_t i = 0; i < axes.size(); i++) {
        if (!last.valid || axes[i] != last.axes[i]) {
            pad->Axis(axis_ids[i], axes[i]);
        }
    }

    last.valid = true;
    last.buttons = reading.Buttons;
    last.axes = axes;
}

} // namespace Uwp
