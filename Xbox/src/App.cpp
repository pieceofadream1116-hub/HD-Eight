#include "App.h"

#include "Covers.h"
#include "UwpPlatform.h"
#include "XboxAudio.h"
#include "XboxInput.h"
#include "XboxRenderer.h"

#include "Shared/CheatManager.h"
#include "Shared/EmuSettings.h"
#include "Shared/HdEightGuard.h"
#include "Shared/Emulator.h"
#include "Shared/KeyManager.h"
#include "Shared/MessageManager.h"
#include "Shared/Video/VideoRenderer.h"
#include "Shared/SaveStateManager.h"
#include "Shared/SettingTypes.h"
#include "Utilities/ArchiveReader.h"
#include "Utilities/FolderUtilities.h"
#include "Utilities/VirtualFile.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <iterator>
#include <cctype>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace {

// Set by the crash catcher (any thread), shown in a red box by the UI thread
std::mutex g_crashLock;
std::string g_crashText;
std::atomic<bool> g_crashed = false;

// The lookout: the screen loop stamps where it is; a separate thread notices when it stops moving
std::atomic<const char*> g_uiMarker = "starting up";
std::atomic<uint64_t> g_uiBeat = 0;
std::atomic<const char*> g_workerMarker = "idle";

void Mark(const char* where)
{
	g_uiMarker = where;
}

void OnCrash(const char* where, unsigned long code, unsigned long long offset, bool inImage)
{
	const char* what = code == 0xC0000005 ? "bad memory access" : code == 0xC00000FD ? "ran out of stack space" :
		code == 0xC0000094 ? "divided by zero" : code == 0xC0000374 ? "memory got corrupted" : "hard crash";
	char text[512];
	snprintf(text, sizeof(text), "HD Eight crashed while %s: %s (error 0x%08lX at %s+0x%llX)", where, what, code,
		inImage ? "HDEight.exe" : "address", offset);
	uwp::Log(std::string("[!] CRASH: ") + text);
	{
		std::lock_guard lock(g_crashLock);
		if(g_crashText.empty()) {
			g_crashText = text;
		}
	}
	g_crashed = true;
}

// Mesen's default NES palette (from its UI, UI/Config/NesConfig.cs); without it every game is drawn black
constexpr uint32_t kDefaultPalette[64] = {
	0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4, 0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00,
	0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08, 0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000,
	0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE, 0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00,
	0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32, 0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000,
	0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF, 0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22,
	0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082, 0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
	0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF, 0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
	0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC, 0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};

const char* XboxButtonName(int button)
{
	switch(button) {
		case pad::A: return "A";
		case pad::B: return "B";
		case pad::X: return "X";
		case pad::Y: return "Y";
		case pad::Back: return "View";
		case pad::Start: return "Menu";
		case pad::LeftStick: return "Left stick click";
		case pad::LeftShoulder: return "LB";
		case pad::RightShoulder: return "RB";
		case pad::LeftTrigger: return "LT";
		case pad::RightTrigger: return "RT";
		default: return "?";
	}
}

// Buttons a NES button can be moved to (the right stick click opens the menu, the D-pad moves)
constexpr pad::Button kAssignable[] = {pad::A, pad::B, pad::X, pad::Y, pad::LeftShoulder, pad::RightShoulder,
	pad::LeftTrigger, pad::RightTrigger, pad::Back, pad::Start, pad::LeftStick};

// The 16 letters Game Genie codes are made of
constexpr const char* kGenieLetters = "APZLGITYEOXUKSVN";

std::string U8(const fs::path& path)
{
	auto s = path.u8string();
	return std::string(s.begin(), s.end());
}

fs::path FromU8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string Upper(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	return s;
}

VideoFilterType FilterFor(PictureStyle style)
{
	switch(style) {
		case PictureStyle::HdUpscale: return VideoFilterType::xBRZ4x;
		case PictureStyle::MaxHd: return VideoFilterType::xBRZ6x;
		case PictureStyle::Hq4x: return VideoFilterType::HQ4x;
		case PictureStyle::TvLook: return VideoFilterType::NtscBlargg;
		case PictureStyle::SuperEagle: return VideoFilterType::SuperEagle;
		default: return VideoFilterType::None;
	}
}

// Xbox A is the NES B button and Xbox B the NES A button, like the real pad's layout (B left of A)
// The player's button choices (Controller page); directions are always the D-pad (and the left stick)
// With turbo switched on for A or B, that button itself fires rapidly (the separate turbo buttons are in TurboMapping)
KeyMapping PadMapping(int player, bool useStick, const AppSettings& s)
{
	const int* buttons = s.buttons;
	KeyMapping m{};
	(s.turboA ? m.TurboA : m.A) = pad::Code(player, (pad::Button)buttons[0]);
	(s.turboB ? m.TurboB : m.B) = pad::Code(player, (pad::Button)buttons[1]);
	m.Select = pad::Code(player, (pad::Button)buttons[4]);
	m.Start = pad::Code(player, (pad::Button)buttons[5]);
	if(useStick) {
		m.Up = pad::Code(player, pad::StickUp);
		m.Down = pad::Code(player, pad::StickDown);
		m.Left = pad::Code(player, pad::StickLeft);
		m.Right = pad::Code(player, pad::StickRight);
	} else {
		m.Up = pad::Code(player, pad::Up);
		m.Down = pad::Code(player, pad::Down);
		m.Left = pad::Code(player, pad::Left);
		m.Right = pad::Code(player, pad::Right);
	}
	return m;
}

// The separate Turbo A / Turbo B buttons, in a mapping of their own so they work alongside turbo switched on for A/B
KeyMapping TurboMapping(int player, const AppSettings& s)
{
	KeyMapping m{};
	m.TurboA = pad::Code(player, (pad::Button)s.buttons[2]);
	m.TurboB = pad::Code(player, (pad::Button)s.buttons[3]);
	return m;
}

void CopyIfChanged(const fs::path& from, const fs::path& to)
{
	std::error_code ec;
	if(!fs::exists(from, ec)) {
		return;
	}
	if(fs::exists(to, ec) && fs::file_size(to, ec) == fs::file_size(from, ec)) {
		return;
	}
	fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
}

// A full-width button: easy to see and to land on with the D-pad
bool BigButton(const char* label)
{
	return ImGui::Button(label, ImVec2(-FLT_MIN, 0));
}

// "<  value  >" row for numbers, driven with the D-pad (left/right buttons)
bool Stepper(const char* label, int& value, int minValue, int maxValue, int step, const char* suffix = "")
{
	bool changed = false;
	ImGui::PushID(label);
	ImGui::TextUnformatted(label);
	ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.45f);
	if(ImGui::Button(" - ")) {
		value = std::max(minValue, value - step);
		changed = true;
	}
	ImGui::SameLine();
	ImGui::Text("%d%s", value, suffix);
	ImGui::SameLine();
	if(ImGui::Button(" + ")) {
		value = std::min(maxValue, value + step);
		changed = true;
	}
	ImGui::PopID();
	return changed;
}

} // namespace

App::App() = default;

App::~App()
{
	Shutdown();
}

