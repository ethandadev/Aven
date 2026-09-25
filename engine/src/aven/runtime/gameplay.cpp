#include "aven/render/ui_layout.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"

#include <algorithm>
#include <cmath>

namespace aven {

GameplaySystems::GameplaySystems(Game& game) : game_(game) {}
GameplaySystems::~GameplaySystems() = default;

void GameplaySystems::start() {
    shakeAmount_ = shakeTime_ = shakeDuration_ = 0;
    shakeOffset_ = {};
    hovered_ = pressed_ = pressedWorld_ = {};
}

void GameplaySystems::stop() { start(); }

float GameplaySystems::random01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return (rng_ & 0xFFFFFF) / static_cast<float>(0x1000000);
}

void GameplaySystems::shake(float amount, float duration) {
    shakeAmount_ = std::max(shakeAmount_ * (shakeDuration_ > 0 ? 1 - shakeTime_ / shakeDuration_ : 0), amount);
    shakeDuration_ = std::max(duration, 0.01f);
    shakeTime_ = 0;
}

void GameplaySystems::preUpdate(float) {
    updateUI();
    updateWorldClicks();
}

void GameplaySystems::updateUI() {
    Scene& scene = game_.scene();
    Input& input = game_.input();
    Vec2 size = game_.screenSize();
    Vec2 mouse = input.mousePosition();
    Vec2 point{mouse.x, size.y - mouse.y};
    Entity top;
    int topOrder = -1000000;
    uint32_t seq = 0, topSeq = 0;
    scene.walk([&](Entity e, int) {
        if (!scene.info(e).active)
            return false;
        ++seq;
        auto* btn = scene.registry().tryGet<UIButton>(e);
        if (!btn)
            return true;
        btn->hovered = false;
        UIRect r = computeUIRect(scene, e, size.x, size.y);
        int order = scene.registry().get<UIElement>(e).order;
        if (r.contains(point) && (order > topOrder || (order == topOrder && seq > topSeq))) {
            top = e;
            topOrder = order;
            topSeq = seq;
        }
        return true;
    });
    hovered_ = top;
    if (top)
        scene.registry().get<UIButton>(top).hovered = true;
    if (input.mousePressed(MouseButton::Left))
        pressed_ = top;
    for (Entity e : scene.registry().entitiesWith<UIButton>())
        scene.registry().get<UIButton>(e).pressed = (e == pressed_ && e == top && input.mouseDown(MouseButton::Left));
    if (input.mouseReleased(MouseButton::Left)) {
        if (pressed_ && pressed_ == top && scene.valid(top))
            game_.scripts().onClick(top);
        pressed_ = {};
    }
}

void GameplaySystems::updateWorldClicks() {
    Input& input = game_.input();
    if (!input.mousePressed(MouseButton::Left) || hovered_)
        return;
    Scene& scene = game_.scene();
    Vec2 size = game_.screenSize();
    CameraView cam = game_.camera(size.x / std::max(size.y, 1.0f));
    Vec3 world = cam.screenToWorld(input.mousePosition(), size);
    // Topmost sprite under the mouse (highest order, then last drawn).
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
        if (game_.physics3D().raycast(origin, dir, 1000.0f, hit))
            best = hit.entity;
    }
    if (best)
        game_.scripts().onClick(best);
}

void GameplaySystems::burst(Entity e, int count) {
    Scene& scene = game_.scene();
    auto* emitter = scene.registry().tryGet<ParticleEmitter>(e);
    if (!emitter)
        return;
    auto& state = scene.registry().getOrEmplace<ParticleState>(e);
    Mat4 world = scene.worldMatrix(e);
    Vec3 origin{world.m[12], world.m[13], world.m[14]};
    bool flat = !scene.registry().has<MeshRenderer>(e) && std::abs(emitter->direction.z) < 1e-4f &&
                std::abs(emitter->gravity.z) < 1e-4f && emitter->shapeType != EmitterShape::Sphere;
    for (int i = 0; i < count && static_cast<int>(state.particles.size()) < emitter->maxParticles; ++i) {
        Particle p;
        Vec3 offset;
        float s = emitter->shapeSize;
        switch (emitter->shapeType) {
        case EmitterShape::Point: break;
        case EmitterShape::Circle: {
            float a = random01() * 2 * kPi, r = std::sqrt(random01()) * s;
            offset = {std::cos(a) * r, std::sin(a) * r, 0};
            break;
        }
        case EmitterShape::Sphere: {
            Vec3 d = normalize(Vec3(random01() * 2 - 1, random01() * 2 - 1, random01() * 2 - 1));
            offset = d * (std::cbrt(random01()) * s);
            break;
        }
        case EmitterShape::Box: offset = {(random01() - 0.5f) * s, (random01() - 0.5f) * s, flat ? 0 : (random01() - 0.5f) * s}; break;
        }
        Vec3 dir = normalize(transformDirection(world, emitter->direction));
        if (lengthSquared(dir) < 1e-6f)
            dir = {0, 1, 0};
        float spread = radians(emitter->spread) * 0.5f;
        if (flat) {
            float a = std::atan2(dir.y, dir.x) + (random01() * 2 - 1) * spread;
            dir = {std::cos(a), std::sin(a), 0};
        } else {
            // Random direction inside a cone around `dir`.
            Vec3 up = std::abs(dir.y) < 0.99f ? Vec3(0, 1, 0) : Vec3(1, 0, 0);
            Vec3 t1 = normalize(cross(dir, up)), t2 = cross(dir, t1);
            float theta = random01() * spread, phi = random01() * 2 * kPi;
            dir = normalize(dir * std::cos(theta) + (t1 * std::cos(phi) + t2 * std::sin(phi)) * std::sin(theta));
        }
        p.position = emitter->worldSpace ? origin + offset : offset;
        p.velocity = dir * (emitter->speed * (0.75f + random01() * 0.5f));
        p.life = emitter->lifetime * (0.7f + random01() * 0.6f);
        p.spin = (random01() * 2 - 1) * 180.0f;
        p.rotation = random01() * 360.0f;
        state.particles.push_back(p);
    }
}

