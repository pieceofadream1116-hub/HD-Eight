#pragma once
// Runs Genesis Plus GX (linked into the app as a libretro core) on a thread of its own.
//
// The emulation thread calls retro_run once per frame. The core hands back each finished picture (copied for the
// UI thread), its sound (sent straight to XAudio2, which also sets the pace: when enough sound is queued the thread
// waits, so games run at their real speed) and asks for the controllers (read from the snapshot XboxInput makes).
// Everything else (save states, cheats, reset) runs on the UI thread while holding the core lock, between frames.

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class XboxAudio;

/// One Genesis pad as the core sees it, as a bit per button.
namespace genesis {
enum Button : uint32_t
{
	Up = 0, Down, Left, Right, A, B, C, X, Y, Z, Start, Mode,
	ButtonCount
};
}

class Core
{
public:
	struct Options
	{
		bool noSpriteLimit = true;   // remove the 20 sprites per line limit (less flicker)
		bool tvLook = false;         // Blargg NTSC filter (composite TV signal)
		bool bestSound = true;       // Nuked YM2612 (accurate FM sound), costs more time
		std::string region = "auto"; // "auto", "ntsc-u", "pal", "ntsc-j"
	};

	/// Reads the pads for player 0-3: genesis::Button bits.
	using InputSource = std::function<uint32_t(int player)>;

	Core(XboxAudio* audio, InputSource input);
	~Core();

	/// Folders: system (Sega CD BIOS files) and saves (battery saves, save states).
	void SetFolders(std::filesystem::path system, std::filesystem::path saves);
	void SetOptions(const Options& options);

	/// Loads a game and starts the emulation thread. Returns false with a reason in Error().
	bool Load(const std::filesystem::path& game);
	/// Stops the thread, writes the battery save and unloads the game.
	void Unload();
	bool Loaded() const { return _loaded; }
	const std::string& Error() const { return _error; }

	void Pause(bool paused) { _paused = paused; }
	void Reset();

	bool SaveState(int slot);
	bool LoadState(int slot);

	/// Game Genie or Action Replay codes; each string may hold several codes separated by '+'.
	void SetCheats(const std::vector<std::string>& codes);

	/// The newest frame as 0xAARRGGBB pixels, if it changed since the last call (UI thread).
	bool TakeFrame(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height);
	/// The shape the core wants the picture shown at (4:3 for most games).
	double AspectRatio() const { return _aspect; }

	uint64_t FrameCount() const { return _frames; }
	const char* Marker() const { return _marker; }
	std::string SystemName() const;

	// Called by the libretro callbacks (emulation thread)
	bool Environment(unsigned cmd, void* data);
	void VideoRefresh(const void* data, unsigned width, unsigned height, size_t pitch);
	size_t AudioBatch(const int16_t* data, size_t frames);
	int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id);
	void InputPoll();

private:
	void Thread();
	void WriteBatterySave();
	std::filesystem::path StatePath(int slot) const;

	XboxAudio* _audio;
	InputSource _input;
	std::filesystem::path _systemDir, _savesDir, _game;
	std::string _systemDirU8, _savesDirU8;

	Options _options;
	std::map<std::string, std::string> _variables;
	std::atomic<bool> _variablesChanged = false;
	std::mutex _variablesLock;

	std::mutex _coreLock;       // held while retro_run runs, and by the UI thread for states, cheats and reset
	std::thread _thread;
	std::atomic<bool> _stop = false;
	std::atomic<bool> _paused = false;
	bool _loaded = false;
	bool _initialized = false;
	std::string _error;

	int _pixelFormat = 0;       // RETRO_PIXEL_FORMAT_*
	std::mutex _frameLock;
	std::vector<uint32_t> _frame;
	uint32_t _width = 0, _height = 0;
	bool _newFrame = false;
	std::atomic<double> _aspect = 4.0 / 3.0;
	double _fps = 60.0;
	double _sampleRate = 44100.0;

	std::array<uint32_t, 4> _padState{};
	std::atomic<uint64_t> _frames = 0;
	std::atomic<const char*> _marker = "no game";
};
