#pragma once
// HD Eight: the game list, the pause menu and the main loop. Mesen runs the games on its own thread; this class owns
// the window and draws Mesen's finished frames plus the big-text menus.

#include "Library.h"
#include "Settings.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Emulator;
class XboxRenderer;
class XboxAudio;
class XboxInput;
class Covers;

class App
{
public:
	App();
	~App();

	int Run();

private:
	enum class Screen { Library, Playing };
	enum class MenuPage { Main, Picture, Cheats, AddCode, Sound, Controller };

	bool Init();
	void Shutdown();
	void SetUpProfile();
	void ReloadFont();
	void ApplyMesenSettings();

	void StartGame(const GameEntry& game);
	void LoadOnWorker(GameEntry game);
	void FinishLoad();
	void SetStep(std::string step);
	void ForwardMesenLog();
	void DrawInfo();
	void DrawLastSession();
	void QuitGame();
	void OpenMenu();
	void CloseMenu();
	void ApplyCheats();
	void Toast(std::string text, int seconds = 3);

	void HandleHotkeys();
	void UploadFrame();
	void DrawGame();
	void DrawLibrary();
	void DrawMenu();
	void DrawMenuMain();
	void DrawPictureOptions();
	void DrawSoundOptions();
	void DrawCheats();
	void DrawController();
	void DrawAddCode();
	void DrawToast();
	bool BackPressed() const;

	SDL_Window* _window = nullptr;
	SDL_Renderer* _sdlRenderer = nullptr;
	SDL_Texture* _gameTexture = nullptr;
	uint32_t _textureWidth = 0;
	uint32_t _textureHeight = 0;
	std::vector<uint32_t> _frameBuffer;
	bool _hasFrame = false;
	bool _running = true;
	bool _fontDirty = true;
	int _fontPixels = 0;

	std::unique_ptr<Emulator> _emu;
	std::unique_ptr<XboxRenderer> _video;
	std::unique_ptr<XboxAudio> _audio;
	std::unique_ptr<XboxInput> _input;
	std::unique_ptr<Covers> _covers;
	size_t _focusedGame = 0;

	std::filesystem::path _profile;
	std::filesystem::path _resFolder;
	AppSettings _settings;
	std::vector<GameEntry> _games;
	CheatDatabase _cheatDb;

	Screen _screen = Screen::Library;
	bool _menuOpen = false;
	MenuPage _page = MenuPage::Main;
	bool _focusNext = true;
	int _saveSlot = 1;
	int _capturing = -1;          // NES button waiting for an Xbox button (Controller page)
	bool _captureReleased = false;
	bool _ignoreUntilReleased = false;
	uint64_t _lastPadButtons = 0;

	GameEntry _currentGame;
	std::string _currentSha1;
	GameCheats _cheats;
	std::string _newCode;

	std::string _toast;
	std::chrono::steady_clock::time_point _toastUntil;

	// Loading runs on its own thread so the TV picture never freezes; the info panel shows each step
	std::thread _loadThread;
	std::atomic<bool> _loading = false;
	std::atomic<bool> _loadDone = false;
	bool _loadOk = false;
	std::string _loadError;
	GameEntry _loadGame;
	std::string _loadSha1;
	GameCheats _loadCheats;
	std::mutex _stepLock;
	std::string _step;
	std::chrono::steady_clock::time_point _stepTime;
	std::chrono::steady_clock::time_point _loadStart;
	bool _warnedSlowLoad = false;
	std::chrono::steady_clock::time_point _playStart;
	uint64_t _framesAtStart = 0;
	bool _warnedNoPicture = false;
	bool _showInfo = false;   // the info box still shows by itself while a game loads or has no picture
	size_t _mesenLogLines = 0;
	std::chrono::steady_clock::time_point _lastLogPoll;
	std::vector<std::string> _lastSession;   // the previous run's log, shown when it ended with a problem

	std::mutex _pickLock;
	std::string _pickedFolder;
	bool _pickDone = false;
};
