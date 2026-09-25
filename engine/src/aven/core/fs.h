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

// Forward-slash relative path from `base` to `path` (portable across OSes, stored in scenes).
std::string relativePath(const stdfs::path& path, const stdfs::path& base);

// Lowercase extension including the dot, e.g. ".png".
std::string extension(const stdfs::path& path);

// Per-user writable directory for save data: e.g. ~/.local/share/Aven/<game> on Linux.
stdfs::path userDataDir(const std::string& gameName);

} // namespace aven::fs
