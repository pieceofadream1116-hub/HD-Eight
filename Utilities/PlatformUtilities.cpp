#include "pch.h"
#include "PlatformUtilities.h"

#if defined(_WIN32) && !defined(HDEIGHT_UWP)
#include <Windows.h>
#endif

void PlatformUtilities::DisableScreensaver()
{
	//Prevent screensaver/etc from starting while using the emulator
	//DirectInput devices apparently do not always count as user input
	#if defined(_WIN32) && !defined(HDEIGHT_UWP)
	SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED | ES_CONTINUOUS);
	#endif
}

void PlatformUtilities::EnableScreensaver()
{
	#if defined(_WIN32) && !defined(HDEIGHT_UWP)
	SetThreadExecutionState(ES_CONTINUOUS);
	#endif
}

void PlatformUtilities::EnableHighResolutionTimer()
{
	#if defined(_WIN32) && !defined(HDEIGHT_UWP)
	//Request a 1ms timer resolution on Windows while a game is running
	timeBeginPeriod(1);
	#endif
}

void PlatformUtilities::RestoreTimerResolution()
{
	#if defined(_WIN32) && !defined(HDEIGHT_UWP)
	timeEndPeriod(1);
	#endif
}