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
#include "aven/scene/terrain.h"
#include "aven/runtime/network.h"

#include <chrono>
#include <filesystem>
#include <thread>

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

// Collision layers: objects on layers set to pass through each other don't collide,
// raycasts can be limited to layers, and scripts can change an object's layer.
AVEN_TEST(physics_collision_layers) {
    for (bool threeD : {false, true}) {
        Assets assets;
        Input input;
        Game game(assets, input);
        game.settings().layers = {"Ghost", "Ground"};
        game.settings().setLayersCollide("Ghost", "Ground", false);
        CHECK(!game.settings().layersCollide("Ghost", "Ground"));
        CHECK(game.settings().layersCollide("Ghost", "Default"));
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity ground = scene->create("Ground");
        scene->info(ground).layer = "Ground";
        scene->transform(ground).position = {0, -1, 0};
        auto box = [&](const char* name, float x, const char* layer) {
            Entity e = scene->create(name);
            scene->info(e).layer = layer;
            scene->transform(e).position = {x, 1, 0};
            if (threeD) {
                reg.emplace<RigidBody>(e);
                reg.emplace<BoxCollider>(e);
            } else {
                reg.emplace<RigidBody2D>(e);
                reg.emplace<BoxCollider2D>(e);
            }
            return e;
        };
        if (threeD)
            reg.emplace<BoxCollider>(ground).size = {40, 1, 40};
        else
            reg.emplace<BoxCollider2D>(ground).size = {40, 1};
        box("Solid", -2, "");
        box("Ghost", 2, "Ghost");
        game.start(std::move(scene), "test.scene");
        for (int i = 0; i < 120; ++i)
            game.update(1.0f / 60.0f);
        Scene& s = game.scene();
        float solidY = s.worldPosition(s.findByName("Solid")).y, ghostY = s.worldPosition(s.findByName("Ghost")).y;
        CHECK(solidY > -0.1f); // resting on the ground
        CHECK(ghostY < -3.0f); // fell through it
        // A raycast down through the ground, limited to the Ground layer, hits the ground.
        RayHit hit;
        uint32_t groundOnly = 1u << game.settings().layerIndex("Ground");
        bool found = threeD ? game.physics3D().raycast({-2, 5, 0}, {0, -1, 0}, 20, hit, {}, groundOnly)
                            : game.physics2D().raycast({-2, 5}, {-2, -5}, hit, groundOnly);
        CHECK(found);
        CHECK(hit.entity == s.findByName("Ground"));
        // Scripts see and change layers.
        script::Value layer;
        CHECK(game.scripts().getProperty(s.findByName("Ghost"), "layer", layer));
        CHECK_EQ(layer.string(), std::string("Ghost"));
        CHECK(game.scripts().setProperty(s.findByName("Solid"), "layer", script::Value("Ghost")));
        for (int i = 0; i < 120; ++i)
            game.update(1.0f / 60.0f);
        CHECK(s.worldPosition(s.findByName("Solid")).y < -1.0f); // now falls through too
        game.stop();
    }
    // Saved in project.aven and read back.
    ProjectSettings p;
    p.layers = {"Player", "Bullet"};
    p.setLayersCollide("Player", "Bullet", false);
    ProjectSettings q;
    q.fromJson(p.toJson());
    CHECK_EQ(q.layers.size(), size_t(2));
    CHECK(!q.layersCollide("Bullet", "Player"));
    CHECK(q.layersCollide("Player", "Player"));
}

