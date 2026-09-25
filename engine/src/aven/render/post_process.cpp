#include "aven/render/post_process.h"

#include "aven/core/log.h"

#include <algorithm>
#include <cmath>

namespace aven {

namespace {

const char* kFullscreenVS = R"(
out vec2 v_uv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    v_uv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// 13-tap downsample (as in Call of Duty: Advanced Warfare); the first pass also
// applies the brightness threshold with a soft knee.
const char* kDownFS = R"(
layout(std140) uniform Params { vec4 u_texel_threshold; };
uniform sampler2D u_source;
in vec2 v_uv;
out vec4 frag_color;
vec3 prefilter(vec3 c) {
    float threshold = u_texel_threshold.z;
    if (threshold <= 0.0) return c;
    float brightness = max(c.r, max(c.g, c.b));
    float knee = threshold * 0.5;
    float soft = clamp(brightness - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-4);
    float contribution = max(soft, brightness - threshold) / max(brightness, 1e-4);
    return c * contribution;
}
void main() {
    vec2 t = u_texel_threshold.xy;
    vec3 a = texture(u_source, v_uv + t * vec2(-2, 2)).rgb;
    vec3 b = texture(u_source, v_uv + t * vec2(0, 2)).rgb;
    vec3 c = texture(u_source, v_uv + t * vec2(2, 2)).rgb;
    vec3 d = texture(u_source, v_uv + t * vec2(-2, 0)).rgb;
    vec3 e = texture(u_source, v_uv).rgb;
    vec3 f = texture(u_source, v_uv + t * vec2(2, 0)).rgb;
    vec3 g = texture(u_source, v_uv + t * vec2(-2, -2)).rgb;
    vec3 h = texture(u_source, v_uv + t * vec2(0, -2)).rgb;
    vec3 i = texture(u_source, v_uv + t * vec2(2, -2)).rgb;
    vec3 j = texture(u_source, v_uv + t * vec2(-1, 1)).rgb;
    vec3 k = texture(u_source, v_uv + t * vec2(1, 1)).rgb;
    vec3 l = texture(u_source, v_uv + t * vec2(-1, -1)).rgb;
    vec3 m = texture(u_source, v_uv + t * vec2(1, -1)).rgb;
    vec3 result = e * 0.125 + (a + c + g + i) * 0.03125 + (b + d + f + h) * 0.0625 + (j + k + l + m) * 0.125;
    result = min(result, vec3(64.0));
    frag_color = vec4(prefilter(result), 1.0);
}
)";

// 9-tap tent filter; blended additively onto the next larger level.
const char* kUpFS = R"(
layout(std140) uniform Params { vec4 u_texel_radius; };
uniform sampler2D u_source;
in vec2 v_uv;
out vec4 frag_color;
void main() {
    vec2 t = u_texel_radius.xy * u_texel_radius.z;
    vec3 s = texture(u_source, v_uv + vec2(-t.x, t.y)).rgb;
    s += texture(u_source, v_uv + vec2(0, t.y)).rgb * 2.0;
    s += texture(u_source, v_uv + vec2(t.x, t.y)).rgb;
    s += texture(u_source, v_uv + vec2(-t.x, 0)).rgb * 2.0;
    s += texture(u_source, v_uv).rgb * 4.0;
    s += texture(u_source, v_uv + vec2(t.x, 0)).rgb * 2.0;
    s += texture(u_source, v_uv + vec2(-t.x, -t.y)).rgb;
    s += texture(u_source, v_uv + vec2(0, -t.y)).rgb * 2.0;
    s += texture(u_source, v_uv + vec2(t.x, -t.y)).rgb;
    frag_color = vec4(s / 16.0, 1.0);
}
)";

const char* kCompositeFS = R"(
layout(std140) uniform Params {
    vec4 u_grade;   // exposure, tonemapper, contrast, saturation
    vec4 u_tint;
    vec4 u_extra;   // vignette intensity, bloom intensity, dither, 0
};
uniform sampler2D u_scene;
uniform sampler2D u_bloom;
in vec2 v_uv;
out vec4 frag_color;
vec3 aces(vec3 x) {
    // Narkowicz 2015 ACES filmic curve.
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}
void main() {
    vec3 c = texture(u_scene, v_uv).rgb;
    c += texture(u_bloom, v_uv).rgb * u_extra.y;
    c *= u_grade.x;
    if (u_grade.y > 1.5) c = c / (1.0 + c);
    else if (u_grade.y > 0.5) c = aces(c);
    c = clamp(c, 0.0, 1.0);
    c = pow(c, vec3(1.0 / 2.2));
    c = (c - 0.5) * u_grade.z + 0.5;
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, u_grade.w);
    c *= u_tint.rgb;
    vec2 d = v_uv - 0.5;
    c *= 1.0 - u_extra.x * smoothstep(0.25, 0.85, dot(d, d) * 2.0);
    // Tiny dither hides banding in smooth gradients (skies).
    float noise = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    c += (noise - 0.5) / 255.0 * u_extra.z;
    frag_color = vec4(clamp(c, 0.0, 1.0), 1.0);
}
)";

