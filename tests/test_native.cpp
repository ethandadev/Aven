#include "test_framework.h"

#include "rynax/assets/assets.h"
#include "rynax/core/fs.h"
#include "rynax/core/log.h"
#include "rynax/platform/input.h"
#include "rynax/runtime/game.h"
#include "rynax/runtime/native.h"
#include "rynax/runtime/script_system.h"
#include "rynax/scene/scene.h"

#include <cmath>
#include <filesystem>

using namespace rynax;
namespace stdfs = std::filesystem;

namespace {

stdfs::path makeProject() {
    stdfs::path dir = stdfs::temp_directory_path() / "rynax_native_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(NativeModules::binDir(dir), ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::copy_file(RYNAX_TEST_MODULE, NativeModules::binDir(dir) / (std::string("movers") + NativeModules::libraryExtension()), ec);
    return dir;
}

} // namespace

RYNAX_TEST(native_module_loads_behaviors_and_properties) {
    stdfs::path dir = makeProject();
    NativeModules& modules = NativeModules::get();
    CHECK(modules.refresh(dir));
    CHECK(!modules.refresh(dir)); // nothing changed
    CHECK_EQ(modules.modules().size(), size_t(1));
    CHECK(modules.modules()[0].error.empty());
    CHECK_EQ(modules.modules()[0].behaviors, 4);
    const NativeBehaviorInfo* mover = modules.find("Mover");
    CHECK(mover != nullptr);
    CHECK(modules.find("Finder") != nullptr);
    CHECK(modules.find("Nope") == nullptr);
    CHECK_EQ(mover->properties.size(), size_t(3));
    CHECK_EQ(mover->properties[0].name, std::string("speed"));
    CHECK_NEAR(mover->properties[0].defaultValue, 2.0, 1e-9);

    // Defaults, then Inspector overrides, land in the behavior's data.
    std::vector<std::max_align_t> data(4);
    Json overrides = Json::object();
    overrides["bounces"] = 9;
    NativeRuntime::applyProperties(*mover, data.data(), overrides);
    CHECK_NEAR(NativeRuntime::readProperty(mover->properties[0], data.data()), 2.0, 1e-6);
    CHECK_NEAR(NativeRuntime::readProperty(mover->properties[1], data.data()), 9.0, 1e-9);
    CHECK_NEAR(NativeRuntime::readProperty(mover->properties[2], data.data()), 1.0, 1e-9);

    // A rebuilt library (newer file) is noticed.
    auto lib = NativeModules::binDir(dir) / (std::string("movers") + NativeModules::libraryExtension());
    std::error_code ec;
    stdfs::last_write_time(lib, stdfs::last_write_time(lib, ec) + std::chrono::seconds(5), ec);
    CHECK(modules.refresh(dir));
    CHECK(modules.find("Mover") != nullptr);
    modules.unloadAll();
}

// A module built with aven.h, from when Rynax was called Aven: found, with its settings.
RYNAX_TEST(native_module_from_aven_still_loads) {
    stdfs::path dir = makeProject();
    std::error_code ec;
    stdfs::remove(NativeModules::binDir(dir) / (std::string("movers") + NativeModules::libraryExtension()), ec);
    stdfs::copy_file(RYNAX_AVEN_MODULE, NativeModules::binDir(dir) / (std::string("old") + NativeModules::libraryExtension()), ec);
    NativeModules& modules = NativeModules::get();
    CHECK(modules.refresh(dir));
    CHECK_EQ(modules.modules().size(), size_t(1));
    CHECK(modules.modules().empty() || modules.modules()[0].error.empty());
    const NativeBehaviorInfo* old = modules.find("OldTimer");
    CHECK(old != nullptr);
    CHECK(old && old->properties.size() == 1 && std::abs(old->properties[0].defaultValue - 3.5) < 1e-9);
    modules.unloadAll();
}

