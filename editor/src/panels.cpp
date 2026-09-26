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
#include <set>

namespace aven::editor {

// ---------------------------------------------------------------- helpers

namespace ui {

void placeWindow(ImVec2 size, ImVec2 where) {
    // First time a tool window opens: its size and a spot on screen (fractions of the window).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * where.x, vp->Pos.y + vp->Size.y * where.y}, ImGuiCond_FirstUseEver,
                            {0.5f, 0.5f});
}

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

bool vectorField(float* v, int count, float step) {
    // Red, green and blue like the move arrows in the Scene view.
    static const ImU32 kColors[3] = {IM_COL32(226, 86, 86, 255), IM_COL32(112, 192, 80, 255), IM_COL32(82, 142, 232, 255)};
    static const char* kNames[3] = {"X", "Y", "Z"};
    bool changed = false;
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float width = (ImGui::GetContentRegionAvail().x - spacing * static_cast<float>(count - 1)) / static_cast<float>(count);
    float h = ImGui::GetFrameHeight();
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        if (i)
            ImGui::SameLine(0, spacing);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::SetNextItemWidth(width);
        changed |= ImGui::DragFloat("##c", &v[i], step, 0, 0, "%.2f");
        if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
            ImGui::SetTooltip("%s (drag, or double-click to type)", kNames[i]);
        ImGui::GetWindowDrawList()->AddRectFilled(p, {p.x + 3, p.y + h}, kColors[i], ImGui::GetStyle().FrameRounding,
                                                  ImDrawFlags_RoundCornersLeft);
        ImGui::PopID();
    }
    return changed;
}

