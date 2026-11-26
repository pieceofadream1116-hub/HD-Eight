#include "XboxInput.h"

#include "Shared/Emulator.h"
#include "Shared/KeyManager.h"

#include <string>

namespace {

constexpr Sint16 kStickThreshold = 16000;
constexpr Sint16 kTriggerThreshold = 12000;

const char* ButtonName(int button)
{
	switch(button) {
		case pad::A: return "A";
		case pad::B: return "B";
		case pad::X: return "X";
		case pad::Y: return "Y";
		case pad::Back: return "View";
		case pad::Guide: return "Guide";
		case pad::Start: return "Menu";
		case pad::LeftStick: return "LS";
		case pad::RightStick: return "RS";
		case pad::LeftShoulder: return "LB";
		case pad::RightShoulder: return "RB";
		case pad::Up: return "Up";
		case pad::Down: return "Down";
		case pad::Left: return "Left";
		case pad::Right: return "Right";
		case pad::StickUp: return "Stick Up";
		case pad::StickDown: return "Stick Down";
		case pad::StickLeft: return "Stick Left";
		case pad::StickRight: return "Stick Right";
		case pad::LeftTrigger: return "LT";
		case pad::RightTrigger: return "RT";
		default: return nullptr;
	}
}

} // namespace

XboxInput::XboxInput(Emulator* emu) : _emu(emu)
{
	KeyManager::RegisterKeyManager(this);
}

XboxInput::~XboxInput()
{
	KeyManager::RegisterKeyManager(nullptr);
	for(auto& gamepad : _pads) {
		if(gamepad) {
			SDL_CloseGamepad(gamepad);
			gamepad = nullptr;
		}
	}
}

void XboxInput::Poll()
{
	// Keep the open pads in step with what is connected; players get the lowest free slot
	for(auto& gamepad : _pads) {
		if(gamepad && !SDL_GamepadConnected(gamepad)) {
			SDL_CloseGamepad(gamepad);
			gamepad = nullptr;
		}
	}
	int count = 0;
	SDL_JoystickID* ids = SDL_GetGamepads(&count);
	if(ids) {
		for(int i = 0; i < count; i++) {
			bool open = false;
			for(auto* gamepad : _pads) {
				if(gamepad && SDL_GetGamepadID(gamepad) == ids[i]) {
					open = true;
				}
			}
			if(open) {
				continue;
			}
			for(auto& gamepad : _pads) {
				if(!gamepad) {
					gamepad = SDL_OpenGamepad(ids[i]);
					break;
				}
			}
		}
		SDL_free(ids);
	}

	uint64_t any = 0;
	for(int player = 0; player < kMaxPlayers; player++) {
		SDL_Gamepad* gamepad = _pads[player];
		uint64_t bits = 0;
		if(gamepad) {
			for(int b = 0; b <= pad::Right; b++) {
				if(SDL_GetGamepadButton(gamepad, (SDL_GamepadButton)b)) {
					bits |= 1ull << b;
				}
			}
			Sint16 lx = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
			Sint16 ly = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
			if(ly < -kStickThreshold) bits |= 1ull << pad::StickUp;
			if(ly > kStickThreshold) bits |= 1ull << pad::StickDown;
			if(lx < -kStickThreshold) bits |= 1ull << pad::StickLeft;
			if(lx > kStickThreshold) bits |= 1ull << pad::StickRight;
			if(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerThreshold) bits |= 1ull << pad::LeftTrigger;
			if(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerThreshold) bits |= 1ull << pad::RightTrigger;

			uint16_t low = _rumbleLow;
			uint16_t high = _rumbleHigh;
			if(player == 0 && (low || high)) {
				SDL_RumbleGamepad(gamepad, low, high, 100);
			}
		}
		_state[player] = bits;
		any |= bits;
	}
	_anyPad = any;
}

bool XboxInput::IsKeyPressed(uint16_t keyCode)
{
	if(_disabled || keyCode < pad::kBase) {
		return false;
	}
	int index = keyCode - pad::kBase;
	int player = index / pad::kPlayerStride;
	int button = index % pad::kPlayerStride;
	if(player >= kMaxPlayers || button >= 64) {
		return false;
	}
	return (_state[player].load() >> button) & 1;
}

vector<uint16_t> XboxInput::GetPressedKeys()
{
	vector<uint16_t> keys;
	for(int player = 0; player < kMaxPlayers; player++) {
		uint64_t bits = _state[player];
		for(int b = 0; b < pad::Count; b++) {
			if((bits >> b) & 1) {
				keys.push_back(pad::Code(player, (pad::Button)b));
			}
		}
	}
	return keys;
}

string XboxInput::GetKeyName(uint16_t keyCode)
{
	if(keyCode < pad::kBase) {
		return "";
	}
	int index = keyCode - pad::kBase;
	const char* name = ButtonName(index % pad::kPlayerStride);
	if(!name) {
		return "";
	}
	return "Pad" + std::to_string(index / pad::kPlayerStride + 1) + " " + name;
}

uint16_t XboxInput::GetKeyCode(string keyName)
{
	for(int player = 0; player < kMaxPlayers; player++) {
		for(int b = 0; b < pad::Count; b++) {
			if(GetKeyName(pad::Code(player, (pad::Button)b)) == keyName) {
				return pad::Code(player, (pad::Button)b);
			}
		}
	}
	return 0;
}

void XboxInput::SetForceFeedback(uint16_t magnitudeRight, uint16_t magnitudeLeft)
{
	_rumbleLow = magnitudeLeft;
	_rumbleHigh = magnitudeRight;
}
