// OpenGL 3.3 core backend for the RHI. The same code runs on WebGL 2 (OpenGL ES 3.0) in browsers.

#include "aven/core/log.h"
#include "aven/render/rhi.h"

#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <emscripten/html5.h>
#define AVEN_WEBGL 1
#else
#include <glad/gl.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <vector>

namespace aven::rhi {

const char* backendName(Backend b) {
    switch (b) {
    case Backend::OpenGL: return "OpenGL";
    case Backend::Vulkan: return "Vulkan";
    case Backend::Metal: return "Metal";
    case Backend::D3D12: return "Direct3D 12";
    }
    return "?";
}

namespace {

template <class T> class Pool {
public:
    uint32_t add(T item) {
        if (!free_.empty()) {
            uint32_t i = free_.back();
            free_.pop_back();
            items_[i] = std::move(item);
            alive_[i] = true;
            return i + 1;
        }
        items_.push_back(std::move(item));
        alive_.push_back(true);
        return static_cast<uint32_t>(items_.size());
    }
    T* get(uint32_t id) {
        if (id == 0 || id > items_.size() || !alive_[id - 1])
            return nullptr;
        return &items_[id - 1];
    }
    const T* get(uint32_t id) const {
        if (id == 0 || id > items_.size() || !alive_[id - 1])
            return nullptr;
        return &items_[id - 1];
    }
    void remove(uint32_t id) {
        if (!get(id))
            return;
        alive_[id - 1] = false;
        free_.push_back(id - 1);
    }

private:
    std::deque<T> items_; // deque keeps pointers stable as resources are added
    std::vector<bool> alive_;
    std::vector<uint32_t> free_;
};

struct GLBuffer {
    GLuint id = 0;
    GLenum target = GL_ARRAY_BUFFER;
    Usage usage = Usage::Immutable;
    size_t size = 0;
};

struct GLTexture {
    GLuint id = 0;
    TextureDesc desc;
};

struct GLShader {
    GLuint program = 0;
    int outputs = 1; // color outputs the fragment shader writes
};

struct GLPipeline {
    PipelineDesc desc;
    GLuint program = 0;
    int outputs = 1;
};

struct GLFramebuffer {
    GLuint id = 0;
    int colorCount = 0;
    int width = 0;
    int height = 0;
};

struct FormatInfo {
    GLenum internal, format, type;
};

FormatInfo formatInfo(PixelFormat f) {
    switch (f) {
    case PixelFormat::RGBA8: return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
    case PixelFormat::RGBA16F: return {GL_RGBA16F, GL_RGBA, GL_FLOAT};
    case PixelFormat::RG16F: return {GL_RG16F, GL_RG, GL_FLOAT};
    case PixelFormat::R8: return {GL_R8, GL_RED, GL_UNSIGNED_BYTE};
    case PixelFormat::R16F: return {GL_R16F, GL_RED, GL_FLOAT};
    case PixelFormat::R32F: return {GL_R32F, GL_RED, GL_FLOAT};
    case PixelFormat::Depth24: return {GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT};
    case PixelFormat::Depth32F: return {GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT};
    }
    return {GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE};
}

bool isDepth(PixelFormat f) { return f == PixelFormat::Depth24 || f == PixelFormat::Depth32F; }

#ifdef AVEN_WEBGL
const void* expandR8(const void* data, int w, int h, std::vector<uint8_t>& out) {
    const auto* src = static_cast<const uint8_t*>(data);
    out.resize(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < static_cast<size_t>(w) * h; ++i) {
        out[i * 4 + 0] = out[i * 4 + 1] = out[i * 4 + 2] = 255;
        out[i * 4 + 3] = src[i];
    }
    return out.data();
}
#endif

GLenum compareFunc(CompareFunc c) {
    switch (c) {
    case CompareFunc::Never: return GL_NEVER;
    case CompareFunc::Less: return GL_LESS;
    case CompareFunc::LessEqual: return GL_LEQUAL;
    case CompareFunc::Equal: return GL_EQUAL;
    case CompareFunc::Greater: return GL_GREATER;
    case CompareFunc::GreaterEqual: return GL_GEQUAL;
    case CompareFunc::NotEqual: return GL_NOTEQUAL;
    case CompareFunc::Always: return GL_ALWAYS;
    }
    return GL_LEQUAL;
}

#ifndef AVEN_WEBGL
void GLAPIENTRY debugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                              const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION)
        return;
    if (type == GL_DEBUG_TYPE_ERROR)
        Log::error("OpenGL: ", message);
    else if (severity == GL_DEBUG_SEVERITY_HIGH)
        Log::warn("OpenGL: ", message);
}
#endif

