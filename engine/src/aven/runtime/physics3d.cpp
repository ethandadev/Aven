// 3D physics on top of Jolt Physics.

#include "aven/core/log.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

namespace aven {

namespace {

namespace Layers {
constexpr JPH::ObjectLayer Static = 0;
constexpr JPH::ObjectLayer Moving = 1;
} // namespace Layers

class ObjectPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == Layers::Moving || b == Layers::Moving;
    }
};

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return 2; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer.GetValue() == 0 ? "static" : "moving";
    }
#endif
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == Layers::Moving || bp.GetValue() == Layers::Moving;
    }
};

// Jolt reports contacts from its worker threads; queue them and hand them to
// scripts after the step, on the main thread.
class ContactQueue final : public JPH::ContactListener {
public:
    struct Event {
        JPH::BodyID a, b;
        bool begin;
        bool sensor;
    };
    std::mutex mutex;
    std::vector<Event> events;

    void OnContactAdded(const JPH::Body& a, const JPH::Body& b, const JPH::ContactManifold&, JPH::ContactSettings&) override {
        std::lock_guard lock(mutex);
        events.push_back({a.GetID(), b.GetID(), true, a.IsSensor() || b.IsSensor()});
    }
    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override {
        std::lock_guard lock(mutex);
        events.push_back({pair.GetBody1ID(), pair.GetBody2ID(), false, false});
    }
};

struct JoltGlobals {
    std::unique_ptr<JPH::TempAllocatorImpl> temp;
    std::unique_ptr<JPH::JobSystemThreadPool> jobs;
};

JoltGlobals& jolt() {
    static JoltGlobals g = [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        JoltGlobals out;
        out.temp = std::make_unique<JPH::TempAllocatorImpl>(16 * 1024 * 1024);
        int threads = std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
        out.jobs = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);
        return out;
    }();
    return g;
}

JPH::Vec3 toJ(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::RVec3 toJR(Vec3 v) { return {v.x, v.y, v.z}; }
JPH::Quat toJ(Quat q) { return {q.x, q.y, q.z, q.w}; }
Vec3 fromJ(JPH::Vec3 v) { return {v.GetX(), v.GetY(), v.GetZ()}; }
Quat fromJ(JPH::Quat q) { return {q.GetX(), q.GetY(), q.GetZ(), q.GetW()}; }

JPH::EMotionType motionType(BodyType t) {
    switch (t) {
    case BodyType::Dynamic: return JPH::EMotionType::Dynamic;
    case BodyType::Static: return JPH::EMotionType::Static;
    case BodyType::Kinematic: return JPH::EMotionType::Kinematic;
    }
    return JPH::EMotionType::Dynamic;
}

JPH::Ref<JPH::Shape> boxShape(Vec3 half) {
    half = max(half, Vec3(0.005f));
    float radius = std::min({0.05f, half.x * 0.5f, half.y * 0.5f, half.z * 0.5f});
    return new JPH::BoxShape(toJ(half), radius);
}

JPH::Ref<JPH::Shape> offsetShape(JPH::Ref<JPH::Shape> shape, Vec3 offset) {
    if (lengthSquared(offset) < 1e-10f)
        return shape;
    JPH::RotatedTranslatedShapeSettings s(toJ(offset), JPH::Quat::sIdentity(), shape);
    auto result = s.Create();
    return result.IsValid() ? result.Get() : shape;
}

} // namespace

struct Physics3D::Impl {
    Game& game;
    Vec3 gravity{0, -9.81f, 0};
    BroadPhaseLayers bpLayers;
    ObjectVsBroadPhaseFilter objVsBp;
    ObjectPairFilter objPair;
    ContactQueue contacts;
    std::unique_ptr<JPH::PhysicsSystem> physics;

    struct BodyRecord {
        JPH::BodyID id;
        Vec3 lastPosition;
        Quat lastRotation;
        bool enabled = true;
        JPH::EMotionType motion = JPH::EMotionType::Static;
    };
    std::unordered_map<Entity, BodyRecord> bodies;
    std::unordered_map<uint32_t, Entity> byBodyId;
    std::vector<Entity> dirty;
    std::map<std::pair<uint32_t, uint32_t>, int> pairCounts;

