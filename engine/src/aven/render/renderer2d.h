#pragma once

#include "aven/render/font.h"
#include "aven/render/rhi.h"
#include "aven/scene/components.h"

#include <map>
#include <vector>

namespace aven {

// Batches quads (sprites, shapes, text glyphs, particles) into as few draw calls
// as possible. Quads that share a texture and blend mode are drawn together.
class Renderer2D {
public:
    struct Vertex {
        float x, y, z;
        float u, v;
        uint32_t color;
        float mode; // 0 = textured, 1 = distance-field text
    };

    bool init(rhi::Device* device);
    void shutdown();

    // linearOutput: convert colors to linear light (for the HDR scene target).
    void begin(const Mat4& viewProjection, bool linearOutput, bool depthTest = false);
    // Corners in order: bottom-left, bottom-right, top-right, top-left.
    void quad(const Vec3 corners[4], const Vec2 uvs[4], Color color, rhi::TextureHandle texture,
              rhi::BlendMode blend = rhi::BlendMode::Alpha, float mode = 0);
    // A size.x by size.y rectangle centered on the transform's origin.
    void sprite(const Mat4& world, Vec2 size, Vec4 uvRect, Color color, rhi::TextureHandle texture,
                rhi::BlendMode blend = rhi::BlendMode::Alpha);
    // Axis-aligned rectangle in the current projection's units.
    void rect(Vec2 min, Vec2 max, Color color, rhi::TextureHandle texture, float z = 0);
    // Text block centered on the transform's origin (vertically) and aligned horizontally.
    void text(const Font& font, std::string_view text, const Mat4& world, float size, Color color, TextAlign align);
    // Text in a 2D y-up space, with `topLeft` the top edge of the block and x set by alignment.
    void textAt(const Font& font, std::string_view text, Vec2 anchor, float size, Color color, TextAlign align,
                bool centerVertically);
    void flush();
    void end();

    uint32_t drawCalls() const { return drawCalls_; }

private:
    rhi::Device* device_ = nullptr;
    rhi::ShaderHandle shader_;
    std::map<std::pair<int, bool>, rhi::PipelineHandle> pipelines_;
    rhi::BufferHandle vertexBuffer_, indexBuffer_;
    std::vector<Vertex> vertices_;
    rhi::TextureHandle batchTexture_;
    rhi::BlendMode batchBlend_ = rhi::BlendMode::Alpha;
    bool depthTest_ = false;
    bool active_ = false;
    uint32_t drawCalls_ = 0;

    struct FrameUniforms {
        Mat4 viewProjection;
        float params[4];
    } uniforms_{};

    rhi::PipelineHandle pipeline(rhi::BlendMode blend, bool depth);
    static constexpr size_t kMaxQuads = 4096;
};

} // namespace aven
