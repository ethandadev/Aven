#include "test_framework.h"

#include "aven/scene/reflection.h"
#include "aven/scene/scene.h"

using namespace aven;

namespace {
struct TestHealth {
    int value = 100;
};
struct Speed {
    float value = 1;
};
} // namespace

AVEN_TEST(ecs_create_destroy_generations) {
    Registry r;
    Entity a = r.create();
    r.emplace<TestHealth>(a, 50);
    CHECK(r.valid(a));
    CHECK_EQ(r.get<TestHealth>(a).value, 50);
    r.destroy(a);
    CHECK(!r.valid(a));
    Entity b = r.create(); // reuses the slot with a new generation
    CHECK_EQ(b.index, a.index);
    CHECK(!r.valid(a));
    CHECK(!r.has<TestHealth>(b));
    CHECK(Entity::fromHandle(b.toHandle()) == b);
}

AVEN_TEST(ecs_each_filters_and_survives_removal) {
    Registry r;
    std::vector<Entity> es;
    for (int i = 0; i < 10; ++i) {
        Entity e = r.create();
        r.emplace<TestHealth>(e, i);
        if (i % 2 == 0)
            r.emplace<Speed>(e, float(i));
        es.push_back(e);
    }
    int visited = 0;
    r.each<TestHealth, Speed>([&](Entity e, TestHealth& h, Speed& s) {
        ++visited;
        CHECK_EQ(float(h.value), s.value);
        r.destroy(e); // destroying during iteration must be safe
    });
    CHECK_EQ(visited, 5);
    CHECK_EQ(r.count<TestHealth>(), size_t(5));
    CHECK_EQ(r.aliveCount(), size_t(5));
}

AVEN_TEST(reflection_names) {
    CHECK_EQ(toSnakeCase("RigidBody2D"), std::string("rigid_body_2d"));
    CHECK_EQ(toSnakeCase("UIElement"), std::string("ui_element"));
    CHECK_EQ(toSnakeCase("gravityScale"), std::string("gravity_scale"));
    CHECK_EQ(toSnakeCase("flipX"), std::string("flip_x"));
    CHECK_EQ(toLabel("gravity_scale"), std::string("Gravity Scale"));
    CHECK(ComponentRegistry::find("SpriteRenderer") != nullptr);
    CHECK(ComponentRegistry::find("rigid_body_2d") != nullptr);
    CHECK(ComponentRegistry::find("spriterenderer") != nullptr);
    CHECK(ComponentRegistry::find("NotAThing") == nullptr);
}

AVEN_TEST(scene_hierarchy_world_transforms) {
    Scene s;
    Entity parent = s.create("Parent");
    Entity child = s.create("Child", parent);
    s.transform(parent).position = {10, 0, 0};
    s.transform(parent).scale = {2, 2, 2};
    s.transform(child).position = {1, 0, 0};
    s.updateTransforms();
    CHECK_NEAR(s.worldPosition(child).x, 12, 1e-5);
    CHECK(s.parent(child) == parent);
    CHECK_EQ(s.children(parent).size(), size_t(1));
    CHECK_EQ(s.roots().size(), size_t(1));

    // Re-parenting keeps the world position by default.
    CHECK(s.setParent(child, {}));
    CHECK_NEAR(s.transform(child).position.x, 12, 1e-4);
    CHECK_NEAR(s.transform(child).scale.x, 2, 1e-4);
    CHECK_EQ(s.roots().size(), size_t(2));

    // Cycles are rejected.
    CHECK(s.setParent(child, parent));
    CHECK(!s.setParent(parent, child));
}

AVEN_TEST(scene_destroy_is_recursive) {
    Scene s;
    Entity a = s.create("A");
    Entity b = s.create("B", a);
    Entity c = s.create("C", b);
    s.destroy(a);
    CHECK(!s.valid(a));
    CHECK(!s.valid(b));
    CHECK(!s.valid(c));
    CHECK_EQ(s.entityCount(), size_t(0));
    CHECK(s.roots().empty());
}