    struct Character {
        JPH::Ref<JPH::CharacterVirtual> character;
        Vec3 lastPosition;
        std::set<uint32_t> touching;
        float height = 0;
    };
    std::unordered_map<Entity, Character> characters;
    bool cursorLocked = false;

    explicit Impl(Game& g) : game(g) {}

    bool running() const { return physics != nullptr; }

    Entity entityOf(JPH::BodyID id) const {
        auto it = byBodyId.find(id.GetIndexAndSequenceNumber());
        return it == byBodyId.end() ? Entity{} : it->second;
    }

    JPH::Ref<JPH::Shape> shapeFor(Entity e, Vec3 scale, float& friction, float& bounciness, bool& trigger) {
        auto& reg = game.scene().registry();
        auto* box = reg.tryGet<BoxCollider>(e);
        auto* sphere = reg.tryGet<SphereCollider>(e);
        Vec3 absScale{std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)};
        float maxScale = std::max({absScale.x, absScale.y, absScale.z});
        friction = 0.5f;
        bounciness = 0;
        trigger = false;
        if (box && sphere) {
            JPH::StaticCompoundShapeSettings compound;
            compound.AddShape(toJ(box->offset * absScale), JPH::Quat::sIdentity(), boxShape(box->size * absScale * 0.5f));
            compound.AddShape(toJ(sphere->offset * absScale), JPH::Quat::sIdentity(),
                              new JPH::SphereShape(std::max(0.005f, sphere->radius * maxScale)));
            friction = box->friction;
            bounciness = box->bounciness;
            trigger = box->isTrigger && sphere->isTrigger;
            auto r = compound.Create();
            return r.IsValid() ? r.Get() : nullptr;
        }
        if (box) {
            friction = box->friction;
            bounciness = box->bounciness;
            trigger = box->isTrigger;
            return offsetShape(boxShape(box->size * absScale * 0.5f), box->offset * absScale);
        }
        if (sphere) {
            friction = sphere->friction;
            bounciness = sphere->bounciness;
            trigger = sphere->isTrigger;
            return offsetShape(new JPH::SphereShape(std::max(0.005f, sphere->radius * maxScale)), sphere->offset * absScale);
        }
        // A RigidBody without a collider gets one that fits its shape.
        if (auto* mr = reg.tryGet<MeshRenderer>(e)) {
            switch (mr->mesh) {
            case MeshShape::Sphere: return new JPH::SphereShape(0.5f * maxScale);
            case MeshShape::Capsule: {
                float radius = 0.5f * std::max(absScale.x, absScale.z);
                float half = std::max(0.01f, absScale.y * 1.0f - radius);
                return new JPH::CapsuleShape(half, radius);
            }
            case MeshShape::Plane: return boxShape({absScale.x * 0.5f, 0.01f, absScale.z * 0.5f});
            default: return boxShape(absScale * 0.5f);
            }
        }
        return boxShape(absScale * 0.5f);
    }

    void createBody(Entity e) {
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        auto* rb = reg.tryGet<RigidBody>(e);
        if (!rb && !reg.has<BoxCollider>(e) && !reg.has<SphereCollider>(e))
            return;
        if (reg.has<CharacterController>(e))
            return; // characters get their own controller
        Vec3 t, s;
        Quat r;
        decompose(scene.worldMatrix(e), t, r, s);
        float friction, bounciness;
        bool trigger;
        JPH::Ref<JPH::Shape> shape = shapeFor(e, s, friction, bounciness, trigger);
        if (!shape)
            return;
        JPH::EMotionType motion = rb ? motionType(rb->type) : JPH::EMotionType::Static;
        JPH::BodyCreationSettings bs(shape, toJR(t), toJ(r), motion,
                                     motion == JPH::EMotionType::Static ? Layers::Static : Layers::Moving);
        bs.mUserData = e.toHandle();
        bs.mFriction = friction;
        bs.mRestitution = bounciness;
        bs.mIsSensor = trigger;
        if (trigger && motion == JPH::EMotionType::Static) {
            // Static sensors must still see moving objects.
            bs.mObjectLayer = Layers::Moving;
            bs.mMotionType = JPH::EMotionType::Kinematic;
            motion = JPH::EMotionType::Kinematic;
        }
        if (rb) {
            bs.mGravityFactor = rb->gravityScale;
            bs.mLinearDamping = rb->linearDamping;
            bs.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            bs.mMassPropertiesOverride.mMass = std::max(rb->mass, 0.001f);
            if (rb->lockRotation)
                bs.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY |
                                  JPH::EAllowedDOFs::TranslationZ;
            bs.mMotionQuality = JPH::EMotionQuality::LinearCast;
        }
        JPH::BodyInterface& bi = physics->GetBodyInterface();
        JPH::BodyID id = bi.CreateAndAddBody(bs, JPH::EActivation::Activate);
        if (id.IsInvalid()) {
            Log::warn("Too many 3D physics objects; '", scene.info(e).name, "' won't collide.");
            return;
        }
        bodies[e] = {id, t, r, true, motion};
        byBodyId[id.GetIndexAndSequenceNumber()] = e;
    }

    void destroyBody(Entity e) {
        auto it = bodies.find(e);
        if (it == bodies.end())
            return;
        JPH::BodyInterface& bi = physics->GetBodyInterface();
        bi.RemoveBody(it->second.id);
        bi.DestroyBody(it->second.id);
        byBodyId.erase(it->second.id.GetIndexAndSequenceNumber());
        bodies.erase(it);
    }

    void createCharacter(Entity e) {
        Scene& scene = game.scene();
        auto& cc = scene.registry().get<CharacterController>(e);
        float radius = std::max(0.05f, cc.radius);
        float height = std::max(cc.height, radius * 2 + 0.01f);
        JPH::Ref<JPH::Shape> capsule = new JPH::CapsuleShape(height * 0.5f - radius, radius);
        // Jolt characters stand on their origin, so lift the capsule to start at the feet.
        JPH::Ref<JPH::Shape> shape = offsetShape(capsule, {0, height * 0.5f, 0});
        JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
        settings->mShape = shape;
        settings->mMaxSlopeAngle = JPH::DegreesToRadians(50.0f);
        settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
        settings->mInnerBodyShape = shape; // lets the character trigger sensors like coins
        settings->mInnerBodyLayer = Layers::Moving;
        Vec3 center = scene.worldPosition(e);
        Vec3 feet = center - Vec3(0, height * 0.5f, 0);
        Character c;
        c.character = new JPH::CharacterVirtual(settings, toJR(feet), JPH::Quat::sIdentity(), e.toHandle(), physics.get());
        c.lastPosition = center;
        c.height = height;
        JPH::BodyID inner = c.character->GetInnerBodyID();
        if (!inner.IsInvalid()) {
            physics->GetBodyInterface().SetUserData(inner, e.toHandle());
            byBodyId[inner.GetIndexAndSequenceNumber()] = e;
        }
        characters[e] = std::move(c);
    }

    void destroyCharacter(Entity e) {
        auto it = characters.find(e);
        if (it == characters.end())
            return;
        JPH::BodyID inner = it->second.character->GetInnerBodyID();
        if (!inner.IsInvalid())
            byBodyId.erase(inner.GetIndexAndSequenceNumber());
        characters.erase(it);
    }

    void sync() {
        Scene& scene = game.scene();
        auto& reg = scene.registry();
        for (Entity e : dirty) {
            destroyBody(e);
            destroyCharacter(e);
        }
        dirty.clear();
        std::vector<Entity> gone;
        for (auto& [e, b] : bodies)
            if (!scene.valid(e) || (!reg.has<RigidBody>(e) && !reg.has<BoxCollider>(e) && !reg.has<SphereCollider>(e)))
                gone.push_back(e);
        for (Entity e : gone)
            destroyBody(e);
        gone.clear();
        for (auto& [e, c] : characters)
            if (!scene.valid(e) || !reg.has<CharacterController>(e))
                gone.push_back(e);
        for (Entity e : gone)
            destroyCharacter(e);

        bool added = false;
        auto ensure = [&](Entity e) {
            if (!bodies.count(e) && !reg.has<CharacterController>(e)) {
                createBody(e);
                added = true;
            }
        };
        for (Entity e : reg.entitiesWith<RigidBody>())
            ensure(e);
        for (Entity e : reg.entitiesWith<BoxCollider>())
            ensure(e);
        for (Entity e : reg.entitiesWith<SphereCollider>())
            ensure(e);
        for (Entity e : reg.entitiesWith<CharacterController>())
            if (!characters.count(e))
                createCharacter(e);
        if (added)
            physics->OptimizeBroadPhase();

        JPH::BodyInterface& bi = physics->GetBodyInterface();
        for (auto& [e, b] : bodies) {
            bool active = scene.isActive(e);
            if (active != b.enabled) {
                if (active)
                    bi.AddBody(b.id, JPH::EActivation::Activate);
                else
                    bi.RemoveBody(b.id);
                b.enabled = active;
            }
            if (!active)
                continue;
            Vec3 t, s;
            Quat r;
            decompose(scene.worldMatrix(e), t, r, s);
            bool moved = length(t - b.lastPosition) > 1e-5f || std::abs(std::abs(dot(r, b.lastRotation)) - 1.0f) > 1e-6f;
            if (moved) {
                // Kinematic bodies moved by scripts glide (so they carry what stands on them);
                // everything else teleports.
                if (b.motion == JPH::EMotionType::Kinematic)
                    bi.MoveKinematic(b.id, toJR(t), toJ(r), 1.0f / 60.0f);
                else
                    bi.SetPositionAndRotation(b.id, toJR(t), toJ(r), JPH::EActivation::Activate);
                b.lastPosition = t;
                b.lastRotation = r;
            }
        }
    }

    void writeBack() {
        Scene& scene = game.scene();
        JPH::BodyInterface& bi = physics->GetBodyInterface();
        for (auto& [e, b] : bodies) {
            if (!b.enabled || b.motion != JPH::EMotionType::Dynamic)
                continue;
            if (!bi.IsActive(b.id))
                continue;
            JPH::RVec3 p;
            JPH::Quat q;
            bi.GetPositionAndRotation(b.id, p, q);
            Vec3 pos{static_cast<float>(p.GetX()), static_cast<float>(p.GetY()), static_cast<float>(p.GetZ())};
            Quat rot = fromJ(q);
            Mat4 world = scene.worldMatrix(e);
            Vec3 t, s;
            Quat r;
            decompose(world, t, r, s);
            scene.setWorldMatrix(e, Mat4::trs(pos, rot, s));
            b.lastPosition = pos;
            b.lastRotation = rot;
        }
    }

    void dispatch() {
        std::vector<ContactQueue::Event> events;
        {
            std::lock_guard lock(contacts.mutex);
            events.swap(contacts.events);
        }
        for (auto& ev : events) {
            uint32_t a = ev.a.GetIndexAndSequenceNumber(), b = ev.b.GetIndexAndSequenceNumber();
            auto key = std::make_pair(std::min(a, b), std::max(a, b));
            int& count = pairCounts[key];
            bool fire = false;
            if (ev.begin) {
                fire = count++ == 0;
            } else if (count > 0) {
                fire = --count == 0;
                if (count == 0)
                    pairCounts.erase(key);
            }
            if (!fire)
                continue;
            Entity ea = entityOf(ev.a), eb = entityOf(ev.b);
            if (!ea || !eb)
                continue;
            bool sensor = ev.sensor;
            if (!ev.begin) {
                auto isSensor = [&](Entity x) {
                    auto& reg = game.scene().registry();
                    auto* box = reg.tryGet<BoxCollider>(x);
                    auto* sphere = reg.tryGet<SphereCollider>(x);
                    return (box && box->isTrigger) || (sphere && sphere->isTrigger);
                };
                sensor = isSensor(ea) || isSensor(eb);
            }
            // Report triggers with the trigger first, like 2D physics does.
            auto& reg = game.scene().registry();
            auto triggerFirst = [&](Entity x) {
                auto* box = reg.tryGet<BoxCollider>(x);
                auto* sphere = reg.tryGet<SphereCollider>(x);
                return (box && box->isTrigger) || (sphere && sphere->isTrigger);
            };
            if (sensor && !triggerFirst(ea))
                std::swap(ea, eb);
            game.scripts().onCollision(ea, eb, ev.begin, sensor);
        }
    }

    void updateCharacter(Entity e, Character& c, float dt) {
        Scene& scene = game.scene();
        auto& cc = scene.registry().get<CharacterController>(e);
        Transform& tr = scene.transform(e);
        Input& input = game.input();
        JPH::CharacterVirtual& ch = *c.character;

        // Scripts or the editor moved the character: teleport.
        Vec3 center = scene.worldPosition(e);
        if (length(center - c.lastPosition) > 1e-4f)
            ch.SetPosition(toJR(center - Vec3(0, c.height * 0.5f, 0)));

        bool grounded = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        Vec3 velocity = cc.velocity;
        if (cc.useInput) {
            Vec2 move{input.axis("horizontal"), input.axis("vertical")};
            if (cc.firstPerson) {
                if (!cursorLocked && game.setCursorLocked && (input.mousePressed(MouseButton::Left))) {
                    game.setCursorLocked(true);
                    cursorLocked = true;
                } else if (cursorLocked && input.keyPressed(keys::Escape) && game.setCursorLocked) {
                    game.setCursorLocked(false);
                    cursorLocked = false;
                }
                if (cursorLocked) {
                    Vec2 d = input.mouseDelta();
                    tr.rotation.y -= d.x * cc.mouseSensitivity;
                    cc.pitch = clamp(cc.pitch - d.y * cc.mouseSensitivity, -89.0f, 89.0f);
                }
                tr.rotation.y -= input.axis("look_x") * 150.0f * dt;
                cc.pitch = clamp(cc.pitch + input.axis("look_y") * 120.0f * dt, -89.0f, 89.0f);
                float yaw = radians(tr.rotation.y);
                Vec3 forward{-std::sin(yaw), 0, -std::cos(yaw)}, right{std::cos(yaw), 0, -std::sin(yaw)};
                Vec3 dir = forward * move.y + right * move.x;
                if (lengthSquared(dir) > 1)
                    dir = normalize(dir);
                velocity.x = dir.x * cc.speed;
                velocity.z = dir.z * cc.speed;
            } else {
                // Third person: move relative to the camera and turn to face the movement.
                Vec2 size = game.screenSize();
                CameraView cam = game.camera(size.x / std::max(size.y, 1.0f));
                Vec3 forward = cam.forward;
                forward.y = 0;
                forward = lengthSquared(forward) > 1e-6f ? normalize(forward) : Vec3(0, 0, -1);
                Vec3 right = normalize(cross(forward, {0, 1, 0}));
                Vec3 dir = forward * move.y + right * move.x;
                if (lengthSquared(dir) > 1)
                    dir = normalize(dir);
                velocity.x = dir.x * cc.speed;
                velocity.z = dir.z * cc.speed;
                if (lengthSquared(dir) > 1e-4f) {
                    float target = degrees(std::atan2(-dir.x, -dir.z));
                    float delta = std::remainder(target - tr.rotation.y, 360.0f);
                    tr.rotation.y += delta * std::min(1.0f, dt * 12.0f);
                }
            }
            if (grounded && input.pressed("jump"))
                velocity.y = std::sqrt(2.0f * cc.gravity * std::max(cc.jumpHeight, 0.0f));
        }
        if (grounded && velocity.y < 0)
            velocity.y = 0;
        velocity.y -= cc.gravity * dt;
        JPH::Vec3 ground = grounded ? ch.GetGroundVelocity() : JPH::Vec3::sZero();
        ch.SetLinearVelocity(toJ(velocity) + ground);

        JPH::CharacterVirtual::ExtendedUpdateSettings settings;
        ch.ExtendedUpdate(dt, toJ(Vec3(0, -cc.gravity, 0)), settings,
                          physics->GetDefaultBroadPhaseLayerFilter(Layers::Moving),
                          physics->GetDefaultLayerFilter(Layers::Moving), {}, {}, *jolt().temp);

        JPH::RVec3 feet = ch.GetPosition();
        Vec3 newCenter{static_cast<float>(feet.GetX()), static_cast<float>(feet.GetY()) + c.height * 0.5f,
                       static_cast<float>(feet.GetZ())};
        scene.setWorldPosition(e, newCenter);
        c.lastPosition = newCenter;
        Vec3 actual = fromJ(ch.GetLinearVelocity() - ground);
        cc.velocity = {velocity.x, actual.y, velocity.z};
        cc.grounded = ch.GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
        if (cc.grounded && cc.velocity.y < 0)
            cc.velocity.y = 0;

        if (cc.firstPerson) {
            // The camera becomes the character's eyes.
            Entity camEntity = SceneRenderer::findCamera(scene);
            if (camEntity && camEntity != e) {
                scene.setWorldPosition(camEntity, newCenter + Vec3(0, c.height * 0.4f, 0));
                if (!scene.parent(camEntity))
                    scene.transform(camEntity).rotation = {cc.pitch, tr.rotation.y, 0};
                else
                    scene.transform(camEntity).rotation = {cc.pitch, 0, 0};
            }
        }

        // Collision callbacks for things the character bumps into.
        std::set<uint32_t> now;
        for (const auto& contact : ch.GetActiveContacts())
            if (contact.mHadCollision && !contact.mBodyB.IsInvalid())
                now.insert(contact.mBodyB.GetIndexAndSequenceNumber());
        for (uint32_t id : now)
            if (!c.touching.count(id))
                if (Entity other = entityOf(JPH::BodyID(id)))
                    game.scripts().onCollision(e, other, true, false);
        for (uint32_t id : c.touching)
            if (!now.count(id))
                if (Entity other = entityOf(JPH::BodyID(id)))
                    game.scripts().onCollision(e, other, false, false);
        c.touching = std::move(now);
    }
};