void App::SetUpProfile()
{
	_profile = uwp::FindExternalProfilePath();
	if(_profile.empty()) {
		_profile = uwp::LocalStatePath() / uwp::kProfileFolderName;
	}
	std::error_code ec;
	fs::create_directories(_profile / "Games", ec);
	fs::create_directories(_profile / "Cheats", ec);

	// Keep the last run's log if it ended with a problem (crash, freeze, game that would not start) to show on the TV
	std::vector<std::string> previous = uwp::ReadLogFile(_profile);
	bool problem = std::any_of(previous.begin(), previous.end(), [](const std::string& line) { return line.find("[!]") != std::string::npos; });
	if(problem) {
		size_t start = previous.size() > 14 ? previous.size() - 14 : 0;
		_lastSession.assign(previous.begin() + start, previous.end());
	}
	uwp::OpenLog(_profile);
	uwp::Log("HD Eight " HDEIGHT_VERSION " starting, profile: " + U8(_profile));

	const char* base = SDL_GetBasePath();
	_resFolder = base ? FromU8(base) / "res" : fs::path("res");

	_settings.Load(_profile / "hd-eight.ini");
	CopyIfChanged(_resFolder / "MesenNesDB.txt", _profile / "MesenNesDB.txt");
	_cheatDb.LoadJson(_resFolder / "CheatDb.Nes.json");
	uwp::Log("Game Genie database: " + std::to_string(_cheatDb.GameCount()) + " games");
}

bool App::Init()
{
	uwp::InstallCrashLogging();
	hdeight::SetCrashReporter(OnCrash);
	std::thread([] {
		uint64_t lastBeat = g_uiBeat;
		auto lastChange = std::chrono::steady_clock::now();
		int reports = 0;
		while(true) {
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
			uint64_t beat = g_uiBeat;
			auto now = std::chrono::steady_clock::now();
			if(beat != lastBeat) {
				lastBeat = beat;
				lastChange = now;
				reports = 0;
				continue;
			}
			double stuck = std::chrono::duration<double>(now - lastChange).count();
			if((reports == 0 && stuck > 3) || (reports == 1 && stuck > 15)) {
				reports++;
				uwp::LogUrgent(std::string("[!] The screen has been stuck for ") + std::to_string((int)stuck) +
					" seconds while " + g_uiMarker.load() + ". Game loader: " + g_workerMarker.load() +
					". Mesen: " + hdeight::emuMarker.load() + ". Video filter: " + hdeight::filterMarker.load() +
					". Frames run: " + std::to_string(hdeight::emuFrames.load()));
			}
		}
	}).detach();
	SetUpProfile();

	// Keep the Xbox B button from sending the app to the background, and turn on the Xbox controller driver
	SDL_SetHint(SDL_HINT_WINRT_HANDLE_BACK_BUTTON, "1");
	SDL_SetHint(SDL_HINT_JOYSTICK_WGI, "1");

	if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD | SDL_INIT_EVENTS)) {
		uwp::Log(std::string("SDL_Init failed: ") + SDL_GetError());
		return false;
	}
	uwp::UseFullCoreWindowBounds();

	_window = SDL_CreateWindow("HD Eight", 1920, 1080, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
	if(!_window) {
		uwp::Log(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
		return false;
	}
	_sdlRenderer = SDL_CreateRenderer(_window, nullptr);
	if(!_sdlRenderer) {
		uwp::Log(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
		return false;
	}
	SDL_SetRenderVSync(_sdlRenderer, 1);
	uwp::Log(std::string("Renderer: ") + SDL_GetRendererName(_sdlRenderer));

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.IniFilename = nullptr;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NoMouseCursorChange;
	io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();
	style.WindowRounding = 0;
	style.FrameRounding = 10;
	style.FramePadding = ImVec2(24, 14);
	style.ItemSpacing = ImVec2(18, 16);
	style.WindowPadding = ImVec2(48, 36);
	style.ScrollbarSize = 28;
	style.Colors[ImGuiCol_WindowBg] = ImVec4(0.04f, 0.07f, 0.16f, 0.96f);
	style.Colors[ImGuiCol_Button] = ImVec4(0.12f, 0.20f, 0.42f, 1.0f);
	style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.80f, 0.16f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.95f, 0.70f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_Header] = ImVec4(0.12f, 0.20f, 0.42f, 1.0f);
	style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.80f, 0.16f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.95f, 0.70f, 0.15f, 1.0f);
	style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.20f, 0.42f, 1.0f);
	style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.80f, 0.16f, 0.18f, 1.0f);
	style.Colors[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);
	style.Colors[ImGuiCol_NavCursor] = ImVec4(1.0f, 0.85f, 0.25f, 1.0f);

	ImGui_ImplSDL3_InitForSDLRenderer(_window, _sdlRenderer);
	ImGui_ImplSDLRenderer3_Init(_sdlRenderer);

	// Mesen
	FolderUtilities::SetHomeFolder(U8(_profile));
	FolderUtilities::AddKnownGameFolder(U8(_profile / "Games"));
	if(!_settings.gamesFolder.empty()) {
		FolderUtilities::AddKnownGameFolder(_settings.gamesFolder);
	}
	_emu = std::make_unique<Emulator>();
	_emu->Initialize(false);
	KeyManager::SetSettings(_emu->GetSettings());
	_video = std::make_unique<XboxRenderer>(_emu.get());
	_audio = std::make_unique<XboxAudio>(_emu.get());
	_audio->OpenDevice();

	_input = std::make_unique<XboxInput>(_emu.get());
	{
		int w = 1920, h = 1080;
		SDL_GetCurrentRenderOutputSize(_sdlRenderer, &w, &h);
		_emu->GetVideoRenderer()->SetRendererSize((uint32_t)w, (uint32_t)h);
	}
	ApplyMesenSettings();
	uwp::Log("Mesen is ready");

	// The profile's Games folder, your ES-DE folders, then the folder picked with "Choose games folder"
	std::vector<fs::path> folders{_profile / "Games"};
	for(const fs::path& folder : uwp::EsDeFolders({"nes", "famicom", "fds"})) {
		folders.push_back(folder);
	}
	if(!_settings.gamesFolder.empty()) {
		folders.push_back(FromU8(_settings.gamesFolder));
	}
	_games = ScanGames(folders);
	uwp::Log("Games found: " + std::to_string(_games.size()));
	_covers = std::make_unique<Covers>(_profile / "Covers");
	LoadBackground();
	return true;
}

