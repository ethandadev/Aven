#pragma once

#include "aven/assets/assets.h"
#include "aven/render/rhi.h"
#include "aven/scene/scene.h"

namespace aven {

struct CameraView;

// Physically based 3D renderer: meshes, lights, shadows and the sky.
class Renderer3D {
public:
    Renderer3D();
    ~Renderer3D();
    bool init(rhi::Device* device, Assets* assets);
    void shutdown();

    bool hasContent(const Scene& scene) const;
    // Renders shadow maps (into their own targets) before the main pass begins.
    void prepare(Scene& scene, const CameraView& camera);
    // Draws the sky and opaque objects into the currently open scene pass.
    void drawOpaque(Scene& scene, const CameraView& camera);
    void drawSky(Scene& scene, const CameraView& camera);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

} // namespace aven
