#include "test_framework.h"

#include "aven/assets/assets.h"
#include "aven/core/fs.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"
#include "aven/scene/scene.h"

#include <filesystem>
#include <set>

using namespace aven;

AVEN_TEST(tilemap_keys_sort_by_row_then_column) {
    Tilemap t;
    for (int y : {-3, 0, 2})
        for (int x : {5, -7, 0, -1, 3})
            t.set(x, y, 1);
    int lastY = -1000, lastX = -1000;
    for (auto& [k, tile] : t.tiles) {
        int x = Tilemap::keyX(k), y = Tilemap::keyY(k);
        CHECK(y > lastY || (y == lastY && x > lastX));
        lastY = y;
        lastX = x;
    }
    CHECK_EQ(t.get(-7, -3), 1);
    CHECK_EQ(t.get(4, 0), -1);
    uint32_t v = t.version;
    CHECK(!t.set(-7, -3, 1)); // no change
    CHECK_EQ(t.version, v);
    CHECK(t.set(-7, -3, -1)); // erase
    CHECK_EQ(t.get(-7, -3), -1);
}

AVEN_TEST(tilemap_saves_as_runs_and_loads_back) {
    Scene scene;
    Entity e = scene.create("Level");
    auto& t = scene.registry().emplace<Tilemap>(e);
    t.tileset = "images/tiles.png";
    for (int x = -10; x <= 10; ++x)
        t.set(x, -3, 0); // a floor crossing x = 0 is one run
    t.set(4, 1, 3);
    t.set(5, 1, 3);
    t.set(6, 1, 9);
    Json saved = scene.save();
    std::string text = saved.dump(1);
    Scene loaded;
    CHECK(loaded.load(Json::parse(text)));
    Entity le;
    loaded.walk([&](Entity x, int) {
        if (loaded.info(x).name == "Level")
            le = x;
        return true;
    });
    CHECK(le);
    auto* lt = loaded.registry().tryGet<Tilemap>(le);
    CHECK(lt != nullptr);
    CHECK_EQ(lt->tiles.size(), t.tiles.size());
    CHECK(lt->tiles == t.tiles);
    CHECK_EQ(lt->tileset, std::string("images/tiles.png"));
    // Three runs: the floor (crossing x = 0), two bricks, one crate.
    const Json& runs = saved["entities"][0]["components"]["Tilemap"]["tiles"];
    CHECK_EQ(runs.size(), size_t(3));
    CHECK_EQ(runs[0][0].asInt(), -10);
    CHECK_EQ(runs[0][3].asInt(), 21);
}

AVEN_TEST(tilemap_rects_cover_exactly_the_tiles) {
    Tilemap t;
    // An L shape, a floating block and a 3x3 square.
    for (int x = 0; x < 8; ++x)
        t.set(x, 0, 1);
    for (int y = 1; y < 5; ++y)
        t.set(0, y, 1);
    t.set(10, 3, 2);
    for (int y = 6; y < 9; ++y)
        for (int x = 3; x < 6; ++x)
            t.set(x, y, 4);
    auto rects = tileRects(t);
    std::set<std::pair<int, int>> covered;
    for (auto& r : rects)
        for (int y = r.y0; y <= r.y1; ++y)
            for (int x = r.x0; x <= r.x1; ++x) {
                CHECK(t.get(x, y) >= 0);                  // never covers an empty cell
                CHECK(covered.insert({x, y}).second);    // never overlaps
            }
    CHECK_EQ(covered.size(), t.tiles.size());
    // The 3x3 square merges into one rectangle and the floor into one run.
    CHECK(rects.size() <= 5);
}

AVEN_TEST(tilemap_is_solid_and_scripts_can_change_it) {
    namespace stdfs = std::filesystem;
    stdfs::path dir = stdfs::temp_directory_path() / "aven_tilemap_test";
    std::error_code ec;
    stdfs::remove_all(dir, ec);
    stdfs::create_directories(dir / "scripts", ec);
    // Digs out the tile under the crate after a second.
    fs::writeText(dir / "scripts/dig.es", "time = 0\n\ndef on_update(dt):\n    time += dt\n"
                                           "    if time > 1 and self.get_tile_at(0.5, -0.5) >= 0:\n"
                                           "        self.set_tile_at(0.5, -0.5, -1)\n");
    Assets assets;
    assets.setRoot(dir);
    Input input;
    Game game(assets, input);
    auto scene = std::make_unique<Scene>();
    auto& reg = scene->registry();
    Entity level = scene->create("Level");
    auto& map = reg.emplace<Tilemap>(level);
    for (int x = -5; x <= 5; ++x)
        map.set(x, -1, 0); // floor with its top at y = 0
    reg.emplace<Script>(level).path = "scripts/dig.es";
    Entity crate = scene->create("Crate");
    scene->transform(crate).position = {0.5f, 2, 0};
    reg.emplace<RigidBody2D>(crate).fixedRotation = true;
    reg.emplace<BoxCollider2D>(crate).size = {0.8f, 0.8f};
    game.start(std::move(scene), "scenes/test.scene");
    for (int i = 0; i < 50; ++i) // under a second: it lands on the floor
        game.update(1.0f / 60.0f);
    float y = game.scene().worldPosition(crate).y;
    CHECK(y > 0.3f && y < 0.5f);
    CHECK(game.physics2D().isOnGround(crate));
    for (int i = 0; i < 90; ++i) // the script digs the hole and the crate falls through
        game.update(1.0f / 60.0f);
    CHECK_EQ(game.scene().registry().get<Tilemap>(game.scene().findByName("Level")).get(0, -1), -1);
    CHECK(game.scene().worldPosition(crate).y < -0.5f);
    game.stop();
}

AVEN_TEST(tilemap_pass_through_tiles_have_no_collision) {
    Tilemap t;
    for (int x = 0; x < 6; ++x)
        t.set(x, 0, x < 3 ? 0 : 6); // ground, then water
    uint64_t before = t.shapeSignature();
    t.setSolidTile(6, false);
    t.setSolidTile(15, false);
    CHECK_EQ(t.notSolid, std::string("6, 15"));
    CHECK(t.shapeSignature() != before); // physics rebuilds
    CHECK(!t.isSolidTile(6));
    CHECK(t.isSolidTile(0));
    auto rects = tileRects(t);
    CHECK_EQ(rects.size(), size_t(1));
    CHECK_EQ(rects[0].x0, 0);
    CHECK_EQ(rects[0].x1, 2);
    t.setSolidTile(6, true);
    CHECK_EQ(t.notSolid, std::string("15"));
}