bool anchorGrid(int32_t& anchor) {
    static const char* kNames[9] = {"Top left", "Top", "Top right", "Left", "Center", "Right", "Bottom left", "Bottom", "Bottom right"};
    bool changed = false;
    float cell = std::round(ImGui::GetFrameHeight() * 0.9f);
    float gap = 3;
    ImVec2 start = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 frame = ImGui::GetColorU32(ImGuiCol_FrameBg), hover = ImGui::GetColorU32(ImGuiCol_FrameBgHovered);
    ImU32 on = ImGui::GetColorU32(ImGuiCol_CheckMark);
    for (int i = 0; i < 9; ++i) {
        ImVec2 p = {start.x + static_cast<float>(i % 3) * (cell + gap), start.y + static_cast<float>(i / 3) * (cell + gap)};
        ImGui::SetCursorScreenPos(p);
        ImGui::PushID(i);
        if (ImGui::InvisibleButton("##a", {cell, cell}) && anchor != i) {
            anchor = i;
            changed = true;
        }
        bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetTooltip("%s", kNames[i]);
        ImGui::PopID();
        dl->AddRectFilled(p, {p.x + cell, p.y + cell}, hovered ? hover : frame, 3);
        // A dot where the anchor sits inside the square.
        ImVec2 dot = {p.x + cell * (0.25f + 0.25f * static_cast<float>(i % 3)), p.y + cell * (0.25f + 0.25f * static_cast<float>(i / 3))};
        dl->AddCircleFilled(dot, anchor == i ? 3.5f : 2.0f, anchor == i ? on : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (anchor == i)
            dl->AddRect(p, {p.x + cell, p.y + cell}, on, 3, 0, 1.5f);
    }
    float size = cell * 3 + gap * 2;
    ImGui::SetCursorScreenPos({start.x + size + 10, start.y + (size - ImGui::GetTextLineHeight()) * 0.5f});
    ImGui::TextDisabled("%s", anchor >= 0 && anchor < 9 ? kNames[anchor] : "?");
    ImGui::SetCursorScreenPos({start.x, start.y + size});
    ImGui::Dummy({0, 0});
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

// Stripe color for component headers.
ImU32 categoryColor(const std::string& category) {
    if (category == "Basics") return IM_COL32(148, 163, 184, 255);
    if (category == "Rendering" || category == "Rendering 2D" || category == "Rendering 3D") return IM_COL32(96, 165, 250, 255);
    if (category == "Scripting") return IM_COL32(251, 146, 60, 255);
    if (category == "Physics 2D" || category == "Physics 3D") return IM_COL32(74, 222, 128, 255);
    if (category == "Audio") return IM_COL32(232, 121, 249, 255);
    if (category == "Effects") return IM_COL32(250, 204, 21, 255);
    if (category == "UI") return IM_COL32(45, 212, 191, 255);
    if (category == "Behaviors") return IM_COL32(244, 114, 182, 255);
    if (category == "Gameplay") return IM_COL32(167, 139, 250, 255);
    return IM_COL32(148, 163, 184, 255);
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

// A folder: an object that only holds other objects (nothing but a Transform of its own).
bool isFolder(Registry& reg, Entity e, bool hasChildren) {
    if (!hasChildren && reg.get<EntityInfo>(e).name != "Folder") // a new, still empty folder counts too
        return false;
    for (auto& info : ComponentRegistry::all())
        if (info.name != "Transform" && info.get(reg, e))
            return false;
    return true;
}

void drawFolderIcon(ImDrawList* dl, ImVec2 c, bool open, ImU32 col) {
    float w = 7, h = 5;
    dl->AddRectFilled({c.x - w, c.y - h - 2}, {c.x - 1, c.y - h + 1}, col, 1.5f); // tab
    if (open)
        dl->AddQuadFilled({c.x - w, c.y - h}, {c.x + w, c.y - h}, {c.x + w - 2, c.y + h}, {c.x - w - 2, c.y + h}, col);
    else
        dl->AddRectFilled({c.x - w, c.y - h}, {c.x + w, c.y + h}, col, 1.5f);
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
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
    if (kids.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected(e))
        flags |= ImGuiTreeNodeFlags_Selected;
    if (!filter.empty())
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    ImGui::PushID(static_cast<int>(e.index));
    bool dim = !s.isActive(e);
    if (dim)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = true;
    bool shown = matches || !filter.empty();
    if (shown) {
        hierarchyOrder_.push_back(info.uuid);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool renaming = renaming_ == info.uuid;
        if (revealIds_.count(info.uuid.value))
            ImGui::SetNextItemOpen(true); // a folder holding the object selected elsewhere (scene view, Find...)
        bool folder = isFolder(reg, e, !kids.empty());
        open = ImGui::TreeNodeEx("##node", flags, "     %s", renaming ? "" : info.name.c_str());
        ImVec2 rowMin = ImGui::GetItemRectMin(), rowMax = ImGui::GetItemRectMax();
        float h = ImGui::GetFrameHeight();
        ImVec2 dot{p.x + ImGui::GetTreeNodeToLabelSpacing() + 6, p.y + h * 0.5f};
        ImDrawList* rowDl = ImGui::GetWindowDrawList();
        if (folder) {
            drawFolderIcon(rowDl, dot, open, IM_COL32(234, 179, 8, 255));
            // How many objects are inside, while it's closed.
            if (!open && !renaming) {
                std::string count = std::to_string(kids.size());
                float labelEnd = rowMin.x + ImGui::GetTreeNodeToLabelSpacing() + ImGui::CalcTextSize(("     " + info.name).c_str()).x;
                rowDl->AddText({labelEnd + 8, rowMin.y + (h - ImGui::GetFontSize()) * 0.5f},
                               ImGui::GetColorU32(ImGuiCol_TextDisabled), count.c_str());
            }
        } else {
            rowDl->AddCircleFilled(dot, 4.5f, entityColor(reg, e));
            // A thin ring keeps white or pale objects visible on light themes.
            rowDl->AddCircle(dot, 4.5f, ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.45f), 0, 1.0f);
        }
        if (scrollToSelected_ && !selection_.empty() && selection_.back() == info.uuid) {
            ImGui::SetScrollHereY(0.35f);
            scrollToSelected_ = false;
        }
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
            revealedFor_ = info.uuid; // already visible: no need to open or scroll
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                toggleSelection(e);
            } else if (io.KeyShift && hierarchyAnchor_) {
                // Select every visible row between the anchor and this one.
                auto a = std::find(lastHierarchyOrder_.begin(), lastHierarchyOrder_.end(), hierarchyAnchor_);
                auto b = std::find(lastHierarchyOrder_.begin(), lastHierarchyOrder_.end(), info.uuid);
                if (a != lastHierarchyOrder_.end() && b != lastHierarchyOrder_.end()) {
                    if (a > b)
                        std::swap(a, b);
                    selection_.assign(a, b + 1);
                    std::erase(selection_, info.uuid);
                    selection_.push_back(info.uuid);
                }
            } else {
                select(e);
            }
            if (!io.KeyShift)
                hierarchyAnchor_ = info.uuid;
        }
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            focusSelected();
        if (!playing_) {
            if (ImGui::BeginDragDropSource()) {
                uint64_t id = info.uuid.value;
                ImGui::SetDragDropPayload("ENTITY", &id, sizeof id);
                auto sel = selectedEntities();
                if (isSelected(e) && sel.size() > 1)
                    ImGui::Text("%d objects", static_cast<int>(sel.size()));
                else
                    ImGui::Text("%s", info.name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                // Top quarter: put before, bottom quarter: put after, middle: make it a child.
                float my = ImGui::GetMousePos().y, rh = rowMax.y - rowMin.y;
                int zone = my < rowMin.y + rh * 0.25f ? -1 : my > rowMax.y - rh * 0.25f ? 1 : 0;
                ImDrawList* fg = ImGui::GetForegroundDrawList();
                ImU32 accent = ImGui::GetColorU32(ImGuiCol_DragDropTarget);
                if (zone == -1)
                    fg->AddLine({rowMin.x, rowMin.y}, {rowMax.x, rowMin.y}, accent, 2.5f);
                else if (zone == 1)
                    fg->AddLine({rowMin.x, rowMax.y}, {rowMax.x, rowMax.y}, accent, 2.5f);
                else
                    fg->AddRect(rowMin, rowMax, accent, 3, 0, 2);
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY", ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    Entity dragged = s.findByUUID({*static_cast<const uint64_t*>(pl->Data)});
                    std::vector<Entity> moving = isSelected(dragged) ? selectedEntities() : std::vector<Entity>{dragged};
                    recordUndo(zone == 0 ? "Change parent" : "Reorder");
                    for (Entity m : moving) {
                        if (!m || m == e || s.isAncestor(m, e))
                            continue;
                        if (zone == 0) {
                            s.setParent(m, e);
                        } else {
                            Entity parent = s.parent(e);
                            int index = s.siblingIndex(e) + (zone == 1 ? 1 : 0);
                            if (s.parent(m) == parent && s.siblingIndex(m) < index)
                                --index;
                            s.setParent(m, parent, true, index);
                        }
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
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                if (!isSelected(e))
                    select(e);
                ImGui::OpenPopup("##entity_menu");
            }
        }
        // Inline rename.
        if (renaming) {
            ImGui::SetCursorScreenPos({rowMin.x + ImGui::GetTreeNodeToLabelSpacing() + 16, rowMin.y});
            ImGui::SetNextItemWidth(rowMax.x - rowMin.x - ImGui::GetTreeNodeToLabelSpacing() - 50);
            if (renameFocus_) {
                ImGui::SetKeyboardFocusHere();
                renameFocus_ = false;
            }
            bool done = ImGui::InputText("##rename", &renameEntityBuffer_,
                                         ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            bool cancel = ImGui::IsKeyPressed(ImGuiKey_Escape);
            if (done || cancel || ImGui::IsItemDeactivated()) {
                if (!cancel && !renameEntityBuffer_.empty() && renameEntityBuffer_ != info.name) {
                    recordUndo("Rename");
                    info.name = renameEntityBuffer_;
                }
                renaming_ = {};
            }
        }
        // Eye: turn the object (and its children) on or off.
        {
            float size = h - 6;
            ImVec2 eye{rowMax.x - size - 4, rowMin.y + 3};
            bool rowHovered = ImGui::IsMouseHoveringRect(rowMin, rowMax);
            ImGui::SetCursorScreenPos(eye);
            if (ImGui::InvisibleButton("##eye", {size, size}) && !playing_) {
                recordUndo(info.active ? "Turn off" : "Turn on");
                info.active = !info.active;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(info.active ? "Turn off (hides it and pauses its scripts)" : "Turn on");
            if (rowHovered || !info.active) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 c{eye.x + size * 0.5f, eye.y + size * 0.5f};
                ImU32 col = ImGui::GetColorU32(info.active ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                dl->PathArcTo({c.x, c.y + size * 0.35f}, size * 0.5f, 3.14159f * 1.2f, 3.14159f * 1.8f, 10);
                dl->PathStroke(col, 0, 1.5f);
                dl->PathArcTo({c.x, c.y - size * 0.35f}, size * 0.5f, 3.14159f * 0.2f, 3.14159f * 0.8f, 10);
                dl->PathStroke(col, 0, 1.5f);
                if (info.active)
                    dl->AddCircleFilled(c, size * 0.14f, col);
                else
                    dl->AddLine({c.x - size * 0.4f, c.y + size * 0.35f}, {c.x + size * 0.4f, c.y - size * 0.35f}, col, 1.5f);
            }
            ImGui::SetCursorScreenPos({rowMin.x, rowMax.y});
            ImGui::Dummy({0, 0});
        }
        if (ImGui::BeginPopup("##entity_menu")) {
            if (ImGui::MenuItem("Rename", chordName(prefs.chord("rename")).c_str())) {
                renaming_ = info.uuid;
                renameEntityBuffer_ = info.name;
                renameFocus_ = true;
            }
            if (ImGui::MenuItem("Duplicate", chordName(prefs.chord("duplicate")).c_str())) {
                recordUndo("Duplicate");
                select(s.duplicate(e));
            }
            if (ImGui::MenuItem("Copy", chordName(prefs.chord("copy")).c_str()))
                copySelection(false);
            if (ImGui::MenuItem("Paste", chordName(prefs.chord("paste")).c_str(), false, !clipboard_.isNull()))
                pasteClipboard();
            bool deleted = false;
            if (ImGui::MenuItem("Delete", chordName(prefs.chord("delete")).c_str())) {
                recordUndo("Delete");
                for (Entity x : selectedEntities())
                    if (s.valid(x))
                        s.destroy(x);
                selection_.clear();
                deleted = true;
            }
            if (!deleted) {
                ImGui::Separator();
                if (ImGui::MenuItem("Group into a folder", chordName(prefs.chord("group")).c_str()))
                    groupSelection();
                if (ImGui::BeginMenu("Add child")) {
                    for (const char* k : {"Folder", "Entity", "Square", "Circle", "Text", "Cube", "Sphere", "Point Light", "Particles", "UI Text", "UI Button"})
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
                if (!kids.empty() && ImGui::MenuItem("Select children"))
                    for (Entity c : kids)
                        addToSelection(c);
                ImGui::Separator();
                if (unlocked(Feature::Prefabs) && ImGui::MenuItem("Save as Prefab"))
                    savePrefab(e);
                if (ImGui::MenuItem("Add blocks script")) {
                    std::string path = newScriptFile(info.name, true);
                    attachScript(e, path);
                    openBlocks(path);
                }
                if (unlocked(Feature::Code) && ImGui::MenuItem("Add EasyScript")) {
                    std::string path = newScriptFile(info.name, false);
                    attachScript(e, path);
                    openScript(path);
                }
            }
            ImGui::EndPopup();
            if (deleted) {
                if (dim)
                    ImGui::PopStyleColor();
                if (open)
                    ImGui::TreePop();
                ImGui::PopID();
                return;
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
        if (shown)
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
        if (unlocked(Feature::Tilemap) && ImGui::MenuItem("Tilemap"))
            createEntity("Tilemap");
        ImGui::Separator();
        ImGui::TextDisabled("3D");
        for (const char* k : {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Player 3D"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        if (unlocked(Feature::Lighting))
            for (const char* k : {"Sun", "Point Light", "Spot Light"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
        ImGui::Separator();
        ImGui::TextDisabled("Other");
        if (ImGui::MenuItem("Folder"))
            createEntity("Folder");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("An empty object that holds others, to keep the Hierarchy tidy.\n"
                              "Drag objects onto it, or select some and press %s.", chordName(prefs.chord("group")).c_str());
        for (const char* k : {"Camera", "Entity"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        if (unlocked(Feature::Particles) && ImGui::MenuItem("Particles"))
            createEntity("Particles");
        if (unlocked(Feature::Audio) && ImGui::MenuItem("Sound"))
            createEntity("Sound");
        if (unlocked(Feature::UI) && ImGui::BeginMenu("UI")) {
            for (const char* k : {"UI Text", "UI Button", "UI Panel", "UI Image", "Score Text", "Health Bar", "Start Menu", "Pause Menu"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::Separator();
    ImGui::BeginChild("##tree");
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {4, 3});
    Scene& s = scene();
    // When something gets selected outside the Hierarchy, open the folders it's in and scroll to it.
    revealIds_.clear();
    if (!selection_.empty() && selection_.back() != revealedFor_) {
        revealedFor_ = selection_.back();
        if (Entity e = s.findByUUID(revealedFor_)) {
            for (Entity p = s.parent(e); p; p = s.parent(p))
                revealIds_.insert(s.info(p).uuid.value);
            scrollToSelected_ = true;
        }
    }
    lastHierarchyOrder_ = std::move(hierarchyOrder_);
    hierarchyOrder_.clear();
    std::vector<Entity> roots = s.roots();
    for (Entity e : roots)
        if (s.valid(e))
            drawEntityNode(e);
    ImGui::PopStyleVar();
    // Dropping on empty space moves objects to the top level.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (!playing_ && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
            Entity dragged = s.findByUUID({*static_cast<const uint64_t*>(pl->Data)});
            std::vector<Entity> moving = isSelected(dragged) ? selectedEntities() : std::vector<Entity>{dragged};
            recordUndo("Change parent");
            for (Entity m : moving)
                if (m)
                    s.setParent(m, {});
        }
        ImGui::EndDragDropTarget();
    }
    if (ImGui::IsItemClicked())
        selection_.clear();
    if (!playing_ && ImGui::BeginPopupContextItem("##empty_menu")) {
        if (ImGui::MenuItem("Paste", nullptr, false, !clipboard_.isNull()))
            pasteClipboard();
        if (ImGui::MenuItem("New empty object"))
            createEntity("Entity");
        if (ImGui::MenuItem("New folder"))
            createEntity("Folder");
        ImGui::EndPopup();
    }
    if (s.roots().empty())
        ImGui::TextDisabled("This scene is empty.\nClick '+ Add' to add objects.");
    ImGui::EndChild();
    // Hierarchy-only shortcuts.
    if (hierarchyFocused_ && !playing_ && !ImGui::GetIO().WantTextInput) {
        if (shortcut("rename"))
            if (Entity e = selected()) {
                renaming_ = s.info(e).uuid;
                renameEntityBuffer_ = s.info(e).name;
                renameFocus_ = true;
            }
    }
    ImGui::End();
}

// ---------------------------------------------------------------- inspector

namespace {

// "RigidBody2D" -> "Rigid Body 2D", "UIElement" -> "UI Element": easier to read in the Inspector.
std::string displayName(const std::string& name) {
    auto at = [&](size_t i) { return i < name.size() ? static_cast<unsigned char>(name[i]) : ' '; };
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        if (i) {
            unsigned char c = at(i), prev = at(i - 1);
            bool wordStart = std::isupper(c) && (std::islower(prev) || (std::isupper(prev) && std::islower(at(i + 1))));
            bool numberStart = std::isdigit(c) && std::isalpha(prev);
            if (wordStart || numberStart)
                out += ' ';
        }
        out += name[i];
    }
    return out;
}

// What a component's settings are when it's first added.
const Json& componentDefaults(const ComponentInfo& info) {
    static std::map<std::string, Json> cache;
    auto it = cache.find(info.name);
    if (it == cache.end()) {
        Registry r;
        Entity x = r.create();
        it = cache.emplace(info.name, saveComponent(info, info.add(r, x))).first;
    }
    return it->second;
}

// Equal, allowing for float rounding in saved files.
bool jsonNear(const Json& a, const Json& b) {
    if (a.isNumber() && b.isNumber())
        return std::abs(a.asNumber() - b.asNumber()) <= 1e-4 * std::max(1.0, std::abs(a.asNumber()));
    if (a.isArray() && b.isArray()) {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (!jsonNear(a[i], b[i]))
                return false;
        return true;
    }
    return a == b;
}

} // namespace

const Json* Editor::prefabRootComponents(const std::string& path) {
    PrefabCache& c = prefabCache_[path];
    int64_t modified = fs::modifiedTime(projectDir_ / path);
    if (modified != c.modified) {
        c.modified = modified;
        c.root = Json();
        if (auto text = fs::readText(projectDir_ / path)) {
            Json data = Json::parse(*text);
            if (data["entities"].size())
                c.root = data["entities"][0]["components"];
        }
    }
    return c.root.isObject() ? &c.root : nullptr;
}

bool Editor::drawComponent(Entity e, const ComponentInfo& info, void* data) {
    bool changed = false;
    bool twoD = !view3D_;
    // On a prefab copy, settings that differ from the prefab are marked (Transform is always per copy).
    const Json* prefab = nullptr;
    if (info.name != "Transform" && info.name != "PrefabInstance")
        if (auto* pi = scene().registry().tryGet<PrefabInstance>(e); pi && !pi->path.empty())
            if (const Json* root = prefabRootComponents(pi->path); root && root->contains(info.name))
                prefab = &(*root)[info.name];
    const Json& defaults = componentDefaults(info);
    for (auto& f : info.fields) {
        if (f.options.runtime)
            continue;
        if (f.options.advanced && !advanced())
            continue;
        // Screen UI is placed by its UI Element; only the Transform's scale does anything.
        if (info.name == "Transform" && f.name != "scale" && scene().registry().has<UIElement>(e))
            continue;
        ImGui::PushID(f.name.c_str());
        ImGui::TableNextRow();
        // Settings just changed by Ask Aven or a recipe glow for a moment.
        for (const std::string& key : {info.name + "/" + f.name, info.name + "/*"}) {
            auto flash = flashFields_.find(key);
            if (flash != flashFields_.end() && flash->second > 0) {
                Color a = prefs.accentColor();
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                                       ImGui::ColorConvertFloat4ToU32({a.r, a.g, a.b, std::min(0.45f, flash->second * 0.25f)}));
            }
        }
        Json current = saveField(f, data);
        const Json* prefabValue = nullptr;
        if (prefab)
            prefabValue = prefab->contains(f.name) ? &(*prefab)[f.name] : &defaults[f.name];
        bool overridden = prefabValue && !jsonNear(*prefabValue, current);
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        std::string label = f.label;
        // In 2D, show rotation as a single "Angle".
        bool transform2D = twoD && info.name == "Transform";
        if (transform2D && f.name == "rotation")
            label = "Angle";
        if (overridden) {
            // Like Unity: a bar and bold name for settings changed on this copy only.
            ImVec2 p = ImGui::GetCursorScreenPos();
            Color a = prefs.accentColor();
            ImGui::GetWindowDrawList()->AddRectFilled({p.x - 5, p.y}, {p.x - 3, p.y + ImGui::GetFrameHeight()},
                                                      ImGui::ColorConvertFloat4ToU32({a.r, a.g, a.b, 1}));
            ImGui::PushFont(fonts.bold);
        }
        ImGui::TextUnformatted(label.c_str());
        if (overridden)
            ImGui::PopFont();
        if (ImGui::IsItemHovered()) {
            std::string tip = f.options.tooltip ? f.options.tooltip : "";
            if (overridden)
                tip += std::string(tip.empty() ? "" : "\n") + "Changed on this copy only (the prefab has something else).";
            tip += std::string(tip.empty() ? "" : "\n") + "Right-click for more.";
            ImGui::SetTooltip("%s", tip.c_str());
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##field_menu");
        }
        bool c = false;
        if (ImGui::BeginPopup("##field_menu")) {
            ImGui::TextDisabled("%s  (%s in scripts)", label.c_str(), f.name.c_str());
            ImGui::Separator();
            bool atDefault = !defaults.contains(f.name) || jsonNear(defaults[f.name], current);
            if (ImGui::MenuItem("Reset to default", nullptr, false, !atDefault && defaults.contains(f.name))) {
                loadField(f, data, defaults[f.name]);
                c = true;
            }
            if (prefabValue && ImGui::MenuItem("Revert to the prefab's value", nullptr, false, overridden)) {
                loadField(f, data, *prefabValue);
                c = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy")) {
                fieldClipboard_ = current;
                fieldClipboardType_ = f.type;
                ImGui::SetClipboardText(current.dump().c_str());
            }
            if (ImGui::MenuItem("Paste", nullptr, false, !fieldClipboard_.isNull() && fieldClipboardType_ == f.type)) {
                loadField(f, data, fieldClipboard_);
                c = true;
            }
            ImGui::EndPopup();
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        switch (f.type) {
        case FieldType::Bool: c |= ImGui::Checkbox("##v", &f.ref<bool>(data)); break;
        case FieldType::Int:
            if (f.options.max > f.options.min)
                c |= ImGui::SliderInt("##v", &f.ref<int>(data), static_cast<int>(f.options.min), static_cast<int>(f.options.max));
            else
                c |= ImGui::DragInt("##v", &f.ref<int>(data), 0.1f);
            break;
        case FieldType::Float:
            if (f.options.max > f.options.min)
                c |= ImGui::SliderFloat("##v", &f.ref<float>(data), f.options.min, f.options.max, "%.2f");
            else
                c |= ImGui::DragFloat("##v", &f.ref<float>(data), f.options.step, 0, 0, "%.2f");
            break;
        case FieldType::Vec2: c |= ui::vectorField(&f.ref<Vec2>(data).x, 2, f.options.step); break;
        case FieldType::Vec3: {
            Vec3& v = f.ref<Vec3>(data);
            if (transform2D && f.name == "rotation")
                c |= ImGui::DragFloat("##v", &v.z, 0.5f, 0, 0, "%.1f°");
            else if (transform2D && !advanced())
                c |= ui::vectorField(&v.x, 2, f.options.step);
            else
                c |= ui::vectorField(&v.x, 3, f.options.step);
            break;
        }
        case FieldType::Color: c |= ImGui::ColorEdit4("##v", &f.ref<Color>(data).r, ImGuiColorEditFlags_AlphaBar); break;
        case FieldType::String:
            if (f.options.multiline)
                c |= ImGui::InputTextMultiline("##v", &f.ref<std::string>(data), {-1, ImGui::GetTextLineHeight() * 3});
            else
                c |= ImGui::InputText("##v", &f.ref<std::string>(data));
            break;
        case FieldType::Asset: {
            std::string& value = f.ref<std::string>(data);
            bool picked = ui::assetField("##v", value, projectFiles(extensionsFor(f.options.asset)), "ASSET_PATH");
            c |= picked;
            // Picking an image gives the sprite the image's shape.
            if (picked && info.name == "SpriteRenderer" && f.name == "texture" && !value.empty()) {
                auto& sr = *static_cast<SpriteRenderer*>(data);
                const TextureAsset& t = assets_.texture(value, sr.pixelArt);
                if (t.height > 0 && !t.missing)
                    sr.size = {sr.size.y * t.width / t.height, sr.size.y};
            }
            break;
        }
        case FieldType::Enum: {
            int32_t& v = f.ref<int32_t>(data);
            if (info.name == "UIElement" && f.name == "anchor") {
                c |= ui::anchorGrid(v);
                break;
            }
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
        case FieldType::EntityRef: c |= entityPicker("##v", f.ref<UUID>(data), e, "(none)"); break;
        }
        if (c) {
            changed = true;
            if (!playing_)
                edited("Change " + f.label);
            else
                noteLiveChange(e, info.name + "/" + f.name);
            // With several objects selected, the change applies to all of them.
            Json value = saveField(f, data);
            for (Entity other : inspected_) {
                if (other == e)
                    continue;
                if (void* od = info.get(scene().registry(), other)) {
                    loadField(f, od, value);
                    if (playing_)
                        noteLiveChange(other, info.name + "/" + f.name);
                }
            }
        }
        ImGui::PopID();
    }
    return changed;
}

bool Editor::entityPicker(const char* label, UUID& id, Entity self, const char* noneLabel) {
    bool c = false;
    ImGui::PushID(label);
    Entity target = scene().findByUUID(id);
    std::string current = target ? scene().info(target).name : noneLabel;
    if (ImGui::BeginCombo("##pick", current.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable(noneLabel, !target)) {
            id = {};
            c = true;
        }
        scene().walk([&](Entity x, int depth) {
            if (x == self)
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
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick an object, or drag one here from the Hierarchy.");
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
            id = {*static_cast<const uint64_t*>(pl->Data)};
            c = true;
        }
        ImGui::EndDragDropTarget();
    }
    ImGui::PopID();
    return c;
}

namespace {

// Function names in a script file (for "Call script function").
std::vector<std::string> scriptFunctions(const std::filesystem::path& file) {
    std::vector<std::string> names;
    auto text = fs::readText(file);
    if (!text)
        return names;
    std::string source = fs::extension(file.string()) == ".blocks" ? blocks::compileFile(*text) : *text;
    size_t pos = 0;
    while (pos < source.size()) {
        size_t nl = source.find('\n', pos);
        std::string_view line(source.data() + pos, (nl == std::string::npos ? source.size() : nl) - pos);
        if (line.starts_with("def ")) {
            size_t open = line.find('(');
            if (open != std::string_view::npos) {
                std::string name(line.substr(4, open - 4));
                // Engine events run by themselves; the list is for your own functions.
                if (!name.starts_with("on_"))
                    names.push_back(name);
            }
        }
        if (nl == std::string::npos)
            break;
        pos = nl + 1;
    }
    return names;
}

const char* clickDoHelp(ClickDo d) {
    switch (d) {
    case ClickDo::LoadScene: return "Goes to another scene, like a level or a menu.";
    case ClickDo::RestartScene: return "Starts this scene again from the beginning.";
    case ClickDo::Quit: return "Closes the game. (In the editor, it stops playing.)";
    case ClickDo::Pause: return "Pauses the game, or resumes it when it is paused. Buttons still work while paused.";
    case ClickDo::Show: return "Turns an object on, like opening a menu panel. Children come with it.";
    case ClickDo::Hide: return "Turns an object off, like closing a menu panel.";
    case ClickDo::ShowHide: return "Turns an object on if it's off, and off if it's on.";
    case ClickDo::Broadcast: return "Sends a message to every script: def on_message(message, data).";
    case ClickDo::PlaySound: return "Plays a sound file.";
    case ClickDo::SetGameValue: return "Sets a game value that every script can read, like game.coins.";
    case ClickDo::AddToGameValue: return "Adds to a game value (use a negative number to take away).";
    case ClickDo::Spawn: return "Makes a copy of a prefab at an object's position.";
    case ClickDo::Destroy: return "Removes an object from the game.";
    case ClickDo::CallFunction: return "Runs a function from an object's script. It gets the number as its value.";
    case ClickDo::Count: break;
    }
    return "";
}

} // namespace

// Tells when nothing in the game sets the value a bar shows (it would stay full forever).
void Editor::drawValueBarHint(Entity e) {
    const std::string& counter = scene().registry().get<ValueBar>(e).counter;
    if (counter.empty())
        return;
    bool set = std::find(codeIndex_.gameValues.begin(), codeIndex_.gameValues.end(), counter) != codeIndex_.gameValues.end();
    auto& reg = scene().registry();
    for (Entity x : reg.entitiesWith<Health>())
        set |= reg.get<Health>(x).counter == counter;
    for (Entity x : reg.entitiesWith<Collectible>())
        set |= reg.get<Collectible>(x).counter == counter;
    for (Entity x : reg.entitiesWith<Clickable>())
        set |= reg.get<Clickable>(x).counter == counter;
    for (Entity x : reg.entitiesWith<ClickActions>())
        for (auto& st : reg.get<ClickActions>(x).steps)
            set |= (st.action == ClickDo::SetGameValue || st.action == ClickDo::AddToGameValue) && st.text == counter;
    if (set)
        return;
    ImGui::PushTextWrapPos(0);
    ImGui::TextColored({1, 0.75f, 0.35f, 1}, "Nothing in this scene sets game.%s yet, so the bar stays full.", counter.c_str());
    ImGui::TextDisabled("Give the player a Health behavior, or set it in a script: game.%s = 3", counter.c_str());
    ImGui::PopTextWrapPos();
}

void Editor::drawClickActions(Entity e, const std::vector<Entity>& selection) {
    auto& ca = scene().registry().get<ClickActions>(e);
    bool changed = false;
    int removeAt = -1, moveFrom = -1, moveTo = -1;
    bool locked = playing_;
    auto& labels = clickDoLabels();
    auto row = [&](const char* label) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%s", label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
    };
    ImGui::BeginDisabled(locked);
    if (ca.steps.empty())
        ImGui::TextWrapped("When this is clicked, nothing happens yet. Add what should happen, step by step.");
    for (size_t i = 0; i < ca.steps.size(); ++i) {
        ClickStep& st = ca.steps[i];
        ImGui::PushID(static_cast<int>(i));
        // Header: number, what it does, move and remove buttons.
        float buttons = ImGui::GetFrameHeight() * 3 + ImGui::GetStyle().ItemSpacing.x * 3;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d.", static_cast<int>(i) + 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttons);
        int action = static_cast<int>(st.action);
        if (ImGui::BeginCombo("##do", labels[static_cast<size_t>(action)].c_str(), ImGuiComboFlags_HeightLarge)) {
            for (int k = 0; k < static_cast<int>(ClickDo::Count); ++k) {
                if (ImGui::Selectable(labels[static_cast<size_t>(k)].c_str(), k == action) && k != action) {
                    st.action = static_cast<ClickDo>(k);
                    st.text.clear();
                    st.number = st.action == ClickDo::AddToGameValue ? 1.0f : 0.0f;
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", clickDoHelp(static_cast<ClickDo>(k)));
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", clickDoHelp(st.action));
        ImGui::SameLine();
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up))
            moveFrom = static_cast<int>(i), moveTo = static_cast<int>(i) - 1;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(i + 1 == ca.steps.size());
        if (ImGui::ArrowButton("##down", ImGuiDir_Down))
            moveFrom = static_cast<int>(i), moveTo = static_cast<int>(i) + 1;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0}))
            removeAt = static_cast<int>(i);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove this step");

        // The settings this kind of step needs.
        if (ImGui::BeginTable("##step", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            auto file = [&](const char* label, AssetKind kind) {
                row(label);
                changed |= ui::assetField("##file", st.text, projectFiles(extensionsFor(kind)), "ASSET_PATH");
            };
            auto target = [&](const char* label) {
                row(label);
                changed |= entityPicker("##target", st.target, {}, "(this object)");
            };
            auto name = [&](const char* label, const char* hint, const std::vector<std::string>& known) {
                row(label);
                float pick = known.empty() ? 0 : ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - pick);
                changed |= ImGui::InputTextWithHint("##name", hint, &st.text);
                if (!known.empty()) {
                    ImGui::SameLine();
                    if (ImGui::ArrowButton("##known", ImGuiDir_Down))
                        ImGui::OpenPopup("known");
                    if (ImGui::BeginPopup("known")) {
                        for (auto& k : known)
                            if (ImGui::Selectable(k.c_str(), k == st.text)) {
                                st.text = k;
                                changed = true;
                            }
                        ImGui::EndPopup();
                    }
                }
            };
            auto number = [&](const char* label, const char* tip) {
                row(label);
                changed |= ImGui::DragFloat("##number", &st.number, 0.1f, 0, 0, "%g");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", tip);
            };
            switch (st.action) {
            case ClickDo::LoadScene: file("Scene", AssetKind::Scene); break;
            case ClickDo::PlaySound: file("Sound", AssetKind::Audio); break;
            case ClickDo::Spawn:
                file("Prefab", AssetKind::Prefab);
                target("At");
                break;
            case ClickDo::Show:
            case ClickDo::Hide:
            case ClickDo::ShowHide:
            case ClickDo::Destroy: target("Object"); break;
            case ClickDo::Broadcast:
                name("Message", "e.g. start_wave", codeIndex_.messages);
                number("Data", "Sent along with the message (the data value of on_message).");
                break;
            case ClickDo::SetGameValue:
            case ClickDo::AddToGameValue:
                name("Game value", "e.g. coins", codeIndex_.gameValues);
                number(st.action == ClickDo::SetGameValue ? "Set to" : "Add", "The number to use.");
                break;
            case ClickDo::CallFunction: {
                target("Object");
                std::vector<std::string> functions;
                Entity who = st.target ? scene().findByUUID(st.target) : e;
                if (who)
                    if (auto* sc = scene().registry().tryGet<Script>(who); sc && !sc->path.empty())
                        functions = scriptFunctions(projectDir_ / sc->path);
                name("Function", "e.g. open_door", functions);
                number("Value", "Given to the function if it takes a value.");
                break;
            }
            case ClickDo::RestartScene:
            case ClickDo::Quit:
            case ClickDo::Pause:
            case ClickDo::Count: break;
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::Spacing();
    }
    if (ImGui::Button("+ Add step", {-1, 0})) {
        ClickStep st;
        // Guess a useful first step: a button on a menu screen usually starts the game.
        if (ca.steps.empty() && scene().registry().has<UIButton>(e)) {
            std::string label = scene().registry().get<UIButton>(e).text;
            std::transform(label.begin(), label.end(), label.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (label.find("quit") != std::string::npos || label.find("exit") != std::string::npos)
                st.action = ClickDo::Quit;
            else if (label.find("restart") != std::string::npos || label.find("again") != std::string::npos)
                st.action = ClickDo::RestartScene;
            else if (label.find("pause") != std::string::npos || label.find("resume") != std::string::npos)
                st.action = ClickDo::Pause;
        }
        ca.steps.push_back(st);
        changed = true;
    }
    ImGui::EndDisabled();
    if (removeAt >= 0) {
        ca.steps.erase(ca.steps.begin() + removeAt);
        changed = true;
    }
    if (moveFrom >= 0) {
        std::swap(ca.steps[static_cast<size_t>(moveFrom)], ca.steps[static_cast<size_t>(moveTo)]);
        changed = true;
    }
    if (changed) {
        edited("Change click actions");
        // With several objects selected, they all get the same steps.
        for (Entity other : selection)
            if (other != e)
                if (auto* o = scene().registry().tryGet<ClickActions>(other))
                    o->steps = ca.steps;
    }
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
        if (overridden && ImGui::BeginPopupContextItem("##reset_var")) {
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
                noteLiveChange(e, "script/" + ex.name);
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
    if (info.name == "Tilemap")
        return unlocked(Feature::Tilemap);
    if (info.name == "NativeScript")
        return unlocked(Feature::NativeCode);
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
        // A component copied from another object (right-click a component > Copy values).
        if (const ComponentInfo* copied = componentClipboardType_.empty() ? nullptr : ComponentRegistry::find(componentClipboardType_);
            copied && !copied->get(reg, e) && filter.empty()) {
            if (ImGui::Selectable(("   Paste " + displayName(copied->name) + " (copied)").c_str())) {
                recordUndo("Paste " + copied->name);
                loadComponent(*copied, copied->add(reg, e), componentClipboard_);
                ensureRequirements(e, copied->name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        for (auto& info : ComponentRegistry::all()) {
            if (info.get(reg, e) || (info.advanced && !advanced()) || !componentUnlocked(info))
                continue;
            std::string hay = info.name + " " + displayName(info.name) + " " + info.description + " " + info.category;
            std::string needle = filter;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
            if (!needle.empty() && hay.find(needle) == std::string::npos)
                continue;
            if (info.category != lastCategory) {
                ImGui::TextDisabled("%s", info.category.c_str());
                lastCategory = info.category;
            }
            if (ImGui::Selectable(("   " + displayName(info.name)).c_str())) {
                recordUndo("Add " + info.name);
                info.add(reg, e);
                ensureRequirements(e, info.name);
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
    if (focusInspector_) {
        ImGui::SetNextWindowFocus();
        focusInspector_ = false;
    }
    ImGui::Begin("Inspector", &showInspector_);
    Scene& s = scene();
    // A locked Inspector keeps showing one object while you select others (to drag them into its settings).
    Entity locked = inspectorLock_ ? s.findByUUID(inspectorLock_) : Entity{};
    if (inspectorLock_ && !locked)
        inspectorLock_ = {};
    Entity e = locked ? locked : selected();
    inspected_ = locked ? std::vector<Entity>{locked} : selectedEntities();
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
    auto selection = inspected_;
    if (selection.size() > 1) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_Header));
        ImGui::BeginChild("##multi", {0, ImGui::GetFrameHeight() * 1.3f}, ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::Text("%d objects selected", static_cast<int>(selection.size()));
        ImGui::SameLine();
        ImGui::TextDisabled("- changes apply to all of them");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    if (ImGui::Checkbox("##active", &info.active))
        edited("Toggle active");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Active: turn off to hide this object and stop its scripts.");
    ImGui::SameLine();
    float small = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(-small);
    ImGui::PushFont(fonts.bold);
    if (ImGui::InputText("##name", &info.name))
        edited("Rename");
    ImGui::PopFont();
    ImGui::SameLine();
    {
        bool isLocked = static_cast<bool>(locked);
        if (isLocked)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        ImVec2 p = ImGui::GetCursorScreenPos();
        if (ImGui::Button("##lock", {ImGui::GetFrameHeight(), ImGui::GetFrameHeight()}))
            inspectorLock_ = isLocked ? UUID{} : info.uuid;
        if (isLocked)
            ImGui::PopStyleColor();
        // A little padlock.
        float h = ImGui::GetFrameHeight();
        ImU32 col = ImGui::GetColorU32(isLocked ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 c = {p.x + h * 0.5f, p.y + h * 0.5f};
        dl->AddRectFilled({c.x - h * 0.22f, c.y - h * 0.02f}, {c.x + h * 0.22f, c.y + h * 0.26f}, col, 2);
        dl->PathArcTo({c.x, c.y - h * 0.04f}, h * 0.14f, 3.14159f, isLocked ? 6.28318f : 5.5f, 10);
        dl->PathStroke(col, 0, 1.8f);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(isLocked ? "Locked: the Inspector keeps showing this object. Click to unlock."
                                       : "Lock the Inspector to this object, so you can select others\n"
                                         "(for example, to drag them into its settings).");
    }
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Tag");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-small);
    if (ImGui::InputTextWithHint("##tag", "e.g. enemy, coin, player", &info.tag))
        edited("Change tag");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tags group objects: find_all(\"enemy\"), is_touching(\"coin\")...");
    ImGui::SameLine();
    if (ImGui::ArrowButton("##tags", ImGuiDir_Down))
        ImGui::OpenPopup("tag_list");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick a tag already used in this scene");
    if (ImGui::BeginPopup("tag_list")) {
        std::set<std::string> tags = {"player", "enemy", "coin", "ground"};
        s.walk([&](Entity x, int) {
            if (!s.info(x).tag.empty())
                tags.insert(s.info(x).tag);
            return true;
        });
        if (ImGui::Selectable("(no tag)", info.tag.empty())) {
            info.tag.clear();
            edited("Change tag");
        }
        for (auto& t : tags)
            if (ImGui::Selectable(t.c_str(), t == info.tag)) {
                info.tag = t;
                edited("Change tag");
                for (Entity other : selection)
                    s.info(other).tag = t;
            }
        ImGui::EndPopup();
    }
    if (!playing_)
        drawAssistant();
    if (auto* pi = s.registry().tryGet<PrefabInstance>(e)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Prefab: %s", stdfs::path(pi->path).stem().string().c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A copy of %s.\nSettings changed on this copy only have bold names with a bar.", pi->path.c_str());
        if (!playing_) {
            std::string path = pi->path;
            ImGui::SameLine();
            if (ImGui::SmallButton("Open"))
                openPrefab(path);
            ImGui::SameLine();
            if (ImGui::SmallButton("Apply"))
                applyToPrefab(e);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Save this object's changes into the prefab (all copies update).");
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert")) {
                recordUndo("Revert to prefab");
                revertToPrefab(e);
                ImGui::End();
                return;
            }
        }
    }
    ImGui::Separator();

    auto& reg = s.registry();
    for (auto& ci : ComponentRegistry::all()) {
        void* data = ci.get(reg, e);
        if (!data)
            continue;
        if (ci.advanced && !advanced() && ci.name != "PrefabInstance" && ci.name != "PostProcessing")
            continue;
        // With several objects selected, only show what they all have.
        bool shared = true;
        for (Entity other : selection)
            if (!ci.get(reg, other))
                shared = false;
        if (!shared)
            continue;
        ImGui::PushID(ci.name.c_str());
        bool keep = true;
        ImVec2 headerPos = ImGui::GetCursorScreenPos();
        bool open = ImGui::CollapsingHeader(displayName(ci.name).c_str(), ci.removable && !playing_ ? &keep : nullptr,
                                            ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
        // A colored stripe tells component families apart.
        ImGui::GetWindowDrawList()->AddRectFilled(headerPos, {headerPos.x + 3, headerPos.y + ImGui::GetFrameHeight()},
                                                  categoryColor(ci.category));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s\n(Right-click for more)", ci.description.c_str());
        if (ImGui::BeginPopupContextItem("##component_menu")) {
            ImGui::TextDisabled("%s  (%s in code)", displayName(ci.name).c_str(), ci.name.c_str());
            if (ImGui::MenuItem("Copy values")) {
                componentClipboard_ = saveComponent(ci, data);
                componentClipboardType_ = ci.name;
            }
            if (ImGui::MenuItem("Paste values", nullptr, false, componentClipboardType_ == ci.name && !playing_)) {
                recordUndo("Paste " + ci.name);
                for (Entity target : selection)
                    if (void* td = ci.get(reg, target))
                        loadComponent(ci, td, componentClipboard_);
            }
            if (ImGui::MenuItem("Reset to defaults", nullptr, false, !playing_)) {
                recordUndo("Reset " + ci.name);
                for (Entity target : selection) {
                    if (ci.removable) {
                        ci.remove(reg, target);
                        ci.add(reg, target);
                    } else if (void* td = ci.get(reg, target)) {
                        loadComponent(ci, td, componentDefaults(ci));
                    }
                }
            }
            if (ci.removable && ImGui::MenuItem("Remove", nullptr, false, !playing_))
                keep = false;
            if (ci.category == "Behaviors" && unlocked(Feature::CodeLadder)) {
                ImGui::Separator();
                if (ImGui::MenuItem("Show as code"))
                    openCodeLadderForBehavior(e, ci.name);
                if (unlocked(Feature::Code) &&
                    ImGui::MenuItem("Turn into a script", nullptr, false, !playing_ && !reg.has<Script>(e) && selection.size() == 1))
                    behaviorToScript(e, ci.name);
            }
            ImGui::Separator();
            ImGui::PushTextWrapPos(300);
            ImGui::TextDisabled("%s", ci.description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndPopup();
        }
        if (!keep) {
            recordUndo("Remove " + ci.name);
            for (Entity target : selection)
                ci.remove(reg, target);
            ImGui::PopID();
            continue;
        }
        if (open) {
            if (ci.name == "ParticleEmitter")
                drawParticlePresets(e, selection);
            if (ci.name == "Tilemap" && unlocked(Feature::Tilemap)) {
                bool painting = showTilePainter_;
                if (ImGui::Button(painting ? "Stop painting" : "Paint tiles", {-1, 28})) {
                    if (painting)
                        showTilePainter_ = false;
                    else
                        openTilePainter();
                }
            }
            if (ci.name == "ValueBar")
                drawValueBarHint(e);
            if (ci.name == "ClickActions") {
                drawClickActions(e, selection);
            } else if (ci.name == "NativeScript") {
                drawNativeScriptInspector(e);
            } else if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp)) {
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
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10, ImGui::GetWindowWidth() - 390));
    ImGui::SetNextItemWidth(170);
    ImGui::InputTextWithHint("##assetsearch", "Search files", &assetSearch_);
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    ImGui::SliderFloat("##cell", &assetCell_, 64, 160, "");
    ImGui::SameLine();
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
    if (!assetSearch_.empty()) {
        // Searching looks through every folder.
        std::string needle = assetSearch_;
        std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
        for (auto& f : assetFiles_) {
            std::string hay = f;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(needle) != std::string::npos)
                entries.emplace_back(projectDir_ / f, ec);
        }
    } else {
        for (auto& entry : stdfs::directory_iterator(dir, ec)) {
            std::string name = entry.path().filename().string();
            if (name.empty() || name[0] == '.' || name == ProjectSettings::kFileName || name == "tutorial.json")
                continue;
            entries.push_back(entry);
        }
    }
    std::sort(entries.begin(), entries.end(), [](auto& a, auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory();
        return a.path().filename() < b.path().filename();
    });
    float cell = assetCell_;
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
            else if (ext == ".c" || ext == ".cpp" || ext == ".cc") { col = IM_COL32(90, 140, 230, 255); badge = ext == ".c" ? "C" : "C++"; }
            else if (ext == ".h" || ext == ".hpp") { col = IM_COL32(120, 150, 200, 255); badge = "HEADER"; }
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
        size_t maxChars = static_cast<size_t>(std::max(6.0f, cell / 7.0f));
        std::string shown = name.size() > maxChars ? name.substr(0, maxChars - 2) + ".." : name;
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
            else if (ext == ".es" || ext == ".blocks" || isNativeSource(rel))
                openScript(rel);
            else if (ext == ".prefab")
                openPrefab(rel);
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
            if (ext == ".prefab" && ImGui::MenuItem("Add to scene"))
                instantiatePrefab(rel, view3D_ ? Vec3{0, 0, 0} : Vec3{cam2D_.x, cam2D_.y, 0});
            if (ext == ".prefab" && ImGui::MenuItem("Edit prefab"))
                openPrefab(rel);
            if (ext == ".png" && unlocked(Feature::PixelEditor) && ImGui::MenuItem("Edit in Pixel Editor"))
                openPixelEditor(rel);
            if (!entry.is_directory() && ImGui::MenuItem("Duplicate")) {
                stdfs::path relPath(rel);
                std::string copy = uniqueName(relPath.parent_path().generic_string(), relPath.stem().string() + "_copy", ext);
                std::error_code copyError;
                stdfs::copy_file(projectDir_ / rel, projectDir_ / copy, copyError);
                if (copyError)
                    notify("Couldn't duplicate " + rel + ": " + copyError.message(), true);
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
    int counts[3] = {0, 0, 0};
    for (auto& l : console_)
        ++counts[l.level == LogLevel::Error ? 2 : l.level == LogLevel::Warning ? 1 : 0];
    auto toggle = [](const char* label, bool& on, ImU32 color) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, on ? color : ImGui::GetColorU32(ImGuiCol_FrameBg));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? IM_COL32(255, 255, 255, 255) : ImGui::GetColorU32(ImGuiCol_TextDisabled));
        if (ImGui::SmallButton(label))
            on = !on;
        ImGui::PopStyleColor(2);
    };
    toggle(("Messages " + std::to_string(counts[0])).c_str(), showInfo_, IM_COL32(80, 90, 110, 255));
    toggle(("Warnings " + std::to_string(counts[1])).c_str(), showWarnings_, IM_COL32(170, 120, 20, 255));
    toggle(("Errors " + std::to_string(counts[2])).c_str(), showErrors_, IM_COL32(170, 45, 50, 255));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x - 130));
    ImGui::InputTextWithHint("##consolesearch", "Search messages", &consoleFilter_);
    ImGui::SameLine();
    if (ImGui::Checkbox("Clear on play", &prefs.clearConsoleOnPlay))
        prefs.save();
    ImGui::Separator();
    ImGui::BeginChild("##log", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(fonts.code);
    int index = 0;
    std::string needle = consoleFilter_;
    std::transform(needle.begin(), needle.end(), needle.begin(), ::tolower);
    for (auto& l : console_) {
        bool show = l.level == LogLevel::Error ? showErrors_ : l.level == LogLevel::Warning ? showWarnings_ : showInfo_;
        if (!show)
            continue;
        if (!needle.empty()) {
            std::string hay = l.file + " " + l.text;
            std::transform(hay.begin(), hay.end(), hay.begin(), ::tolower);
            if (hay.find(needle) == std::string::npos)
                continue;
        }
        ImVec4 color = l.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1) : l.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1) : ImVec4(0.85f, 0.87f, 0.9f, 1);
        ImGui::PushID(index++);
        if (l.level != LogLevel::Info && unlocked(Feature::Doctor)) {
            if (ImGui::SmallButton("Doctor"))
                openDoctorFor(l.text, l.file, l.line);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Explain this in plain words, with a fix if there is one");
            ImGui::SameLine();
        }
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
                if (isNativeSource(tab.path)) {
                    ImGui::TextDisabled("C/C++");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(nativeBuild_ != nullptr);
                    if (ImGui::SmallButton(nativeBuild_ ? "Building..." : "Save and Build (Ctrl+B)")) {
                        fs::writeText(projectDir_ / tab.path, tab.code->text());
                        tab.modified = false;
                        buildNativeModule();
                    }
                    ImGui::EndDisabled();
                    ImGui::SameLine();
                    if (ImGui::SmallButton("aven.h"))
                        openScript("native/include/aven.h");
                } else {
                ImGui::TextDisabled("EasyScript");
                ImGui::SameLine();
                if (ImGui::SmallButton("Reference"))
                    showReference_ = true;
                if (unlocked(Feature::CodeLadder)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Code Ladder"))
                        openCodeLadderForScript(tab.path);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("See this script in C (Aven native code), C# (Unity), GDScript (Godot), Luau (Roblox) and C++ (Unreal)");
                }
                }
                // Jump to a function or variable.
                auto items = tab.code->outline();
                if (!items.empty()) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11);
                    if (ImGui::BeginCombo("##outline", "Go to...", ImGuiComboFlags_HeightLarge)) {
                        for (size_t i = 0; i < items.size(); ++i) {
                            ImGui::PushID(static_cast<int>(i));
                            std::string label = (items[i].function ? "def " : "") + items[i].name;
                            if (ImGui::Selectable(label.c_str()))
                                tab.code->gotoPosition(items[i].line, 0);
                            ImGui::SameLine(ImGui::GetFontSize() * 12);
                            ImGui::TextDisabled("line %d", items[i].line + 1);
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Jump to a function or variable in this file");
                }
                ImVec2 avail = ImGui::GetContentRegionAvail();
                if (tab.code->draw("##code", avail)) {
                    tab.modified = true;
                    if (!tab.code->intel)
                        checkScript(tab); // with code intelligence, problems are checked as you type
                }
            } else if (tab.blocks) {
                if (ImGui::Button("Save (Ctrl+S)")) {
                    fs::writeText(projectDir_ / tab.path, tab.blocks->save().dump(2));
                    tab.modified = false;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Show code", &tab.blocks->showCode);
                if (unlocked(Feature::CodeLadder)) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Code Ladder"))
                        openCodeLadderForScript(tab.path);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Climb from blocks to EasyScript, then to the code other engines use");
                }
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
    changed |= ImGui::InputTextMultiline("Description", &settings_.description, {0, ImGui::GetTextLineHeight() * 3.5f});
    ui::helpMarker("One or two sentences about your game, shown on its web page and game card.");
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
            std::string hay = e.name + " " + e.signature + " " + e.help;
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
            if (!e.help.empty()) {
                ImGui::PopFont();
                ImGui::Indent(ImGui::GetTreeNodeToLabelSpacing());
                ImGui::PushTextWrapPos(0);
                ImGui::TextDisabled("%s", e.help.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Unindent(ImGui::GetTreeNodeToLabelSpacing());
                ImGui::PushFont(fonts.code);
            }
        }
    }
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::End();
}

} // namespace aven::editor
