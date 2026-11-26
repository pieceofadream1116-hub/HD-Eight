#pragma once
// HD Eight (Xbox): runs a piece of Mesen and catches a hard crash (bad memory access, stack overflow...) instead of
// letting the whole app freeze while Windows takes a crash dump. The app shows the report on the TV.

#include <atomic>
#include <cstdint>
#include <type_traits>

namespace hdeight {

/// where: what was running ("starting the game"); code: Windows exception code; offset: crash spot inside HDEight.exe
/// (or the raw address when inImage is false).
using CrashReporter = void (*)(const char* where, unsigned long code, unsigned long long offset, bool inImage);

void SetCrashReporter(CrashReporter reporter);

/// Live "where is it now" markers for the on-screen info: the emulation thread and the video filter thread.
inline std::atomic<const char*> emuMarker{"not started"};
inline std::atomic<const char*> filterMarker{"not started"};
inline std::atomic<uint64_t> emuFrames{0};

/// Calls fn(ctx); returns false if it crashed (the crash was reported and the thread can carry on).
bool RunGuarded(const char* where, void (*fn)(void*), void* ctx);

template<typename F>
bool Guard(const char* where, F&& f)
{
	using Fn = std::remove_reference_t<F>;
	return RunGuarded(where, [](void* p) { (*static_cast<Fn*>(p))(); }, (void*)&f);
}

} // namespace hdeight

#ifdef HDEIGHT_UWP
#define HD8_EMU(text) (hdeight::emuMarker = (text))
#define HD8_FILTER(text) (hdeight::filterMarker = (text))
#else
#define HD8_EMU(text) ((void)0)
#define HD8_FILTER(text) ((void)0)
#endif