class GLDevice final : public Device {
public:
    bool init(void* loader) {
#ifdef AVEN_WEBGL
        (void)loader;
        // Float render targets (HDR, bloom) need this WebGL 2 extension.
        EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context = emscripten_webgl_get_current_context();
        if (!emscripten_webgl_enable_extension(context, "EXT_color_buffer_float"))
            Log::warn("This browser can't draw to float textures; some effects may look different.");
        emscripten_webgl_enable_extension(context, "OES_texture_float_linear");
#else
        int version = gladLoadGL(reinterpret_cast<GLADloadfunc>(loader));
        if (!version) {
            Log::error("Failed to load OpenGL functions.");
            return false;
        }
        if (GLAD_VERSION_MAJOR(version) < 3 || (GLAD_VERSION_MAJOR(version) == 3 && GLAD_VERSION_MINOR(version) < 3)) {
            Log::error("OpenGL 3.3 is required, but only ", GLAD_VERSION_MAJOR(version), ".",
                       GLAD_VERSION_MINOR(version), " is available.");
            return false;
        }
        if (std::getenv("AVEN_GL_DEBUG") && GLAD_GL_KHR_debug) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugCallback, nullptr);
        }
#endif
        glGenVertexArrays(1, &vao_);
        glBindVertexArray(vao_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        GLint align = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &align);
        uboAlign_ = static_cast<size_t>(std::max(align, 16));
        glGenBuffers(1, &ubo_);
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(kUboSize), nullptr, GL_STREAM_DRAW);
#ifndef AVEN_WEBGL
        if (GLAD_GL_EXT_texture_filter_anisotropic)
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAniso_);
#endif
        return true;
    }

    ~GLDevice() override {
        if (ubo_)
            glDeleteBuffers(1, &ubo_);
        if (vao_)
            glDeleteVertexArrays(1, &vao_);
    }

    Backend backend() const override { return Backend::OpenGL; }

    std::string description() const override {
        auto str = [](GLenum e) {
            const GLubyte* s = glGetString(e);
            return s ? std::string(reinterpret_cast<const char*>(s)) : std::string("?");
        };
        return "OpenGL " + str(GL_VERSION) + " on " + str(GL_RENDERER);
    }

    // --- buffers

    BufferHandle createBuffer(const BufferDesc& d) override {
        GLBuffer b;
        b.target = d.type == BufferType::Index ? GL_ELEMENT_ARRAY_BUFFER : GL_ARRAY_BUFFER;
        b.usage = d.usage;
        b.size = d.size;
        glGenBuffers(1, &b.id);
        glBindBuffer(b.target, b.id);
        glBufferData(b.target, static_cast<GLsizeiptr>(d.size), d.data, glUsage(d.usage));
        return {buffers_.add(b)};
    }

    static GLenum glUsage(Usage u) {
        switch (u) {
        case Usage::Immutable: return GL_STATIC_DRAW;
        case Usage::Dynamic: return GL_DYNAMIC_DRAW;
        case Usage::Stream: return GL_STREAM_DRAW;
        }
        return GL_STATIC_DRAW;
    }

    void updateBuffer(BufferHandle h, const void* data, size_t size) override {
        GLBuffer* b = buffers_.get(h.id);
        if (!b)
            return;
        glBindBuffer(b->target, b->id);
        if (size > b->size || b->usage == Usage::Stream) {
            // Orphan and reallocate so the driver doesn't wait for draws still using the old data.
            b->size = std::max(size, b->size);
            glBufferData(b->target, static_cast<GLsizeiptr>(b->size), nullptr, glUsage(b->usage));
        }
        glBufferSubData(b->target, 0, static_cast<GLsizeiptr>(size), data);
    }

    // --- textures

    TextureHandle createTexture(const TextureDesc& d) override {
        GLTexture t;
        t.desc = d;
        t.desc.data = nullptr;
        glGenTextures(1, &t.id);
        glBindTexture(GL_TEXTURE_2D, t.id);
        FormatInfo fi = formatInfo(d.format);
        const void* pixels = d.data;
#ifdef AVEN_WEBGL
        // WebGL 2 has no texture swizzle: single-channel textures become white RGBA with that channel as alpha.
        std::vector<uint8_t> expanded;
        if (d.format == PixelFormat::R8) {
            fi = formatInfo(PixelFormat::RGBA8);
            if (d.data)
                pixels = expandR8(d.data, d.width, d.height, expanded);
        }
#endif
        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<GLint>(fi.internal), d.width, d.height, 0, fi.format, fi.type, pixels);
        GLint wrap = d.wrap == Wrap::Repeat ? GL_REPEAT : d.wrap == Wrap::Mirror ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        bool linear = d.filter == Filter::Linear;
        GLint minFilter = d.mipmaps ? (linear ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR)
                                    : (linear ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
#ifndef AVEN_WEBGL
        if (d.format == PixelFormat::R8) {
            // Single-channel textures read as white with alpha (fonts, masks).
            GLint swizzle[] = {GL_ONE, GL_ONE, GL_ONE, GL_RED};
            glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzle);
        }
