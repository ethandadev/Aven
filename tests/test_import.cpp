#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <filesystem>

using namespace aven;

namespace {

namespace stdfs = std::filesystem;

struct ImportErrors {
    std::vector<std::string> errors;
    int sink;
    ImportErrors() {
        sink = Log::addSink([this](const LogMessage& m) {
            if (m.level == LogLevel::Error)
                errors.push_back(m.file + ":" + std::to_string(m.line) + " " + m.text);
        });
    }
    ~ImportErrors() { Log::removeSink(sink); }
    bool has(const std::string& text) const {
        for (auto& e : errors)
            if (e.find(text) != std::string::npos)
                return true;
        return false;
    }
};

stdfs::path freshProject(const char* name) {
    stdfs::path dir = stdfs::temp_directory_path() / name;
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts/lib", ec);
    stdfs::create_directories(dir / "scripts/player", ec);
    return dir;
}

// Runs one frame of a scene with an object for each script.
void runScripts(Game& game, std::initializer_list<const char*> scripts) {
    auto scene = std::make_unique<Scene>();
    for (const char* path : scripts) {
        Entity e = scene->create(stdfs::path(path).stem().string());
        scene->registry().emplace<Script>(e).path = path;
    }
    game.start(std::move(scene), "test.scene");
    game.update(1.0f / 60.0f);
}

} // namespace

// `import` shares one copy of a script between everything that imports it: its functions,
// its variables (changed by one importer, seen by the other), dotted names for folders (the last
// part is the name: `import lib.tools` gives `tools`), `as`,
// `from ... import`, and a file name in quotes.
AVEN_TEST(import_shares_one_module) {
    stdfs::path dir = freshProject("aven_import_test");
    fs::writeText(dir / "scripts/utils.es", "count = 0\nspeed = 7\n"
                                            "def double(x):\n    return x * 2\n"
                                            "def bump():\n    global count\n    count += 1\n    return count\n");
    fs::writeText(dir / "scripts/lib/tools.es", "def triple(x):\n    return x * 3\n");
    fs::writeText(dir / "scripts/player/moves.es", "import helper\ndef jump():\n    return helper.height()\n");
    fs::writeText(dir / "scripts/player/helper.es", "def height():\n    return 5\n"); // next to moves.es
    fs::writeText(dir / "scripts/a.es", "import utils\nfrom utils import double, speed as fast\nimport lib.tools\n"
                                        "import lib.tools as t\nimport \"player/moves.es\" as moves\n"
                                        "def on_start():\n    game.d = double(4)\n    game.s = fast\n"
                                        "    game.t = tools.triple(2) + t.triple(1)\n    game.j = moves.jump()\n"
                                        "    game.first = utils.bump()\n");
    fs::writeText(dir / "scripts/b.es", "import utils\ndef on_start():\n    wait(0.05)\n    game.second = utils.bump()\n"
                                        "    game.count = utils.count\n");
    ImportErrors catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    runScripts(game, {"scripts/a.es", "scripts/b.es"});
    for (int i = 0; i < 10; ++i)
        game.update(1.0f / 60.0f);
    for (auto& e : catcher.errors)
        std::printf("    %s\n", e.c_str());
    CHECK(catcher.errors.empty());
    auto& s = game.scripts();
    CHECK_EQ(s.gameNumber("d"), 8.0);
    CHECK_EQ(s.gameNumber("s"), 7.0);
    CHECK_EQ(s.gameNumber("t"), 9.0);
    CHECK_EQ(s.gameNumber("j"), 5.0);
    CHECK_EQ(s.gameNumber("first"), 1.0);
    CHECK_EQ(s.gameNumber("second"), 2.0); // the same utils, not a copy each
    CHECK_EQ(s.gameNumber("count"), 2.0);
    game.stop();
}

// Mistakes get messages that say what to do.
AVEN_TEST(import_errors_explain_themselves) {
    stdfs::path dir = freshProject("aven_import_errors_test");
    fs::writeText(dir / "scripts/missing.es", "import nothing_here\n");
    fs::writeText(dir / "scripts/x.es", "import y\ndef hi():\n    return 1\n");
    fs::writeText(dir / "scripts/y.es", "import x\ndef hi():\n    return 2\n");
    fs::writeText(dir / "scripts/broken.es", "def oops(:\n");
    fs::writeText(dir / "scripts/uses_broken.es", "import broken\n");
    fs::writeText(dir / "scripts/escape.es", "import \"../../outside.es\"\n");
    fs::writeText(dir / "scripts/z.es", "def hi():\n    return 3\n");
    fs::writeText(dir / "scripts/from_bad.es", "from z import nope\n");
    {
        ImportErrors catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        runScripts(game, {"scripts/missing.es"});
        CHECK(catcher.has("There's no script called 'nothing_here'"));
        game.stop();
    }
    {
        ImportErrors catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        runScripts(game, {"scripts/x.es"});
        CHECK(catcher.has("imports a script that imports it back"));
        game.stop();
    }
    {
        ImportErrors catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        runScripts(game, {"scripts/uses_broken.es"});
        CHECK(catcher.has("can't be imported"));
        game.stop();
    }
    {
        ImportErrors catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        runScripts(game, {"scripts/escape.es"});
        CHECK(catcher.has("There's no script called")); // outside the game folder: not found
        game.stop();
    }
    {
        ImportErrors catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        runScripts(game, {"scripts/from_bad.es"});
        CHECK(catcher.has("nope"));
        game.stop();
    }
}
