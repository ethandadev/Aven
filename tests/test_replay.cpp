#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/replay.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"

#include <filesystem>

using namespace aven;

namespace {

std::unique_ptr<Scene> replayScene() {
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity ground = scene->create("Ground");
    scene->transform(ground).position = {0, -3, 0};
    reg.emplace<BoxCollider2D>(ground).size = {40, 1};
    Entity player = scene->create("Player");
    scene->info(player).tag = "player";
    reg.emplace<RigidBody2D>(player).fixedRotation = true;
    reg.emplace<BoxCollider2D>(player);
    reg.emplace<PlatformerController>(player);
    Entity wanderer = scene->create("Wanderer");
    reg.emplace<Script>(wanderer).path = "scripts/wander.es";
    Entity spawner = scene->create("Spawner");
    scene->transform(spawner).position = {0, 6, 0};
    auto& sp = reg.emplace<Spawner>(spawner);
    sp.prefab = "prefabs/ball.prefab";
    sp.interval = 0.3f;
    sp.randomRange = {5, 0};
    return scene;
}

// Where everything ended up, by name and order.
std::vector<Vec3> positions(Game& game) {
    std::vector<Vec3> out;
    game.scene().walk([&](Entity e, int) {
        out.push_back(game.scene().transform(e).position);
        return true;
    });
    return out;
}

} // namespace

AVEN_TEST(replay_plays_back_exactly) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_replay_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    stdfs::create_directories(dir / "prefabs", ec);
    fs::writeText(dir / "scripts/wander.es", "def on_update(dt):\n    self.x += random_range(-1, 1) * dt * 10\n"
                                             "    self.y = random() * 3\n");
    fs::writeText(dir / "prefabs/ball.prefab",
                  R"({"entities": [{"id": "0000000000000001", "name": "Ball", "components": {"Transform": {}, "RigidBody2D": {},
                     "CircleCollider2D": {"radius": 0.3}}}]})");

    Replay header;
    header.seed = 424242;
    header.scenePath = "scenes/test.scene";
    header.startScene = replayScene()->save();
    stdfs::path file = dir / "session.replay";

    std::vector<Vec3> recorded;
    {
        Assets assets;
        assets.setRoot(dir);
        Input input;
        Game game(assets, input);
        ReplayRecorder recorder;
        recorder.begin(file, header);
        auto scene = std::make_unique<Scene>();
        CHECK(scene->load(header.startScene));
        game.setRandomSeed(header.seed);
        game.start(std::move(scene), header.scenePath);
        for (int i = 0; i < 240; ++i) {
            input.beginFrame();
            input.onKey(Input::keyFromName("right"), i > 20 && i < 120);
            input.onKey(Input::keyFromName("space"), i == 60 || i == 61 || i == 150);
            input.onMouseMove({static_cast<float>(i * 3), 200.0f});
            float dt = (i % 3 == 0) ? 1.0f / 50.0f : 1.0f / 70.0f; // uneven frames, like a real editor
            game.setScreenSize({800, 600});
            recorder.addFrame(dt, {800, 600}, input);
            game.update(dt);
        }
        recorder.addEvent("error", "something went wrong", "scripts/wander.es", 2);
        recorder.end();
        recorded = positions(game);
        game.stop();
    }

    Replay loaded;
    std::string error;
    CHECK(loaded.load(file, &error));
    CHECK(error.empty());
    CHECK_EQ(loaded.frames.size(), size_t(240));
    CHECK_EQ(loaded.events.size(), size_t(1));
    CHECK(loaded.cleanExit);
    CHECK_EQ(loaded.seed, 424242u);

    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    ReplayPlayer player;
    CHECK(player.start(game, input, loaded));
    while (player.step()) {
    }
    std::vector<Vec3> replayed = positions(game);
    CHECK_EQ(replayed.size(), recorded.size());
    CHECK(recorded.size() > 5); // balls were spawned
    bool same = replayed.size() == recorded.size();
    for (size_t i = 0; same && i < recorded.size(); ++i)
        same = replayed[i].x == recorded[i].x && replayed[i].y == recorded[i].y;
    CHECK(same);
    game.stop();
}
