#include "Settings.h"

#include <algorithm>
#include <fstream>

const char* PictureStyleName(PictureStyle style)
{
	switch(style) {
		case PictureStyle::SharpPixels: return "Sharp pixels";
		case PictureStyle::Smooth: return "Smooth";
		case PictureStyle::TvLook: return "Old TV look";
		case PictureStyle::MaxHd: return "Max HD";
		default: return "?";
	}
}

const char* PictureStyleHelp(PictureStyle style)
{
	switch(style) {
		case PictureStyle::SharpPixels: return "The original blocky pixels, crisp and clean.";
		case PictureStyle::Smooth: return "The original pixels, softened so edges don't look jagged.";
		case PictureStyle::TvLook: return "Like a real TV of the time: colours blend the way games expected (waterfalls, shadows).";
		case PictureStyle::MaxHd: return "Rounds off the blocky edges, then smooths the picture up to fill a 4K TV cleanly.";
		default: return "";
	}
}

const char* ScreenShapeName(ScreenShape shape)
{
	switch(shape) {
		case ScreenShape::OriginalTv: return "Original TV shape";
		case ScreenShape::PixelPerfect: return "Square pixels";
		case ScreenShape::FillScreen: return "Fill the screen";
		default: return "?";
	}
}

void AppSettings::Load(const std::filesystem::path& file)
{
	_file = file;
	std::ifstream in(file);
	std::string line;
	while(std::getline(in, line)) {
		if(!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		size_t eq = line.find('=');
		if(eq == std::string::npos) {
			continue;
		}
		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);
		auto toInt = [&](int def) {
			try {
				return std::stoi(value);
			} catch(...) {
				return def;
			}
		};
		if(key == "gamesFolder") {
			gamesFolder = value;
		} else if(key == "backgroundPicture") {
			backgroundPicture = value;
		} else if(key == "picture") {
			picture = (PictureStyle)std::clamp(toInt((int)picture), 0, (int)PictureStyle::Count - 1);
		} else if(key == "shape") {
			shape = (ScreenShape)std::clamp(toInt((int)shape), 0, (int)ScreenShape::Count - 1);
		} else if(key == "scanlines") {
			scanlines = std::clamp(toInt(scanlines), 0, 100);
		} else if(key == "bestSound") {
			bestSound = toInt(1) != 0;
		} else if(key == "noFlicker") {
			noFlicker = toInt(1) != 0;
		} else if(key == "volume") {
			volume = std::clamp(toInt(volume), 0, 100);
		} else if(key == "uiScale") {
			uiScale = std::clamp(toInt(uiScale), 60, 200);
		} else if(key.rfind("turbo", 0) == 0 && key.size() == 6 && key[5] >= '0' && key[5] < '3') {
			turbo[key[5] - '0'] = toInt(0) != 0;
		} else if(key.rfind("button", 0) == 0 && key.size() == 7 && key[6] >= '0' && key[6] < '0' + kButtonCount) {
			int i = key[6] - '0';
			buttons[i] = std::clamp(toInt(buttons[i]), 0, 63);
		}
	}
}

void AppSettings::Save() const
{
	if(_file.empty()) {
		return;
	}
	std::ofstream out(_file, std::ios::trunc);
	out << "gamesFolder=" << gamesFolder << "\n";
	out << "backgroundPicture=" << backgroundPicture << "\n";
	out << "picture=" << (int)picture << "\n";
	out << "shape=" << (int)shape << "\n";
	out << "scanlines=" << scanlines << "\n";
	out << "bestSound=" << (bestSound ? 1 : 0) << "\n";
	out << "noFlicker=" << (noFlicker ? 1 : 0) << "\n";
	out << "volume=" << volume << "\n";
	out << "uiScale=" << uiScale << "\n";
	for(int i = 0; i < 3; i++) {
		out << "turbo" << i << "=" << (turbo[i] ? 1 : 0) << "\n";
	}
	for(int i = 0; i < kButtonCount; i++) {
		out << "button" << i << "=" << buttons[i] << "\n";
	}
}
