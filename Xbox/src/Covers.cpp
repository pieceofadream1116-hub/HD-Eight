#include "Covers.h"

#include "UwpPlatform.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string_view>
#include <fstream>
#include <optional>
#include <sstream>

namespace fs = std::filesystem;
using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Storage::Streams;

namespace {

constexpr uint32_t kMaxBytes = 4 * 1024 * 1024;
constexpr uint32_t kMaxEdge = 512; // sharp enough for a TV

std::string U8(const fs::path& path)
{
	auto s = path.u8string();
	return std::string(s.begin(), s.end());
}

fs::path FromU8(const std::string& s)
{
	return fs::path(std::u8string(s.begin(), s.end()));
}

std::string UrlEncode(const std::string& text)
{
	static const char* hex = "0123456789ABCDEF";
	std::string out;
	for(unsigned char ch : text) {
		if(std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
			out.push_back((char)ch);
		} else {
			out.push_back('%');
			out.push_back(hex[ch >> 4]);
			out.push_back(hex[ch & 15]);
		}
	}
	return out;
}

// Libretro thumbnail names replace these characters with '_'
std::string ToLibretroName(std::string name)
{
	for(char& ch : name) {
		if(std::string_view("&*/:`<>?\\|\"").find(ch) != std::string_view::npos) {
			ch = '_';
		}
	}
	return name;
}

// "After Burner (USA) (Unl)" -> "After Burner"
std::string CleanTitle(const std::string& title)
{
	std::string out;
	int depth = 0;
	for(char ch : title) {
		if(ch == '(' || ch == '[') {
			depth++;
		} else if(ch == ')' || ch == ']') {
			depth = std::max(0, depth - 1);
		} else if(depth == 0) {
			out.push_back(ch);
		}
	}
	while(!out.empty() && out.back() == ' ') {
		out.pop_back();
	}
	// No-Intro puts "The" at the end: "Legend of Zelda, The" -> "The Legend of Zelda"
	if(out.size() > 5 && out.compare(out.size() - 5, 5, ", The") == 0) {
		out = "The " + out.substr(0, out.size() - 5);
	}
	return out;
}

// status: the HTTP status code, or 0 when the request never got an answer (no network, blocked...)
std::optional<std::string> HttpGet(Windows::Web::Http::HttpClient& client, const std::string& url, int* status = nullptr)
{
	if(status) {
		*status = 0;
	}
	try {
		auto response = client.GetAsync(Windows::Foundation::Uri(to_hstring(url))).get();
		if(status) {
			*status = (int)response.StatusCode();
		}
		if(!response.IsSuccessStatusCode()) {
			return std::nullopt;
		}
		auto buffer = response.Content().ReadAsBufferAsync().get();
		uint32_t length = buffer.Length();
		if(length == 0 || length > kMaxBytes) {
			return std::nullopt;
		}
		std::string bytes(length, '\0');
		DataReader::FromBuffer(buffer).ReadBytes(array_view<uint8_t>((uint8_t*)bytes.data(), (uint8_t*)bytes.data() + length));
		return bytes;
	} catch(const hresult_error& e) {
		static std::atomic<int> logged = 0;
		if(logged++ < 3) {
			uwp::Log("[!] Download failed: " + to_string(e.message()) + " (" + url.substr(0, 60) + ")");
		}
		return std::nullopt;
	} catch(...) {
		return std::nullopt;
	}
}

// Reads the JSON string that starts at text[pos] == '"' (handles \n, \", \uXXXX...)
std::string ReadJsonString(const std::string& text, size_t pos)
{
	std::string out;
	for(pos++; pos < text.size() && text[pos] != '"'; pos++) {
		char ch = text[pos];
		if(ch != '\\' || pos + 1 >= text.size()) {
			out.push_back(ch);
			continue;
		}
		char esc = text[++pos];
		switch(esc) {
			case 'n': case 'r': case 't': out.push_back(' '); break;
			case 'u': {
				if(pos + 4 >= text.size()) {
					return out;
				}
				unsigned code = (unsigned)std::stoul(text.substr(pos + 1, 4), nullptr, 16);
				pos += 4;
				if(code >= 0xD800 && code <= 0xDBFF && pos + 6 < text.size() && text[pos + 1] == '\\' && text[pos + 2] == 'u') {
					unsigned low = (unsigned)std::stoul(text.substr(pos + 3, 4), nullptr, 16);
					code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
					pos += 6;
				}
				if(code < 0x80) {
					out.push_back((char)code);
				} else if(code < 0x800) {
					out.push_back((char)(0xC0 | (code >> 6)));
					out.push_back((char)(0x80 | (code & 0x3F)));
				} else if(code < 0x10000) {
					out.push_back((char)(0xE0 | (code >> 12)));
					out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
					out.push_back((char)(0x80 | (code & 0x3F)));
				} else {
					out.push_back((char)(0xF0 | (code >> 18)));
					out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
					out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
					out.push_back((char)(0x80 | (code & 0x3F)));
				}
				break;
			}
			default: out.push_back(esc); break;
		}
	}
	return out;
}

// nullopt when Wikipedia could not be reached (then nothing is saved, so it is tried again next time)
std::optional<std::string> WikipediaDescription(Windows::Web::Http::HttpClient& client, const std::string& title)
{
	std::string url = "https://en.wikipedia.org/w/api.php?action=query&format=json&generator=search&gsrsearch=" +
		UrlEncode(title + " NES video game") + "&gsrlimit=1&prop=extracts&exintro=1&explaintext=1&exchars=520";
	int status = 0;
	auto body = HttpGet(client, url, &status);
	if(!body) {
		uwp::Log("Wikipedia answered " + std::to_string(status) + " for " + title);
		return std::nullopt;
	}
	size_t key = body->find("\"extract\":");
	if(key == std::string::npos) {
		return std::string{};
	}
	size_t quote = body->find('"', key + 10);
	return quote == std::string::npos ? std::string{} : ReadJsonString(*body, quote);
}

bool Decode(const std::string& bytes, std::vector<uint8_t>& rgba, int& width, int& height, uint32_t maxEdge = kMaxEdge)
{
	try {
		InMemoryRandomAccessStream stream;
		DataWriter writer(stream);
		writer.WriteBytes(array_view<const uint8_t>((const uint8_t*)bytes.data(), (const uint8_t*)bytes.data() + bytes.size()));
		writer.StoreAsync().get();
		writer.DetachStream();
		stream.Seek(0);

		BitmapDecoder decoder = BitmapDecoder::CreateAsync(stream).get();
		uint32_t w = decoder.PixelWidth();
		uint32_t h = decoder.PixelHeight();
		if(w == 0 || h == 0) {
			return false;
		}
		double scale = std::min(1.0, (double)maxEdge / std::max(w, h));
		BitmapTransform transform;
		transform.ScaledWidth(std::max(1u, (uint32_t)(w * scale)));
		transform.ScaledHeight(std::max(1u, (uint32_t)(h * scale)));
		transform.InterpolationMode(BitmapInterpolationMode::Fant);
		PixelDataProvider pixels = decoder.GetPixelDataAsync(BitmapPixelFormat::Rgba8, BitmapAlphaMode::Straight, transform,
			ExifOrientationMode::IgnoreExifOrientation, ColorManagementMode::DoNotColorManage).get();
		com_array<uint8_t> data = pixels.DetachPixelData();
		width = (int)transform.ScaledWidth();
		height = (int)transform.ScaledHeight();
		if(data.size() < (size_t)width * height * 4) {
			return false;
		}
		rgba.assign(data.begin(), data.end());
		return true;
	} catch(...) {
		return false;
	}
}

std::optional<std::string> ReadFile(const fs::path& file)
{
	std::ifstream in(file, std::ios::binary);
	if(!in) {
		return std::nullopt;
	}
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

void WriteFile(const fs::path& file, const std::string& data)
{
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	out.write(data.data(), (std::streamsize)data.size());
}

std::string SafeFileName(std::string name)
{
	for(char& ch : name) {
		if(std::string_view("<>:\"/\\|?*").find(ch) != std::string_view::npos) {
			ch = '_';
		}
	}
	return name;
}

} // namespace

bool DecodeImage(const std::string& bytes, std::vector<uint8_t>& rgba, int& width, int& height, uint32_t maxEdge)
{
	return Decode(bytes, rgba, width, height, maxEdge);
}

Covers::Covers(fs::path folder) : _folder(std::move(folder))
{
	std::error_code ec;
	fs::create_directories(_folder, ec);
	_thread = std::thread([this] { Worker(); });
}

Covers::~Covers()
{
	{
		std::lock_guard lock(_lock);
		_stop = true;
		_queue.clear();
	}
	_wake.notify_all();
	if(_thread.joinable()) {
		_thread.detach(); // a download may be in progress; the app is closing anyway
	}
	for(auto& [title, info] : _info) {
		if(info.texture) {
			SDL_DestroyTexture(info.texture);
		}
	}
}

const Covers::Info& Covers::Get(SDL_Renderer* renderer, const GameEntry& game)
{
	auto [it, added] = _info.try_emplace(game.title);
	Info& info = it->second;
	if(added) {
		{
			std::lock_guard lock(_lock);
			// Newest request first: the player is looking at it now
			_queue.push_front(game);
		}
		_wake.notify_one();
	}
	if(!info.coverDone || !info.descriptionDone) {
		Result result;
		bool have = false;
		{
			std::lock_guard lock(_lock);
			auto found = _results.find(game.title);
			if(found != _results.end()) {
				result = std::move(found->second);
				_results.erase(found);
				have = true;
			}
		}
		if(have) {
			if(result.descriptionDone) {
				info.description = std::move(result.description);
				info.descriptionDone = true;
			}
			if(result.coverDone) {
				if(!result.rgba.empty()) {
					info.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, result.width, result.height);
					if(info.texture) {
						SDL_UpdateTexture(info.texture, nullptr, result.rgba.data(), result.width * 4);
						SDL_SetTextureScaleMode(info.texture, SDL_SCALEMODE_LINEAR);
						info.width = result.width;
						info.height = result.height;
					}
				}
				info.coverDone = true;
			}
		}
	}
	return info;
}

