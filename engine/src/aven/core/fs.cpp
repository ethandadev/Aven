#include "aven/core/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <limits.h>
#include <mach-o/dyld.h>
#else
#include <limits.h>
#include <unistd.h>
#endif

namespace aven::fs {

std::optional<std::string> readText(const stdfs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    std::string text = ss.str();
    // Normalize Windows line endings so scripts behave the same everywhere.
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::optional<std::vector<uint8_t>> readBinary(const stdfs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return std::nullopt;
    auto size = in.tellg();
    std::vector<uint8_t> data(static_cast<size_t>(size));
    in.seekg(0);
    if (size > 0 && !in.read(reinterpret_cast<char*>(data.data()), size))
        return std::nullopt;
    return data;
}

bool writeText(const stdfs::path& path, std::string_view text) {
    return writeBinary(path, text.data(), text.size());
}

bool writeBinary(const stdfs::path& path, const void* data, size_t size) {
    std::error_code ec;
    if (path.has_parent_path())
        stdfs::create_directories(path.parent_path(), ec);
    // Write to a temp file then rename, so a crash never leaves a half-written scene.
    stdfs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return false;
        out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        if (!out)
            return false;
    }
    stdfs::rename(tmp, path, ec);
    if (ec) {
        stdfs::remove(path, ec);
        stdfs::rename(tmp, path, ec);
    }
    return !ec;
}

bool exists(const stdfs::path& path) {
    std::error_code ec;
    return stdfs::exists(path, ec);
}

int64_t modifiedTime(const stdfs::path& path) {
    std::error_code ec;
    auto t = stdfs::last_write_time(path, ec);
    if (ec)
        return 0;
    return static_cast<int64_t>(t.time_since_epoch().count());
}

stdfs::path executableDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return stdfs::path(std::wstring(buf, n)).parent_path();
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0)
        return stdfs::canonical(buf).parent_path();
    return stdfs::current_path();
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0)
        return stdfs::path(std::string(buf, static_cast<size_t>(n))).parent_path();
    return stdfs::current_path();
#endif
}

std::string relativePath(const stdfs::path& path, const stdfs::path& base) {
    std::error_code ec;
    stdfs::path rel = stdfs::relative(path, base, ec);
    if (ec || rel.empty())
        rel = path;
    return rel.generic_string();
}

std::string extension(const stdfs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

stdfs::path userDataDir(const std::string& gameName) {
    stdfs::path base;
#if defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"))
        base = appdata;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        base = stdfs::path(home) / "Library" / "Application Support";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = stdfs::path(home) / ".local" / "share";
#endif
    if (base.empty())
        base = stdfs::temp_directory_path();
    std::string safe;
    for (char c : gameName)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c : '_';
    if (safe.empty())
        safe = "Game";
    return base / "Aven" / safe;
}

} // namespace aven::fs
