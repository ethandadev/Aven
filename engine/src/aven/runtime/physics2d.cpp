#include "aven/core/log.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <box2d/box2d.h>

#include <cmath>
#include <unordered_map>

namespace aven {

namespace {

void* toUserData(Entity e) { return reinterpret_cast<void*>(static_cast<uintptr_t>(e.toHandle())); }
Entity fromUserData(void* p) { return Entity::fromHandle(static_cast<uint64_t>(reinterpret_cast<uintptr_t>(p))); }

float angleOf(const Mat4& m) { return std::atan2(m.m[1], m.m[0]); }
Vec2 scaleOf(const Mat4& m) {
    return {std::sqrt(m.m[0] * m.m[0] + m.m[1] * m.m[1]), std::sqrt(m.m[4] * m.m[4] + m.m[5] * m.m[5])};
}

b2BodyType bodyType(BodyType t) {
    switch (t) {
    case BodyType::Dynamic: return b2_dynamicBody;
    case BodyType::Static: return b2_staticBody;
    case BodyType::Kinematic: return b2_kinematicBody;
    }
    return b2_dynamicBody;
}

} // namespace

struct Physics2D::Impl {
    Game& game;
    b2WorldId world = b2_nullWorldId;
    Vec2 gravity{0, -9.81f};

    struct Body {
        b2BodyId id;
        Vec2 lastPosition;
        float lastAngle = 0;
        bool enabled = true;
    };
    std::unordered_map<Entity, Body> bodies;
    std::vector<Entity> dirty;

    explicit Impl(Game& g) : game(g) {}

    bool running() const { return B2_IS_NON_NULL(world); }

    void destroyBody(Entity e) {
        auto it = bodies.find(e);
        if (it == bodies.end())
            return;
        if (b2Body_IsValid(it->second.id))
            b2DestroyBody(it->second.id);
        bodies.erase(it);
    }

    void createBody(Entity e) {
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        auto* rb = reg.tryGet<RigidBody2D>(e);
        auto* box = reg.tryGet<BoxCollider2D>(e);
        auto* circle = reg.tryGet<CircleCollider2D>(e);
        if (!rb && !box && !circle)
            return;
        Mat4 m = scene.worldMatrix(e);
        Vec2 pos{m.m[12], m.m[13]};
        float angle = angleOf(m);
        Vec2 scale = scaleOf(m);

        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = rb ? bodyType(rb->type) : b2_staticBody;
        bd.position = {pos.x, pos.y};
        bd.rotation = b2MakeRot(angle);
        bd.userData = toUserData(e);
        if (rb) {
            bd.gravityScale = rb->gravityScale;
            bd.fixedRotation = rb->fixedRotation;
            bd.isBullet = rb->continuous;
            bd.linearDamping = rb->linearDamping;
        }
        b2BodyId id = b2CreateBody(world, &bd);

        float area = 0;
        if (box)
            area += std::abs(box->size.x * scale.x * box->size.y * scale.y);
        if (circle) {
            float r = circle->radius * std::max(scale.x, scale.y);
            area += kPi * r * r;
        }
        float density = rb && area > 1e-6f ? std::max(rb->mass, 0.001f) / area : 1.0f;

        auto shapeDef = [&](float friction, float bounciness, bool trigger) {
            b2ShapeDef sd = b2DefaultShapeDef();
            sd.density = density;
            sd.material.friction = friction;
            sd.material.restitution = bounciness;
            sd.isSensor = trigger;
            sd.enableSensorEvents = true;
            sd.enableContactEvents = true;
            sd.userData = toUserData(e);
            return sd;
        };
        if (box) {
            b2ShapeDef sd = shapeDef(box->friction, box->bounciness, box->isTrigger);
            b2Polygon poly = b2MakeOffsetBox(std::max(0.005f, box->size.x * scale.x * 0.5f),
                                             std::max(0.005f, box->size.y * scale.y * 0.5f),
                                             {box->offset.x * scale.x, box->offset.y * scale.y}, b2Rot_identity);
            b2CreatePolygonShape(id, &sd, &poly);
        }
        if (circle) {
            b2ShapeDef sd = shapeDef(circle->friction, circle->bounciness, circle->isTrigger);
            b2Circle c{{circle->offset.x * scale.x, circle->offset.y * scale.y},
                       std::max(0.005f, circle->radius * std::max(scale.x, scale.y))};
            b2CreateCircleShape(id, &sd, &c);
        }
        bodies[e] = {id, pos, angle, true};
    }