void App::ApplyMesenSettings()
{
	EmuSettings* s = _emu->GetSettings();

	VideoConfig video = s->GetVideoConfig();
	video.VideoFilter = FilterFor(_settings.picture);
	video.ScanlineIntensity = _settings.scanlines / 100.0;
	video.AspectRatio = VideoAspectRatio::NoStretching;
	video.UseBilinearInterpolation = false;
	s->SetVideoConfig(video);

	AudioConfig audio = s->GetAudioConfig();
	audio.EnableAudio = true;
	audio.MasterVolume = (uint32_t)_settings.volume;
	audio.SampleRate = 48000;
	audio.AudioLatency = 60;
	s->SetAudioConfig(audio);

	EmulationConfig emulation = s->GetEmulationConfig();
	emulation.EmulationSpeed = 100;
	s->SetEmulationConfig(emulation);

	PreferencesConfig prefs = s->GetPreferences();
	prefs.DisableOsd = true;  // HD Eight shows its own messages
	prefs.DisableGameSelectionScreen = true;
	s->SetPreferences(prefs);

	NesConfig nes = s->GetNesConfig();
	nes.Port1.Type = ControllerType::NesController;
	nes.Port1.Keys.Mapping1 = PadMapping(0, false, _settings);
	nes.Port1.Keys.Mapping2 = PadMapping(0, true, _settings);
	nes.Port1.Keys.Mapping3 = TurboMapping(0, _settings);
	nes.Port1.Keys.TurboSpeed = 2; // Mesen's usual turbo speed
	nes.Port2.Type = ControllerType::NesController;
	nes.Port2.Keys.Mapping1 = PadMapping(1, false, _settings);
	nes.Port2.Keys.Mapping2 = PadMapping(1, true, _settings);
	nes.Port2.Keys.Mapping3 = TurboMapping(1, _settings);
	nes.Port2.Keys.TurboSpeed = 2;
	nes.AutoConfigureInput = true;
	nes.EnableHdPacks = _settings.hdPacks;
	nes.RemoveSpriteLimit = _settings.noFlicker;
	nes.AdaptiveSpriteLimit = _settings.noFlicker;
	nes.IsFullColorPalette = false;
	std::memset(nes.UserPalette, 0, sizeof(nes.UserPalette));
	std::memcpy(nes.UserPalette, kDefaultPalette, sizeof(kDefaultPalette));
	for(uint32_t& volume : nes.ChannelVolumes) {
		volume = 100;
	}
	for(uint32_t& panning : nes.ChannelPanning) {
		panning = 100;
	}
	s->SetNesConfig(nes);
}

void App::ReloadFont()
{
	int w = 1920, h = 1080;
	SDL_GetCurrentRenderOutputSize(_sdlRenderer, &w, &h);
	// About 50 pixels tall on a 1080p TV at 100%
	int pixels = std::max(20, h * _settings.uiScale / 2200);
	if(pixels == _fontPixels) {
		_fontDirty = false;
		return;
	}
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Clear();
	ImFont* font = io.Fonts->AddFontFromFileTTF(U8(_resFolder / "SplineSans-Bold.ttf").c_str(), (float)pixels);
	if(!font) {
		ImFontConfig config;
		config.SizePixels = (float)pixels;
		io.Fonts->AddFontDefault(&config);
	}
	ImGui_ImplSDLRenderer3_DestroyDeviceObjects();
	_fontPixels = pixels;
	_fontDirty = false;
}

int App::Run()
{
	if(!Init()) {
		return 1;
	}

	while(_running) {
		g_uiBeat++;
		Mark("reading Xbox events");
		SDL_Event event;
		while(SDL_PollEvent(&event)) {
			ImGui_ImplSDL3_ProcessEvent(&event);
			switch(event.type) {
				case SDL_EVENT_QUIT:
				case SDL_EVENT_TERMINATING:
					_running = false;
					break;
				case SDL_EVENT_WILL_ENTER_BACKGROUND:
					_settings.Save();
					if(_screen == Screen::Playing && !_menuOpen) {
						OpenMenu();
					}
					break;
				default:
					break;
			}
		}

		Mark("checking the folder picker");
		{
			std::lock_guard lock(_pickLock);
			if(_pickDone) {
				_pickDone = false;
				if(!_pickedFolder.empty()) {
					_settings.gamesFolder = _pickedFolder;
					_settings.Save();
					FolderUtilities::AddKnownGameFolder(_pickedFolder);
					std::vector<fs::path> folders{_profile / "Games"};
					for(const fs::path& folder : uwp::EsDeFolders({"nes", "famicom", "fds"})) {
						folders.push_back(folder);
					}
					folders.push_back(FromU8(_pickedFolder));
					_games = ScanGames(folders);
					Toast("Found " + std::to_string(_games.size()) + " games");
				}
			}
			if(_pictureDone) {
				_pictureDone = false;
				if(!_pickedPicture.empty()) {
					UseBackgroundPicture(FromU8(_pickedPicture));
				}
			}
		}

		Mark("reading the controllers");
		_input->Poll();
		if(_loading && _loadDone) {
			Mark("finishing the game load");
			FinishLoad();
		}
		if(!_loading) {
			Mark("checking the menu buttons");
			HandleHotkeys();
		}
		Mark("copying the game picture");
		UploadFrame();
		Mark("reading Mesen's messages");
		ForwardMesenLog();

		if(_fontDirty) {
			ReloadFont();
		}
		Mark("starting the menus");
		ImGui_ImplSDLRenderer3_NewFrame();
		ImGui_ImplSDL3_NewFrame();
		ImGui::NewFrame();

		Mark("drawing the game list or menu");
		if(_screen == Screen::Library && !_loading) {
			DrawLibrary();
		} else if(_screen == Screen::Playing && _menuOpen) {
			DrawMenu();
		}
		Mark("drawing the info box");
		DrawInfo();
		DrawToast();

		Mark("drawing the screen");
		ImGui::Render();
		SDL_SetRenderDrawColor(_sdlRenderer, 0, 0, 0, 255);
		SDL_RenderClear(_sdlRenderer);
		if(_screen == Screen::Playing) {
			DrawGame();
		}
		ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), _sdlRenderer);
		Mark("showing the frame on the TV");
		SDL_RenderPresent(_sdlRenderer);
	}

	Shutdown();
	return 0;
}

void App::Shutdown()
{
	if(_loadThread.joinable()) {
		if(_loadDone) {
			_loadThread.join();
		} else {
			uwp::Log("[!] Closing while a game is still loading (step: " + _step + ")");
			_loadThread.detach();
			return; // Mesen is stuck inside the load: leave it, the app is closing anyway
		}
	}
	if(_emu) {
		uwp::Log("HD Eight closed normally");
		_settings.Save();
		_emu->Stop(false);
		_input.reset();
		_audio.reset();
		_video.reset();
		_emu->Release();
		_emu.reset();
	}
	if(ImGui::GetCurrentContext()) {
		ImGui_ImplSDLRenderer3_Shutdown();
		ImGui_ImplSDL3_Shutdown();
		ImGui::DestroyContext();
	}
	_covers.reset();
	if(_bgThread.joinable()) {
		_bgThread.join();
	}
	FreeBackground();
	if(_gameTexture) {
		SDL_DestroyTexture(_gameTexture);
		_gameTexture = nullptr;
	}
	if(_sdlRenderer) {
		SDL_DestroyRenderer(_sdlRenderer);
		_sdlRenderer = nullptr;
	}
	if(_window) {
		SDL_DestroyWindow(_window);
		_window = nullptr;
		SDL_Quit();
	}
}