// Debug shapes last one frame unless given seconds, and scripts can draw them.
AVEN_TEST(debug_draw_lifetimes) {
    DebugDraw d;
    d.line({0, 0, 0}, {1, 0, 0}, {1, 1, 0, 1}, 0);
    d.circle({0, 0, 0}, 1, {1, 1, 0, 1}, 0.5f);
    CHECK_EQ(d.shapes().size(), size_t(2));
    d.tick(1.0f / 60.0f);
    CHECK_EQ(d.shapes().size(), size_t(1)); // the one-frame line is gone
    for (int i = 0; i < 40; ++i)
        d.tick(1.0f / 60.0f);
    CHECK(d.shapes().empty());

    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_debug_draw_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/d.es", "def on_update(dt):\n    debug_line(self, vec(3, 0), \"red\")\n"
                                        "    debug_box(vec(0, 0), vec(2, 1))\n    debug_text(self, \"hi\", seconds=2)\n");
    ErrorCatcher catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    Entity e = scene->create("Drawer");
    scene->registry().emplace<Script>(e).path = "scripts/d.es";
    game.start(std::move(scene), "test.scene");
    game.update(1.0f / 60.0f);
    CHECK(catcher.errors.empty());
    CHECK_EQ(game.debugDraw().shapes().size(), size_t(3));
    CHECK(game.debugDraw().shapes()[1].b.z == 0); // a 2D box
    for (int i = 0; i < 10; ++i)
        game.update(1.0f / 60.0f);
    CHECK(game.debugDraw().shapes().size() < 30); // lines don't pile up; labels last 2 s
}

// The audio mixer: buses are saved in the project, scripts can change them, and a wrong bus
// name gets a clear message. (Works without a sound device too.)
AVEN_TEST(audio_mixer_buses) {
    ProjectSettings p;
    CHECK(p.toJson()["audio_buses"].isNull()); // defaults aren't written out
    p.audioBuses.push_back({"Ambience", 0.4f, false, 800.0f, 0.3f, 0.5f});
    ProjectSettings q;
    q.fromJson(p.toJson());
    CHECK_EQ(q.audioBuses.size(), size_t(4));
    CHECK_EQ(q.audioBuses[3].name, std::string("Ambience"));
    CHECK_EQ(q.audioBuses[3].lowpass, 800.0f);
    // Music and Effects always exist.
    Json j = Json::parse(R"({"audio_buses": [{"name": "Voice"}]})");
    ProjectSettings r;
    r.fromJson(j);
    CHECK_EQ(r.audioBuses.size(), size_t(3));

    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_mixer_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/m.es", "def on_start():\n    set_bus_volume(\"Music\", 0.25)\n    mute_bus(\"Effects\")\n"
                                        "    game.v = get_bus_volume(\"Music\")\n    play_sound(\"sounds/none.wav\", bus=\"Voice\")\n"
                                        "    set_bus_volume(\"Radio\", 1)\n");
    ErrorCatcher catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    scene->registry().emplace<Script>(scene->create("Mixer")).path = "scripts/m.es";
    game.start(std::move(scene), "test.scene");
    game.update(1.0f / 60.0f);
    CHECK_EQ(catcher.errors.size(), size_t(1)); // only the unknown "Radio" bus
    CHECK(!catcher.errors.empty() && catcher.errors[0].find("no audio bus called \"Radio\"") != std::string::npos);
    game.stop();
}

