#include "aven/render/scene_renderer.h"

#include "aven/core/log.h"
#include "aven/render/post_process.h"
#include "aven/render/renderer3d.h"
#include "aven/render/ui_layout.h"

#include <algorithm>
#include <cstring>

namespace aven {

// ---------------------------------------------------------------- camera helpers

void CameraView::screenRay(Vec2 pixel, Vec2 size, Vec3& origin, Vec3& direction) const {
    float x = pixel.x / size.x * 2.0f - 1.0f;
    float y = 1.0f - pixel.y / size.y * 2.0f;
    Mat4 inv = inverse(viewProjection);
    Vec3 nearPt = transformPoint(inv, {x, y, -1});
    Vec3 farPt = transformPoint(inv, {x, y, 1});
    origin = nearPt;
    direction = normalize(farPt - nearPt);
}

Vec3 CameraView::screenToWorld(Vec2 pixel, Vec2 size, float depth) const {
    Vec3 o, d;
    screenRay(pixel, size, o, d);
    // Most 2D games live on the z = 0 plane; hit it when the camera faces it.
    if (std::abs(d.z) > 1e-4f) {
        float t = (depth - o.z) / d.z;
        if (t > 0)
            return o + d * t;
    }
    return o + d * (orthographic ? 0.0f : std::max(depth, 1.0f));
}

Vec2 CameraView::worldToScreen(Vec3 world, Vec2 size) const {
    Vec4 clip = viewProjection * Vec4(world, 1.0f);
    if (clip.w == 0)
        return {};
    Vec3 ndc = clip.xyz() / clip.w;
    return {(ndc.x * 0.5f + 0.5f) * size.x, (0.5f - ndc.y * 0.5f) * size.y};
}

CameraView SceneRenderer::makeCamera(Vec3 position, Quat rotation, bool ortho, float sizeOrFov, float aspect,
                                     float nearClip, float farClip) {
    CameraView v;
    v.position = position;
    v.orthographic = ortho;
    v.aspect = aspect;
    v.nearClip = nearClip;
    v.farClip = farClip;
    v.view = inverse(Mat4::trs(position, rotation, {1, 1, 1}));
    v.forward = rotate(rotation, {0, 0, -1});
    if (ortho) {
        v.orthoSize = sizeOrFov;
        float s = sizeOrFov;
        v.projection = Mat4::orthographic(-s * aspect, s * aspect, -s, s, -farClip, farClip);
    } else {
        v.fieldOfView = sizeOrFov;
        v.projection = Mat4::perspective(radians(sizeOrFov), aspect, nearClip, farClip);
    }
    v.viewProjection = v.projection * v.view;
    return v;
}

Entity SceneRenderer::findCamera(const Scene& scene) {
    Entity fallback;
    Entity primary;
    scene.walk([&](Entity e, int) {
        if (!scene.isActive(e))
            return false;
        if (const Camera* c = scene.registry().tryGet<Camera>(e)) {
            if (c->primary && !primary)
                primary = e;
            if (!fallback)
                fallback = e;
        }
        return true;
    });
    return primary ? primary : fallback;
}

CameraView SceneRenderer::cameraFromEntity(const Scene& scene, Entity e, float aspect) {
    const Camera& cam = scene.registry().get<Camera>(e);
    Vec3 t, s;
    Quat r;
    decompose(scene.worldMatrix(e), t, r, s);
    bool ortho = cam.projection == Projection::Orthographic;
    CameraView v = makeCamera(t, r, ortho, ortho ? cam.size : cam.fieldOfView, aspect, cam.nearClip, cam.farClip);
    v.background = cam.background;
    v.entity = e;
    return v;
}

CameraView SceneRenderer::sceneCamera(const Scene& scene, float aspect) {
    Entity e = findCamera(scene);
    if (e)
        return cameraFromEntity(scene, e, aspect);
    return makeCamera({0, 0, 10}, Quat{}, true, 5.0f, aspect);
}

// ---------------------------------------------------------------- setup

SceneRenderer::SceneRenderer() : renderer3D_(std::make_unique<Renderer3D>()), post_(std::make_unique<PostProcessor>()) {}

SceneRenderer::~SceneRenderer() {
    shutdown();
}

bool SceneRenderer::init(rhi::Device* device, Assets* assets) {
    device_ = device;
    assets_ = assets;
    if (!renderer2D_.init(device) || !renderer3D_->init(device, assets) || !post_->init(device)) {
        Log::error("The renderer failed to start.");
        return false;
    }
    return true;
}

void SceneRenderer::shutdown() {
    if (!device_)
        return;
    releaseTargets();
    post_->shutdown();
    renderer3D_->shutdown();
    renderer2D_.shutdown();
    device_ = nullptr;
}

void SceneRenderer::releaseTargets() {
    for (auto fb : {sceneFb_, outputFb_})
        if (fb)
            device_->destroy(fb);
    for (auto t : {sceneColor_, sceneNormal_, sceneDepth_, outputColor_})
        if (t)
            device_->destroy(t);
    sceneFb_ = outputFb_ = {};
    sceneColor_ = sceneNormal_ = sceneDepth_ = outputColor_ = {};
    width_ = height_ = 0;
}

void SceneRenderer::ensureTargets(int w, int h) {
    if (w == width_ && h == height_ && sceneFb_)
        return;
    releaseTargets();
    width_ = w;
    height_ = h;
    auto tex = [&](rhi::PixelFormat f, const char* label) {
        rhi::TextureDesc d;
        d.width = w;
        d.height = h;
        d.format = f;
        d.renderTarget = true;
        d.filter = rhi::Filter::Linear;
        d.label = label;
        return device_->createTexture(d);
    };
    sceneColor_ = tex(rhi::PixelFormat::RGBA16F, "scene color");
    sceneNormal_ = tex(rhi::PixelFormat::RGBA8, "scene normals");
    sceneDepth_ = tex(rhi::PixelFormat::Depth24, "scene depth");
    outputColor_ = tex(rhi::PixelFormat::RGBA8, "output");
    rhi::FramebufferDesc sd;
    sd.colors = {sceneColor_, sceneNormal_};
    sd.depth = sceneDepth_;
    sd.label = "scene";
    sceneFb_ = device_->createFramebuffer(sd);
    rhi::FramebufferDesc od;
    od.colors = {outputColor_};
    od.label = "output";
    outputFb_ = device_->createFramebuffer(od);
    post_->resize(w, h);
}

// ---------------------------------------------------------------- frame

void SceneRenderer::render(Scene& scene, const CameraView& cameraIn, int w, int h, const RenderOptions& options) {
    if (!device_ || w <= 0 || h <= 0)
        return;
    ensureTargets(w, h);
    scene.updateTransforms();

    CameraView camera = cameraIn;
    if (lengthSquared(cameraShake) > 0) {
        camera.view = inverse(Mat4::translation(cameraShake)) * camera.view;
        camera.viewProjection = camera.projection * camera.view;
    }

    bool has3D = renderer3D_->hasContent(scene);
    if (has3D)
        renderer3D_->prepare(scene, camera);

    Color bg = camera.background;
    rhi::PassDesc pass;
    pass.framebuffer = sceneFb_;
    pass.width = w;
    pass.height = h;
    pass.clearValue = {std::pow(bg.r, 2.2f), std::pow(bg.g, 2.2f), std::pow(bg.b, 2.2f), 1};
    pass.label = "scene";
    device_->beginPass(pass);
    if (has3D) {
        renderer3D_->drawSky(scene, camera);
        renderer3D_->drawOpaque(scene, camera);
    }
    device_->endPass();

    const PostProcessing* pp =
        options.postProcessing && camera.entity ? scene.registry().tryGet<PostProcessing>(camera.entity) : nullptr;
    if (has3D && pp && pp->ssao)
        post_->applySSAO(sceneFb_, sceneDepth_, sceneNormal_, camera.projection, *pp);

    rhi::PassDesc overlay;
    overlay.framebuffer = sceneFb_;
    overlay.width = w;
    overlay.height = h;
    overlay.clearColor = false;
    overlay.clearDepth = false;
    device_->beginPass(overlay);
    if (has3D)
        renderer3D_->drawTransparent(scene, camera);
    draw2D(scene, camera, has3D);
    if (sceneOverlay)
        sceneOverlay(camera);
    device_->endPass();

    post_->composite(sceneColor_, outputFb_, pp);

    if (options.drawUI || screenOverlay) {
        rhi::PassDesc ui;
        ui.framebuffer = outputFb_;
        ui.width = w;
        ui.height = h;
        ui.clearColor = false;
        ui.clearDepth = false;
        device_->beginPass(ui);
        if (options.drawUI)
            drawUI(scene, w, h);
        if (screenOverlay)
            screenOverlay(camera);
        device_->endPass();
    }
}

void SceneRenderer::draw2D(Scene& scene, const CameraView& camera, bool depthTest) {
    struct Item {
        int order;
        float depth;
        uint32_t seq;
        Entity e;
    };
    std::vector<Item> items;
    uint32_t seq = 0;
    auto& reg = scene.registry();
    scene.walk([&](Entity e, int) {
        if (!reg.get<EntityInfo>(e).active)
            return false;
        ++seq;
        if (reg.has<UIElement>(e) || reg.has<Hidden>(e))
            return true;
        const SpriteRenderer* sr = reg.tryGet<SpriteRenderer>(e);
        const TextRenderer* tr = reg.tryGet<TextRenderer>(e);
        const ParticleState* ps = reg.tryGet<ParticleState>(e);
        bool particles = ps && !ps->particles.empty() && reg.has<ParticleEmitter>(e);
        if (!sr && !tr && !particles)
            return true;
        const Mat4& world = reg.get<WorldTransform>(e).matrix;
        float viewZ = transformPoint(camera.view, {world.m[12], world.m[13], world.m[14]}).z;
        int order = sr ? sr->order : tr ? tr->order : 0;
        items.push_back({order, viewZ, seq, e});
        return true;
    });
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        if (a.order != b.order)
            return a.order < b.order;
        if (a.depth != b.depth)
            return a.depth < b.depth;
        return a.seq < b.seq;
    });

    renderer2D_.begin(camera.viewProjection, true, depthTest);
    for (const Item& item : items) {
        const Mat4& world = reg.get<WorldTransform>(item.e).matrix;
        if (const SpriteRenderer* sr = reg.tryGet<SpriteRenderer>(item.e)) {
            const TextureAsset& tex =
                sr->texture.empty() ? assets_->shape(sr->shape) : assets_->texture(sr->texture, sr->pixelArt);
            int cols = std::max(1, sr->columns), rows = std::max(1, sr->rows);
            int frame = std::clamp(sr->frame, 0, cols * rows - 1);
            int col = frame % cols, row = frame / cols;
            float u0 = static_cast<float>(col) / cols, u1 = static_cast<float>(col + 1) / cols;
            float v1 = 1.0f - static_cast<float>(row) / rows, v0 = 1.0f - static_cast<float>(row + 1) / rows;
            if (sr->flipX)
                std::swap(u0, u1);
            if (sr->flipY)
                std::swap(v0, v1);
            renderer2D_.sprite(world, sr->size, {u0, v0, u1, v1}, sr->color, tex.handle);
        }
        if (const TextRenderer* tr = reg.tryGet<TextRenderer>(item.e))
            renderer2D_.text(assets_->font(tr->font), tr->text, world, tr->fontSize, tr->color, tr->align);
        const ParticleState* ps = reg.tryGet<ParticleState>(item.e);
        const ParticleEmitter* pe = reg.tryGet<ParticleEmitter>(item.e);
        if (ps && pe && !ps->particles.empty()) {
            // Camera-facing quads: use the camera's right and up directions.
            Mat4 camWorld = inverse(camera.view);
            Vec3 right = normalize(camWorld.column(0).xyz()), up = normalize(camWorld.column(1).xyz());
            rhi::TextureHandle tex = pe->texture.empty() ? assets_->glow().handle : assets_->texture(pe->texture).handle;
            rhi::BlendMode blend = pe->additive ? rhi::BlendMode::Additive : rhi::BlendMode::Alpha;
            Vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
            for (const Particle& p : ps->particles) {
                float t = p.age / std::max(p.life, 1e-4f);
                float size = lerp(pe->startSize, pe->endSize, t) * 0.5f;
                Color c = lerp(pe->startColor, pe->endColor, t);
                Vec3 center = pe->worldSpace ? p.position : transformPoint(world, p.position);
                float a = radians(p.rotation);
                Vec3 r = (right * std::cos(a) + up * std::sin(a)) * size;
                Vec3 u = (up * std::cos(a) - right * std::sin(a)) * size;
                Vec3 corners[4] = {center - r - u, center + r - u, center + r + u, center - r + u};
                renderer2D_.quad(corners, uvs, c, tex, blend);
            }
        }
    }
    renderer2D_.end();
}

