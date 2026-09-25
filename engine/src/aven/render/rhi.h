#pragma once

// Render Hardware Interface: the only layer that talks to a graphics API.
// The renderer is written against this interface, so new backends (Vulkan,
// Metal, Direct3D 12) can be added without touching rendering code.
// OpenGL 3.3 is the first backend because it runs almost everywhere.

#include "aven/math/math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace aven::rhi {

enum class Backend { OpenGL, Vulkan, Metal, D3D12 };
const char* backendName(Backend b);

template <class Tag> struct Handle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
    explicit operator bool() const { return id != 0; }
    bool operator==(const Handle&) const = default;
};
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using ShaderHandle = Handle<struct ShaderTag>;
using PipelineHandle = Handle<struct PipelineTag>;
using FramebufferHandle = Handle<struct FramebufferTag>;

enum class BufferType { Vertex, Index };
enum class Usage { Immutable, Dynamic, Stream };

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    Usage usage = Usage::Immutable;
    const void* data = nullptr;
    size_t size = 0;
    const char* label = nullptr;
};

enum class PixelFormat { RGBA8, RGBA16F, RG16F, R8, R16F, R32F, Depth24, Depth32F };
enum class Filter { Nearest, Linear };
enum class Wrap { Repeat, Clamp, Mirror };

struct TextureDesc {
    int width = 1;
    int height = 1;
    PixelFormat format = PixelFormat::RGBA8;
    Filter filter = Filter::Linear;
    Wrap wrap = Wrap::Clamp;
    bool mipmaps = false;
    bool renderTarget = false;
    bool depthCompare = false; // sampled as a shadow map (sampler2DShadow)
    const void* data = nullptr;
    const char* label = nullptr;
};

enum class VertexFormat { Float, Float2, Float3, Float4, UByte4N };

struct VertexAttribute {
    VertexFormat format;
    uint32_t offset;
};

struct VertexLayout {
    uint32_t stride = 0;
    std::vector<VertexAttribute> attributes; // attribute i binds to shader attribute i
};

struct ShaderDesc {
    std::string vertex;
    std::string fragment;
    std::vector<std::string> attributes;    // names, location = index
    std::vector<std::string> uniformBlocks; // names, slot = index
    std::vector<std::string> textures;      // sampler names, slot = index
    std::vector<std::string> outputs;       // fragment outputs, location = index (default "frag_color")
    const char* label = nullptr;
};

enum class Primitive { Triangles, TriangleStrip, Lines, LineStrip, Points };
enum class CullMode { None, Back, Front };
enum class CompareFunc { Never, Less, LessEqual, Equal, Greater, GreaterEqual, NotEqual, Always };
enum class BlendMode { Opaque, Alpha, Additive, Premultiplied, Multiply };
enum class IndexType { UInt16, UInt32 };

struct PipelineDesc {
    ShaderHandle shader;
    VertexLayout layout;
    Primitive primitive = Primitive::Triangles;
    IndexType indexType = IndexType::UInt32;
    CullMode cull = CullMode::None;
    bool depthTest = false;
    bool depthWrite = false;
    CompareFunc depthCompare = CompareFunc::LessEqual;
    BlendMode blend = BlendMode::Opaque;
    bool colorWrite = true;
    int colorTargets = 1;
    float depthBias = 0;
    float slopeBias = 0;
    const char* label = nullptr;
};

struct FramebufferDesc {
    std::vector<TextureHandle> colors;
    TextureHandle depth;
    const char* label = nullptr;
};

struct PassDesc {
    FramebufferHandle framebuffer; // invalid = the window
    int width = 0;
    int height = 0;
    bool clearColor = true;
    Color clearValue{0, 0, 0, 1};
    bool clearDepth = true;
    float depthValue = 1.0f;
    const char* label = nullptr;
};

constexpr int kMaxTextureSlots = 12;
constexpr int kMaxUniformSlots = 4;

struct Bindings {
    BufferHandle vertexBuffer;
    size_t vertexOffset = 0;
    BufferHandle indexBuffer;
    TextureHandle textures[kMaxTextureSlots];
};

struct TextureInfo {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::RGBA8;
};

struct FrameStats {
    uint32_t drawCalls = 0;
    uint64_t triangles = 0;
    uint32_t passes = 0;
};

class Device {
public:
    virtual ~Device() = default;

    virtual Backend backend() const = 0;
    virtual std::string description() const = 0; // GPU / driver

    virtual BufferHandle createBuffer(const BufferDesc& desc) = 0;
    // Replaces the buffer's contents; dynamic/stream buffers grow as needed.
    virtual void updateBuffer(BufferHandle buffer, const void* data, size_t size) = 0;
    virtual TextureHandle createTexture(const TextureDesc& desc) = 0;
    virtual void updateTexture(TextureHandle texture, const void* data) = 0;
    virtual void updateTextureRegion(TextureHandle texture, int x, int y, int w, int h, const void* data) = 0;
    virtual void generateMipmaps(TextureHandle texture) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& desc, std::string* error = nullptr) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc& desc) = 0;
    virtual FramebufferHandle createFramebuffer(const FramebufferDesc& desc) = 0;

    virtual void destroy(BufferHandle h) = 0;
    virtual void destroy(TextureHandle h) = 0;
    virtual void destroy(ShaderHandle h) = 0;
    virtual void destroy(PipelineHandle h) = 0;
    virtual void destroy(FramebufferHandle h) = 0;

    virtual TextureInfo textureInfo(TextureHandle h) const = 0;
    // Backend object id, e.g. the GL texture name (used to show textures in the editor UI).
    virtual uint64_t nativeTexture(TextureHandle h) const = 0;

    virtual void beginFrame() = 0;
    virtual void endFrame() = 0;
    virtual void beginPass(const PassDesc& pass) = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void setScissor(int x, int y, int w, int h, bool enabled) = 0;
    virtual void applyPipeline(PipelineHandle pipeline) = 0;
    virtual void applyBindings(const Bindings& bindings) = 0;
    virtual void applyUniforms(int slot, const void* data, size_t size) = 0;
    virtual void draw(uint32_t first, uint32_t count, uint32_t instances = 1) = 0;
    virtual void drawIndexed(uint32_t firstIndex, uint32_t count, uint32_t instances = 1) = 0;
    virtual void endPass() = 0;

    // Reads RGBA8 pixels (origin bottom-left). Used for screenshots and mouse picking.
    virtual void readPixels(FramebufferHandle fb, int attachment, int x, int y, int w, int h, void* out) = 0;
    // Copies a color texture to the window, scaled to fit.
    virtual void blitToScreen(FramebufferHandle fb, int srcW, int srcH, int dstW, int dstH) = 0;

    virtual const FrameStats& stats() const = 0;
};

// Creates a device for the current window's graphics context. `loader` is the
// window system's function loader (glfwGetProcAddress for OpenGL).
std::unique_ptr<Device> createDevice(Backend backend, void* loader);

} // namespace aven::rhi
