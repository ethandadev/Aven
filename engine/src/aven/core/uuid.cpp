#include "aven/core/uuid.h"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <random>

namespace aven {

UUID UUID::generate() {
    static std::mutex mutex;
    static std::mt19937_64 rng{std::random_device{}()};
    std::lock_guard lock(mutex);
    uint64_t v = 0;
    while (v == 0)
        v = rng();
    return {v};
}

UUID UUID::fromString(const std::string& hex) {
    return {std::strtoull(hex.c_str(), nullptr, 16)};
}

std::string UUID::toString() const {
    char buf[17];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(value));
    return buf;
}

} // namespace aven
