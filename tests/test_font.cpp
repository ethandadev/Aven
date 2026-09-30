#include "test_framework.h"

#include "rynax/assets/assets.h"
#include "rynax/render/font.h"

using namespace rynax;

// The built-in font has more than the characters prepared up front: other alphabets and accented
// letters are added the first time they're drawn, and ones it doesn't have fall back to '?'.
RYNAX_TEST(font_adds_characters_on_first_use) {
    Assets assets;
    Font& font = assets.defaultFont();
    CHECK(font.glyph('A') != nullptr);
    CHECK(font.glyph(0x0416) != nullptr); // Ж (Cyrillic)
    CHECK(font.glyph(0x03A9) != nullptr); // Ω (Greek)
    CHECK(font.glyph(0x0142) != nullptr); // ł (Polish)
    CHECK(font.glyph(0x4E2D) == nullptr); // 中: not in this font
    // Measuring uses them too: Cyrillic text has a width, like Latin text of the same length.
    CHECK(font.lineWidth("Привет", 32) > font.lineWidth("Hi", 32));
    int drawn = 0;
    font.layout("Ωmega\nЖук", 32, TextAlign::Left, [&](const Font::Glyph&, float, float, float) { ++drawn; });
    CHECK_EQ(drawn, 8); // two lines, every letter drawn
}
