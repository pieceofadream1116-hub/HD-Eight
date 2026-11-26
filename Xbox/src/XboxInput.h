#pragma once
// Gamepad input for Mesen. The UI thread reads the Xbox controllers through SDL and publishes a snapshot that Mesen's
// emulation thread reads when it polls the controllers.

#include "pch.h"
#include "Shared/Interfaces/IKeyManager.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>

class Emulator;

namespace pad {

// Key codes HD Eight gives to Mesen: one per gamepad button, per player
constexpr uint16_t kBase = IKeyManager::BaseGamepadIndex;
constexpr uint16_t kPlayerStride = 0x40;

enum Button : uint16_t
{
	A = 0, B, X, Y, Back, Guide, Start, LeftStick, RightStick, LeftShoulder, RightShoulder,
	Up, Down, Left, Right,
	// Synthesized from the analog sticks and triggers
	StickUp = 32, StickDown, StickLeft, StickRight, LeftTrigger, RightTrigger,
	Count = 40
};

inline uint16_t Code(int player, Button button)
{
	return (uint16_t)(kBase + player * kPlayerStride + button);
}

} // namespace pad

class XboxInput final : public IKeyManager
{
public:
	static constexpr int kMaxPlayers = 4;

	explicit XboxInput(Emulator* emu);
	~XboxInput() override;

	/// UI thread: opens newly connected pads and publishes the current button state.
	void Poll();

	/// Buttons held on any pad (bit per pad::Button), for menu navigation.
	uint64_t AnyPadButtons() const { return _anyPad; }

	void RefreshState() override {}
	void UpdateDevices() override {}
	bool IsMouseButtonPressed(MouseButton) override { return false; }
	bool IsKeyPressed(uint16_t keyCode) override;
	vector<uint16_t> GetPressedKeys() override;
	string GetKeyName(uint16_t keyCode) override;
	uint16_t GetKeyCode(string keyName) override;
	bool SetKeyState(uint16_t, bool) override { return false; }
	void ResetKeyState() override {}
	void SetDisabled(bool disabled) override { _disabled = disabled; }
	void SetForceFeedback(uint16_t magnitudeRight, uint16_t magnitudeLeft) override;

private:
	Emulator* _emu;
	std::array<std::atomic<uint64_t>, kMaxPlayers> _state{};
	std::atomic<uint64_t> _anyPad = 0;
	std::atomic<bool> _disabled = false;
	std::array<SDL_Gamepad*, kMaxPlayers> _pads{};
	std::atomic<uint16_t> _rumbleLow = 0;
	std::atomic<uint16_t> _rumbleHigh = 0;
};
