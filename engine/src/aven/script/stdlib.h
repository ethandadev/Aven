#pragma once

#include "aven/script/value.h"

#include <string>

namespace aven::script {

// Parses "red", "#ff8800", "#f80" or "ff8800" into RGBA 0..1. Returns false if unknown.
bool parseColorName(const std::string& text, double out[4]);
// Accepts a color value, a color name / hex text, or a list [r, g, b(, a)] in 0..255.
Value toColor(const Value& v, const char* context);
// Makes random() and friends repeatable (Bug replay seeds this when a game starts).
void seedRandom(uint32_t seed);

} // namespace aven::script
