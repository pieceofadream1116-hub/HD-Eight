// File access for the core (libretro-common's VFS functions), written with plain C/C++ file calls.
//
// libretro-common's own Windows version uses FindFirstFileW and GetFileAttributesW, which UWP apps cannot call, and
// its UWP version is written in C++/CX. These do the same job with _wfopen and std::filesystem, which work on the
// Xbox's USB drives thanks to the broadFileSystemAccess capability (the same way HD Eight opens its games).

#define NOMINMAX
#include <windows.h>

#include <vfs/vfs.h>
#include <vfs/vfs_implementation.h>

#include <io.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {

fs::path PathFromUtf8(const char* path)
{
	std::string s(path ? path : "");
	return fs::path(std::u8string(s.begin(), s.end()));
}

} // namespace

struct libretro_vfs_implementation_dir
{
	fs::directory_iterator it;
	fs::directory_iterator end;
	bool first = true;
	std::string name;
	bool isDir = false;
};

extern "C" {

libretro_vfs_implementation_file* retro_vfs_file_open_impl(const char* path, unsigned mode, unsigned hints)
{
	if(!path || !*path) {
		return nullptr;
	}
	const wchar_t* wmode = L"rb";
	if(mode == RETRO_VFS_FILE_ACCESS_WRITE) {
		wmode = L"wb";
	} else if(mode == RETRO_VFS_FILE_ACCESS_READ_WRITE) {
		wmode = L"w+b";
	} else if(mode == (RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING) ||
		mode == (RETRO_VFS_FILE_ACCESS_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING)) {
		wmode = L"r+b";
	}
	FILE* fp = _wfopen(PathFromUtf8(path).c_str(), wmode);
	if(!fp) {
		return nullptr;
	}
	auto* stream = static_cast<libretro_vfs_implementation_file*>(std::calloc(1, sizeof(libretro_vfs_implementation_file)));
	if(!stream) {
		std::fclose(fp);
		return nullptr;
	}
	stream->fp = fp;
	stream->hints = hints;
	stream->fd = -1;
	stream->orig_path = _strdup(path);
	_fseeki64(fp, 0, SEEK_END);
	stream->size = _ftelli64(fp);
	_fseeki64(fp, 0, SEEK_SET);
	return stream;
}

int retro_vfs_file_close_impl(libretro_vfs_implementation_file* stream)
{
	if(!stream) {
		return -1;
	}
	if(stream->fp) {
		std::fclose(stream->fp);
	}
	std::free(stream->orig_path);
	std::free(stream->buf);
	std::free(stream);
	return 0;
}

int retro_vfs_file_error_impl(libretro_vfs_implementation_file* stream)
{
	return stream && stream->fp ? std::ferror(stream->fp) : -1;
}

int64_t retro_vfs_file_size_impl(libretro_vfs_implementation_file* stream)
{
	return stream ? stream->size : 0;
}

int64_t retro_vfs_file_truncate_impl(libretro_vfs_implementation_file* stream, int64_t length)
{
	if(!stream || !stream->fp) {
		return -1;
	}
	std::fflush(stream->fp);
	if(_chsize_s(_fileno(stream->fp), length) != 0) {
		return -1;
	}
	stream->size = length;
	return 0;
}

int64_t retro_vfs_file_tell_impl(libretro_vfs_implementation_file* stream)
{
	return stream && stream->fp ? _ftelli64(stream->fp) : -1;
}

int64_t retro_vfs_file_seek_impl(libretro_vfs_implementation_file* stream, int64_t offset, int seek_position)
{
	if(!stream || !stream->fp) {
		return -1;
	}
	int whence = seek_position == RETRO_VFS_SEEK_POSITION_CURRENT ? SEEK_CUR :
		seek_position == RETRO_VFS_SEEK_POSITION_END ? SEEK_END : SEEK_SET;
	if(_fseeki64(stream->fp, offset, whence) != 0) {
		return -1;
	}
	return _ftelli64(stream->fp);
}

int64_t retro_vfs_file_read_impl(libretro_vfs_implementation_file* stream, void* s, uint64_t len)
{
	if(!stream || !stream->fp || !s) {
		return -1;
	}
	return (int64_t)std::fread(s, 1, (size_t)len, stream->fp);
}

int64_t retro_vfs_file_write_impl(libretro_vfs_implementation_file* stream, const void* s, uint64_t len)
{
	if(!stream || !stream->fp || !s) {
		return -1;
	}
	int64_t written = (int64_t)std::fwrite(s, 1, (size_t)len, stream->fp);
	int64_t position = _ftelli64(stream->fp);
	if(position > stream->size) {
		stream->size = position;
	}
	return written;
}

int retro_vfs_file_flush_impl(libretro_vfs_implementation_file* stream)
{
	return stream && stream->fp && std::fflush(stream->fp) == 0 ? 0 : -1;
}

int retro_vfs_file_remove_impl(const char* path)
{
	std::error_code ec;
	return fs::remove(PathFromUtf8(path), ec) && !ec ? 0 : -1;
}

int retro_vfs_file_rename_impl(const char* old_path, const char* new_path)
{
	std::error_code ec;
	fs::rename(PathFromUtf8(old_path), PathFromUtf8(new_path), ec);
	return ec ? -1 : 0;
}

const char* retro_vfs_file_get_path_impl(libretro_vfs_implementation_file* stream)
{
	return stream ? stream->orig_path : nullptr;
}

int retro_vfs_stat_impl(const char* path, int32_t* size)
{
	std::error_code ec;
	fs::path p = PathFromUtf8(path);
	auto status = fs::status(p, ec);
	if(ec || !fs::exists(status)) {
		return 0;
	}
	if(fs::is_directory(status)) {
		if(size) {
			*size = 0;
		}
		return RETRO_VFS_STAT_IS_VALID | RETRO_VFS_STAT_IS_DIRECTORY;
	}
	if(size) {
		auto bytes = fs::file_size(p, ec);
		*size = ec ? 0 : (int32_t)bytes;
	}
	return RETRO_VFS_STAT_IS_VALID;
}

int retro_vfs_mkdir_impl(const char* dir)
{
	std::error_code ec;
	fs::path p = PathFromUtf8(dir);
	if(fs::is_directory(p, ec)) {
		return -2; // already there
	}
	return fs::create_directories(p, ec) && !ec ? 0 : -1;
}

libretro_vfs_implementation_dir* retro_vfs_opendir_impl(const char* dir, bool include_hidden)
{
	(void)include_hidden;
	std::error_code ec;
	auto* stream = new libretro_vfs_implementation_dir();
	stream->it = fs::directory_iterator(PathFromUtf8(dir), fs::directory_options::skip_permission_denied, ec);
	if(ec) {
		delete stream;
		return nullptr;
	}
	return stream;
}

bool retro_vfs_readdir_impl(libretro_vfs_implementation_dir* dirstream)
{
	if(!dirstream) {
		return false;
	}
	std::error_code ec;
	if(!dirstream->first) {
		dirstream->it.increment(ec);
	}
	dirstream->first = false;
	if(ec || dirstream->it == dirstream->end) {
		return false;
	}
	auto name = dirstream->it->path().filename().u8string();
	dirstream->name.assign(name.begin(), name.end());
	dirstream->isDir = dirstream->it->is_directory(ec);
	return true;
}

const char* retro_vfs_dirent_get_name_impl(libretro_vfs_implementation_dir* dirstream)
{
	return dirstream ? dirstream->name.c_str() : nullptr;
}

bool retro_vfs_dirent_is_dir_impl(libretro_vfs_implementation_dir* dirstream)
{
	return dirstream && dirstream->isDir;
}

int retro_vfs_closedir_impl(libretro_vfs_implementation_dir* dirstream)
{
	delete dirstream;
	return 0;
}

} // extern "C"
