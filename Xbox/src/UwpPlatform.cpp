#include "UwpPlatform.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <exception>
#include <fstream>
#include <mutex>

namespace uwp {

using namespace winrt;
using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Storage::Pickers;

namespace {

std::mutex g_logLock;
FILE* g_logFile = nullptr;
std::deque<std::string> g_recent;

bool IsWritableDirectory(const std::filesystem::path& path)
{
	std::error_code error;
	if(!std::filesystem::is_directory(path, error)) {
		return false;
	}
	// Really write a file: some drives are listed but read-only for the app container (missing ACLs)
	const auto probe = path / ".hdeight-write-probe";
	{
		std::ofstream file(probe, std::ios::binary | std::ios::trunc);
		if(!file) {
			return false;
		}
		file << "hd8";
		if(!file) {
			return false;
		}
	}
	std::filesystem::remove(probe, error);
	return true;
}

} // namespace

std::filesystem::path FindExternalProfilePath()
{
	for(char drive = 'D'; drive <= 'Z'; drive++) {
		std::filesystem::path candidate = std::string{drive} + ":\\" + kProfileFolderName;
		if(IsWritableDirectory(candidate)) {
			return candidate;
		}
	}
	// No HDEight folder yet: create one on the first external drive the app can write to
	for(char drive = 'D'; drive <= 'Z'; drive++) {
		std::filesystem::path root = std::string{drive} + ":\\";
		if(!IsWritableDirectory(root)) {
			continue;
		}
		std::filesystem::path candidate = root / kProfileFolderName;
		std::error_code error;
		std::filesystem::create_directories(candidate, error);
		if(!error && IsWritableDirectory(candidate)) {
			return candidate;
		}
	}
	return {};
}

std::filesystem::path LocalStatePath()
{
	try {
		return std::filesystem::path(ApplicationData::Current().LocalFolder().Path().c_str());
	} catch(...) {
		return {};
	}
}

void OpenLog(const std::filesystem::path& folder)
{
	std::lock_guard lock(g_logLock);
	if(g_logFile) {
		fclose(g_logFile);
	}
	g_logFile = _wfopen((folder / "hd-eight.log").c_str(), L"w");
}

void Log(std::string_view message)
{
	SYSTEMTIME st{};
	GetLocalTime(&st);
	char stamp[32];
	snprintf(stamp, sizeof(stamp), "[%02d:%02d:%02d.%03d] ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	std::string line = std::string(stamp) + std::string(message) + "\n";

	OutputDebugStringA(line.c_str());
	std::lock_guard lock(g_logLock);
	g_recent.push_back(std::string(stamp) + std::string(message));
	if(g_recent.size() > 200) {
		g_recent.pop_front();
	}
	if(g_logFile) {
		fwrite(line.data(), 1, line.size(), g_logFile);
		fflush(g_logFile);
	}
}

std::vector<std::string> RecentLog(size_t count)
{
	std::lock_guard lock(g_logLock);
	size_t start = g_recent.size() > count ? g_recent.size() - count : 0;
	return std::vector<std::string>(g_recent.begin() + start, g_recent.end());
}

std::vector<std::string> ReadLogFile(const std::filesystem::path& folder)
{
	std::vector<std::string> lines;
	std::ifstream file(folder / "hd-eight.log");
	std::string line;
	while(std::getline(file, line)) {
		lines.push_back(line);
	}
	return lines;
}

namespace {

// Written without the log lock: the crash may have happened while another thread held it
void WriteCrashLine(const char* text)
{
	OutputDebugStringA(text);
	if(g_logFile) {
		fwrite(text, 1, strlen(text), g_logFile);
		fflush(g_logFile);
	}
}

} // namespace

void InstallCrashLogging()
{
	// (UWP apps cannot install a vectored exception handler, so only C++ errors are caught here)
	std::set_terminate([] {
		std::string text = "[!] CRASH: unhandled C++ error";
		try {
			if(auto current = std::current_exception()) {
				std::rethrow_exception(current);
			}
		} catch(const std::exception& e) {
			text += std::string(": ") + e.what();
		} catch(...) {
		}
		text += "\n";
		WriteCrashLine(text.c_str());
		std::abort();
	});
}

void UseFullCoreWindowBounds()
{
	try {
		using namespace winrt::Windows::UI::ViewManagement;
		ApplicationView::GetForCurrentView().SetDesiredBoundsMode(ApplicationViewBoundsMode::UseCoreWindow);
	} catch(...) {
		// Not fatal: the app still works inside the TV-safe area
	}
}

namespace {

fire_and_forget PickFolderAsync(std::function<void(std::string)> callback)
{
	std::string result;
	try {
		FolderPicker picker;
		try {
			picker.SuggestedStartLocation(PickerLocationId::ComputerFolder);
		} catch(...) {
		}
		picker.FileTypeFilter().Append(L"*");
		if(auto folder = co_await picker.PickSingleFolderAsync()) {
			result = to_string(folder.Path());
		}
	} catch(...) {
		result.clear();
	}
	callback(result);
}

} // namespace

void PickFolder(std::function<void(std::string)> callback)
{
	PickFolderAsync(std::move(callback));
}

} // namespace uwp
