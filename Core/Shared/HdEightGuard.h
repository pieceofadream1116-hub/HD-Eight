#pragma once
// HD Eight (Xbox): runs a piece of Mesen and catches a hard crash (bad memory access, stack overflow...) instead of
// letting the whole app freeze while Windows takes a crash dump. The app shows the report on the TV.

#include <type_traits>

namespace hdeight {

/// where: what was running ("starting the game"); code: Windows exception code; offset: crash spot inside HDEight.exe
/// (or the raw address when inImage is false).
using CrashReporter = void (*)(const char* where, unsigned long code, unsigned long long offset, bool inImage);

void SetCrashReporter(CrashReporter reporter);

/// Calls fn(ctx); returns false if it crashed (the crash was reported and the thread can carry on).
bool RunGuarded(const char* where, void (*fn)(void*), void* ctx);

template<typename F>
bool Guard(const char* where, F&& f)
{
	using Fn = std::remove_reference_t<F>;
	return RunGuarded(where, [](void* p) { (*static_cast<Fn*>(p))(); }, (void*)&f);
}

} // namespace hdeight