void App::HandleHotkeys()
{
	uint64_t buttons = _input->AnyPadButtons();
	uint64_t pressed = buttons & ~_lastPadButtons;
	_lastPadButtons = buttons;

	auto held = [&](pad::Button b) { return (buttons >> b) & 1; };
	auto down = [&](pad::Button b) { return (pressed >> b) & 1; };

	if(_screen == Screen::Playing && !_menuOpen) {
		// Menu: click the right stick, or press View and Menu together
		bool combo = held(pad::Back) && held(pad::Start) && (down(pad::Back) || down(pad::Start));
		if(down(pad::RightStick) || combo) {
			OpenMenu();
		}
	} else if(_screen == Screen::Playing && _menuOpen && _page == MenuPage::Main && down(pad::RightStick)) {
		CloseMenu();
	}
}

void App::UploadFrame()
{
	uint32_t w = 0, h = 0;
	if(!_video || !_video->TakeFrame(_frameBuffer, w, h)) {
		return;
	}
	if(!_gameTexture || w != _textureWidth || h != _textureHeight) {
		if(_gameTexture) {
			SDL_DestroyTexture(_gameTexture);
		}
		_gameTexture = SDL_CreateTexture(_sdlRenderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, (int)w, (int)h);
		_textureWidth = w;
		_textureHeight = h;
	}
	if(_gameTexture) {
		SDL_UpdateTexture(_gameTexture, nullptr, _frameBuffer.data(), (int)(w * sizeof(uint32_t)));
		_hasFrame = true;
	}
}

void App::DrawGame()
{
	if(!_gameTexture || !_hasFrame) {
		return;
	}
	SDL_SetTextureScaleMode(_gameTexture, _settings.picture == PictureStyle::SharpPixels ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR);

	int outW = 0, outH = 0;
	SDL_GetCurrentRenderOutputSize(_sdlRenderer, &outW, &outH);
	SDL_FRect dst{0, 0, (float)outW, (float)outH};
	if(_settings.shape != ScreenShape::FillScreen) {
		double aspect = _settings.shape == ScreenShape::OriginalTv ? 4.0 / 3.0 : 256.0 / 240.0;
		float w = (float)outW;
		float h = (float)(outW / aspect);
		if(h > outH) {
			h = (float)outH;
			w = (float)(outH * aspect);
		}
		if(_settings.shape == ScreenShape::PixelPerfect) {
			// Whole multiples of the NES picture, so every pixel is the same size
			int scale = std::max(1, (int)(outH / 240));
			h = (float)(240 * scale);
			w = (float)(256 * scale);
		}
		dst = SDL_FRect{(outW - w) / 2, (outH - h) / 2, w, h};
	}
	SDL_RenderTexture(_sdlRenderer, _gameTexture, nullptr, &dst);
}

void App::SetStep(std::string step)
{
	uwp::Log("Step: " + step);
	std::lock_guard lock(_stepLock);
	_step = std::move(step);
	_stepTime = std::chrono::steady_clock::now();
}

void App::StartGame(const GameEntry& game)
{
	if(_loading) {
		return;
	}
	if(_loadThread.joinable()) {
		_loadThread.join();
	}
	ApplyMesenSettings();
	_loadGame = game;
	_loadOk = false;
	_loadError.clear();
	_loadDone = false;
	_warnedSlowLoad = false;
	_loadStart = std::chrono::steady_clock::now();
	_loading = true;
	SetStep("Getting ready to start " + game.title);
	_loadThread = std::thread([this, game] { LoadOnWorker(game); });
}

// Runs on the load thread. Only touches Mesen and the step text; the UI thread waits for _loadDone.
void App::LoadOnWorker(GameEntry game)
{
	try {
		std::string path = U8(game.path);
		SetStep("Opening the game file: " + path);
		VirtualFile rom(path);
		if(rom.IsArchive()) {
			SetStep("Looking inside the zip file");
			std::unique_ptr<ArchiveReader> reader = ArchiveReader::GetReader(path);
			std::vector<std::string> files = reader ? reader->GetFileList(VirtualFile::RomExtensions) : std::vector<std::string>{};
			if(files.empty()) {
				_loadError = "There is no NES game inside " + game.title;
				_loadDone = true;
				return;
			}
			rom = VirtualFile(path, files.front());
		}
		SetStep("Reading the game file");
		if(!rom.IsValid()) {
			_loadError = "HD Eight could not read the file. It may be on a drive the app is not allowed to open.";
			_loadDone = true;
			return;
		}
		SetStep("Mesen is starting the game");
		g_workerMarker = "inside Mesen's LoadRom";
		bool ok = false;
		if(!hdeight::Guard("starting the game", [&] { ok = _emu->LoadRom(rom, VirtualFile()); })) {
			_loadError = "Mesen crashed while starting the game.";
			_loadDone = true;
			return;
		}
		g_workerMarker = "LoadRom finished";
		_loadOk = ok;
		if(ok) {
			// Done here, not on the screen thread: turning cheats on waits for Mesen to finish a frame
			SetStep("Turning on Game Genie codes");
			g_workerMarker = "turning on cheats (waits for Mesen to finish a frame)";
			_loadSha1 = Upper(_emu->GetHash(HashType::Sha1Cheat));
			_loadCheats = GameCheats{};
			_loadCheats.Load(_profile / "Cheats" / (_loadSha1 + ".txt"));
			_cheats = _loadCheats;
			ApplyCheats();
			g_workerMarker = "done";
		}
		if(!_loadOk) {
			_loadError = "Mesen could not start this game (it may not be an NES game, or the file is damaged).";
		}
	} catch(const std::exception& e) {
		_loadError = std::string("Error while starting the game: ") + e.what();
	} catch(...) {
		_loadError = "Unknown error while starting the game";
	}
	_loadDone = true;
}

void App::FinishLoad()
{
	if(_loadThread.joinable()) {
		_loadThread.join();
	}
	_loading = false;
	if(!_loadOk) {
		uwp::Log("[!] Load failed: " + _loadError);
		SetStep("Could not start " + _loadGame.title);
		Toast(_loadError, 10);
		_focusNext = true;
		return;
	}
	SetStep("Game started, waiting for the first picture");
	_currentGame = _loadGame;
	_currentSha1 = _loadSha1;
	uwp::Log("Game SHA-1: " + _currentSha1);
	_cheats = _loadCheats;
	_hasFrame = false;
	_screen = Screen::Playing;
	_menuOpen = false;
	_playStart = std::chrono::steady_clock::now();
	_framesAtStart = _video->FrameCount();
	_warnedNoPicture = false;
	Toast("Click the right stick for the menu");
}