void GameplaySystems::updateParticles(Entity e, ParticleEmitter& emitter, float dt) {
    Scene& scene = game_.scene();
    auto& state = scene.registry().getOrEmplace<ParticleState>(e);
    bool active = emitter.emitting && scene.isActive(e);
    if (active && !state.wasEmitting && emitter.burst > 0)
        burst(e, emitter.burst);
    state.wasEmitting = active;
    if (active && emitter.rate > 0) {
        state.emitAccumulator += emitter.rate * dt;
        int n = static_cast<int>(state.emitAccumulator);
        state.emitAccumulator -= static_cast<float>(n);
        if (n > 0)
            burst(e, n);
    }
    auto& ps = scene.registry().get<ParticleState>(e).particles;
    for (size_t i = 0; i < ps.size();) {
        Particle& p = ps[i];
        p.age += dt;
        if (p.age >= p.life) {
            p = ps.back();
            ps.pop_back();
            continue;
        }
        p.velocity += emitter.gravity * dt;
        p.position += p.velocity * dt;
        p.rotation += p.spin * dt;
        ++i;
    }
}

void GameplaySystems::update(float dt) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();

    reg.each<SpriteAnimator>([&](Entity e, SpriteAnimator& anim) {
        auto* sr = reg.tryGet<SpriteRenderer>(e);
        if (!sr || !anim.playing || !scene.isActive(e))
            return;
        int count = std::max(1, anim.lastFrame - anim.firstFrame + 1);
        anim.time += dt * anim.fps;
        int step = static_cast<int>(anim.time);
        if (anim.loop) {
            sr->frame = anim.firstFrame + step % count;
        } else if (step >= count) {
            sr->frame = anim.lastFrame;
            anim.playing = false;
        } else {
            sr->frame = anim.firstFrame + step;
        }
    });

    reg.each<ParticleEmitter>([&](Entity e, ParticleEmitter& emitter) { updateParticles(e, emitter, dt); });

    reg.each<ModelAnimator>([&](Entity e, ModelAnimator& anim) {
        if (anim.playing && scene.isActive(e))
            anim.time += dt * anim.speed;
    });

    reg.each<CameraFollow>([&](Entity e, CameraFollow& follow) {
        Entity target = scene.findByUUID(follow.target);
        if (!target || !scene.isActive(e))
            return;
        Vec3 goal = scene.worldPosition(target) + follow.offset;
        Vec3 pos = scene.worldPosition(e);
        float k = follow.smoothness <= 0 ? 1.0f : 1.0f - std::exp(-follow.smoothness * dt);
        Vec3 next = pos;
        if (follow.followX)
            next.x = lerp(pos.x, goal.x, k);
        if (follow.followY)
            next.y = lerp(pos.y, goal.y, k);
        if (follow.followZ)
            next.z = lerp(pos.z, goal.z, k);
        scene.setWorldPosition(e, next);
        if (follow.lookAtTarget) {
            Vec3 d = scene.worldPosition(target) - next;
            float yaw = degrees(std::atan2(-d.x, -d.z));
            float pitch = degrees(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)));
            scene.transform(e).rotation = {pitch, yaw, 0};
        }
    });

    if (shakeDuration_ > 0) {
        shakeTime_ += dt;
        float t = 1.0f - shakeTime_ / shakeDuration_;
        if (t <= 0) {
            shakeDuration_ = 0;
            shakeOffset_ = {};
        } else {
            float a = shakeAmount_ * t * t;
            shakeOffset_ = {(random01() * 2 - 1) * a, (random01() * 2 - 1) * a, 0};
        }
    }
}

} // namespace aven
