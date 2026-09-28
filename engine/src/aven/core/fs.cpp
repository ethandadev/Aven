#include "aven/core/fs.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
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
    std::error_code ec;
    if (stdfs::is_directory(path, ec))
        return std::nullopt; // (a folder opens as a stream on some systems, and reads as nothing)
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
    std::error_code ec;
    if (stdfs::is_directory(path, ec))
        return std::nullopt;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in)
        return std::nullopt;
    auto size = in.tellg();
    if (size < 0)
        return std::nullopt; // not something with a size (a device, a pipe)
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
        out.close(); // (flushes: a full disk shows up here)
        if (!out) {
            stdfs::remove(tmp, ec);
            return false;
        }
    }
    stdfs::rename(tmp, path, ec);
    if (ec) {
        // Renaming over the old file can fail (another program has it open on Windows, a
        // different drive): copy instead. The old file stays as it was if that fails too.
        ec.clear();
        stdfs::copy_file(tmp, path, stdfs::copy_options::overwrite_existing, ec);
        std::error_code ignored;
        stdfs::remove(tmp, ignored);
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
    // Paths can be longer than MAX_PATH: grow until the whole name fits.
    std::wstring buf(MAX_PATH, L'\0');
    for (;;) {
        DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0)
            return stdfs::current_path();
        if (n < buf.size())
            return stdfs::path(buf.substr(0, n)).parent_path();
        if (buf.size() >= 32768)
            return stdfs::current_path();
        buf.resize(buf.size() * 2);
    }
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof buf;
    if (_NSGetExecutablePath(buf, &size) == 0) {
        std::error_code ec;
        stdfs::path exe = stdfs::canonical(buf, ec);
        return (ec ? stdfs::path(buf) : exe).parent_path();
    }
    return stdfs::current_path();
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n > 0)
        return stdfs::path(std::string(buf, static_cast<size_t>(n))).parent_path();
    return stdfs::current_path();
#endif
}

stdfs::path resourceDir() {
    stdfs::path exe = executableDir();
    std::error_code ec;
    if (exe.filename() == "MacOS" && exe.parent_path().filename() == "Contents" &&
        stdfs::is_directory(exe.parent_path() / "Resources", ec))
        return exe.parent_path() / "Resources";
    return exe;
}

std::string relativePath(const stdfs::path& path, const stdfs::path& base) {
    std::error_code ec;
    stdfs::path rel = stdfs::relative(path, base, ec);
    if (ec || rel.empty())
        rel = path;
    return rel.generic_string();
}

std::string toUtf8(const stdfs::path& path) {
    std::u8string s = path.u8string();
    return std::string(s.begin(), s.end());
}

stdfs::path fromUtf8(std::string_view utf8) {
    return stdfs::path(std::u8string(utf8.begin(), utf8.end()));
}

std::string extension(const stdfs::path& path) {
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

stdfs::path insideFolder(const stdfs::path& root, std::string_view relative) {
    if (relative.empty() || relative.find('\0') != std::string_view::npos || relative.find(':') != std::string_view::npos ||
        relative.find('\\') != std::string_view::npos || relative.front() == '/')
        return {};
    stdfs::path rel = stdfs::path(std::string(relative)).lexically_normal();
    if (rel.is_absolute() || rel.has_root_name() || rel.has_root_directory())
        return {};
    for (const auto& part : rel)
        if (part == "..")
            return {};
    std::error_code ec;
    stdfs::path base = stdfs::weakly_canonical(root, ec);
    if (ec)
        return {};
    stdfs::path real = stdfs::weakly_canonical(base / rel, ec);
    if (ec)
        return {};
    // Symbolic links can still point elsewhere: compare the resolved paths part by part.
    auto r = base.begin(), f = real.begin();
    for (; r != base.end(); ++r, ++f) {
        if (r->empty() && std::next(r) == base.end())
            break; // a trailing separator
        if (f == real.end() || *r != *f)
            return {};
    }
    return real;
}

void persist() {
#ifdef __EMSCRIPTEN__
    // The page mounts IndexedDB at /saves (player/web/index.html); this copies the changes into it.
    EM_ASM({ FS.syncfs(false, function(err) { if (err) console.warn('Saving failed', err); }); });
#endif
}

std::string safeFolderName(const std::string& name) {
    std::string safe;
    for (char c : name)
        safe += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c : '_';
    // Windows drops spaces at the ends of folder names, and some names are devices, not folders.
    while (!safe.empty() && safe.back() == ' ')
        safe.pop_back();
    while (!safe.empty() && safe.front() == ' ')
        safe.erase(safe.begin());
    if (safe.size() > 64)
        safe.resize(64);
    std::string lower = safe;
    for (char& c : lower)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    static const char* reserved[] = {"con", "prn", "aux", "nul", "com1", "com2", "com3", "com4", "com5", "com6", "com7",
                                     "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"};
    for (const char* r : reserved)
        if (lower == r)
            safe += '_';
    return safe.empty() ? "Game" : safe;
}

stdfs::path userDataDir(const std::string& gameName) {
    stdfs::path base;
#if defined(_WIN32)
    if (const wchar_t* appdata = _wgetenv(L"APPDATA")) // (wide: a name like 张伟 doesn't fit the ANSI code page)
        base = appdata;
#elif defined(__APPLE__)
    if (const char* home = std::getenv("HOME"))
        base = stdfs::path(home) / "Library" / "Application Support";
#elif defined(__EMSCRIPTEN__)
    base = "/saves";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        base = xdg;
    else if (const char* home = std::getenv("HOME"))
        base = stdfs::path(home) / ".local" / "share";
#endif
    if (base.empty())
        base = stdfs::temp_directory_path();
    // Before 0.4.1 names weren't trimmed or shortened: a folder made then keeps being used.
    std::string legacy;
    for (char c : gameName)
        legacy += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ' ') ? c : '_';
    std::string safe = safeFolderName(gameName);
    std::error_code ec;
    if (!legacy.empty() && legacy != safe && stdfs::is_directory(base / "Aven" / legacy, ec))
        return base / "Aven" / legacy;
    return base / "Aven" / safe;
}

} // namespace aven::fs