void App::ForwardMesenLog()
{
	auto now = std::chrono::steady_clock::now();
	if(now - _lastLogPoll < std::chrono::milliseconds(500)) {
		return;
	}
	_lastLogPoll = now;

	// Mesen's own messages ("could not load file", game database...) go into its log list; copy the new ones into ours
	std::string text = MessageManager::GetLog();
	std::vector<std::string> lines;
	size_t pos = 0;
	while(pos < text.size()) {
		size_t end = text.find('\n', pos);
		if(end == std::string::npos) {
			end = text.size();
		}
		lines.push_back(text.substr(pos, end - pos));
		pos = end + 1;
	}
	if(lines.size() < _mesenLogLines) {
		_mesenLogLines = 0; // Mesen trimmed its list
	}
	for(size_t i = _mesenLogLines; i < lines.size(); i++) {
		uwp::Log("Mesen: " + lines[i]);
	}
	_mesenLogLines = lines.size();

	if(_loading && !_warnedSlowLoad && now - _loadStart > std::chrono::seconds(10)) {
		_warnedSlowLoad = true;
		std::lock_guard lock(_stepLock);
		uwp::Log("[!] Still loading after 10 seconds, stuck at: " + _step + ". Loader: " + g_workerMarker.load() +
			". Mesen: " + hdeight::emuMarker.load() + ". Video filter: " + hdeight::filterMarker.load() +
			". Frames run: " + std::to_string(hdeight::emuFrames.load()) + ". Sounds: " + std::to_string(_audio->BufferCount()));
	}
	if(_screen == Screen::Playing && !_menuOpen && !_warnedNoPicture && now - _playStart > std::chrono::seconds(5) &&
		_video->FrameCount() == _framesAtStart) {
		_warnedNoPicture = true;
		uwp::Log(std::string("[!] The game is running but no picture arrived after 5 seconds. Mesen: ") +
			hdeight::emuMarker.load() + ". Video filter: " + hdeight::filterMarker.load() + ". Frames run: " +
			std::to_string(hdeight::emuFrames.load()) + ". Sounds: " + std::to_string(_audio->BufferCount()));
	}
	if(_screen == Screen::Playing && _hasFrame) {
		std::lock_guard lock(_stepLock);
		if(_step.rfind("Game started", 0) == 0) {
			_step = "Playing";
		}
	}
}

void App::QuitGame()
{
	_emu->Stop(false);
	_screen = Screen::Library;
	_menuOpen = false;
	_page = MenuPage::Main;
	_focusNext = true;
	_hasFrame = false;
}

void App::OpenMenu()
{
	_emu->Pause();
	_menuOpen = true;
	_page = MenuPage::Main;
	_focusNext = true;
}

void App::CloseMenu()
{
	_menuOpen = false;
	_emu->Resume();
}

void App::ApplyCheats()
{
	std::vector<CheatCode> codes;
	auto add = [&](const std::string& list) {
		size_t start = 0;
		while(start <= list.size()) {
			size_t end = list.find_first_of(";+ ", start);
			std::string one = Upper(list.substr(start, end == std::string::npos ? std::string::npos : end - start));
			if(!one.empty() && one.size() < sizeof(CheatCode::Code)) {
				CheatCode code{};
				code.Type = CheatType::NesGameGenie;
				std::strncpy(code.Code, one.c_str(), sizeof(code.Code) - 1);
				codes.push_back(code);
			}
			if(end == std::string::npos) {
				break;
			}
			start = end + 1;
		}
	};
	for(const std::string& enabled : _cheats.enabledCodes) {
		add(enabled);
	}
	if(codes.empty()) {
		_emu->GetCheatManager()->ClearCheats(false);
	} else {
		_emu->GetCheatManager()->SetCheats(codes);
	}
}

void App::Toast(std::string text, int seconds)
{
	_toast = std::move(text);
	_toastUntil = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
}

bool App::BackPressed() const
{
	return ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false);
}

// ------------------------------------------------------------------------------------------------------------------
// Screens

static void BeginFullScreen(const char* name, bool& focusNext, float widthFraction = 1.0f)
{
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImVec2 size(viewport->Size.x * widthFraction, viewport->Size.y);
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + (viewport->Size.x - size.x) / 2, viewport->Pos.y));
	ImGui::SetNextWindowSize(size);
	if(focusNext) {
		ImGui::SetNextWindowFocus();
	}
	ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
	if(focusNext) {
		ImGui::SetKeyboardFocusHere();
		focusNext = false;
	}
}

static void Title(const char* text)
{
	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.25f, 1.0f));
	ImGui::SetWindowFontScale(1.4f);
	ImGui::TextUnformatted(text);
	ImGui::SetWindowFontScale(1.0f);
	ImGui::PopStyleColor();
	ImGui::Separator();
}

// The moving backdrop behind the game list: a deep blue gradient with soft lights drifting slowly across it and a
// faint band of light sweeping by now and then (the same look as EMU's game list)
static void DrawMovingBackground()
{
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 size = ImGui::GetMainViewport()->Size;
	const float t = (float)ImGui::GetTime();
	dl->AddRectFilledMultiColor(ImVec2(0, 0), size, IM_COL32(12, 30, 78, 255), IM_COL32(8, 22, 62, 255),
		IM_COL32(2, 7, 24, 255), IM_COL32(4, 12, 36, 255));

	struct Light
	{
		float periodX, periodY, phase, radius;
		int r, g, b;
	};
	static const Light lights[] = {
		{47.0f, 31.0f, 0.0f, 0.55f, 40, 110, 230},
		{61.0f, 43.0f, 2.1f, 0.45f, 20, 70, 200},
		{37.0f, 53.0f, 4.2f, 0.38f, 70, 140, 255},
	};
	constexpr float kTau = 6.2831853f;
	const float extent = std::max(size.x, size.y);
	for(const Light& light : lights) {
		ImVec2 centre(size.x * (0.5f + 0.42f * std::sin(t * kTau / light.periodX + light.phase)),
			size.y * (0.5f + 0.38f * std::cos(t * kTau / light.periodY + light.phase * 1.3f)));
		const int rings = 14;
		for(int i = rings; i >= 1; i--) {
			dl->AddCircleFilled(centre, extent * light.radius * i / rings, IM_COL32(light.r, light.g, light.b, 6), 48);
		}
	}

	const float sweep = std::fmod(t / 14.0f, 1.0f);
	const float x = -size.x * 0.4f + sweep * size.x * 1.8f;
	const float width = size.x * 0.18f;
	const ImU32 clear = IM_COL32(120, 170, 255, 0);
	const ImU32 glow = IM_COL32(120, 170, 255, 13);
	dl->AddRectFilledMultiColor(ImVec2(x, 0), ImVec2(x + width, size.y), clear, glow, glow, clear);
	dl->AddRectFilledMultiColor(ImVec2(x + width, 0), ImVec2(x + width * 2, size.y), glow, clear, clear, glow);
}

// The player's own picture: copied into the profile folder (so it stays even if the original is moved), then decoded
// on a thread of its own
void App::UseBackgroundPicture(const fs::path& picked)
{
	std::error_code ec;
	std::string ext = picked.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
	for(const char* old : {".jpg", ".jpeg", ".png", ".bmp"}) {
		fs::remove(_profile / (std::string("Background") + old), ec);
	}
	fs::path copy = _profile / ("Background" + ext);
	if(!fs::copy_file(picked, copy, fs::copy_options::overwrite_existing, ec)) {
		uwp::Log("Could not copy the background picture " + U8(picked) + ": " + ec.message());
		copy = picked; // use it where it is
	}
	_settings.backgroundPicture = U8(copy);
	_settings.Save();
	LoadBackground();
	Toast("Your background picture is on");
}

