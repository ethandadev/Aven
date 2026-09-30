#pragma once

// Tiny self-contained unit test harness.

#include <cmath>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace rynax::test {

struct TestCase {
    const char* name;
    void (*fn)();
};

std::vector<TestCase>& registry();
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, void (*fn)()) { registry().push_back({name, fn}); }
};

} // namespace rynax::test

#define RYNAX_TEST(name)                                                                                           \
    static void name();                                                                                           \
    static ::rynax::test::Registrar name##_registrar(#name, name);                                                 \
    static void name()

#define CHECK(expr)                                                                                               \
    do {                                                                                                          \
        if (!(expr))                                                                                              \
            ::rynax::test::reportFailure(__FILE__, __LINE__, "CHECK(" #expr ") failed");                           \
    } while (0)

#define CHECK_EQ(a, b)                                                                                            \
    do {                                                                                                          \
        auto&& va_ = (a);                                                                                         \
        auto&& vb_ = (b);                                                                                         \
        if (!(va_ == vb_)) {                                                                                      \
            std::ostringstream ss_;                                                                               \
            ss_ << "CHECK_EQ(" #a ", " #b ") failed: " << va_ << " != " << vb_;                                   \
            ::rynax::test::reportFailure(__FILE__, __LINE__, ss_.str());                                           \
        }                                                                                                         \
    } while (0)

#define CHECK_NEAR(a, b, eps)                                                                                     \
    do {                                                                                                          \
        double va_ = (a), vb_ = (b);                                                                              \
        if (std::abs(va_ - vb_) > (eps)) {                                                                        \
            std::ostringstream ss_;                                                                               \
            ss_ << "CHECK_NEAR(" #a ", " #b ") failed: " << va_ << " vs " << vb_;                                 \
            ::rynax::test::reportFailure(__FILE__, __LINE__, ss_.str());                                           \
        }                                                                                                         \
    } while (0)

#include <sstream>