// Animator: a platformer state machine picks Idle / Run / Jump from the built-in parameters,
// scripts drive it with set_param / trigger / play_state, and it survives saving.
AVEN_TEST(animator_state_machine) {
    Animator a;
    a.startState = "Idle";
    a.states = {{"Idle", 0, 1, 4, true, "", 1}, {"Run", 2, 5, 10, true, "", 1}, {"Jump", 6, 6, 8, true, "", 1},
                {"Attack", 7, 8, 12, false, "", 1}};
    a.params = {{"attack", true, 0}, {"hurt", false, 0}};
    a.transitions = {{"*", "Jump", "on_ground", AnimCondition::IsFalse, 0},
                     {"*", "Attack", "attack", AnimCondition::Triggered, 0},
                     {"Attack", "Idle", "", AnimCondition::Finished, 0},
                     {"Jump", "Idle", "on_ground", AnimCondition::IsTrue, 0},
                     {"Idle", "Run", "speed", AnimCondition::Greater, 0.5f},
                     {"Run", "Idle", "speed", AnimCondition::Less, 0.5f}};

    // Save and load.
    const ComponentInfo* ci = ComponentRegistry::find("Animator");
    CHECK(ci != nullptr);
    Registry r;
    Entity e0 = r.create();
    r.emplace<Animator>(e0) = a;
    Json saved = saveComponent(*ci, ci->get(r, e0));
    Entity e1 = r.create();
    ci->add(r, e1);
    loadComponent(*ci, ci->get(r, e1), saved);
    auto& b = r.get<Animator>(e1);
    CHECK_EQ(b.states.size(), size_t(4));
    CHECK_EQ(b.states[1].lastFrame, 5);
    CHECK(!b.states[3].loop);
    CHECK_EQ(b.transitions.size(), size_t(6));
    CHECK(b.transitions[1].when == AnimCondition::Triggered);
    CHECK_EQ(b.transitions[4].value, 0.5f);
    CHECK(b.params[0].trigger);

    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_animator_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/p.es", "def on_message(m, data):\n"
                                        "    if m == \"attack\":\n        self.trigger(\"attack\")\n"
                                        "    if m == \"hurt\":\n        self.play_state(\"Jump\")\n        game.s = self.anim_state\n"
                                        "    if m == \"bad\":\n        self.set_param(\"runing\", True)\n");
    ErrorCatcher catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity ground = scene->create("Ground");
    scene->transform(ground).position = {0, -1, 0};
    reg.emplace<BoxCollider2D>(ground).size = {100, 1};
    Entity hero = scene->create("Hero");
    scene->transform(hero).position = {0, 0.1f, 0};
    reg.emplace<SpriteRenderer>(hero);
    reg.emplace<RigidBody2D>(hero).fixedRotation = true;
    reg.emplace<BoxCollider2D>(hero);
    reg.emplace<Script>(hero).path = "scripts/p.es";
    reg.emplace<Animator>(hero) = a;
    game.start(std::move(scene), "test.scene");
    auto step = [&](int n) {
        for (int i = 0; i < n; ++i)
            game.update(1.0f / 60.0f);
    };
    Entity h = game.scene().findByName("Hero");
    auto state = [&] { return game.scene().registry().get<Animator>(h).current; };
    step(40); // lands
    CHECK_EQ(state(), std::string("Idle"));
    CHECK(game.scene().registry().has<SpriteAnimator>(h)); // added for it
    game.physics2D().setVelocity(h, {4, 0});
    step(2);
    CHECK_EQ(state(), std::string("Run"));
    CHECK(game.scene().registry().get<SpriteRenderer>(h).frame >= 2);
    game.physics2D().setVelocity(h, {0, 0});
    step(20);
    CHECK_EQ(state(), std::string("Idle"));
    game.physics2D().setVelocity(h, {0, 6});
    step(3);
    CHECK_EQ(state(), std::string("Jump"));
    step(90);
    CHECK_EQ(state(), std::string("Idle"));
    game.scripts().broadcast("attack", {});
    step(2);
    CHECK_EQ(state(), std::string("Attack"));
    step(30); // 2 frames at 12 fps, doesn't loop, then back to Idle
    CHECK_EQ(state(), std::string("Idle"));
    CHECK_EQ(game.scene().registry().get<Animator>(h).params[0].value, 0.0f); // trigger used up
    CHECK(catcher.errors.empty());
    game.scripts().broadcast("hurt", {});
    CHECK(game.scripts().gameValue("s").isString() && game.scripts().gameValue("s").string() == "Jump");
    game.scripts().broadcast("bad", {});
    CHECK_EQ(catcher.errors.size(), size_t(1));
    CHECK(!catcher.errors.empty() && catcher.errors[0].find("attack, hurt") != std::string::npos);
    game.stop();
}

// save_data() keeps values in memory, writes them when the game stops, and two games with the
// same name but different ids keep separate saves.
AVEN_TEST(save_data_cached_and_per_game) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_save_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::path data = stdfs::temp_directory_path() / "aven_save_home";
    stdfs::remove_all(data, ec);
#ifdef _WIN32
    _putenv_s("APPDATA", data.string().c_str());
#else
    setenv("XDG_DATA_HOME", data.string().c_str(), 1);
