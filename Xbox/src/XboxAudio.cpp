#include "XboxAudio.h"

#include "Shared/HdEightGuard.h"
#include "UwpPlatform.h"

#include <string>
#include <thread>

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
	SDL_AudioStream* stream = _stream.exchange(nullptr);
	std::lock_guard lock(_lock);
	if(stream) {
		SDL_DestroyAudioStream(stream);
	}
}

namespace {

// The Saturn app gives SDL a callback: SDL calls it when the device wants more sound. Mesen pushes its sound with
// SDL_PutAudioStreamData instead, so there is nothing to do here.
void SDLCALL AudioCallback(void*, SDL_AudioStream*, int, int) {}

} // namespace

void XboxAudio::OpenDeviceInBackground()
{
	_state = "opening the Xbox sound device";
	uwp::Log("Opening the Xbox sound device (on its own thread)");
	SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "512");
	std::thread([] {
		auto start = SDL_GetTicks();
		SDL_AudioSpec spec{};
		spec.format = SDL_AUDIO_S16;
		spec.channels = 2;
		spec.freq = 48000;
		SDL_AudioStream* stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioCallback, nullptr);
		if(!stream) {
			_state = "no sound device (games play silently)";
			uwp::Log(std::string("[!] Could not open the sound device: ") + SDL_GetError());
			return;
		}
		_state = "starting the sound device";
		SDL_ResumeAudioStreamDevice(stream);
		_stream = stream;
		_state = "sound on";
		uwp::Log("Sound device open after " + std::to_string(SDL_GetTicks() - start) + " ms");
	}).detach();
}

void XboxAudio::PlayBuffer(int16_t* soundBuffer, uint32_t sampleCount, uint32_t sampleRate, bool isStereo)
{
	_bufferCount++;
	HD8_EMU("sending sound to the Xbox (SDL audio)");
	if(!_stream) {
		HD8_EMU("sound skipped (sound device not open yet)");
		return;
	}
	std::lock_guard lock(_lock);
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
