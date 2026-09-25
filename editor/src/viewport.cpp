#include "editor.h"

#include "aven/core/fs.h"
#include "aven/render/renderer3d.h"
#include "aven/render/ui_layout.h"
#include "aven/runtime/script_system.h"

#include <imgui.h>

#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

CameraView Editor::editorCamera() const {
    float aspect = viewportSize_.x / std::max(viewportSize_.y, 1.0f);
    CameraView cam;
    if (view3D_)
        cam = SceneRenderer::makeCamera(cam3D_, Quat::fromEuler({camPitch_, camYaw_, 0}), false, 60.0f, aspect, 0.05f, 2000.0f);
    else
        cam = SceneRenderer::makeCamera(cam2D_, Quat{}, true, camZoom_, aspect, 0.1f, 2000.0f);
    // Use the scene's camera background so the editor view matches the game.
    Entity c = SceneRenderer::findCamera(*scene_);
    if (c)
        cam.background = scene_->registry().get<Camera>(c).background;
    else
        cam.background = Color::fromHex(0x2A3040);
    if (c && scene_->registry().has<PostProcessing>(c))
        cam.entity = c;
    return cam;
}

void Editor::focusSelected() {
    Entity e = selected();
    if (!e)
        return;
    Vec3 p = scene_->worldPosition(e);
    if (view3D_) {
        Vec3 mn, mx;
        float radius = 2;
        if (renderer_.renderer3D().worldBounds(*scene_, e, mn, mx)) {
            p = (mn + mx) * 0.5f;
            radius = std::max(length(mx - mn) * 0.5f, 0.5f);
        }
        Vec3 back = rotate(Quat::fromEuler({camPitch_, camYaw_, 0}), {0, 0, 1});
        cam3D_ = p + back * (radius * 2.5f + 1.0f);
    } else {
        cam2D_ = {p.x, p.y, 10};
    }
}

void Editor::updateViewportCamera(float dt) {
    ImGuiIO& io = ImGui::GetIO();
    if (!viewportHovered_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        return;
    Vec2 delta{io.MouseDelta.x, io.MouseDelta.y};
    if (!view3D_) {
        float unitsPerPixel = camZoom_ * 2.0f / std::max(viewportSize_.y, 1.0f);
        bool pan = ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0) || ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0) ||
                   (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0));
        if (pan) {
            cam2D_.x -= delta.x * unitsPerPixel;
            cam2D_.y += delta.y * unitsPerPixel;
        }
        if (viewportHovered_ && io.MouseWheel != 0) {
            // Zoom toward the mouse pointer.
            Vec2 local{io.MousePos.x - viewportPos_.x, io.MousePos.y - viewportPos_.y};
            Vec3 before = editorCamera().screenToWorld(local, viewportSize_);
            camZoom_ = std::clamp(camZoom_ * (io.MouseWheel > 0 ? 0.88f : 1.14f), 0.5f, 500.0f);
            Vec3 after = editorCamera().screenToWorld(local, viewportSize_);
            cam2D_.x += before.x - after.x;
            cam2D_.y += before.y - after.y;
        }
        return;
    }
    Quat rot = Quat::fromEuler({camPitch_, camYaw_, 0});
    Vec3 forward = rotate(rot, {0, 0, -1}), right = rotate(rot, {1, 0, 0}), up{0, 1, 0};
    if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        // Look around, and fly with WASD while the right mouse button is held.
        camYaw_ -= delta.x * 0.25f;
        camPitch_ = std::clamp(camPitch_ - delta.y * 0.25f, -89.0f, 89.0f);
        float speed = (io.KeyShift ? 18.0f : 6.0f) * dt;
        if (ImGui::IsKeyDown(ImGuiKey_W)) cam3D_ += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) cam3D_ -= forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) cam3D_ += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) cam3D_ -= right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) cam3D_ += up * speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) cam3D_ -= up * speed;
    }
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0)) {
        float s = 0.01f + length(cam3D_) * 0.0015f;
        cam3D_ -= right * (delta.x * s);
        cam3D_ += rotate(rot, {0, 1, 0}) * (delta.y * s);
    }
    if (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0)) {
        // Orbit around the selected object (or a point in front of the camera).
        Entity sel = selected();
        Vec3 pivot = sel ? scene_->worldPosition(sel) : cam3D_ + forward * 8.0f;
        float dist = length(cam3D_ - pivot);
        camYaw_ -= delta.x * 0.3f;
        camPitch_ = std::clamp(camPitch_ - delta.y * 0.3f, -89.0f, 89.0f);
        Vec3 back = rotate(Quat::fromEuler({camPitch_, camYaw_, 0}), {0, 0, 1});
        cam3D_ = pivot + back * dist;
    }
    if (viewportHovered_ && io.MouseWheel != 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Right))
        cam3D_ += forward * (io.MouseWheel * (1.0f + length(cam3D_) * 0.08f));
}

