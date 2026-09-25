#include "aven/render/renderer3d.h"

#include "aven/render/scene_renderer.h"

namespace aven {

struct Renderer3D::Impl {
    rhi::Device* device = nullptr;
    Assets* assets = nullptr;
};

Renderer3D::Renderer3D() : impl_(new Impl) {}
Renderer3D::~Renderer3D() { delete impl_; }

bool Renderer3D::init(rhi::Device* device, Assets* assets) {
    impl_->device = device;
    impl_->assets = assets;
    return true;
}

void Renderer3D::shutdown() {}

bool Renderer3D::hasContent(const Scene& scene) const {
    return scene.registry().count<MeshRenderer>() > 0;
}

void Renderer3D::prepare(Scene&, const CameraView&) {}
void Renderer3D::drawOpaque(Scene&, const CameraView&) {}
void Renderer3D::drawSky(Scene&, const CameraView&) {}

} // namespace aven
