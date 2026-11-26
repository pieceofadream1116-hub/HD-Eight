#pragma once
// HD Eight's own settings (picture, sound, folders), saved as a small text file in the profile folder.

#include <filesystem>
#include <string>
#include <vector>

/// The picture styles shown in the menu, from plain to "pushed to the max".
enum class PictureStyle
{
	SharpPixels = 0,  // original pixels, no filter
	Smooth,           // original pixels, smoothed when scaled up
	HdUpscale,        // xBRZ 4x
	MaxHd,            // xBRZ 6x
	Hq4x,             // HQ4x
	TvLook,           // NTSC (Blargg) composite TV signal
	SuperEagle,       // Super Eagle
	Count
};

const char* PictureStyleName(PictureStyle style);
const char* PictureStyleHelp(PictureStyle style);

enum class ScreenShape
{
	OriginalTv = 0,   // 4:3, like a TV of the time
	PixelPerfect,     // square pixels (8:7)
	FillScreen,       // stretch to the whole TV
	Count
};

const char* ScreenShapeName(ScreenShape shape);

struct AppSettings
{
	std::string gamesFolder;                 // extra folder picked by the player (the profile's Games folder is always scanned)
	PictureStyle picture = PictureStyle::HdUpscale;
	ScreenShape shape = ScreenShape::OriginalTv;
	int scanlines = 0;                       // 0-100
	bool hdPacks = true;                     // load HD packs from the HdPacks folder
	bool noFlicker = true;                   // remove the 8 sprites per line limit
	int volume = 100;                        // 0-100
	int uiScale = 100;                       // text size in percent (100 = big, made for a TV)

	void Load(const std::filesystem::path& file);
	void Save() const;

private:
	std::filesystem::path _file;
};
