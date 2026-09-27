#include "aven/render/ui_layout.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/particles.h"
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
    startBehaviors();
}

void GameplaySystems::stop() {
    shakeAmount_ = shakeTime_ = shakeDuration_ = 0;
    shakeOffset_ = {};
    hovered_ = pressed_ = pressedWorld_ = {};
    behaviorStates_.clear();
    walkers_.clear();
    pathCache_.clear();
}

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
        if (!scene.info(e).active || scene.registry().has<Hidden>(e))
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
        if (pressed_ && pressed_ == top && scene.valid(top)) {
            game_.scripts().onClick(top);
            onBehaviorClick(top);
        }
        pressed_ = {};
    }
}

void GameplaySystems::updateWorldClicks() {
    Input& input = game_.input();
    if (!input.mousePressed(MouseButton::Left) || hovered_)
        return;
    Vec3 world;
    Entity best = entityUnderMouse(world);
    if (best) {
        game_.scripts().onClick(best);
        onBehaviorClick(best);
    }
}

void GameplaySystems::burst(Entity e, int count) { emitParticles(game_.scene(), e, count, rng_); }

void GameplaySystems::updateParticles(Entity e, ParticleEmitter& emitter, float dt) {
    simulateParticles(game_.scene(), e, emitter, dt, rng_);
}

// Animator: picks the state from its parameters, then drives the SpriteAnimator or ModelAnimator.
void GameplaySystems::enterAnimState(Entity e, Animator& a, const std::string& name) {
    auto it = std::find_if(a.states.begin(), a.states.end(), [&](const AnimState& s) { return s.name == name; });
    if (it == a.states.end())
        return;
    auto& reg = game_.scene().registry();
    a.current = name;
    a.time = 0;
    if (reg.has<SpriteRenderer>(e)) {
        auto& sa = reg.getOrEmplace<SpriteAnimator>(e);
        sa.firstFrame = it->firstFrame;
        sa.lastFrame = std::max(it->firstFrame, it->lastFrame);
        sa.fps = it->fps;
        sa.loop = it->loop;
        sa.playing = true;
        sa.time = 0;
        reg.get<SpriteRenderer>(e).frame = sa.firstFrame;
    }
    if (auto* mr = reg.tryGet<MeshRenderer>(e); mr && mr->mesh == MeshShape::Model) {
        auto& ma = reg.getOrEmplace<ModelAnimator>(e);
        ma.clip = it->clip;
        ma.speed = it->speed;
        ma.mode = it->loop ? ModelAnimationMode::Loop : ModelAnimationMode::Once;
        ma.playing = true;
        ma.time = 0;
    }
}

float GameplaySystems::animParam(Entity e, Animator& a, const std::string& name) {
    for (auto& p : a.params)
        if (p.name == name)
            return p.value;
    if (name == "time_in_state")
        return a.time;
    script::Value v;
    ScriptSystem& scripts = game_.scripts();
    if (name == "speed") {
        // How fast it moves sideways (2D) or along the ground (3D).
        Vec3 vel;
        if (scripts.getProperty(e, "velocity", v) && v.isVec())
            vel = ScriptSystem::toVec3(v, "speed");
        bool threeD = game_.scene().registry().has<MeshRenderer>(e) || game_.scene().registry().has<CharacterController>(e);
        return threeD ? std::sqrt(vel.x * vel.x + vel.z * vel.z) : std::abs(vel.x);
    }
    if (name == "vertical_speed")
        return scripts.getProperty(e, "velocity_y", v) && v.isNumber() ? static_cast<float>(v.number()) : 0.0f;
    if (name == "on_ground")
        return scripts.getProperty(e, "on_ground", v) && v.truthy() ? 1.0f : 0.0f;
    return 0.0f;
}

void GameplaySystems::updateAnimators(float dt) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    for (Entity e : reg.entitiesWith<Animator>()) {
        if (!scene.isActive(e))
            continue;
        auto& a = reg.get<Animator>(e);
        if (a.states.empty())
            continue;
        if (a.current.empty())
            enterAnimState(e, a, a.startState.empty() ? a.states.front().name : a.startState);
        a.time += dt;
        auto state = std::find_if(a.states.begin(), a.states.end(), [&](const AnimState& s) { return s.name == a.current; });
        bool finished = false;
        if (state != a.states.end()) {
            auto* sa = reg.tryGet<SpriteAnimator>(e);
            if (!state->loop && sa)
                finished = !sa->playing;
        }
        // The first transition whose condition holds, from this state or from any state.
        for (auto& t : a.transitions) {
            if ((t.from != "*" && t.from != a.current) || t.to == a.current || t.to.empty())
                continue;
            bool go = false;
            switch (t.when) {
            case AnimCondition::IsTrue: go = animParam(e, a, t.param) > 0.5f; break;
            case AnimCondition::IsFalse: go = animParam(e, a, t.param) <= 0.5f; break;
            case AnimCondition::Greater: go = animParam(e, a, t.param) > t.value; break;
            case AnimCondition::Less: go = animParam(e, a, t.param) < t.value; break;
            case AnimCondition::Triggered:
                for (auto& p : a.params)
                    if (p.name == t.param && p.value > 0.5f) {
                        go = true;
                        p.value = 0; // a trigger is used up
                    }
                break;
            case AnimCondition::Finished: go = finished || (t.value > 0 && a.time >= t.value); break;
            case AnimCondition::Always: go = true; break;
            case AnimCondition::Count: break;
            }
            if (go) {
                enterAnimState(e, a, t.to);
                break;
            }
        }
    }
}

void GameplaySystems::update(float dt) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();

    updateAnimators(dt);

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