RYNAX_TEST(native_behaviors_run_in_the_game) {
    stdfs::path dir = makeProject();
    // EasyScript talks to the C behavior, and hears C's broadcast.
    fs::writeText(dir / "scripts/talker.es", "t = 0\nsent = False\n\ndef on_update(dt):\n    t += dt\n"
                                              "    if t > 1 and not sent:\n        sent = True\n"
                                              "        find(\"Mover\").send(\"boost\", 2)\n\n"
                                              "def on_message(message, data):\n    if message == \"hello\":\n"
                                              "        game.heard = data\n");
    std::vector<std::string> errors;
    int sink = Log::addSink([&](const LogMessage& m) {
        if (m.level == LogLevel::Error)
            errors.push_back(m.text);
    });
    {
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity mover = scene->create("Mover");
        auto& ns = reg.emplace<NativeScript>(mover);
        ns.className = "Mover";
        ns.overrides["speed"] = 4;
        Entity target = scene->create("Target");
        scene->info(target).tag = "goal";
        Entity finder = scene->create("Finder");
        reg.emplace<NativeScript>(finder).className = "Finder";
        Entity talker = scene->create("Talker");
        reg.emplace<Script>(talker).path = "scripts/talker.es";
        Entity ground = scene->create("Ground");
        scene->transform(ground).position = {0, -20, 0};
        reg.emplace<BoxCollider2D>(ground).size = {40, 1};
        Entity faller = scene->create("Faller");
        scene->transform(faller).position = {0, -18, 0};
        reg.emplace<RigidBody2D>(faller);
        reg.emplace<BoxCollider2D>(faller);
        auto& fs2 = reg.emplace<NativeScript>(faller);
        fs2.className = "Mover";
        fs2.overrides["speed"] = 0;
        Entity missing = scene->create("Missing");
        reg.emplace<NativeScript>(missing).className = "Nope";
        Entity fuse = scene->create("Fuse");
        reg.emplace<NativeScript>(fuse).className = "Fuse"; // destroys itself mid-update
        // API version 2: Home > Family > (Child, Middle > Grandchild with a script to call).
        fs::writeText(dir / "scripts/greeter.es", "def greet(name):\n    return \"hi \" + name\n\n"
                                                   "def add(a, b):\n    return a + b\n");
        Entity home = scene->create("Home");
        Entity family = scene->create("Family", home);
        reg.emplace<NativeScript>(family).className = "Family";
        scene->create("Child", family);
        Entity middle = scene->create("Middle", family);
        Entity grandchild = scene->create("Grandchild", middle);
        reg.emplace<Script>(grandchild).path = "scripts/greeter.es";

        game.start(std::move(scene), "scenes/test.scene");
        for (int i = 0; i < 60; ++i)
            game.update(1.0f / 60.0f);
        Scene& s = game.scene();
        CHECK_NEAR(s.transform(s.findByName("Mover")).position.x, 4.0f, 0.1f);
        CHECK_NEAR(game.scripts().gameNumber("started"), 2.0, 1e-9);
        CHECK_NEAR(s.transform(s.findByName("Target")).position.y, 5.0f, 1e-5f);
        CHECK_NEAR(game.scripts().gameNumber("target_is_goal"), 1.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("goals"), 1.0, 1e-9);
        CHECK(s.findByName("Finder (done)"));
        CHECK_NEAR(game.scripts().gameNumber("heard"), 7.0, 1e-9);
        CHECK(game.scripts().gameNumber("hits") >= 1);
        CHECK_NEAR(game.scripts().gameNumber("fuse_done"), 3.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("fuse_destroyed"), 1.0, 1e-9);
        CHECK(!s.findByName("Fuse"));
        CHECK_NEAR(game.scripts().gameNumber("family_parent_ok"), 1.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("family_top_has_no_parent"), 1.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("family_children"), 2.0, 1e-9);
        CHECK_NEAR(s.transform(s.findByName("Grandchild")).position.x, 7.0f, 1e-5f);
        CHECK_NEAR(game.scripts().gameNumber("family_missing_child"), 1.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("family_greeting_ok"), 1.0, 1e-9);
        CHECK_NEAR(game.scripts().gameNumber("family_sum"), 5.0, 1e-9);
        for (int i = 0; i < 60; ++i) // boosted to 6 units per second by the EasyScript
            game.update(1.0f / 60.0f);
        CHECK_NEAR(s.transform(s.findByName("Mover")).position.x, 10.0f, 0.2f);
        game.stop();
    }
    Log::removeSink(sink);
    // The two mistakes are reported (once each), and nothing else went wrong.
    bool nope = false, nonsense = false;
    for (auto& e : errors) {
        nope |= e.find("'Nope'") != std::string::npos;
        nonsense |= e.find("nonsense_property") != std::string::npos;
    }
    CHECK(nope);
    CHECK(nonsense);
    CHECK_EQ(errors.size(), size_t(2));
    NativeModules::get().unloadAll();
}

#ifdef RYNAX_EXAMPLE_CPP_MODULE
// Modules written in C++ export the same entry point (no name mangling).
RYNAX_TEST(native_module_in_cpp_loads) {
    stdfs::path dir = stdfs::temp_directory_path() / "rynax_native_cpp_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(NativeModules::binDir(dir), ec);
    stdfs::copy_file(RYNAX_EXAMPLE_CPP_MODULE, NativeModules::binDir(dir) / (std::string("patrol") + NativeModules::libraryExtension()), ec);
    NativeModules& modules = NativeModules::get();
    CHECK(modules.refresh(dir));
    CHECK(modules.modules().size() == 1 && modules.modules()[0].error.empty());
    const NativeBehaviorInfo* patrol = modules.find("Patrol");
    CHECK(patrol != nullptr);
    CHECK(patrol && patrol->properties.size() == 2 && patrol->callbacks.on_update);
    modules.unloadAll();
}
#endif
