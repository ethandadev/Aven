#include "aven/core/zip.h"

#include "aven/core/fs.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>

// From stb_image_write (compiled in aven/assets/stb_impl.cpp): a zlib stream, freed with free().
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace aven::zip {

namespace {

constexpr uint32_t kLocalHeader = 0x04034b50, kCentralHeader = 0x02014b50, kEnd = 0x06054b50;
constexpr uint64_t kMaxFile = 1ull << 30;  // 1 GB for one file
constexpr uint64_t kMaxTotal = 4ull << 30; // 4 GB unpacked in all

void put16(std::string& s, uint16_t v) {
    s += static_cast<char>(v & 0xFF);
    s += static_cast<char>(v >> 8);
}
void put32(std::string& s, uint32_t v) {
    put16(s, static_cast<uint16_t>(v & 0xFFFF));
    put16(s, static_cast<uint16_t>(v >> 16));
}
uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t get32(const uint8_t* p) { return static_cast<uint32_t>(get16(p)) | (static_cast<uint32_t>(get16(p + 2)) << 16); }

// Files that are compressed already gain nothing from deflate.
bool alreadyCompressed(const std::string& name) {
    std::string ext = fs::extension(name);
    for (const char* e : {".png", ".jpg", ".jpeg", ".webp", ".ogg", ".mp3", ".flac", ".glb", ".zip", ".wasm", ".gz"})
        if (ext == e)
            return true;
    return false;
}

// A path that stays inside the folder: no "..", no "/x" or "C:x", no NUL.
bool safeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find(':') != std::string::npos || name.find('\0') != std::string::npos)
        return false;
    size_t start = 0;
    while (start <= name.size()) {
        size_t slash = name.find('/', start);
        std::string part = name.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..")
            return false;
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return true;
}

} // namespace

