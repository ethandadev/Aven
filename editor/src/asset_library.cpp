// The Asset Library: ready-made sprites, animations, backgrounds, 3D textures and 3D models
// (editor/data/library, made from code by tools/library/generate.py and free to use). Click an
// item to place it, drag it into the scene, or just add it to the project; files are copied into
// the project the first time they're used.

#include "editor.h"

#include "rynax/core/fs.h"
#include "rynax/render/renderer3d.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>

namespace rynax::editor {

namespace {

struct Kind {
    const char* id;
    const char* label;
};
const Kind kKinds[] = {{"", "All"},         {"sprite", "Sprites"},    {"animation", "Animations"},
                       {"background", "Backgrounds"}, {"model", "3D models"}, {"texture", "3D textures"}};

std::string lowerText(std::string t) {
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return t;
}

} // namespace

const Json& Editor::library() {
    if (library_.isNull()) {
        auto text = fs::readText(editorDataDir() / "library" / "library.json");
        library_ = text ? Json::parse(*text) : Json::object();
        if (!library_["items"].isArray())
            library_["items"] = Json::array();
    }
    return library_;
}

void Editor::openAssetLibrary() { showLibrary_ = focusLibrary_ = true; }

std::string Editor::addLibraryItem(int index) {
    const Json& items = library()["items"];
    if (index < 0 || index >= static_cast<int>(items.size()) || !hasProject())
        return {};
    const Json& item = items[index];
    std::string dest = item["dest"].asString("");
    stdfs::path src = editorDataDir() / "library" / item["file"].asString("");
    std::error_code ec;
    if (!stdfs::exists(projectDir_ / dest, ec)) {
        stdfs::create_directories((projectDir_ / dest).parent_path(), ec);
        stdfs::copy_file(src, projectDir_ / dest, ec);
        if (ec) {
            notify("Couldn't copy " + item["name"].asString("the item") + ": " + ec.message(), true);
            return {};
        }
        scanAssets();
    }
    return dest;
}

// A texture with the roughness and metalness that suit it; tiled to the object's size.
void Editor::applyLibraryTexture(Entity e, int index, const std::string& path) {
    const Json& item = library()["items"][index];
    auto& mr = scene_->registry().get<MeshRenderer>(e);
    mr.texture = path;
    mr.color = {1, 1, 1, mr.color.a};
    mr.roughness = static_cast<float>(item["roughness"].asNumber(0.8));
    mr.metallic = static_cast<float>(item["metallic"].asNumber(0.0));
    Vec3 s = scene_->transform(e).scale;
    if (mr.mesh == MeshShape::Plane)
        mr.tiling = {std::max(1.0f, std::round(s.x)), std::max(1.0f, std::round(s.z))};
    else if (mr.mesh == MeshShape::Cube)
        mr.tiling = {std::max(1.0f, std::round(s.x)), std::max(1.0f, std::round(s.y))};
}

void Editor::placeLibraryItem(int index, Vec2 local) {
    if (!hasProject() || playing_)
        return;
    const Json& item = library()["items"][index];
    std::string kind = item["kind"].asString("");
    std::string path = addLibraryItem(index);
    if (path.empty())
        return;
    auto& reg = scene_->registry();
    if (kind == "texture") {
        // Onto the object under the mouse, or a new cube.
        Entity e = view3D_ ? pickEntity(*scene_, editorCamera(), local) : Entity{};
        if (e && reg.has<MeshRenderer>(e) && reg.get<MeshRenderer>(e).mesh != MeshShape::Model) {
            recordUndo("Apply texture");
        } else {
            Vec3 at = dropPoint(local);
            e = createEntity("Cube");
            scene_->transform(e).position = {at.x, at.y + (view3D_ ? 0.5f : 0.0f), view3D_ ? at.z : 0};
        }
        applyLibraryTexture(e, index, path);
        select(e);
        notify("Textured " + scene_->info(e).name + " with " + item["name"].asString(""));
        return;
    }
    if (kind == "background") {
        // Fills the camera's view, behind everything.
        Entity cam = SceneRenderer::findCamera(*scene_);
        float height = cam && reg.has<Camera>(cam) ? reg.get<Camera>(cam).size * 2 : 10.0f;
        Vec3 at = cam ? scene_->worldPosition(cam) : Vec3{};
        Entity e = createEntity("Sprite");
        scene_->info(e).name = item["name"].asString("Background");
        auto& sr = reg.get<SpriteRenderer>(e);
        sr.texture = path;
        sr.size = {height * 320.0f / 180.0f, height};
        // Skies go at the very back; hill, mountain and forest layers just in front of them.
        sr.order = item["tags"].asString("").find("layer") != std::string::npos ? -90 : -100;
        sr.pixelArt = false;
        scene_->transform(e).position = {at.x, at.y, 0};
        return;
    }
    if (kind == "animation") {
        // A sprite sheet: one row of frames, played by a Sprite Animator.
        int frames = std::max(1, item["frames"].asInt(1));
        Vec3 at = dropPoint(local);
        Entity e = createEntity("Sprite");
        scene_->info(e).name = item["name"].asString("Animation").substr(0, item["name"].asString("").find(" ("));
        auto& sr = reg.get<SpriteRenderer>(e);
        sr.texture = path;
        sr.columns = frames;
        sr.rows = 1;
        sr.size = {1, 1};
        auto& anim = reg.emplace<SpriteAnimator>(e);
        anim.lastFrame = frames - 1;
        anim.fps = frames > 2 ? 8.0f : 4.0f;
        scene_->transform(e).position = {at.x, at.y, view3D_ ? at.z : 0};
        return;
    }
    placeAsset(path, local);
    Entity e = selected();
    if (!e)
        return;
    scene_->info(e).name = item["name"].asString(scene_->info(e).name);
    if (kind == "model" && libraryColliders_) {
        // A collider that fits the model, so the player can't walk through it.
        Vec3 mn, mx;
        scene_->updateTransforms(); // bounds are measured where it now stands
        if (renderer_.renderer3D().worldBounds(*scene_, e, mn, mx)) {
            Vec3 pos = scene_->worldPosition(e);
            auto& box = reg.getOrEmplace<BoxCollider>(e);
            box.size = mx - mn;
            box.offset = (mn + mx) * 0.5f - pos;
        }
    }
}

void Editor::drawAssetLibrary() {
    ui::placeWindow({780, 600}, {0.5f, 0.5f});
    if (focusLibrary_) {
        ImGui::SetNextWindowFocus();
        focusLibrary_ = false;
    }
    if (!ImGui::Begin("Asset Library###AssetLibrary", &showLibrary_)) {
        ImGui::End();
        return;
    }
    const Json& items = library()["items"];
    for (int k = 0; k < static_cast<int>(std::size(kKinds)); ++k) {
        if (k)
            ImGui::SameLine();
        bool on = libraryKind_ == k;
        if (on)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(kKinds[k].label))
            libraryKind_ = k;
        if (on)
            ImGui::PopStyleColor();
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 210);
    ImGui::InputTextWithHint("##libsearch", "Search: tree, stone, enemy, pickup...", &libraryFilter_);
    ImGui::SameLine();
    ImGui::Checkbox("Colliders on models", &libraryColliders_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Placed 3D models get a Box Collider that fits them, so things bump into them.");
    ImGui::Separator();

    ImGui::BeginChild("##libgrid", {0, -ImGui::GetFrameHeightWithSpacing()});
    const float cell = 118, thumb = 96;
    int perRow = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + ImGui::GetStyle().ItemSpacing.x)));
    std::string needle = lowerText(libraryFilter_);
    int shown = 0;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        const Json& item = items[i];
        std::string kind = item["kind"].asString("");
        if (libraryKind_ > 0 && kind != kKinds[libraryKind_].id)
            continue;
        std::string name = item["name"].asString("");
        if (!needle.empty() && lowerText(name + " " + item["tags"].asString("") + " " + kind).find(needle) == std::string::npos)
            continue;
        if (shown++ % perRow)
            ImGui::SameLine();
        ImGui::PushID(i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        // One button covers the tile; the picture and name are drawn over it, so it stays the last item
        // (a group here has no ID, and a drag that starts off it would trip ImGui's drag-source check).
        ImGui::InvisibleButton("##item", {cell, cell + ImGui::GetTextLineHeight() + 6});
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + cell, p.y + cell}, ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg), 6);
        // The picture: the model's rendering, or the image itself (pixel art stays crisp).
        std::string picture = kind == "model" ? item["thumb"].asString("") : item["file"].asString("");
        if (!picture.empty()) {
            const TextureAsset& tex = assets_.texture((editorDataDir() / "library" / picture).string(), kind == "sprite" || kind == "animation");
            if (!tex.missing && tex.width > 0) {
                float w = static_cast<float>(tex.width), h = static_cast<float>(tex.height);
                ImVec2 uv0{0, 1}, uv1{1, 0};
                if (kind == "animation") {
                    int frames = std::max(1, item["frames"].asInt(1));
                    w /= static_cast<float>(frames);
                    uv1.x = 1.0f / static_cast<float>(frames);
                }
                float k = thumb / std::max(w, h);
                ImVec2 size{w * k, h * k};
                ImVec2 at{p.x + (cell - size.x) * 0.5f, p.y + (cell - size.y) * 0.5f};
                dl->AddImageRounded(static_cast<ImTextureID>(device_.nativeTexture(tex.handle)), at, {at.x + size.x, at.y + size.y},
                                    uv0, uv1, IM_COL32(255, 255, 255, 255), kind == "model" ? 4.0f : 0.0f);
            }
        }
        std::string label = name;
        while (label.size() > 3 && ImGui::CalcTextSize(label.c_str()).x > cell)
            label = label.substr(0, label.size() - 4) + "...";
        dl->AddText({p.x + (cell - ImGui::CalcTextSize(label.c_str()).x) * 0.5f, p.y + cell + 3},
                    ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        if (hovered)
            ImGui::SetTooltip("%s\n%s\n\nClick to place it, or drag it into the scene.\nRight-click for more.", name.c_str(),
                              item["tags"].asString("").c_str());
        bool canPlace = hasProject() && !playing_;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && canPlace)
            placeLibraryItem(i, {viewportSize_.x * 0.5f, viewportSize_.y * 0.5f});
        if (canPlace && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("LIBRARY_ITEM", &i, sizeof i);
            ImGui::TextUnformatted(name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem("##libmenu")) {
            ImGui::TextDisabled("%s", name.c_str());
            if (ImGui::MenuItem("Place in the scene", nullptr, false, canPlace))
                placeLibraryItem(i, {viewportSize_.x * 0.5f, viewportSize_.y * 0.5f});
            // Every selected shape (3D mesh, not an imported model) takes the texture.
            std::vector<Entity> texturable;
            for (Entity x : selectedEntities())
                if (auto* mr = scene_->registry().tryGet<MeshRenderer>(x); mr && mr->mesh != MeshShape::Model)
                    texturable.push_back(x);
            const char* applyLabel = texturable.size() > 1 ? "Apply to the selected objects" : "Apply to the selected object";
            if (kind == "texture" && ImGui::MenuItem(applyLabel, nullptr, false, canPlace && !texturable.empty())) {
                std::string path = addLibraryItem(i);
                if (!path.empty()) {
                    recordUndo("Apply texture");
                    for (Entity x : texturable)
                        applyLibraryTexture(x, i, path);
                }
            }
            if (ImGui::MenuItem("Add to the project only", nullptr, false, canPlace)) {
                std::string path = addLibraryItem(i);
                if (!path.empty())
                    notify("Added " + path);
            }
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (shown == 0)
        ImGui::TextDisabled(items.size() ? "Nothing matches." : "The asset library wasn't found next to the editor (data/library).");
    ImGui::EndChild();
    ImGui::TextDisabled("%d items. All free to use in any game (CC0). Files are copied into your project when used.",
                        static_cast<int>(items.size()));
    ImGui::End();
}

} // namespace rynax::editor