Physics3D::Physics3D(Game& game) : impl_(std::make_unique<Impl>(game)) {}

Physics3D::~Physics3D() {
    stop();
}

void Physics3D::start() {
    stop();
    auto& reg = impl_->game.scene().registry();
    if (reg.count<RigidBody>() == 0 && reg.count<BoxCollider>() == 0 && reg.count<SphereCollider>() == 0 &&
        reg.count<CharacterController>() == 0)
        return; // no 3D physics in this scene; skip the setup cost
    jolt();
    impl_->physics = std::make_unique<JPH::PhysicsSystem>();
    impl_->physics->Init(8192, 0, 16384, 8192, impl_->bpLayers, impl_->objVsBp, impl_->objPair);
    impl_->physics->SetGravity(toJ(impl_->gravity));
    impl_->physics->SetContactListener(&impl_->contacts);
    impl_->sync();
}

void Physics3D::stop() {
    if (!impl_->running())
        return;
    impl_->characters.clear();
    JPH::BodyInterface& bi = impl_->physics->GetBodyInterface();
    for (auto& [e, b] : impl_->bodies) {
        if (b.enabled)
            bi.RemoveBody(b.id);
        bi.DestroyBody(b.id);
    }
    impl_->bodies.clear();
    impl_->byBodyId.clear();
    impl_->pairCounts.clear();
    impl_->contacts.events.clear();
    impl_->physics.reset();
    impl_->cursorLocked = false;
}

