#pragma once

#include "aven/math/math.h"
#include "aven/render/rhi.h"

#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aven {

// Decodes one UTF-8 character starting at text[i], advancing i. Invalid bytes become U+FFFD.
uint32_t decodeUtf8(std::string_view text, size_t& i);

enum class TextAlign : int32_t;

// A font rendered as a signed distance field atlas, so text stays sharp at any size.
class Font {
public:
    struct Glyph {
        float u0, v0, u1, v1; // atlas coordinates
        float x0, y0, x1, y1; // quad relative to the pen position, in base-size pixels (y down)
        float advance;
    };

    ~Font();
    bool load(rhi::Device* device, const uint8_t* ttf, size_t size);
    void release();

    const Glyph* glyph(uint32_t codepoint) const;
    float kerning(uint32_t a, uint32_t b) const;

    // All metrics are for text of the given size (height in any unit).
    float lineHeight(float size) const { return lineHeight_ * size / kBaseSize; }
    float ascent(float size) const { return ascent_ * size / kBaseSize; }
    float lineWidth(std::string_view line, float size) const;
    Vec2 measure(std::string_view text, float size) const;

    // Calls fn(glyph, x, y, scale) for each visible glyph. (x, y) is the pen position with
    // y growing downward from the top of the text block; alignment is applied per line.
    template <class Fn> void layout(std::string_view text, float size, TextAlign align, Fn&& fn) const;

    rhi::TextureHandle atlas() const { return atlas_; }
    bool valid() const { return atlas_.valid(); }
    static constexpr float kBaseSize = 48.0f;
    static constexpr float kEdge = 180.0f / 255.0f; // distance value at the glyph outline

private:
    rhi::Device* device_ = nullptr;
    rhi::TextureHandle atlas_;
    std::vector<uint8_t> ttf_;
    void* info_ = nullptr; // stbtt_fontinfo
    std::unordered_map<uint32_t, Glyph> glyphs_;
    float scale_ = 1;
    float ascent_ = 0, lineHeight_ = 0;
};

} // namespace aven

#include "aven/scene/components.h"

namespace aven {

template <class Fn> void Font::layout(std::string_view text, float size, TextAlign align, Fn&& fn) const {
    float scale = size / kBaseSize;
    float y = 0;
    size_t lineStart = 0;
    while (lineStart <= text.size()) {
        size_t lineEnd = text.find('\n', lineStart);
        if (lineEnd == std::string_view::npos)
            lineEnd = text.size();
        std::string_view line = text.substr(lineStart, lineEnd - lineStart);
        float width = lineWidth(line, size);
        float x = align == TextAlign::Left ? 0.0f : align == TextAlign::Center ? -width * 0.5f : -width;
        uint32_t prev = 0;
        for (size_t i = 0; i < line.size();) {
            uint32_t cp = decodeUtf8(line, i);
            const Glyph* g = glyph(cp);
            if (!g)
                g = glyph('?');
            if (!g)
                continue;
            if (prev)
                x += kerning(prev, cp) * scale;
            fn(*g, x, y + ascent_ * scale, scale);
            x += g->advance * scale;
            prev = cp;
        }
        y += lineHeight_ * scale;
        lineStart = lineEnd + 1;
    }
}

} // namespace aven