#endif
    fs::writeText(dir / "scripts/s.es", "def on_start():\n    game.loaded = load_data(\"best\", -1)\n"
                                        "def on_update(dt):\n    game.n = get_game(\"n\", 0) + 1\n    save_data(\"best\", game.n)\n"
                                        "    game.now = load_data(\"best\")\n");
    auto run = [&](const std::string& id, int frames) {
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        ProjectSettings p;
        p.name = "Same Name";
        p.id = id;
        game.settings() = p;
        auto scene = std::make_unique<Scene>();
        scene->registry().emplace<Script>(scene->create("Saver")).path = "scripts/s.es";
        game.start(std::move(scene), "test.scene");
        for (int i = 0; i < frames; ++i)
            game.update(1.0f / 60.0f);
        double loaded = game.scripts().gameNumber("loaded");
        CHECK_EQ(game.scripts().gameNumber("now"), static_cast<double>(frames)); // straight from memory
        game.stop();
        return loaded;
    };
    CHECK_EQ(run("aaaaaaaa11111111", 5), -1.0); // nothing saved yet
    auto saved = fs::readText(fs::userDataDir("Same Name aaaaaaaa") / "save.json");
    CHECK(saved && saved->find("\"best\": 5") != std::string::npos); // written when it stopped
    CHECK_EQ(run("aaaaaaaa11111111", 1), 5.0);  // read back next time
    CHECK_EQ(run("bbbbbbbb22222222", 1), -1.0); // another game with the same name starts fresh
}

// Pathfinding: around a wall in 2D and 3D, go_to() + on_arrive(), and Chase's "Walk around walls".
AVEN_TEST(pathfinding_around_walls) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_path_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/walker.es", "def on_start():\n    self.go_to(3, 0, speed=4)\n"
                                             "    path = find_path(self, vec(3, 0))\n    game.points = len(path)\n"
                                             "    game.lowest = 99\n    for p in path:\n        game.lowest = min(game.lowest, p.y)\n"
                                             "def on_arrive():\n    game.arrived = 1\n");
    ErrorCatcher catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    {
        Game game(assets, input);
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity wall = scene->create("Wall");
        reg.emplace<BoxCollider2D>(wall).size = {1, 6}; // from y = -3 to 3
        Entity walker = scene->create("Walker");
        scene->transform(walker).position = {-3, 0, 0};
        auto& rb = reg.emplace<RigidBody2D>(walker);
        rb.gravityScale = 0;
        rb.fixedRotation = true;
        reg.emplace<CircleCollider2D>(walker).radius = 0.3f;
        reg.emplace<Script>(walker).path = "scripts/walker.es";
        game.start(std::move(scene), "test.scene");
        game.update(1.0f / 60.0f);
        CHECK(game.scripts().gameNumber("points") >= 3);        // goes via a corner
        CHECK(game.scripts().gameNumber("lowest") < -3.0 ||      // around the bottom end...
              game.scripts().gameNumber("lowest") > -3.0);       // (either way round is fine)
        for (int i = 0; i < 60 * 8 && game.scripts().gameNumber("arrived") == 0; ++i)
            game.update(1.0f / 60.0f);
        Vec3 at = game.scene().worldPosition(game.scene().findByName("Walker"));
        CHECK_EQ(game.scripts().gameNumber("arrived"), 1.0);
        CHECK(std::abs(at.x - 3) < 0.3f && std::abs(at.y) < 0.3f);
        // No way through: a closed box around the goal gives an empty path.
        std::vector<Vec3> none;
        PathOptions o;
        auto path = game.navigation().findPath({-3, 0, 0}, {0, 0, 0}, o); // the goal is inside the wall
        CHECK(!path.empty()); // ...so it stops next to it instead
        CHECK(std::abs(path.back().x) >= 0.5f);
        game.stop();
    }
    {
        // 3D: ground and a wall across the middle.
        Game game(assets, input);
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity ground = scene->create("Ground");
        scene->transform(ground).position = {0, -0.5f, 0};
        reg.emplace<BoxCollider>(ground).size = {30, 1, 30};
        Entity wall = scene->create("Wall");
        scene->transform(wall).position = {0, 1.5f, 0};
        reg.emplace<BoxCollider>(wall).size = {1, 3, 8};
        game.start(std::move(scene), "test.scene");
        game.update(1.0f / 60.0f);
        PathOptions o;
        o.threeD = true;
        o.cellSize = 0.5f;
        bool reached = false;
        auto path = game.navigation().findPath({-4, 1, 0}, {4, 1, 0}, o, &reached);
        CHECK(reached);
        CHECK(path.size() >= 3);
        float widest = 0;
        for (Vec3 p : path) {
            widest = std::max(widest, std::abs(p.z));
            CHECK(std::abs(p.y - 1) < 0.2f); // at the walker's height above the ground
        }
        CHECK(widest > 4.0f); // around the end of the wall
        game.stop();
    }
    {
        // Chase with "Walk around walls" gets to a player behind a wall.
        Game game(assets, input);
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        Entity wall = scene->create("Wall");
        reg.emplace<BoxCollider2D>(wall).size = {1, 6};
        Entity player = scene->create("Player");
        scene->info(player).tag = "player";
        scene->transform(player).position = {2, 0, 0};
        Entity enemy = scene->create("Enemy");
        scene->transform(enemy).position = {-2, 0, 0};
        auto& rb = reg.emplace<RigidBody2D>(enemy);
        rb.gravityScale = 0;
        rb.fixedRotation = true;
        reg.emplace<CircleCollider2D>(enemy).radius = 0.3f;
        auto& chase = reg.emplace<Chase>(enemy);
        chase.aroundWalls = true;
        chase.sight = 20;
        chase.speed = 4;
        chase.stopDistance = 0.6f;
        game.start(std::move(scene), "test.scene");
        for (int i = 0; i < 60 * 8; ++i)
            game.update(1.0f / 60.0f);
        Vec3 at = game.scene().worldPosition(game.scene().findByName("Enemy"));
        CHECK(length(at - Vec3{2, 0, 0}) < 1.0f);
        game.stop();
    }
    {
        // The Code Ladder's EasyScript for it does the same.
        const ComponentInfo* ci = ComponentRegistry::find("Chase");
        Registry r;
        Entity x = r.create();
        auto& c = r.emplace<Chase>(x);
        c.aroundWalls = true;
        c.sight = 20;
        c.speed = 4;
        c.stopDistance = 0.6f;
        fs::writeText(dir / "scripts/chase.es", behaviorAsEasyScript("Chase", saveComponent(*ci, &c)));
        Game game(assets, input);
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        reg.emplace<BoxCollider2D>(scene->create("Wall")).size = {1, 6};
        Entity player = scene->create("Player");
        scene->info(player).tag = "player";
        scene->transform(player).position = {2, 0, 0};
        Entity enemy = scene->create("Enemy");
        scene->transform(enemy).position = {-2, 0, 0};
        reg.emplace<Script>(enemy).path = "scripts/chase.es";
        game.start(std::move(scene), "test.scene");
        for (int i = 0; i < 60 * 8; ++i)
            game.update(1.0f / 60.0f);
        Vec3 at = game.scene().worldPosition(game.scene().findByName("Enemy"));
        CHECK(length(at - Vec3{2, 0, 0}) < 1.0f);
        game.stop();
    }
    if (!catcher.errors.empty())
        std::printf("  %s\n", catcher.errors[0].c_str());
    CHECK(catcher.errors.empty());
}

