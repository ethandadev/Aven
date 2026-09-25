// Particle simulation: emitting and moving particles for ParticleEmitter components.

#include "aven/runtime/particles.h"

#include <cmath>

namespace aven {

namespace {

float next01(uint32_t& rng) {
    if (rng == 0)
        rng = 12345;
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return (rng & 0xFFFFFF) / static_cast<float>(0x1000000);
}

} // namespace

void emitParticles(Scene& scene, Entity e, int count, uint32_t& rng) {
    auto random01 = [&] { return next01(rng); };
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

void simulateParticles(Scene& scene, Entity e, ParticleEmitter& emitter, float dt, uint32_t& rng) {
    auto& state = scene.registry().getOrEmplace<ParticleState>(e);
    bool active = emitter.emitting && scene.isActive(e);
    if (active && !state.wasEmitting && emitter.burst > 0)
        emitParticles(scene, e, emitter.burst, rng);
    state.wasEmitting = active;
    if (active && emitter.rate > 0) {
        state.emitAccumulator += emitter.rate * dt;
        int n = static_cast<int>(state.emitAccumulator);
        state.emitAccumulator -= static_cast<float>(n);
        if (n > 0)
            emitParticles(scene, e, n, rng);
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

} // namespace aven
