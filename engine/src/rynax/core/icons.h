#pragma once

// App icons for exported games: resizing a picture, and the icon formats each system wants.
//   Windows: the icon inside the .exe (replaceExeIcon)
//   macOS:   an .icns file in the .app (makeIcns)
//   Linux:   a PNG beside a .desktop file

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rynax::icons {

// A size x size copy of an RGBA picture, center-cropped to a square first. Smooth in both directions.
std::vector<uint8_t> resize(const uint8_t* rgba, int w, int h, int size);
// PNG file bytes for RGBA pixels.
std::vector<uint8_t> encodePng(const uint8_t* rgba, int w, int h);
// An .icns file from PNGs keyed by pixel size (16...1024; sizes it doesn't use are ignored).
std::vector<uint8_t> makeIcns(const std::map<int, std::vector<uint8_t>>& pngBySize);
// Swaps the icon inside a Windows program (a PE file's first icon group) for a 256 x 256 PNG. The
// program must have room for it: Rynax's player carries a placeholder with 300 KB to spare.
bool replaceExeIcon(std::vector<uint8_t>& exe, const std::vector<uint8_t>& png256, std::string& error);

} // namespace rynax::icons
