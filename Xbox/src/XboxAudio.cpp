#include "XboxAudio.h"

#include "Shared/HdEightGuard.h"

#include "Shared/Emulator.h"
#include "Shared/EmuSettings.h"
#include "Shared/Audio/SoundMixer.h"

XboxAudio::XboxAudio(Emulator* emu) : _emu(emu)
{
	_emu->GetSoundMixer()->RegisterAudioDevice(this);
}

XboxAudio::~XboxAudio()
{
	_emu->GetSoundMixer()->RegisterAudioDevice(nullptr);
	std::lock_guard lock(_lock);
	if(_stream) {
		SDL_DestroyAudioStream(_stream);
		_stream = nullptr;
	}
}

bool XboxAudio::OpenDevice()
{
	std::lock_guard lock(_lock);
	if(_stream) {
		return true;
	}
	SDL_AudioSpec spec{};
	spec.format = SDL_AUDIO_S16;
	spec.channels = _isStereo ? 2 : 1;
	spec.freq = (int)_sampleRate;
	_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
	if(!_stream) {
		return false;
	}
	// Play from the start: an empty stream just plays silence
	SDL_ResumeAudioStreamDevice(_stream);
	return true;
}

void XboxAudio::PlayBuffer(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate, bool isStereo)
{
	_bufferCount++;
	HD8_EMU("sending sound to the Xbox (SDL audio)");
	std::lock_guard lock(_lock);
	if(!_stream) {
		HD8_EMU("sound skipped (no sound device)");
		return;
	}
	if(sampleRate != _sampleRate || isStereo != _isStereo) {
		// Only the stream's input format changes; the device itself is not touched from this thread
		SDL_AudioSpec spec{};
		spec.format = SDL_AUDIO_S16;
		spec.channels = isStereo ? 2 : 1;
		spec.freq = (int)sampleRate;
		SDL_SetAudioStreamFormat(_stream, &spec, nullptr);
		_sampleRate = sampleRate;
		_isStereo = isStereo;
	}

	const int bytesPerFrame = 2 * (isStereo ? 2 : 1);
	const uint32_t latencyMs = _emu->GetSettings()->GetAudioConfig().AudioLatency;
	const int targetBytes = (int)((uint64_t)sampleRate * latencyMs / 1000) * bytesPerFrame;

	// Too far behind (e.g. after a long menu pause): drop the backlog instead of playing late sound
	if(SDL_GetAudioStreamQueued(_stream) > targetBytes * 4) {
		SDL_ClearAudioStream(_stream);
	}
	SDL_PutAudioStreamData(_stream, soundBuffer, (int)(sampleCount * bytesPerFrame));
	HD8_EMU("sound sent");
}

void XboxAudio::Stop()
{
	std::lock_guard lock(_lock);
	if(_stream) {
		SDL_ClearAudioStream(_stream);
	}
}

void XboxAudio::Pause()
{
	// Nothing to do: the queued sound runs out and the device plays silence until Mesen sends more
}

void XboxAudio::ProcessEndOfFrame()
{
	std::lock_guard lock(_lock);
	if(!_stream || _sampleRate == 0) {
		return;
	}
	const int bytesPerFrame = 2 * (_isStereo ? 2 : 1);
	int queued = SDL_GetAudioStreamQueued(_stream);
	double latencyMs = (double)queued / bytesPerFrame * 1000.0 / _sampleRate;
	if(queued == 0) {
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
	stats.BufferSize = _stream ? (uint32_t)SDL_GetAudioStreamQueued(_stream) : 0;
	return stats;
}
