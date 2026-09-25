// Ready-made behaviors (Patrol, Chase, Collectible, Hazard, Health, controllers...).
//
// Each behavior is a normal component with settings in the Inspector. The editor can show
// the equivalent EasyScript for any of them, so they double as examples to learn from.

#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace aven {

namespace {

bool sameText(const std::string& a, const std::string& b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// Does `e` match a tag setting? Empty matches anything; the name works too.
bool matches(Scene& scene, Entity e, const std::string& tag) {
    if (tag.empty() || tag == "anything")
        return true;
    const EntityInfo& info = scene.info(e);
    return sameText(info.tag, tag) || sameText(info.name, tag);
}

bool is3D(Scene& scene, Entity e) {
    auto& reg = scene.registry();
    return reg.has<MeshRenderer>(e) || reg.has<CharacterController>(e) || reg.has<RigidBody>(e) || reg.has<BoxCollider>(e) ||
           reg.has<SphereCollider>(e);
}

bool dynamicBody2D(Scene& scene, Entity e) {
    auto* rb = scene.registry().tryGet<RigidBody2D>(e);
    return rb && rb->type == BodyType::Dynamic;
}

void setAlpha(Scene& scene, Entity e, float a) {
    auto& reg = scene.registry();
    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
        sr->color.a = a;
    if (auto* tr = reg.tryGet<TextRenderer>(e))
        tr->color.a = a;
    if (auto* ut = reg.tryGet<UIText>(e))
        ut->color.a = a;
    if (auto* ui = reg.tryGet<UIImage>(e))
        ui->color.a = a;
    if (auto* mr = reg.tryGet<MeshRenderer>(e))
        mr->color.a = a;
}

// Rough world-space box of an object, for touch checks without physics.
bool boundsOf(Scene& scene, Entity e, Vec3& mn, Vec3& mx) {
    auto& reg = scene.registry();
    Vec3 p = scene.worldPosition(e);
    Vec3 s = scene.transform(e).scale;
    Vec2 half{0, 0};
    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
        half = sr->size * 0.5f;
    else if (auto* b = reg.tryGet<BoxCollider2D>(e))
        half = b->size * 0.5f;
    else if (auto* c = reg.tryGet<CircleCollider2D>(e))
        half = {c->radius, c->radius};
    else if (reg.has<MeshRenderer>(e)) {
        mn = p - Vec3{std::abs(s.x), std::abs(s.y), std::abs(s.z)} * 0.5f;
        mx = p + Vec3{std::abs(s.x), std::abs(s.y), std::abs(s.z)} * 0.5f;
        return true;
    } else
        return false;
    Vec3 h{half.x * std::abs(s.x), half.y * std::abs(s.y), 1000.0f};
    mn = p - h;
    mx = p + h;
    return true;
}

bool overlap(Scene& scene, Entity a, Entity b) {
    Vec3 amn, amx, bmn, bmx;
    if (!boundsOf(scene, a, amn, amx) || !boundsOf(scene, b, bmn, bmx))
        return false;
    return amn.x <= bmx.x && amx.x >= bmn.x && amn.y <= bmx.y && amx.y >= bmn.y && amn.z <= bmx.z && amx.z >= bmn.z;
}

std::string formatNumber(double v) {
    char buf[64];
    if (std::abs(v - std::round(v)) < 1e-9)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else
        std::snprintf(buf, sizeof buf, "%.2f", v);
    return buf;
}

} // namespace

GameplaySystems::BehaviorState& GameplaySystems::state(Entity e) {
    auto& st = behaviorStates_[e.toHandle()];
    if (!st.started) {
        Scene& scene = game_.scene();
        st.started = true;
        st.start = scene.transform(e).position;
        st.startScale = scene.transform(e).scale;
        st.startWorld = scene.worldPosition(e);
    }
    return st;
}

void GameplaySystems::moveTo(Entity e, Vec3 local) {
    Scene& scene = game_.scene();
    scene.transform(e).position = local;
    scene.updateTransforms();
}

// Picks what's under the mouse: sprites first, then 2D physics shapes, then 3D objects.
Entity GameplaySystems::entityUnderMouse(Vec3& world) {
    Input& input = game_.input();
    Scene& scene = game_.scene();
    Vec2 size = game_.screenSize();
    CameraView cam = game_.camera(size.x / std::max(size.y, 1.0f));
    world = cam.screenToWorld(input.mousePosition(), size);
    Entity best;
    int bestOrder = -1000000;
    scene.walk([&](Entity e, int) {
        if (!scene.info(e).active)
            return false;
        auto* sr = scene.registry().tryGet<SpriteRenderer>(e);
        if (!sr || scene.registry().has<Hidden>(e))
            return true;
        Vec3 local = transformPoint(inverse(scene.registry().get<WorldTransform>(e).matrix), world);
        if (std::abs(local.x) <= sr->size.x * 0.5f && std::abs(local.y) <= sr->size.y * 0.5f && sr->order >= bestOrder) {
            best = e;
            bestOrder = sr->order;
        }
        return true;
    });
    if (!best)
        best = game_.physics2D().pointQuery({world.x, world.y});
    if (!best) {
        Vec3 origin, dir;
        cam.screenRay(input.mousePosition(), size, origin, dir);
        RayHit hit;
        if (game_.physics3D().raycast(origin, dir, 1000.0f, hit)) {
            best = hit.entity;
            world = origin + dir * hit.distance;
        }
    }
    return best;
}

void GameplaySystems::startBehaviors() {
    behaviorStates_.clear();
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    // Controllers need physics: add it if it's missing so they work straight away.
    reg.each<PlatformerController>([&](Entity e, PlatformerController&) {
        if (!reg.has<RigidBody2D>(e)) {
            auto& rb = reg.emplace<RigidBody2D>(e);
            rb.fixedRotation = true;
            rb.gravityScale = 2.5f;
        }
        if (!reg.has<BoxCollider2D>(e) && !reg.has<CircleCollider2D>(e)) {
            Vec2 size{0.8f, 1.0f};
            if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                size = {sr->size.x * 0.75f, sr->size.y * 0.95f};
            auto& box = reg.emplace<BoxCollider2D>(e);
            box.size = size;
            box.friction = 0;
        }
    });
    reg.each<Health>([&](Entity, Health& h) { h.current = h.maxHealth; });
}

void GameplaySystems::damage(Entity victim, int amount, Vec3 from, float knockback) {
    Scene& scene = game_.scene();
    if (!scene.valid(victim))
        return;
    auto& reg = scene.registry();
    auto* health = reg.tryGet<Health>(victim);
    if (!health) {
        // Nothing to take hits: start the level again.
        game_.requestSceneChange(game_.scenePath());
        return;
    }
    BehaviorState& st = state(victim);
    if (st.invincible > 0 || health->current <= 0)
        return;
    health->current = std::max(0, health->current - amount);
    st.invincible = health->invincibleTime;
    shake(0.25f, 0.25f);
    if (knockback > 0) {
        Vec3 away = scene.worldPosition(victim) - from;
        if (is3D(scene, victim)) {
            away.y = 0;
            Vec3 push = (lengthSquared(away) > 1e-6f ? normalize(away) : Vec3{0, 0, 1}) * knockback + Vec3{0, knockback * 0.5f, 0};
            if (auto* cc = reg.tryGet<CharacterController>(victim))
                cc->velocity = push;
            else
                game_.physics3D().setVelocity(victim, push);
        } else {
            Vec2 a{away.x, 0};
            a = std::abs(a.x) > 1e-4f ? normalize(a) : Vec2{1, 0};
            game_.physics2D().setVelocity(victim, {a.x * knockback, knockback * 0.8f});
        }
    }
    game_.scripts().broadcast("hurt", script::Value(static_cast<double>(health->current)));
    if (health->current > 0)
        return;
    switch (health->whenZero) {
    case WhenHealthRunsOut::RestartScene: game_.requestSceneChange(game_.scenePath()); break;
    case WhenHealthRunsOut::Respawn:
        moveTo(victim, st.start);
        health->current = health->maxHealth;
        game_.physics2D().setVelocity(victim, {0, 0});
        break;
    case WhenHealthRunsOut::Destroy: game_.destroyEntity(victim); break;
    case WhenHealthRunsOut::Nothing: break;
    }
}

void GameplaySystems::sparkle(Vec3 at, Color color) {
    Scene& scene = game_.scene();
    Entity fx = scene.create("Sparkle");
    scene.transform(fx).position = at;
    auto& pe = scene.registry().emplace<ParticleEmitter>(fx);
    pe.rate = 0;
    pe.burst = 14;
    pe.lifetime = 0.5f;
    pe.speed = 4;
    pe.spread = 180;
    pe.gravity = {0, -4, 0};
    pe.startColor = color;
    pe.endColor = {color.r, color.g, color.b, 0};
    pe.startSize = 0.18f;
    pe.endSize = 0;
    scene.registry().emplace<Lifetime>(fx).seconds = 0.8f;
    scene.updateTransforms();
}

void GameplaySystems::onBehaviorCollision(Entity a, Entity b, bool begin) {
    if (!begin)
        return;
    Scene& scene = game_.scene();
    for (auto [self, other] : {std::pair{a, b}, std::pair{b, a}}) {
        if (!scene.valid(self) || !scene.valid(other) || !scene.isActive(self))
            continue;
        auto& reg = scene.registry();
        if (auto* c = reg.tryGet<Collectible>(self); c && matches(scene, other, c->collectorTag)) {
            BehaviorState& st = state(self);
            if (st.collected)
                continue;
            st.collected = true;
            game_.scripts().addToGameNumber(c->counter, c->amount);
            if (!c->sound.empty())
                game_.audio().playSound(c->sound);
            if (c->sparkle)
                sparkle(scene.worldPosition(self), Color::fromHex(0xFDE047));
            game_.destroyEntity(self);
            continue;
        }
        if (auto* h = reg.tryGet<Hazard>(self); h && matches(scene, other, h->victimTag)) {
            damage(other, h->damage, scene.worldPosition(self), h->knockback);
            if (h->vanishOnHit) {
                game_.destroyEntity(self);
                continue;
            }
        }
        if (auto* link = reg.tryGet<SceneLink>(self); link && link->when == LinkTrigger::Touch && matches(scene, other, link->tag)) {
            BehaviorState& st = state(self);
            if (!st.linkPending) {
                st.linkPending = true;
                st.linkTimer = link->delay;
            }
        }
    }
}

void GameplaySystems::onBehaviorClick(Entity e) {
    Scene& scene = game_.scene();
    if (!scene.valid(e))
        return;
    auto& reg = scene.registry();
    if (auto* c = reg.tryGet<Clickable>(e)) {
        game_.scripts().addToGameNumber(c->counter, c->amount);
        if (!c->sound.empty())
            game_.audio().playSound(c->sound);
        if (c->bounce)
            state(e).bounce = 0.12f;
    }
    if (auto* link = reg.tryGet<SceneLink>(e); link && link->when == LinkTrigger::Click) {
        BehaviorState& st = state(e);
        st.linkPending = true;
        st.linkTimer = link->delay;
    }
}

void GameplaySystems::updateBehaviors(float dt) {
    if (dt <= 0)
        return;
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    Input& input = game_.input();
    Physics2D& p2 = game_.physics2D();
    float t = game_.time();

    reg.each<Patrol>([&](Entity e, Patrol& p) {
        if (!scene.isActive(e))
            return;
        BehaviorState& st = state(e);
        int axis = static_cast<int>(p.axis);
        Vec3 pos = scene.transform(e).position;
        float offset = (&pos.x)[axis] - (&st.start.x)[axis];
        if (offset > p.distance)
            st.dir = -1;
        else if (offset < -p.distance)
            st.dir = 1;
        if (dynamicBody2D(scene, e) && axis < 2) {
            Vec2 v = p2.velocity(e);
            (&v.x)[axis] = st.dir * p.speed;
            p2.setVelocity(e, v);
        } else {
            (&pos.x)[axis] += st.dir * p.speed * dt;
            moveTo(e, pos);
        }
        if (p.flipSprite && axis == 0)
            if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                sr->flipX = st.dir < 0;
    });

    reg.each<Chase>([&](Entity e, Chase& c) {
        if (!scene.isActive(e))
            return;
        Vec3 me = scene.worldPosition(e);
        bool threeD = is3D(scene, e);
        Entity best;
        float bestDist = c.sight;
        for (Entity o : scene.findAllWithTag(c.targetTag)) {
            if (o == e || !scene.isActive(o))
                continue;
            Vec3 d = scene.worldPosition(o) - me;
            if (!threeD)
                d.z = 0;
            float dist = length(d);
            if (dist < bestDist) {
                bestDist = dist;
                best = o;
            }
        }
        Vec3 dir{0, 0, 0};
        if (best && (c.runAway || bestDist > c.stopDistance)) {
            dir = scene.worldPosition(best) - me;
            if (threeD)
                dir.y = 0;
            else
                dir.z = 0;
            dir = lengthSquared(dir) > 1e-8f ? normalize(dir) * (c.runAway ? -1.0f : 1.0f) : Vec3{};
        }
        if (dynamicBody2D(scene, e)) {
            Vec2 v = p2.velocity(e);
            bool gravity = reg.get<RigidBody2D>(e).gravityScale != 0;
            v.x = dir.x * c.speed;
            if (!gravity)
                v.y = dir.y * c.speed;
            p2.setVelocity(e, v);
        } else if (best) {
            scene.setWorldPosition(e, me + dir * (c.speed * dt));
        }
        if (c.flipSprite && std::abs(dir.x) > 0.05f)
            if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                sr->flipX = dir.x < 0;
    });

    reg.each<Spin>([&](Entity e, Spin& s) {
        if (scene.isActive(e))
            scene.transform(e).rotation += s.speed * dt;
    });

    reg.each<Bob>([&](Entity e, Bob& b) {
        if (!scene.isActive(e))
            return;
        BehaviorState& st = state(e);
        Vec3 pos = scene.transform(e).position;
        pos.y = st.start.y + std::sin(t * b.speed * 2.0f * kPi) * b.height;
        moveTo(e, pos);
    });

    // Hazards keep hurting while something stays in them (after the victim's invincibility runs out).
    reg.each<Hazard>([&](Entity e, Hazard& h) {
        if (!scene.isActive(e))
            return;
        std::vector<Entity> touching = p2.touching(e);
        for (Entity o : game_.physics3D().touching(e))
            touching.push_back(o);
        for (Entity o : scene.findAllWithTag(h.victimTag))
            if (std::find(touching.begin(), touching.end(), o) == touching.end() && overlap(scene, e, o) &&
                !reg.has<RigidBody2D>(o) && !reg.has<CharacterController>(o))
                touching.push_back(o); // objects without physics bodies
        for (Entity o : touching)
            if (scene.valid(o) && matches(scene, o, h.victimTag) && reg.has<Health>(o) && state(o).invincible <= 0)
                damage(o, h.damage, scene.worldPosition(e), h.knockback);
    });

    reg.each<Health>([&](Entity e, Health& h) {
        BehaviorState& st = state(e);
        if (st.invincible > 0) {
            st.invincible -= dt;
            bool blink = st.invincible > 0 && static_cast<int>(st.invincible * 12) % 2 == 0;
            setAlpha(scene, e, blink ? 0.35f : 1.0f);
        }
        if (!h.counter.empty())
            game_.scripts().setGameValue(h.counter, script::Value(static_cast<double>(h.current)));
    });

    std::vector<Entity> expired;
    reg.each<Lifetime>([&](Entity e, Lifetime& l) {
        BehaviorState& st = state(e);
        st.age += dt;
        float left = l.seconds - st.age;
        if (l.fadeOut && left < 0.5f)
            setAlpha(scene, e, std::max(0.0f, left / 0.5f));
        if (left <= 0)
            expired.push_back(e);
    });
    for (Entity e : expired)
        game_.destroyEntity(e);

    // Screen edges come from the primary camera.
    Vec2 screen = game_.screenSize();
    CameraView cam = game_.camera(screen.x / std::max(screen.y, 1.0f));
    reg.each<WrapAround>([&](Entity e, WrapAround& w) {
        if (!cam.orthographic)
            return;
        Vec3 p = scene.worldPosition(e);
        float hh = cam.orthoSize + w.margin, hw = cam.orthoSize * screen.x / std::max(screen.y, 1.0f) + w.margin;
        Vec3 c = cam.position;
        bool moved = false;
        if (p.x > c.x + hw) p.x = c.x - hw, moved = true;
        else if (p.x < c.x - hw) p.x = c.x + hw, moved = true;
        if (p.y > c.y + hh) p.y = c.y - hh, moved = true;
        else if (p.y < c.y - hh) p.y = c.y + hh, moved = true;
        if (moved)
            scene.setWorldPosition(e, p);
    });

    reg.each<PlatformerController>([&](Entity e, PlatformerController& pc) {
        if (!scene.isActive(e))
            return;
        BehaviorState& st = state(e);
        float x = input.axis("horizontal");
        Vec2 v = p2.velocity(e);
        v.x = x * pc.speed;
        bool ground = p2.isOnGround(e);
        if (ground) {
            st.coyote = pc.coyoteTime;
            st.jumpsLeft = pc.extraJumps;
        } else {
            st.coyote -= dt;
        }
        bool jumpPressed = input.pressed("jump") || input.pressed("up");
        if (jumpPressed && (st.coyote > 0 || st.jumpsLeft > 0)) {
            if (st.coyote <= 0)
                --st.jumpsLeft;
            st.coyote = 0;
            v.y = pc.jumpPower;
            if (!pc.jumpSound.empty())
                game_.audio().playSound(pc.jumpSound);
        }
        // Letting go of jump early makes a smaller hop.
        if (!input.down("jump") && !input.down("up") && v.y > 0 && !ground)
            v.y *= std::pow(0.5f, dt * 60.0f / 6.0f);
        p2.setVelocity(e, v);
        if (pc.flipSprite && std::abs(x) > 0.01f)
            if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                sr->flipX = x < 0;
    });

    reg.each<TopDownController>([&](Entity e, TopDownController& td) {
        if (!scene.isActive(e))
            return;
        Vec2 move{input.axis("horizontal"), input.axis("vertical")};
        if (dot(move, move) > 1)
            move = normalize(move);
        Vec2 v = move * td.speed;
        if (reg.has<RigidBody2D>(e))
            p2.setVelocity(e, v);
        else {
            Vec3 pos = scene.transform(e).position;
            moveTo(e, {pos.x + v.x * dt, pos.y + v.y * dt, pos.z});
        }
        if (dot(move, move) > 0.01f) {
            if (td.faceMovement)
                scene.transform(e).rotation.z = degrees(std::atan2(move.y, move.x)) - 90.0f;
            else if (std::abs(move.x) > 0.01f)
                if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                    sr->flipX = move.x < 0;
        }
    });

    Vec3 mouseWorld;
    Entity underMouse;
    bool mouseNeeded = !reg.entitiesWith<FollowMouse>().empty() || !reg.entitiesWith<Draggable>().empty();
    if (mouseNeeded)
        underMouse = entityUnderMouse(mouseWorld);

    reg.each<FollowMouse>([&](Entity e, FollowMouse& f) {
        if (!scene.isActive(e) || (f.onlyWhileHeld && !input.mouseDown(MouseButton::Left)))
            return;
        Vec3 p = scene.worldPosition(e);
        Vec3 target{mouseWorld.x, mouseWorld.y, p.z};
        float k = f.smoothness <= 0 ? 1.0f : 1.0f - std::exp(-f.smoothness * dt);
        scene.setWorldPosition(e, p + (target - p) * k);
    });

    reg.each<Draggable>([&](Entity e, Draggable& d) {
        BehaviorState& st = state(e);
        if (input.mousePressed(MouseButton::Left) && underMouse == e) {
            st.dragging = true;
            st.dragOffset = scene.worldPosition(e) - Vec3{mouseWorld.x, mouseWorld.y, 0};
        }
        if (!st.dragging)
            return;
        Vec3 p = Vec3{mouseWorld.x, mouseWorld.y, 0} + st.dragOffset;
        p.z = scene.worldPosition(e).z;
        if (!input.mouseDown(MouseButton::Left)) {
            st.dragging = false;
            if (d.snapToGrid && d.gridSize > 0) {
                p.x = std::round(p.x / d.gridSize) * d.gridSize;
                p.y = std::round(p.y / d.gridSize) * d.gridSize;
            }
        }
        scene.setWorldPosition(e, p);
        if (reg.has<RigidBody2D>(e))
            p2.setVelocity(e, {0, 0});
    });

    reg.each<Shooter>([&](Entity e, Shooter& s) {
        if (!scene.isActive(e) || s.prefab.empty())
            return;
        BehaviorState& st = state(e);
        st.cooldown -= dt;
        if (st.cooldown > 0 || !input.down(s.action))
            return;
        st.cooldown = s.cooldown;
        Vec3 origin = scene.worldPosition(e);
        bool facingLeft = false;
        if (auto* sr = reg.tryGet<SpriteRenderer>(e))
            facingLeft = sr->flipX;
        Vec3 dir{0, 1, 0};
        switch (s.aim) {
        case AimMode::Up: dir = {0, 1, 0}; break;
        case AimMode::Right: dir = {1, 0, 0}; break;
        case AimMode::Facing:
            dir = is3D(scene, e) ? normalize(transformDirection(scene.worldMatrix(e), {0, 0, -1})) : Vec3{facingLeft ? -1.0f : 1.0f, 0, 0};
            break;
        case AimMode::Mouse: {
            Vec3 m;
            entityUnderMouse(m);
            Vec3 d = m - origin;
            d.z = 0;
            dir = lengthSquared(d) > 1e-6f ? normalize(d) : Vec3{0, 1, 0};
            break;
        }
        }
        Vec3 offset{s.offset.x * (facingLeft ? -1.0f : 1.0f), s.offset.y, 0};
        // Spawning can add components and move this Shooter in memory, so keep copies of what's needed after.
        float bulletSpeed = s.bulletSpeed;
        std::string sound = s.sound;
        Entity bullet = game_.spawnPrefab(s.prefab, origin + offset);
        if (!bullet)
            return;
        if (!is3D(scene, bullet)) {
            if (!reg.has<RigidBody2D>(bullet)) {
                auto& rb = reg.emplace<RigidBody2D>(bullet);
                rb.gravityScale = 0;
                rb.fixedRotation = true;
            }
            p2.setVelocity(bullet, {dir.x * bulletSpeed, dir.y * bulletSpeed});
            scene.transform(bullet).rotation.z = degrees(std::atan2(dir.y, dir.x)) - 90.0f;
            if (!reg.has<Lifetime>(bullet))
                reg.emplace<Lifetime>(bullet).seconds = 4.0f;
        } else {
            game_.physics3D().setVelocity(bullet, dir * bulletSpeed);
        }
        if (!sound.empty())
            game_.audio().playSound(sound);
    });

    reg.each<Spawner>([&](Entity e, Spawner& s) {
        if (!scene.isActive(e) || s.prefab.empty())
            return;
        BehaviorState& st = state(e);
        st.cooldown -= dt;
        if (st.cooldown > 0)
            return;
        st.cooldown = s.interval;
        std::erase_if(st.spawned, [&](uint64_t h) { return !scene.valid(Entity::fromHandle(h)); });
        if (static_cast<int>(st.spawned.size()) >= s.maxAlive)
            return;
        Vec3 p = scene.worldPosition(e);
        p.x += (random01() * 2 - 1) * s.randomRange.x;
        p.y += (random01() * 2 - 1) * s.randomRange.y;
        if (Entity copy = game_.spawnPrefab(s.prefab, p))
            st.spawned.push_back(copy.toHandle());
    });

    reg.each<Clickable>([&](Entity e, Clickable&) {
        BehaviorState& st = state(e);
        if (st.bounce <= 0)
            return;
        st.bounce -= dt;
        float k = st.bounce > 0 ? 1.0f + std::sin(st.bounce / 0.12f * kPi) * 0.08f : 1.0f;
        scene.transform(e).scale = st.startScale * k;
    });

    reg.each<ScoreDisplay>([&](Entity e, ScoreDisplay& sd) {
        script::Value v = game_.scripts().gameValue(sd.counter);
        std::string value = v.isNumber() ? formatNumber(v.number()) : v.isNone() ? "0" : v.toString();
        std::string text = sd.format;
        size_t at = text.find("{}");
        if (at != std::string::npos)
            text.replace(at, 2, value);
        else
            text += value;
        if (auto* ut = reg.tryGet<UIText>(e))
            ut->text = text;
        if (auto* tr = reg.tryGet<TextRenderer>(e))
            tr->text = text;
    });

    std::string nextScene;
    reg.each<SceneLink>([&](Entity e, SceneLink& link) {
        BehaviorState& st = state(e);
        if (link.when == LinkTrigger::AfterTime && !st.linkPending) {
            st.linkPending = true;
            st.linkTimer = link.delay;
        }
        if (!st.linkPending || link.scene.empty())
            return;
        st.linkTimer -= dt;
        if (st.linkTimer <= 0)
            nextScene = link.scene;
    });
    if (!nextScene.empty())
        game_.requestSceneChange(nextScene);
}

} // namespace aven