    void syncBodies() {
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        for (Entity e : dirty)
            destroyBody(e);
        dirty.clear();
        // Bodies whose entity or components are gone.
        for (auto it = bodies.begin(); it != bodies.end();) {
            Entity e = it->first;
            bool keep = scene.valid(e) &&
                        (reg.has<RigidBody2D>(e) || reg.has<BoxCollider2D>(e) || reg.has<CircleCollider2D>(e));
            if (!keep) {
                if (b2Body_IsValid(it->second.id))
                    b2DestroyBody(it->second.id);
                it = bodies.erase(it);
            } else {
                ++it;
            }
        }
        auto ensure = [&](Entity e) {
            if (!bodies.count(e))
                createBody(e);
        };
        for (Entity e : reg.entitiesWith<RigidBody2D>())
            ensure(e);
        for (Entity e : reg.entitiesWith<BoxCollider2D>())
            ensure(e);
        for (Entity e : reg.entitiesWith<CircleCollider2D>())
            ensure(e);

        // Objects moved or disabled by scripts.
        for (auto& [e, body] : bodies) {
            bool active = scene.isActive(e);
            if (active != body.enabled) {
                if (active)
                    b2Body_Enable(body.id);
                else
                    b2Body_Disable(body.id);
                body.enabled = active;
            }
            Mat4 m = scene.worldMatrix(e);
            Vec2 pos{m.m[12], m.m[13]};
            float angle = angleOf(m);
            if (length(pos - body.lastPosition) > 1e-5f || std::abs(angle - body.lastAngle) > 1e-5f) {
                b2Body_SetTransform(body.id, {pos.x, pos.y}, b2MakeRot(angle));
                body.lastPosition = pos;
                body.lastAngle = angle;
            }
        }
    }

    void writeBack() {
        Scene& scene = game.scene();
        for (auto& [e, body] : bodies) {
            if (!body.enabled || b2Body_GetType(body.id) == b2_staticBody)
                continue;
            b2Vec2 p = b2Body_GetPosition(body.id);
            float angle = b2Rot_GetAngle(b2Body_GetRotation(body.id));
            Transform& t = scene.transform(e);
            if (scene.parent(e)) {
                Vec3 world = scene.worldPosition(e);
                scene.setWorldPosition(e, {p.x, p.y, world.z});
            } else {
                t.position.x = p.x;
                t.position.y = p.y;
            }
            if (!scene.registry().get<RigidBody2D>(e).fixedRotation)
                t.rotation.z += degrees(angle - body.lastAngle);
            body.lastPosition = {p.x, p.y};
            body.lastAngle = angle;
        }
    }

    void dispatchEvents() {
        struct Event {
            Entity a, b;
            bool begin, trigger;
        };
        std::vector<Event> events;
        b2ContactEvents contacts = b2World_GetContactEvents(world);
        for (int i = 0; i < contacts.beginCount; ++i) {
            auto& ev = contacts.beginEvents[i];
            events.push_back({fromUserData(b2Shape_GetUserData(ev.shapeIdA)),
                              fromUserData(b2Shape_GetUserData(ev.shapeIdB)), true, false});
        }
        for (int i = 0; i < contacts.endCount; ++i) {
            auto& ev = contacts.endEvents[i];
            if (!b2Shape_IsValid(ev.shapeIdA) || !b2Shape_IsValid(ev.shapeIdB))
                continue;
            events.push_back({fromUserData(b2Shape_GetUserData(ev.shapeIdA)),
                              fromUserData(b2Shape_GetUserData(ev.shapeIdB)), false, false});
        }
        b2SensorEvents sensors = b2World_GetSensorEvents(world);
        for (int i = 0; i < sensors.beginCount; ++i) {
            auto& ev = sensors.beginEvents[i];
            events.push_back({fromUserData(b2Shape_GetUserData(ev.sensorShapeId)),
                              fromUserData(b2Shape_GetUserData(ev.visitorShapeId)), true, true});
        }
        for (int i = 0; i < sensors.endCount; ++i) {
            auto& ev = sensors.endEvents[i];
            if (!b2Shape_IsValid(ev.sensorShapeId) || !b2Shape_IsValid(ev.visitorShapeId))
                continue;
            events.push_back({fromUserData(b2Shape_GetUserData(ev.sensorShapeId)),
                              fromUserData(b2Shape_GetUserData(ev.visitorShapeId)), false, true});
        }
        for (auto& ev : events)
            game.scripts().onCollision(ev.a, ev.b, ev.begin, ev.trigger);
    }

    const Body* find(Entity e) const {
        auto it = bodies.find(e);
        return it == bodies.end() || !b2Body_IsValid(it->second.id) ? nullptr : &it->second;
    }

