#include "Core.h"

#include "UwpPlatform.h"
#include "XboxAudio.h"

#include "libretro.h"

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace fs = std::filesystem;

namespace {

Core* g_core = nullptr;

std::string U8(const fs::path& path)
{
	auto s = path.u8string();
	return std::string(s.begin(), s.end());
}

bool EnvironmentCallback(unsigned cmd, void* data)
{
	return g_core && g_core->Environment(cmd, data);
}
void VideoCallback(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if(g_core) {
		g_core->VideoRefresh(data, width, height, pitch);
	}
}
void AudioSampleCallback(int16_t left, int16_t right)
{
	int16_t frame[2] = {left, right};
	if(g_core) {
		g_core->AudioBatch(frame, 1);
	}
}
size_t AudioBatchCallback(const int16_t* data, size_t frames)
{
	return g_core ? g_core->AudioBatch(data, frames) : frames;
}
void InputPollCallback()
{
	if(g_core) {
		g_core->InputPoll();
	}
}
int16_t InputStateCallback(unsigned port, unsigned device, unsigned index, unsigned id)
{
	return g_core ? g_core->InputState(port, device, index, id) : 0;
}

void LogCallback(enum retro_log_level level, const char* fmt, ...)
{
	if(level < RETRO_LOG_INFO) {
		return; // debug chatter
	}
	char text[1024];
	va_list args;
	va_start(args, fmt);
	vsnprintf(text, sizeof(text), fmt, args);
	va_end(args);
	std::string line(text);
	while(!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
		line.pop_back();
	}
	uwp::Log((level >= RETRO_LOG_ERROR ? "[!] Core: " : "Core: ") + line);
}

// Genesis button -> the libretro pad button Genesis Plus GX reads for it (see its input descriptors)
constexpr unsigned kRetroIdFor[genesis::ButtonCount] = {
	RETRO_DEVICE_ID_JOYPAD_UP, RETRO_DEVICE_ID_JOYPAD_DOWN, RETRO_DEVICE_ID_JOYPAD_LEFT, RETRO_DEVICE_ID_JOYPAD_RIGHT,
	RETRO_DEVICE_ID_JOYPAD_Y,      // A
	RETRO_DEVICE_ID_JOYPAD_B,      // B
	RETRO_DEVICE_ID_JOYPAD_A,      // C
	RETRO_DEVICE_ID_JOYPAD_L,      // X
	RETRO_DEVICE_ID_JOYPAD_X,      // Y
	RETRO_DEVICE_ID_JOYPAD_R,      // Z
	RETRO_DEVICE_ID_JOYPAD_START,  // Start
	RETRO_DEVICE_ID_JOYPAD_SELECT, // Mode
};

int16_t RetroMask(uint32_t genesisBits)
{
	int16_t mask = 0;
	for(uint32_t b = 0; b < genesis::ButtonCount; b++) {
		if((genesisBits >> b) & 1) {
			mask |= (int16_t)(1 << kRetroIdFor[b]);
		}
	}
	return mask;
}

} // namespace

Core::Core(XboxAudio* audio, InputSource input) : _audio(audio), _input(std::move(input))
{
	g_core = this;
	retro_set_environment(EnvironmentCallback);
	retro_set_video_refresh(VideoCallback);
	retro_set_audio_sample(AudioSampleCallback);
	retro_set_audio_sample_batch(AudioBatchCallback);
	retro_set_input_poll(InputPollCallback);
	retro_set_input_state(InputStateCallback);
	SetOptions(_options);
}

Core::~Core()
{
	Unload();
	if(_initialized) {
		retro_deinit();
		_initialized = false;
	}
	g_core = nullptr;
}

void Core::SetFolders(fs::path system, fs::path saves)
{
	_systemDir = std::move(system);
	_savesDir = std::move(saves);
	_systemDirU8 = U8(_systemDir);
	_savesDirU8 = U8(_savesDir);
}

void Core::SetOptions(const Options& options)
{
	std::lock_guard lock(_variablesLock);
	_options = options;
	_variables["genesis_plus_gx_no_sprite_limit"] = options.noSpriteLimit ? "enabled" : "disabled";
	_variables["genesis_plus_gx_blargg_ntsc_filter"] = options.tvLook ? "composite" : "disabled";
	_variables["genesis_plus_gx_ym2612"] = options.bestSound ? "nuked (ym2612)" : "mame (ym2612)";
	_variables["genesis_plus_gx_region_detect"] = options.region;
	_variables["genesis_plus_gx_system_bram"] = "per bios";
	_variables["genesis_plus_gx_bios"] = "disabled";
	_variables["genesis_plus_gx_sound_output"] = "stereo";
	_variables["genesis_plus_gx_aspect_ratio"] = "auto";
	_variables["genesis_plus_gx_overscan"] = "disabled";
	_variables["genesis_plus_gx_render"] = "single field";
	_variables["genesis_plus_gx_frameskip"] = "disabled";
	_variablesChanged = true;
}

