#pragma once
// Plays Mesen's mixed sound through XAudio2, the Xbox's own sound system.
//
// SDL's sound path (WASAPI) cannot be used on Xbox: opening the device waits forever for an activation that the
// console never delivers (HD Eight 0.1.0.9 to 0.1.0.12, and the Saturn app before it). XAudio2 starts instantly.
// Each sound buffer from Mesen is copied into one of a few slots and queued on a source voice; when every slot
// is still playing, the buffer is dropped instead of making the emulator wait.

#include "pch.h"
#include "Shared/Interfaces/IAudioDevice.h"

#include <array>
#include <atomic>
#include <mutex>
#include <vector>

class Emulator;
struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

class XboxAudio final : public IAudioDevice
{
public:
	explicit XboxAudio(Emulator* emu);
	~XboxAudio() override;

	/// Starts XAudio2 (main thread, at startup). Returns false if there is no sound; games then play silently.
	bool OpenDevice();

	/// What the sound is doing, for the on-screen info.
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

	/// Called from XAudio2's thread when a slot has finished playing.
	void OnSlotDone(size_t slot);

private:
	static constexpr size_t kSlots = 8;

	void Release();

	Emulator* _emu;
	std::mutex _lock;
	IXAudio2* _engine = nullptr;
	IXAudio2MasteringVoice* _master = nullptr;
	IXAudio2SourceVoice* _voice = nullptr;
	class Callback;
	Callback* _callback = nullptr;

	std::array<std::vector<int16_t>, kSlots> _slots;
	std::array<std::atomic<bool>, kSlots> _busy{};
	std::array<uint32_t, kSlots> _slotFrames{};
	std::array<size_t, kSlots> _slotIds{};
	std::atomic<uint32_t> _queuedFrames = 0;
	size_t _next = 0;
	bool _started = false;

	uint32_t _sampleRate = 48000;
	std::atomic<const char*> _state = "not started";
	double _averageLatency = 0;
	uint32_t _underruns = 0;
	uint32_t _dropped = 0;
	std::atomic<uint64_t> _bufferCount = 0;
};
