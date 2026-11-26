#include "UwpPlatform.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.Storage.Pickers.h>
#include <winrt/Windows.UI.ViewManagement.h>

#include <windows.h>

#include <cstdio>
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
	if(g_logFile) {
		fwrite(line.data(), 1, line.size(), g_logFile);
		fflush(g_logFile);
	}
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