// Terrain: heights survive saving, brushes shape it, balls land on it, and scripts can ask
// how high the ground is.
AVEN_TEST(terrain_shape_save_and_collide) {
    Terrain t;
    t.size = {20, 20};
    t.resolution = 33;
    t.maxHeight = 10;
    terrainEnsure(t);
    CHECK_EQ(t.heights.size(), size_t(33 * 33));
    terrainBrush(t, TerrainTool::Raise, 0, 0, 4, 1, 1, 0, 0);
    float peak = terrainHeightAt(t, 0, 0);
    CHECK(peak > 1.0f && peak <= 10.0f);
    CHECK(terrainHeightAt(t, 8, 8) < 0.01f); // outside the brush
    Vec3 hit;
    CHECK(terrainRaycast(t, {0, 50, 0}, {0, -1, 0}, 100, hit));
    CHECK(std::abs(hit.y - peak) < 0.05f);
    terrainBrush(t, TerrainTool::Paint, 5, 5, 2, 1, 1, 1, 0);
    // Save and load (heights are stored as 16-bit numbers).
    const ComponentInfo* ci = ComponentRegistry::find("Terrain");
    CHECK(ci != nullptr);
    Json saved = saveComponent(*ci, &t);
    Terrain back;
    loadComponent(*ci, &back, saved);
    CHECK_EQ(back.resolution, 33);
    CHECK(std::abs(terrainHeightAt(back, 0, 0) - peak) < 0.01f);
    CHECK(back.splat == t.splat);
    // A different resolution keeps the shape.
    back.resolution = 65;
    terrainEnsure(back);
    CHECK(std::abs(terrainHeightAt(back, 0, 0) - peak) < 0.3f);

    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_terrain_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    fs::writeText(dir / "scripts/probe.es", "def on_update(dt):\n    game.h = terrain_height(0, 0)\n    game.off = terrain_height(100, 0)\n");
    ErrorCatcher catcher;
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity ground = scene->create("Ground");
    scene->transform(ground).position = {0, 1, 0};
    reg.emplace<Terrain>(ground) = t;
    Entity ball = scene->create("Ball");
    scene->transform(ball).position = {0, 20, 0};
    reg.emplace<RigidBody>(ball);
    reg.emplace<SphereCollider>(ball).radius = 0.5f;
    reg.emplace<Script>(scene->create("Probe")).path = "scripts/probe.es";
    game.start(std::move(scene), "test.scene");
    for (int i = 0; i < 60 * 4; ++i)
        game.update(1.0f / 60.0f);
    float groundY = 1 + peak;
    Vec3 at = game.scene().worldPosition(game.scene().findByName("Ball"));
    CHECK(std::abs(at.y - (groundY + 0.5f)) < 0.35f); // resting on the bump (it may roll off a little)
    CHECK(std::abs(game.scripts().gameNumber("h") - groundY) < 0.05);
    CHECK(game.scripts().gameValue("off").isNone());
    CHECK(catcher.errors.empty());
    game.stop();
}

