#include "aven/render/font.h"

#include "aven/core/log.h"

#include <stb_truetype.h>

#include <algorithm>
#include <cstring>

namespace aven {

uint32_t decodeUtf8(std::string_view s, size_t& i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    auto cont = [&](size_t k) -> uint32_t {
        if (i + k >= s.size())
            return 0x80000000u;
        unsigned char b = static_cast<unsigned char>(s[i + k]);
        return (b & 0xC0) == 0x80 ? (b & 0x3Fu) : 0x80000000u;
    };
    uint32_t cp;
    size_t len;
    if (c < 0x80) {
        cp = c;
        len = 1;
    } else if ((c >> 5) == 6) {
        cp = ((c & 0x1Fu) << 6) | cont(1);
        len = 2;
    } else if ((c >> 4) == 14) {
        cp = ((c & 0x0Fu) << 12) | (cont(1) << 6) | cont(2);
        len = 3;
    } else if ((c >> 3) == 30) {
        cp = ((c & 0x07u) << 18) | (cont(1) << 12) | (cont(2) << 6) | cont(3);
        len = 4;
    } else {
        cp = 0xFFFD;
        len = 1;
    }
    if (cp & 0x80000000u) {
        cp = 0xFFFD;
        len = 1;
    }
    i += len;
    return cp;
}

Font::~Font() {
    release();
}

void Font::release() {
    if (device_ && atlas_.valid())
        device_->destroy(atlas_);
    atlas_ = {};
    delete static_cast<stbtt_fontinfo*>(info_);
    info_ = nullptr;
    glyphs_.clear();
}

bool Font::load(rhi::Device* device, const uint8_t* ttf, size_t size) {
    release();
    device_ = device;
    ttf_.assign(ttf, ttf + size);
    auto* info = new stbtt_fontinfo();
    info_ = info;
    if (!stbtt_InitFont(info, ttf_.data(), stbtt_GetFontOffsetForIndex(ttf_.data(), 0))) {
        Log::error("This font file could not be read.");
        return false;
    }
    scale_ = stbtt_ScaleForPixelHeight(info, kBaseSize);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &gap);
    ascent_ = ascent * scale_;
    lineHeight_ = (ascent - descent + gap) * scale_;

    std::vector<uint32_t> codepoints;
    for (uint32_t c = 32; c < 127; ++c)
        codepoints.push_back(c);
    for (uint32_t c = 160; c < 256; ++c)
        codepoints.push_back(c);
    for (uint32_t c : {0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u, 0x20ACu, 0x2190u, 0x2191u, 0x2192u,
                       0x2193u, 0x2605u, 0x2606u, 0x2665u, 0x2713u, 0x00D7u, 0xFFFDu})
        codepoints.push_back(c);

    const int atlasSize = 1024, padding = 6;
    const unsigned char onEdge = 180;
    const float distScale = 180.0f / padding;
    std::vector<uint8_t> pixels(static_cast<size_t>(atlasSize * atlasSize), 0);
    int penX = 1, penY = 1, rowHeight = 0;

    for (uint32_t cp : codepoints) {
        int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(cp));
        if (glyphIndex == 0 && cp != ' ')
            continue;
        int advance, lsb;
        stbtt_GetGlyphHMetrics(info, glyphIndex, &advance, &lsb);
        Glyph g{};
        g.advance = advance * scale_;
        int w = 0, h = 0, xoff = 0, yoff = 0;
        unsigned char* sdf =
            stbtt_GetGlyphSDF(info, scale_, glyphIndex, padding, onEdge, distScale, &w, &h, &xoff, &yoff);
        if (sdf && w > 0 && h > 0) {
            if (penX + w + 1 >= atlasSize) {
                penX = 1;
                penY += rowHeight + 1;
                rowHeight = 0;
            }
            if (penY + h + 1 >= atlasSize) {
                stbtt_FreeSDF(sdf, nullptr);
                Log::warn("Font atlas is full; some characters won't show.");
                break;
            }
            for (int row = 0; row < h; ++row)
                std::memcpy(&pixels[static_cast<size_t>((penY + row) * atlasSize + penX)], sdf + row * w,
                            static_cast<size_t>(w));
            g.u0 = static_cast<float>(penX) / atlasSize;
            g.v0 = static_cast<float>(penY) / atlasSize;
            g.u1 = static_cast<float>(penX + w) / atlasSize;
            g.v1 = static_cast<float>(penY + h) / atlasSize;
            g.x0 = static_cast<float>(xoff);
            g.y0 = static_cast<float>(yoff);
            g.x1 = static_cast<float>(xoff + w);
            g.y1 = static_cast<float>(yoff + h);
            penX += w + 1;
            rowHeight = std::max(rowHeight, h);
        }
        if (sdf)
            stbtt_FreeSDF(sdf, nullptr);
        glyphs_[cp] = g;
    }

    if (device_) {
        rhi::TextureDesc td;
        td.width = atlasSize;
        td.height = atlasSize;
        td.format = rhi::PixelFormat::R8;
        td.filter = rhi::Filter::Linear;
        td.data = pixels.data();
        td.label = "font atlas";
        atlas_ = device_->createTexture(td);
    }
    return true;
}

const Font::Glyph* Font::glyph(uint32_t codepoint) const {
    auto it = glyphs_.find(codepoint);
    return it == glyphs_.end() ? nullptr : &it->second;
}

float Font::kerning(uint32_t a, uint32_t b) const {
    if (!info_)
        return 0;
    return stbtt_GetCodepointKernAdvance(static_cast<stbtt_fontinfo*>(info_), static_cast<int>(a),
                                         static_cast<int>(b)) *
           scale_;
}

float Font::lineWidth(std::string_view line, float size) const {
    float scale = size / kBaseSize;
    float w = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < line.size();) {
        uint32_t cp = decodeUtf8(line, i);
        const Glyph* g = glyph(cp);
        if (!g)
            g = glyph('?');
        if (!g)
            continue;
        if (prev)
            w += kerning(prev, cp) * scale;
        w += g->advance * scale;
        prev = cp;
    }
    return w;
}

Vec2 Font::measure(std::string_view text, float size) const {
    float width = 0;
    int lines = 0;
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos)
            end = text.size();
        width = std::max(width, lineWidth(text.substr(start, end - start), size));
        ++lines;
        start = end + 1;
    }
    return {width, lines * lineHeight(size)};
}

} // namespace aven
