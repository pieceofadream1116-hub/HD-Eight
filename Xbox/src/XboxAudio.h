#pragma once
// Plays Mesen's mixed sound through an SDL audio stream.

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
	void Open(uint32_t sampleRate, bool isStereo);
	void Close();

	Emulator* _emu;
	std::mutex _lock;
	SDL_AudioStream* _stream = nullptr;
	uint32_t _sampleRate = 0;
	bool _isStereo = true;
	bool _playing = false;
	double _averageLatency = 0;
	uint32_t _underruns = 0;
	std::atomic<uint64_t> _bufferCount = 0;
};
