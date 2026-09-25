#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/scene/scene.h"

#include <filesystem>

using namespace aven;

namespace {

// Runs one script on one object for a number of frames.
struct ScriptRun {
    std::filesystem::path dir;
    Assets assets;
    Input input;
    Game game{assets, input};
    Entity hero;

    ScriptRun(const char* name, const std::string& source, bool threeD = false) {
        namespace stdfs = std::filesystem;
        dir = stdfs::temp_directory_path() / name;
        std::error_code ec;
        stdfs::remove_all(dir, ec);
        stdfs::create_directories(dir / "scripts", ec);
        fs::writeText(dir / "scripts/test.es", source);
        assets.setRoot(dir);
        auto scene = std::make_unique<Scene>();
        hero = scene->create("Hero");
        if (threeD)
            scene->registry().emplace<MeshRenderer>(hero);
        else
            scene->registry().emplace<SpriteRenderer>(hero);
        scene->registry().emplace<Script>(hero).path = "scripts/test.es";
        game.start(std::move(scene), "scenes/test.scene");
    }
    ~ScriptRun() { game.stop(); }

    void frames(int n) {
        for (int i = 0; i < n; ++i)
            game.update(1.0f / 60.0f);
    }
    double number(const char* var) {
        auto inst = game.scripts().instanceOf(hero);
        auto* v = inst ? inst->find(script::intern(var)) : nullptr;
        return v && v->isNumber() ? v->number() : -1;
    }
    Entity bubble() {
        for (Entity c : game.scene().children(hero))
            if (game.scene().info(c).name == "_speech")
                return c;
        return {};
    }
};

} // namespace

AVEN_TEST(tween_calls_back_functions_with_or_without_a_value) {
    ScriptRun run("aven_tween_test", "done = 0\n\n"
                                     "def plain():\n    done += 1\n\n"
                                     "def with_object(obj):\n    done += 10\n\n"
                                     "def on_start():\n"
                                     "    self.tween(\"x\", 5, 0.2, \"linear\", plain)\n"
                                     "    self.tween(\"y\", 5, 0.2, \"linear\", with_object)\n");
    run.frames(30);
    CHECK_EQ(run.number("done"), 11.0);
    CHECK_NEAR(run.game.scene().transform(run.hero).position.x, 5.0f, 1e-4f);
}

AVEN_TEST(say_keeps_the_newest_bubble_up_for_its_own_time) {
    ScriptRun run("aven_say_test", "t = 0\n\n"
                                   "def on_start():\n    self.say(\"first\", 1)\n\n"
                                   "def on_update(dt):\n"
                                   "    t += dt\n"
                                   "    if t > 0.75 and t < 0.8:\n"
                                   "        self.say(\"second\", 1)\n");
    run.frames(80); // past the first bubble's time
    Entity b = run.bubble();
    CHECK(b);
    CHECK_EQ(run.game.scene().registry().get<TextRenderer>(b).text, std::string("second"));
    run.frames(60); // past the second one's
    CHECK(!run.bubble());
}

AVEN_TEST(a_number_rotation_turns_without_resetting_other_axes) {
    ScriptRun run("aven_rotation_test", "def on_start():\n    self.rotation_x = 20\n    self.rotation = 45\n", true);
    run.frames(2);
    Vec3 r = run.game.scene().transform(run.hero).rotation;
    CHECK_NEAR(r.x, 20.0f, 1e-4f);
    CHECK_NEAR(r.y, 45.0f, 1e-4f);
}
