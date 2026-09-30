#pragma once

// Game systems besides scripting: 2D/3D physics, audio, and the gameplay
// helpers (animation, particles, camera follow/shake, UI buttons).

#include "rynax/runtime/project.h"
#include "rynax/scene/scene.h"

#include <memory>
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>

namespace rynax {

class Game;

struct RayHit {
    Entity entity;
    Vec3 point;
    Vec3 normal;
    float distance = 0;
};

class Physics2D {
public:
    explicit Physics2D(Game& game);
    ~Physics2D();
    void start();
    void stop();
    void step(float dt);

    void setGravity(Vec2 gravity);
    Vec2 gravity() const;
    void onDestroy(Entity e);
    void refresh(Entity e);
    void updateLayer(Entity e); // after its collision layer changed
    bool hasBody(Entity e) const;
    // Goes up whenever a collider that doesn't move is made or removed (pathfinding looks again).
    uint64_t staticChanges() const;
    // Makes bodies for new colliders now (normally done on the next step), so queries made
    // before it (like find_path() in on_start) see them.
    void sync();

    Vec2 velocity(Entity e) const;
    void setVelocity(Entity e, Vec2 v);
    void applyForce(Entity e, Vec2 f);
    void applyImpulse(Entity e, Vec2 impulse);
    bool isOnGround(Entity e) const;
    std::vector<Entity> touching(Entity e) const;
    // `layers`: which collision layers it can hit (bit i = layer i).
    bool raycast(Vec2 from, Vec2 to, RayHit& hit, uint32_t layers = 0xFFFFFFFFu) const;
    // Entity whose collider contains the point, if any.
    Entity pointQuery(Vec2 point) const;
    // Whether a circle overlaps something solid that doesn't move (walls, tilemap tiles, ground):
    // not triggers, and not bodies that move. For pathfinding.
    bool blockedAt(Vec2 center, float radius) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Physics3D {
public:
    explicit Physics3D(Game& game);
    ~Physics3D();
    void start();
    void stop();
    void step(float dt);
    // Character controllers read input every frame, not just on physics steps.
    void updateCharacters(float dt);

    void setGravity(Vec3 gravity);
    Vec3 gravity() const;
    void onDestroy(Entity e);
    void refresh(Entity e);
    void updateLayer(Entity e); // after its collision layer changed
    bool hasBody(Entity e) const;
    // Goes up whenever a collider that doesn't move is made or removed (pathfinding looks again).
    uint64_t staticChanges() const;
    void sync(); // as Physics2D::sync

    Vec3 velocity(Entity e) const;
    void setVelocity(Entity e, Vec3 v);
    void applyForce(Entity e, Vec3 f);
    void applyImpulse(Entity e, Vec3 impulse);
    bool isOnGround(Entity e) const;
    std::vector<Entity> touching(Entity e) const;
    bool raycast(Vec3 from, Vec3 direction, float maxDistance, RayHit& hit, Entity ignore = {},
                 uint32_t layers = 0xFFFFFFFFu) const;
    // A ray that only hits solid things that don't move (ground, walls): for pathfinding.
    bool raycastStatic(Vec3 from, Vec3 direction, float maxDistance, RayHit& hit) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class AudioSystem {
public:
    explicit AudioSystem(Game& game);
    ~AudioSystem();
    bool available() const;
    void start();
    void stop();
    void update(float dt);

    void playSound(const std::string& path, float volume = 1, float pitch = 1, std::optional<Vec3> position = {},
                   const std::string& bus = "Effects");
    void playMusic(const std::string& path, float volume = 1, bool loop = true);
    void stopMusic();
    void setMasterVolume(float volume);
    float masterVolume() const;
    void playSource(Entity e);
    void stopSource(Entity e);
    void onDestroy(Entity e);

    // The mixer (Project Settings > Audio mixer). Volumes set while playing last until the game stops.
    std::vector<std::string> busNames() const;
    bool setBusVolume(const std::string& bus, float volume, std::optional<bool> muted = {}); // false: no such bus
    float busVolume(const std::string& bus) const;
    bool busMuted(const std::string& bus) const;
    void applyMixer(const std::vector<AudioBus>& buses); // the editor's mixer, live while playing

private:
    Game& game() const;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class GameplaySystems {
public:
    void seedRandom(uint32_t seed) { rng_ = seed ? seed : 12345; }
    explicit GameplaySystems(Game& game);
    ~GameplaySystems();
    void start();
    void stop();
    // Runs before scripts: UI hover/click detection.
    void preUpdate(float dt);
    // Runs after scripts and physics: animation, particles, camera.
    void update(float dt);

    void shake(float amount, float duration);
    Vec3 shakeOffset() const { return shakeOffset_; }
    void burst(Entity e, int count);
    Entity hoveredButton() const { return hovered_; }

    // Behaviors (behaviors.cpp)
    void startBehaviors();
    void updateBehaviors(float dt);
    void onBehaviorCollision(Entity a, Entity b, bool begin);
    void onBehaviorClick(Entity e);
    // Animator state machines (gameplay.cpp).
    void updateAnimators(float dt);
    void enterAnimState(Entity e, Animator& a, const std::string& state);
    float animParam(Entity e, Animator& a, const std::string& name);
    void runClickStep(Entity self, const ClickStep& step);
    void damage(Entity victim, int amount, Vec3 from, float knockback);
    void sparkle(Vec3 at, Color color);
    Entity entityUnderMouse(Vec3& world);

    // Walking along a path (pathfinding): self.go_to() and Chase's "Walk around walls".
    void goTo(Entity e, Vec3 destination, Entity target, float speed);
    void stopWalking(Entity e);
    bool walking(Entity e) const { return walkers_.count(e.toHandle()) > 0; }
    // The direction to head in to reach `to` around walls (zero when there or no way), updating
    // the cached path every so often.
    Vec3 pathDirection(Entity e, Vec3 to, bool threeD, float stopDistance);
    void updateWalkers(float dt);

private:
    struct BehaviorState {
        bool started = false;
        Vec3 start, startWorld, startScale{1, 1, 1};
        float dir = 1;
        float cooldown = 0, age = 0, invincible = 0, coyote = 0, bounce = 0, linkTimer = 0;
        int jumpsLeft = 0;
        bool collected = false, dragging = false, linkPending = false;
        Vec3 dragOffset;
        std::vector<uint64_t> spawned;
    };
    std::unordered_map<uint64_t, BehaviorState> behaviorStates_;
    struct Walker {
        Vec3 destination;
        uint64_t target = 0; // an object to walk to (it may move), or 0
        float speed = 3;
        std::vector<Vec3> path;
        size_t next = 1;
        float repath = 0;
    };
    std::unordered_map<uint64_t, Walker> walkers_;
    struct PathCache {
        std::vector<Vec3> path;
        size_t next = 1;
        float age = 1e9f;
        Vec3 goal;
    };
    std::unordered_map<uint64_t, PathCache> pathCache_;
    void moveAlong(Entity e, Vec3 dir, float speed, float dt);
    BehaviorState& state(Entity e);
    void moveTo(Entity e, Vec3 local);

    Game& game_;
    float shakeAmount_ = 0, shakeTime_ = 0, shakeDuration_ = 0;
    Vec3 shakeOffset_;
    Entity hovered_, pressed_;
    Entity pressedWorld_;
    uint32_t rng_ = 12345;

    float random01();
    void updateParticles(Entity e, ParticleEmitter& emitter, float dt);
    void updateUI();
    void updateWorldClicks();
};

} // namespace rynax
