#include "test_framework.h"

#include "rynax/scene/reflection.h"
#include "rynax/scene/scene.h"

using namespace rynax;

namespace {
struct TestHealth {
    int value = 100;
};
struct Speed {
    float value = 1;
};
} // namespace

RYNAX_TEST(ecs_create_destroy_generations) {
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

RYNAX_TEST(ecs_each_filters_and_survives_removal) {
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

RYNAX_TEST(reflection_names) {
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

RYNAX_TEST(scene_hierarchy_world_transforms) {
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

RYNAX_TEST(scene_destroy_is_recursive) {
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

RYNAX_TEST(scene_save_load_roundtrip) {
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

RYNAX_TEST(scene_instantiate_remaps_ids) {
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

RYNAX_TEST(scene_duplicate_places_after_original) {
    Scene s;
    Entity a = s.create("A");
    Entity b = s.create("B");
    Entity a2 = s.duplicate(a);
    CHECK_EQ(s.roots().size(), size_t(3));
    CHECK(s.roots()[0] == a);
    CHECK(s.roots()[1] == a2);
    CHECK(s.roots()[2] == b);
}

RYNAX_TEST(scene_unknown_component_is_skipped) {
    Scene s;
    std::string err;
    CHECK(s.load(Json::parse(R"({"entities":[{"id":"1","name":"X","components":{"Bogus":{},"Transform":{"position":[5,0,0]}}}]})"),
                 &err));
    Entity x = s.findByName("X");
    CHECK_NEAR(s.transform(x).position.x, 5, 1e-6);
}

// Hand-edited scenes: a camelCase field name is still read, and a bad choice keeps the default.
RYNAX_TEST(scene_forgives_camel_case_fields) {
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

// Hand-edited and merged files: a child listed before its parent still finds it; a parent loop
// doesn't hang (the entities end up at the top); prefab entries without an id don't adopt others.
RYNAX_TEST(scene_load_links_parents_in_any_order) {
    Scene s;
    CHECK(s.load(Json::parse(R"({"entities":[
        {"id":"2","name":"Child","parent":"1","components":{}},
        {"id":"1","name":"Parent","components":{}},
        {"id":"3","name":"LoopA","parent":"4","components":{}},
        {"id":"4","name":"LoopB","parent":"3","components":{}},
        {"id":"5","name":"Self","parent":"5","components":{}}]})")));
    Entity child = s.findByName("Child"), parent = s.findByName("Parent");
    CHECK(s.parent(child) == parent);
    // One of the loop is linked, the other can't be: no entity is its own ancestor.
    Entity a = s.findByName("LoopA"), b = s.findByName("LoopB");
    CHECK(!(s.isAncestor(a, a)) && !(s.isAncestor(b, b)));
    CHECK(!s.parent(s.findByName("Self")));

    Scene p;
    auto tops = p.instantiate(Json::parse(R"({"entities":[
        {"name":"NoId","components":{}},
        {"name":"AlsoNoId","components":{}},
        {"id":"9","name":"Kid","parent":"8","components":{}},
        {"id":"8","name":"Top","components":{}}]})"));
    CHECK_EQ(tops.size(), size_t(3)); // NoId, AlsoNoId and Top; Kid is under Top
    CHECK(!p.parent(p.findByName("AlsoNoId")));
    CHECK(p.parent(p.findByName("Kid")) == p.findByName("Top"));
}

RYNAX_TEST(scene_walk_order_and_changes_during_the_walk) {
    Scene s;
    Entity a = s.create("A"), b = s.create("B");
    Entity a1 = s.create("A1", a), a2 = s.create("A2", a), a11 = s.create("A11", a1);
    (void)b;
    (void)a2;
    (void)a11;
    std::string order;
    s.walk([&](Entity e, int depth) {
        order += std::to_string(depth) + s.info(e).name + " ";
        return true;
    });
    CHECK_EQ(order, std::string("0A 1A1 2A11 1A2 0B "));
    // Returning false skips what's inside.
    order.clear();
    s.walk([&](Entity e, int) {
        order += s.info(e).name + " ";
        return e != a1;
    });
    CHECK_EQ(order, std::string("A A1 A2 B "));
    // Destroying objects (even the one being visited) or adding them while walking is safe.
    int visited = 0;
    s.walk([&](Entity e, int) {
        ++visited;
        if (e == a1) {
            s.destroy(a1); // (and A11 inside it)
            return true;
        }
        if (e == b)
            s.create("C", b);
        return true;
    });
    CHECK(visited >= 4);
    CHECK(!s.findByName("A11"));
    CHECK(s.findByName("C"));
}
