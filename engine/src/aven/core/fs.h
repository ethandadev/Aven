#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace aven::fs {

namespace stdfs = std::filesystem;

std::optional<std::string> readText(const stdfs::path& path);
std::optional<std::vector<uint8_t>> readBinary(const stdfs::path& path);
bool writeText(const stdfs::path& path, std::string_view text);
bool writeBinary(const stdfs::path& path, const void* data, size_t size);

bool exists(const stdfs::path& path);
// Modification time as an opaque tick count, 0 if the file is missing. Used for hot reload.
int64_t modifiedTime(const stdfs::path& path);

// Directory containing the running executable.
stdfs::path executableDir();
// Where the files that ship beside the program are (templates, data...): executableDir(), except
// inside a macOS app (Aven.app/Contents/MacOS), where they're in Contents/Resources.
stdfs::path resourceDir();

// Forward-slash relative path from `base` to `path` (portable across OSes, stored in scenes).
std::string relativePath(const stdfs::path& path, const stdfs::path& base);

// Lowercase extension including the dot, e.g. ".png".
std::string extension(const stdfs::path& path);

// `relative` joined onto `root`, or empty if it would leave the folder: absolute paths, "..",
// drive letters, backslashes, NULs, or links that resolve outside. For paths that come from
// games and web requests.
stdfs::path insideFolder(const stdfs::path& root, std::string_view relative);

// Web builds keep save data in the browser (IndexedDB): call after writing it. Elsewhere, nothing.
void persist();

// Per-user writable directory for save data: e.g. ~/.local/share/Aven/<game> on Linux.
stdfs::path userDataDir(const std::string& gameName);
// A name that works as a folder or file name on every system: letters, digits, - _ and spaces
// (not at the ends), at most 64 characters, never a Windows device name like "con". "Game" if empty.
std::string safeFolderName(const std::string& name);

} // namespace aven::fs
