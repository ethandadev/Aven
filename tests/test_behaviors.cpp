#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/input.h"
#include "aven/runtime/behavior_code.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"

#include <filesystem>

using namespace aven;

namespace {

struct ErrorCatcher {
    std::vector<std::string> errors;
    int sink;
    ErrorCatcher() {
        sink = Log::addSink([this](const LogMessage& m) {
            if (m.level == LogLevel::Error)
                errors.push_back(m.file + ":" + std::to_string(m.line) + " " + m.text);
        });
    }
    ~ErrorCatcher() { Log::removeSink(sink); }
};

std::unique_ptr<Scene> testScene(const std::string& script) {
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity cam = scene->create("Camera");
    scene->transform(cam).position = {0, 0, 10};
    reg.emplace<Camera>(cam);
    Entity ground = scene->create("Ground");
    scene->transform(ground).position = {0, -3, 0};
    reg.emplace<BoxCollider2D>(ground).size = {40, 1};
    Entity player = scene->create("Player");
    scene->info(player).tag = "player";
    scene->transform(player).position = {2, -2, 0};
    reg.emplace<SpriteRenderer>(player);
    reg.emplace<RigidBody2D>(player).fixedRotation = true;
    reg.emplace<BoxCollider2D>(player);
    Entity tester = scene->create("Tester");
    reg.emplace<SpriteRenderer>(tester);
    reg.emplace<TextRenderer>(tester); // for ScoreDisplay
    auto& rb = reg.emplace<RigidBody2D>(tester);
    rb.gravityScale = 0;
    rb.fixedRotation = true;
    reg.emplace<BoxCollider2D>(tester).isTrigger = true;
    reg.emplace<Script>(tester).path = script;
    return scene;
}

} // namespace

// Every behavior can be shown as EasyScript. That code must actually work: run each one.
AVEN_TEST(behaviors_easyscript_equivalents_run) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_behavior_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::create_directories(dir / "prefabs", ec);
    const char* prefab = R"({"entities": [{"id": "0000000000000001", "name": "Bullet", "components": {"Transform": {}}}]})";
    fs::writeText(dir / "prefabs/bullet.prefab", prefab);
    fs::writeText(dir / "prefabs/enemy.prefab", prefab);

    int checked = 0;
    for (auto& ci : ComponentRegistry::all()) {
        if (ci.category != "Behaviors")
            continue;
        Registry r;
        Entity e = r.create();
        ci.add(r, e);
        Json values = saveComponent(ci, ci.get(r, e));
        std::string code = behaviorAsEasyScript(ci.name, values);
        CHECK(!code.empty());
        std::string path = "scripts/" + ci.name + ".es";
        fs::writeText(dir / path, code);

        ErrorCatcher catcher;
        Assets assets;
        assets.setRoot(dir);
        Input input;
        {
            Game game(assets, input);
            game.start(testScene(path), "scenes/test.scene");
            for (int i = 0; i < 30; ++i)
                game.update(1.0f / 60.0f);
            game.stop();
        }
        if (!catcher.errors.empty())
            std::printf("  %s equivalent failed:\n%s\n  -> %s\n", ci.name.c_str(), code.c_str(), catcher.errors[0].c_str());
        CHECK(catcher.errors.empty());
        ++checked;
    }
    CHECK(checked >= 15);
}

// The behaviors themselves: a coin is collected by the player and counted.
AVEN_TEST(behavior_collectible_counts) {
    Assets assets;
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity player = scene->create("Player");
    scene->info(player).tag = "player";
    auto& rb = reg.emplace<RigidBody2D>(player);
    rb.gravityScale = 0;
    reg.emplace<BoxCollider2D>(player);
    reg.emplace<TopDownController>(player);
    Entity coin = scene->create("Coin");
    scene->transform(coin).position = {0.5f, 0, 0};
    reg.emplace<CircleCollider2D>(coin).isTrigger = true;
    auto& c = reg.emplace<Collectible>(coin);
    c.counter = "coins";
    c.amount = 5;
    c.sparkle = false;
    game.start(std::move(scene), "test.scene");
    for (int i = 0; i < 10; ++i)
        game.update(1.0f / 60.0f);
    CHECK_EQ(game.scripts().gameNumber("coins"), 5.0);
    CHECK(!game.scene().findByName("Coin"));
}
