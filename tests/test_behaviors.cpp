#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/platform/input.h"
#include "aven/runtime/behavior_code.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
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

// A bullet (Hazard + Vanish On Hit) destroys an enemy with 1 health; the enemy's script scores on_destroy.
AVEN_TEST(behavior_bullet_hits_enemy) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_bullet_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/enemy.es", "def on_destroy():\n    game.score = get_game(\"score\", 0) + 1\n");
    Assets assets;
    assets.setRoot(dir);
    Input input;
    ErrorCatcher catcher;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity enemy = scene->create("Enemy");
    scene->info(enemy).tag = "enemy";
    reg.emplace<RigidBody2D>(enemy).gravityScale = 0;
    reg.emplace<CircleCollider2D>(enemy).radius = 0.4f;
    auto& h = reg.emplace<Health>(enemy);
    h.maxHealth = 1;
    h.whenZero = WhenHealthRunsOut::Destroy;
    h.counter = "";
    reg.emplace<Script>(enemy).path = "scripts/enemy.es";
    Entity bullet = scene->create("Bullet");
    scene->transform(bullet).position = {-2, 0, 0};
    auto& brb = reg.emplace<RigidBody2D>(bullet);
    brb.gravityScale = 0;
    brb.continuous = true;
    auto& col = reg.emplace<CircleCollider2D>(bullet);
    col.radius = 0.12f;
    col.isTrigger = true;
    auto& hz = reg.emplace<Hazard>(bullet);
    hz.victimTag = "enemy";
    hz.vanishOnHit = true;
    game.start(std::move(scene), "test.scene");
    game.physics2D().setVelocity(game.scene().findByName("Bullet"), {12, 0});
    for (int i = 0; i < 40; ++i)
        game.update(1.0f / 60.0f);
    CHECK(!game.scene().findByName("Enemy"));
    CHECK(!game.scene().findByName("Bullet"));
    CHECK_EQ(game.scripts().gameNumber("score"), 1.0);
    CHECK(catcher.errors.empty());
}

// Click Actions: a no-code list of steps. The steps survive saving, and the code the
// editor shows for them does the same thing.
AVEN_TEST(behavior_click_actions_run_steps) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_click_actions_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/door.es", "def open_door(amount):\n    game.opened = amount\n");

    auto makeScene = [] {
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity menu = scene->create("Menu");
        scene->info(menu).active = false;
        Entity door = scene->create("Door");
        reg.emplace<Script>(door).path = "scripts/door.es";
        scene->create("Coin");
        Entity button = scene->create("Button");
        auto& ca = reg.emplace<ClickActions>(button);
        auto step = [&](ClickDo d, Entity target, std::string text, float number) {
            ca.steps.push_back({d, target ? scene->info(target).uuid : UUID{}, std::move(text), number});
        };
        step(ClickDo::Show, menu, "", 0);
        step(ClickDo::AddToGameValue, {}, "coins", 2);
        step(ClickDo::SetGameValue, {}, "lives", 3);
        step(ClickDo::CallFunction, door, "open_door", 7);
        step(ClickDo::Destroy, scene->findByName("Coin"), "", 0);
        step(ClickDo::Pause, {}, "", 0);
        // Saved and loaded again, like a scene file.
        Json saved = scene->save();
        auto loaded = std::make_unique<Scene>();
        std::string error;
        CHECK(loaded->load(saved, &error));
        CHECK(error.empty());
        return loaded;
    };

    auto check = [&](Game& game) {
        Scene& s = game.scene();
        CHECK(s.info(s.findByName("Menu")).active);
        CHECK_EQ(game.scripts().gameNumber("coins"), 2.0);
        CHECK_EQ(game.scripts().gameNumber("lives"), 3.0);
        CHECK_EQ(game.scripts().gameNumber("opened"), 7.0);
        CHECK(!s.findByName("Coin"));
        CHECK(game.paused());
    };

    Assets assets;
    assets.setRoot(dir);
    Input input;
    std::string code;
    {
        ErrorCatcher catcher;
        Game game(assets, input);
        game.start(makeScene(), "scenes/test.scene");
        game.update(1.0f / 60.0f);
        Scene& s = game.scene();
        CHECK_EQ(s.registry().get<ClickActions>(s.findByName("Button")).steps.size(), size_t(6));
        // The code version, with object names filled in.
        code = behaviorAsEasyScript("ClickActions", saveComponent(*ComponentRegistry::find("ClickActions"),
                                                                  &s.registry().get<ClickActions>(s.findByName("Button"))),
                                    [&](const std::string& id) {
                                        Entity e = s.findByUUID(UUID::fromString(id));
                                        return e ? s.info(e).name : std::string();
                                    });
        game.gameplay().onBehaviorClick(s.findByName("Button"));
        game.update(1.0f / 60.0f);
        check(game);
        CHECK(catcher.errors.empty());
        game.stop();
    }
    CHECK(code.find("find(\"Menu\").active = True") != std::string::npos);
    CHECK(code.find("find(\"Door\").send(\"open_door\", 7)") != std::string::npos);
    fs::writeText(dir / "scripts/button.es", code);
    {
        ErrorCatcher catcher;
        Game game(assets, input);
        auto scene = makeScene();
        Entity button = scene->findByName("Button");
        scene->registry().remove<ClickActions>(button);
        scene->registry().emplace<Script>(button).path = "scripts/button.es";
        game.start(std::move(scene), "scenes/test.scene");
        game.update(1.0f / 60.0f);
        game.scripts().onClick(game.scene().findByName("Button"));
        game.update(1.0f / 60.0f);
        check(game);
        if (!catcher.errors.empty())
            std::printf("%s\n  -> %s\n", code.c_str(), catcher.errors[0].c_str());
        CHECK(catcher.errors.empty());
    }
}

// A Value Bar follows a game value; with max 0 its highest value counts as full.
AVEN_TEST(behavior_value_bar_follows_game_value) {
    Assets assets;
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    Entity bar = scene->create("Bar");
    scene->registry().emplace<UIElement>(bar);
    scene->registry().emplace<ValueBar>(bar);
    game.start(std::move(scene), "test.scene");
    Entity e = game.scene().findByName("Bar");
    auto& vb = game.scene().registry().get<ValueBar>(e);
    game.update(1.0f / 60.0f);
    CHECK_EQ(vb.fill, 1.0f); // nothing sets game.health yet
    game.scripts().setGameValue("health", script::Value(4.0));
    game.update(1.0f / 60.0f);
    game.scripts().setGameValue("health", script::Value(1.0));
    for (int i = 0; i < 120; ++i)
        game.update(1.0f / 60.0f);
    CHECK(std::abs(vb.fill - 0.25f) < 0.01f);
    vb.max = 2;
    for (int i = 0; i < 120; ++i)
        game.update(1.0f / 60.0f);
    CHECK(std::abs(vb.fill - 0.5f) < 0.01f);
}