void App::LoadBackground()
{
	FreeBackground();
	if(_settings.backgroundPicture.empty()) {
		return;
	}
	if(_bgThread.joinable()) {
		_bgThread.join();
	}
	fs::path file = FromU8(_settings.backgroundPicture);
	_bgThread = std::thread([this, file] {
		uwp::InitThreadForWinRT();
		std::ifstream in(file, std::ios::binary);
		std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		std::vector<uint8_t> rgba;
		int w = 0, h = 0;
		bool ok = !bytes.empty() && DecodeImage(bytes, rgba, w, h, 2560);
		if(!ok) {
			uwp::Log("Could not read the background picture " + U8(file));
		}
		std::lock_guard lock(_bgLock);
		_bgPixels = ok ? std::move(rgba) : std::vector<uint8_t>{};
		_bgPixW = w;
		_bgPixH = h;
		_bgReady = ok;
	});
}

void App::FreeBackground()
{
	if(_bgTexture) {
		SDL_DestroyTexture(_bgTexture);
		_bgTexture = nullptr;
	}
	std::lock_guard lock(_bgLock);
	_bgReady = false;
	_bgPixels.clear();
}

// Behind the game list: the player's own picture (filling the screen, darkened a little so the text stays easy to
// read), or the moving blue backdrop
void App::DrawBackground()
{
	{
		std::lock_guard lock(_bgLock);
		if(_bgReady) {
			_bgReady = false;
			if(_bgTexture) {
				SDL_DestroyTexture(_bgTexture);
			}
			_bgTexture = SDL_CreateTexture(_sdlRenderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, _bgPixW, _bgPixH);
			if(_bgTexture) {
				SDL_UpdateTexture(_bgTexture, nullptr, _bgPixels.data(), _bgPixW * 4);
				SDL_SetTextureScaleMode(_bgTexture, SDL_SCALEMODE_LINEAR);
				_bgTexW = _bgPixW;
				_bgTexH = _bgPixH;
			}
			_bgPixels.clear();
		}
	}
	if(!_bgTexture || _bgTexW <= 0 || _bgTexH <= 0) {
		DrawMovingBackground();
		return;
	}
	ImDrawList* dl = ImGui::GetBackgroundDrawList();
	const ImVec2 size = ImGui::GetMainViewport()->Size;
	// Fill the screen, keeping the picture's shape (the edges that do not fit are cut off)
	const float scale = std::max(size.x / _bgTexW, size.y / _bgTexH);
	const float w = _bgTexW * scale, h = _bgTexH * scale;
	const ImVec2 min((size.x - w) * 0.5f, (size.y - h) * 0.5f);
	dl->AddImage((ImTextureID)(intptr_t)_bgTexture, min, ImVec2(min.x + w, min.y + h));
	dl->AddRectFilled(ImVec2(0, 0), size, IM_COL32(0, 0, 0, 120));
}

void App::DrawLibrary()
{
	DrawBackground();
	ImGui::SetNextWindowBgAlpha(0.0f); // let the backdrop show through
	BeginFullScreen("Library", _focusNext);
	Title("HD EIGHT   (version " HDEIGHT_VERSION ")");
	DrawLastSession();

	if(BigButton("Choose games folder")) {
		uwp::PickFolder([this](std::string folder) {
			std::lock_guard lock(_pickLock);
			_pickedFolder = std::move(folder);
			_pickDone = true;
		});
	}
	if(ImGui::CollapsingHeader("Picture and sound")) {
		DrawPictureOptions();
		DrawSoundOptions();
	}
	if(ImGui::CollapsingHeader("Background")) {
		if(BigButton("Choose my own background picture")) {
			uwp::PickPicture([this](std::string file) {
				std::lock_guard lock(_pickLock);
				_pickedPicture = std::move(file);
				_pictureDone = true;
			});
		}
		ImGui::TextDisabled("A .jpg or .png picture from your drive");
		if(!_settings.backgroundPicture.empty() && BigButton("Use the moving blue background")) {
			_settings.backgroundPicture.clear();
			_settings.Save();
			FreeBackground();
			Toast("Moving blue background is back");
		}
	}

	if(_games.empty()) {
		ImGui::Spacing();
		ImGui::TextWrapped("No games yet. Put your NES games (.nes, .fds or .zip) in this folder:");
		ImGui::TextWrapped("%s", U8(_profile / "Games").c_str());
		ImGui::TextWrapped("or press A on \"Choose games folder\".");
	} else {
		ImGui::Text("%d games. Press A to play.", (int)_games.size());
		const float listWidth = ImGui::GetContentRegionAvail().x * 0.5f;
		ImGui::BeginChild("games", ImVec2(listWidth, 0), ImGuiChildFlags_NavFlattened);
		for(size_t i = 0; i < _games.size(); i++) {
			ImGui::PushID((int)i);
			bool chosen = ImGui::Selectable(_games[i].title.c_str(), false);
			if(ImGui::IsItemFocused()) {
				_focusedGame = i;
			}
			if(chosen) {
				GameEntry game = _games[i];
				ImGui::PopID();
				ImGui::EndChild();
				ImGui::End();
				StartGame(game);
				return;
			}
			ImGui::PopID();
		}
		ImGui::EndChild();

		// Box art and description of the highlighted game
		ImGui::SameLine();
		ImGui::BeginChild("details", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoNav);
		if(_covers && _focusedGame < _games.size()) {
			const Covers::Info& info = _covers->Get(_sdlRenderer, _games[_focusedGame]);
			ImVec2 avail = ImGui::GetContentRegionAvail();
			if(info.texture) {
				float scale = std::min(avail.x / info.width, avail.y * 0.6f / info.height);
				float w = info.width * scale;
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail.x - w) / 2);
				ImGui::Image((ImTextureID)(intptr_t)info.texture, ImVec2(w, info.height * scale));
			} else if(!info.coverDone) {
				ImGui::TextDisabled("Looking for the box art...");
			} else {
				ImGui::TextDisabled("No box art found");
			}
			ImGui::SetWindowFontScale(0.75f);
			if(!info.descriptionDone) {
				ImGui::TextDisabled("Looking for a description...");
			} else if(!info.description.empty()) {
				ImGui::TextWrapped("%s", info.description.c_str());
			}
			ImGui::SetWindowFontScale(1.0f);
		}
		ImGui::EndChild();
	}
	ImGui::End();
}

void App::DrawMenu()
{
	BeginFullScreen("Menu", _focusNext, 0.6f);
	switch(_page) {
		case MenuPage::Main: DrawMenuMain(); break;
		case MenuPage::Picture:
			Title("Picture");
			DrawPictureOptions();
			if(BigButton("Back")) {
				_page = MenuPage::Main;
				_focusNext = true;
			}
			break;
		case MenuPage::Sound:
			Title("Sound and text");
			DrawSoundOptions();
			if(BigButton("Back")) {
				_page = MenuPage::Main;
				_focusNext = true;
			}
			break;
		case MenuPage::Controller: DrawController(); break;
		case MenuPage::Cheats: DrawCheats(); break;
		case MenuPage::AddCode: DrawAddCode(); break;
	}
	ImGui::End();

	if(_capturing < 0 && BackPressed()) {
		if(_page == MenuPage::Main) {
			CloseMenu();
		} else if(_page == MenuPage::AddCode) {
			_page = MenuPage::Cheats;
			_focusNext = true;
		} else {
			_page = MenuPage::Main;
			_focusNext = true;
		}
	}
}