namespace {

// Draws a rounded panel without stretching its corners.
void drawPanel(Renderer2D& r, Assets& assets, const UIRect& rect, Shape2D shape, const std::string& texture,
               Color color) {
    if (!texture.empty()) {
        r.rect(rect.min, rect.max, color, assets.texture(texture).handle);
        return;
    }
    if (shape != Shape2D::RoundedSquare) {
        r.rect(rect.min, rect.max, color, assets.shape(shape).handle);
        return;
    }
    rhi::TextureHandle tex = assets.shape(Shape2D::RoundedSquare).handle;
    Vec2 size = rect.size();
    float border = std::min({size.x * 0.5f, size.y * 0.5f, 14.0f * rect.scale});
    const float t = 0.2f; // texture border as a fraction of the shape texture
    float xs[4] = {rect.min.x, rect.min.x + border, rect.max.x - border, rect.max.x};
    float ys[4] = {rect.min.y, rect.min.y + border, rect.max.y - border, rect.max.y};
    float us[4] = {0, t, 1 - t, 1};
    for (int yi = 0; yi < 3; ++yi)
        for (int xi = 0; xi < 3; ++xi) {
            Vec3 c[4] = {{xs[xi], ys[yi], 0}, {xs[xi + 1], ys[yi], 0}, {xs[xi + 1], ys[yi + 1], 0}, {xs[xi], ys[yi + 1], 0}};
            Vec2 uv[4] = {{us[xi], us[yi]}, {us[xi + 1], us[yi]}, {us[xi + 1], us[yi + 1]}, {us[xi], us[yi + 1]}};
            r.quad(c, uv, color, tex);
        }
}

Vec2 textAnchor(const UIRect& rect, TextAlign align, float padding) {
    float y = rect.center().y;
    switch (align) {
    case TextAlign::Left: return {rect.min.x + padding, y};
    case TextAlign::Right: return {rect.max.x - padding, y};
    default: return {rect.center().x, y};
    }
}

} // namespace

