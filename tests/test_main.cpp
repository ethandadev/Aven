#include "test_framework.h"

#include "rynax/core/log.h"

#include <cstring>

namespace rynax::test {

namespace {
int g_failures = 0;
}

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_failures;
    std::cerr << "  " << file << ":" << line << ": " << message << "\n";
}

} // namespace rynax::test

int main(int argc, char** argv) {
    using namespace rynax::test;
    const char* filter = argc > 1 ? argv[1] : nullptr;
    rynax::Log::setEchoToStdout(false);
    int run = 0, failedTests = 0;
    for (auto& t : registry()) {
        if (filter && !std::strstr(t.name, filter))
            continue;
        int before = g_failures;
        ++run;
        t.fn();
        if (g_failures != before) {
            ++failedTests;
            std::cerr << "FAIL " << t.name << "\n";
        }
    }
    std::cout << run - failedTests << "/" << run << " tests passed\n";
    return failedTests == 0 ? 0 : 1;
}