void App::DrawMenuMain()
{
	Title(_currentGame.title.c_str());
	if(BigButton("Back to game")) {
		CloseMenu();
		return;
	}
	Stepper("Save slot", _saveSlot, 1, 10, 1);
	if(BigButton("Save game")) {
		_emu->GetSaveStateManager()->SaveState(_saveSlot, false);
		Toast("Saved to slot " + std::to_string(_saveSlot));
	}
	if(BigButton("Load game")) {
		if(_emu->GetSaveStateManager()->LoadState(_saveSlot)) {
			Toast("Loaded slot " + std::to_string(_saveSlot));
			CloseMenu();
			return;
		}
		Toast("Slot " + std::to_string(_saveSlot) + " is empty");
	}
	if(BigButton("Game Genie cheats")) {
		_page = MenuPage::Cheats;
		_focusNext = true;
	}
	if(BigButton("Picture")) {
		_page = MenuPage::Picture;
		_focusNext = true;
	}
	if(BigButton("Sound and text size")) {
		_page = MenuPage::Sound;
		_focusNext = true;
	}
	if(BigButton("Controller buttons")) {
		_page = MenuPage::Controller;
		_focusNext = true;
	}
	if(BigButton("Reset game")) {
		_emu->Reset();
		CloseMenu();
		return;
	}
	if(BigButton(_showInfo ? "Hide info on screen" : "Show info on screen")) {
		_showInfo = !_showInfo;
	}
	if(BigButton("Quit to game list")) {
		QuitGame();
	}
}

void App::DrawPictureOptions()
{
	bool changed = false;
	ImGui::TextUnformatted("Picture style");
	for(int i = 0; i < (int)PictureStyle::Count; i++) {
		PictureStyle style = (PictureStyle)i;
		if(ImGui::RadioButton(PictureStyleName(style), _settings.picture == style)) {
			_settings.picture = style;
			changed = true;
		}
		if(ImGui::IsItemFocused()) {
			ImGui::SameLine();
			ImGui::TextDisabled("%s", PictureStyleHelp(style));
		}
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Screen shape");
	for(int i = 0; i < (int)ScreenShape::Count; i++) {
		ScreenShape shape = (ScreenShape)i;
		if(ImGui::RadioButton(ScreenShapeName(shape), _settings.shape == shape)) {
			_settings.shape = shape;
			changed = true;
		}
	}
	ImGui::Separator();
	changed |= Stepper("TV lines", _settings.scanlines, 0, 100, 10, "%");
	changed |= ImGui::Checkbox("No sprite flicker", &_settings.noFlicker);
	if(ImGui::Checkbox("HD packs", &_settings.hdPacks)) {
		changed = true;
		Toast("HD packs change when a game starts");
	}
	if(changed) {
		_settings.Save();
		ApplyMesenSettings();
	}
}

void App::DrawSoundOptions()
{
	if(Stepper("Volume", _settings.volume, 0, 100, 10, "%")) {
		_settings.Save();
		ApplyMesenSettings();
	}
	if(Stepper("Text size", _settings.uiScale, 60, 160, 10, "%")) {
		_settings.Save();
		_fontDirty = true;
	}
}

void App::DrawCheats()
{
	Title("Game Genie");
	bool changed = false;
	if(BigButton("Type a new code")) {
		_newCode.clear();
		_page = MenuPage::AddCode;
		_focusNext = true;
		return;
	}
	if(BigButton("Back")) {
		_page = MenuPage::Main;
		_focusNext = true;
		return;
	}

	auto row = [&](const CheatEntry& cheat, int id) {
		bool on = _cheats.IsEnabled(cheat.code);
		ImGui::PushID(id);
		std::string label = cheat.desc + "   (" + cheat.code + ")";
		if(ImGui::Checkbox(label.c_str(), &on)) {
			_cheats.SetEnabled(cheat.code, on);
			changed = true;
		}
		ImGui::PopID();
	};

	int id = 0;
	if(!_cheats.custom.empty()) {
		ImGui::TextUnformatted("Your codes");
		for(const CheatEntry& cheat : _cheats.custom) {
			row(cheat, id++);
		}
	}
	const std::vector<CheatEntry>* known = _cheatDb.Find(_currentSha1);
	if(known && !known->empty()) {
		ImGui::TextUnformatted("Known codes for this game");
		for(const CheatEntry& cheat : *known) {
			row(cheat, id++);
		}
	} else if(_cheats.custom.empty()) {
		ImGui::TextWrapped("No known codes for this game. You can type one in.");
	}

	if(changed) {
		_cheats.Save(_profile / "Cheats" / (_currentSha1 + ".txt"));
		ApplyCheats();
		Toast("Cheats updated");
	}
}

void App::DrawAddCode()
{
	Title("Type a Game Genie code");
	ImGui::SetWindowFontScale(1.6f);
	ImGui::Text("%s_", _newCode.c_str());
	ImGui::SetWindowFontScale(1.0f);
	ImGui::TextDisabled("Codes are 6 or 8 letters");

	const float size = ImGui::GetFontSize() * 2.2f;
	for(int i = 0; i < 16; i++) {
		char letter[2] = {kGenieLetters[i], 0};
		if(i % 8 != 0) {
			ImGui::SameLine();
		}
		if(ImGui::Button(letter, ImVec2(size, size)) && _newCode.size() < 8) {
			_newCode += letter;
		}
	}
	if(ImGui::Button("Delete letter") && !_newCode.empty()) {
		_newCode.pop_back();
	}
	bool valid = _newCode.size() == 6 || _newCode.size() == 8;
	ImGui::BeginDisabled(!valid);
	if(BigButton("Add this code")) {
		CheatEntry entry;
		entry.code = _newCode;
		entry.desc = "My code " + std::to_string(_cheats.custom.size() + 1);
		entry.custom = true;
		_cheats.custom.push_back(entry);
		_cheats.SetEnabled(entry.code, true);
		_cheats.Save(_profile / "Cheats" / (_currentSha1 + ".txt"));
		ApplyCheats();
		Toast("Code " + _newCode + " is on");
		_page = MenuPage::Cheats;
		_focusNext = true;
	}
	ImGui::EndDisabled();
	if(BigButton("Cancel")) {
		_page = MenuPage::Cheats;
		_focusNext = true;
	}
}

void App::DrawToast()
{
	if(_toast.empty() || std::chrono::steady_clock::now() > _toastUntil) {
		return;
	}
	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + viewport->Size.x / 2, viewport->Pos.y + viewport->Size.y * 0.9f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
	ImGui::SetNextWindowBgAlpha(0.85f);
	ImGui::Begin("Toast", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoSavedSettings);
	ImGui::TextUnformatted(_toast.c_str());
	ImGui::End();
}

void App::DrawLastSession()
{
	if(_lastSession.empty()) {
		return;
	}
	ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.08f, 0.08f, 1.0f));
	ImGui::BeginChild("lastSession", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders);
	ImGui::TextWrapped("Last time, HD Eight had a problem. These are its last notes. Take a photo of this for Claude:");
	ImGui::SetWindowFontScale(0.6f);
	for(const std::string& line : _lastSession) {
		ImGui::TextWrapped("%s", line.c_str());
	}
	ImGui::SetWindowFontScale(1.0f);
	if(ImGui::Button("Hide this")) {
		_lastSession.clear();
		_focusNext = true;
	}
	ImGui::EndChild();
	ImGui::PopStyleColor();
}