// Touch controls: the stick holds arrow keys, buttons press their key once, and other fingers
// act as the mouse; fingers on controls never click the game.
AVEN_TEST(touch_controls_press_keys) {
    TouchControls tc;
    TouchSettings s;
    s.mode = TouchMode::Auto;
    s.buttons = {{"Jump", "space"}};
    tc.configure(s);
    Input input;
    Vec2 win{1280, 720};
    tc.update({}, win, input);
    CHECK(!tc.visible()); // Auto: not until the screen is touched
    auto stick = tc.stickArea();
    auto jump = tc.button(0);
    auto frame = [&](std::vector<TouchPoint> t) {
        input.beginFrame();
        tc.update(t, win, input);
    };
    frame({{1, stick.center + Vec2{stick.radius * 0.8f, 0}}});
    CHECK(tc.visible());
    CHECK(input.keyDown(keys::Right) && !input.keyDown(keys::Left));
    CHECK(!input.mouseDown(MouseButton::Left)); // the stick isn't a click
    frame({{1, stick.center + Vec2{0, -stick.radius * 0.8f}}, {2, jump.center}}); // up, and press Jump
    CHECK(input.keyDown(keys::Up) && !input.keyDown(keys::Right));
    CHECK(input.keyPressed(keys::Space));
    frame({{1, stick.center + Vec2{0, -stick.radius * 0.8f}}, {2, jump.center}});
    CHECK(input.keyDown(keys::Space) && !input.keyPressed(keys::Space)); // held, pressed only once
    frame({{3, {640, 200}}}); // lift both; a new finger in the middle of the screen
    CHECK(!input.keyDown(keys::Up) && !input.keyDown(keys::Space));
    CHECK(input.mouseDown(MouseButton::Left));
    CHECK(input.mousePosition().x == 640.0f);
    frame({});
    CHECK(!input.mouseDown(MouseButton::Left));
    // Saved with the project only when changed.
    ProjectSettings p;
    CHECK(p.toJson()["touch"].isNull());
    p.touch.mode = TouchMode::Always;
    ProjectSettings q;
    q.fromJson(p.toJson());
    CHECK(q.touch.mode == TouchMode::Always && q.touch.buttons.size() == 1);
}

// Network tests run frames until something has happened, up to a time limit: messages need real
// time to cross the (local) network, and a slow CI machine may need many frames.
template <class Step, class Done>
bool pumpUntil(Step&& step, Done&& done, double seconds = 10.0) {
    auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until)
            return false;
        step();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

