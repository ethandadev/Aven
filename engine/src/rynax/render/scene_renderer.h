#pragma once

#include "rynax/assets/assets.h"
#include "rynax/render/quality.h"
#include "rynax/render/renderer2d.h"
#include "rynax/render/rhi.h"
#include "rynax/scene/scene.h"

#include <functional>
#include <memory>

namespace rynax {

class Renderer3D;
class PostProcessor;
class DebugDraw;

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
    bool debugDraw = false; // Game::render: also draw the game's debug_line() shapes
    bool renderScale = true; // draw the world with fewer pixels on Low quality (the editor has its own setting)
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

    // Graphics quality (Low / Medium / High / Ultra, quality.h): shadows, lights, SSAO, bloom,
    // anti-aliasing, render scale.
    void setQuality(const RenderQuality& quality);
    const RenderQuality& quality() const { return quality_; }

    rhi::TextureHandle outputTexture() const { return outputColor_; }
    rhi::FramebufferHandle outputFramebuffer() const { return outputFb_; }
    rhi::FramebufferHandle sceneFramebuffer() const { return sceneFb_; }
    rhi::TextureHandle sceneDepth() const { return sceneDepth_; } // (sceneWidth() x sceneHeight())
    int width() const { return width_; }
    int height() const { return height_; }
    int sceneWidth() const { return sceneWidth_; }
    int sceneHeight() const { return sceneHeight_; }

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
    // Shapes scripts drew with debug_line() and friends; null = don't draw them.
    const DebugDraw* debugDraw = nullptr;

private:
    rhi::Device* device_ = nullptr;
    Assets* assets_ = nullptr;
    Renderer2D renderer2D_;
    std::unique_ptr<Renderer3D> renderer3D_;
    std::unique_ptr<PostProcessor> post_;
    int width_ = 0, height_ = 0;
    int sceneWidth_ = 0, sceneHeight_ = 0; // (smaller than the output on a lower render scale)
    RenderQuality quality_;
    rhi::TextureHandle sceneColor_, sceneNormal_, sceneDepth_, outputColor_;
    rhi::FramebufferHandle sceneFb_, outputFb_;
    rhi::FramebufferHandle sceneColorFb_; // the scene without its normals: they're only for SSAO

    void ensureTargets(int w, int h, int sceneW, int sceneH);
    void releaseTargets();
    void draw2D(Scene& scene, const CameraView& camera, bool depthTest);
    void drawTilemap(const Tilemap& map, const Mat4& world, const CameraView& camera);
    void drawUI(Scene& scene, int w, int h);
};

} // namespace rynax
