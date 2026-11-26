#pragma once
// Plays Mesen's mixed sound through an SDL audio stream.
//
// The Xbox sound device is opened once, on the screen (main) thread, when the app starts: opening it from Mesen's
// emulation thread hangs on Xbox (test of 0.1.0.9 stuck on the first sound buffer). Mesen's thread only pours
// samples into the open stream.

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

	/// Main thread only: opens the Xbox sound device and starts it playing. Returns false if there is no sound device.
	bool OpenDevice();

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
	SDL_AudioStream* _stream = nullptr;
	uint32_t _sampleRate = 48000; // format of the samples Mesen sends (the stream converts to the device format)
	bool _isStereo = true;
	double _averageLatency = 0;
	uint32_t _underruns = 0;
	std::atomic<uint64_t> _bufferCount = 0;
};