// FXAA 3.11-style edge smoothing (compact PC quality version).
const char* kFxaaFS = R"(
layout(std140) uniform Params { vec4 u_texel; };
uniform sampler2D u_source;
in vec2 v_uv;
out vec4 frag_color;
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec2 t = u_texel.xy;
    vec3 rgbNW = texture(u_source, v_uv + vec2(-1.0, -1.0) * t).rgb;
    vec3 rgbNE = texture(u_source, v_uv + vec2(1.0, -1.0) * t).rgb;
    vec3 rgbSW = texture(u_source, v_uv + vec2(-1.0, 1.0) * t).rgb;
    vec3 rgbSE = texture(u_source, v_uv + vec2(1.0, 1.0) * t).rgb;
    vec3 rgbM = texture(u_source, v_uv).rgb;
    float lNW = luma(rgbNW), lNE = luma(rgbNE), lSW = luma(rgbSW), lSE = luma(rgbSE), lM = luma(rgbM);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, vec2(-8.0), vec2(8.0)) * t;
    vec3 a = 0.5 * (texture(u_source, v_uv + dir * (1.0 / 3.0 - 0.5)).rgb +
                    texture(u_source, v_uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture(u_source, v_uv + dir * -0.5).rgb + texture(u_source, v_uv + dir * 0.5).rgb);
    float lB = luma(b);
    frag_color = vec4((lB < lMin || lB > lMax) ? a : b, 1.0);
}
)";

const char* kSsaoFS = R"(
layout(std140) uniform Params {
    mat4 u_projection;
    mat4 u_inv_projection;
    vec4 u_settings; // radius, intensity, texel x, texel y
};
uniform sampler2D u_depth;
uniform sampler2D u_normals;
in vec2 v_uv;
out vec4 frag_color;
vec3 viewPosition(vec2 uv) {
    float z = texture(u_depth, uv).r * 2.0 - 1.0;
    vec4 p = u_inv_projection * vec4(uv * 2.0 - 1.0, z, 1.0);
    return p.xyz / p.w;
}
float hash(vec2 p) { return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453); }
void main() {
    float depth = texture(u_depth, v_uv).r;
    if (depth >= 1.0) { frag_color = vec4(1.0); return; }
    vec3 pos = viewPosition(v_uv);
    vec3 n = normalize(texture(u_normals, v_uv).xyz * 2.0 - 1.0);
    float angle = hash(gl_FragCoord.xy) * 6.2831853;
    vec3 rnd = vec3(cos(angle), sin(angle), 0.0);
    vec3 tangent = normalize(rnd - n * dot(rnd, n));
    vec3 bitangent = cross(n, tangent);
    mat3 tbn = mat3(tangent, bitangent, n);
    float radius = u_settings.x;
    float occlusion = 0.0;
    const int SAMPLES = 16;
    for (int i = 0; i < SAMPLES; ++i) {
        float fi = float(i);
        float r1 = hash(vec2(fi * 7.13, fi * 1.71 + 3.3));
        float r2 = hash(vec2(fi * 3.77 + 1.1, fi * 9.31));
        float r3 = hash(vec2(fi * 5.51 + 2.2, fi * 2.19 + 7.7));
        vec3 s = normalize(vec3(r1 * 2.0 - 1.0, r2 * 2.0 - 1.0, r3 * 0.9 + 0.1));
        float scale = (fi + 1.0) / float(SAMPLES);
        s *= mix(0.1, 1.0, scale * scale);
        vec3 samplePos = pos + tbn * s * radius;
        vec4 offset = u_projection * vec4(samplePos, 1.0);
        vec2 suv = offset.xy / offset.w * 0.5 + 0.5;
        if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) continue;
        float sceneZ = viewPosition(suv).z;
        float range = smoothstep(0.0, 1.0, radius / abs(pos.z - sceneZ));
        occlusion += (sceneZ >= samplePos.z + 0.02 ? 1.0 : 0.0) * range;
    }
    float ao = 1.0 - occlusion / float(SAMPLES);
    frag_color = vec4(vec3(pow(clamp(ao, 0.0, 1.0), u_settings.y)), 1.0);
}
)";

