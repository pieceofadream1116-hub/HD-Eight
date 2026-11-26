#pragma once
// HD Eight: the game list, the pause menu and the main loop. Mesen runs the games on its own thread; this class owns
// the window and draws Mesen's finished frames plus the big-text menus.

#include "Library.h"
#include "Settings.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class Emulator;
class XboxRenderer;
class XboxAudio;
class XboxInput;

class App
{
public:
	App();
	~App();

	int Run();

private:
	enum class Screen { Library, Playing };
	enum class MenuPage { Main, Picture, Cheats, AddCode, Sound };

	bool Init();
	void Shutdown();
	void SetUpProfile();
	void ReloadFont();
	void ApplyMesenSettings();

	void StartGame(const GameEntry& game);
	void QuitGame();
	void OpenMenu();
	void CloseMenu();
	void ApplyCheats();
	void Toast(std::string text);

	void HandleHotkeys();
	void UploadFrame();
	void DrawGame();
	void DrawLibrary();
	void DrawMenu();
	void DrawMenuMain();
	void DrawPictureOptions();
	void DrawSoundOptions();
	void DrawCheats();
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
	uint64_t _lastPadButtons = 0;

	GameEntry _currentGame;
	std::string _currentSha1;
	GameCheats _cheats;
	std::string _newCode;

	std::string _toast;
	std::chrono::steady_clock::time_point _toastUntil;

	std::mutex _pickLock;
	std::string _pickedFolder;
	bool _pickDone = false;
};
