#pragma once
// Plays Mesen's mixed sound through an SDL audio stream.
//
// Opening the Xbox sound device hung both from Mesen's emulation thread (0.1.0.9) and from the screen thread
// at startup (0.1.0.10 stuck on the splash screen), so it is opened on a thread of its own that nothing waits for.
// Mesen's thread only pours samples into the stream once it is open.

#include "pch.h"
#include "Shared/Interfaces/IAudioDevice.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <mutex>

class Emulator;

class XboxAudio final : public IAudioDevice
{
public:
	explicit XboxAudio(Emulator* emu);
	~XboxAudio() override;

	/// Starts opening the Xbox sound device on its own thread (opening it can hang on Xbox, from any thread).
	/// Until it is open, Mesen's sound is skipped and games play silently.
	void OpenDeviceInBackground();

	/// What the sound device is doing, for the on-screen info.
	const char* State() const { return _state; }

	void PlayBuffer(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate, bool isStereo) override;
	void Stop() override;
	void Pause() override;
	void ProcessEndOfFrame() override;

	string GetAvailableDevices() override { return "Default"; }
	void SetAudioDevice(string) override {}

	AudioStatistics GetStatistics() override;

	/// How many sound buffers Mesen has sent (for the on-screen info).
	uint64_t BufferCount() const { return _bufferCount; }

private:
	Emulator* _emu;
	std::mutex _lock;
	std::atomic<SDL_AudioStream*> _stream = nullptr;
	std::atomic<const char*> _state = "not started";
	uint32_t _sampleRate = 48000; // format of the samples Mesen sends (the stream converts to the device format)
	bool _isStereo = true;
	double _averageLatency = 0;
	uint32_t _underruns = 0;
	std::atomic<uint64_t> _bufferCount = 0;
};
