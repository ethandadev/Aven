// Fuzzing: mangled scripts and JSON must give errors, never crashes or hangs. Seeds are the
// templates' scripts and scenes; each round flips, deletes, duplicates and splices bytes.
// A fixed seed keeps it repeatable; AVEN_FUZZ_ROUNDS=N runs longer (CI runs it under the
// sanitizers, where a crash or bad memory access fails the build).

#include "test_framework.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/json.h"
#include "aven/scene/scene.h"
#include "aven/script/intel.h"
#include "aven/script/translate.h"
#include "aven/script/vm.h"

#include <cstdlib>
#include <filesystem>
#include <random>

using namespace aven;

namespace {

std::vector<std::string> seeds(const std::vector<std::string>& extensions) {
    std::vector<std::string> out;
    namespace stdfs = std::filesystem;
    std::error_code ec;
    for (auto& e : stdfs::recursive_directory_iterator(stdfs::path(AVEN_SOURCE_DIR) / "templates", ec))
        for (auto& ext : extensions)
            if (e.path().extension() == ext)
                if (auto t = fs::readText(e.path()))
                    out.push_back(*t);
    return out;
}

std::string mutate(std::string s, std::mt19937& rng, const std::vector<std::string>& pool) {
    const char* tokens[] = {"(", ")", "[", "]", "{", "}", ":", ",", "\"", "'", "\n", "    ", "def ", "if ", "while ",
                            "for ", " in ", "return", "=", "==", "+", "**", "//", "not ", "lambda", "None", "\\", "#",
                            "1e309", "-", ".", "@", "\t", "else:", "elif ", "break", "self.", "\xff", "\x00"};
    int edits = 1 + static_cast<int>(rng() % 6);
    for (int i = 0; i < edits; ++i) {
        size_t at = s.empty() ? 0 : rng() % s.size();
        switch (rng() % 6) {
        case 0: // flip a byte
            if (!s.empty())
                s[at] = static_cast<char>(rng() % 256);
            break;
        case 1: // delete a run
            if (!s.empty())
                s.erase(at, 1 + rng() % 16);
            break;
        case 2: // insert a token
            s.insert(at, tokens[rng() % (sizeof tokens / sizeof *tokens)]);
            break;
        case 3: // duplicate a run
            if (!s.empty())
                s.insert(at, s.substr(at, 1 + rng() % 64));
            break;
        case 4: { // splice in part of another seed
            const std::string& other = pool[rng() % pool.size()];
            if (!other.empty()) {
                size_t from = rng() % other.size();
                s.insert(at, other.substr(from, 1 + rng() % 80));
            }
            break;
        }
        case 5: // deep nesting
            s.insert(at, std::string(1 + rng() % 600, "([{"[rng() % 3]));
            break;
        }
    }
    return s;
}

int rounds(int fallback) {
    const char* env = std::getenv("AVEN_FUZZ_ROUNDS");
    return env ? std::max(1, std::atoi(env)) : fallback;
}

} // namespace

AVEN_TEST(fuzz_easyscript_compile_and_run) {
    auto pool = seeds({".es"});
    CHECK(!pool.empty());
    std::mt19937 rng(1234);
    script::VM vm;
    vm.onPrint = [](const std::string&, const std::string&, int) {};
    int errors = 0;
    vm.onError = [&](const script::ScriptError&) { ++errors; };
    int n = rounds(400);
    for (int i = 0; i < n; ++i) {
        std::string src = mutate(pool[rng() % pool.size()], rng, pool);
        if (auto module = vm.compile(src, "fuzz.es")) {
            // It compiled: run its top level and its functions a little (the VM's step budget
            // stops endless loops).
            if (auto inst = vm.createInstance(module, script::Value(), "Fuzz"))
                for (const char* fn : {"on_start", "on_update", "run"})
                    vm.callFunction(inst, script::intern(fn), {script::Value(0.016)}, false);
        }
    }
    CHECK(errors > 0); // mangled code does produce errors
}

