#pragma once

// Game systems besides scripting: 2D/3D physics, audio, and the gameplay
// helpers (animation, particles, camera follow/shake, UI buttons).

#include "aven/scene/scene.h"

#include <memory>
#include <unordered_map>
#include <optional>
#include <string>
#include <vector>

namespace aven {

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
    bool hasBody(Entity e) const;

    Vec2 velocity(Entity e) const;
    void setVelocity(Entity e, Vec2 v);
    void applyForce(Entity e, Vec2 f);
    void applyImpulse(Entity e, Vec2 impulse);
    bool isOnGround(Entity e) const;
    std::vector<Entity> touching(Entity e) const;
    bool raycast(Vec2 from, Vec2 to, RayHit& hit) const;
    // Entity whose collider contains the point, if any.
    Entity pointQuery(Vec2 point) const;

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
    bool hasBody(Entity e) const;

    Vec3 velocity(Entity e) const;
    void setVelocity(Entity e, Vec3 v);
    void applyForce(Entity e, Vec3 f);
    void applyImpulse(Entity e, Vec3 impulse);
    bool isOnGround(Entity e) const;
    std::vector<Entity> touching(Entity e) const;
    bool raycast(Vec3 from, Vec3 direction, float maxDistance, RayHit& hit, Entity ignore = {}) const;

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

    void playSound(const std::string& path, float volume = 1, float pitch = 1, std::optional<Vec3> position = {});
    void playMusic(const std::string& path, float volume = 1, bool loop = true);
    void stopMusic();
    void setMasterVolume(float volume);
    float masterVolume() const;
    void playSource(Entity e);
    void stopSource(Entity e);
    void onDestroy(Entity e);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class GameplaySystems {
public:
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
    void damage(Entity victim, int amount, Vec3 from, float knockback);
    void sparkle(Vec3 at, Color color);
    Entity entityUnderMouse(Vec3& world);

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

} // namespace aven
