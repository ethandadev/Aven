#pragma once

#include <cstddef>

namespace aven::embedded {

// Files built into the engine (fonts, the app icon). Returns nullptr if `name` isn't embedded.
const unsigned char* find(const char* name, std::size_t* size);

} // namespace aven::embedded
