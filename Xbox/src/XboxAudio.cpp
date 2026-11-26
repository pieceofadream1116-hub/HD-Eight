#include "XboxAudio.h"

#include "Shared/HdEightGuard.h"
#include "UwpPlatform.h"

#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/Audio/SoundMixer.h"

#include <windows.h>
#include <xaudio2.h>

#include <cstring>
#include <string>

class XboxAudio::Callback final : public IXAudio2VoiceCallback
{
public:
	explicit Callback(XboxAudio& owner) : _owner(owner) {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
	void STDMETHODCALLTYPE OnVoiceProcessingPassEnd() override {}
	void STDMETHODCALLTYPE OnStreamEnd() override {}
	void STDMETHODCALLTYPE OnBufferStart(void*) override {}
	void STDMETHODCALLTYPE OnLoopEnd(void*) override {}
	void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT) override {}
	void STDMETHODCALLTYPE OnBufferEnd(void* context) override
	{
		_owner.OnSlotDone(*static_cast<size_t*>(context));
	}

private:
	XboxAudio& _owner;
};

XboxAudio::XboxAudio(Emulator* emu) : _emu(emu)
{
	for(size_t i = 0; i < kSlots; i++) {
		_slotIds[i] = i;
	}
	_callback = new Callback(*this);
	_emu->GetSoundMixer()->RegisterAudioDevice(this);
}

XboxAudio::~XboxAudio()
{
	_emu->GetSoundMixer()->RegisterAudioDevice(nullptr);
	{
		std::lock_guard lock(_lock);
		Release();
	}
	delete _callback;
}

void XboxAudio::Release()
{
	if(_voice) {
		_voice->Stop(0);
		_voice->FlushSourceBuffers();
		_voice->DestroyVoice();
		_voice = nullptr;
	}
	if(_master) {
		_master->DestroyVoice();
		_master = nullptr;
	}
	if(_engine) {
		_engine->Release();
		_engine = nullptr;
	}
	for(auto& busy : _busy) {
		busy = false;
	}
	_queuedFrames = 0;
	_started = false;
}

bool XboxAudio::OpenDevice()
{
	std::lock_guard lock(_lock);
	_state = "starting XAudio2";
	if(FAILED(XAudio2Create(&_engine, 0, XAUDIO2_DEFAULT_PROCESSOR)) || !_engine) {
		_engine = nullptr;
		_state = "no sound (XAudio2 would not start)";
		uwp::Log("[!] XAudio2Create failed");
		return false;
	}
	if(FAILED(_engine->CreateMasteringVoice(&_master, 2, _sampleRate, 0, nullptr, nullptr, AudioCategory_GameEffects)) || !_master) {
		_master = nullptr;
		Release();
		_state = "no sound (no speakers found)";
		uwp::Log("[!] XAudio2 CreateMasteringVoice failed");
		return false;
	}
	WAVEFORMATEX format{};
	format.wFormatTag = WAVE_FORMAT_PCM;
	format.nChannels = 2;
	format.nSamplesPerSec = _sampleRate;
	format.wBitsPerSample = 16;
	format.nBlockAlign = 4;
	format.nAvgBytesPerSec = _sampleRate * 4;
	// Allow up to 4x the base rate so Mesen's small speed adjustments and other rates can be followed
	if(FAILED(_engine->CreateSourceVoice(&_voice, &format, 0, 4.0f, _callback)) || !_voice) {
		_voice = nullptr;
		Release();
		_state = "no sound (could not make a voice)";
		uwp::Log("[!] XAudio2 CreateSourceVoice failed");
		return false;
	}
	_state = "sound on (XAudio2)";
	uwp::Log("Sound on (XAudio2, 48000 Hz stereo)");
	return true;
}

void XboxAudio::OnSlotDone(size_t slot)
{
	_queuedFrames -= _slotFrames[slot];
	_busy[slot] = false;
}

void XboxAudio::PlayBuffer(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate, bool isStereo)
{
	_bufferCount++;
	std::lock_guard lock(_lock);
	if(!_voice || sampleCount == 0) {
		return;
	}
	if(sampleRate != _sampleRate && sampleRate > 0) {
		_voice->SetFrequencyRatio((float)sampleRate / (float)_sampleRate);
	}

	// Too far behind (e.g. after a long menu pause): drop the backlog instead of playing late sound
	const uint32_t latencyMs = _emu->GetSettings()->GetAudioConfig().AudioLatency;
	const uint32_t targetFrames = sampleRate * latencyMs / 1000;
	if(_queuedFrames > targetFrames * 4) {
		_voice->FlushSourceBuffers();
	}

	size_t slot = _next;
	if(_busy[slot]) {
		_dropped++;
		return; // every slot is still playing: skip this bit rather than make the game wait
	}
	_next = (_next + 1) % kSlots;

	auto& data = _slots[slot];
	if(isStereo) {
		data.assign(soundBuffer, soundBuffer + sampleCount * 2);
	} else {
		data.resize(sampleCount * 2);
		for(uint32_t i = 0; i < sampleCount; i++) {
			data[i * 2] = data[i * 2 + 1] = soundBuffer[i];
		}
	}
	_slotFrames[slot] = sampleCount;
	_busy[slot] = true;
	_queuedFrames += sampleCount;

	XAUDIO2_BUFFER buffer{};
	buffer.AudioBytes = (UINT32)(data.size() * sizeof(int16_t));
	buffer.pAudioData = reinterpret_cast<const BYTE*>(data.data());
	buffer.pContext = &_slotIds[slot];
	if(FAILED(_voice->SubmitSourceBuffer(&buffer))) {
		_busy[slot] = false;
		_queuedFrames -= sampleCount;
		return;
	}
	if(!_started && _queuedFrames >= targetFrames / 2) {
		_voice->Start(0);
		_started = true;
	}
}

void XboxAudio::Stop()
{
	std::lock_guard lock(_lock);
	if(_voice) {
		_voice->Stop(0);
		_voice->FlushSourceBuffers();
		_started = false;
	}
}

void XboxAudio::Pause()
{
	std::lock_guard lock(_lock);
	if(_voice) {
		_voice->Stop(0);
		_started = false;
	}
}

void XboxAudio::ProcessEndOfFrame()
{
	std::lock_guard lock(_lock);
	if(!_voice) {
		return;
	}
	double latencyMs = (double)_queuedFrames * 1000.0 / _sampleRate;
	if(_started && _queuedFrames == 0) {
		_underruns++;
	}
	// Smoothed, like Mesen's own sound managers: the mixer nudges its sample rate to keep this near the target
	_averageLatency = _averageLatency == 0 ? latencyMs : _averageLatency * 0.95 + latencyMs * 0.05;
}

AudioStatistics XboxAudio::GetStatistics()
{
	std::lock_guard lock(_lock);
	AudioStatistics stats;
	stats.AverageLatency = _averageLatency;
	stats.BufferUnderrunEventCount = _underruns;
	stats.BufferSize = _queuedFrames * 4;
	return stats;
}