bool Core::Environment(unsigned cmd, void* data)
{
	switch(cmd) {
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
			static_cast<retro_log_callback*>(data)->log = LogCallback;
			return true;
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
			int format = *static_cast<const enum retro_pixel_format*>(data);
			if(format != RETRO_PIXEL_FORMAT_RGB565 && format != RETRO_PIXEL_FORMAT_XRGB8888 && format != RETRO_PIXEL_FORMAT_0RGB1555) {
				return false;
			}
			_pixelFormat = format;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			*static_cast<const char**>(data) = _systemDirU8.c_str();
			return true;
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
			*static_cast<const char**>(data) = _savesDirU8.c_str();
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE: {
			auto* var = static_cast<retro_variable*>(data);
			std::lock_guard lock(_variablesLock);
			auto it = var->key ? _variables.find(var->key) : _variables.end();
			var->value = it == _variables.end() ? nullptr : it->second.c_str();
			return var->value != nullptr;
		}
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			*static_cast<bool*>(data) = _variablesChanged.exchange(false);
			return true;
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
			return true;
		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*static_cast<bool*>(data) = true;
			return true;
		case RETRO_ENVIRONMENT_SET_GEOMETRY: {
			const auto* geometry = static_cast<const retro_game_geometry*>(data);
			if(geometry->aspect_ratio > 0) {
				_aspect = geometry->aspect_ratio;
			}
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO: {
			const auto* info = static_cast<const retro_system_av_info*>(data);
			if(info->geometry.aspect_ratio > 0) {
				_aspect = info->geometry.aspect_ratio;
			}
			if(info->timing.fps > 0) {
				_fps = info->timing.fps;
			}
			if(info->timing.sample_rate > 0) {
				_sampleRate = info->timing.sample_rate;
			}
			return true;
		}
		case RETRO_ENVIRONMENT_SET_MESSAGE: {
			const auto* message = static_cast<const retro_message*>(data);
			if(message && message->msg) {
				uwp::Log(std::string("Core message: ") + message->msg);
			}
			return true;
		}
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
			return true;
		default:
			return false; // core options v2, VFS, LED, disk control...: the core falls back to its defaults
	}
}

void Core::VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch)
{
	if(!data || width == 0 || height == 0) {
		return; // duplicated frame: keep the last picture
	}
	std::lock_guard lock(_frameLock);
	_frame.resize((size_t)width * height);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	for(unsigned y = 0; y < height; y++) {
		uint32_t* out = _frame.data() + (size_t)y * width;
		if(_pixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
			const uint32_t* line = reinterpret_cast<const uint32_t*>(src + y * pitch);
			for(unsigned x = 0; x < width; x++) {
				out[x] = 0xFF000000 | line[x];
			}
		} else if(_pixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
			const uint16_t* line = reinterpret_cast<const uint16_t*>(src + y * pitch);
			for(unsigned x = 0; x < width; x++) {
				uint16_t p = line[x];
				uint32_t r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
				out[x] = 0xFF000000 | ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
			}
		} else {
			const uint16_t* line = reinterpret_cast<const uint16_t*>(src + y * pitch);
			for(unsigned x = 0; x < width; x++) {
				uint16_t p = line[x];
				uint32_t r = (p >> 10) & 0x1F, g = (p >> 5) & 0x1F, b = p & 0x1F;
				out[x] = 0xFF000000 | ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
			}
		}
	}
	_width = width;
	_height = height;
	_newFrame = true;
}

size_t Core::AudioBatch(const int16_t* data, size_t frames)
{
	if(_audio && frames) {
		_audio->Push(data, (uint32_t)frames, (uint32_t)_sampleRate);
	}
	return frames;
}

void Core::InputPoll()
{
	for(int player = 0; player < 4; player++) {
		_padState[player] = _input ? _input(player) : 0;
	}
}

int16_t Core::InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	(void)index;
	if(port >= 4 || (device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD) {
		return 0;
	}
	int16_t mask = RetroMask(_padState[port]);
	if(id == RETRO_DEVICE_ID_JOYPAD_MASK) {
		return mask;
	}
	return id < 16 ? (int16_t)((mask >> id) & 1) : 0;
}

