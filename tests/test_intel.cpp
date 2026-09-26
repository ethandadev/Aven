#include "test_framework.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/json.h"
#include "aven/script/intel.h"

#include <filesystem>

using namespace aven;
using namespace aven::script;

namespace {

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> lines;
    for (size_t start = 0;;) {
        size_t nl = text.find('\n', start);
        lines.push_back(text.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return lines;
}

const ProjectIndex& engineIndex() {
    static ProjectIndex ix = [] {
        ProjectIndex i;
        i.fillFromEngine();
        if (auto text = fs::readText(std::filesystem::path(AVEN_SOURCE_DIR) / "editor/data/api_docs.json")) {
            Json docs = Json::parse(*text);
            for (auto& m : docs.members())
                i.docs[m.key] = m.value.asString();
        }
        if (auto header = fs::readText(std::filesystem::path(AVEN_SOURCE_DIR) / "sdk/include/aven.h"))
            i.readCApi(*header);
        i.files = {"sounds/jump.wav", "sounds/coin.wav", "prefabs/coin.prefab", "scenes/main.scene", "images/hero.png"};
        i.tags = {"player", "enemy"};
        return i;
    }();
    return ix;
}

// Suggestions with the cursor at the end of the last line.
std::vector<Suggestion> suggestAtEnd(const std::string& text, bool manual = false, CodeKind kind = CodeKind::EasyScript) {
    auto lines = split(text);
    CodeIntel intel(engineIndex(), kind);
    int from = 0;
    return intel.suggest(lines, static_cast<int>(lines.size()) - 1, static_cast<int>(lines.back().size()), from, manual);
}

bool has(const std::vector<Suggestion>& list, const std::string& label) {
    for (auto& s : list)
        if (s.label == label)
            return true;
    return false;
}

std::vector<Diagnostic> check(const std::string& source, const ProjectIndex& ix = engineIndex()) {
    CodeIntel intel(ix, CodeKind::EasyScript);
    return intel.diagnose(source);
}

bool mentions(const std::vector<Diagnostic>& list, const std::string& text) {
    for (auto& d : list)
        if (d.message.find(text) != std::string::npos)
            return true;
    return false;
}

} // namespace

AVEN_TEST(intel_matches_prefixes_initials_and_words) {
    CHECK(matchScore("key_pressed", "key") > matchScore("key_pressed", "kp"));
    CHECK(matchScore("key_pressed", "kp") > 0);
    CHECK(matchScore("play_sound", "sound") > 0);
    CHECK(matchScore("velocity", "zz") < 0);
    CHECK(matchScore("Velocity", "vel") > 0);
}

AVEN_TEST(intel_suggests_by_context) {
    // Members after self.
    auto self = suggestAtEnd("def on_update(dt):\n    self.vel");
    CHECK(has(self, "velocity"));
    CHECK(has(self, "velocity_x"));
    CHECK(!has(self, "print"));
    // Key names, files and tags inside strings.
    CHECK(has(suggestAtEnd("if key_pressed(\"sp"), "space"));
    auto sounds = suggestAtEnd("play_sound(\"");
    CHECK(has(sounds, "sounds/jump.wav"));
    CHECK(!has(sounds, "images/hero.png"));
    CHECK(has(suggestAtEnd("spawn(\""), "prefabs/coin.prefab"));
    CHECK(has(suggestAtEnd("def on_collide(other):\n    if other.tag == \""), "player"));
    CHECK(has(suggestAtEnd("self.get_component(\"Rig"), "RigidBody2D"));
    // Events after def, written out with their parameters.
    auto events = suggestAtEnd("def on_up");
    CHECK(!events.empty());
    CHECK_EQ(events.front().label, std::string("on_update"));
    CHECK_EQ(events.front().insert, std::string("on_update(dt):\n\t$0"));
    // The file's own variables come before the engine's names.
    auto mine = suggestAtEnd("speed = 5\n\ndef on_update(dt):\n    self.x += sp");
    CHECK(!mine.empty());
    CHECK_EQ(mine.front().label, std::string("speed"));
    // Methods of lists and text, judged by how the variable was made.
    CHECK(has(suggestAtEnd("items = []\nitems.app"), "append"));
    CHECK(has(suggestAtEnd("name = \"hero\"\nname.up"), "upper"));
    // Nothing inside comments.
    CHECK(suggestAtEnd("# self.vel").empty());
}

AVEN_TEST(intel_shows_parameter_hints_and_help) {
    CodeIntel intel(engineIndex(), CodeKind::EasyScript);
    SignatureHelp sig;
    auto lines = split("play_sound(\"sounds/jump.wav\", ");
    CHECK(intel.signature(lines, 0, static_cast<int>(lines[0].size()), sig));
    CHECK_EQ(sig.active, 1);
    CHECK(sig.label.rfind("play_sound(", 0) == 0);
    CHECK(sig.params.size() >= 2);
    CHECK(!sig.doc.empty());
    // Your own functions too.
    lines = split("def jump(power, sound):\n    pass\njump(3, ");
    CHECK(intel.signature(lines, 2, static_cast<int>(lines[2].size()), sig));
    CHECK_EQ(sig.label, std::string("jump(power, sound)"));
    CHECK_EQ(sig.active, 1);

    int from = 0, to = 0;
    lines = split("speed = 5  # how fast\ndef on_update(dt):\n    self.x += speed * dt");
    std::string h = intel.hover(lines, 2, 17, from, to);
    CHECK(h.find("shown in the Inspector") != std::string::npos);
    CHECK(h.find("how fast") != std::string::npos);
    CHECK(intel.hover(lines, 2, 10, from, to).find("Left and right") != std::string::npos); // self.x

    int defLine = -1, defCol = -1;
    CHECK(intel.definition(lines, 2, 17, defLine, defCol));
    CHECK_EQ(defLine, 0);
    auto outline = intel.outline(lines);
    CHECK_EQ(outline.size(), size_t(2));
}

AVEN_TEST(intel_finds_likely_mistakes) {
    auto d = check("speed = 5\n\ndef on_update(dt):\n    self.x += spd * dt\n");
    CHECK_EQ(d.size(), size_t(1));
    CHECK(mentions(d, "Did you mean 'speed'?"));
    CHECK_EQ(d[0].line, 3);
    CHECK_EQ(d[0].col, 14);
    CHECK_EQ(d[0].endCol, 17);
    CHECK(!d[0].error);

    CHECK(mentions(check("def on_update(dt):\n    if key_down(\"spcae\"):\n        pass\n"), "Did you mean 'space'?"));
    CHECK(mentions(check("def on_updte(dt):\n    pass\n"), "Did you mean 'on_update'?"));
    CHECK(mentions(check("def update(dt):\n    pass\n"), "Rename it to on_update?"));
    CHECK(mentions(check("def on_start():\n    play_sound(\"sounds/jmp.wav\")\n"), "Did you mean 'sounds/jump.wav'?"));
    CHECK(mentions(check("def on_start():\n    self.velocty = vec(1, 0)\n    print(self.velocty)\n"), "self doesn't have"));
    auto args = check("def on_start():\n    wait(1, 2, 3)\n");
    CHECK(mentions(args, "wait() needs 0 to 1 values, but gets 3"));
    CHECK(args[0].error);
    auto syntax = check("def on_start()\n    pass\n");
    CHECK_EQ(syntax.size(), size_t(1));
    CHECK(syntax[0].error);
    // Correct code has nothing to report.
    CHECK(check("hits = 0\n\ndef on_collide(other):\n    global hits\n    hits += 1\n    for e in find_all(\"enemy\"):\n"
                "        e.say(f\"{hits} hits\")\n    self.sprite.color = color(\"red\")\n    self.best = max(hits, 3)\n"
                "    print(self.best)\n")
              .empty());
}

AVEN_TEST(intel_has_no_false_alarms_on_the_templates) {
    // Every script in every template (and the code the blocks become) must check clean.
    namespace stdfs = std::filesystem;
    stdfs::path root = stdfs::path(AVEN_SOURCE_DIR) / "templates";
    int files = 0;
    for (auto& t : stdfs::directory_iterator(root)) {
        ProjectIndex ix = engineIndex();
        ix.files.clear();
        for (auto& f : stdfs::recursive_directory_iterator(t.path()))
            if (f.is_regular_file())
                ix.files.push_back(stdfs::relative(f.path(), t.path()).generic_string());
        for (auto& f : stdfs::recursive_directory_iterator(t.path())) {
            std::string ext = f.path().extension().string();
            if (ext != ".es" && ext != ".blocks")
                continue;
            auto text = fs::readText(f.path());
            CHECK(text.has_value());
            std::string source = ext == ".es" ? *text : blocks::compileFile(*text, nullptr);
            auto problems = check(source, ix);
            for (auto& p : problems)
                std::printf("  %s:%d: %s\n", f.path().filename().string().c_str(), p.line + 1, p.message.c_str());
            CHECK(problems.empty());
            ++files;
        }
    }
    CHECK(files > 20);
}

AVEN_TEST(intel_knows_the_c_api) {
    auto s = suggestAtEnd("aven_key_d", false, CodeKind::C);
    CHECK(has(s, "aven_key_down"));
    CHECK(has(suggestAtEnd("b->on_up", false, CodeKind::C), "on_update"));
    CHECK(has(suggestAtEnd("aven_key_down(\"sp", false, CodeKind::C), "space"));
    CodeIntel intel(engineIndex(), CodeKind::C);
    SignatureHelp sig;
    auto lines = split("aven_set(self, ");
    CHECK(intel.signature(lines, 0, static_cast<int>(lines[0].size()), sig));
    CHECK_EQ(sig.active, 1);
    CHECK(sig.label.find("aven_set(") != std::string::npos);
}