void Covers::Worker()
{
	uwp::InitThreadForWinRT();
	while(true) {
		GameEntry game;
		{
			std::unique_lock lock(_lock);
			_wake.wait(lock, [this] { return _stop || !_queue.empty(); });
			if(_stop) {
				return;
			}
			game = _queue.front();
			_queue.pop_front();
		}
		Fetch(game);
	}
}

void Covers::Fetch(const GameEntry& game)
{
	const std::string base = SafeFileName(game.title);
	const fs::path coverFile = _folder / FromU8(base + ".png");
	const fs::path noCoverFile = _folder / FromU8(base + ".noart"); // (".nocover" files from 0.1.0.15 are ignored)
	const fs::path textFile = _folder / FromU8(base + ".txt");
	const std::string clean = CleanTitle(game.title);

	Result result;
	std::optional<Windows::Web::Http::HttpClient> client;
	auto http = [&]() -> Windows::Web::Http::HttpClient& {
		if(!client) {
			client.emplace();
			// Wikipedia (and some image hosts) refuse requests that do not say which app is asking
			client->DefaultRequestHeaders().UserAgent().TryParseAdd(L"HDEight/0.1 (Xbox UWP NES emulator; github.com/pieceofadream1116-hub/HD-Eight)");
		}
		return *client;
	};

	// Description (an empty file from an earlier failed try counts as missing)
	auto text = ReadFile(textFile);
	if(text && !text->empty()) {
		result.description = *text;
	} else if(auto fetched = WikipediaDescription(http(), clean)) {
		result.description = *fetched;
		if(!fetched->empty()) {
			WriteFile(textFile, *fetched);
		}
	}
	result.descriptionDone = true;
	{
		std::lock_guard lock(_lock);
		_results[game.title] = result;
	}

	// Box art: the file name itself first (No-Intro names match Libretro's), then the clean title with regions
	std::optional<std::string> image = ReadFile(coverFile);
	std::error_code ec;
	if(!image && !fs::exists(noCoverFile, ec)) {
		std::vector<std::string> candidates{ToLibretroName(game.title)};
		for(const char* region : {"USA", "USA, Europe", "Europe", "Japan", "World"}) {
			candidates.push_back(ToLibretroName(clean) + " (" + region + ")");
		}
		bool allNotFound = true;
		for(const std::string& candidate : candidates) {
			int status = 0;
			image = HttpGet(http(), "https://thumbnails.libretro.com/Nintendo%20-%20Nintendo%20Entertainment%20System/Named_Boxarts/" +
				UrlEncode(candidate) + ".png", &status);
			if(image) {
				uwp::Log("Box art found: " + candidate);
				WriteFile(coverFile, *image);
				break;
			}
			if(status != 404) {
				allNotFound = false;
				uwp::Log("Box art site answered " + std::to_string(status) + " for " + candidate);
				break; // not a simple "no such picture": stop and try again another time
			}
		}
		if(!image && allNotFound) {
			// Only remember "no box art" when the site really said it has none
			WriteFile(noCoverFile, "");
			uwp::Log("No box art for " + game.title);
		}
	}
	if(image) {
		Decode(*image, result.rgba, result.width, result.height);
	}
	result.coverDone = true;
	{
		std::lock_guard lock(_lock);
		_results[game.title] = std::move(result);
	}
}