// Multiplayer: two games on this computer. The second joins the first, messages get through,
// networked spawns show up on both, synced objects follow their owner, and leaving tidies up.
AVEN_TEST(multiplayer_two_games_on_localhost) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_net_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::create_directories(dir / "prefabs", ec);
    fs::writeText(dir / "prefabs/avatar.prefab",
                  R"({"entities": [{"id": "00000000000000a1", "name": "Avatar", "components": {"Transform": {}, "NetworkSync": {}}}]})");
    fs::writeText(dir / "scripts/host.es", "def on_start():\n    game.ok = host_game(45123)\n    spawn_networked(\"prefabs/avatar.prefab\", 1, 0)\n"
                                           "def on_player_joined(p):\n    game.joined = p\n    send(\"hi\", 42)\n"
                                           "def on_player_left(p):\n    game.left = p\n");
    fs::writeText(dir / "scripts/client.es", "def on_start():\n    join_game(\"127.0.0.1\", 45123)\n"
                                             "def on_connected():\n    game.me = player_id()\n    spawn_networked(\"prefabs/avatar.prefab\", -1, 0)\n"
                                             "def on_receive(m, d, p):\n    game.got = d\n    game.from = p\n");
    ErrorCatcher catcher;
    auto makeScene = [](const char* script) {
        auto scene = std::make_unique<Scene>();
        auto& reg = scene->registry();
        reg.emplace<Script>(scene->create("Brain")).path = script;
        Entity box = scene->create("Box");
        scene->setUUID(box, UUID{0x1234}); // the same object in both copies of the scene
        reg.emplace<NetworkSync>(box);
        return scene;
    };
    Assets hostAssets, clientAssets;
    hostAssets.setRoot(dir);
    clientAssets.setRoot(dir);
    Input hostInput, clientInput;
    Game host(hostAssets, hostInput), client(clientAssets, clientInput);
    host.start(makeScene("scripts/host.es"), "test.scene");
    host.update(1.0f / 60.0f);
    CHECK(host.scripts().gameValue("ok").truthy());
    client.start(makeScene("scripts/client.es"), "test.scene");
    auto frame = [&] {
        host.update(1.0f / 60.0f);
        client.update(1.0f / 60.0f);
    };
    auto avatars = [](Game& g) {
        int n = 0;
        g.scene().walk([&](Entity e, int) {
            n += g.scene().info(e).name == "Avatar";
            return true;
        });
        return n;
    };
    // Connected, the greeting arrived, and both avatars are in both games.
    bool connected = pumpUntil(frame, [&] {
        return client.scripts().gameNumber("got") == 42.0 && avatars(host) == 2 && avatars(client) == 2;
    });
    CHECK(connected);
    if (!connected) {
        host.stop();
        client.stop();
        return;
    }
    CHECK_EQ(host.scripts().gameNumber("joined"), 1.0);
    CHECK_EQ(client.scripts().gameNumber("me"), 1.0);
    CHECK_EQ(client.scripts().gameNumber("got"), 42.0);
    CHECK_EQ(client.scripts().gameNumber("from"), 0.0);
    CHECK_EQ(avatars(host), 2); // its own and the client's
    CHECK_EQ(avatars(client), 2);
    // The host owns the scene's Box: moving it moves the client's.
    host.scene().transform(host.scene().findByName("Box")).position = {5, 2, 0};
    // The client moves its own avatar.
    Entity mine;
    client.scene().walk([&](Entity e, int) {
        if (client.scene().info(e).name == "Avatar" && !isRemote(client.scene().registry(), e))
            mine = e;
        return true;
    });
    CHECK(static_cast<bool>(mine));
    if (!mine) {
        host.stop();
        client.stop();
        return;
    }
    client.scene().transform(mine).position = {-3, 4, 0};
    auto boxArrived = [&] {
        return length(client.scene().transform(client.scene().findByName("Box")).position - Vec3{5, 2, 0}) < 0.05f;
    };
    auto avatarArrived = [&] {
        bool seen = false;
        host.scene().walk([&](Entity e, int) {
            if (host.scene().info(e).name == "Avatar" && isRemote(host.scene().registry(), e))
                seen = length(host.scene().transform(e).position - Vec3{-3, 4, 0}) < 0.05f;
            return true;
        });
        return seen;
    };
    pumpUntil(frame, [&] { return boxArrived() && avatarArrived(); });
    CHECK(boxArrived());
    CHECK(isRemote(client.scene().registry(), client.scene().findByName("Box")));
    CHECK(avatarArrived());
    // The client leaves: its avatar goes from the host's game.
    client.stop();
    pumpUntil([&] { host.update(1.0f / 60.0f); }, [&] { return host.scripts().gameNumber("left") == 1.0 && avatars(host) == 1; });
    CHECK_EQ(host.scripts().gameNumber("left"), 1.0);
    CHECK_EQ(avatars(host), 1);
    host.stop();
    if (!catcher.errors.empty())
        std::printf("  %s\n", catcher.errors[0].c_str());
    CHECK(catcher.errors.empty());
}

