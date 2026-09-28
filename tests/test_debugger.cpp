#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <filesystem>

using namespace aven;
using script::VM;

namespace {

std::string valueOf(const std::vector<std::pair<std::string, std::string>>& list, const std::string& name) {
    for (auto& [n, v] : list)
        if (n == name)
            return v;
    return "(missing)";
}

} // namespace

// Breakpoints pause the game at a line; the paused code shows its calls, local values and
// script variables; Step Into, Out and Over move a line at a time; Continue runs on to the next
// breakpoint.
AVEN_TEST(debugger_breakpoints_and_stepping) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_debugger_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/d.es", "count = 0\n"               // 1
                                        "def helper(n):\n"          // 2
                                        "    m = n * 2\n"           // 3
                                        "    return m\n"            // 4
                                        "def on_update(dt):\n"      // 5
                                        "    global count\n"        // 6
                                        "    count += 1\n"          // 7
                                        "    x = helper(count)\n"   // 8
                                        "    game.last = x\n");     // 9
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    VM& vm = game.scripts().vm();
    // (by value: CHECK_EQ keeps references to what it compares)
    auto topLine = [&] { return vm.debugFrames().at(0).line; };
    auto frameCount = [&] { return vm.debugFrames().size(); };
    auto topVar = [&](const char* name) { return valueOf(vm.debugFrames().at(0).vars, name); };
    vm.setBreakpoints({{"scripts/d.es", {8}}});
    auto scene = std::make_unique<Scene>();
    Entity e = scene->create("Counter");
    scene->registry().emplace<Script>(e).path = "scripts/d.es";
    game.start(std::move(scene), "test.scene");
    game.update(1.0f / 60.0f);

    CHECK(vm.debugPaused());
    auto frames = vm.debugFrames();
    CHECK_EQ(frames.size(), size_t(1));
    CHECK_EQ(frames[0].function, std::string("on_update"));
    CHECK_EQ(frames[0].file, std::string("scripts/d.es"));
    CHECK_EQ(frames[0].line, 8);
    CHECK_EQ(frames[0].object, std::string("Counter"));
    CHECK_EQ(valueOf(frames[0].vars, "count"), std::string("1"));
    CHECK(valueOf(frames[0].locals, "dt") != "(missing)");
    CHECK_EQ(valueOf(frames[0].locals, "x"), std::string("(missing)")); // not set yet

    // The game waits while paused.
    for (int i = 0; i < 5; ++i)
        game.update(1.0f / 60.0f);
    CHECK(vm.debugPaused());
    CHECK_EQ(topVar("count"), std::string("1"));

    vm.debugResume(VM::Step::Into); // into helper()
    frames = vm.debugFrames();
    CHECK_EQ(frames.size(), size_t(2));
    CHECK_EQ(frames[0].function, std::string("helper"));
    CHECK_EQ(frames[0].line, 3);
    CHECK_EQ(valueOf(frames[0].locals, "n"), std::string("1"));
    CHECK_EQ(frames[1].line, 8); // where on_update called it

    vm.debugResume(VM::Step::Out); // back in on_update, on the next line
    frames = vm.debugFrames();
    CHECK_EQ(frames.size(), size_t(1));
    CHECK_EQ(frames[0].line, 9);
    CHECK_EQ(valueOf(frames[0].locals, "x"), std::string("2"));

    vm.debugResume(VM::Step::Over); // off the end of on_update...
    CHECK(!vm.debugPaused());
    CHECK_EQ(game.scripts().gameNumber("last"), 2.0);
    game.update(1.0f / 60.0f); // ...so it stops at the next line that runs: next frame's
    CHECK(vm.debugPaused());
    CHECK_EQ(topLine(), 7);

    vm.debugResume(VM::Step::Over); // over, not into, helper()
    CHECK_EQ(topLine(), 8);
    vm.debugResume(VM::Step::Over);
    CHECK_EQ(topLine(), 9);
    CHECK_EQ(frameCount(), size_t(1));

    vm.debugResume(VM::Step::Continue); // to the breakpoint, next frame
    CHECK(!vm.debugPaused());
    game.update(1.0f / 60.0f);
    CHECK(vm.debugPaused());
    CHECK_EQ(topLine(), 8);
    CHECK_EQ(topVar("count"), std::string("3"));

    // No breakpoints: it runs freely.
    vm.setBreakpoints({});
    vm.debugResume(VM::Step::Continue);
    for (int i = 0; i < 3; ++i)
        game.update(1.0f / 60.0f);
    CHECK(!vm.debugPaused());
    CHECK_EQ(game.scripts().gameNumber("last"), 12.0); // count 6

    // Stopping the game while paused is fine.
    vm.setBreakpoints({{"scripts/d.es", {3}}});
    game.update(1.0f / 60.0f);
    CHECK(vm.debugPaused());
    game.stop();
    CHECK(!vm.debugPaused());
}
