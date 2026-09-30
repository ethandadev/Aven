#include "rynax/core/zip.h"

#include "rynax/core/fs.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>

// From stb_image_write (compiled in rynax/assets/stb_impl.cpp): a zlib stream, freed with free().
extern "C" unsigned char* stbi_zlib_compress(unsigned char* data, int data_len, int* out_len, int quality);

namespace rynax::zip {

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

namespace {
struct CrcTable {
    uint32_t v[256];
    CrcTable() {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            v[i] = c;
        }
    }
};
} // namespace

uint32_t crc32(const void* data, size_t size) {
    static const CrcTable crcs; // made once, safely, even with several threads zipping at the same time
    const uint32_t* table = crcs.v;
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
        if (!alreadyCompressed(name) && size > 64 && size < 0x7FFFFFFFu) { // (the compressor counts in int)
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

namespace {

// Where a zip's bytes come from: memory, or a file read piece by piece (so a big zip isn't all in memory).
struct Source {
    virtual ~Source() = default;
    virtual uint64_t size() const = 0;
    virtual bool readAt(uint64_t at, void* out, size_t n) = 0;
};

struct MemorySource : Source {
    const std::vector<uint8_t>& bytes;
    explicit MemorySource(const std::vector<uint8_t>& b) : bytes(b) {}
    uint64_t size() const override { return bytes.size(); }
    bool readAt(uint64_t at, void* out, size_t n) override {
        if (at > bytes.size() || n > bytes.size() - at)
            return false;
        if (n)
            std::memcpy(out, bytes.data() + at, n);
        return true;
    }
};

struct FileSource : Source {
    std::ifstream in;
    uint64_t length = 0;
    explicit FileSource(const stdfs::path& path) : in(path, std::ios::binary) {
        std::error_code ec;
        length = stdfs::file_size(path, ec);
        if (ec)
            length = 0;
    }
    uint64_t size() const override { return length; }
    bool readAt(uint64_t at, void* out, size_t n) override {
        if (!in || at > length || n > length - at)
            return false;
        in.seekg(static_cast<std::streamoff>(at));
        in.read(static_cast<char*>(out), static_cast<std::streamsize>(n));
        return static_cast<size_t>(in.gcount()) == n;
    }
};

// One file in the zip, from its central directory.
struct Item {
    std::string name;
    uint16_t method = 0;
    uint32_t crc = 0, packedSize = 0, size = 0;
    uint64_t dataAt = 0;
    bool executable = false;
};

// Deflate can't shrink data more than about 1032 to 1: an entry claiming more is lying (a "zip bomb"),
// and is refused before any memory is set aside for it.
bool plausible(const Item& it) {
    if (it.method == 0)
        return it.packedSize == it.size;
    // ...and it never grows data by more than a little (stored blocks have 5 bytes of header per 64 KB).
    return static_cast<uint64_t>(it.size) <= 1100ull * it.packedSize + 1024 &&
           static_cast<uint64_t>(it.packedSize) <= static_cast<uint64_t>(it.size) + it.size / 64 + 1024;
}

// The list of files: names checked, sizes limited, nothing unpacked yet.
bool directory(Source& src, std::vector<Item>& items, std::string& error) {
    items.clear();
    const uint64_t n = src.size();
    if (n < 22) {
        error = "it isn't a zip file";
        return false;
    }
    // The end record is in the last 22 bytes, plus up to 64 KB of comment.
    uint64_t tailAt = n > 22 + 0xFFFF ? n - (22 + 0xFFFF) : 0;
    std::vector<uint8_t> tail(static_cast<size_t>(n - tailAt));
    if (!src.readAt(tailAt, tail.data(), tail.size())) {
        error = "the file couldn't be read";
        return false;
    }
    size_t end = std::string::npos;
    for (size_t i = tail.size() - 22 + 1; i-- > 0;)
        if (get32(&tail[i]) == kEnd) {
            end = i;
            break;
        }
    if (end == std::string::npos) {
        error = "it isn't a zip file";
        return false;
    }
    uint64_t endAt = tailAt + end;
    uint16_t count = get16(&tail[end + 10]);
    uint32_t centralSize = get32(&tail[end + 12]), centralAt = get32(&tail[end + 16]);
    if (centralAt == 0xFFFFFFFFu || count == 0xFFFF) {
        error = "it's a ZIP64 archive (over 4 GB), which isn't supported";
        return false;
    }
    if (static_cast<uint64_t>(centralAt) + centralSize > endAt) {
        error = "the zip file is damaged";
        return false;
    }
    std::vector<uint8_t> central(centralSize);
    if (!src.readAt(centralAt, central.data(), central.size())) {
        error = "the zip file is damaged";
        return false;
    }
    uint64_t total = 0;
    size_t at = 0;
    for (uint16_t e = 0; e < count; ++e) {
        if (at + 46 > central.size() || get32(&central[at]) != kCentralHeader) {
            error = "the zip file is damaged";
            return false;
        }
        const uint8_t* h = &central[at];
        uint16_t madeBy = get16(h + 4), flags = get16(h + 8);
        uint16_t nameLen = get16(h + 28), extraLen = get16(h + 30), commentLen = get16(h + 32);
        Item it;
        it.method = get16(h + 10);
        it.crc = get32(h + 16);
        it.packedSize = get32(h + 20);
        it.size = get32(h + 24);
        uint32_t attributes = get32(h + 38), local = get32(h + 42);
        if (at + 46 + nameLen > central.size()) {
            error = "the zip file is damaged";
            return false;
        }
        it.name.assign(reinterpret_cast<const char*>(h + 46), nameLen);
        at += 46u + nameLen + extraLen + commentLen;
        std::replace(it.name.begin(), it.name.end(), '\\', '/'); // some Windows tools
        if (it.name.empty() || it.name.back() == '/')
            continue; // a folder
        if (!safeName(it.name)) {
            error = "it has a file that would go outside its folder (" + it.name + ")";
            return false;
        }
        if (flags & 1) {
            error = "it's password-protected";
            return false;
        }
        if (it.method != 0 && it.method != 8) {
            error = "it uses a kind of compression Rynax can't read (" + it.name + ")";
            return false;
        }
        if (it.size > kMaxFile || (total += it.size) > kMaxTotal) {
            error = "it's too big to unpack";
            return false;
        }
        if (!plausible(it)) {
            error = "the zip file is damaged (" + it.name + " claims to unpack to far more than it could)";
            return false;
        }
        uint8_t localHeader[30];
        if (!src.readAt(local, localHeader, 30) || get32(localHeader) != kLocalHeader) {
            error = "the zip file is damaged";
            return false;
        }
        it.dataAt = static_cast<uint64_t>(local) + 30 + get16(localHeader + 26) + get16(localHeader + 28);
        if (it.dataAt + it.packedSize > n) {
            error = "the zip file is cut short";
            return false;
        }
        it.executable = (madeBy >> 8) == 3 && ((attributes >> 16) & 0111) != 0; // made on Unix, with an x bit
        items.push_back(std::move(it));
    }
    return true;
}

// One file's contents, checked against its CRC.
bool unpack(Source& src, const Item& it, std::vector<uint8_t>& out, std::string& error) {
    std::vector<uint8_t> packed(it.packedSize);
    if (!src.readAt(it.dataAt, packed.data(), packed.size())) {
        error = "the zip file is cut short";
        return false;
    }
    if (it.method == 0) {
        out = std::move(packed);
    } else {
        // Into a buffer of the promised size: a stream that unpacks to more fails instead of growing.
        out.assign(it.size, 0);
        if (it.size) {
            int got = stbi_zlib_decode_noheader_buffer(reinterpret_cast<char*>(out.data()), static_cast<int>(it.size),
                                                       reinterpret_cast<const char*>(packed.data()), static_cast<int>(packed.size()));
            if (got != static_cast<int>(it.size)) {
                error = "the zip file is damaged (" + it.name + ")";
                return false;
            }
        }
    }
    if (crc32(out.data(), out.size()) != it.crc) {
        error = "the zip file is damaged (" + it.name + " doesn't match its checksum)";
        return false;
    }
    return true;
}

} // namespace

bool read(const std::vector<uint8_t>& zip, std::vector<Entry>& files, std::string& error) {
    files.clear();
    MemorySource src(zip);
    std::vector<Item> items;
    if (!directory(src, items, error))
        return false;
    for (auto& it : items) {
        Entry entry;
        entry.name = it.name;
        entry.executable = it.executable;
        if (!unpack(src, it, entry.data, error))
            return false;
        files.push_back(std::move(entry));
    }
    return true;
}

bool list(const stdfs::path& zipPath, std::vector<std::string>& names, std::string& error) {
    names.clear();
    FileSource src(zipPath);
    std::vector<Item> items;
    if (!directory(src, items, error))
        return false;
    for (auto& it : items)
        names.push_back(it.name);
    return true;
}

bool extract(const stdfs::path& zipPath, const stdfs::path& folder, std::string& error) {
    FileSource src(zipPath);
    if (!src.in) {
        error = "the file couldn't be read";
        return false;
    }
    std::vector<Item> items;
    if (!directory(src, items, error))
        return false;
    if (items.empty()) {
        error = "the zip is empty";
        return false;
    }
    // "My Game/project.rynax", "My Game/scenes/..." unpack as "project.rynax", "scenes/...".
    std::string top = items[0].name.substr(0, items[0].name.find('/') + 1);
    bool shared = top.size() > 1 && top.back() == '/';
    for (auto& it : items)
        shared = shared && it.name.rfind(top, 0) == 0 && it.name.size() > top.size();
    std::error_code ec;
    stdfs::create_directories(folder, ec);
    // One file at a time: only one is ever in memory.
    std::vector<uint8_t> data;
    for (auto& it : items) {
        std::string name = shared ? it.name.substr(top.size()) : it.name;
        stdfs::path to = fs::insideFolder(folder, name);
        if (to.empty()) {
            error = "it has a file that would go outside its folder (" + it.name + ")";
            return false;
        }
        if (!unpack(src, it, data, error))
            return false;
        stdfs::create_directories(to.parent_path(), ec);
        if (!fs::writeBinary(to, data.data(), data.size())) {
            error = "couldn't write " + name;
            return false;
        }
        if (it.executable) {
            stdfs::permissions(to, stdfs::perms::owner_exec | stdfs::perms::group_exec | stdfs::perms::others_exec,
                               stdfs::perm_options::add, ec);
        }
    }
    return true;
}

} // namespace rynax::zip