uint32_t crc32(const void* data, size_t size) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t crc = 0xFFFFFFFFu;
    const auto* p = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool write(const stdfs::path& zipPath, const stdfs::path& folder, const std::function<bool(const std::string&)>& include,
           const std::function<bool(const std::string&)>& executable) {
    // MS-DOS date and time: now.
    std::time_t now = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    uint16_t dosTime = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
    uint16_t dosDate = static_cast<uint16_t>(((std::max(local.tm_year, 80) - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);

    std::vector<std::string> names;
    std::error_code ec;
    for (auto it = stdfs::recursive_directory_iterator(folder, ec); !ec && it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fileEc;
        if (!it->is_regular_file(fileEc))
            continue;
        std::string name = stdfs::relative(it->path(), folder, fileEc).generic_string();
        if (fileEc || name.empty() || (include && !include(name)))
            continue;
        names.push_back(name);
    }
    if (ec)
        return false;
    std::sort(names.begin(), names.end()); // the same folder always makes the same zip
    if (names.size() > 0xFFFF)
        return false;

    std::string out, central;
    for (const std::string& name : names) {
        auto data = fs::readBinary(folder / stdfs::path(name));
        if (!data || data->size() > 0xFFFFFFFFull || out.size() > 0xFFFFFFFFull)
            return false;
        uint32_t crc = crc32(data->data(), data->size());
        uint32_t size = static_cast<uint32_t>(data->size());
        uint16_t method = 0;
        std::string packed;
        if (!alreadyCompressed(name) && size > 64) {
            int zlen = 0;
            if (unsigned char* z = stbi_zlib_compress(data->data(), static_cast<int>(size), &zlen, 8)) {
                // A zlib stream is 2 bytes of header, the deflate data, and 4 bytes of checksum.
                if (zlen > 6 && static_cast<uint32_t>(zlen - 6) < size) {
                    packed.assign(reinterpret_cast<const char*>(z) + 2, static_cast<size_t>(zlen - 6));
                    method = 8;
                }
                std::free(z);
            }
        }
        if (method == 0)
            packed.assign(reinterpret_cast<const char*>(data->data()), data->size());
        uint32_t offset = static_cast<uint32_t>(out.size());
        std::error_code permEc;
        bool program = executable ? executable(name)
                                  : (stdfs::status(folder / stdfs::path(name), permEc).permissions() & stdfs::perms::owner_exec) !=
                                        stdfs::perms::none;
        uint32_t unixMode = 0100000u | (program ? 0755u : 0644u); // a regular file, rwxr-xr-x or rw-r--r--
        auto common = [&](std::string& s) {
            put16(s, 0x0800); // UTF-8 names
            put16(s, method);
            put16(s, dosTime);
            put16(s, dosDate);
            put32(s, crc);
            put32(s, static_cast<uint32_t>(packed.size()));
            put32(s, size);
            put16(s, static_cast<uint16_t>(name.size()));
            put16(s, 0); // extra
        };
        put32(out, kLocalHeader);
        put16(out, 20);
        common(out);
        out += name;
        out += packed;
        put32(central, kCentralHeader);
        put16(central, (3 << 8) | 20); // made by: Unix, so the permissions below count
        put16(central, 20); // needed
        common(central);
        put16(central, 0); // comment
        put16(central, 0); // disk
        put16(central, 0); // internal attributes
        put32(central, unixMode << 16); // external attributes: the Unix permissions
        put32(central, offset);
        central += name;
    }
    if (out.size() > 0xFFFFFFFFull)
        return false;
    uint32_t centralOffset = static_cast<uint32_t>(out.size());
    out += central;
    put32(out, kEnd);
    put16(out, 0);
    put16(out, 0);
    put16(out, static_cast<uint16_t>(names.size()));
    put16(out, static_cast<uint16_t>(names.size()));
    put32(out, static_cast<uint32_t>(central.size()));
    put32(out, centralOffset);
    put16(out, 0);
    return fs::writeBinary(zipPath, out.data(), out.size());
}

bool read(const std::vector<uint8_t>& zip, std::vector<Entry>& files, std::string& error) {
    files.clear();
    const size_t n = zip.size();
    // The end record is in the last 22 bytes, plus up to 64 KB of comment.
    size_t end = std::string::npos;
    for (size_t i = n >= 22 ? n - 22 : std::string::npos; i != std::string::npos; --i) {
        if (get32(&zip[i]) == kEnd) {
            end = i;
            break;
        }
        if (i == 0 || n - i > 22 + 0xFFFF)
            break;
    }
    if (end == std::string::npos) {
        error = "it isn't a zip file";
        return false;
    }
    uint16_t count = get16(&zip[end + 10]);
    uint32_t centralSize = get32(&zip[end + 12]), at = get32(&zip[end + 16]);
    if (at == 0xFFFFFFFFu || count == 0xFFFF) {
        error = "it's a ZIP64 archive (over 4 GB), which isn't supported";
        return false;
    }
    if (static_cast<uint64_t>(at) + centralSize > end) {
        error = "the zip file is damaged";
        return false;
    }
    uint64_t total = 0;
    for (uint16_t e = 0; e < count; ++e) {
        if (static_cast<uint64_t>(at) + 46 > end || get32(&zip[at]) != kCentralHeader) {
            error = "the zip file is damaged";
            return false;
        }
        const uint8_t* h = &zip[at];
        uint16_t flags = get16(h + 8), method = get16(h + 10);
        uint32_t crc = get32(h + 16), packedSize = get32(h + 20), size = get32(h + 24);
        uint16_t nameLen = get16(h + 28), extraLen = get16(h + 30), commentLen = get16(h + 32);
        uint32_t local = get32(h + 42);
        if (static_cast<uint64_t>(at) + 46 + nameLen > end) {
            error = "the zip file is damaged";
            return false;
        }
        std::string name(reinterpret_cast<const char*>(h + 46), nameLen);
        at += 46u + nameLen + extraLen + commentLen;
        std::replace(name.begin(), name.end(), '\\', '/'); // some Windows tools
        if (name.empty() || name.back() == '/')
            continue; // a folder
        if (!safeName(name)) {
            error = "it has a file that would go outside its folder (" + name + ")";
            return false;
        }
        if (flags & 1) {
            error = "it's password-protected";
            return false;
        }
        if (method != 0 && method != 8) {
            error = "it uses a kind of compression Aven can't read (" + name + ")";
            return false;
        }
        if (size > kMaxFile || (total += size) > kMaxTotal) {
            error = "it's too big to unpack";
            return false;
        }
        if (static_cast<uint64_t>(local) + 30 > n || get32(&zip[local]) != kLocalHeader) {
            error = "the zip file is damaged";
            return false;
        }
        uint64_t dataAt = static_cast<uint64_t>(local) + 30 + get16(&zip[local + 26]) + get16(&zip[local + 28]);
        if (dataAt + packedSize > n) {
            error = "the zip file is cut short";
            return false;
        }
        Entry entry;
        entry.name = name;
        entry.data.resize(size);
        const char* packed = reinterpret_cast<const char*>(&zip[dataAt]);
        if (method == 0) {
            if (packedSize != size) {
                error = "the zip file is damaged";
                return false;
            }
            if (size)
                std::memcpy(entry.data.data(), packed, size);
        } else if (size) {
            // Into a buffer of the promised size: a stream that unpacks to more fails instead of growing.
            int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(entry.data.data()), static_cast<int>(size), packed,
                                                       static_cast<int>(packedSize));
            if (got != static_cast<int>(size)) {
                error = "the zip file is damaged (" + name + ")";
                return false;
            }
        }
        if (crc32(entry.data.data(), entry.data.size()) != crc) {
            error = "the zip file is damaged (" + name + " doesn't match its checksum)";
            return false;
        }
        files.push_back(std::move(entry));
    }
    return true;
}

bool extract(const stdfs::path& zipPath, const stdfs::path& folder, std::string& error) {
    auto bytes = fs::readBinary(zipPath);
    if (!bytes) {
        error = "the file couldn't be read";
        return false;
    }
    std::vector<Entry> files;
    if (!read(*bytes, files, error))
        return false;
    if (files.empty()) {
        error = "the zip is empty";
        return false;
    }
    // "My Game/project.aven", "My Game/scenes/..." unpack as "project.aven", "scenes/...".
    std::string top = files[0].name.substr(0, files[0].name.find('/') + 1);
    bool shared = top.size() > 1 && top.back() == '/';
    for (auto& f : files)
        shared = shared && f.name.rfind(top, 0) == 0 && f.name.size() > top.size();
    std::error_code ec;
    stdfs::create_directories(folder, ec);
    for (auto& f : files) {
        std::string name = shared ? f.name.substr(top.size()) : f.name;
        stdfs::path to = fs::insideFolder(folder, name);
        if (to.empty()) {
            error = "it has a file that would go outside its folder (" + f.name + ")";
            return false;
        }
        stdfs::create_directories(to.parent_path(), ec);
        if (!fs::writeBinary(to, f.data.data(), f.data.size())) {
            error = "couldn't write " + name;
            return false;
        }
    }
    return true;
}

} // namespace aven::zip
