#pragma once
// The game list (files found in the games folders) and the Game Genie cheat database.

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

struct GameEntry
{
	std::filesystem::path path;
	std::string title;   // file name without extension
	std::string system;  // "NES", "SNES", "Game Boy", ...
};

/// Lists the games in the folders (and their subfolders), sorted by title.
std::vector<GameEntry> ScanGames(const std::vector<std::filesystem::path>& folders);

struct CheatEntry
{
	std::string desc;
	std::string code;      // one or more codes separated by ';'
	bool custom = false;   // typed in by the player, not from the database
};

/// Mesen's cheat databases (CheatDb.Nes.json, CheatDb.Snes.json): known Game Genie codes for each game, by SHA-1.
class CheatDatabase
{
public:
	void LoadJson(const std::filesystem::path& file);
	const std::vector<CheatEntry>* Find(const std::string& sha1) const;
	size_t GameCount() const { return _games.size(); }

private:
	std::unordered_map<std::string, std::vector<CheatEntry>> _games;
};

/// The cheats a player switched on (and the ones they typed in) for one game, saved in Cheats/<sha1>.txt.
struct GameCheats
{
	std::vector<CheatEntry> custom;
	std::vector<std::string> enabledCodes;

	void Load(const std::filesystem::path& file);
	void Save(const std::filesystem::path& file) const;
	bool IsEnabled(const std::string& code) const;
	void SetEnabled(const std::string& code, bool enabled);
};
