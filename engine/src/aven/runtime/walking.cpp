// Walking along paths: self.go_to() and Chase's "Walk around walls". The path comes from
// Navigation (navigation.cpp) and is worked out again every so often, so moving targets and
// opened doors are followed.

#include "aven/runtime/game.h"
#include "aven/runtime/navigation.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <cmath>

namespace aven {

namespace {

bool looks3D(Scene& scene, Entity e) {
    auto& reg = scene.registry();
    return reg.has<MeshRenderer>(e) || reg.has<CharacterController>(e) || reg.has<RigidBody>(e) || reg.has<BoxCollider>(e) ||
           reg.has<SphereCollider>(e);
}

float walkerRadius(Scene& scene, Entity e, bool threeD) {
    // About half the object's size, so it fits through the gaps it can fit through.
    auto& reg = scene.registry();
    Mat4 m = scene.worldMatrix(e);
    Vec3 s{length(m.column(0).xyz()), length(m.column(1).xyz()), length(m.column(2).xyz())};
    float r = 0.4f;
    if (auto* b = reg.tryGet<BoxCollider2D>(e))
        r = 0.5f * std::max(b->size.x * std::abs(s.x), b->size.y * std::abs(s.y));
    else if (auto* c = reg.tryGet<CircleCollider2D>(e))
        r = c->radius * std::max(std::abs(s.x), std::abs(s.y));
    else if (auto* cc = reg.tryGet<CharacterController>(e))
        r = cc->radius;
    else if (threeD)
        r = 0.5f * std::max(std::abs(s.x), std::abs(s.z));
    return std::clamp(r * 0.9f, 0.1f, 2.0f);
}

} // namespace

void GameplaySystems::moveAlong(Entity e, Vec3 dir, float speed, float dt) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    if (auto* rb = reg.tryGet<RigidBody2D>(e); rb && rb->type == BodyType::Dynamic) {
        Vec2 v = game_.physics2D().velocity(e);
        v.x = dir.x * speed;
        if (rb->gravityScale == 0)
            v.y = dir.y * speed;
        game_.physics2D().setVelocity(e, v);
    } else if (auto* rb3 = reg.tryGet<RigidBody>(e); rb3 && rb3->type == BodyType::Dynamic) {
        Vec3 v = game_.physics3D().velocity(e);
        game_.physics3D().setVelocity(e, {dir.x * speed, v.y, dir.z * speed});
    } else {
        scene.setWorldPosition(e, scene.worldPosition(e) + dir * (speed * dt));
    }
    if (std::abs(dir.x) > 0.05f && !looks3D(scene, e))
        if (auto* sr = reg.tryGet<SpriteRenderer>(e))
            sr->flipX = dir.x < 0;
}

Vec3 GameplaySystems::pathDirection(Entity e, Vec3 to, bool threeD, float stopDistance) {
    Scene& scene = game_.scene();
    Vec3 me = scene.worldPosition(e);
    auto flat = [&](Vec3 v) {
        if (threeD)
            v.y = 0;
        else
            v.z = 0;
        return v;
    };
    if (length(flat(to - me)) <= stopDistance)
        return {};
    PathCache& pc = pathCache_[e.toHandle()];
    // Look again when the path is old or the goal moved.
    if (pc.age > 0.4f || length(flat(to - pc.goal)) > 0.75f || pc.path.empty()) {
        PathOptions o;
        o.threeD = threeD;
        o.radius = walkerRadius(scene, e, threeD);
        o.cellSize = std::clamp(o.radius, 0.25f, 1.0f);
        pc.path = game_.navigation().findPath(me, to, o);
        pc.next = 1;
        pc.age = 0;
        pc.goal = to;
    }
    // Waypoints close by are done.
    while (pc.next < pc.path.size() && length(flat(pc.path[pc.next] - me)) < 0.15f)
        ++pc.next;
    if (pc.next >= pc.path.size())
        return {};
    // Debug drawing (the Debug toggle) shows the way it's going.
    for (size_t i = pc.next; i < pc.path.size(); ++i)
        game_.debugDraw().line(i == pc.next ? me : pc.path[i - 1], pc.path[i], {0.3f, 0.9f, 1.0f, 0.9f}, 0);
    Vec3 d = flat(pc.path[pc.next] - me);
    return lengthSquared(d) > 1e-8f ? normalize(d) : Vec3{};
}

void GameplaySystems::goTo(Entity e, Vec3 destination, Entity target, float speed) {
    Walker& w = walkers_[e.toHandle()];
    w.destination = destination;
    w.target = target.toHandle();
    w.speed = speed;
    pathCache_.erase(e.toHandle()); // a new goal: find a new path
}

void GameplaySystems::stopWalking(Entity e) {
    if (walkers_.erase(e.toHandle()))
        moveAlong(e, {}, 0, 0); // a body stops moving too
    pathCache_.erase(e.toHandle());
}

void GameplaySystems::updateWalkers(float dt) {
    Scene& scene = game_.scene();
    for (auto it = pathCache_.begin(); it != pathCache_.end();) {
        if (!scene.valid(Entity::fromHandle(it->first))) {
            it = pathCache_.erase(it); // (spawned chasers come and go: don't keep their paths)
            continue;
        }
        it->second.age += dt;
        ++it;
    }
    std::vector<Entity> arrived;
    for (auto it = walkers_.begin(); it != walkers_.end();) {
        Entity e = Entity::fromHandle(it->first);
        Walker& w = it->second;
        if (!scene.valid(e)) {
            it = walkers_.erase(it);
            continue;
        }
        if (!scene.isActive(e)) {
            ++it;
            continue;
        }
        if (w.target) {
            Entity t = Entity::fromHandle(w.target);
            if (!scene.valid(t)) { // what it was walking to is gone
                moveAlong(e, {}, 0, dt);
                it = walkers_.erase(it);
                continue;
            }
            w.destination = scene.worldPosition(t);
        }
        bool threeD = looks3D(scene, e);
        float stop = w.target ? walkerRadius(scene, e, threeD) + 0.4f : 0.1f;
        Vec3 dir = pathDirection(e, w.destination, threeD, stop);
        if (lengthSquared(dir) < 1e-8f) {
            moveAlong(e, {}, 0, dt);
            arrived.push_back(e);
            it = walkers_.erase(it);
            continue;
        }
        moveAlong(e, dir, w.speed, dt);
        ++it;
    }
    // Scripts hear about it after the walking is done (they may start walking again).
    for (Entity e : arrived)
        game_.scripts().callEvent(e, "on_arrive");
}

} // namespace aven