const char* kBlurFS = R"(
layout(std140) uniform Params { vec4 u_texel; };
uniform sampler2D u_source;
in vec2 v_uv;
out vec4 frag_color;
void main() {
    float sum = 0.0;
    for (int x = -2; x < 2; ++x)
        for (int y = -2; y < 2; ++y)
            sum += texture(u_source, v_uv + (vec2(x, y) + 0.5) * u_texel.xy).r;
    frag_color = vec4(vec3(sum / 16.0), 1.0);
}
)";

} // namespace

bool PostProcessor::init(rhi::Device* device) {
    device_ = device;
    auto shader = [&](const char* fs, std::vector<std::string> textures, const char* label) {
        rhi::ShaderDesc d;
        d.vertex = kFullscreenVS;
        d.fragment = fs;
        d.uniformBlocks = {"Params"};
        d.textures = std::move(textures);
        d.label = label;
        return device_->createShader(d);
    };
    downShader_ = shader(kDownFS, {"u_source"}, "bloom down");
    upShader_ = shader(kUpFS, {"u_source"}, "bloom up");
    compositeShader_ = shader(kCompositeFS, {"u_scene", "u_bloom"}, "composite");
    fxaaShader_ = shader(kFxaaFS, {"u_source"}, "fxaa");
    ssaoShader_ = shader(kSsaoFS, {"u_depth", "u_normals"}, "ssao");
    blurShader_ = shader(kBlurFS, {"u_source"}, "ao blur");
    if (!downShader_ || !upShader_ || !compositeShader_ || !fxaaShader_ || !ssaoShader_ || !blurShader_)
        return false;
    auto pipe = [&](rhi::ShaderHandle s, rhi::BlendMode blend) {
        rhi::PipelineDesc p;
        p.shader = s;
        p.blend = blend;
        return device_->createPipeline(p);
    };
    downPipe_ = pipe(downShader_, rhi::BlendMode::Opaque);
    upPipe_ = pipe(upShader_, rhi::BlendMode::Additive);
    compositePipe_ = pipe(compositeShader_, rhi::BlendMode::Opaque);
    fxaaPipe_ = pipe(fxaaShader_, rhi::BlendMode::Opaque);
    ssaoPipe_ = pipe(ssaoShader_, rhi::BlendMode::Opaque);
    blurPipe_ = pipe(blurShader_, rhi::BlendMode::Opaque);
    multiplyPipe_ = pipe(blurShader_, rhi::BlendMode::Multiply);

    float px[4] = {0, 0, 0, 0};
    rhi::TextureDesc td;
    td.format = rhi::PixelFormat::RGBA16F;
    td.data = px;
    td.label = "black";
    black_ = device_->createTexture(td);
    return true;
}

void PostProcessor::shutdown() {
    if (!device_)
        return;
    resize(0, 0);
    for (auto p : {downPipe_, upPipe_, compositePipe_, fxaaPipe_, ssaoPipe_, blurPipe_, multiplyPipe_})
        device_->destroy(p);
    for (auto s : {downShader_, upShader_, compositeShader_, fxaaShader_, ssaoShader_, blurShader_})
        device_->destroy(s);
    device_->destroy(black_);
    device_ = nullptr;
}

PostProcessor::Level PostProcessor::makeLevel(int w, int h, rhi::PixelFormat format, const char* label) {
    Level l;
    l.w = std::max(1, w);
    l.h = std::max(1, h);
    rhi::TextureDesc td;
    td.width = l.w;
    td.height = l.h;
    td.format = format;
    td.renderTarget = true;
    td.label = label;
    l.texture = device_->createTexture(td);
    rhi::FramebufferDesc fd;
    fd.colors = {l.texture};
    fd.label = label;
    l.fb = device_->createFramebuffer(fd);
    return l;
}

void PostProcessor::destroyLevel(Level& l) {
    if (l.fb)
        device_->destroy(l.fb);
    if (l.texture)
        device_->destroy(l.texture);
    l = {};
}

void PostProcessor::resize(int w, int h) {
    if (w == width_ && h == height_)
        return;
    for (auto& l : bloom_)
        destroyLevel(l);
    bloom_.clear();
    destroyLevel(ldr_);
    destroyLevel(ao_);
    destroyLevel(aoBlur_);
    width_ = w;
    height_ = h;
    if (w <= 0 || h <= 0)
        return;
    int bw = w / 2, bh = h / 2;
    for (int i = 0; i < 6 && bw >= 2 && bh >= 2; ++i) {
        bloom_.push_back(makeLevel(bw, bh, rhi::PixelFormat::RGBA16F, "bloom"));
        bw /= 2;
        bh /= 2;
    }
    ldr_ = makeLevel(w, h, rhi::PixelFormat::RGBA8, "ldr");
    ao_ = makeLevel(w / 2, h / 2, rhi::PixelFormat::RGBA8, "ssao");
    aoBlur_ = makeLevel(w / 2, h / 2, rhi::PixelFormat::RGBA8, "ssao blur");
}

