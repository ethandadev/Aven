// Placeholder 3D physics; replaced by the Jolt-based implementation.

#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"

namespace aven {

struct Physics3D::Impl {
    Game& game;
    Vec3 gravity{0, -9.81f, 0};
    explicit Impl(Game& g) : game(g) {}
};

Physics3D::Physics3D(Game& game) : impl_(std::make_unique<Impl>(game)) {}
Physics3D::~Physics3D() = default;
void Physics3D::start() {}
void Physics3D::stop() {}
void Physics3D::step(float) {}
void Physics3D::updateCharacters(float) {}
void Physics3D::setGravity(Vec3 g) { impl_->gravity = g; }
Vec3 Physics3D::gravity() const { return impl_->gravity; }
void Physics3D::onDestroy(Entity) {}
void Physics3D::refresh(Entity) {}
bool Physics3D::hasBody(Entity) const { return false; }
Vec3 Physics3D::velocity(Entity) const { return {}; }
void Physics3D::setVelocity(Entity, Vec3) {}
void Physics3D::applyForce(Entity, Vec3) {}
void Physics3D::applyImpulse(Entity, Vec3) {}
bool Physics3D::isOnGround(Entity) const { return false; }
std::vector<Entity> Physics3D::touching(Entity) const { return {}; }
bool Physics3D::raycast(Vec3, Vec3, float, RayHit&, Entity) const { return false; }

} // namespace aven
