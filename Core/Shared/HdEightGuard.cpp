#include "pch.h"
#include "Shared/HdEightGuard.h"

#if defined(_WIN32)
#include <windows.h>
#include <malloc.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace hdeight {

static CrashReporter g_reporter = nullptr;

void SetCrashReporter(CrashReporter reporter)
{
	g_reporter = reporter;
}

static int CrashFilter(EXCEPTION_POINTERS* info, unsigned long* code, ULONG_PTR* address)
{
	DWORD c = info->ExceptionRecord->ExceptionCode;
	switch(c) {
		case EXCEPTION_ACCESS_VIOLATION:
		case EXCEPTION_STACK_OVERFLOW:
		case EXCEPTION_ILLEGAL_INSTRUCTION:
		case EXCEPTION_INT_DIVIDE_BY_ZERO:
		case EXCEPTION_PRIV_INSTRUCTION:
		case EXCEPTION_IN_PAGE_ERROR:
		case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
		case EXCEPTION_DATATYPE_MISALIGNMENT:
		case 0xC0000374: // heap corruption
			*code = c;
			*address = (ULONG_PTR)info->ExceptionRecord->ExceptionAddress;
			return EXCEPTION_EXECUTE_HANDLER;
		default:
			return EXCEPTION_CONTINUE_SEARCH; // C++ exceptions and the like go on to their normal handlers
	}
}

static void Report(const char* where, unsigned long code, ULONG_PTR address)
{
	if(code == EXCEPTION_STACK_OVERFLOW) {
		_resetstkoflw();
	}
	ULONG_PTR base = (ULONG_PTR)&__ImageBase;
	auto nt = (IMAGE_NT_HEADERS*)(base + __ImageBase.e_lfanew);
	bool inImage = address >= base && address < base + nt->OptionalHeader.SizeOfImage;
	if(g_reporter) {
		g_reporter(where, code, inImage ? (unsigned long long)(address - base) : (unsigned long long)address, inImage);
	}
}

bool RunGuarded(const char* where, void (*fn)(void*), void* ctx)
{
	unsigned long code = 0;
	ULONG_PTR address = 0;
	__try {
		fn(ctx);
	} __except(CrashFilter(GetExceptionInformation(), &code, &address)) {
		Report(where, code, address);
		return false;
	}
	return true;
}

} // namespace hdeight

#else

namespace hdeight {
void SetCrashReporter(CrashReporter) {}
bool RunGuarded(const char*, void (*fn)(void*), void* ctx)
{
	fn(ctx);
	return true;
}
} // namespace hdeight

#endif