void PostProcessor::fullscreen(rhi::PipelineHandle pipe, rhi::FramebufferHandle fb, int w, int h,
                               std::initializer_list<rhi::TextureHandle> textures, const void* uniforms, size_t size,
                               bool clear) {
    rhi::PassDesc pass;
    pass.framebuffer = fb;
    pass.width = w;
    pass.height = h;
    pass.clearColor = clear;
    pass.clearDepth = false;
    device_->beginPass(pass);
    device_->applyPipeline(pipe);
    rhi::Bindings b;
    int i = 0;
    for (auto t : textures)
        b.textures[i++] = t;
    device_->applyBindings(b);
    if (uniforms)
        device_->applyUniforms(0, uniforms, size);
    device_->draw(0, 3);
    device_->endPass();
}

void PostProcessor::applySSAO(rhi::FramebufferHandle sceneFb, rhi::TextureHandle depth, rhi::TextureHandle normals,
                              const Mat4& projection, const PostProcessing& s) {
    if (!ao_.fb)
        return;
    struct {
        Mat4 projection, inverseProjection;
        float settings[4];
    } u{projection, inverse(projection), {s.ssaoRadius, s.ssaoIntensity, 0, 0}};
    fullscreen(ssaoPipe_, ao_.fb, ao_.w, ao_.h, {depth, normals}, &u, sizeof u);
    float texel[4] = {1.0f / ao_.w, 1.0f / ao_.h, 0, 0};
    fullscreen(blurPipe_, aoBlur_.fb, aoBlur_.w, aoBlur_.h, {ao_.texture}, texel, sizeof texel);
    // Multiply the blurred occlusion into the scene color.
    fullscreen(multiplyPipe_, sceneFb, width_, height_, {aoBlur_.texture}, texel, sizeof texel, false);
}

void PostProcessor::composite(rhi::TextureHandle sceneColor, rhi::FramebufferHandle output,
                              const PostProcessing* settings) {
    PostProcessing defaults;
    defaults.tonemapper = Tonemapper::None;
    defaults.bloom = false;
    defaults.vignette = false;
    defaults.fxaa = false;
    const PostProcessing& s = settings ? *settings : defaults;

    rhi::TextureHandle bloomTexture = black_;
    if (s.bloom && s.bloomIntensity > 0 && !bloom_.empty()) {
        rhi::TextureHandle source = sceneColor;
        int sw = width_, sh = height_;
        for (size_t i = 0; i < bloom_.size(); ++i) {
            float p[4] = {1.0f / sw, 1.0f / sh, i == 0 ? s.bloomThreshold : 0.0f, 0};
            fullscreen(downPipe_, bloom_[i].fb, bloom_[i].w, bloom_[i].h, {source}, p, sizeof p);
            source = bloom_[i].texture;
            sw = bloom_[i].w;
            sh = bloom_[i].h;
        }
        for (size_t i = bloom_.size() - 1; i > 0; --i) {
            float p[4] = {1.0f / bloom_[i].w, 1.0f / bloom_[i].h, 1.0f, 0};
            fullscreen(upPipe_, bloom_[i - 1].fb, bloom_[i - 1].w, bloom_[i - 1].h, {bloom_[i].texture}, p, sizeof p,
                       false);
        }
        bloomTexture = bloom_[0].texture;
    }

    struct {
        float grade[4];
        float tint[4];
        float extra[4];
    } u{{s.exposure, static_cast<float>(s.tonemapper), s.contrast, s.saturation},
        {s.tint.r, s.tint.g, s.tint.b, 1},
        {s.vignette ? s.vignetteIntensity : 0.0f, s.bloom ? s.bloomIntensity * 0.25f : 0.0f, settings ? 1.0f : 0.0f, 0}};

    if (s.fxaa && ldr_.fb) {
        fullscreen(compositePipe_, ldr_.fb, width_, height_, {sceneColor, bloomTexture}, &u, sizeof u);
        float texel[4] = {1.0f / width_, 1.0f / height_, 0, 0};
        fullscreen(fxaaPipe_, output, width_, height_, {ldr_.texture}, texel, sizeof texel);
    } else {
        fullscreen(compositePipe_, output, width_, height_, {sceneColor, bloomTexture}, &u, sizeof u);
    }
}

} // namespace aven
