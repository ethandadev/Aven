#include "aven/render/renderer2d.h"

#include "aven/core/log.h"

#include <cstddef>

namespace aven {

namespace {

const char* kVertex = R"(
layout(std140) uniform Frame2D { mat4 u_view_proj; vec4 u_params; };
in vec3 a_position;
in vec2 a_uv;
in vec4 a_color;
in float a_mode;
out vec2 v_uv;
out vec4 v_color;
flat out float v_mode;
void main() {
    v_uv = a_uv;
    v_color = a_color;
    v_mode = a_mode;
    gl_Position = u_view_proj * vec4(a_position, 1.0);
}
)";

const char* kFragment = R"(
layout(std140) uniform Frame2D { mat4 u_view_proj; vec4 u_params; };
uniform sampler2D u_texture;
in vec2 v_uv;
in vec4 v_color;
flat in float v_mode;
out vec4 frag_color;
void main() {
    vec4 c;
    if (v_mode > 0.5) {
        float d = texture(u_texture, v_uv).a;
        float w = max(fwidth(d) * 0.75, 1e-4);
        float edge = 180.0 / 255.0;
        c = vec4(v_color.rgb, v_color.a * smoothstep(edge - w, edge + w, d));
    } else {
        c = texture(u_texture, v_uv) * v_color;
    }
    if (c.a <= 0.002) discard;
    if (u_params.x > 0.5) c.rgb = pow(c.rgb, vec3(2.2));
    frag_color = c;
}
)";

} // namespace

bool Renderer2D::init(rhi::Device* device) {
    device_ = device;
    rhi::ShaderDesc sd;
    sd.vertex = kVertex;
    sd.fragment = kFragment;
    sd.attributes = {"a_position", "a_uv", "a_color", "a_mode"};
    sd.uniformBlocks = {"Frame2D"};
    sd.textures = {"u_texture"};
    sd.label = "sprite";
    shader_ = device_->createShader(sd);
    if (!shader_)
        return false;

    rhi::BufferDesc vb;
    vb.type = rhi::BufferType::Vertex;
    vb.usage = rhi::Usage::Stream;
    vb.size = kMaxQuads * 4 * sizeof(Vertex);
    vb.label = "sprite vertices";
    vertexBuffer_ = device_->createBuffer(vb);

    std::vector<uint16_t> indices(kMaxQuads * 6);
    for (size_t q = 0; q < kMaxQuads; ++q) {
        uint16_t base = static_cast<uint16_t>(q * 4);
        uint16_t* i = &indices[q * 6];
        i[0] = base;
        i[1] = static_cast<uint16_t>(base + 1);
        i[2] = static_cast<uint16_t>(base + 2);
        i[3] = base;
        i[4] = static_cast<uint16_t>(base + 2);
        i[5] = static_cast<uint16_t>(base + 3);
    }
    rhi::BufferDesc ib;
    ib.type = rhi::BufferType::Index;
    ib.data = indices.data();
    ib.size = indices.size() * sizeof(uint16_t);
    ib.label = "sprite indices";
    indexBuffer_ = device_->createBuffer(ib);
    vertices_.reserve(kMaxQuads * 4);
    return true;
}

void Renderer2D::shutdown() {
    if (!device_)
        return;
    for (auto& [k, p] : pipelines_)
        device_->destroy(p);
    pipelines_.clear();
    device_->destroy(shader_);
    device_->destroy(vertexBuffer_);
    device_->destroy(indexBuffer_);
    device_ = nullptr;
}

rhi::PipelineHandle Renderer2D::pipeline(rhi::BlendMode blend, bool depth) {
    auto key = std::make_pair(static_cast<int>(blend), depth);
    auto it = pipelines_.find(key);
    if (it != pipelines_.end())
        return it->second;
    rhi::PipelineDesc pd;
    pd.shader = shader_;
    pd.layout.stride = sizeof(Vertex);
    pd.layout.attributes = {
        {rhi::VertexFormat::Float3, offsetof(Vertex, x)},
        {rhi::VertexFormat::Float2, offsetof(Vertex, u)},
        {rhi::VertexFormat::UByte4N, offsetof(Vertex, color)},
        {rhi::VertexFormat::Float, offsetof(Vertex, mode)},
    };
    pd.indexType = rhi::IndexType::UInt16;
    pd.blend = blend;
    pd.depthTest = depth;
    pd.depthWrite = false;
    pd.label = "sprite";
    return pipelines_[key] = device_->createPipeline(pd);
}

void Renderer2D::begin(const Mat4& viewProjection, bool linearOutput, bool depthTest) {
    uniforms_.viewProjection = viewProjection;
    uniforms_.params[0] = linearOutput ? 1.0f : 0.0f;
    depthTest_ = depthTest;
    vertices_.clear();
    batchTexture_ = {};
    active_ = true;
    drawCalls_ = 0;
}

