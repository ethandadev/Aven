#include "rynax/core/base64.h"

namespace rynax {

namespace {
const char kChars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < size)
            n |= static_cast<uint32_t>(data[i + 1]) << 8;
        if (i + 2 < size)
            n |= data[i + 2];
        out += kChars[(n >> 18) & 63];
        out += kChars[(n >> 12) & 63];
        out += i + 1 < size ? kChars[(n >> 6) & 63] : '=';
        out += i + 2 < size ? kChars[n & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    int value[256];
    for (int& v : value)
        v = -1;
    for (int i = 0; i < 64; ++i)
        value[static_cast<unsigned char>(kChars[i])] = i;
    std::vector<uint8_t> out;
    out.reserve(text.size() * 3 / 4);
    uint32_t buffer = 0;
    int bits = 0;
    for (char c : text) {
        int v = value[static_cast<unsigned char>(c)];
        if (v < 0)
            continue; // '=', whitespace
        buffer = (buffer << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

} // namespace rynax
