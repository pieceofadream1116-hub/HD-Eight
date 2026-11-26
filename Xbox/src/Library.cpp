#include "Library.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace {

std::string Lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	return s;
}

const char* SystemForExtension(const std::string& ext)
{
	static const std::unordered_map<std::string, const char*> systems = {
		{".nes", "NES"}, {".unf", "NES"}, {".unif", "NES"}, {".fds", "Famicom Disk"}, {".qd", "Famicom Disk"},
		{".nsf", "NES Music"}, {".nsfe", "NES Music"}, {".studybox", "NES"},
		// HD Eight is an NES emulator: Mesen's other systems are left out of the list
		{".zip", "Archive"}, {".7z", "Archive"},
	};
	auto it = systems.find(ext);
	return it == systems.end() ? nullptr : it->second;
}

// Reads a JSON string value starting at the opening quote; handles the escapes used by Mesen's cheat files
std::string ReadJsonString(const std::string& text, size_t& pos)
{
	std::string out;
	pos++; // opening quote
	while(pos < text.size() && text[pos] != '"') {
		char c = text[pos++];
		if(c == '\\' && pos < text.size()) {
			char e = text[pos++];
			switch(e) {
				case 'n': out += '\n'; break;
				case 't': out += '\t'; break;
				case 'u': {
					unsigned int code = 0;
					if(pos + 4 <= text.size()) {
						code = (unsigned int)std::stoul(text.substr(pos, 4), nullptr, 16);
						pos += 4;
					}
					if(code < 0x80) {
						out += (char)code;
					} else if(code < 0x800) {
						out += (char)(0xC0 | (code >> 6));
						out += (char)(0x80 | (code & 0x3F));
					} else {
						out += (char)(0xE0 | (code >> 12));
						out += (char)(0x80 | ((code >> 6) & 0x3F));
						out += (char)(0x80 | (code & 0x3F));
					}
					break;
				}
				default: out += e; break;
			}
		} else {
			out += c;
		}
	}
	pos++; // closing quote
	return out;
}

} // namespace

std::vector<GameEntry> ScanGames(const std::vector<std::filesystem::path>& folders)
{
	std::vector<GameEntry> games;
	std::set<std::filesystem::path> seen;
	for(const auto& folder : folders) {
		std::error_code error;
		if(folder.empty() || !std::filesystem::is_directory(folder, error)) {
			continue;
		}
		auto options = std::filesystem::directory_options::skip_permission_denied;
		for(auto it = std::filesystem::recursive_directory_iterator(folder, options, error);
			it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
			if(error) {
				break;
			}
			if(!it->is_regular_file(error)) {
				continue;
			}
			std::string ext = Lower(it->path().extension().string());
			const char* system = SystemForExtension(ext);
			if(!system) {
				continue;
			}
			auto canonical = std::filesystem::weakly_canonical(it->path(), error);
			if(!seen.insert(error ? it->path() : canonical).second) {
				continue;
			}
			games.push_back({it->path(), it->path().stem().string(), system});
		}
	}
	std::sort(games.begin(), games.end(), [](const GameEntry& a, const GameEntry& b) {
		return Lower(a.title) < Lower(b.title);
	});
	return games;
}

void CheatDatabase::LoadJson(const std::filesystem::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if(!in) {
		return;
	}
	std::stringstream buffer;
	buffer << in.rdbuf();
	const std::string text = buffer.str();

	// The file is a list of {"name", "sha1", "cheats": [{"desc", "code"}...]} objects
	std::string sha1;
	std::string desc;
	size_t pos = 0;
	while((pos = text.find('"', pos)) != std::string::npos) {
		std::string key = ReadJsonString(text, pos);
		size_t colon = text.find_first_not_of(" \t\r\n", pos);
		if(colon == std::string::npos || text[colon] != ':') {
			continue;
		}
		size_t value = text.find_first_not_of(" \t\r\n", colon + 1);
		if(value == std::string::npos) {
			break;
		}
		if(text[value] != '"') {
			pos = value;
			continue;
		}
		pos = value;
		std::string str = ReadJsonString(text, pos);
		if(key == "sha1") {
			sha1 = str;
			std::transform(sha1.begin(), sha1.end(), sha1.begin(), [](unsigned char c) { return (char)std::toupper(c); });
		} else if(key == "desc") {
			desc = str;
		} else if(key == "code" && !sha1.empty()) {
			_games[sha1].push_back({desc, str, false});
		}
	}
}

const std::vector<CheatEntry>* CheatDatabase::Find(const std::string& sha1) const
{
	std::string key = sha1;
	std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return (char)std::toupper(c); });
	auto it = _games.find(key);
	return it == _games.end() ? nullptr : &it->second;
}

void GameCheats::Load(const std::filesystem::path& file)
{
	custom.clear();
	enabledCodes.clear();
	std::ifstream in(file);
	std::string line;
	while(std::getline(in, line)) {
		if(!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if(line.rfind("on|", 0) == 0) {
			enabledCodes.push_back(line.substr(3));
		} else if(line.rfind("custom|", 0) == 0) {
			std::string rest = line.substr(7);
			size_t bar = rest.find('|');
			CheatEntry entry;
			entry.custom = true;
			entry.code = rest.substr(0, bar);
			entry.desc = bar == std::string::npos ? entry.code : rest.substr(bar + 1);
			custom.push_back(entry);
		}
	}
}

void GameCheats::Save(const std::filesystem::path& file) const
{
	std::error_code error;
	std::filesystem::create_directories(file.parent_path(), error);
	std::ofstream out(file, std::ios::trunc);
	for(const auto& entry : custom) {
		out << "custom|" << entry.code << "|" << entry.desc << "\n";
	}
	for(const auto& code : enabledCodes) {
		out << "on|" << code << "\n";
	}
}

bool GameCheats::IsEnabled(const std::string& code) const
{
	return std::find(enabledCodes.begin(), enabledCodes.end(), code) != enabledCodes.end();
}

void GameCheats::SetEnabled(const std::string& code, bool enabled)
{
	auto it = std::find(enabledCodes.begin(), enabledCodes.end(), code);
	if(enabled && it == enabledCodes.end()) {
		enabledCodes.push_back(code);
	} else if(!enabled && it != enabledCodes.end()) {
		enabledCodes.erase(it);
	}
}
