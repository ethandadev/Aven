#pragma once

#include "rynax/scene/scene.h"

#include <cstdint>

namespace rynax {

// Particle simulation, shared by running games and the editor's live preview.
// `rng` is a random state that advances as particles are made.
void emitParticles(Scene& scene, Entity e, int count, uint32_t& rng);
void simulateParticles(Scene& scene, Entity e, ParticleEmitter& emitter, float dt, uint32_t& rng);

} // namespace rynax
