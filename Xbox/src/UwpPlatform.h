#pragma once
// UWP (Xbox Dev Mode / Windows) helpers for HD Eight. Implemented with C++/WinRT in UwpPlatform.cpp.

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace uwp {

/// Name of the folder at the root of a drive (e.g. E:\HDEight) where HD Eight keeps its settings, saves and games.
inline constexpr const char* kProfileFolderName = "HDEight";

/// Finds (or creates) a writable <drive>:\HDEight folder on drives D: to Z:. Empty if no drive is usable.
std::filesystem::path FindExternalProfilePath();

/// The app's LocalState folder.
std::filesystem::path LocalStatePath();

/// Opens hd-eight.log in the profile folder. Later Log() calls append to it.
void OpenLog(const std::filesystem::path& folder);

/// Writes a timestamped line to the log file (and the debugger).
void Log(std::string_view message);

/// Like Log, but never waits more than a moment for the log lock (for the lookout thread).
void LogUrgent(std::string_view message);

/// The newest log lines, oldest first (for showing on the TV).
std::vector<std::string> RecentLog(size_t count);

/// Reads hd-eight.log as the last session left it (call before OpenLog, which starts a new one).
std::vector<std::string> ReadLogFile(const std::filesystem::path& folder);

/// Writes unhandled C++ errors into the log before the app closes.
void InstallCrashLogging();

/// Use the whole screen on Xbox instead of the TV-safe area (no black borders).
void UseFullCoreWindowBounds();

/// Shows the system folder picker. The callback receives the chosen path, or an empty string if cancelled.
/// Must be called from the UI thread; the callback may run on another thread.
void PickFolder(std::function<void(std::string)> callback);

} // namespace uwp