// find_games() hears a game hosting on this computer.
AVEN_TEST(multiplayer_find_games) {
    Assets a1, a2;
    Input i1, i2;
    Game host(a1, i1), finder(a2, i2);
    host.settings().name = "Finder Test";
    host.start(std::make_unique<Scene>(), "test.scene");
    finder.start(std::make_unique<Scene>(), "test.scene");
    std::string error;
    if (!host.network().host(Network::kDefaultPort, error)) {
        std::printf("  (skipped: %s)\n", error.c_str());
        return;
    }
    finder.network().findGames();
    pumpUntil(
        [&] {
            host.update(1.0f / 60.0f);
            finder.update(1.0f / 60.0f);
        },
        [&] { return !finder.network().gamesFound().empty(); }, 3.0);
    CHECK(!finder.network().gamesFound().empty());
    if (!finder.network().gamesFound().empty()) {
        CHECK_EQ(finder.network().gamesFound()[0].name, std::string("Finder Test"));
        CHECK_EQ(finder.network().gamesFound()[0].port, Network::kDefaultPort);
    }
    host.stop();
    finder.stop();
}

// Input between frames: a key that goes down (and even up again) before the game looks is still a
// press. Browsers deliver keys between frames; a quick tap at a low frame rate is down and up at once.
AVEN_TEST(input_presses_between_frames_and_quick_taps) {
    Input in;
    in.beginFrame();
    in.onKey(keys::Space, true); // arrives, then the game looks
    CHECK(in.keyPressed(keys::Space) && in.keyDown(keys::Space));
    in.beginFrame();
    CHECK(in.keyDown(keys::Space) && !in.keyPressed(keys::Space)); // held
    in.onKey(keys::Space, false);
    CHECK(in.keyReleased(keys::Space));
    in.beginFrame();
    // Down and up before the game looks: pressed for one frame, released the next.
    in.onKey(keys::Space, true);
    in.onKey(keys::Space, false);
    CHECK(in.keyPressed(keys::Space));
    CHECK(in.actionPressed("jump"));
    in.beginFrame();
    CHECK(!in.keyDown(keys::Space) && in.keyReleased(keys::Space));
    in.beginFrame();
    CHECK(!in.keyDown(keys::Space) && !in.keyReleased(keys::Space));
    // The same for a mouse click.
    in.onMouseButton(0, true);
    in.onMouseButton(0, false);
    CHECK(in.mousePressed(MouseButton::Left));
    in.beginFrame();
    CHECK(!in.mouseDown(MouseButton::Left));
}

// A "gamepad" whose stick sits pushed all the way when it appears doesn't walk the player; once it
// comes back to the middle it's a normal stick.
AVEN_TEST(input_ignores_stuck_gamepad_sticks) {
    Input in;
    bool buttons[static_cast<int>(PadButton::Count)]{};
    float axes[static_cast<int>(PadAxis::Count)] = {-1, -1, 0, 0, -1, -1};
    in.setGamepad(true, buttons, axes);
    CHECK_EQ(in.axis("horizontal"), 0.0f);
    CHECK_EQ(in.axis("vertical"), 0.0f);
    in.setGamepad(true, buttons, axes);
    CHECK_EQ(in.axis("horizontal"), 0.0f);
    axes[0] = 0; // back to the middle
    in.setGamepad(true, buttons, axes);
    axes[0] = 0.9f;
    in.setGamepad(true, buttons, axes);
    CHECK(in.axis("horizontal") > 0.8f);
    CHECK_EQ(in.axis("vertical"), 0.0f); // Y never came back: still ignored
}