    std::vector<b2ContactData> contacts(b2BodyId id) const {
        int capacity = b2Body_GetContactCapacity(id);
        std::vector<b2ContactData> data(static_cast<size_t>(std::max(capacity, 0)));
        int n = capacity > 0 ? b2Body_GetContactData(id, data.data(), capacity) : 0;
        data.resize(static_cast<size_t>(n));
        return data;
    }
};

Physics2D::Physics2D(Game& game) : impl_(std::make_unique<Impl>(game)) {}

Physics2D::~Physics2D() {
    stop();
}

void Physics2D::start() {
    stop();
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = {impl_->gravity.x, impl_->gravity.y};
    impl_->world = b2CreateWorld(&wd);
}

void Physics2D::stop() {
    if (impl_->running())
        b2DestroyWorld(impl_->world);
    impl_->world = b2_nullWorldId;
    impl_->bodies.clear();
    impl_->dirty.clear();
}

void Physics2D::step(float dt) {
    if (!impl_->running())
        return;
    impl_->syncBodies();
    if (impl_->bodies.empty())
        return;
    b2World_Step(impl_->world, dt, 4);
    impl_->writeBack();
    impl_->dispatchEvents();
}

void Physics2D::setGravity(Vec2 g) {
    impl_->gravity = g;
    if (impl_->running())
        b2World_SetGravity(impl_->world, {g.x, g.y});
}

Vec2 Physics2D::gravity() const { return impl_->gravity; }

void Physics2D::onDestroy(Entity e) { impl_->destroyBody(e); }

void Physics2D::refresh(Entity e) {
    if (impl_->bodies.count(e))
        impl_->dirty.push_back(e);
}

bool Physics2D::hasBody(Entity e) const { return impl_->find(e) != nullptr; }

Vec2 Physics2D::velocity(Entity e) const {
    const auto* b = impl_->find(e);
    if (!b)
        return {};
    b2Vec2 v = b2Body_GetLinearVelocity(b->id);
    return {v.x, v.y};
}

void Physics2D::setVelocity(Entity e, Vec2 v) {
    if (const auto* b = impl_->find(e)) {
        b2Body_SetLinearVelocity(b->id, {v.x, v.y});
        b2Body_SetAwake(b->id, true);
    }
}

void Physics2D::applyForce(Entity e, Vec2 f) {
    if (const auto* b = impl_->find(e))
        b2Body_ApplyForceToCenter(b->id, {f.x, f.y}, true);
}

void Physics2D::applyImpulse(Entity e, Vec2 i) {
    if (const auto* b = impl_->find(e))
        b2Body_ApplyLinearImpulseToCenter(b->id, {i.x, i.y}, true);
}

bool Physics2D::isOnGround(Entity e) const {
    const auto* b = impl_->find(e);
    if (!b)
        return false;
    for (auto& c : impl_->contacts(b->id)) {
        if (c.manifold.pointCount == 0 || b2Shape_IsSensor(c.shapeIdA) || b2Shape_IsSensor(c.shapeIdB))
            continue;
        bool close = false;
        for (int i = 0; i < c.manifold.pointCount; ++i)
            close = close || c.manifold.points[i].separation < 0.05f;
        if (!close)
            continue;
        bool weAreA = B2_ID_EQUALS(b2Shape_GetBody(c.shapeIdA), b->id);
        // The normal points from A to B; flip it so it points from the other object toward us.
        float ny = weAreA ? -c.manifold.normal.y : c.manifold.normal.y;
        if (ny > 0.5f)
            return true;
    }
    return false;
}

std::vector<Entity> Physics2D::touching(Entity e) const {
    std::vector<Entity> out;
    const auto* b = impl_->find(e);
    if (!b)
        return out;
    for (auto& c : impl_->contacts(b->id)) {
        if (c.manifold.pointCount == 0)
            continue;
        bool weAreA = B2_ID_EQUALS(b2Shape_GetBody(c.shapeIdA), b->id);
        out.push_back(fromUserData(b2Shape_GetUserData(weAreA ? c.shapeIdB : c.shapeIdA)));
    }
    return out;
}

bool Physics2D::raycast(Vec2 from, Vec2 to, RayHit& hit) const {
    if (!impl_->running())
        return false;
    Vec2 d = to - from;
    b2RayResult r = b2World_CastRayClosest(impl_->world, {from.x, from.y}, {d.x, d.y}, b2DefaultQueryFilter());
    if (!r.hit)
        return false;
    hit.entity = fromUserData(b2Shape_GetUserData(r.shapeId));
    hit.point = {r.point.x, r.point.y, 0};
    hit.normal = {r.normal.x, r.normal.y, 0};
    hit.distance = r.fraction * length(d);
    return true;
}

Entity Physics2D::pointQuery(Vec2 point) const {
    if (!impl_->running())
        return {};
    struct Ctx {
        b2Vec2 p;
        Entity found;
    } ctx{{point.x, point.y}, {}};
    b2AABB box{{point.x - 0.001f, point.y - 0.001f}, {point.x + 0.001f, point.y + 0.001f}};
    b2World_OverlapAABB(
        impl_->world, box, b2DefaultQueryFilter(),
        [](b2ShapeId shape, void* user) {
            auto* c = static_cast<Ctx*>(user);
            if (b2Shape_TestPoint(shape, c->p)) {
                c->found = fromUserData(b2Shape_GetUserData(shape));
                return false;
            }
            return true;
        },
        &ctx);
    return ctx.found;
}

} // namespace aven