AVEN_TEST(scene_save_load_roundtrip) {
    Scene s;
    s.name = "Level 1";
    Entity player = s.create("Player");
    s.info(player).tag = "player";
    s.transform(player).position = {1, 2, 3};
    auto& sprite = s.registry().emplace<SpriteRenderer>(player);
    sprite.color = {1, 0, 0, 1};
    sprite.shape = Shape2D::Circle;
    s.registry().emplace<RigidBody2D>(player).fixedRotation = true;
    auto& script = s.registry().emplace<Script>(player);
    script.path = "scripts/player.es";
    script.overrides["speed"] = 7;
    Entity hat = s.create("Hat", player);
    s.info(hat).active = false;
    Entity cam = s.create("Camera");
    s.registry().emplace<Camera>(cam);
    s.registry().emplace<CameraFollow>(cam).target = s.info(player).uuid;

    Json saved = s.save();
    Scene loaded;
    std::string err;
    CHECK(loaded.load(Json::parse(saved.dump(2)), &err));
    CHECK_EQ(loaded.name, std::string("Level 1"));
    CHECK_EQ(loaded.entityCount(), size_t(3));
    Entity p2 = loaded.findByName("Player");
    CHECK(static_cast<bool>(p2));
    CHECK_EQ(loaded.info(p2).tag, std::string("player"));
    CHECK_NEAR(loaded.transform(p2).position.z, 3, 1e-6);
    CHECK(loaded.registry().get<SpriteRenderer>(p2).shape == Shape2D::Circle);
    CHECK(loaded.registry().get<RigidBody2D>(p2).fixedRotation);
    CHECK_EQ(loaded.registry().get<Script>(p2).overrides["speed"].asInt(), 7);
    Entity hat2 = loaded.findByName("Hat");
    CHECK(loaded.parent(hat2) == p2);
    CHECK(!loaded.isActive(hat2));
    Entity cam2 = loaded.findByName("Camera");
    CHECK(loaded.findByUUID(loaded.registry().get<CameraFollow>(cam2).target) == p2);
    // Saving again gives identical output.
    CHECK_EQ(loaded.save().dump(2), saved.dump(2));
}

AVEN_TEST(scene_instantiate_remaps_ids) {
    Scene s;
    Entity root = s.create("Turret");
    Entity barrel = s.create("Barrel", root);
    s.registry().emplace<CameraFollow>(root).target = s.info(barrel).uuid;
    Json prefab = s.saveEntities({root});

    auto copies = s.instantiate(prefab);
    CHECK_EQ(copies.size(), size_t(1));
    Entity copy = copies[0];
    CHECK(copy != root);
    CHECK(!(s.info(copy).uuid == s.info(root).uuid));
    CHECK_EQ(s.children(copy).size(), size_t(1));
    Entity copyBarrel = s.children(copy)[0];
    // The reference points at the copied barrel, not the original.
    CHECK(s.findByUUID(s.registry().get<CameraFollow>(copy).target) == copyBarrel);
    CHECK_EQ(s.entityCount(), size_t(4));
}

AVEN_TEST(scene_duplicate_places_after_original) {
    Scene s;
    Entity a = s.create("A");
    Entity b = s.create("B");
    Entity a2 = s.duplicate(a);
    CHECK_EQ(s.roots().size(), size_t(3));
    CHECK(s.roots()[0] == a);
    CHECK(s.roots()[1] == a2);
    CHECK(s.roots()[2] == b);
}

AVEN_TEST(scene_unknown_component_is_skipped) {
    Scene s;
    std::string err;
    CHECK(s.load(Json::parse(R"({"entities":[{"id":"1","name":"X","components":{"Bogus":{},"Transform":{"position":[5,0,0]}}}]})"),
                 &err));
    Entity x = s.findByName("X");
    CHECK_NEAR(s.transform(x).position.x, 5, 1e-6);
}

// Hand-edited scenes: a camelCase field name is still read, and a bad choice keeps the default.
AVEN_TEST(scene_forgives_camel_case_fields) {
    Json data = Json::parse(R"({"entities": [{"id": "0000000000000001", "name": "Box", "components": {
        "RigidBody2D": {"gravityScale": 0.5, "type": "Sideways"}}}]})");
    Scene s;
    CHECK(s.load(data));
    Entity e = s.findByName("Box");
    auto* rb = s.registry().tryGet<RigidBody2D>(e);
    CHECK(rb != nullptr);
    CHECK_NEAR(rb->gravityScale, 0.5f, 1e-6f);
    CHECK(rb->type == BodyType::Dynamic);
}