void SceneRenderer::drawUI(Scene& scene, int w, int h) {
    struct Item {
        int order;
        uint32_t seq;
        Entity e;
    };
    std::vector<Item> items;
    uint32_t seq = 0;
    auto& reg = scene.registry();
    scene.walk([&](Entity e, int) {
        if (!reg.get<EntityInfo>(e).active)
            return false;
        if (const UIElement* ui = reg.tryGet<UIElement>(e))
            items.push_back({ui->order, seq++, e});
        return true;
    });
    if (items.empty())
        return;
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.order != b.order ? a.order < b.order : a.seq < b.seq;
    });

    renderer2D_.begin(Mat4::orthographic(0, static_cast<float>(w), 0, static_cast<float>(h), -1, 1), false);
    for (const Item& item : items) {
        UIRect rect = computeUIRect(scene, item.e, static_cast<float>(w), static_cast<float>(h));
        if (const UIImage* img = reg.tryGet<UIImage>(item.e))
            drawPanel(renderer2D_, *assets_, rect, img->shape, img->texture, img->color);
        if (const UIButton* btn = reg.tryGet<UIButton>(item.e)) {
            Color c = btn->pressed ? btn->pressedColor : btn->hovered ? btn->hoverColor : btn->normalColor;
            drawPanel(renderer2D_, *assets_, rect, Shape2D::RoundedSquare, "", c);
            renderer2D_.textAt(assets_->defaultFont(), btn->text, rect.center(), btn->fontSize * rect.scale,
                               btn->textColor, TextAlign::Center, true);
        }
        if (const UIText* txt = reg.tryGet<UIText>(item.e)) {
            Vec2 anchor = textAnchor(rect, txt->align, 4.0f * rect.scale);
            renderer2D_.textAt(assets_->font(txt->font), txt->text, anchor, txt->fontSize * rect.scale, txt->color,
                               txt->align, true);
        }
    }
    renderer2D_.end();
}

void SceneRenderer::present(int windowWidth, int windowHeight) {
    if (outputFb_)
        device_->blitToScreen(outputFb_, width_, height_, windowWidth, windowHeight);
}

std::vector<uint8_t> SceneRenderer::readOutput() {
    std::vector<uint8_t> pixels(static_cast<size_t>(width_) * static_cast<size_t>(height_) * 4);
    if (!outputFb_)
        return pixels;
    device_->readPixels(outputFb_, 0, 0, 0, width_, height_, pixels.data());
    // Flip so the first row is the top of the image.
    size_t row = static_cast<size_t>(width_) * 4;
    std::vector<uint8_t> tmp(row);
    for (int y = 0; y < height_ / 2; ++y) {
        uint8_t* a = &pixels[static_cast<size_t>(y) * row];
        uint8_t* b = &pixels[static_cast<size_t>(height_ - 1 - y) * row];
        std::memcpy(tmp.data(), a, row);
        std::memcpy(a, b, row);
        std::memcpy(b, tmp.data(), row);
    }
    return pixels;
}

} // namespace aven
