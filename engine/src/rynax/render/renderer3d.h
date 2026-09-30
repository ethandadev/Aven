#pragma once

#include "rynax/assets/assets.h"
#include "rynax/render/rhi.h"
#include "rynax/scene/scene.h"

#include <memory>

namespace rynax {

struct RenderQuality;

struct CameraView;
class Model;

// Physically based 3D renderer: meshes and models, lights with shadows, the sky
// and fog. Draws into the scene's HDR target.
class Renderer3D {
public:
    Renderer3D();
    ~Renderer3D();
    bool init(rhi::Device* device, Assets* assets);
    void shutdown();
    // Shadows, shadow resolution and lights per pixel (quality.h). High is the default.
    void setQuality(const RenderQuality& quality);

    bool hasContent(const Scene& scene) const;
    // Gathers objects and lights, and renders shadow maps. Call before the scene pass.
    void prepare(Scene& scene, const CameraView& camera);
    void drawSky(Scene& scene, const CameraView& camera);
    void drawOpaque(Scene& scene, const CameraView& camera);
    void drawTransparent(Scene& scene, const CameraView& camera);

    // Loads (and caches) a glTF model from the project. Null if it can't be loaded.
    Model* model(const std::string& path);
    // Closest 3D object hit by a world-space ray (uses the real mesh triangles).
    Entity raycast(Scene& scene, Vec3 origin, Vec3 direction, float* distance = nullptr);
    // World-space bounds of an entity's mesh, for gizmos and framing.
    bool worldBounds(Scene& scene, Entity e, Vec3& min, Vec3& max);

    uint32_t drawnObjects() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rynax
