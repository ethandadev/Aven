#pragma once

// Base64, for binary data inside JSON files (terrain heights).

#include <cstdint>
#include <string>
#include <vector>

namespace aven {

std::string base64Encode(const uint8_t* data, size_t size);
std::vector<uint8_t> base64Decode(const std::string& text); // skips anything that isn't base64

} // namespace aven