void Editor::pickInViewport(Vec2 local) {
    Scene& s = *scene_;
    auto& reg = s.registry();
    // UI elements first: they're drawn on top.
    Entity best;
    int bestOrder = -1000000;
    s.walk([&](Entity e, int) {
        if (!s.info(e).active)
            return false;
        if (reg.has<UIElement>(e)) {
            UIRect r = computeUIRect(s, e, viewportSize_.x, viewportSize_.y);
            Vec2 p{local.x, viewportSize_.y - local.y};
            if (r.contains(p) && reg.get<UIElement>(e).order >= bestOrder) {
                best = e;
                bestOrder = reg.get<UIElement>(e).order;
            }
        }
        return true;
    });
    if (best) {
        select(best);
        return;
    }
    CameraView cam = editorCamera();
    if (view3D_) {
        Vec3 o, d;
        cam.screenRay(local, viewportSize_, o, d);
        float dist;
        Entity hit = renderer_.renderer3D().raycast(s, o, d, &dist);
        if (hit) {
            select(hit);
            return;
        }
    }
    Vec3 world = cam.screenToWorld(local, viewportSize_);
    bestOrder = -1000000;
    s.walk([&](Entity e, int) {
        if (!s.info(e).active)
            return false;
        Vec2 half{0, 0};
        int order = 0;
        if (auto* sr = reg.tryGet<SpriteRenderer>(e)) {
            half = sr->size * 0.5f;
            order = sr->order;
        } else if (auto* tr = reg.tryGet<TextRenderer>(e)) {
            Vec2 m = assets_.font(tr->font).measure(tr->text, tr->fontSize);
            half = m * 0.5f;
            order = tr->order;
        } else if (reg.has<ParticleEmitter>(e) || reg.has<Camera>(e) || reg.has<Light>(e) || reg.has<AudioSource>(e)) {
            half = {0.3f, 0.3f};
        } else {
            return true;
        }
        Vec3 p = transformPoint(inverse(s.worldMatrix(e)), world);
        if (std::abs(p.x) <= half.x && std::abs(p.y) <= half.y && order >= bestOrder) {
            best = e;
            bestOrder = order;
        }
        return true;
    });
    select(best);
}

namespace {

// Thin quads used to draw lines in the scene view.
void line(Renderer2D& r, const CameraView& cam, Vec3 a, Vec3 b, float pixels, Color c, float viewportHeight,
          rhi::TextureHandle white) {
    Vec3 dir = b - a;
    if (lengthSquared(dir) < 1e-12f)
        return;
    Vec3 mid = (a + b) * 0.5f;
    float worldPerPixel = cam.orthographic ? cam.orthoSize * 2.0f / viewportHeight
                                           : length(mid - cam.position) * 2.0f * std::tan(radians(cam.fieldOfView) * 0.5f) / viewportHeight;
    Vec3 toCam = cam.orthographic ? -cam.forward : normalize(cam.position - mid);
    Vec3 side = cross(normalize(dir), toCam);
    if (lengthSquared(side) < 1e-8f)
        return;
    side = normalize(side) * (pixels * worldPerPixel * 0.5f);
    Vec3 corners[4] = {a - side, b - side, b + side, a + side};
    Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    r.quad(corners, uv, c, white);
}

} // namespace

