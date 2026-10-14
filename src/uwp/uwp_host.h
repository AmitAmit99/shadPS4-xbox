// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <filesystem>
#include <string_view>

// What the UWP app (Xbox Dev Mode, XBOX_PORT.md) and the emulator share. The app owns the
// window and the controllers; the emulator runs on a thread of its own.
namespace Uwp {

/// The app's writable folder (LocalState).
const std::filesystem::path& DataFolder();

/// A line in LocalState\shadps4-uwp.txt, the app's own log (the emulator's log is
/// LocalState\user\log\shad_log.txt).
void AppLog(std::string_view line);

/// Blocks the emulator's main thread until the app is closing.
void WaitForExitRequest();

/// The app is closing: WaitForExitRequest returns.
void RequestExit();

/// Points the file functions the app and its runtimes import at versions that fall back to the
/// *FromApp functions where access is refused (the Xbox's USB drive). Call first, once.
void RedirectFileApis();

/// Logs player 1 in on the guest's controller 0 (the app has no SDL gamepad code).
void InitControllers();

/// Reads the first Xbox controller and hands what changed to the guest's controller 0.
void PollControllers();

} // namespace Uwp
