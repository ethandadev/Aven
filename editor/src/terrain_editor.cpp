// Terrain tools: the Inspector's sculpt and paint brushes, layers and generators, and the brush
// itself in the Scene view (a ring that follows the ground; hold the left button to use it).

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/render/scene_renderer.h"
#include "aven/scene/terrain.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

namespace {

const char* const kToolNames[] = {"Raise", "Lower", "Smooth", "Flatten", "Paint"};
const char* const kToolTips[] = {
    "Pull the ground up (Shift: push it down)",
    "Push the ground down (Shift: pull it up)",
    "Soften bumps and sharp edges",
    "Level the ground to one height (Ctrl-click the ground to pick the height)",
    "Paint the chosen layer onto the ground",
};

} // namespace

bool Editor::sculptingTerrain() {
    if (terrainTool_ < 0 || playing_ || !view3D_)
        return false;
    Entity e = selected();
    return e && scene().registry().has<Terrain>(e);
}

void Editor::drawTerrainInspector(Entity e) {
    auto& reg = scene().registry();
    auto& t = reg.get<Terrain>(e);
    terrainEnsure(t);
    ImGui::BeginDisabled(playing_);

    // --- tools
    ImGui::SeparatorText("Sculpt and paint");
    float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 4) / 5;
    for (int i = 0; i < 5; ++i) {
        if (i)
            ImGui::SameLine();
        bool on = terrainTool_ == i;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kToolNames[i], {w, 30}))
            terrainTool_ = on ? -1 : i;
        if (on)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", kToolTips[i]);
    }
    if (terrainTool_ >= 0) {
        ImGui::TextDisabled(view3D_ ? "Hold the left button on the ground in the Scene view. Esc stops."
                                    : "Switch the Scene view to 3D to use the brush.");
        ImGui::SetNextItemWidth(-90);
        ImGui::SliderFloat("Size", &brushRadius_, 0.5f, std::max(4.0f, std::max(t.size.x, t.size.y) * 0.25f), "%.1f m");
        ImGui::SetNextItemWidth(-90);
        ImGui::SliderFloat("Strength", &brushStrength_, 0.05f, 1.0f, "%.2f");
        if (terrainTool_ == static_cast<int>(TerrainTool::Flatten)) {
            float meters = flattenLevel_ * t.maxHeight;
            ImGui::SetNextItemWidth(-90);
            if (ImGui::SliderFloat("Height", &meters, 0.0f, t.maxHeight, "%.1f m"))
                flattenLevel_ = meters / std::max(t.maxHeight, 0.01f);
        }
        if (terrainTool_ == static_cast<int>(TerrainTool::Paint)) {
            ImGui::TextDisabled("Paint with:");
            for (int i = 0; i < static_cast<int>(t.layers.size()); ++i) {
                ImGui::SameLine();
                std::string label = t.layers[static_cast<size_t>(i)].texture.empty()
                                        ? "Layer " + std::to_string(i + 1)
                                        : stdfs::path(t.layers[static_cast<size_t>(i)].texture).stem().string();
                if (ImGui::RadioButton((label + "##layer" + std::to_string(i)).c_str(), paintLayer_ == i))
                    paintLayer_ = i;
            }
        }
    }

    // --- whole-terrain actions
    ImGui::SeparatorText("Shape it all at once");
    ImGui::SetNextItemWidth(ui::px(120));
    ImGui::SliderFloat("##hills", &hillsAmount_, 0.05f, 1.0f, "hills %.2f");
    ImGui::SameLine();
    if (ImGui::Button("Make hills")) {
        edited("Make hills");
        terrainGenerateHills(t, hillsAmount_, static_cast<uint32_t>(ImGui::GetTime() * 1000.0));
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("New rolling hills everywhere (different each time)");
    ImGui::SameLine();
    if (ImGui::Button("Flatten all")) {
        edited("Flatten terrain");
        std::fill(t.heights.begin(), t.heights.end(), 0.0f);
        ++t.revision;
    }
    if (ImGui::Button("Paint by shape", {-1, 0})) {
        edited("Paint terrain");
        terrainAutoPaint(t);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Layer 1 on flat ground, layer 2 on slopes, layer 3 on steep sides, layer 4 up high");

    // --- layers
    ImGui::SeparatorText("Layers");
    int remove = -1;
    for (size_t i = 0; i < t.layers.size(); ++i) {
        TerrainLayer& l = t.layers[i];
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%d", static_cast<int>(i + 1));
        ImGui::SameLine();
        float avail = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(avail * 0.5f);
        std::string current = l.texture.empty() ? "(plain color)" : l.texture;
        if (ImGui::BeginCombo("##tex", current.c_str())) {
            if (ImGui::Selectable("(plain color)", l.texture.empty())) {
                edited("Terrain layer");
                l.texture.clear();
            }
            for (auto& f : assetFiles_) {
                std::string ext = fs::extension(f);
                if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
                    continue;
                if (ImGui::Selectable(f.c_str(), f == l.texture)) {
                    edited("Terrain layer");
                    l.texture = f;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                edited("Terrain layer");
                l.texture.assign(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (ImGui::ColorEdit3("##color", &l.color.r, ImGuiColorEditFlags_NoInputs))
            edited("Terrain layer");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 8));
        if (ImGui::DragFloat("##tile", &l.tileSize, 0.1f, 0.25f, 100.0f, "%.1f m"))
            edited("Terrain layer");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How many meters one copy of the texture covers");
        ImGui::SameLine();
        ImGui::BeginDisabled(t.layers.size() <= 1);
        if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0}))
            remove = static_cast<int>(i);
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    if (remove >= 0) {
        edited("Remove terrain layer");
        // Its paint goes to the first layer.
        size_t n = t.splat.size() / 4;
        for (size_t p = 0; p < n; ++p) {
            uint8_t* w = &t.splat[p * 4];
            int moved = w[remove];
            for (int c = remove; c < 3; ++c)
                w[c] = w[c + 1];
            w[3] = 0;
            w[0] = static_cast<uint8_t>(std::min(255, w[0] + moved));
        }
        t.layers.erase(t.layers.begin() + remove);
        paintLayer_ = std::min(paintLayer_, static_cast<int>(t.layers.size()) - 1);
        ++t.revision;
    }
    ImGui::BeginDisabled(t.layers.size() >= 4);
    if (ImGui::SmallButton("+ Layer")) {
        edited("Add terrain layer");
        t.layers.push_back({"", {0.6f, 0.55f, 0.45f, 1}, 4.0f});
        ++t.revision;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("up to 4; drag textures from Assets");
    ImGui::EndDisabled();
}

void Editor::sculptTerrainInViewport(const CameraView& cam, float dt) {
    sculptStroke_ = ImGui::IsMouseDown(ImGuiMouseButton_Left) && sculptStroke_;
    Entity e = selected();
    auto& t = scene().registry().get<Terrain>(e);
    terrainEnsure(t);
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        terrainTool_ = -1;
        return;
    }
    if (!viewportHovered_)
        return;
    // The ground under the mouse, in the terrain's own space.
    Vec2 local{io.MousePos.x - viewportPos_.x, io.MousePos.y - viewportPos_.y};
    Vec3 o, d;
    cam.screenRay(local, viewportSize_, o, d);
    Mat4 world = scene().worldMatrix(e), toLocal = inverse(world);
    Vec3 lo = transformPoint(toLocal, o), ld = transformPoint(toLocal, o + d) - lo;
    Vec3 hit;
    if (!terrainRaycast(t, lo, ld, 5000.0f, hit))
        return;
    // Brush size in the terrain's space (it may be scaled).
    float scaleX = length(world.column(0).xyz());
    float radius = brushRadius_ / std::max(scaleX, 1e-4f);

    // The ring.
    ImDrawList* dl = ImGui::GetWindowDrawList();
    Color accent = prefs.accentColor();
    ImU32 ring = ImGui::ColorConvertFloat4ToU32({accent.r, accent.g, accent.b, 0.95f});
    std::vector<ImVec2> pts;
    for (int i = 0; i <= 48; ++i) {
        float a = static_cast<float>(i) / 48.0f * 6.2831853f;
        Vec3 p{hit.x + std::cos(a) * radius, 0, hit.z + std::sin(a) * radius};
        p.y = terrainHeightAt(t, p.x, p.z) + 0.05f;
        Vec2 s = cam.worldToScreen(transformPoint(world, p), viewportSize_);
        pts.push_back({viewportPos_.x + s.x, viewportPos_.y + s.y});
    }
    dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), ring, 0, 2.0f);
    Vec2 c = cam.worldToScreen(transformPoint(world, {hit.x, hit.y + 0.05f, hit.z}), viewportSize_);
    dl->AddCircleFilled({viewportPos_.x + c.x, viewportPos_.y + c.y}, 3.0f, ring);

    auto tool = static_cast<TerrainTool>(terrainTool_);
    if (tool == TerrainTool::Flatten && io.KeyCtrl) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            flattenLevel_ = hit.y / std::max(t.maxHeight, 0.01f); // pick the height to level to
        return;
    }
    if (io.KeyShift && (tool == TerrainTool::Raise || tool == TerrainTool::Lower))
        tool = tool == TerrainTool::Raise ? TerrainTool::Lower : TerrainTool::Raise;
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        sculptStroke_ = true; // a stroke starts on the ground, not when dragging in from elsewhere
    if (sculptStroke_ && !io.KeyAlt) {
        edited(tool == TerrainTool::Paint ? "Paint terrain" : "Sculpt terrain"); // one undo step per stroke
        // (the layer picked can be gone: removed, or undone, since)
        paintLayer_ = std::clamp(paintLayer_, 0, std::max(0, static_cast<int>(t.layers.size()) - 1));
        terrainBrush(t, tool, hit.x, hit.z, radius, brushStrength_, std::min(dt, 0.05f), paintLayer_, flattenLevel_);
    }
}

} // namespace aven::editor