void Physics3D::step(float dt) {
    if (!impl_->running()) {
        // Colliders may be added later by scripts.
        auto& reg = impl_->game.scene().registry();
        if (reg.count<RigidBody>() + reg.count<BoxCollider>() + reg.count<SphereCollider>() +
                reg.count<CharacterController>() ==
            0)
            return;
        start();
        if (!impl_->running())
            return;
    }
    impl_->sync();
    impl_->physics->Update(dt, 1, jolt().temp.get(), jolt().jobs.get());
    impl_->writeBack();
    impl_->dispatch();
}

void Physics3D::updateCharacters(float dt) {
    if (!impl_->running() || dt <= 0)
        return;
    std::vector<Entity> list;
    for (auto& [e, c] : impl_->characters)
        list.push_back(e);
    for (Entity e : list) {
        auto it = impl_->characters.find(e);
        if (it == impl_->characters.end() || !impl_->game.scene().valid(e) || !impl_->game.scene().isActive(e))
            continue;
        impl_->updateCharacter(e, it->second, dt);
    }
}

void Physics3D::setGravity(Vec3 g) {
    impl_->gravity = g;
    if (impl_->running())
        impl_->physics->SetGravity(toJ(g));
}

Vec3 Physics3D::gravity() const { return impl_->gravity; }