// The info panel: what HD Eight is doing right now, Mesen's counters and the newest log lines
void App::DrawInfo()
{
	if(g_crashed) {
		std::string text;
		{
			std::lock_guard lock(g_crashLock);
			text = g_crashText;
		}
		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 24, viewport->Pos.y + 24));
		ImGui::SetNextWindowSize(ImVec2(viewport->Size.x - 48, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.45f, 0.05f, 0.05f, 0.97f));
		ImGui::Begin("Crash", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
		ImGui::TextWrapped("%s", text.c_str());
		ImGui::TextWrapped("Take a photo of this for Claude, then close HD Eight with the Xbox button.");
		ImGui::SetWindowFontScale(0.6f);
		for(const std::string& line : uwp::RecentLog(8)) {
			ImGui::TextWrapped("%s", line.c_str());
		}
		ImGui::SetWindowFontScale(1.0f);
		ImGui::End();
		ImGui::PopStyleColor();
		return;
	}
	bool noPicture = _screen == Screen::Playing && !_hasFrame;
	if(!_loading && !noPicture && !(_showInfo && _screen == Screen::Playing && !_menuOpen)) {
		return;
	}
	auto now = std::chrono::steady_clock::now();
	std::string step;
	double stepSeconds = 0;
	{
		std::lock_guard lock(_stepLock);
		step = _step;
		stepSeconds = std::chrono::duration<double>(now - _stepTime).count();
	}

	const ImGuiViewport* viewport = ImGui::GetMainViewport();
	bool big = _loading || noPicture;
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x + 24, viewport->Pos.y + viewport->Size.y - 24), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
	ImGui::SetNextWindowSize(ImVec2(big ? viewport->Size.x - 48 : viewport->Size.x * 0.42f, 0));
	ImGui::SetNextWindowBgAlpha(0.85f);
	ImGui::Begin("Info", nullptr,
		ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
			ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize);
	if(!big) {
		ImGui::SetWindowFontScale(0.6f);
	}

	// A moving dot shows the screen itself is not frozen
	static const char* spinner[] = {"|", "/", "-", "\\"};
	int tick = (int)(std::chrono::duration<double>(now.time_since_epoch()).count() * 4) % 4;
	if(_loading) {
		double total = std::chrono::duration<double>(now - _loadStart).count();
		ImGui::Text("%s  Starting %s ... %.0f seconds", spinner[tick], _loadGame.title.c_str(), total);
	} else {
		ImGui::Text("%s  %s", spinner[tick], _screen == Screen::Playing ? _currentGame.title.c_str() : "HD Eight");
	}
	ImGui::TextWrapped("Now: %s (%.0f s)", step.c_str(), stepSeconds);
	ImGui::Text("Frames run: %llu   Pictures: %llu   Sounds: %llu   Controller reads: %llu",
		(unsigned long long)hdeight::emuFrames.load(), (unsigned long long)_video->FrameCount(),
		(unsigned long long)_audio->BufferCount(), (unsigned long long)_input->ReadCount());
	ImGui::TextWrapped("Mesen: %s", hdeight::emuMarker.load());
	ImGui::TextWrapped("Video filter: %s", hdeight::filterMarker.load());
	ImGui::TextWrapped("Sound: %s", _audio->State());
	if(_loading) {
		ImGui::TextWrapped("Loader: %s", g_workerMarker.load());
	}

	if(big) {
		ImGui::Separator();
		ImGui::SetWindowFontScale(0.6f);
		for(const std::string& line : uwp::RecentLog(8)) {
			bool problem = line.find("[!]") != std::string::npos;
			if(problem) {
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
			}
			ImGui::TextWrapped("%s", line.c_str());
			if(problem) {
				ImGui::PopStyleColor();
			}
		}
		ImGui::SetWindowFontScale(1.0f);
	}
	ImGui::SetWindowFontScale(1.0f);
	ImGui::End();
}

// Controller page: pick a NES button, then press the Xbox button it should be on
void App::DrawController()
{
	Title("Controller buttons");
	uint64_t held = _input->AnyPadButtons();

	if(_capturing >= 0) {
		ImGui::TextWrapped("Press the Xbox button you want for NES %s", kNesButtonNames[_capturing]);
		ImGui::TextDisabled("(A, B, X, Y, LB, RB, LT, RT, View, Menu or a left stick click)");
		if(!_captureReleased) {
			// Wait until the button that opened this is let go
			_captureReleased = held == 0;
			return;
		}
		for(pad::Button button : kAssignable) {
			if((held >> button) & 1) {
				_settings.buttons[_capturing] = button;
				_settings.Save();
				ApplyMesenSettings();
				Toast(std::string("NES ") + kNesButtonNames[_capturing] + " is now Xbox " + XboxButtonName(button));
				_capturing = -1;
				_focusNext = true;
				_ignoreUntilReleased = true;
				break;
			}
		}
		return;
	}
	if(_ignoreUntilReleased) {
		// Do not let the button just pressed also click something on this page
		if(held != 0) {
			ImGui::GetIO().ClearInputKeys();
			return;
		}
		_ignoreUntilReleased = false;
	}

	ImGui::TextWrapped("Pick a NES button, then press the Xbox button you want for it.");
	for(int i = 0; i < AppSettings::kButtonCount; i++) {
		std::string label = std::string("NES ") + kNesButtonNames[i] + ":   Xbox " + XboxButtonName(_settings.buttons[i]);
		ImGui::PushID(i);
		if(BigButton(label.c_str())) {
			_capturing = i;
			_captureReleased = false;
		}
		ImGui::PopID();
	}
	ImGui::Separator();
	ImGui::TextUnformatted("Turbo (the button fires by itself while you hold it)");
	if(ImGui::Checkbox("Turbo on NES A", &_settings.turboA) | ImGui::Checkbox("Turbo on NES B", &_settings.turboB)) {
		_settings.Save();
		ApplyMesenSettings();
	}
	ImGui::Separator();
	if(BigButton("Put buttons back the way they were")) {
		AppSettings defaults;
		std::copy(std::begin(defaults.buttons), std::end(defaults.buttons), _settings.buttons);
		_settings.turboA = false;
		_settings.turboB = false;
		_settings.Save();
		ApplyMesenSettings();
		Toast("Buttons are back to normal");
	}
	if(BigButton("Back")) {
		_page = MenuPage::Main;
		_focusNext = true;
	}
}
