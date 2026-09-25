#include "test_framework.h"

#include "aven/core/fs.h"
#include "aven/script/vm.h"

#include <filesystem>
#include <sstream>

using namespace aven;

// Every ```easyscript example in docs/ must compile, so the guides can't teach broken code.
AVEN_TEST(docs_examples_compile) {
    int examples = 0;
    for (auto& entry : std::filesystem::directory_iterator(std::filesystem::path(AVEN_SOURCE_DIR) / "docs")) {
        if (entry.path().extension() != ".md")
            continue;
        auto text = fs::readText(entry.path());
        CHECK(text.has_value());
        std::istringstream in(*text);
        std::string line, code;
        bool inside = false;
        int start = 0, n = 0;
        while (std::getline(in, line)) {
            ++n;
            if (!inside && line.rfind("```easyscript", 0) == 0) {
                inside = true;
                code.clear();
                start = n;
            } else if (inside && line.rfind("```", 0) == 0) {
                inside = false;
                script::VM vm;
                std::string error;
                vm.onError = [&](const script::ScriptError& e) { error = e.what() + std::string(" (line ") + std::to_string(e.line) + ")"; };
                bool ok = vm.compile(code, entry.path().filename().string()) != nullptr;
                if (!ok)
                    std::printf("  %s:%d: %s\n", entry.path().filename().string().c_str(), start, error.c_str());
                CHECK(ok);
                ++examples;
            } else if (inside) {
                code += line + "\n";
            }
        }
    }
    CHECK(examples >= 10);
}