void Physics3D::onDestroy(Entity e) {
    if (!impl_->running())
        return;
    impl_->destroyBody(e);
    impl_->destroyCharacter(e);
}

void Physics3D::refresh(Entity e) {
    if (impl_->running())
        impl_->dirty.push_back(e);
}

bool Physics3D::hasBody(Entity e) const { return impl_->running() && impl_->bodies.count(e); }

Vec3 Physics3D::velocity(Entity e) const {
    if (!hasBody(e))
        return {};
    return fromJ(impl_->physics->GetBodyInterface().GetLinearVelocity(impl_->bodies.at(e).id));
}

void Physics3D::setVelocity(Entity e, Vec3 v) {
    if (hasBody(e))
        impl_->physics->GetBodyInterface().SetLinearVelocity(impl_->bodies.at(e).id, toJ(v));
}

void Physics3D::applyForce(Entity e, Vec3 f) {
    if (hasBody(e))
        impl_->physics->GetBodyInterface().AddForce(impl_->bodies.at(e).id, toJ(f));
}

void Physics3D::applyImpulse(Entity e, Vec3 i) {
    if (hasBody(e))
        impl_->physics->GetBodyInterface().AddImpulse(impl_->bodies.at(e).id, toJ(i));
}

bool Physics3D::isOnGround(Entity e) const {
    if (!impl_->running())
        return false;
    auto it = impl_->characters.find(e);
    if (it != impl_->characters.end())
        return it->second.character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround;
    if (!hasBody(e))
        return false;
    // A short ray down from the body's center.
    RayHit hit;
    Vec3 pos = impl_->game.scene().worldPosition(e);
    Vec3 mn, mx;
    float half = 0.5f;
    (void)mn;
    (void)mx;
    if (auto* box = impl_->game.scene().registry().tryGet<BoxCollider>(e))
        half = box->size.y * 0.5f * std::abs(impl_->game.scene().transform(e).scale.y);
    else if (auto* sphere = impl_->game.scene().registry().tryGet<SphereCollider>(e))
        half = sphere->radius;
    return raycast(pos, {0, -1, 0}, half + 0.08f, hit, e);
}

