#include "XboxRenderer.h"

#include "Shared/Emulator.h"
#include "Shared/RenderedFrame.h"
#include "Shared/Video/VideoRenderer.h"

#include <cstring>

XboxRenderer::XboxRenderer(Emulator* emu) : _emu(emu)
{
	_emu->GetVideoRenderer()->RegisterRenderingDevice(this);
}

XboxRenderer::~XboxRenderer()
{
	_emu->GetVideoRenderer()->UnregisterRenderingDevice(this);
}

void XboxRenderer::UpdateFrame(RenderedFrame& frame)
{
	if(!frame.FrameBuffer || frame.Width == 0 || frame.Height == 0) {
		return;
	}
	std::lock_guard lock(_lock);
	size_t pixels = (size_t)frame.Width * frame.Height;
	_frame.resize(pixels);
	memcpy(_frame.data(), frame.FrameBuffer, pixels * sizeof(uint32_t));
	_width = frame.Width;
	_height = frame.Height;
	_newFrame = true;
	_frameCount++;
}

void XboxRenderer::ClearFrame()
{
	std::lock_guard lock(_lock);
	std::fill(_frame.begin(), _frame.end(), 0xFF000000);
	_newFrame = true;
}

void XboxRenderer::Render(RenderSurfaceInfo& emuHud, RenderSurfaceInfo& scriptHud)
{
	(void)scriptHud;
	if(!emuHud.IsDirty || !emuHud.Buffer || emuHud.Width == 0 || emuHud.Height == 0) {
		return;
	}
	std::lock_guard lock(_lock);
	size_t pixels = (size_t)emuHud.Width * emuHud.Height;
	_hud.resize(pixels);
	memcpy(_hud.data(), emuHud.Buffer, pixels * sizeof(uint32_t));
	_hudWidth = emuHud.Width;
	_hudHeight = emuHud.Height;
	_newHud = true;
	emuHud.IsDirty = false;
}

bool XboxRenderer::TakeFrame(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height)
{
	std::lock_guard lock(_lock);
	if(!_newFrame) {
		return false;
	}
	out = _frame;
	width = _width;
	height = _height;
	_newFrame = false;
	return true;
}

bool XboxRenderer::TakeHud(std::vector<uint32_t>& out, uint32_t& width, uint32_t& height)
{
	std::lock_guard lock(_lock);
	if(!_newHud) {
		return false;
	}
	out = _hud;
	width = _hudWidth;
	height = _hudHeight;
	_newHud = false;
	return true;
}
