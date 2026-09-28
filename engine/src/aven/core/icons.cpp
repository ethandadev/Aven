#include "aven/core/icons.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

// From stb_image_write (compiled in aven/assets/stb_impl.cpp): PNG bytes, freed with free().
extern "C" unsigned char* stbi_write_png_to_mem(const unsigned char* pixels, int stride_bytes, int x, int y, int n,
                                                int* out_len);

namespace aven::icons {

std::vector<uint8_t> resize(const uint8_t* rgba, int w, int h, int size) {
    std::vector<uint8_t> out(static_cast<size_t>(size) * size * 4, 0);
    if (!rgba || w <= 0 || h <= 0 || size <= 0)
        return out;
    // The middle square of the picture.
    int side = std::min(w, h), x0 = (w - side) / 2, y0 = (h - side) / 2;
    float scale = static_cast<float>(side) / static_cast<float>(size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            // Average the source pixels this one covers (at least one), weighting colors by alpha so
            // transparent edges don't turn dark.
            float sx0 = x * scale, sx1 = (x + 1) * scale, sy0 = y * scale, sy1 = (y + 1) * scale;
            if (scale < 1) { // making it bigger: sample around the center instead
                float cx = (x + 0.5f) * scale - 0.5f, cy = (y + 0.5f) * scale - 0.5f;
                sx0 = std::floor(cx), sy0 = std::floor(cy), sx1 = sx0 + 1, sy1 = sy0 + 1;
            }
            double sum[4] = {0, 0, 0, 0}, weight = 0;
            for (int sy = static_cast<int>(sy0); sy < static_cast<int>(std::ceil(sy1)); ++sy)
                for (int sx = static_cast<int>(sx0); sx < static_cast<int>(std::ceil(sx1)); ++sx) {
                    int px = std::clamp(sx, 0, side - 1) + x0, py = std::clamp(sy, 0, side - 1) + y0;
                    const uint8_t* p = rgba + (static_cast<size_t>(py) * w + px) * 4;
                    double a = p[3] / 255.0;
                    sum[0] += p[0] * a;
                    sum[1] += p[1] * a;
                    sum[2] += p[2] * a;
                    sum[3] += a;
                    weight += 1;
                }
            uint8_t* o = &out[(static_cast<size_t>(y) * size + x) * 4];
            double a = weight > 0 ? sum[3] / weight : 0;
            for (int c = 0; c < 3; ++c)
                o[c] = static_cast<uint8_t>(sum[3] > 0 ? std::clamp(sum[c] / sum[3], 0.0, 255.0) : 0);
            o[3] = static_cast<uint8_t>(std::lround(a * 255));
        }
    return out;
}

std::vector<uint8_t> encodePng(const uint8_t* rgba, int w, int h) {
    int len = 0;
    unsigned char* png = stbi_write_png_to_mem(rgba, w * 4, w, h, 4, &len);
    std::vector<uint8_t> out;
    if (png) {
        out.assign(png, png + len);
        std::free(png);
    }
    return out;
}

namespace {

void put32be(std::vector<uint8_t>& v, uint32_t x) {
    for (int s = 24; s >= 0; s -= 8)
        v.push_back(static_cast<uint8_t>(x >> s));
}

} // namespace

std::vector<uint8_t> makeIcns(const std::map<int, std::vector<uint8_t>>& pngBySize) {
    // PNG-based entries macOS 10.7+ reads: the type says the size (and 2x versions reuse bigger PNGs).
    static const std::pair<const char*, int> kTypes[] = {{"icp4", 16},  {"icp5", 32},  {"icp6", 64},   {"ic07", 128},
                                                         {"ic08", 256}, {"ic09", 512}, {"ic10", 1024}, {"ic11", 32},
                                                         {"ic12", 64},  {"ic13", 256}, {"ic14", 512}};
    std::vector<uint8_t> body;
    for (auto& [type, size] : kTypes) {
        auto it = pngBySize.find(size);
        if (it == pngBySize.end() || it->second.empty())
            continue;
        body.insert(body.end(), type, type + 4);
        put32be(body, static_cast<uint32_t>(it->second.size() + 8));
        body.insert(body.end(), it->second.begin(), it->second.end());
    }
    std::vector<uint8_t> out = {'i', 'c', 'n', 's'};
    put32be(out, static_cast<uint32_t>(body.size() + 8));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

bool replaceExeIcon(std::vector<uint8_t>& exe, const std::vector<uint8_t>& png, std::string& error) {
    auto fits = [&](size_t at, size_t n) { return at <= exe.size() && n <= exe.size() - at; };
    auto u16 = [&](size_t at) -> uint32_t { return fits(at, 2) ? exe[at] | (exe[at + 1] << 8) : 0; };
    auto u32 = [&](size_t at) -> uint32_t { return fits(at, 4) ? u16(at) | (u16(at + 2) << 16) : 0; };
    auto set16 = [&](size_t at, uint32_t v) {
        exe[at] = static_cast<uint8_t>(v);
        exe[at + 1] = static_cast<uint8_t>(v >> 8);
    };
    auto set32 = [&](size_t at, uint32_t v) {
        set16(at, v & 0xFFFF);
        set16(at + 2, v >> 16);
    };
    auto fail = [&](const std::string& why) {
        error = why;
        return false;
    };
    if (!fits(0, 64) || exe[0] != 'M' || exe[1] != 'Z')
        return fail("it isn't a Windows program");
    size_t pe = u32(0x3C);
    if (!fits(pe, 24) || std::memcmp(&exe[pe], "PE\0\0", 4) != 0)
        return fail("it isn't a Windows program");
    uint32_t sections = u16(pe + 6), optSize = u16(pe + 20);
    size_t opt = pe + 24;
    uint32_t magic = u16(opt);
    size_t dirs = magic == 0x20b ? opt + 112 : magic == 0x10b ? opt + 96 : 0;
    if (!dirs || !fits(dirs, 3 * 8))
        return fail("its header is damaged");
    uint32_t resRva = u32(dirs + 2 * 8);
    size_t sectionTable = opt + optSize;
    auto toOffset = [&](uint32_t rva) -> size_t {
        for (uint32_t i = 0; i < sections; ++i) {
            size_t s = sectionTable + i * 40;
            if (!fits(s, 40))
                break;
            uint32_t va = u32(s + 12), size = std::max(u32(s + 8), u32(s + 16)), raw = u32(s + 20);
            if (rva >= va && rva < va + size)
                return raw + (rva - va);
        }
        return 0;
    };
    size_t root = toOffset(resRva);
    if (!resRva || !root)
        return fail("it has no icon to replace");
    // A resource directory: 16 bytes, then 8-byte entries (named ones first, then numbered).
    auto find = [&](size_t dir, int id, bool first) -> uint32_t {
        uint32_t count = u16(dir + 12) + u16(dir + 14);
        for (uint32_t i = 0; i < count; ++i) {
            size_t entry = dir + 16 + i * 8;
            if (first || (!(u32(entry) & 0x80000000u) && static_cast<int>(u32(entry)) == id))
                return u32(entry + 4);
        }
        return 0xFFFFFFFFu;
    };
    // type -> name (id) -> language -> data entry {rva, size}
    auto dataEntry = [&](int type, int id, bool anyId) -> size_t {
        uint32_t t = find(root, type, false);
        if (t == 0xFFFFFFFFu || !(t & 0x80000000u))
            return 0;
        uint32_t n = find(root + (t & 0x7FFFFFFF), id, anyId);
        if (n == 0xFFFFFFFFu || !(n & 0x80000000u))
            return 0;
        uint32_t l = find(root + (n & 0x7FFFFFFF), 0, true);
        if (l == 0xFFFFFFFFu || (l & 0x80000000u))
            return 0;
        size_t at = root + l;
        return fits(at, 16) ? at : 0;
    };
    size_t group = dataEntry(14, 0, true); // RT_GROUP_ICON
    if (!group)
        return fail("it has no icon to replace");
    size_t groupData = toOffset(u32(group));
    if (!groupData || !fits(groupData, 6) || u16(groupData + 4) == 0)
        return fail("its icon is damaged");
    // The group's first picture (Aven's player has just one).
    size_t entry = groupData + 6;
    if (!fits(entry, 14))
        return fail("its icon is damaged");
    uint32_t iconId = u16(entry + 12);
    size_t icon = dataEntry(3, static_cast<int>(iconId), false); // RT_ICON
    if (!icon)
        return fail("its icon is damaged");
    size_t iconData = toOffset(u32(icon));
    uint32_t room = u32(icon + 4);
    if (png.size() > room)
        return fail("the icon picture is too big (" + std::to_string(png.size() / 1024) + " KB; at most " +
                    std::to_string(room / 1024) + " KB fits)");
    if (!iconData || !fits(iconData, room))
        return fail("its icon is damaged");
    std::copy(png.begin(), png.end(), exe.begin() + static_cast<long>(iconData));
    std::fill(exe.begin() + static_cast<long>(iconData + png.size()), exe.begin() + static_cast<long>(iconData + room), 0);
    set32(icon + 4, static_cast<uint32_t>(png.size()));
    exe[entry] = 0;      // width 256
    exe[entry + 1] = 0;  // height 256
    exe[entry + 2] = 0;  // colors
    set16(entry + 4, 1); // planes
    set16(entry + 6, 32);
    set32(entry + 8, static_cast<uint32_t>(png.size()));
    return true;
}

} // namespace aven::icons