std::vector<Entity> Physics3D::touching(Entity e) const {
    std::vector<Entity> out;
    if (!impl_->running())
        return out;
    auto cit = impl_->characters.find(e);
    if (cit != impl_->characters.end()) {
        for (uint32_t id : cit->second.touching)
            if (Entity o = impl_->entityOf(JPH::BodyID(id)))
                out.push_back(o);
        return out;
    }
    auto it = impl_->bodies.find(e);
    if (it == impl_->bodies.end())
        return out;
    uint32_t mine = it->second.id.GetIndexAndSequenceNumber();
    for (auto& [pair, count] : impl_->pairCounts) {
        if (pair.first == mine)
            if (Entity o = impl_->entityOf(JPH::BodyID(pair.second)))
                out.push_back(o);
        if (pair.second == mine)
            if (Entity o = impl_->entityOf(JPH::BodyID(pair.first)))
                out.push_back(o);
    }
    return out;
}

bool Physics3D::raycast(Vec3 from, Vec3 direction, float maxDistance, RayHit& hit, Entity ignore) const {
    if (!impl_->running() || lengthSquared(direction) < 1e-10f)
        return false;
    Vec3 dir = normalize(direction) * maxDistance;
    JPH::RRayCast ray{toJR(from), toJ(dir)};
    JPH::RayCastResult result;
    JPH::IgnoreSingleBodyFilter ignoreFilter(ignore && impl_->bodies.count(ignore) ? impl_->bodies.at(ignore).id : JPH::BodyID());
    auto cit = impl_->characters.find(ignore);
    JPH::IgnoreSingleBodyFilter characterFilter(cit != impl_->characters.end() ? cit->second.character->GetInnerBodyID()
                                                                               : JPH::BodyID());
    const JPH::BodyFilter& filter = cit != impl_->characters.end() ? static_cast<const JPH::BodyFilter&>(characterFilter)
                                                                    : static_cast<const JPH::BodyFilter&>(ignoreFilter);
    if (!impl_->physics->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, filter))
        return false;
    hit.entity = impl_->entityOf(result.mBodyID);
    JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
    hit.point = {static_cast<float>(point.GetX()), static_cast<float>(point.GetY()), static_cast<float>(point.GetZ())};
    hit.distance = result.mFraction * maxDistance;
    JPH::BodyLockRead lock(impl_->physics->GetBodyLockInterface(), result.mBodyID);
    if (lock.Succeeded())
        hit.normal = fromJ(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point));
    return static_cast<bool>(hit.entity);
}

} // namespace aven
