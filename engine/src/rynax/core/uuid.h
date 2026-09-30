#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace rynax {

// 64-bit random identifier that keeps entity references stable across save/load.
struct UUID {
    uint64_t value = 0;

    static UUID generate();
    static UUID fromString(const std::string& hex);
    std::string toString() const;

    explicit operator bool() const { return value != 0; }
    bool operator==(const UUID&) const = default;
};

} // namespace rynax

template <> struct std::hash<rynax::UUID> {
    size_t operator()(const rynax::UUID& id) const noexcept { return std::hash<uint64_t>{}(id.value); }
};
