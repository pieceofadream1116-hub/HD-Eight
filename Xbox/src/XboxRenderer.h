#pragma once
// Receives the finished (already filtered) frames from Mesen's video thread and hands the newest one to the UI thread,
// which uploads it to an SDL texture.

#include "pch.h"
#include "Shared/Interfaces/IRenderingDevice.h"

#include <atomic>
#include <mutex>
#include <vector>

class Emulator;

class XboxRenderer final : public IRenderingDevice
{
public:
	explicit XboxRenderer(Emulator* emu);
	~XboxRenderer() override;

	void UpdateFrame(RenderedFrame& frame) override;
	void ClearFrame() override;
	void Render(RenderSurfaceInfo& emuHud, RenderSurfaceInfo& scriptHud) override;
	void Reset() override {}
	void SetExclusiveFullscreenMode(bool, void*) override {}

	/// Copies the newest frame into `out` if there is one the caller has not seen yet (UI thread).
	bool TakeFrame(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height);

	/// Copies Mesen's on-screen messages layer (save state, cheats...) if it changed (UI thread).
	bool TakeHud(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height);

	/// How many finished pictures Mesen has sent (for the on-screen info).
	uint64_t FrameCount() const { return _frameCount; }

private:
	Emulator* _emu;
	std::mutex _lock;
	std::vector<uint32_t> _frame;
	uint32_t _width = 0;
	uint32_t _height = 0;
	bool _newFrame = false;
	std::atomic<uint64_t> _frameCount = 0;

	std::vector<uint32_t> _hud;
	uint32_t _hudWidth = 0;
	uint32_t _hudHeight = 0;
	bool _newHud = false;
};