void Editor::drawSceneOverlay(const CameraView& cam) {
    if (playing_)
        return;
    Scene& s = *scene_;
    auto& reg = s.registry();
    Renderer2D& r = renderer_.renderer2D();
    rhi::TextureHandle white = assets_.white().handle;
    float vh = viewportSize_.y;
    r.begin(cam.viewProjection, true, view3D_);

    if (showGrid_) {
        if (view3D_) {
            float extent = 60;
            Vec3 c{std::round(cam.position.x), 0, std::round(cam.position.z)};
            for (int i = -60; i <= 60; ++i) {
                float alpha = i % 5 == 0 ? 0.28f : 0.12f;
                Color col{1, 1, 1, alpha};
                if (i + c.x == 0)
                    col = {0.9f, 0.3f, 0.3f, 0.6f};
                line(r, cam, {c.x + i, 0.001f, c.z - extent}, {c.x + i, 0.001f, c.z + extent}, 1.2f, i + c.x == 0 ? Color{0.3f, 0.5f, 0.95f, 0.6f} : col, vh, white);
                line(r, cam, {c.x - extent, 0.001f, c.z + i}, {c.x + extent, 0.001f, c.z + i}, 1.2f, i + c.z == 0 ? Color{0.9f, 0.3f, 0.3f, 0.6f} : Color{1, 1, 1, alpha}, vh, white);
            }
        } else {
            float halfH = camZoom_, halfW = camZoom_ * viewportSize_.x / std::max(vh, 1.0f);
            float step = camZoom_ > 40 ? 10.0f : camZoom_ > 12 ? 5.0f : 1.0f;
            float x0 = std::floor((cam2D_.x - halfW) / step) * step, x1 = cam2D_.x + halfW;
            float y0 = std::floor((cam2D_.y - halfH) / step) * step, y1 = cam2D_.y + halfH;
            for (float x = x0; x <= x1; x += step)
                line(r, cam, {x, cam2D_.y - halfH, -50}, {x, cam2D_.y + halfH, -50}, 1.0f,
                     x == 0 ? Color{0.4f, 0.8f, 0.4f, 0.55f} : Color{1, 1, 1, std::fmod(std::abs(x), step * 5) < 0.01f ? 0.16f : 0.07f}, vh, white);
            for (float y = y0; y <= y1; y += step)
                line(r, cam, {cam2D_.x - halfW, y, -50}, {cam2D_.x + halfW, y, -50}, 1.0f,
                     y == 0 ? Color{0.9f, 0.35f, 0.35f, 0.55f} : Color{1, 1, 1, std::fmod(std::abs(y), step * 5) < 0.01f ? 0.16f : 0.07f}, vh, white);
        }
    }

    // What the game camera sees (2D).
    Entity gameCam = SceneRenderer::findCamera(s);
    if (gameCam && !view3D_) {
        const Camera& c = reg.get<Camera>(gameCam);
        if (c.projection == Projection::Orthographic) {
            Vec3 p = s.worldPosition(gameCam);
            float aspect = static_cast<float>(settings_.width) / std::max(settings_.height, 1);
            float h = c.size, w = c.size * aspect;
            Color col{1, 1, 1, 0.45f};
            line(r, cam, {p.x - w, p.y - h, 0}, {p.x + w, p.y - h, 0}, 1.5f, col, vh, white);
            line(r, cam, {p.x - w, p.y + h, 0}, {p.x + w, p.y + h, 0}, 1.5f, col, vh, white);
            line(r, cam, {p.x - w, p.y - h, 0}, {p.x - w, p.y + h, 0}, 1.5f, col, vh, white);
            line(r, cam, {p.x + w, p.y - h, 0}, {p.x + w, p.y + h, 0}, 1.5f, col, vh, white);
        }
    }

    // Small icons for invisible objects (lights, cameras, sounds, empty objects).
    s.walk([&](Entity e, int) {
        if (!s.info(e).active)
            return false;
        bool icon = reg.has<Light>(e) || reg.has<Camera>(e) || reg.has<AudioSource>(e) || reg.has<ParticleEmitter>(e);
        if (!icon)
            return true;
        Vec3 p = s.worldPosition(e);
        Color col = reg.has<Light>(e) ? Color{1, 0.85f, 0.3f, 0.9f} : reg.has<Camera>(e) ? Color{0.6f, 0.8f, 1, 0.9f} : Color{0.8f, 0.6f, 1, 0.9f};
        float worldPerPixel = cam.orthographic ? cam.orthoSize * 2.0f / vh : length(p - cam.position) * 1.15f / vh;
        float size = 7 * worldPerPixel;
        Mat4 camWorld = inverse(cam.view);
        Vec3 rt = normalize(camWorld.column(0).xyz()) * size, up = normalize(camWorld.column(1).xyz()) * size;
        Vec3 corners[4] = {p - rt - up, p + rt - up, p + rt + up, p - rt + up};
        Vec2 uv[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        r.quad(corners, uv, col, assets_.shape(reg.has<Light>(e) ? Shape2D::Star : Shape2D::Circle).handle);
        if (auto* l = reg.tryGet<Light>(e); l && l->type != LightType::Point) {
            Vec3 dir = normalize(transformDirection(s.worldMatrix(e), {0, 0, -1}));
            line(r, cam, p, p + dir * 1.5f, 2.0f, col, vh, white);
        }
        return true;
    });

    // Selection outline and colliders.
    Entity sel = selected();
    if (sel) {
        Color outline{1.0f, 0.62f, 0.1f, 1.0f};
        Mat4 m = s.worldMatrix(sel);
        auto box2D = [&](Vec2 half, Vec2 offset, Color c) {
            Vec3 q[4] = {transformPoint(m, {offset.x - half.x, offset.y - half.y, 0}), transformPoint(m, {offset.x + half.x, offset.y - half.y, 0}),
                         transformPoint(m, {offset.x + half.x, offset.y + half.y, 0}), transformPoint(m, {offset.x - half.x, offset.y + half.y, 0})};
            for (int i = 0; i < 4; ++i)
                line(r, cam, q[i], q[(i + 1) % 4], 2.0f, c, vh, white);
        };
        if (auto* sr = reg.tryGet<SpriteRenderer>(sel))
            box2D(sr->size * 0.5f, {0, 0}, outline);
        if (auto* b = reg.tryGet<BoxCollider2D>(sel))
            box2D(b->size * 0.5f, b->offset, {0.3f, 1, 0.4f, 1});
        if (auto* c = reg.tryGet<CircleCollider2D>(sel)) {
            for (int i = 0; i < 32; ++i) {
                float a0 = i * 2 * kPi / 32, a1 = (i + 1) * 2 * kPi / 32;
                Vec3 p0 = transformPoint(m, {c->offset.x + std::cos(a0) * c->radius, c->offset.y + std::sin(a0) * c->radius, 0});
                Vec3 p1 = transformPoint(m, {c->offset.x + std::cos(a1) * c->radius, c->offset.y + std::sin(a1) * c->radius, 0});
                line(r, cam, p0, p1, 2.0f, {0.3f, 1, 0.4f, 1}, vh, white);
            }
        }
        Vec3 mn, mx;
        if (view3D_ && renderer_.renderer3D().worldBounds(s, sel, mn, mx)) {
            Vec3 c[8];
            for (int i = 0; i < 8; ++i)
                c[i] = {(i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z};
            int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
            for (auto& ed : edges)
                line(r, cam, c[ed[0]], c[ed[1]], 1.5f, outline, vh, white);
        }
    }
    r.end();
}

void Editor::handleViewportDrop() {
    if (!ImGui::BeginDragDropTarget())
        return;
    if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
        std::string path(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
        std::string ext = fs::extension(path);
        Vec2 local{ImGui::GetMousePos().x - viewportPos_.x, ImGui::GetMousePos().y - viewportPos_.y};
        CameraView cam = editorCamera();
        Vec3 at = cam.screenToWorld(local, viewportSize_);
        if (view3D_) {
            Vec3 o, d;
            cam.screenRay(local, viewportSize_, o, d);
            float dist;
            Entity hit = renderer_.renderer3D().raycast(*scene_, o, d, &dist);
            at = hit ? o + d * dist : (std::abs(d.y) > 1e-3f && -o.y / d.y > 0 ? o + d * (-o.y / d.y) : o + d * 8.0f);
        }
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga") {
            Entity e = createEntity("Sprite");
            scene_->info(e).name = stdfs::path(path).stem().string();
            auto& sr = scene_->registry().get<SpriteRenderer>(e);
            sr.texture = path;
            const TextureAsset& t = assets_.texture(path, true);
            if (t.height > 0)
                sr.size = {static_cast<float>(t.width) / t.height, 1.0f};
            scene_->transform(e).position = {at.x, at.y, view3D_ ? at.z : 0};
        } else if (ext == ".prefab") {
            instantiatePrefab(path, at);
        } else if (ext == ".gltf" || ext == ".glb") {
            Entity e = createEntity("Entity");
            scene_->info(e).name = stdfs::path(path).stem().string();
            auto& mr = scene_->registry().emplace<MeshRenderer>(e);
            mr.mesh = MeshShape::Model;
            mr.model = path;
            scene_->transform(e).position = at;
        } else if (ext == ".es" || ext == ".blocks") {
            // Attach to whatever is under the mouse.
            pickInViewport(local);
            if (Entity e = selected())
                attachScript(e, path);
            else
                notify("Drop scripts onto an object to attach them.");
        } else if (ext == ".scene") {
            openScene(path);
        } else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") {
            Entity e = createEntity("Sound");
            scene_->registry().get<AudioSource>(e).clip = path;
            scene_->info(e).name = stdfs::path(path).stem().string();
        }
    }
    ImGui::EndDragDropTarget();
}

