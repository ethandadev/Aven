// Fuzzing: mangled scripts and JSON must give errors, never crashes or hangs. Seeds are the
// templates' scripts and scenes; each round flips, deletes, duplicates and splices bytes.
// A fixed seed keeps it repeatable; RYNAX_FUZZ_ROUNDS=N runs longer (CI runs it under the
// sanitizers, where a crash or bad memory access fails the build).

#include "test_framework.h"

#include "rynax/blocks/blocks.h"
#include "rynax/core/fs.h"
#include "rynax/core/json.h"
#include "rynax/core/zip.h"
#include "rynax/scene/scene.h"
#include "rynax/script/intel.h"
#include "rynax/script/translate.h"
#include "rynax/script/vm.h"

#include <cstdlib>
#include <filesystem>
#include <random>

using namespace rynax;

namespace {

std::vector<std::string> seeds(const std::vector<std::string>& extensions) {
    std::vector<std::string> out;
    namespace stdfs = std::filesystem;
    std::error_code ec;
    for (auto& e : stdfs::recursive_directory_iterator(stdfs::path(RYNAX_SOURCE_DIR) / "templates", ec))
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
    const char* env = std::getenv("RYNAX_FUZZ_ROUNDS");
    return env ? std::max(1, std::atoi(env)) : fallback;
}

} // namespace

RYNAX_TEST(fuzz_easyscript_compile_and_run) {
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

RYNAX_TEST(fuzz_json_parse) {
    auto pool = seeds({".scene", ".json", ".rynax", ".blocks", ".prefab"});
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

RYNAX_TEST(fuzz_code_ladder) {
    auto pool = seeds({".es"});
    std::mt19937 rng(4242);
    int n = rounds(300);
    const script::TargetLanguage langs[] = {script::TargetLanguage::Unity, script::TargetLanguage::Godot,
                                            script::TargetLanguage::Roblox, script::TargetLanguage::Unreal,
                                            script::TargetLanguage::RynaxC};
    for (int i = 0; i < n; ++i) {
        std::string src = mutate(pool[rng() % pool.size()], rng, pool);
        (void)script::translate(src, langs[rng() % 5], {"Fuzz", false});
    }
}

RYNAX_TEST(fuzz_code_intel) {
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

RYNAX_TEST(fuzz_blocks_compile) {
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

RYNAX_TEST(fuzz_scene_load) {
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

// A game in a .zip someone sent: damaged zips (cut short, bytes changed, sizes that lie) are
// refused, and nothing is ever written outside the folder it's unpacked into.
RYNAX_TEST(fuzz_zip_read_and_extract) {
    namespace stdfs = std::filesystem;
    stdfs::path work = stdfs::temp_directory_path() / ("rynax_fuzz_zip_" + std::to_string(std::random_device{}()));
    std::error_code ec;
    stdfs::create_directories(work, ec);
    stdfs::path good = work / "game.zip";
    CHECK(zip::write(good, stdfs::path(RYNAX_SOURCE_DIR) / "templates" / "platformer"));
    auto original = fs::readBinary(good);
    CHECK(original && !original->empty());
    std::mt19937 rng(8080);
    int n = rounds(200);
    for (int i = 0; i < n && original; ++i) {
        std::vector<uint8_t> bytes = *original;
        int edits = 1 + static_cast<int>(rng() % 8);
        for (int k = 0; k < edits && !bytes.empty(); ++k) {
            size_t at = rng() % bytes.size();
            switch (rng() % 4) {
            case 0: bytes[at] = static_cast<uint8_t>(rng()); break;                                  // a changed byte
            case 1: bytes.resize(at); break;                                                         // cut short
            case 2: bytes.insert(bytes.begin() + static_cast<std::ptrdiff_t>(at), 0xFF); break;     // an extra byte
            case 3: for (size_t b = at; b < std::min(bytes.size(), at + 4); ++b) bytes[b] = 0xFF; break; // a huge size
            }
        }
        std::vector<zip::Entry> files;
        std::string error;
        (void)zip::read(bytes, files, error);
        if (i % 10 == 0) { // (on disk too, now and then: extract() reads a piece at a time)
            stdfs::path bad = work / "bad.zip", out = work / "out";
            fs::writeBinary(bad, bytes.data(), bytes.size());
            std::vector<std::string> names;
            (void)zip::list(bad, names, error);
            (void)zip::extract(bad, out, error);
            for (auto it = stdfs::recursive_directory_iterator(out, ec); !ec && it != stdfs::recursive_directory_iterator();
                 it.increment(ec))
                CHECK(fs::insideFolder(out, fs::relativePath(it->path(), out)) != stdfs::path());
            stdfs::remove_all(out, ec);
        }
    }
    stdfs::remove_all(work, ec);
}
