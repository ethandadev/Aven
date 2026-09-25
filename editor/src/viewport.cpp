#include "editor.h"

#include "aven/core/fs.h"
#include "aven/render/renderer3d.h"
#include "aven/render/ui_layout.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/scene/reflection.h"

#include <imgui.h>

#include <ImGuizmo.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace aven::editor {

namespace {

const char* kAspectNames[] = {"Free", "Game window size", "16:9", "4:3", "9:16 (phone)", "1:1"};

ImU32 toU32(Color c, float alphaScale = 1.0f) {
    return ImGui::ColorConvertFloat4ToU32({c.r, c.g, c.b, c.a * alphaScale});
}

} // namespace

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

// The camera the viewport is showing: the editor camera, or the game's camera while playing.
CameraView Editor::viewportCamera() const {
    if (playing_ && game_)
        return game_->camera(viewportSize_.x / std::max(viewportSize_.y, 1.0f));
    return editorCamera();
}

void Editor::focusSelected() {
    Entity e = selected();
    if (!e || playing_)
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
        // Zoom so the object fills about a third of the view.
        Vec2 half{0.5f, 0.5f};
        if (auto* sr = scene_->registry().tryGet<SpriteRenderer>(e))
            half = sr->size * 0.5f;
        Vec3 sc = scene_->transform(e).scale;
        camZoom_ = std::clamp(std::max(half.x * std::abs(sc.x), half.y * std::abs(sc.y)) * 3.0f, 1.5f, 60.0f);
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
        float speed = prefs.flySpeed * (io.KeyShift ? 3.0f : 1.0f) * dt;
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

Entity Editor::pickEntity(Scene& s, const CameraView& cam, Vec2 local) {
    auto& reg = s.registry();
    // UI elements first: they're drawn on top.
    Entity best;
    int bestOrder = -1000000;
    s.walk([&](Entity e, int) {
        if (!s.info(e).active || reg.has<Hidden>(e))
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
    if (best)
        return best;
    if (!cam.orthographic) {
        Vec3 o, d;
        cam.screenRay(local, viewportSize_, o, d);
        float dist;
        if (Entity hit = renderer_.renderer3D().raycast(s, o, d, &dist))
            return hit;
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
        } else if (!playing_ &&
                   (reg.has<ParticleEmitter>(e) || reg.has<Camera>(e) || reg.has<Light>(e) || reg.has<AudioSource>(e))) {
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
    return best;
}

void Editor::pickInViewport(Vec2 local) {
    Entity e = pickEntity(scene(), viewportCamera(), local);
    if (ImGui::GetIO().KeyCtrl)
        toggleSelection(e);
    else if (ImGui::GetIO().KeyShift)
        addToSelection(e);
    else
        select(e);
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
    // While playing, only outline what's selected, and only when paused (for Play-and-edit).
    if (playing_ && (!paused_ || selection_.empty()))
        return;
    Scene& s = scene();
    auto& reg = s.registry();
    Renderer2D& r = renderer_.renderer2D();
    rhi::TextureHandle white = assets_.white().handle;
    float vh = viewportSize_.y;
    r.begin(cam.viewProjection, true, !cam.orthographic);

    if (showGrid_ && !playing_) {
        Color g = prefs.gridColor;
        float k = prefs.gridOpacity;
        if (!cam.orthographic) {
            float extent = 60;
            Vec3 c{std::round(cam.position.x), 0, std::round(cam.position.z)};
            for (int i = -60; i <= 60; ++i) {
                float alpha = (i % 5 == 0 ? 0.28f : 0.12f) * k;
                Color col{g.r, g.g, g.b, alpha};
                line(r, cam, {c.x + i, 0.001f, c.z - extent}, {c.x + i, 0.001f, c.z + extent}, 1.2f,
                     i + c.x == 0 ? Color{0.3f, 0.5f, 0.95f, 0.6f} : col, vh, white);
                line(r, cam, {c.x - extent, 0.001f, c.z + i}, {c.x + extent, 0.001f, c.z + i}, 1.2f,
                     i + c.z == 0 ? Color{0.9f, 0.3f, 0.3f, 0.6f} : col, vh, white);
            }
        } else {
            float halfH = camZoom_, halfW = camZoom_ * viewportSize_.x / std::max(vh, 1.0f);
            float step = camZoom_ > 40 ? 10.0f : camZoom_ > 12 ? 5.0f : 1.0f;
            float x0 = std::floor((cam2D_.x - halfW) / step) * step, x1 = cam2D_.x + halfW;
            float y0 = std::floor((cam2D_.y - halfH) / step) * step, y1 = cam2D_.y + halfH;
            for (float x = x0; x <= x1; x += step)
                line(r, cam, {x, cam2D_.y - halfH, -50}, {x, cam2D_.y + halfH, -50}, 1.0f,
                     x == 0 ? Color{0.4f, 0.8f, 0.4f, 0.55f}
                            : Color{g.r, g.g, g.b, (std::fmod(std::abs(x), step * 5) < 0.01f ? 0.16f : 0.07f) * k},
                     vh, white);
            for (float y = y0; y <= y1; y += step)
                line(r, cam, {cam2D_.x - halfW, y, -50}, {cam2D_.x + halfW, y, -50}, 1.0f,
                     y == 0 ? Color{0.9f, 0.35f, 0.35f, 0.55f}
                            : Color{g.r, g.g, g.b, (std::fmod(std::abs(y), step * 5) < 0.01f ? 0.16f : 0.07f) * k},
                     vh, white);
        }
    }

    // What the game camera sees (2D).
    Entity gameCam = SceneRenderer::findCamera(s);
    if (gameCam && cam.orthographic && !playing_) {
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
    if (prefs.showIcons && !playing_) {
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
    }

    // Selection outlines and colliders.
    Color outline = prefs.selectionColor;
    for (Entity sel : selectedEntities()) {
        if (reg.has<UIElement>(sel))
            continue;
        Mat4 m = s.worldMatrix(sel);
        auto box2D = [&](Vec2 half, Vec2 offset, Color c) {
            Vec3 q[4] = {transformPoint(m, {offset.x - half.x, offset.y - half.y, 0}), transformPoint(m, {offset.x + half.x, offset.y - half.y, 0}),
                         transformPoint(m, {offset.x + half.x, offset.y + half.y, 0}), transformPoint(m, {offset.x - half.x, offset.y + half.y, 0})};
            for (int i = 0; i < 4; ++i)
                line(r, cam, q[i], q[(i + 1) % 4], 2.0f, c, vh, white);
        };
        if (auto* sr = reg.tryGet<SpriteRenderer>(sel))
            box2D(sr->size * 0.5f, {0, 0}, outline);
        if (prefs.showColliders) {
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
        }
        Vec3 mn, mx;
        if (!cam.orthographic && renderer_.renderer3D().worldBounds(s, sel, mn, mx)) {
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
            Entity e = pickEntity(*scene_, cam, local);
            if (e) {
                select(e);
                attachScript(e, path);
            } else {
                notify("Drop scripts onto an object to attach them.");
            }
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

// Gizmos for the selection. Works in the editor, and on the running game while paused.
void Editor::drawGizmo(const CameraView& cam, ImVec2 pos, ImVec2 size, bool& usingGizmo, bool& overGizmo) {
    Scene& s = scene();
    Entity sel = selected();
    ImGuizmo::SetOrthographic(cam.orthographic);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(pos.x, pos.y, size.x, size.y);
    if (!sel || s.registry().has<UIElement>(sel))
        return;
    Mat4 world = s.worldMatrix(sel);
    Mat4 before = world;
    ImGuizmo::OPERATION op;
    if (!cam.orthographic)
        op = gizmoOp_ == 0 ? ImGuizmo::TRANSLATE : gizmoOp_ == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE;
    else
        op = gizmoOp_ == 0 ? (ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y)
           : gizmoOp_ == 1 ? ImGuizmo::ROTATE_Z
                           : (ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y);
    bool doSnap = snap_ || ImGui::GetIO().KeyCtrl;
    float step = gizmoOp_ == 1 ? prefs.rotateSnap : gizmoOp_ == 2 ? prefs.scaleSnap : prefs.moveSnap;
    float snapValues[3] = {step, step, step};
    ImGuizmo::MODE mode = gizmoOp_ == 2 || prefs.gizmoLocal ? ImGuizmo::LOCAL : ImGuizmo::WORLD;
    ImGuizmo::Manipulate(cam.view.m, cam.projection.m, op, mode, world.m, nullptr, doSnap ? snapValues : nullptr);
    usingGizmo = ImGuizmo::IsUsing();
    overGizmo = ImGuizmo::IsOver();
    if (!usingGizmo)
        return;
    const char* label = gizmoOp_ == 0 ? "Move" : gizmoOp_ == 1 ? "Rotate" : "Scale";
    if (playing_)
        noteLiveChange(sel, std::string("Transform/") + (gizmoOp_ == 0 ? "position" : gizmoOp_ == 1 ? "rotation" : "scale"));
    else
        edited(label);
    // Other selected objects move by the same amount.
    Mat4 delta = world * inverse(before);
    s.setWorldMatrix(sel, world);
    for (Entity other : selectedEntities()) {
        if (other == sel || s.registry().has<UIElement>(other))
            continue;
        bool nested = false;
        for (Entity p = s.parent(other); p; p = s.parent(p))
            if (isSelected(p))
                nested = true; // moves with its selected parent already
        if (nested)
            continue;
        if (gizmoOp_ == 2) {
            // Scaling applies to each object's own size.
            Vec3 k{length(world.column(0).xyz()) / std::max(length(before.column(0).xyz()), 1e-6f),
                   length(world.column(1).xyz()) / std::max(length(before.column(1).xyz()), 1e-6f),
                   length(world.column(2).xyz()) / std::max(length(before.column(2).xyz()), 1e-6f)};
            Transform& t = s.transform(other);
            t.scale = {t.scale.x * k.x, t.scale.y * k.y, t.scale.z * k.z};
        } else {
            s.setWorldMatrix(other, delta * s.worldMatrix(other));
        }
        if (playing_)
            noteLiveChange(other, std::string("Transform/") + (gizmoOp_ == 0 ? "position" : "rotation"));
    }
    if (cam.orthographic) {
        // Keep 2D objects flat.
        for (Entity e : selectedEntities()) {
            Transform& t = s.transform(e);
            t.rotation.x = t.rotation.y = 0;
        }
    }
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

    drawPrefabBar();
    // A slim bar with view options.
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {6, 2});
    ImGui::SetCursorPos({ImGui::GetCursorPosX() + 6, ImGui::GetCursorPosY() + 3});
    ImGui::SetNextItemWidth(130);
    ImGui::Combo("##aspect", &gameAspect_, kAspectNames, IM_ARRAYSIZE(kAspectNames));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Screen shape used while playing (and in the scene view for UI).");
    ImGui::SameLine();
    ImGui::Checkbox("Stats", &showStats_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Mute", &muteGame_) && game_)
        game_->audio().setMasterVolume(muteGame_ ? 0.0f : 1.0f);
    if (playing_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::SliderFloat("##speed", &game_->timeScale, 0.1f, 2.0f, "Speed %.1fx"))
            game_->timeScale = std::max(0.1f, game_->timeScale);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Slow motion (or fast forward) to see what's going on.");
    } else {
        ImGui::SameLine();
        ImGui::Checkbox("Icons", &prefs.showIcons);
        ImGui::SameLine();
        ImGui::Checkbox("Colliders", &prefs.showColliders);
    }
    ImGui::PopStyleVar();

    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    // Letterbox to the chosen screen shape.
    float aspect = 0;
    switch (gameAspect_) {
    case 1: aspect = static_cast<float>(settings_.width) / std::max(settings_.height, 1); break;
    case 2: aspect = 16.0f / 9.0f; break;
    case 3: aspect = 4.0f / 3.0f; break;
    case 4: aspect = 9.0f / 16.0f; break;
    case 5: aspect = 1.0f; break;
    default: break;
    }
    ImVec2 size = avail, pos = origin;
    if (aspect > 0 && avail.x > 1 && avail.y > 1) {
        if (avail.x / avail.y > aspect)
            size.x = avail.y * aspect;
        else
            size.y = avail.x / aspect;
        pos = {origin.x + (avail.x - size.x) * 0.5f, origin.y + (avail.y - size.y) * 0.5f};
        ImGui::GetWindowDrawList()->AddRectFilled(origin, {origin.x + avail.x, origin.y + avail.y}, IM_COL32(12, 13, 16, 255));
    }
    viewportPos_ = {pos.x, pos.y};
    viewportSize_ = {std::max(size.x, 1.0f), std::max(size.y, 1.0f)};
    float scale = ImGui::GetIO().DisplayFramebufferScale.x;
    int w = static_cast<int>(viewportSize_.x * scale), h = static_cast<int>(viewportSize_.y * scale);

    if (playing_) {
        viewportFocused_ = ImGui::IsWindowFocused();
        // The game only sees input while its view is focused and running.
        if (viewportFocused_ && !paused_ && !ImGui::GetIO().WantTextInput)
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
    ImGui::SetCursorScreenPos(pos);
    if (w > 0 && h > 0 && (!playing_ || game_)) {
        device_.beginFrame();
        auto t0 = std::chrono::steady_clock::now();
        if (playing_ && game_) {
            RenderOptions opts;
            game_->render(renderer_, w, h, opts);
        } else {
            RenderOptions opts;
            renderer_.render(*scene_, editorCamera(), w, h, opts);
        }
        renderMs_ = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
        ImTextureID tex = static_cast<ImTextureID>(device_.nativeTexture(renderer_.outputTexture()));
        ImGui::Image(tex, size, {0, 1}, {1, 0});
    } else {
        ImGui::Dummy(size);
    }
    viewportHovered_ = ImGui::IsItemHovered();
    if (!playing_)
        viewportFocused_ = ImGui::IsWindowFocused();
    if (!playing_)
        handleViewportDrop();

    if (playing_ && !paused_ && viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        ImGui::SetWindowFocus();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool editing = !playing_ || paused_;
    if (editing) {
        if (!playing_)
            updateViewportCamera(dt);
        CameraView cam = viewportCamera();
        bool usingGizmo = false, overGizmo = false;
        drawGizmo(cam, pos, size, usingGizmo, overGizmo);
        gizmoWasUsing_ = usingGizmo;

        // 3D view cube: click a face to look from that side.
        if (!playing_ && view3D_) {
            Quat rot = Quat::fromEuler({camPitch_, camYaw_, 0});
            Mat4 view = cam.view;
            float dist = 8.0f;
            if (Entity sel = selected())
                dist = std::max(1.0f, length(scene_->worldPosition(sel) - cam3D_));
            ImGuizmo::ViewManipulate(view.m, dist, {pos.x + size.x - 104, pos.y + 8}, {96, 96}, 0x00000000);
            if (ImGuizmo::IsUsingViewManipulate()) {
                Mat4 camWorld = inverse(view);
                Vec3 forward = normalize(-camWorld.column(2).xyz());
                camPitch_ = degrees(std::asin(std::clamp(forward.y, -1.0f, 1.0f)));
                camYaw_ = degrees(std::atan2(-forward.x, -forward.z));
                cam3D_ = camWorld.column(3).xyz();
            }
            (void)rot;
        }

        // Dragging a selected UI element moves it on screen.
        Entity sel = selected();
        if (sel && scene().registry().has<UIElement>(sel) && viewportHovered_ &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2) && !ImGui::GetIO().KeyAlt) {
            UIRect r = computeUIRect(scene(), sel, viewportSize_.x, viewportSize_.y);
            ImVec2 m = ImGui::GetIO().MouseClickedPos[0];
            Vec2 start{m.x - viewportPos_.x, viewportSize_.y - (m.y - viewportPos_.y)};
            if (r.contains(start) || gizmoWasUsing_) {
                if (playing_)
                    noteLiveChange(sel, "UIElement/offset");
                else
                    edited("Move UI");
                ImVec2 d = ImGui::GetIO().MouseDelta;
                scene().registry().get<UIElement>(sel).offset += Vec2(d.x, -d.y) / r.scale;
                gizmoWasUsing_ = true;
            }
        }

        // Click to select; drag on empty space (2D) to select everything in a box.
        ImGuiIO& io = ImGui::GetIO();
        if (viewportHovered_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !overGizmo && !io.KeyAlt &&
            !ImGuizmo::IsViewManipulateHovered()) {
            Vec2 local{io.MousePos.x - viewportPos_.x, io.MousePos.y - viewportPos_.y};
            boxSelecting_ = cam.orthographic && !pickEntity(scene(), cam, local);
            boxStart_ = {io.MousePos.x, io.MousePos.y};
        }
        if (boxSelecting_) {
            ImVec2 a{boxStart_.x, boxStart_.y}, b = io.MousePos;
            ImVec2 mn{std::min(a.x, b.x), std::min(a.y, b.y)}, mx{std::max(a.x, b.x), std::max(a.y, b.y)};
            Color acc = prefs.accentColor();
            dl->AddRectFilled(mn, mx, toU32(acc, 0.15f));
            dl->AddRect(mn, mx, toU32(acc, 0.9f));
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                boxSelecting_ = false;
                if (mx.x - mn.x > 4 || mx.y - mn.y > 4) {
                    Vec3 w0 = cam.screenToWorld({mn.x - viewportPos_.x, mx.y - viewportPos_.y}, viewportSize_);
                    Vec3 w1 = cam.screenToWorld({mx.x - viewportPos_.x, mn.y - viewportPos_.y}, viewportSize_);
                    if (!io.KeyShift && !io.KeyCtrl)
                        selection_.clear();
                    Scene& s = scene();
                    s.walk([&](Entity e, int) {
                        if (!s.info(e).active)
                            return false;
                        auto& reg = s.registry();
                        if (!reg.has<SpriteRenderer>(e) && !reg.has<TextRenderer>(e) && !reg.has<MeshRenderer>(e))
                            return true;
                        Vec3 p = s.worldPosition(e);
                        if (p.x >= w0.x && p.x <= w1.x && p.y >= w0.y && p.y <= w1.y)
                            addToSelection(e);
                        return true;
                    });
                } else {
                    pickInViewport({b.x - viewportPos_.x, b.y - viewportPos_.y});
                }
            }
        } else if (viewportHovered_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && !usingGizmo && !overGizmo &&
                   !io.KeyAlt && !ImGuizmo::IsUsingViewManipulate()) {
            ImVec2 click = io.MouseClickedPos[0];
            ImVec2 now = io.MousePos;
            if (std::abs(click.x - now.x) < 4 && std::abs(click.y - now.y) < 4)
                pickInViewport({now.x - viewportPos_.x, now.y - viewportPos_.y});
        }
        if (!playing_ && viewportHovered_ && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && selected())
            focusSelected();

        if (!playing_ && prefs.showHints) {
            const char* hint = view3D_ ? "Right-drag: look  |  Right-drag + WASD: fly  |  Alt+drag: orbit  |  F: focus"
                                       : "Right-drag: pan  |  Scroll: zoom  |  Drag: box select  |  W/E/R: move/rotate/scale";
            dl->AddText({pos.x + 10, pos.y + size.y - 24}, IM_COL32(255, 255, 255, 110), hint);
        }
    } else {
        gizmoWasUsing_ = false;
        boxSelecting_ = false;
    }

    if (playing_) {
        dl->AddRectFilled(pos, {pos.x + size.x, pos.y + 3}, paused_ ? IM_COL32(250, 200, 60, 255) : IM_COL32(60, 200, 120, 255));
        const char* hint = paused_ ? "Paused: click objects to inspect and change them, then Keep or Undo your changes"
                           : viewportFocused_ ? "Playing - Ctrl+P or the stop button goes back to editing"
                                              : "Click the game to control it";
        dl->AddText({pos.x + 10, pos.y + size.y - 24}, IM_COL32(255, 255, 255, 140), hint);
        drawLiveChangesBar(pos, size);
        drawErrorBar(pos, size);
    }
    if (showStats_)
        drawStats(pos);
    ImGui::End();
}

void Editor::drawStats(ImVec2 pos) {
    const rhi::FrameStats& st = device_.stats();
    Scene& s = scene();
    char text[512];
    int n = std::snprintf(text, sizeof text, "%.0f FPS  (%.1f ms)\nDraw calls: %u   Triangles: %llu\nObjects: %d\nRender: %.2f ms",
                          ImGui::GetIO().Framerate, 1000.0f / std::max(ImGui::GetIO().Framerate, 1.0f), st.drawCalls,
                          static_cast<unsigned long long>(st.triangles), static_cast<int>(s.registry().aliveCount()), renderMs_);
    if (playing_ && game_) {
        const GameProfile& p = game_->profile();
        std::snprintf(text + n, sizeof text - static_cast<size_t>(n), "\nScripts: %.2f ms  Physics: %.2f ms\nGameplay: %.2f ms",
                      p.scripts, p.physics, p.gameplay);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 ts = ImGui::CalcTextSize(text);
    ImVec2 a{pos.x + 8, pos.y + 8};
    dl->AddRectFilled(a, {a.x + ts.x + 16, a.y + ts.y + 12}, IM_COL32(0, 0, 0, 160), 6);
    dl->AddText({a.x + 8, a.y + 6}, IM_COL32(230, 240, 235, 255), text);
}

} // namespace aven::editor
