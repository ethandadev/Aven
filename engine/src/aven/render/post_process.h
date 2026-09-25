#pragma once

#include "aven/render/rhi.h"
#include "aven/scene/components.h"

#include <vector>

namespace aven {

// Screen effects applied after the scene is drawn: ambient occlusion, bloom,
// tonemapping, color grading, vignette and anti-aliasing.
class PostProcessor {
public:
    bool init(rhi::Device* device);
    void shutdown();
    void resize(int width, int height);

    // Darkens creases and contact points by multiplying the scene color (3D only).
    void applySSAO(rhi::FramebufferHandle sceneFb, rhi::TextureHandle depth, rhi::TextureHandle normals,
                   const Mat4& projection, const PostProcessing& settings);

    // Scene HDR color -> final image in `output`.
    void composite(rhi::TextureHandle sceneColor, rhi::FramebufferHandle output, const PostProcessing* settings);

private:
    rhi::Device* device_ = nullptr;
    int width_ = 0, height_ = 0;

    rhi::ShaderHandle downShader_, upShader_, compositeShader_, fxaaShader_, ssaoShader_, blurShader_;
    rhi::PipelineHandle downPipe_, upPipe_, compositePipe_, fxaaPipe_, ssaoPipe_, blurPipe_, multiplyPipe_;

    struct Level {
        rhi::TextureHandle texture;
        rhi::FramebufferHandle fb;
        int w = 0, h = 0;
    };
    std::vector<Level> bloom_;
    Level ldr_, ao_, aoBlur_;
    rhi::TextureHandle black_;

    Level makeLevel(int w, int h, rhi::PixelFormat format, const char* label);
    void destroyLevel(Level& l);
    void fullscreen(rhi::PipelineHandle pipe, rhi::FramebufferHandle fb, int w, int h,
                    std::initializer_list<rhi::TextureHandle> textures, const void* uniforms, size_t size,
                    bool clear = true);
};

} // namespace aven