AVEN_TEST(fuzz_json_parse) {
    auto pool = seeds({".scene", ".json", ".aven", ".blocks", ".prefab"});
    CHECK(!pool.empty());
    std::mt19937 rng(99);
    int n = rounds(1500);
    int bad = 0;
    for (int i = 0; i < n; ++i) {
        std::string text = mutate(pool[rng() % pool.size()], rng, pool);
        std::string error;
        Json j = Json::parse(text, &error);
        bad += !error.empty();
        (void)j.dump(); // whatever came out can be written back
    }
    CHECK(bad > 0);
}

// The files people hand-edit (or that arrive damaged) go through more than the VM: the code ladder,
// the code editor's intelligence, blocks and scenes must all say "no" politely too.

AVEN_TEST(fuzz_code_ladder) {
    auto pool = seeds({".es"});
    std::mt19937 rng(4242);
    int n = rounds(300);
    const script::TargetLanguage langs[] = {script::TargetLanguage::Unity, script::TargetLanguage::Godot,
                                            script::TargetLanguage::Roblox, script::TargetLanguage::Unreal,
                                            script::TargetLanguage::AvenC};
    for (int i = 0; i < n; ++i) {
        std::string src = mutate(pool[rng() % pool.size()], rng, pool);
        (void)script::translate(src, langs[rng() % 5], {"Fuzz", false});
    }
}

AVEN_TEST(fuzz_code_intel) {
    auto pool = seeds({".es"});
    std::mt19937 rng(777);
    script::ProjectIndex index;
    index.fillFromEngine();
    script::CodeIntel intel(index, script::CodeKind::EasyScript);
    int n = rounds(200);
    for (int i = 0; i < n; ++i) {
        std::string src = mutate(pool[rng() % pool.size()], rng, pool);
        std::vector<std::string> lines;
        for (size_t start = 0;;) {
            size_t nl = src.find('\n', start);
            lines.push_back(src.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
            if (nl == std::string::npos)
                break;
            start = nl + 1;
        }
        (void)intel.diagnose(src);
        (void)intel.outline(lines);
        // The cursor anywhere, even past the end of a line (the editor can ask about any spot).
        for (int k = 0; k < 4; ++k) {
            int line = static_cast<int>(rng() % lines.size());
            int col = static_cast<int>(rng() % (lines[static_cast<size_t>(line)].size() + 3));
            int from = 0, to = 0, outLine = 0, outCol = 0;
            script::SignatureHelp help;
            (void)intel.suggest(lines, line, col, from, k % 2 == 0);
            (void)intel.hover(lines, line, col, from, to);
            (void)intel.signature(lines, line, col, help);
            (void)intel.definition(lines, line, col, outLine, outCol);
        }
    }
}

AVEN_TEST(fuzz_blocks_compile) {
    auto pool = seeds({".blocks"});
    CHECK(!pool.empty());
    std::mt19937 rng(31337);
    int n = rounds(400);
    for (int i = 0; i < n; ++i) {
        std::string text = mutate(pool[rng() % pool.size()], rng, pool);
        std::string error;
        (void)blocks::compileFile(text, &error);
    }
}

AVEN_TEST(fuzz_scene_load) {
    auto pool = seeds({".scene", ".prefab"});
    CHECK(!pool.empty());
    std::mt19937 rng(2024);
    int n = rounds(300);
    for (int i = 0; i < n; ++i) {
        std::string text = mutate(pool[rng() % pool.size()], rng, pool);
        std::string error;
        Json data = Json::parse(text, &error);
        if (!error.empty())
            continue;
        Scene scene;
        if (scene.load(data, &error)) {
            scene.updateTransforms();
            (void)scene.save().dump();
        }
        Scene other; // the same data as a prefab
        (void)other.instantiate(data, {});
        other.updateTransforms();
    }
}