#endif
        if (d.depthCompare && isDepth(d.format)) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
#ifdef AVEN_WEBGL
            // WebGL has no border color; the shadow shaders treat outside the map as lit.
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
#else
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            float border[] = {1, 1, 1, 1};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
#endif
        }
        if (d.mipmaps) {
#ifndef AVEN_WEBGL
            if (maxAniso_ > 1 && linear)
                glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::min(8.0f, maxAniso_));
#endif
            if (d.data)
                glGenerateMipmap(GL_TEXTURE_2D);
        }
        return {textures_.add(t)};
    }

    void updateTexture(TextureHandle h, const void* data) override {
        GLTexture* t = textures_.get(h.id);
        if (!t)
            return;
        updateTextureRegion(h, 0, 0, t->desc.width, t->desc.height, data);
        if (t->desc.mipmaps)
            generateMipmaps(h);
    }

    void updateTextureRegion(TextureHandle h, int x, int y, int w, int hgt, const void* data) override {
        GLTexture* t = textures_.get(h.id);
        if (!t)
            return;
        FormatInfo fi = formatInfo(t->desc.format);
#ifdef AVEN_WEBGL
        std::vector<uint8_t> expanded;
        if (t->desc.format == PixelFormat::R8) {
            fi = formatInfo(PixelFormat::RGBA8);
            data = expandR8(data, w, hgt, expanded);
        }
#endif
        glBindTexture(GL_TEXTURE_2D, t->id);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, hgt, fi.format, fi.type, data);
    }

    void generateMipmaps(TextureHandle h) override {
        if (GLTexture* t = textures_.get(h.id)) {
            glBindTexture(GL_TEXTURE_2D, t->id);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
    }

    TextureInfo textureInfo(TextureHandle h) const override {
        const GLTexture* t = textures_.get(h.id);
        return t ? TextureInfo{t->desc.width, t->desc.height, t->desc.format} : TextureInfo{};
    }

    uint64_t nativeTexture(TextureHandle h) const override {
        const GLTexture* t = textures_.get(h.id);
        return t ? t->id : 0;
    }

    // --- shaders

    static bool compileStage(GLuint shader, const std::string& src, std::string& log) {
        const char* s = src.c_str();
        glShaderSource(shader, 1, &s, nullptr);
        glCompileShader(shader);
        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char buf[4096];
            glGetShaderInfoLog(shader, sizeof buf, nullptr, buf);
            log = buf;
        }
        return ok;
    }

    ShaderHandle createShader(const ShaderDesc& d, std::string* error) override {
#ifdef AVEN_WEBGL
        // GLSL ES 3.00 is GLSL 3.30 with precision qualifiers.
        std::string header = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                             "precision highp sampler2DShadow;\nprecision highp samplerCube;\n#define AVEN_WEBGL 1\n";
#else
        std::string header = "#version 330 core\n";
#endif
        GLuint vs = glCreateShader(GL_VERTEX_SHADER), fs = glCreateShader(GL_FRAGMENT_SHADER);
        std::string log;
        auto fail = [&](const std::string& what) {
            std::string msg = std::string("Shader '") + (d.label ? d.label : "?") + "' " + what + ":\n" + log;
            if (error)
                *error = msg;
            else
                Log::error(msg);
            glDeleteShader(vs);
            glDeleteShader(fs);
            return ShaderHandle{};
        };
        if (!compileStage(vs, header + d.vertex, log))
            return fail("vertex stage failed to compile");
#ifdef AVEN_WEBGL
        // GLSL ES has no glBindFragDataLocation: give each output its location in the source.
        std::string fragment;
        {
            std::vector<std::string> outputs = d.outputs.empty() ? std::vector<std::string>{"frag_color"} : d.outputs;
            size_t start = 0;
            while (start <= d.fragment.size()) {
                size_t end = d.fragment.find('\n', start);
                std::string line = d.fragment.substr(start, end == std::string::npos ? std::string::npos : end - start);
                size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line.compare(first, 4, "out ") == 0)
                    for (size_t i = 0; i < outputs.size(); ++i) {
                        size_t semi = line.find(';');
                        size_t name = line.rfind(outputs[i], semi);
                        if (semi != std::string::npos && name != std::string::npos && name + outputs[i].size() == semi &&
                            (line[name - 1] == ' ' || line[name - 1] == '\t')) {
                            line.insert(first, "layout(location = " + std::to_string(i) + ") ");
                            break;
                        }
                    }
                fragment += line + "\n";
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
        }
        if (!compileStage(fs, header + fragment, log))
            return fail("fragment stage failed to compile");
#else
        if (!compileStage(fs, header + d.fragment, log))
            return fail("fragment stage failed to compile");
#endif
        GLuint program = glCreateProgram();
        glAttachShader(program, vs);
        glAttachShader(program, fs);
        for (size_t i = 0; i < d.attributes.size(); ++i)
            glBindAttribLocation(program, static_cast<GLuint>(i), d.attributes[i].c_str());
#ifndef AVEN_WEBGL
        if (d.outputs.empty()) {
            glBindFragDataLocation(program, 0, "frag_color");
        } else {
            for (size_t i = 0; i < d.outputs.size(); ++i)
                glBindFragDataLocation(program, static_cast<GLuint>(i), d.outputs[i].c_str());
        }
#endif
        glLinkProgram(program);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char buf[4096];
            glGetProgramInfoLog(program, sizeof buf, nullptr, buf);
            log = buf;
            glDeleteProgram(program);
            if (error)
                *error = std::string("Shader '") + (d.label ? d.label : "?") + "' failed to link:\n" + log;
            else
                Log::error("Shader '", d.label ? d.label : "?", "' failed to link:\n", log);
            return {};
        }
        glUseProgram(program);
        for (size_t i = 0; i < d.uniformBlocks.size(); ++i) {
            GLuint index = glGetUniformBlockIndex(program, d.uniformBlocks[i].c_str());
            if (index != GL_INVALID_INDEX)
                glUniformBlockBinding(program, index, static_cast<GLuint>(i));
        }
        for (size_t i = 0; i < d.textures.size(); ++i) {
            GLint loc = glGetUniformLocation(program, d.textures[i].c_str());
            if (loc >= 0)
                glUniform1i(loc, static_cast<GLint>(i));
        }
        currentProgram_ = program;
        return {shaders_.add({program, d.outputs.empty() ? 1 : static_cast<int>(d.outputs.size())})};
    }

    PipelineHandle createPipeline(const PipelineDesc& d) override {
        GLShader* s = shaders_.get(d.shader.id);
        if (!s)
            return {};
        return {pipelines_.add({d, s->program, s->outputs})};
    }

    FramebufferHandle createFramebuffer(const FramebufferDesc& d) override {
        GLFramebuffer f;
        glGenFramebuffers(1, &f.id);
        glBindFramebuffer(GL_FRAMEBUFFER, f.id);
        std::vector<GLenum> drawBuffers;
        for (size_t i = 0; i < d.colors.size(); ++i) {
            GLTexture* t = textures_.get(d.colors[i].id);
            if (!t)
                continue;
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i), GL_TEXTURE_2D, t->id, 0);
            drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i));
            f.width = t->desc.width;
            f.height = t->desc.height;
        }
        if (GLTexture* t = textures_.get(d.depth.id)) {
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, t->id, 0);
            f.width = t->desc.width;
            f.height = t->desc.height;
        }
        if (drawBuffers.empty()) {
            GLenum none = GL_NONE;
            glDrawBuffers(1, &none);
            glReadBuffer(GL_NONE);
        } else {
            glDrawBuffers(static_cast<GLsizei>(drawBuffers.size()), drawBuffers.data());
        }
        f.colorCount = static_cast<int>(drawBuffers.size());
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            Log::error("Framebuffer '", d.label ? d.label : "?", "' is incomplete (status 0x", std::hex, status, ")");
            glDeleteFramebuffers(1, &f.id);
            return {};
        }
        return {framebuffers_.add(f)};
    }

    void destroy(BufferHandle h) override {
        if (GLBuffer* b = buffers_.get(h.id)) {
            glDeleteBuffers(1, &b->id);
            buffers_.remove(h.id);
        }
    }
    void destroy(TextureHandle h) override {
        if (GLTexture* t = textures_.get(h.id)) {
            glDeleteTextures(1, &t->id);
            textures_.remove(h.id);
        }
    }
    void destroy(ShaderHandle h) override {
        if (GLShader* s = shaders_.get(h.id)) {
            glDeleteProgram(s->program);
            shaders_.remove(h.id);
        }
    }
    void destroy(PipelineHandle h) override { pipelines_.remove(h.id); }
    void destroy(FramebufferHandle h) override {
        if (GLFramebuffer* f = framebuffers_.get(h.id)) {
            glDeleteFramebuffers(1, &f->id);
            framebuffers_.remove(h.id);
        }
    }

    // --- frame and passes

    void beginFrame() override {
        stats_ = {};
        // Other code (like the editor's UI) may have changed GL state since last frame.
        currentProgram_ = 0;
        glBindVertexArray(vao_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    }
    void endFrame() override {}

    void beginPass(const PassDesc& p) override {
        GLFramebuffer* f = framebuffers_.get(p.framebuffer.id);
        glBindFramebuffer(GL_FRAMEBUFFER, f ? f->id : 0);
#ifdef AVEN_WEBGL
        passColors_ = f ? f->colorCount : 1;
        activeDrawBuffers_ = -1; // each framebuffer keeps its own setting
        setDrawBuffers(passColors_); // all targets, so a clear reaches every one
#endif
        glViewport(0, 0, p.width, p.height);
        glDisable(GL_SCISSOR_TEST);
        GLbitfield mask = 0;
        if (p.clearColor) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(p.clearValue.r, p.clearValue.g, p.clearValue.b, p.clearValue.a);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (p.clearDepth) {
            glDepthMask(GL_TRUE);
#ifdef AVEN_WEBGL
            glClearDepthf(p.depthValue);
#else
            glClearDepth(p.depthValue);
#endif
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask)
            glClear(mask);
        ++stats_.passes;
    }

    void setViewport(int x, int y, int w, int h) override { glViewport(x, y, w, h); }

    void setScissor(int x, int y, int w, int h, bool enabled) override {
        if (enabled) {
            glEnable(GL_SCISSOR_TEST);
            glScissor(x, y, w, h);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }
    }

    void applyPipeline(PipelineHandle h) override {
        GLPipeline* p = pipelines_.get(h.id);
        current_ = p;
        if (!p)
            return;
#ifdef AVEN_WEBGL
        // WebGL refuses to draw when a target has no matching shader output (desktop GL allows it).
        if (passColors_ > 1)
            setDrawBuffers(std::min(p->outputs, passColors_));
#endif
        const PipelineDesc& d = p->desc;
        if (currentProgram_ != p->program) {
            glUseProgram(p->program);
            currentProgram_ = p->program;
        }
        if (d.cull == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT);
        }
        if (d.depthTest) {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(compareFunc(d.depthCompare));
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
        GLboolean cw = d.colorWrite ? GL_TRUE : GL_FALSE;
        glColorMask(cw, cw, cw, cw);
        if (d.depthBias != 0 || d.slopeBias != 0) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(d.slopeBias, d.depthBias);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        switch (d.blend) {
        case BlendMode::Opaque: glDisable(GL_BLEND); break;
        case BlendMode::Alpha:
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Additive:
            glEnable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            break;
        case BlendMode::Premultiplied:
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            break;
        case BlendMode::Multiply:
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO);
            break;
        }
    }

    void applyBindings(const Bindings& b) override {
        if (!current_)
            return;
        GLBuffer* vb = buffers_.get(b.vertexBuffer.id);
        const VertexLayout& layout = current_->desc.layout;
        if (vb) {
            glBindBuffer(GL_ARRAY_BUFFER, vb->id);
            for (size_t i = 0; i < layout.attributes.size(); ++i) {
                const auto& a = layout.attributes[i];
                GLint comps = 1;
                GLenum type = GL_FLOAT;
                GLboolean norm = GL_FALSE;
                switch (a.format) {
                case VertexFormat::Float: comps = 1; break;
                case VertexFormat::Float2: comps = 2; break;
                case VertexFormat::Float3: comps = 3; break;
                case VertexFormat::Float4: comps = 4; break;
                case VertexFormat::UByte4N:
                    comps = 4;
                    type = GL_UNSIGNED_BYTE;
                    norm = GL_TRUE;
                    break;
                }
                glEnableVertexAttribArray(static_cast<GLuint>(i));
                glVertexAttribPointer(static_cast<GLuint>(i), comps, type, norm, static_cast<GLsizei>(layout.stride),
                                      reinterpret_cast<const void*>(static_cast<uintptr_t>(a.offset + b.vertexOffset)));
            }
        }
        for (size_t i = layout.attributes.size(); i < enabledAttribs_; ++i)
            glDisableVertexAttribArray(static_cast<GLuint>(i));
        enabledAttribs_ = layout.attributes.size();
        if (GLBuffer* ib = buffers_.get(b.indexBuffer.id))
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ib->id);
        for (int i = 0; i < kMaxTextureSlots; ++i) {
            GLTexture* t = textures_.get(b.textures[i].id);
            if (!t)
                continue;
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(i));
            glBindTexture(GL_TEXTURE_2D, t->id);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    void writeUniforms(int slot, const void* data, size_t size) {
        size_t aligned = (size + uboAlign_ - 1) / uboAlign_ * uboAlign_;
        glBufferSubData(GL_UNIFORM_BUFFER, static_cast<GLintptr>(uboOffset_), static_cast<GLsizeiptr>(size), data);
        glBindBufferRange(GL_UNIFORM_BUFFER, static_cast<GLuint>(slot), ubo_, static_cast<GLintptr>(uboOffset_),
                          static_cast<GLsizeiptr>(size));
        uboOffset_ += aligned;
    }

    void applyUniforms(int slot, const void* data, size_t size) override {
        size_t aligned = (size + uboAlign_ - 1) / uboAlign_ * uboAlign_;
        glBindBuffer(GL_UNIFORM_BUFFER, ubo_);
        if (uboOffset_ + aligned > kUboSize) {
            // Orphan the buffer; bindings of other slots now point at fresh storage, so
            // re-upload what they last held to keep them valid.
            glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(kUboSize), nullptr, GL_STREAM_DRAW);
            uboOffset_ = 0;
            for (int i = 0; i < kMaxUniformSlots; ++i)
                if (i != slot && !lastUniforms_[i].empty())
                    writeUniforms(i, lastUniforms_[i].data(), lastUniforms_[i].size());
        }
        if (slot >= 0 && slot < kMaxUniformSlots)
            lastUniforms_[slot].assign(static_cast<const uint8_t*>(data), static_cast<const uint8_t*>(data) + size);
        writeUniforms(slot, data, size);
    }

    static GLenum primitive(Primitive p) {
        switch (p) {
        case Primitive::Triangles: return GL_TRIANGLES;
        case Primitive::TriangleStrip: return GL_TRIANGLE_STRIP;
        case Primitive::Lines: return GL_LINES;
        case Primitive::LineStrip: return GL_LINE_STRIP;
        case Primitive::Points: return GL_POINTS;
        }
        return GL_TRIANGLES;
    }

    void countDraw(uint32_t count, uint32_t instances) {
        ++stats_.drawCalls;
        if (current_ && current_->desc.primitive == Primitive::Triangles)
            stats_.triangles += static_cast<uint64_t>(count / 3) * instances;
    }

    void draw(uint32_t first, uint32_t count, uint32_t instances) override {
        if (!current_ || count == 0)
            return;
        GLenum mode = primitive(current_->desc.primitive);
        if (instances > 1)
            glDrawArraysInstanced(mode, static_cast<GLint>(first), static_cast<GLsizei>(count), static_cast<GLsizei>(instances));
        else
            glDrawArrays(mode, static_cast<GLint>(first), static_cast<GLsizei>(count));
        countDraw(count, instances);
    }

    void drawIndexed(uint32_t firstIndex, uint32_t count, uint32_t instances) override {
        if (!current_ || count == 0)
            return;
        GLenum mode = primitive(current_->desc.primitive);
        bool wide = current_->desc.indexType == IndexType::UInt32;
        GLenum type = wide ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT;
        const void* offset = reinterpret_cast<const void*>(static_cast<uintptr_t>(firstIndex * (wide ? 4u : 2u)));
        if (instances > 1)
            glDrawElementsInstanced(mode, static_cast<GLsizei>(count), type, offset, static_cast<GLsizei>(instances));
        else
            glDrawElements(mode, static_cast<GLsizei>(count), type, offset);
        countDraw(count, instances);
    }

    void endPass() override {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        current_ = nullptr;
    }

    void readPixels(FramebufferHandle fbh, int attachment, int x, int y, int w, int h, void* out) override {
        GLFramebuffer* f = framebuffers_.get(fbh.id);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, f ? f->id : 0);
        if (f)
            glReadBuffer(GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(attachment));
        else
            glReadBuffer(GL_BACK);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    void blitToScreen(FramebufferHandle fbh, int srcW, int srcH, int dstW, int dstH) override {
        GLFramebuffer* f = framebuffers_.get(fbh.id);
        if (!f)
            return;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, f->id);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glBlitFramebuffer(0, 0, srcW, srcH, 0, 0, dstW, dstH, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    const FrameStats& stats() const override { return stats_; }

private:
    static constexpr size_t kUboSize = 4 * 1024 * 1024;

    Pool<GLBuffer> buffers_;
    Pool<GLTexture> textures_;
    Pool<GLShader> shaders_;
    Pool<GLPipeline> pipelines_;
    Pool<GLFramebuffer> framebuffers_;
    GLPipeline* current_ = nullptr;
    GLuint currentProgram_ = 0;
#ifdef AVEN_WEBGL
    int passColors_ = 1, activeDrawBuffers_ = -1;
    void setDrawBuffers(int active) {
        if (passColors_ <= 1 || active == activeDrawBuffers_)
            return;
        GLenum buffers[8];
        for (int i = 0; i < passColors_ && i < 8; ++i)
            buffers[i] = i < active ? GL_COLOR_ATTACHMENT0 + static_cast<GLenum>(i) : GL_NONE;
        glDrawBuffers(std::min(passColors_, 8), buffers);
        activeDrawBuffers_ = active;
    }
#endif
    GLuint vao_ = 0;
    GLuint ubo_ = 0;
    size_t uboOffset_ = 0;
    std::vector<uint8_t> lastUniforms_[kMaxUniformSlots];
    size_t uboAlign_ = 256;
    size_t enabledAttribs_ = 0;
    float maxAniso_ = 1.0f;
    FrameStats stats_;
};

} // namespace

std::unique_ptr<Device> createDevice(Backend backend, void* loader) {
    if (backend != Backend::OpenGL) {
        Log::error(backendName(backend), " is not available yet in this build; using OpenGL.");
    }
    auto device = std::make_unique<GLDevice>();
    if (!device->init(loader))
        return nullptr;
    return device;
}

} // namespace aven::rhi
