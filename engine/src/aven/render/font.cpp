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
    missing_.clear();
    penX_ = penY_ = 1;
    rowHeight_ = 0;
    full_ = false;
}

namespace {
constexpr int kPadding = 6;
constexpr unsigned char kOnEdge = 180;
constexpr float kDistScale = 180.0f / kPadding;
} // namespace

const Font::Glyph* Font::addGlyph(uint32_t cp, std::vector<uint8_t>* pixels) const {
    auto* info = static_cast<stbtt_fontinfo*>(info_);
    if (!info || missing_.count(cp))
        return nullptr;
    int glyphIndex = stbtt_FindGlyphIndex(info, static_cast<int>(cp));
    if (glyphIndex == 0 && cp != ' ') {
        missing_[cp] = true;
        return nullptr;
    }
    int advance, lsb;
    stbtt_GetGlyphHMetrics(info, glyphIndex, &advance, &lsb);
    Glyph g{};
    g.advance = advance * scale_;
    int w = 0, h = 0, xoff = 0, yoff = 0;
    unsigned char* sdf = stbtt_GetGlyphSDF(info, scale_, glyphIndex, kPadding, kOnEdge, kDistScale, &w, &h, &xoff, &yoff);
    if (sdf && w > 0 && h > 0) {
        if (penX_ + w + 1 >= kAtlasSize) {
            penX_ = 1;
            penY_ += rowHeight_ + 1;
            rowHeight_ = 0;
        }
        if (full_ || penY_ + h + 1 >= kAtlasSize) {
            stbtt_FreeSDF(sdf, nullptr);
            if (!full_)
                Log::warn("The font has no room for more characters; some won't show.");
            full_ = true;
            missing_[cp] = true;
            return nullptr;
        }
        if (pixels) {
            for (int row = 0; row < h; ++row)
                std::memcpy(&(*pixels)[static_cast<size_t>(penY_ + row) * kAtlasSize + static_cast<size_t>(penX_)], sdf + row * w,
                            static_cast<size_t>(w));
        } else if (device_ && atlas_.valid()) {
            device_->updateTextureRegion(atlas_, penX_, penY_, w, h, sdf);
        }
        g.u0 = static_cast<float>(penX_) / kAtlasSize;
        g.v0 = static_cast<float>(penY_) / kAtlasSize;
        g.u1 = static_cast<float>(penX_ + w) / kAtlasSize;
        g.v1 = static_cast<float>(penY_ + h) / kAtlasSize;
        g.x0 = static_cast<float>(xoff);
        g.y0 = static_cast<float>(yoff);
        g.x1 = static_cast<float>(xoff + w);
        g.y1 = static_cast<float>(yoff + h);
        penX_ += w + 1;
        rowHeight_ = std::max(rowHeight_, h);
    }
    if (sdf)
        stbtt_FreeSDF(sdf, nullptr);
    return &(glyphs_[cp] = g);
}

bool Font::load(rhi::Device* device, const uint8_t* ttf, size_t size) {
    release();
    device_ = device;
    ttf_.assign(ttf, ttf + size);
    auto* info = new stbtt_fontinfo();
    int offset = ttf_.empty() ? -1 : stbtt_GetFontOffsetForIndex(ttf_.data(), 0);
    if (offset < 0 || !stbtt_InitFont(info, ttf_.data(), offset)) {
        delete info; // (a half-set-up font would be read later: kerning, glyphs)
        Log::error("This font file could not be read.");
        return false;
    }
    info_ = info;
    scale_ = stbtt_ScaleForPixelHeight(info, kBaseSize);
    int ascent, descent, gap;
    stbtt_GetFontVMetrics(info, &ascent, &descent, &gap);
    ascent_ = ascent * scale_;
    lineHeight_ = (ascent - descent + gap) * scale_;

    // The common characters go in straight away; any others the first time they're drawn.
    std::vector<uint8_t> pixels(static_cast<size_t>(kAtlasSize) * kAtlasSize, 0);
    for (uint32_t c = 32; c < 127; ++c)
        addGlyph(c, &pixels);
    for (uint32_t c = 160; c < 256; ++c)
        addGlyph(c, &pixels);
    for (uint32_t c : {0x2018u, 0x2019u, 0x201Cu, 0x201Du, 0x2022u, 0x2026u, 0x20ACu, 0x2190u, 0x2191u, 0x2192u,
                       0x2193u, 0x2605u, 0x2606u, 0x2665u, 0x2713u, 0x00D7u, 0xFFFDu})
        addGlyph(c, &pixels);

    if (device_) {
        rhi::TextureDesc td;
        td.width = kAtlasSize;
        td.height = kAtlasSize;
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
    if (it != glyphs_.end())
        return &it->second;
    return addGlyph(codepoint, nullptr);
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
