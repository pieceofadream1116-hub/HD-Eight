#pragma once
// The app's own settings (picture, sound, buttons, folders), saved as a small text file in the profile folder.

#include <filesystem>
#include <string>

/// The picture styles shown in the menu.
enum class PictureStyle
{
	SharpPixels = 0,  // original pixels, no filter
	Smooth,           // original pixels, smoothed when scaled up
	TvLook,           // NTSC (Blargg) composite TV signal, made by the core
	MaxHd,            // edges rounded off (Scale2x twice), then smoothed up to the TV's size
	Count
};

const char* PictureStyleName(PictureStyle style);
const char* PictureStyleHelp(PictureStyle style);

enum class ScreenShape
{
	OriginalTv = 0,   // the shape the game was made for (4:3 on a TV of the time)
	PixelPerfect,     // whole multiples of the picture, square pixels
	FillScreen,       // stretch to the whole TV
	Count
};

const char* ScreenShapeName(ScreenShape shape);

/// The Genesis buttons that can be moved to another Xbox button, in AppSettings::buttons order.
inline constexpr const char* kPadButtonNames[] = {"A", "B", "C", "X", "Y", "Z", "Start", "Mode"};

struct AppSettings
{
	std::string gamesFolder;                 // extra folder picked by the player (the profile's Games folder is always scanned)
	PictureStyle picture = PictureStyle::Smooth;
	ScreenShape shape = ScreenShape::OriginalTv;
	int scanlines = 0;                       // 0-100
	bool noFlicker = true;                   // remove the sprites per line limit
	bool bestSound = true;                   // Nuked YM2612 (the most accurate FM sound)
	int volume = 100;                        // 0-100
	int uiScale = 100;                       // text size in percent (100 = big, made for a TV)

	std::string backgroundPicture;           // the player's own picture behind the game list (empty: the moving blue one)

	/// Xbox button (pad::Button number) for each Genesis button, in the order of kPadButtonNames.
	/// Default: the bottom row X A B = Genesis A B C, the top row LB Y RB = X Y Z, Menu = Start, View = Mode.
	static constexpr int kButtonCount = 8;
	int buttons[kButtonCount] = {2, 0, 1, 9, 3, 10, 6, 4};
	bool turbo[3] = {false, false, false};   // Genesis A, B, C fire by themselves while held

	void Load(const std::filesystem::path& file);
	void Save() const;

private:
	std::filesystem::path _file;
};