void Renderer2D::quad(const Vec3 c[4], const Vec2 uv[4], Color color, rhi::TextureHandle texture,
                      rhi::BlendMode blend, float mode) {
    if (!texture)
        return;
    if (!vertices_.empty() && (texture != batchTexture_ || blend != batchBlend_ || vertices_.size() >= kMaxQuads * 4))
        flush();
    batchTexture_ = texture;
    batchBlend_ = blend;
    uint32_t packed = color.toRGBA8();
    for (int i = 0; i < 4; ++i)
        vertices_.push_back({c[i].x, c[i].y, c[i].z, uv[i].x, uv[i].y, packed, mode});
}

void Renderer2D::sprite(const Mat4& world, Vec2 size, Vec4 uvRect, Color color, rhi::TextureHandle texture,
                        rhi::BlendMode blend) {
    float hx = size.x * 0.5f, hy = size.y * 0.5f;
    Vec3 corners[4] = {transformPoint(world, {-hx, -hy, 0}), transformPoint(world, {hx, -hy, 0}),
                       transformPoint(world, {hx, hy, 0}), transformPoint(world, {-hx, hy, 0})};
    Vec2 uvs[4] = {{uvRect.x, uvRect.y}, {uvRect.z, uvRect.y}, {uvRect.z, uvRect.w}, {uvRect.x, uvRect.w}};
    quad(corners, uvs, color, texture, blend);
}

void Renderer2D::rect(Vec2 mn, Vec2 mx, Color color, rhi::TextureHandle texture, float z) {
    Vec3 corners[4] = {{mn.x, mn.y, z}, {mx.x, mn.y, z}, {mx.x, mx.y, z}, {mn.x, mx.y, z}};
    Vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    quad(corners, uvs, color, texture);
}

void Renderer2D::text(const Font& font, std::string_view str, const Mat4& world, float size, Color color,
                      TextAlign align) {
    if (!font.valid() || str.empty())
        return;
    Vec2 extent = font.measure(str, size);
    float top = extent.y * 0.5f;
    font.layout(str, size, align, [&](const Font::Glyph& g, float x, float y, float scale) {
        if (g.x1 <= g.x0)
            return;
        float l = x + g.x0 * scale, r = x + g.x1 * scale;
        float t = top - (y + g.y0 * scale), b = top - (y + g.y1 * scale);
        Vec3 corners[4] = {transformPoint(world, {l, b, 0}), transformPoint(world, {r, b, 0}),
                           transformPoint(world, {r, t, 0}), transformPoint(world, {l, t, 0})};
        Vec2 uvs[4] = {{g.u0, g.v1}, {g.u1, g.v1}, {g.u1, g.v0}, {g.u0, g.v0}};
        quad(corners, uvs, color, font.atlas(), rhi::BlendMode::Alpha, 1.0f);
    });
}

void Renderer2D::textAt(const Font& font, std::string_view str, Vec2 anchor, float size, Color color,
                        TextAlign align, bool centerVertically) {
    if (!font.valid() || str.empty())
        return;
    float top = anchor.y + (centerVertically ? font.measure(str, size).y * 0.5f : 0.0f);
    font.layout(str, size, align, [&](const Font::Glyph& g, float x, float y, float scale) {
        if (g.x1 <= g.x0)
            return;
        float l = anchor.x + x + g.x0 * scale, r = anchor.x + x + g.x1 * scale;
        float t = top - (y + g.y0 * scale), b = top - (y + g.y1 * scale);
        Vec3 corners[4] = {{l, b, 0}, {r, b, 0}, {r, t, 0}, {l, t, 0}};
        Vec2 uvs[4] = {{g.u0, g.v1}, {g.u1, g.v1}, {g.u1, g.v0}, {g.u0, g.v0}};
        quad(corners, uvs, color, font.atlas(), rhi::BlendMode::Alpha, 1.0f);
    });
}

void Renderer2D::flush() {
    if (vertices_.empty() || !active_)
        return;
    device_->updateBuffer(vertexBuffer_, vertices_.data(), vertices_.size() * sizeof(Vertex));
    device_->applyPipeline(pipeline(batchBlend_, depthTest_));
    rhi::Bindings b;
    b.vertexBuffer = vertexBuffer_;
    b.indexBuffer = indexBuffer_;
    b.textures[0] = batchTexture_;
    device_->applyBindings(b);
    device_->applyUniforms(0, &uniforms_, sizeof uniforms_);
    device_->drawIndexed(0, static_cast<uint32_t>(vertices_.size() / 4 * 6));
    ++drawCalls_;
    vertices_.clear();
}

void Renderer2D::end() {
    flush();
    active_ = false;
}

} // namespace aven
