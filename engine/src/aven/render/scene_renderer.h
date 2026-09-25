#pragma once

#include "aven/assets/assets.h"
#include "aven/render/renderer2d.h"
#include "aven/render/rhi.h"
#include "aven/scene/scene.h"

#include <functional>
#include <memory>

namespace aven {

class Renderer3D;
class PostProcessor;

struct CameraView {
    Mat4 view;
    Mat4 projection;
    Mat4 viewProjection;
    Vec3 position;
    Vec3 forward{0, 0, -1};
    bool orthographic = true;
    float orthoSize = 5;
    float fieldOfView = 60;
    float nearClip = 0.1f;
    float farClip = 1000;
    float aspect = 16.0f / 9.0f;
    Color background = Color::fromHex(0x1E2533);
    Entity entity; // the Camera entity, if any

    // World position of a point on screen (pixels, origin top-left) at the given distance.
    Vec3 screenToWorld(Vec2 pixel, Vec2 screenSize, float depth = 0) const;
    Vec2 worldToScreen(Vec3 world, Vec2 screenSize) const;
    // Ray from the camera through a screen pixel.
    void screenRay(Vec2 pixel, Vec2 screenSize, Vec3& origin, Vec3& direction) const;
};

struct RenderOptions {
    bool drawUI = true;
    bool postProcessing = true;
    bool drawCameraBackground = true;
};

// Draws a scene: 3D objects with lighting and shadows, 2D sprites and text,
// post-processing, then screen-space UI on top.
class SceneRenderer {
public:
    SceneRenderer();
    ~SceneRenderer();

    bool init(rhi::Device* device, Assets* assets);
    void shutdown();

    static CameraView cameraFromEntity(const Scene& scene, Entity cameraEntity, float aspect);
    static Entity findCamera(const Scene& scene);
    // The scene's primary camera, or a default 2D camera if there is none.
    static CameraView sceneCamera(const Scene& scene, float aspect);
    static CameraView makeCamera(Vec3 position, Quat rotation, bool orthographic, float sizeOrFov, float aspect,
                                 float nearClip = 0.1f, float farClip = 1000.0f);

    // Renders into the offscreen output (size w x h). Use outputTexture() or present().
    void render(Scene& scene, const CameraView& camera, int width, int height, const RenderOptions& options = {});
    // Copies the last rendered frame to the window.
    void present(int windowWidth, int windowHeight);

    rhi::TextureHandle outputTexture() const { return outputColor_; }
    rhi::FramebufferHandle outputFramebuffer() const { return outputFb_; }
    rhi::FramebufferHandle sceneFramebuffer() const { return sceneFb_; }
    rhi::TextureHandle sceneDepth() const { return sceneDepth_; }
    int width() const { return width_; }
    int height() const { return height_; }

    Renderer2D& renderer2D() { return renderer2D_; }
    Renderer3D& renderer3D() { return *renderer3D_; }
    Assets& assets() { return *assets_; }
    rhi::Device& device() { return *device_; }

    // Extra drawing into the scene (HDR) target after the world, e.g. editor grids and gizmos.
    std::function<void(const CameraView&)> sceneOverlay;
    // Extra drawing on top of the final image, e.g. editor icons.
    std::function<void(const CameraView&)> screenOverlay;

    // Screenshot of the last frame as RGBA8, top row first.
    std::vector<uint8_t> readOutput();

    // Offset applied to the camera (screen shake). Set by the game each frame.
    Vec3 cameraShake;

private:
    rhi::Device* device_ = nullptr;
    Assets* assets_ = nullptr;
    Renderer2D renderer2D_;
    std::unique_ptr<Renderer3D> renderer3D_;
    std::unique_ptr<PostProcessor> post_;
    int width_ = 0, height_ = 0;
    rhi::TextureHandle sceneColor_, sceneNormal_, sceneDepth_, outputColor_;
    rhi::FramebufferHandle sceneFb_, outputFb_;

    void ensureTargets(int w, int h);
    void releaseTargets();
    void draw2D(Scene& scene, const CameraView& camera, bool depthTest);
    void drawUI(Scene& scene, int w, int h);
};

} // namespace aven