void Editor::drawViewport(float dt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    std::string title = playing_ ? (paused_ ? "Game (paused)###Viewport" : "Game (playing)###Viewport") : "Scene###Viewport";
    if (focusViewport_) {
        ImGui::SetNextWindowFocus();
        focusViewport_ = false;
    }
    ImGui::Begin(title.c_str(), nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    viewportPos_ = {pos.x, pos.y};
    viewportSize_ = {std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)};
    float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    int w = static_cast<int>(viewportSize_.x * scale), h = static_cast<int>(viewportSize_.y * scale);

    if (playing_) {
        viewportFocused_ = ImGui::IsWindowFocused();
        // The game only sees input while its view is focused.
        if (viewportFocused_ && !ImGui::GetIO().WantTextInput)
            gameInput_.mirror(window_.input(), viewportPos_);
        else
            gameInput_.reset();
        game_->setScreenSize(viewportSize_);
        if (!paused_ || stepOnce_) {
            game_->update(stepOnce_ ? 1.0f / 60.0f : dt);
            stepOnce_ = false;
        }
        if (game_->quitRequested())
            stop();
    }
    if (w > 0 && h > 0) {
        device_.beginFrame();
        if (playing_ && game_) {
            game_->render(renderer_, w, h);
        } else {
            RenderOptions opts;
            renderer_.render(*scene_, editorCamera(), w, h, opts);
        }
        ImTextureID tex = static_cast<ImTextureID>(device_.nativeTexture(renderer_.outputTexture()));
        ImGui::Image(tex, avail, {0, 1}, {1, 0});
    }
    viewportHovered_ = ImGui::IsItemHovered();
    if (!playing_)
        viewportFocused_ = ImGui::IsWindowFocused();
    handleViewportDrop();

    if (playing_ && viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::SetWindowFocus();

    if (!playing_) {
        updateViewportCamera(dt);
        Entity sel = selected();
        ImGuizmo::SetOrthographic(!view3D_);
        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(pos.x, pos.y, avail.x, avail.y);
        bool usingGizmo = false;
        bool overGizmo = false;
        if (sel && !scene_->registry().has<UIElement>(sel)) {
            CameraView cam = editorCamera();
            Mat4 world = scene_->worldMatrix(sel);
            ImGuizmo::OPERATION op;
            if (view3D_)
                op = gizmoOp_ == 0 ? ImGuizmo::TRANSLATE : gizmoOp_ == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
            else
                op = gizmoOp_ == 0 ? (ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y)
                   : gizmoOp_ == 1 ? ImGuizmo::ROTATE_Z
                                   : (ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y);
            bool doSnap = snap_ || ImGui::GetIO().KeyCtrl;
            float snapValues[3];
            float step = gizmoOp_ == 1 ? 15.0f : gizmoOp_ == 2 ? 0.25f : 0.5f;
            snapValues[0] = snapValues[1] = snapValues[2] = step;
            ImGuizmo::Manipulate(cam.view.m, cam.projection.m, op, gizmoOp_ == 0 ? ImGuizmo::WORLD : ImGuizmo::LOCAL, world.m,
                                 nullptr, doSnap ? snapValues : nullptr);
            usingGizmo = ImGuizmo::IsUsing();
            overGizmo = ImGuizmo::IsOver();
            if (usingGizmo) {
                edited(gizmoOp_ == 0 ? "Move" : gizmoOp_ == 1 ? "Rotate" : "Scale");
                scene_->setWorldMatrix(sel, world);
                if (!view3D_) {
                    // Keep 2D objects flat.
                    Transform& t = scene_->transform(sel);
                    t.rotation.x = t.rotation.y = 0;
                }
            }
        }
        gizmoWasUsing_ = usingGizmo;
        // Dragging a selected UI element moves it on screen.
        if (sel && scene_->registry().has<UIElement>(sel) && viewportHovered_ &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2) && !ImGui::GetIO().KeyAlt) {
            UIRect r = computeUIRect(*scene_, sel, viewportSize_.x, viewportSize_.y);
            ImVec2 m = ImGui::GetIO().MouseClickedPos[0];
            Vec2 start{m.x - viewportPos_.x, viewportSize_.y - (m.y - viewportPos_.y)};
            if (r.contains(start) || gizmoWasUsing_) {
                edited("Move UI");
                ImVec2 d = ImGui::GetIO().MouseDelta;
                scene_->registry().get<UIElement>(sel).offset += Vec2(d.x, -d.y) / r.scale;
                gizmoWasUsing_ = true;
            }
        }
        if (viewportHovered_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !usingGizmo && !overGizmo &&
            !ImGui::GetIO().KeyAlt) {
            ImVec2 click = ImGui::GetIO().MouseClickedPos[0];
            ImVec2 now = ImGui::GetIO().MousePos;
            if (std::abs(click.x - now.x) < 4 && std::abs(click.y - now.y) < 4)
                pickInViewport({now.x - viewportPos_.x, now.y - viewportPos_.y});
        }
        if (viewportHovered_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selected())
            focusSelected();

        // Hints for beginners.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* hint = view3D_ ? "Right-drag: look  |  Right-drag + WASD: fly  |  Alt+drag: orbit  |  F: focus"
                                   : "Right-drag: pan  |  Scroll: zoom  |  Click: select  |  W/E/R: move/rotate/scale";
        dl->AddText({pos.x + 10, pos.y + avail.y - 24}, IM_COL32(255, 255, 255, 110), hint);
    } else {
        gizmoWasUsing_ = false;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const char* hint = viewportFocused_ ? "Playing - press Ctrl+P or the stop button to go back to editing"
                                            : "Click the game to control it";
        dl->AddRectFilled({pos.x, pos.y}, {pos.x + avail.x, pos.y + 3}, IM_COL32(60, 200, 120, 255));
        dl->AddText({pos.x + 10, pos.y + avail.y - 24}, IM_COL32(255, 255, 255, 120), hint);
    }
    ImGui::End();
}

} // namespace aven::editor