bool Core::Load(const fs::path& game)
{
	Unload();
	_error.clear();
	_game = game;
	if(!_initialized) {
		retro_init();
		_initialized = true;
	}
	for(unsigned port = 0; port < 2; port++) {
		retro_set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
	}
	_marker = "loading the game";
	std::string path = U8(game);
	retro_game_info info{};
	info.path = path.c_str();
	if(!retro_load_game(&info)) {
		_error = "Genesis Plus GX could not start this game (it may be the wrong kind of file, or a Sega CD game whose BIOS is missing).";
		_marker = "load failed";
		return false;
	}
	retro_system_av_info av{};
	retro_get_system_av_info(&av);
	_fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
	_sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
	_aspect = av.geometry.aspect_ratio > 0 ? av.geometry.aspect_ratio : 4.0 / 3.0;
	uwp::Log("Game loaded: " + std::to_string(av.geometry.base_width) + "x" + std::to_string(av.geometry.base_height) +
		", " + std::to_string(_fps) + " frames a second, sound " + std::to_string((int)_sampleRate) + " Hz");

	// Battery save (cartridge SRAM), kept next to the save states
	if(void* sram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)) {
		size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
		std::ifstream in(_savesDir / (game.stem().wstring() + L".srm"), std::ios::binary);
		if(in && size) {
			in.read(static_cast<char*>(sram), (std::streamsize)size);
			uwp::Log("Battery save loaded");
		}
	}

	_loaded = true;
	_stop = false;
	_paused = false;
	_frames = 0;
	_thread = std::thread([this] { Thread(); });
	return true;
}

void Core::Unload()
{
	if(!_loaded) {
		return;
	}
	_stop = true;
	if(_thread.joinable()) {
		_thread.join();
	}
	{
		std::lock_guard lock(_coreLock);
		WriteBatterySave();
		retro_unload_game();
	}
	_loaded = false;
	_marker = "no game";
	std::lock_guard lock(_frameLock);
	_frame.clear();
	_newFrame = false;
}

void Core::WriteBatterySave()
{
	void* sram = retro_get_memory_data(RETRO_MEMORY_SAVE_RAM);
	size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	if(!sram || !size || _game.empty()) {
		return;
	}
	// Games without a battery leave the save RAM filled with 0xFF (Genesis Plus GX's blank value) or zero
	const uint8_t* bytes = static_cast<const uint8_t*>(sram);
	bool used = false;
	for(size_t i = 0; i < size && !used; i++) {
		used = bytes[i] != 0xFF && bytes[i] != 0x00;
	}
	if(!used) {
		return;
	}
	std::error_code ec;
	fs::create_directories(_savesDir, ec);
	std::ofstream out(_savesDir / (_game.stem().wstring() + L".srm"), std::ios::binary | std::ios::trunc);
	out.write(static_cast<const char*>(sram), (std::streamsize)size);
}

void Core::Thread()
{
	uwp::InitThreadForWinRT();
	using clock = std::chrono::steady_clock;
	auto next = clock::now();
	auto lastBatterySave = clock::now();
	while(!_stop) {
		if(_paused) {
			_marker = "paused (menu open)";
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			next = clock::now();
			continue;
		}
		{
			std::lock_guard lock(_coreLock);
			_marker = "running a frame";
			retro_run();
			_frames++;
			if(clock::now() - lastBatterySave > std::chrono::seconds(30)) {
				WriteBatterySave();
				lastBatterySave = clock::now();
			}
		}
		_marker = "waiting for the next frame";
		// The sound sets the pace; the clock is the backup when there is no sound
		auto frame = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / _fps));
		next += frame;
		auto now = clock::now();
		if(_audio && _audio->Working()) {
			_audio->WaitForRoom(_stop);
			next = clock::now();
		} else if(next > now) {
			std::this_thread::sleep_until(next);
		} else if(now - next > std::chrono::milliseconds(100)) {
			next = now; // fell far behind: do not race to catch up
		}
	}
}

void Core::Reset()
{
	std::lock_guard lock(_coreLock);
	retro_reset();
}

fs::path Core::StatePath(int slot) const
{
	return _savesDir / (_game.stem().wstring() + L".state" + std::to_wstring(slot));
}

bool Core::SaveState(int slot)
{
	std::lock_guard lock(_coreLock);
	size_t size = retro_serialize_size();
	std::vector<uint8_t> data(size);
	if(!size || !retro_serialize(data.data(), size)) {
		return false;
	}
	std::error_code ec;
	fs::create_directories(_savesDir, ec);
	std::ofstream out(StatePath(slot), std::ios::binary | std::ios::trunc);
	out.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)size);
	return (bool)out;
}

bool Core::LoadState(int slot)
{
	std::ifstream in(StatePath(slot), std::ios::binary);
	if(!in) {
		return false;
	}
	std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
	std::lock_guard lock(_coreLock);
	return !data.empty() && retro_unserialize(data.data(), data.size());
}

void Core::SetCheats(const std::vector<std::string>& codes)
{
	std::lock_guard lock(_coreLock);
	retro_cheat_reset();
	unsigned index = 0;
	for(const std::string& code : codes) {
		retro_cheat_set(index++, true, code.c_str());
	}
}

bool Core::TakeFrame(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height)
{
	std::lock_guard lock(_frameLock);
	if(!_newFrame) {
		return false;
	}
	out = _frame;
	width = _width;
	height = _height;
	_newFrame = false;
	return true;
}

std::string Core::SystemName() const
{
	return "Genesis";
}
