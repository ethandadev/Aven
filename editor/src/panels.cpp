#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"
#include "aven/script/vm.h"
#include "block_editor.h"
#include "code_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aven::editor {

// ---------------------------------------------------------------- helpers

namespace ui {

void panelClass() {
    // Docked panels close from their tab; hide the extra close button on the dock node.
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
    ImGui::SetNextWindowClass(&wc);
}

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(320);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void sectionHeader(const char* text) {
    ImGui::Spacing();
    ImGui::TextDisabled("%s", text);
    ImGui::Separator();
}

bool iconButton(const char* id, int icon, const char* tooltip, bool active, float size) {
    if (size <= 0)
        size = ImGui::GetFrameHeight();
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton("##btn", {size, size});
    bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 bg = active ? ImGui::GetColorU32(ImGuiCol_SliderGrab) : ImGui::GetColorU32(hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    dl->AddRectFilled(p, {p.x + size, p.y + size}, bg, ImGui::GetStyle().FrameRounding);
    ImU32 fg = active ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_Text);
    float c = size * 0.5f, s = size * 0.26f;
    ImVec2 m{p.x + c, p.y + c};
    switch (icon) {
    case Play: dl->AddTriangleFilled({m.x - s * 0.7f, m.y - s}, {m.x - s * 0.7f, m.y + s}, {m.x + s, m.y}, IM_COL32(90, 220, 140, 255)); break;
    case Stop: dl->AddRectFilled({m.x - s * 0.8f, m.y - s * 0.8f}, {m.x + s * 0.8f, m.y + s * 0.8f}, IM_COL32(245, 100, 100, 255), 2); break;
    case Pause:
        dl->AddRectFilled({m.x - s * 0.8f, m.y - s}, {m.x - s * 0.25f, m.y + s}, fg);
        dl->AddRectFilled({m.x + s * 0.25f, m.y - s}, {m.x + s * 0.8f, m.y + s}, fg);
        break;
    case Step:
        dl->AddTriangleFilled({m.x - s, m.y - s}, {m.x - s, m.y + s}, {m.x + s * 0.4f, m.y}, fg);
        dl->AddRectFilled({m.x + s * 0.5f, m.y - s}, {m.x + s, m.y + s}, fg);
        break;
    case Move:
        dl->AddLine({m.x - s, m.y}, {m.x + s, m.y}, fg, 2);
        dl->AddLine({m.x, m.y - s}, {m.x, m.y + s}, fg, 2);
        dl->AddTriangleFilled({m.x + s + 3, m.y}, {m.x + s - 2, m.y - 4}, {m.x + s - 2, m.y + 4}, fg);
        dl->AddTriangleFilled({m.x, m.y - s - 3}, {m.x - 4, m.y - s + 2}, {m.x + 4, m.y - s + 2}, fg);
        break;
    case Rotate:
        dl->PathArcTo(m, s, 0.3f, 5.2f, 16);
        dl->PathStroke(fg, 0, 2);
        dl->AddTriangleFilled({m.x + s * 0.85f + 4, m.y - s * 0.2f}, {m.x + s * 0.85f - 4, m.y - s * 0.2f}, {m.x + s * 0.85f, m.y + 4}, fg);
        break;
    case Scale:
        dl->AddRect({m.x - s, m.y - s * 0.2f}, {m.x + s * 0.2f, m.y + s}, fg, 0, 0, 2);
        dl->AddLine({m.x - s * 0.2f, m.y + s * 0.2f}, {m.x + s, m.y - s}, fg, 2);
        dl->AddTriangleFilled({m.x + s + 1, m.y - s - 1}, {m.x + s - 5, m.y - s}, {m.x + s, m.y - s + 5}, fg);
        break;
    case Grid:
        for (int i = -1; i <= 1; ++i) {
            dl->AddLine({m.x - s, m.y + i * s * 0.66f}, {m.x + s, m.y + i * s * 0.66f}, fg, 1.5f);
            dl->AddLine({m.x + i * s * 0.66f, m.y - s}, {m.x + i * s * 0.66f, m.y + s}, fg, 1.5f);
        }
        break;
    case Magnet:
        dl->PathArcTo({m.x, m.y + s * 0.1f}, s * 0.8f, 0.0f, 3.14159f, 12);
        dl->PathStroke(fg, 0, 3);
        dl->AddLine({m.x - s * 0.8f, m.y + s * 0.1f}, {m.x - s * 0.8f, m.y - s}, fg, 3);
        dl->AddLine({m.x + s * 0.8f, m.y + s * 0.1f}, {m.x + s * 0.8f, m.y - s}, fg, 3);
        break;
    default: dl->AddCircleFilled(m, s * 0.6f, fg); break;
    }
    if (hovered && tooltip)
        ImGui::SetTooltip("%s", tooltip);
    ImGui::PopID();
    return clicked;
}

bool assetField(const char* label, std::string& value, const std::vector<std::string>& options, const char* dragType) {
    bool changed = false;
    ImGui::PushID(label);
    std::string preview = value.empty() ? "(none)" : value;
    if (ImGui::BeginCombo("##asset", preview.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("(none)", value.empty())) {
            value.clear();
            changed = true;
        }
        for (auto& o : options)
            if (ImGui::Selectable(o.c_str(), o == value)) {
                value = o;
                changed = true;
            }
        if (options.empty())
            ImGui::TextDisabled("No matching files yet. Drag files into the window to add them.");
        ImGui::EndCombo();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(dragType)) {
            value.assign(static_cast<const char*>(p->Data), static_cast<size_t>(p->DataSize));
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return changed;
}

} // namespace ui

namespace {

std::vector<std::string> extensionsFor(AssetKind kind) {
    switch (kind) {
    case AssetKind::Image: return {".png", ".jpg", ".jpeg", ".bmp", ".tga"};
    case AssetKind::Model: return {".gltf", ".glb"};
    case AssetKind::Audio: return {".wav", ".mp3", ".ogg", ".flac"};
    case AssetKind::Script: return {".es", ".blocks"};
    case AssetKind::Font: return {".ttf", ".otf"};
    case AssetKind::Prefab: return {".prefab"};
    case AssetKind::Scene: return {".scene"};
    default: return {};
    }
}

// Colored dot shown next to objects in the hierarchy.
ImU32 entityColor(Registry& reg, Entity e) {
    if (reg.has<Camera>(e)) return IM_COL32(120, 190, 255, 255);
    if (reg.has<Light>(e)) return IM_COL32(255, 210, 90, 255);
    if (reg.has<UIElement>(e)) return IM_COL32(120, 230, 170, 255);
    if (reg.has<MeshRenderer>(e)) return IM_COL32(180, 150, 255, 255);
    if (reg.has<SpriteRenderer>(e)) {
        Color c = reg.get<SpriteRenderer>(e).color;
        return IM_COL32(static_cast<int>(c.r * 255), static_cast<int>(c.g * 255), static_cast<int>(c.b * 255), 255);
    }
    if (reg.has<Script>(e)) return IM_COL32(255, 140, 90, 255);
    return IM_COL32(150, 155, 165, 255);
}

} // namespace

// ---------------------------------------------------------------- hierarchy

void Editor::drawEntityNode(Entity e) {
    Scene& s = scene();
    auto& reg = s.registry();
    EntityInfo& info = s.info(e);
    const auto& kids = s.children(e);
    std::string filter = hierarchyFilter_;
    bool matches = filter.empty();
    if (!matches) {
        std::string a = info.name, b = filter;
        std::transform(a.begin(), a.end(), a.begin(), ::tolower);
        std::transform(b.begin(), b.end(), b.begin(), ::tolower);
        matches = a.find(b) != std::string::npos;
    }
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding;
    if (kids.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (selected() == e)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (!filter.empty())
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushID(static_cast<int>(e.index));
    bool dim = !s.isActive(e);
    if (dim)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = true;
    if (matches || !filter.empty()) {
        ImVec2 p = ImGui::GetCursorScreenPos();
        open = ImGui::TreeNodeEx("##node", flags, "     %s", info.name.c_str());
        float h = ImGui::GetFrameHeight();
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + ImGui::GetTreeNodeToLabelSpacing() + 6, p.y + h * 0.5f}, 4.5f,
                                                    entityColor(reg, e));
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            select(e);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            focusSelected();
        if (!playing_) {
            if (ImGui::BeginDragDropSource()) {
                uint64_t id = info.uuid.value;
                ImGui::SetDragDropPayload("ENTITY", &id, sizeof id);
                ImGui::Text("%s", info.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
                    Entity dragged = s.findByUUID({*static_cast<const uint64_t*>(pl->Data)});
                    if (dragged && dragged != e && !s.isAncestor(dragged, e)) {
                        recordUndo("Change parent");
                        s.setParent(dragged, e);
                    }
                }
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_PATH")) {
                    std::string path(static_cast<const char*>(pl->Data), static_cast<size_t>(pl->DataSize));
                    std::string ext = fs::extension(path);
                    if (ext == ".es" || ext == ".blocks")
                        attachScript(e, path);
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem()) {
                select(e);
                if (ImGui::MenuItem("Duplicate", "Ctrl+D")) {
                    recordUndo("Duplicate");
                    select(s.duplicate(e));
                }
                if (ImGui::MenuItem("Delete", "Del")) {
                    recordUndo("Delete");
                    s.destroy(e);
                    selection_.clear();
                    ImGui::EndPopup();
                    if (dim)
                        ImGui::PopStyleColor();
                    if (open)
                        ImGui::TreePop();
                    ImGui::PopID();
                    return;
                }
                if (ImGui::BeginMenu("Add child")) {
                    for (const char* k : {"Entity", "Square", "Circle", "Text", "Cube", "Sphere", "Point Light", "Particles", "UI Text", "UI Button"})
                        if (ImGui::MenuItem(k))
                            createEntity(k, e);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Move up") && s.siblingIndex(e) > 0) {
                    recordUndo("Reorder");
                    s.setParent(e, s.parent(e), false, s.siblingIndex(e) - 1);
                }
                if (ImGui::MenuItem("Move down")) {
                    recordUndo("Reorder");
                    s.setParent(e, s.parent(e), false, s.siblingIndex(e) + 1);
                }
                if (s.parent(e) && ImGui::MenuItem("Move out of parent")) {
                    recordUndo("Change parent");
                    s.setParent(e, {});
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save as Prefab"))
                    savePrefab(e);
                if (ImGui::MenuItem("Add blocks script")) {
                    std::string path = newScriptFile(info.name, true);
                    attachScript(e, path);
                    openBlocks(path);
                }
                if (ImGui::MenuItem("Add EasyScript")) {
                    std::string path = newScriptFile(info.name, false);
                    attachScript(e, path);
                    openScript(path);
                }
                ImGui::EndPopup();
            }
        }
    }
    if (dim)
        ImGui::PopStyleColor();
    if (open) {
        std::vector<Entity> copy = kids;
        for (Entity c : copy)
            if (s.valid(c))
                drawEntityNode(c);
        if (matches || !filter.empty())
            ImGui::TreePop();
    }
    ImGui::PopID();
}

void Editor::drawHierarchy() {
    ui::panelClass();
    ImGui::Begin("Hierarchy", &showHierarchy_);
    hierarchyFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    ImGui::SetNextItemWidth(-60);
    ImGui::InputTextWithHint("##filter", "Search...", &hierarchyFilter_);
    ImGui::SameLine();
    ImGui::BeginDisabled(playing_);
    if (ImGui::Button("+ Add"))
        ImGui::OpenPopup("add_object");
    ImGui::EndDisabled();
    if (ImGui::BeginPopup("add_object")) {
        ImGui::TextDisabled("2D");
        for (const char* k : {"Square", "Circle", "Triangle", "Star", "Heart", "Sprite", "Text"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        ImGui::Separator();
        ImGui::TextDisabled("3D");
        for (const char* k : {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Player 3D", "Sun", "Point Light", "Spot Light"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        ImGui::Separator();
        ImGui::TextDisabled("Other");
        for (const char* k : {"Camera", "Particles", "Sound", "UI Text", "UI Button", "UI Panel", "Entity"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        ImGui::EndPopup();
    }
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
    Scene& s = scene();
    std::vector<Entity> roots = s.roots();
    for (Entity e : roots)
        if (s.valid(e))
            drawEntityNode(e);
    ImGui::PopStyleVar();
    // Dropping on empty space moves an object to the top level.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (!playing_ && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
            Entity dragged = s.findByUUID({*static_cast<const uint64_t*>(pl->Data)});
            if (dragged) {
                recordUndo("Change parent");
                s.setParent(dragged, {});
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked())
        selection_.clear();
    if (s.roots().empty())
        ImGui::TextDisabled("This scene is empty.\nClick '+ Add' to add objects.");
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- inspector

bool Editor::drawComponent(Entity e, const ComponentInfo& info, void* data) {
    bool changed = false;
    bool twoD = !view3D_;
    for (auto& f : info.fields) {
        if (f.options.runtime)
            continue;
        if (f.options.advanced && !advanced())
            continue;
        ImGui::PushID(f.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        std::string label = f.label;
        // In 2D, show rotation as a single "Angle".
        bool transform2D = twoD && info.name == "Transform";
        if (transform2D && f.name == "rotation")
            label = "Angle";
        ImGui::TextUnformatted(label.c_str());
        if (f.options.tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", f.options.tooltip);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        bool c = false;
        switch (f.type) {
        case FieldType::Bool: c = ImGui::Checkbox("##v", &f.ref<bool>(data)); break;
        case FieldType::Int: c = ImGui::DragInt("##v", &f.ref<int>(data), 0.1f); break;
        case FieldType::Float:
            if (f.options.max > f.options.min)
                c = ImGui::SliderFloat("##v", &f.ref<float>(data), f.options.min, f.options.max, "%.2f");
            else
                c = ImGui::DragFloat("##v", &f.ref<float>(data), f.options.step, 0, 0, "%.2f");
            break;
        case FieldType::Vec2: c = ImGui::DragFloat2("##v", &f.ref<Vec2>(data).x, f.options.step, 0, 0, "%.2f"); break;
        case FieldType::Vec3: {
            Vec3& v = f.ref<Vec3>(data);
            if (transform2D && f.name == "rotation")
                c = ImGui::DragFloat("##v", &v.z, 0.5f, 0, 0, "%.1f°");
            else if (transform2D && !advanced())
                c = ImGui::DragFloat2("##v", &v.x, f.options.step, 0, 0, "%.2f");
            else
                c = ImGui::DragFloat3("##v", &v.x, f.options.step, 0, 0, "%.2f");
            break;
        }
        case FieldType::Color: c = ImGui::ColorEdit4("##v", &f.ref<Color>(data).r, ImGuiColorEditFlags_AlphaBar); break;
        case FieldType::String:
            if (f.options.multiline)
                c = ImGui::InputTextMultiline("##v", &f.ref<std::string>(data), {-1, ImGui::GetTextLineHeight() * 3});
            else
                c = ImGui::InputText("##v", &f.ref<std::string>(data));
            break;
        case FieldType::Asset: {
            std::string& value = f.ref<std::string>(data);
            c = ui::assetField("##v", value, projectFiles(extensionsFor(f.options.asset)), "ASSET_PATH");
            // Picking an image gives the sprite the image's shape.
            if (c && info.name == "SpriteRenderer" && f.name == "texture" && !value.empty()) {
                auto& sr = *static_cast<SpriteRenderer*>(data);
                const TextureAsset& t = assets_.texture(value, sr.pixelArt);
                if (t.height > 0 && !t.missing)
                    sr.size = {sr.size.y * t.width / t.height, sr.size.y};
            }
            break;
        }
        case FieldType::Enum: {
            int32_t& v = f.ref<int32_t>(data);
            const auto& names = f.options.enumNames;
            const char* current = v >= 0 && v < static_cast<int32_t>(names.size()) ? names[static_cast<size_t>(v)].c_str() : "?";
            if (ImGui::BeginCombo("##v", current)) {
                for (size_t i = 0; i < names.size(); ++i)
                    if (ImGui::Selectable(names[i].c_str(), static_cast<int32_t>(i) == v)) {
                        v = static_cast<int32_t>(i);
                        c = true;
                    }
                ImGui::EndCombo();
            }
            break;
        }
        case FieldType::EntityRef: {
            UUID& id = f.ref<UUID>(data);
            Entity target = scene().findByUUID(id);
            std::string current = target ? scene().info(target).name : "(none)";
            if (ImGui::BeginCombo("##v", current.c_str())) {
                if (ImGui::Selectable("(none)", !target)) {
                    id = {};
                    c = true;
                }
                scene().walk([&](Entity x, int depth) {
                    if (x == e)
                        return true;
                    std::string name = std::string(static_cast<size_t>(depth) * 2, ' ') + scene().info(x).name;
                    ImGui::PushID(static_cast<int>(x.index));
                    if (ImGui::Selectable(name.c_str(), x == target)) {
                        id = scene().info(x).uuid;
                        c = true;
                    }
                    ImGui::PopID();
                    return true;
                });
                ImGui::EndCombo();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
                    id = {*static_cast<const uint64_t*>(pl->Data)};
                    c = true;
                }
                ImGui::EndDragDropTarget();
            }
            break;
        }
        }
        if (c) {
            changed = true;
            if (!playing_)
                edited("Change " + f.label);
        }
        ImGui::PopID();
    }
    return changed;
}

void Editor::drawScriptVariables(Entity e) {
    auto* sc = scene().registry().tryGet<Script>(e);
    if (!sc || sc->path.empty())
        return;
    auto text = fs::readText(projectDir_ / sc->path);
    if (!text)
        return;
    std::string source = *text;
    if (fs::extension(sc->path) == ".blocks")
        source = blocks::compileFile(source);
    script::VM vm;
    vm.onError = [](const script::ScriptError&) {};
    auto mod = vm.compile(source, sc->path);
    if (!mod) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored({1, 0.45f, 0.45f, 1}, "This script has an error.");
        return;
    }
    // While playing, show the live values of this object's variables.
    std::shared_ptr<script::Instance> live = playing_ ? game_->scripts().instanceOf(e) : nullptr;
    for (auto& ex : mod->exports) {
        if (ex.name == "is_clone") // set by clone(), not something to edit
            continue;
        ImGui::PushID(ex.name.c_str());
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        bool overridden = sc->overrides.contains(ex.name);
        ImGui::TextColored(overridden ? ImVec4(1, 0.8f, 0.4f, 1) : ImGui::GetStyleColorVec4(ImGuiCol_Text), "%s", toLabel(ex.name).c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", ex.comment.empty() ? "A variable from the script." : ex.comment.c_str(),
                              overridden ? "\n(Changed for this object only. Right-click to reset.)" : "");
        if (overridden && ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Reset to the script's value")) {
                edited("Reset variable");
                sc->overrides.erase(ex.name);
            }
            ImGui::EndPopup();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        script::Value value = overridden ? script::VM::fromJson(sc->overrides[ex.name]) : ex.defaultValue;
        if (live)
            if (script::Value* v = live->find(script::intern(ex.name)))
                value = *v;
        bool c = false;
        script::Value result = value;
        if (value.isNumber()) {
            float f = static_cast<float>(value.number());
            if ((c = ImGui::DragFloat("##v", &f, 0.1f)))
                result = script::Value(static_cast<double>(f));
        } else if (value.isBool()) {
            bool b = value.boolean();
            if ((c = ImGui::Checkbox("##v", &b)))
                result = script::Value(b);
        } else if (value.isString()) {
            std::string s = value.string();
            if ((c = ImGui::InputText("##v", &s)))
                result = script::Value(s);
        } else if (value.isVec() && value.vecObj().isColor) {
            auto& o = value.vecObj();
            float col[4] = {static_cast<float>(o.v[0]), static_cast<float>(o.v[1]), static_cast<float>(o.v[2]), static_cast<float>(o.v[3])};
            if ((c = ImGui::ColorEdit4("##v", col)))
                result = script::Value::color(col[0], col[1], col[2], col[3]);
        } else if (value.isVec()) {
            auto& o = value.vecObj();
            float v[3] = {static_cast<float>(o.v[0]), static_cast<float>(o.v[1]), static_cast<float>(o.v[2])};
            if ((c = (o.components == 2 ? ImGui::DragFloat2("##v", v, 0.1f) : ImGui::DragFloat3("##v", v, 0.1f))))
                result = script::Value::vec(v[0], v[1], v[2], o.components);
        } else {
            ImGui::TextDisabled("%s", value.repr().c_str());
        }
        if (c) {
            if (live) {
                live->set(script::intern(ex.name), result);
            } else {
                edited("Change " + ex.name);
                sc->overrides[ex.name] = script::VM::toJson(result);
            }
        }
        ImGui::PopID();
    }
}

bool Editor::componentUnlocked(const ComponentInfo& info) const {
    const std::string& c = info.category;
    if (c == "Physics 2D" || c == "Physics 3D")
        return unlocked(Feature::Physics);
    if (c == "Audio")
        return unlocked(Feature::Audio);
    if (c == "Effects")
        return unlocked(Feature::Particles);
    if (c == "UI")
        return unlocked(Feature::UI);
    if (info.name == "Light" || info.name == "Environment")
        return unlocked(Feature::Lighting);
    if (info.name == "PostProcessing")
        return unlocked(Feature::PostProcessing);
    if (info.name == "SpriteAnimator")
        return unlocked(Feature::Animation);
    return true;
}

void Editor::drawAddComponent(Entity e) {
    static std::string filter;
    ImGui::Spacing();
    float w = ImGui::GetContentRegionAvail().x;
    if (ImGui::Button("+ Add Component", {w, 30})) {
        filter.clear();
        ImGui::OpenPopup("add_component");
    }
    ImGui::SetNextWindowSize({w, 420});
    if (ImGui::BeginPopup("add_component")) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##search", "Search components...", &filter);
        std::string lastCategory;
        auto& reg = scene().registry();
        for (auto& info : ComponentRegistry::all()) {
            if (info.get(reg, e) || (info.advanced && !advanced()) || !componentUnlocked(info))
                continue;
            std::string hay = info.name + " " + info.description + " " + info.category;
            std::string needle = filter;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            if (!needle.empty() && hay.find(needle) == std::string::npos)
                continue;
            if (info.category != lastCategory) {
                ImGui::TextDisabled("%s", info.category.c_str());
                lastCategory = info.category;
            }
            if (ImGui::Selectable(("   " + info.name).c_str())) {
                recordUndo("Add " + info.name);
                info.add(reg, e);
                if (info.name == "Script") {
                    // A new script is ready to edit straight away.
                    std::string path = newScriptFile(scene().info(e).name, !advanced());
                    reg.get<Script>(e).path = path;
                    fs::extension(path) == ".blocks" ? openBlocks(path) : openScript(path);
                }
                if (info.name == "BoxCollider2D")
                    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                        reg.get<BoxCollider2D>(e).size = sr->size;
                if (info.name == "CircleCollider2D")
                    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
                        reg.get<CircleCollider2D>(e).radius = std::max(sr->size.x, sr->size.y) * 0.5f;
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", info.description.c_str());
        }
        ImGui::EndPopup();
    }
}

void Editor::drawInspector() {
    ui::panelClass();
    ImGui::Begin("Inspector", &showInspector_);
    Entity e = selected();
    Scene& s = scene();
    if (!e) {
        ImGui::TextDisabled("Select an object to see and change its settings.");
        ImGui::Spacing();
        ImGui::TextWrapped("Tip: click objects in the Scene view or the Hierarchy. Every object is made of components: "
                           "a Transform (where it is), plus things like a SpriteRenderer (how it looks) or a Script "
                           "(what it does).");
        ImGui::End();
        return;
    }
    EntityInfo& info = s.info(e);
    if (ImGui::Checkbox("##active", &info.active))
        edited("Toggle active");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Active: turn off to hide this object and stop its scripts.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::PushFont(fonts.bold);
    if (ImGui::InputText("##name", &info.name))
        edited("Rename");
    ImGui::PopFont();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Tag");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##tag", "e.g. enemy, coin, player", &info.tag))
        edited("Change tag");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tags group objects: find_all(\"enemy\"), is_touching(\"coin\")...");
    if (auto* pi = s.registry().tryGet<PrefabInstance>(e))
        ImGui::TextDisabled("From prefab: %s", pi->path.c_str());
    ImGui::Separator();

    auto& reg = s.registry();
    for (auto& ci : ComponentRegistry::all()) {
        void* data = ci.get(reg, e);
        if (!data)
            continue;
        if (ci.advanced && !advanced() && ci.name != "PrefabInstance" && ci.name != "PostProcessing")
            continue;
        ImGui::PushID(ci.name.c_str());
        bool keep = true;
        bool open = ImGui::CollapsingHeader(ci.name.c_str(), ci.removable && !playing_ ? &keep : nullptr,
                                            ImGuiTreeNodeFlags_DefaultOpen);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", ci.description.c_str());
        if (!keep) {
            recordUndo("Remove " + ci.name);
            ci.remove(reg, e);
            ImGui::PopID();
            continue;
        }
        if (open) {
            if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 110);
                ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                drawComponent(e, ci, data);
                if (ci.name == "Script")
                    drawScriptVariables(e);
                ImGui::EndTable();
            }
            if (ci.name == "Script") {
                auto& sc = reg.get<Script>(e);
                if (!sc.path.empty()) {
                    bool isBlocks = fs::extension(sc.path) == ".blocks";
                    if (ImGui::Button(isBlocks ? "Edit blocks" : "Edit script", {ImGui::GetContentRegionAvail().x * 0.5f - 4, 0}))
                        openScript(sc.path);
                    ImGui::SameLine();
                }
                if (ImGui::Button("New script...", {-1, 0}))
                    ImGui::OpenPopup("newscript");
                if (ImGui::BeginPopup("newscript")) {
                    if (ImGui::MenuItem("Blocks (drag and drop)")) {
                        std::string path = newScriptFile(info.name, true);
                        edited("New script");
                        sc.path = path;
                        openBlocks(path);
                    }
                    if (ImGui::MenuItem("EasyScript (type code)")) {
                        std::string path = newScriptFile(info.name, false);
                        edited("New script");
                        sc.path = path;
                        openScript(path);
                    }
                    ImGui::EndPopup();
                }
            }
        }
        ImGui::PopID();
    }
    if (!playing_)
        drawAddComponent(e);
    ImGui::End();
}

// ---------------------------------------------------------------- assets

void Editor::drawAssets() {
    ui::panelClass();
    ImGui::Begin("Assets", &showAssets_);
    // Breadcrumbs
    if (ImGui::SmallButton("Project"))
        assetFolder_.clear();
    std::string built;
    size_t start = 0;
    while (start < assetFolder_.size()) {
        size_t slash = assetFolder_.find('/', start);
        std::string part = assetFolder_.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        built += (built.empty() ? "" : "/") + part;
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        if (ImGui::SmallButton(part.c_str()))
            assetFolder_ = built;
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 90);
    if (ImGui::SmallButton("+ Create"))
        ImGui::OpenPopup("create_asset");
    if (ImGui::BeginPopup("create_asset")) {
        std::string folder = assetFolder_.empty() ? "" : assetFolder_ + "/";
        if (ImGui::MenuItem("Blocks script")) {
            std::string p = newScriptFile("new_blocks", true);
            openBlocks(p);
        }
        if (ImGui::MenuItem("EasyScript")) {
            std::string p = newScriptFile("new_script", false);
            openScript(p);
        }
        if (ImGui::MenuItem("Scene (2D)"))
            newScene(false);
        if (ImGui::MenuItem("Scene (3D)"))
            newScene(true);
        if (ImGui::MenuItem("Folder")) {
            std::string p = uniqueName(assetFolder_.empty() ? "." : assetFolder_, "new_folder", "");
            std::error_code ec;
            stdfs::create_directories(projectDir_ / p, ec);
            scanAssets();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();

    ImGui::BeginChild("##files");
    std::error_code ec;
    stdfs::path dir = projectDir_ / assetFolder_;
    std::vector<stdfs::directory_entry> entries;
    for (auto& entry : stdfs::directory_iterator(dir, ec)) {
        std::string name = entry.path().filename().string();
        if (name.empty() || name[0] == '.' || name == ProjectSettings::kFileName || name == "tutorial.json")
            continue;
        entries.push_back(entry);
    }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory();
        return a.path().filename() < b.path().filename();
    });
    float cell = 92;
    int columns = std::max(1, static_cast<int>(ImGui::GetContentRegionAvail().x / (cell + 8)));
    int i = 0;
    std::string pendingDelete;
    for (auto& entry : entries) {
        std::string name = entry.path().filename().string();
        std::string rel = fs::relativePath(entry.path(), projectDir_);
        std::string ext = fs::extension(entry.path());
        if (i++ % columns)
            ImGui::SameLine();
        ImGui::PushID(rel.c_str());
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool isSelected = selectedAsset_ == rel;
        ImGui::InvisibleButton("##item", {cell, cell});
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, {p.x + cell, p.y + cell}, isSelected ? ImGui::GetColorU32(ImGuiCol_Header) : hovered ? ImGui::GetColorU32(ImGuiCol_FrameBgHovered) : 0, 6);
        ImVec2 iconMin{p.x + 16, p.y + 8}, iconMax{p.x + cell - 16, p.y + cell - 30};
        bool isImage = ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga";
        if (entry.is_directory()) {
            dl->AddRectFilled({iconMin.x, iconMin.y + 6}, iconMax, IM_COL32(230, 180, 70, 255), 4);
            dl->AddRectFilled(iconMin, {iconMin.x + 22, iconMin.y + 10}, IM_COL32(230, 180, 70, 255), 3);
        } else if (isImage) {
            const TextureAsset& t = assets_.texture(rel, true);
            float aspect = t.height ? static_cast<float>(t.width) / t.height : 1.0f;
            float w = iconMax.x - iconMin.x, h = iconMax.y - iconMin.y;
            ImVec2 size = aspect > w / h ? ImVec2(w, w / aspect) : ImVec2(h * aspect, h);
            ImVec2 o{iconMin.x + (w - size.x) * 0.5f, iconMin.y + (h - size.y) * 0.5f};
            dl->AddImage(static_cast<ImTextureID>(device_.nativeTexture(t.handle)), o, {o.x + size.x, o.y + size.y}, {0, 1}, {1, 0});
        } else {
            ImU32 col = IM_COL32(120, 130, 150, 255);
            const char* badge = "FILE";
            if (ext == ".es") { col = IM_COL32(255, 140, 90, 255); badge = "CODE"; }
            else if (ext == ".blocks") { col = IM_COL32(242, 176, 30, 255); badge = "BLOCKS"; }
            else if (ext == ".scene") { col = IM_COL32(90, 170, 255, 255); badge = "SCENE"; }
            else if (ext == ".prefab") { col = IM_COL32(120, 220, 170, 255); badge = "PREFAB"; }
            else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac") { col = IM_COL32(207, 99, 207, 255); badge = "SOUND"; }
            else if (ext == ".gltf" || ext == ".glb") { col = IM_COL32(170, 140, 255, 255); badge = "MODEL"; }
            else if (ext == ".ttf" || ext == ".otf") { col = IM_COL32(200, 200, 200, 255); badge = "FONT"; }
            dl->AddRectFilled(iconMin, iconMax, col, 6);
            ImVec2 ts = ImGui::CalcTextSize(badge);
            dl->AddText({(iconMin.x + iconMax.x - ts.x) * 0.5f, (iconMin.y + iconMax.y - ts.y) * 0.5f}, IM_COL32(20, 20, 30, 255), badge);
        }
        std::string shown = name.size() > 13 ? name.substr(0, 11) + ".." : name;
        ImVec2 ts = ImGui::CalcTextSize(shown.c_str());
        dl->AddText({p.x + (cell - ts.x) * 0.5f, p.y + cell - 22}, ImGui::GetColorU32(ImGuiCol_Text), shown.c_str());
        if (hovered && name != shown)
            ImGui::SetTooltip("%s", name.c_str());
        if (ImGui::IsItemClicked())
            selectedAsset_ = rel;
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (entry.is_directory())
                assetFolder_ = rel;
            else if (ext == ".scene")
                openScene(rel);
            else if (ext == ".es" || ext == ".blocks")
                openScript(rel);
            else if (ext == ".prefab")
                instantiatePrefab(rel, view3D_ ? Vec3{0, 0, 0} : Vec3{cam2D_.x, cam2D_.y, 0});
        }
        if (!entry.is_directory() && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload("ASSET_PATH", rel.data(), rel.size());
            ImGui::Text("%s", name.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Rename")) {
                renameTarget_ = rel;
                std::snprintf(renameBuffer_, sizeof renameBuffer_, "%s", name.c_str());
            }
            if (!entry.is_directory() && ImGui::MenuItem("Duplicate")) {
                stdfs::path relPath(rel);
                std::string copy = uniqueName(relPath.parent_path().generic_string(), relPath.stem().string() + "_copy", ext);
                std::error_code ec;
                stdfs::copy_file(projectDir_ / rel, projectDir_ / copy, ec);
                if (ec)
                    notify("Couldn't duplicate " + rel + ": " + ec.message(), true);
                else
                    notify("Made " + copy);
                scanAssets();
            }
            if (ImGui::MenuItem("Delete"))
                pendingDelete = rel;
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if (entries.empty())
        ImGui::TextDisabled("This folder is empty. Drag images, sounds or models from your computer into this window.");
    ImGui::EndChild();

    if (!renameTarget_.empty())
        ImGui::OpenPopup("Rename file");
    if (ImGui::BeginPopupModal("Rename file", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(260);
        bool enter = ImGui::InputText("##rename", renameBuffer_, sizeof renameBuffer_, ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter || ImGui::Button("Rename")) {
            stdfs::path from = projectDir_ / renameTarget_;
            stdfs::path to = from.parent_path() / renameBuffer_;
            std::error_code rec;
            stdfs::rename(from, to, rec);
            std::string newRel = fs::relativePath(to, projectDir_);
            // Keep references working.
            recordUndo("Rename file");
            for (auto& ci : ComponentRegistry::all())
                scene_->registry().forEachEntity([&](Entity x) {
                    if (void* c = ci.get(scene_->registry(), x))
                        for (auto& f : ci.fields)
                            if (f.type == FieldType::Asset && f.ref<std::string>(c) == renameTarget_)
                                f.ref<std::string>(c) = newRel;
                });
            for (auto& t : tabs_)
                if (t->path == renameTarget_)
                    t->path = newRel;
            renameTarget_.clear();
            scanAssets();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            renameTarget_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (!pendingDelete.empty()) {
        std::error_code rec;
        stdfs::remove_all(projectDir_ / pendingDelete, rec);
        notify("Deleted " + pendingDelete);
        scanAssets();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- console

void Editor::drawConsole() {
    std::string title = errorCount_ > 0 ? "Console (" + std::to_string(errorCount_) + " errors)###Console" : "Console###Console";
    ui::panelClass();
    ImGui::Begin(title.c_str(), &showConsole_);
    if (ImGui::SmallButton("Clear"))
        console_.clear();
    ImGui::SameLine();
    ImGui::Checkbox("Errors only", &errorsOnly_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Clear on play", &prefs.clearConsoleOnPlay))
        prefs.save();
    ImGui::Separator();
    ImGui::BeginChild("##log", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(fonts.code);
    int index = 0;
    for (auto& l : console_) {
        if (errorsOnly_ && l.level != LogLevel::Error)
            continue;
        ImVec4 color = l.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1) : l.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1) : ImVec4(0.85f, 0.87f, 0.9f, 1);
        ImGui::PushID(index++);
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        std::string prefix = l.file.empty() ? "" : l.file + ":" + std::to_string(l.line) + "  ";
        std::string text = prefix + l.text + (l.count > 1 ? "  (x" + std::to_string(l.count) + ")" : "");
        if (ImGui::Selectable(text.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && !l.file.empty()) {
            if (fs::exists(projectDir_ / l.file))
                openScript(l.file, l.line);
        }
        if (ImGui::IsItemHovered() && !l.file.empty())
            ImGui::SetTooltip("Click to open %s at line %d", l.file.c_str(), l.line);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    if (console_.empty())
        ImGui::TextDisabled("Messages from print() and any errors show up here.");
    ImGui::PopFont();
    if (consoleScrollToBottom_ && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 40)
        ImGui::SetScrollHereY(1.0f);
    consoleScrollToBottom_ = false;
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- script tabs

void Editor::drawScriptTabs() {
    // New script windows open as tabs next to the scene view.
    ImGuiWindow* sceneWindow = ImGui::FindWindowByName("###Viewport");
    ImGuiID sceneDock = sceneWindow ? sceneWindow->DockId : 0;
    for (auto it = tabs_.begin(); it != tabs_.end();) {
        ScriptTab& tab = **it;
        std::string title = stdfs::path(tab.path).filename().string() + (tab.modified ? " *" : "") + "###tab:" + tab.path;
        if (tab.focus) {
            ImGui::SetNextWindowFocus();
            tab.focus = false;
        }
        if (sceneDock)
            ImGui::SetNextWindowDockID(sceneDock, ImGuiCond_FirstUseEver);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {4, 4});
        bool visible = ImGui::Begin(title.c_str(), &tab.open, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleVar();
        if (visible) {
            if (tab.code) {
                if (ImGui::Button("Save (Ctrl+S)")) {
                    fs::writeText(projectDir_ / tab.path, tab.code->text());
                    tab.modified = false;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("EasyScript");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reference"))
                    showReference_ = true;
                ImVec2 avail = ImGui::GetContentRegionAvail();
                if (tab.code->draw("##code", avail)) {
                    tab.modified = true;
                    checkScript(tab);
                }
            } else if (tab.blocks) {
                if (ImGui::Button("Save (Ctrl+S)")) {
                    fs::writeText(projectDir_ / tab.path, tab.blocks->save().dump(2));
                    tab.modified = false;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Show code", &tab.blocks->showCode);
                ImGui::SameLine();
                ImGui::TextDisabled("Drag blocks from the left. Right-click a block for more.");
                ImVec2 avail = ImGui::GetContentRegionAvail();
                if (tab.blocks->draw("##blocks", avail)) {
                    tab.modified = true;
                    checkScript(tab);
                    // Blocks save as you go so Play always uses the latest version.
                    fs::writeText(projectDir_ / tab.path, tab.blocks->save().dump(2));
                    tab.modified = false;
                }
            }
        }
        ImGui::End();
        if (!tab.open) {
            if (tab.modified) {
                if (tab.code)
                    fs::writeText(projectDir_ / tab.path, tab.code->text());
                else if (tab.blocks)
                    fs::writeText(projectDir_ / tab.path, tab.blocks->save().dump(2));
            }
            it = tabs_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- windows

void Editor::drawSettings() {
    ImGui::SetNextWindowSize({520, 560}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Project Settings", &showSettings_)) {
        ImGui::End();
        return;
    }
    bool changed = false;
    ui::sectionHeader("Game");
    changed |= ImGui::InputText("Name", &settings_.name);
    changed |= ImGui::InputText("Version", &settings_.version);
    if (ImGui::BeginCombo("Start scene", settings_.startScene.c_str())) {
        for (auto& s : projectFiles({".scene"}))
            if (ImGui::Selectable(s.c_str(), s == settings_.startScene)) {
                settings_.startScene = s;
                changed = true;
            }
        ImGui::EndCombo();
    }
    ui::sectionHeader("Window");
    changed |= ImGui::InputInt("Width", &settings_.width);
    changed |= ImGui::InputInt("Height", &settings_.height);
    changed |= ImGui::Checkbox("Resizable", &settings_.resizable);
    changed |= ImGui::Checkbox("Start fullscreen", &settings_.fullscreen);
    changed |= ImGui::Checkbox("VSync (smooth, no tearing)", &settings_.vsync);
    changed |= ImGui::Checkbox("Advanced mode in the editor", &settings_.advancedMode);

    ui::sectionHeader("Controls");
    ImGui::TextWrapped("Actions let players change controls. Scripts use them like key_pressed(\"jump\"). "
                       "List keys separated by commas.");
    Input& input = window_.input();
    input.loadActions(settings_.inputActions);
    auto actions = input.actions();
    bool actionsChanged = false;
    if (ImGui::BeginTable("##actions", 2, ImGuiTableFlags_BordersInnerH)) {
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Keys");
        for (auto& a : actions) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a.name.c_str());
            ImGui::TableSetColumnIndex(1);
            std::string keys;
            for (auto& b : a.bindings)
                keys += (keys.empty() ? "" : ", ") + b;
            ImGui::SetNextItemWidth(-1);
            ImGui::PushID(a.name.c_str());
            if (ImGui::InputText("##keys", &keys, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
                a.bindings.clear();
                size_t start = 0;
                while (start <= keys.size()) {
                    size_t comma = keys.find(',', start);
                    std::string k = keys.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
                    k.erase(0, k.find_first_not_of(' '));
                    k.erase(k.find_last_not_of(' ') + 1);
                    if (!k.empty()) {
                        if (!Input::isValidName(k))
                            notify("\"" + k + "\" isn't a key name I know.", true);
                        a.bindings.push_back(k);
                    }
                    if (comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
                actionsChanged = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (actionsChanged) {
        input.setActions(actions);
        settings_.inputActions = input.saveActions();
        changed = true;
    }
    if (ImGui::Button("Reset controls to defaults")) {
        settings_.inputActions = Json::object();
        changed = true;
    }
    if (changed)
        settings_.save(projectDir_);
    ImGui::End();
}

void Editor::drawLearn() {
    ImGui::Begin("Learn", &showLearn_);
    if (tutorial_.isNull()) {
        ImGui::TextDisabled("This project doesn't have a tutorial.");
        ImGui::End();
        return;
    }
    const Json& steps = tutorial_["steps"];
    int count = static_cast<int>(steps.size());
    learnStep_ = std::clamp(learnStep_, 0, std::max(0, count - 1));
    ImGui::PushFont(fonts.big);
    ImGui::TextWrapped("%s", tutorial_["title"].asString("Tutorial").c_str());
    ImGui::PopFont();
    ImGui::ProgressBar(count ? static_cast<float>(learnStep_ + 1) / count : 0, {-1, 6}, "");
    ImGui::TextDisabled("Step %d of %d", learnStep_ + 1, count);
    ImGui::Spacing();
    if (count) {
        const Json& step = steps[static_cast<size_t>(learnStep_)];
        ImGui::PushFont(fonts.bold);
        ImGui::TextWrapped("%s", step["title"].asString().c_str());
        ImGui::PopFont();
        ImGui::Spacing();
        ImGui::TextWrapped("%s", step["text"].asString().c_str());
        if (step.contains("code")) {
            ImGui::Spacing();
            ImGui::PushFont(fonts.code);
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(30, 33, 39, 255));
            std::string code = step["code"].asString();
            float h = ImGui::GetTextLineHeightWithSpacing() * (1 + static_cast<float>(std::count(code.begin(), code.end(), '\n'))) + 12;
            ImGui::BeginChild("##code", {-1, h}, ImGuiChildFlags_Borders);
            ImGui::TextUnformatted(code.c_str());
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        if (step.contains("open")) {
            std::string file = step["open"].asString();
            if (ImGui::Button(("Open " + file).c_str()))
                openScript(file);
        }
        if (step.contains("select")) {
            std::string name = step["select"].asString();
            ImGui::SameLine();
            if (ImGui::Button(("Select " + name).c_str()))
                select(scene_->findByName(name));
        }
        if (step["play"].asBool()) {
            ImGui::SameLine();
            if (ImGui::Button(playing_ ? "Stop" : "Play it!"))
                playing_ ? stop() : play();
        }
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::BeginDisabled(learnStep_ == 0);
    if (ImGui::Button("< Back", {90, 0}))
        --learnStep_;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(learnStep_ + 1 >= count);
    if (ImGui::Button("Next >", {90, 0}))
        ++learnStep_;
    ImGui::EndDisabled();
    ImGui::End();
}

void Editor::drawReference() {
    ImGui::SetNextWindowSize({560, 620}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Scripting Reference", &showReference_)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Everything your scripts can use. The same names work in blocks (they turn into this code), "
                       "and the C API uses aven_ + the same name.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##filter", "Search, e.g. key, sound, spawn...", referenceFilter_, sizeof referenceFilter_);
    ImGui::BeginChild("##list");
    ImGui::PushFont(fonts.code);
    std::string filter = referenceFilter_;
    std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
    std::vector<std::string> groups;
    for (auto& e : api_)
        if (std::find(groups.begin(), groups.end(), e.group) == groups.end())
            groups.push_back(e.group);
    for (auto& g : groups) {
        bool header = false;
        for (auto& e : api_) {
            if (e.group != g)
                continue;
            std::string hay = e.name + " " + e.signature;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (!filter.empty() && hay.find(filter) == std::string::npos)
                continue;
            if (!header) {
                ImGui::PopFont();
                ImGui::PushFont(fonts.bold);
                ImGui::Spacing();
                ImGui::TextUnformatted(g.c_str());
                ImGui::PopFont();
                ImGui::PushFont(fonts.code);
                header = true;
            }
            ImGui::BulletText("%s", e.signature.c_str());
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::End();
}

void Editor::drawExport() {
    ImGui::SetNextWindowSize({520, 300}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Build & Export", &showExport_)) {
        ImGui::End();
        return;
    }
    if (exportFolder_.empty())
        exportFolder_ = (projectDir_ / "exports").string();
    ImGui::TextWrapped("Makes a folder with your game that anyone can run without Aven. Zip it up and share it!");
    ImGui::Spacing();
    ImGui::InputText("Output folder", &exportFolder_);
    ImGui::TextDisabled("Game name: %s  |  Start scene: %s", settings_.name.c_str(), settings_.startScene.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Export game", {160, 34})) {
        saveScene();
        saveAllScripts();
        std::string message;
        exportGame(exportFolder_, message);
        exportResult_ = message;
    }
    if (!exportResult_.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", exportResult_.c_str());
    }
    ImGui::End();
}

bool Editor::exportGame(const stdfs::path& folder, std::string& message) {
    std::error_code ec;
    std::string safeName;
    for (char c : settings_.name)
        safeName += (std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_') ? c : '_';
    if (safeName.empty())
        safeName = "Game";
    stdfs::path out = folder / safeName;
    // Only ever replace a previous export of a game; never delete an unrelated folder.
    if (stdfs::exists(out, ec)) {
        bool previousExport = ProjectSettings::isProject(out / "game") && stdfs::exists(out / "README.txt", ec);
        bool empty = stdfs::is_directory(out, ec) && stdfs::directory_iterator(out, ec) == stdfs::directory_iterator();
        if (!previousExport && !empty) {
            message = "There's already a folder called '" + safeName + "' in " + folder.string() +
                      " that isn't an Aven export. Pick a different folder so nothing gets overwritten.";
            return false;
        }
        stdfs::remove_all(out / "game", ec);
    }
    stdfs::create_directories(out / "game", ec);
    // Copy the project, skipping editor-only files and previous exports.
    for (auto it = stdfs::recursive_directory_iterator(projectDir_, ec); it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        stdfs::path rel = stdfs::relative(it->path(), projectDir_, ec);
        std::string first = rel.begin()->string();
        if (first == "exports" || (!first.empty() && first[0] == '.') || rel == "tutorial.json") {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (stdfs::equivalent(it->path(), folder, ec)) {
            it.disable_recursion_pending();
            continue;
        }
        if (it->is_directory())
            stdfs::create_directories(out / "game" / rel, ec);
        else
            stdfs::copy_file(it->path(), out / "game" / rel, stdfs::copy_options::overwrite_existing, ec);
    }
#ifdef _WIN32
    stdfs::path player = fs::executableDir() / "aven-player.exe";
    stdfs::path exe = out / (safeName + ".exe");
#else
    stdfs::path player = fs::executableDir() / "aven-player";
    stdfs::path exe = out / safeName;
#endif
    if (!stdfs::exists(player)) {
        message = "Couldn't find aven-player next to the editor, so only the game files were exported to " + out.string();
        return false;
    }
    stdfs::copy_file(player, exe, stdfs::copy_options::overwrite_existing, ec);
    if (ec) {
        message = "Export failed: " + ec.message();
        return false;
    }
    stdfs::permissions(exe, stdfs::perms::owner_exec | stdfs::perms::group_exec | stdfs::perms::others_exec,
                       stdfs::perm_options::add, ec);
    fs::writeText(out / "README.txt", settings_.name + " " + settings_.version + "\n\nRun " + exe.filename().string() +
                                          " to play.\nMade with Aven.\n");
    message = "Done! Your game is in:\n" + out.string() + "\nRun " + exe.filename().string() + " to play it.";
    Log::info("Exported game to ", out.string());
    return true;
}

} // namespace aven::editor
