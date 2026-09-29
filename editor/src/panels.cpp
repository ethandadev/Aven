#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/scene/reflection.h"
#include "aven/script/vm.h"
#include "block_editor.h"
#include "code_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <set>

namespace aven::editor {

// ---------------------------------------------------------------- helpers

namespace ui {

namespace {
float gPixelScale = 1.0f;
}

float px(float size) { return size * gPixelScale; }
void setPixelScale(float scale) { gPixelScale = scale > 0 ? scale : 1.0f; }

ImVec2 fitted(ImVec2 size) {
    ImVec2 work = ImGui::GetMainViewport()->WorkSize;
    return {std::min(px(size.x), work.x * 0.95f), std::min(px(size.y), work.y * 0.95f)};
}

void placeWindow(ImVec2 size, ImVec2 where) {
    // First time a tool window opens: its size (at 100% UI scale) and a spot on screen (fractions of the window).
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(fitted(size), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * where.x, vp->Pos.y + vp->Size.y * where.y}, ImGuiCond_FirstUseEver,
                            {0.5f, 0.5f});
}

void panelClass() {
    // Docked panels close from their tab; hide the extra close button on the dock node.
    ImGuiWindowClass wc;
    wc.DockNodeFlagsOverrideSet = ImGuiDockNodeFlags_NoCloseButton;
    ImGui::SetNextWindowClass(&wc);
}

std::string ellipsize(const std::string& text, float width, ImFont* font) {
    ImFont* f = font ? font : ImGui::GetFont();
    float size = font ? font->FontSize : ImGui::GetFontSize();
    auto measure = [&](const std::string& t) { return f->CalcTextSizeA(size, FLT_MAX, 0, t.c_str()).x; };
    if (measure(text) <= width)
        return text;
    std::string cut = text;
    while (!cut.empty()) {
        // Back to the start of the last character (UTF-8 continuation bytes are 10xxxxxx).
        size_t end = cut.size() - 1;
        while (end > 0 && (static_cast<unsigned char>(cut[end]) & 0xC0) == 0x80)
            --end;
        cut.erase(end);
        while (!cut.empty() && cut.back() == ' ')
            cut.pop_back();
        if (measure(cut + "...") <= width)
            break;
    }
    return cut + "...";
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
    case Save: { // a floppy disk
        float r = s * 0.95f;
        dl->AddRect({m.x - r, m.y - r}, {m.x + r, m.y + r}, fg, 2, 0, 1.8f);
        dl->AddRectFilled({m.x - r * 0.5f, m.y - r}, {m.x + r * 0.45f, m.y - r * 0.3f}, fg);
        dl->AddRect({m.x - r * 0.6f, m.y + r * 0.1f}, {m.x + r * 0.6f, m.y + r}, fg, 1, 0, 1.5f);
        break;
    }
    case Undo:
    case Redo: { // a curved arrow, pointing left (undo) or right (redo)
        float dir = icon == Undo ? 1.0f : -1.0f;
        ImVec2 c{m.x + dir * s * 0.05f, m.y + s * 0.25f};
        if (icon == Undo)
            dl->PathArcTo(c, s * 0.8f, -3.14159f * 0.5f, 3.14159f * 0.5f, 14);
        else
            dl->PathArcTo(c, s * 0.8f, 3.14159f * 1.5f, 3.14159f * 0.5f, 14);
        dl->PathStroke(fg, 0, 2);
        ImVec2 tip{c.x - dir * s * 0.45f, c.y - s * 0.8f};
        dl->AddTriangleFilled({tip.x - dir * s * 0.45f, tip.y}, {tip.x + dir * s * 0.1f, tip.y - s * 0.4f},
                              {tip.x + dir * s * 0.1f, tip.y + s * 0.4f}, fg);
        dl->AddLine({tip.x, tip.y}, {c.x, tip.y}, fg, 2);
        if (icon == Undo)
            dl->AddLine({c.x, c.y + s * 0.8f}, {c.x - s * 0.5f, c.y + s * 0.8f}, fg, 2);
        else
            dl->AddLine({c.x, c.y + s * 0.8f}, {c.x + s * 0.5f, c.y + s * 0.8f}, fg, 2);
        break;
    }
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
    // The same colors as the move arrows in the Scene view.
    const ImU32 kColors[3] = {axisColorU32(0), axisColorU32(1), axisColorU32(2)};
    static const char* kNames[3] = {"X", "Y", "Z"};
    bool changed = false;
    float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
    float avail = ImGui::GetContentRegionAvail().x;
    float width = (avail - spacing * static_cast<float>(count - 1)) / static_cast<float>(count);
    // Too narrow for "-12.50" side by side (a slim Inspector, big text): one under the other instead.
    bool stacked = count > 1 && width < ImGui::CalcTextSize("-00.00").x + ImGui::GetStyle().FramePadding.x * 2 + px(3);
    if (stacked)
        width = avail;
    float h = ImGui::GetFrameHeight();
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i);
        if (i && !stacked)
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

std::string lowered(std::string text) {
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    return text;
}

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

namespace {

// Colored dot shown next to objects in the hierarchy.
ImU32 entityColor(Registry& reg, Entity e) {
    if (reg.has<Camera>(e)) return IM_COL32(120, 190, 255, 255);
    if (reg.has<Light>(e)) return IM_COL32(255, 210, 90, 255);
    if (reg.has<UIElement>(e)) return IM_COL32(120, 230, 170, 255);
    if (reg.has<MeshRenderer>(e)) return IM_COL32(180, 150, 255, 255);
    if (reg.has<SpriteRenderer>(e)) {
        Color c = reg.get<SpriteRenderer>(e).color; // (can be above 1 for a glow)
        auto byte = [](float v) { return static_cast<int>(std::clamp(v, 0.0f, 1.0f) * 255.0f); };
        return IM_COL32(byte(c.r), byte(c.g), byte(c.b), 255);
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

// Does an object match a Hierarchy search? Words must all match: plain words search names,
// t:Component, tag:name and layer:name filter (all ignoring case).
bool Editor::matchesSearch(Entity e, const std::string& query) {
    Scene& s = scene();
    auto& reg = s.registry();
    const EntityInfo& info = s.info(e);
    std::string q = lowered(query);
    size_t start = 0;
    while (start < q.size()) {
        size_t space = q.find(' ', start);
        std::string term = q.substr(start, space == std::string::npos ? std::string::npos : space - start);
        start = space == std::string::npos ? q.size() : space + 1;
        if (term.empty())
            continue;
        auto after = [&](const char* prefix) { return term.rfind(prefix, 0) == 0 ? term.substr(std::strlen(prefix)) : std::string("\x01"); };
        if (std::string want = after("t:"); want != "\x01") {
            bool found = false;
            if (want == "prefab")
                found = reg.has<PrefabInstance>(e);
            for (auto& ci : ComponentRegistry::all()) {
                if (found || !ci.get(reg, e))
                    continue;
                std::string name = lowered(ci.name), spaced = lowered(displayName(ci.name));
                found = name.find(want) != std::string::npos || spaced.find(want) != std::string::npos;
            }
            if (!found)
                return false;
        } else if (std::string want = after("tag:"); want != "\x01") {
            if (lowered(info.tag).find(want) == std::string::npos || (want.empty() && !info.tag.empty()))
                return false;
        } else if (std::string want = after("layer:"); want != "\x01") {
            if (lowered(info.layer.empty() ? "Default" : info.layer).find(want) == std::string::npos)
                return false;
        } else if (lowered(info.name).find(term) == std::string::npos) {
            return false;
        }
    }
    return true;
}

// With a search typed in, the Hierarchy lists the matches flat, each with where it sits.
void Editor::drawHierarchySearch() {
    Scene& s = scene();
    auto& reg = s.registry();
    std::vector<Entity> found;
    s.walk([&](Entity e, int) {
        if (matchesSearch(e, hierarchyFilter_))
            found.push_back(e);
        return true;
    });
    ImGui::TextDisabled("%d found", static_cast<int>(found.size()));
    if (!found.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Select all")) {
            selection_.clear();
            for (Entity e : found)
                addToSelection(e);
        }
    }
    for (Entity e : found) {
        const EntityInfo& info = s.info(e);
        hierarchyOrder_.push_back(info.uuid);
        ImGui::PushID(static_cast<int>(e.index));
        ImVec2 p = ImGui::GetCursorScreenPos();
        std::string path;
        for (Entity parent = s.parent(e); parent; parent = s.parent(parent))
            path = s.info(parent).name + (path.empty() ? "" : " / " + path);
        ImGui::Selectable(("     " + info.name).c_str(), isSelected(e), ImGuiSelectableFlags_AllowDoubleClick);
        if (ImGui::IsItemClicked()) {
            ImGuiIO& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                toggleSelection(e);
            } else if (io.KeyShift && hierarchyAnchor_) {
                auto a = std::find(lastHierarchyOrder_.begin(), lastHierarchyOrder_.end(), hierarchyAnchor_);
                auto b = std::find(lastHierarchyOrder_.begin(), lastHierarchyOrder_.end(), info.uuid);
                if (a != lastHierarchyOrder_.end() && b != lastHierarchyOrder_.end()) {
                    if (a > b)
                        std::swap(a, b);
                    selection_.assign(a, b + 1);
                    std::erase(selection_, info.uuid);
                    selection_.push_back(info.uuid);
                }
            } else if (isSelected(e) && selection_.size() > 1) {
                pendingSelect_ = info.uuid;
            } else {
                select(e);
            }
            if (!io.KeyShift)
                hierarchyAnchor_ = info.uuid;
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                focusSelected();
        }
        if (pendingSelect_ == info.uuid && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f)
                select(e);
            pendingSelect_ = {};
        }
        if (!playing_ && ImGui::BeginDragDropSource()) {
            uint64_t id = info.uuid.value;
            ImGui::SetDragDropPayload("ENTITY", &id, sizeof id);
            if (isSelected(e) && selection_.size() > 1)
                ImGui::Text("%d objects", static_cast<int>(selection_.size()));
            else
                ImGui::Text("%s", info.name.c_str());
            ImGui::EndDragDropSource();
        }
        float h = ImGui::GetItemRectSize().y;
        ImGui::GetWindowDrawList()->AddCircleFilled({p.x + 8, p.y + h * 0.5f}, 4.5f, entityColor(reg, e));
        if (!path.empty()) {
            float x = p.x + ImGui::CalcTextSize(("     " + info.name).c_str()).x + 12;
            ImGui::GetWindowDrawList()->AddText({x, p.y + (h - ImGui::GetFontSize()) * 0.5f},
                                                ImGui::GetColorU32(ImGuiCol_TextDisabled), ("in " + path).c_str());
        }
        ImGui::PopID();
    }
    if (found.empty())
        ImGui::TextDisabled("Nothing matches. Try part of a name,\nor t:Sprite, tag:enemy, layer:Bullet.");
}

// What a drag from the Hierarchy moves: the whole selection if the dragged object is part of it,
// without objects whose parent is also moving (they come along), in the order they're listed.
std::vector<Entity> Editor::draggedEntities(Entity dragged) {
    if (!dragged)
        return {};
    return isSelected(dragged) ? topSelection() : std::vector<Entity>{dragged};
}

void Editor::drawEntityNode(Entity e) {
    Scene& s = scene();
    auto& reg = s.registry();
    EntityInfo& info = s.info(e);
    const auto& kids = s.children(e);
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap;
    if (kids.empty())
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (isSelected(e))
        flags |= ImGuiTreeNodeFlags_Selected;
    ImGui::PushID(static_cast<int>(e.index));
    bool dim = !s.isActive(e);
    if (dim)
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    bool open = true;
    bool shown = true;
    // Rows scrolled out of sight get a stand-in of the same height instead of the whole row (a
    // folder of thousands, open, cost most of a frame). They keep their place in the order (arrow
    // keys, Shift-click) and their open state; only rows being renamed, revealed, scrolled to,
    // dragged or clicked are always drawn in full.
    bool cheap = false;
    if (shown) {
        float rowH = ImGui::GetFrameHeight();
        ImVec2 at = ImGui::GetCursorScreenPos();
        const ImGuiPayload* drag = ImGui::GetDragDropPayload();
        bool dragged = drag && drag->IsDataType("ENTITY") && *static_cast<const uint64_t*>(drag->Data) == info.uuid.value;
        bool special = renaming_ == info.uuid || revealIds_.count(info.uuid.value) || pendingSelect_ == info.uuid || dragged ||
                       (scrollToSelected_ && !selection_.empty() && selection_.back() == info.uuid);
        if (!special && !ImGui::IsRectVisible(at, ImVec2(at.x + 1, at.y + rowH))) {
            cheap = true;
            hierarchyOrder_.push_back(info.uuid);
            ImGuiID nodeId = ImGui::GetID("##node");
            open = kids.empty() || ImGui::TreeNodeGetOpen(nodeId); // (a leaf's tree node counts as open)
            hierarchyRows_[info.uuid.value] = {nodeId, open && !kids.empty(), !kids.empty()};
            ImGui::Dummy(ImVec2(1, rowH));
            if (open)
                ImGui::TreePush("##node"); // (what an open tree node does: indent, and its ID)
        }
    }
    if (shown && !cheap) {
        hierarchyOrder_.push_back(info.uuid);
        ImVec2 p = ImGui::GetCursorScreenPos();
        bool renaming = renaming_ == info.uuid;
        if (revealIds_.count(info.uuid.value))
            ImGui::SetNextItemOpen(true); // a folder holding the object selected elsewhere (scene view, Find...)
        bool folder = isFolder(reg, e, !kids.empty());
        open = ImGui::TreeNodeEx("##node", flags, "     %s", renaming ? "" : info.name.c_str());
        const ImGuiID nodeId = ImGui::GetItemID();
        hierarchyRows_[info.uuid.value] = {nodeId, open && !kids.empty(), !kids.empty()}; // leaves count as open to ImGui
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
            } else if (isSelected(e) && selection_.size() > 1) {
                pendingSelect_ = info.uuid; // decided on release: dragging moves the whole selection
            } else {
                select(e);
            }
            if (!io.KeyShift)
                hierarchyAnchor_ = info.uuid;
        }
        if (pendingSelect_ == info.uuid && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseDragMaxDistanceSqr[0] < 16.0f)
                select(e); // a plain click on one of several selected objects: just that one
            pendingSelect_ = {};
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
                    std::vector<Entity> moving = draggedEntities(s.findByUUID({*static_cast<const uint64_t*>(pl->Data)}));
                    // An object can't go into (or beside) itself or its own children.
                    std::erase_if(moving, [&](Entity m) { return m == e || s.isAncestor(m, e); });
                    if (!moving.empty())
                        recordUndo(zone == 0 ? "Change parent" : "Reorder");
                    Entity last; // "after": each one goes after the one before, keeping their order
                    for (Entity m : moving) {
                        if (zone == 0) {
                            s.setParent(m, e);
                        } else {
                            Entity parent = s.parent(e);
                            Entity ref = zone == 1 && last ? last : e;
                            int index = s.siblingIndex(ref) + (zone == 1 ? 1 : 0);
                            if (s.parent(m) == parent && s.siblingIndex(m) < index)
                                --index;
                            s.setParent(m, parent, true, index);
                            last = m;
                        }
                    }
                    if (zone == 0 && !moving.empty())
                        ImGui::GetStateStorage()->SetInt(nodeId, 1); // open the folder they went into
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
            if (ImGui::MenuItem("Duplicate", chordName(prefs.chord("duplicate")).c_str()))
                duplicateSelection();
            if (ImGui::MenuItem("Copy", chordName(prefs.chord("copy")).c_str()))
                copySelection(false);
            if (unlocked(Feature::Code) && ImGui::MenuItem("Copy as code")) {
                std::string code = "find(\"" + info.name + "\")";
                ImGui::SetClipboardText(code.c_str());
                notify("Copied " + code);
            }
            if (ImGui::MenuItem("Paste", chordName(prefs.chord("paste")).c_str(), false, !clipboard_.isNull()))
                pasteClipboard();
            if (ImGui::MenuItem("Paste in Place", chordName(prefs.chord("paste_in_place")).c_str(), false, !clipboard_.isNull()))
                pasteClipboard(true);
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
                if (ImGui::MenuItem("Move up"))
                    reorderSelection(-1);
                if (ImGui::MenuItem("Move down"))
                    reorderSelection(1);
                if (s.parent(e) && ImGui::MenuItem("Move out of parent"))
                    unparentSelection();
                if (ImGui::BeginMenu("Select")) {
                    drawSelectMenu(e);
                    ImGui::EndMenu();
                }
                if (selectedEntities().size() >= 2 && ImGui::BeginMenu("Align")) {
                    drawAlignMenu();
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                size_t count = selectedEntities().size();
                if (unlocked(Feature::Prefabs) && ImGui::MenuItem(count > 1 ? "Save each as a Prefab" : "Save as Prefab"))
                    prefabSelection();
                // With several selected, they all get the same new script.
                auto addScript = [&](bool blocks) {
                    std::string path = newScriptFile(info.name, blocks);
                    recordUndo("Attach script");
                    undoPaused_ = true;
                    for (Entity x : selectedEntities())
                        attachScript(x, path);
                    undoPaused_ = false;
                    blocks ? openBlocks(path) : openScript(path);
                };
                if (ImGui::MenuItem("Add blocks script"))
                    addScript(true);
                if (unlocked(Feature::Code) && ImGui::MenuItem("Add EasyScript"))
                    addScript(false);
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

void Editor::hierarchyKeys() {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || keyboardClaimed() || renaming_ || ImGui::IsAnyItemActive() || hierarchyOrder_.empty())
        return;
    Scene& s = scene();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        selection_.clear();
        return;
    }
    const UUID current = selection_.empty() ? UUID{} : selection_.back();
    auto at = std::find(hierarchyOrder_.begin(), hierarchyOrder_.end(), current);
    // Picks a row: on its own, or (with Shift) everything from the anchor to it.
    auto pick = [&](UUID id) {
        if (io.KeyShift && hierarchyAnchor_) {
            auto a = std::find(hierarchyOrder_.begin(), hierarchyOrder_.end(), hierarchyAnchor_);
            auto b = std::find(hierarchyOrder_.begin(), hierarchyOrder_.end(), id);
            if (a != hierarchyOrder_.end() && b != hierarchyOrder_.end()) {
                auto lo = std::min(a, b), hi = std::max(a, b);
                selection_.assign(lo, hi + 1);
                std::erase(selection_, id);
                selection_.push_back(id); // the one the arrows move from next
                return;
            }
        }
        selection_ = {id};
        hierarchyAnchor_ = id;
    };
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
        if (at == hierarchyOrder_.end())
            pick(hierarchyOrder_.front());
        else if (at + 1 != hierarchyOrder_.end())
            pick(*(at + 1));
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
        if (at == hierarchyOrder_.end())
            pick(hierarchyOrder_.back());
        else if (at != hierarchyOrder_.begin())
            pick(*(at - 1));
    } else if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        pick(hierarchyOrder_.front());
    } else if (ImGui::IsKeyPressed(ImGuiKey_End)) {
        pick(hierarchyOrder_.back());
    } else if (at != hierarchyOrder_.end() && (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_LeftArrow))) {
        auto row = hierarchyRows_.find(current.value);
        if (row == hierarchyRows_.end())
            return;
        ImGuiStorage* storage = ImGui::GetStateStorage();
        if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            if (row->second.hasChildren && !row->second.open)
                storage->SetInt(row->second.nodeId, 1); // open it
            else if (row->second.open && at + 1 != hierarchyOrder_.end())
                pick(*(at + 1)); // already open: into its first child
        } else {
            if (row->second.open)
                storage->SetInt(row->second.nodeId, 0); // close it
            else if (Entity e = s.findByUUID(current); e && s.parent(e))
                pick(s.info(s.parent(e)).uuid); // closed or empty: up to its parent
        }
    }
}

void Editor::drawHierarchy() {
    ui::panelClass();
    ImGui::Begin("Hierarchy", &showHierarchy_);
    hierarchyFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    // Room for the "+ Add" button next to it, however big the text is.
    ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("+ Add").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x));
    ImGui::InputTextWithHint("##filter", "Search...", &hierarchyFilter_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Search by name, or filter:\n  t:RigidBody2D   objects with a component\n  tag:enemy   by tag\n"
                          "  layer:Bullet   by collision layer\n  t:prefab   prefab copies\nCombine them: tag:enemy t:script");
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
        for (const char* k : {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Player 3D", "Terrain"})
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
    // The arrow keys pick rows (hierarchyKeys), so ImGui's own keyboard navigation stays out of the tree.
    ImGui::BeginChild("##tree", {0, 0}, 0, ImGuiWindowFlags_NoNavInputs);
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
    hierarchyRows_.clear();
    if (!hierarchyFilter_.empty()) {
        drawHierarchySearch();
    } else {
        std::vector<Entity> roots = s.roots();
        for (Entity e : roots)
            if (s.valid(e))
                drawEntityNode(e);
    }
    ImGui::PopStyleVar();
    if (hierarchyFocused_)
        hierarchyKeys();
    // Dropping on empty space moves objects to the top level.
    ImGui::Dummy(ImGui::GetContentRegionAvail());
    if (!playing_ && ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ENTITY")) {
            std::vector<Entity> moving = draggedEntities(s.findByUUID({*static_cast<const uint64_t*>(pl->Data)}));
            recordUndo("Change parent");
            for (Entity m : moving)
                s.setParent(m, {});
        }
        // Files from Assets (prefabs, images, models, sounds) go into the scene, in the middle of the view.
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET_PATH"))
            placeAssets(draggedAssets(std::string(static_cast<const char*>(pl->Data), static_cast<size_t>(pl->DataSize))),
                        {viewportSize_.x * 0.5f, viewportSize_.y * 0.5f});
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

// ---------------------------------------------------------------- assets

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
    needle = lowered(needle);
    for (auto& l : console_) {
        bool show = l.level == LogLevel::Error ? showErrors_ : l.level == LogLevel::Warning ? showWarnings_ : showInfo_;
        if (!show)
            continue;
        if (!needle.empty()) {
            std::string hay = l.file + " " + l.text;
            hay = lowered(hay);
            if (hay.find(needle) == std::string::npos)
                continue;
        }
        ImVec4 color = l.level == LogLevel::Error ? ImVec4(1.0f, 0.45f, 0.45f, 1) : l.level == LogLevel::Warning ? ImVec4(1.0f, 0.8f, 0.35f, 1) : ImVec4(0.85f, 0.87f, 0.9f, 1);
        ImGui::PushID(index++);
        if (l.level != LogLevel::Info && unlocked(Feature::Doctor) && prefs.beginnerHelpers) {
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
                if (ImGui::Button(keyText("Save (Ctrl+S)").c_str()))
                    saveTab(tab);
                ImGui::SameLine();
                if (isNativeSource(tab.path)) {
                    ImGui::TextDisabled("C/C++");
                    ImGui::SameLine();
                    ImGui::BeginDisabled(nativeBuild_ != nullptr);
                    if (ImGui::SmallButton(keyText(nativeBuild_ ? "Building..." : "Save and Build (Ctrl+B)").c_str()) &&
                        saveTab(tab))
                        buildNativeModule();
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
                if (ImGui::Button(keyText("Save (Ctrl+S)").c_str()))
                    saveTab(tab);
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
                    saveTab(tab);
                }
            }
        }
        ImGui::End();
        if (!tab.open && tab.modified && !saveTab(tab))
            tab.open = true; // (closing would lose the changes it couldn't save)
        if (!tab.open) {
            it = tabs_.erase(it);
        } else {
            ++it;
        }
    }
}

// ---------------------------------------------------------------- windows

void Editor::drawSettings() {
    ImGui::SetNextWindowSize(ui::fitted({560, 680}), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Project Settings", &showSettings_)) {
        ImGui::End();
        return;
    }
    bool changed = false;
    // Tabs; a link to a part of the settings ("settings:mixer") opens its tab.
    auto tab = [&](const char* label, std::initializer_list<const char*> sections) {
        ImGuiTabItemFlags flags = 0;
        for (const char* section : sections)
            if (settingsSection_ == section) {
                flags = ImGuiTabItemFlags_SetSelected;
                settingsSection_.clear();
            }
        return ImGui::BeginTabItem(label, nullptr, flags);
    };
    if (ImGui::BeginTabBar("##settings_tabs", ImGuiTabBarFlags_DrawSelectedOverline)) {
        if (tab("Game", {"game", "window", "online"})) {
            ui::sectionHeader("Game");
            changed |= ImGui::InputText("Name", &settings_.name);
            changed |= ImGui::InputText("Version", &settings_.version);
            changed |= ImGui::InputTextMultiline("Description", &settings_.description,
                                                 {0, ImGui::GetTextLineHeight() * 3.5f});
            ui::helpMarker("One or two sentences about your game, shown on its web page and game card.");
            if (ImGui::BeginCombo("Start scene", settings_.startScene.c_str())) {
                for (auto& s : projectFiles({".scene"}))
                    if (ImGui::Selectable(s.c_str(), s == settings_.startScene)) {
                        settings_.startScene = s;
                        changed = true;
                    }
                ImGui::EndCombo();
            }
            ui::sectionHeader("Online multiplayer");
            changed |= ImGui::InputTextWithHint("Online relay", "relay.example.com:4243", &settings_.relay);
            ui::helpMarker("The relay server that host_online() and join_online() go through, so friends on different "
                           "networks (over the internet) can play together. It's a small program, aven-relay, that "
                           "runs on any server; the online multiplayer guide explains how. Games on the same Wi-Fi "
                           "don't need one (host_game and join_game).");
            ui::sectionHeader("Window");
            // Kept to what a window can be once typing is done (not on each key: "1920" starts as "1").
            auto windowSide = [&](const char* label, int& value, int low) {
                changed |= ImGui::InputInt(label, &value, 10, 100);
                if (!ImGui::IsItemActive() && (value < low || value > 16384)) { // (also after the - and + buttons)
                    value = std::clamp(value, low, 16384);
                    changed = true;
                }
            };
            windowSide("Width", settings_.width, 160);
            windowSide("Height", settings_.height, 120);
            changed |= ImGui::Checkbox("Resizable", &settings_.resizable);
            changed |= ImGui::Checkbox("Start fullscreen", &settings_.fullscreen);
            changed |= ImGui::Checkbox("VSync (smooth, no tearing)", &settings_.vsync);
            {
                const char* levels[] = {"Low", "Medium", "High", "Ultra"};
                GraphicsQuality q = GraphicsQuality::High;
                parseQuality(settings_.graphicsQuality, q);
                int level = static_cast<int>(q);
                if (ImGui::Combo("Graphics quality", &level, levels, 4)) {
                    settings_.graphicsQuality = levels[level];
                    changed = true;
                }
                ui::helpMarker("What the game starts with. Low: no shadows, fewer pixels, runs on almost anything. "
                               "Medium: sun shadows, bloom. High: every shadow and effect. Ultra: sharper shadows. "
                               "Players can pick their own on the title screen (Build & Share), and scripts can "
                               "with set_graphics_quality(\"Low\"). Pressing Play here uses this too.");
            }
            changed |= ImGui::Checkbox("Advanced mode in the editor", &settings_.advancedMode);

            ImGui::EndTabItem();
        }
        if (tab("Controls", {"controls", "input", "touch"})) {
            ui::sectionHeader("Controls");
            ImGui::TextWrapped(
                "Actions let players change controls. Scripts use them like key_pressed(\"jump\"). "
                "List keys separated by commas.");
            Input& input = window_.input();
            input.loadActions(settings_.inputActions);
            auto actions = input.actions();
            bool actionsChanged = false;
            if (ImGui::BeginTable("##actions", 2, ImGuiTableFlags_BordersInnerH)) {
                ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, ui::px(90));
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
                    if (ImGui::InputText("##keys", &keys, ImGuiInputTextFlags_EnterReturnsTrue) ||
                        ImGui::IsItemDeactivatedAfterEdit()) {
                        a.bindings.clear();
                        size_t start = 0;
                        while (start <= keys.size()) {
                            size_t comma = keys.find(',', start);
                            std::string k = keys.substr(start, comma == std::string::npos ? std::string::npos
                                                                                          : comma - start);
                            k.erase(0, k.find_first_not_of(' '));
                            k.erase(k.find_last_not_of(' ') + 1);
                            if (!k.empty()) {
                                if (!Input::isValidName(k))
                                    notify("\"" + k + "\" isn't a key name I know.", true);
                                a.bindings.push_back(k);
                            }
                            if (comma == std::string::npos) break;
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

            ui::sectionHeader("Touch controls");
            ImGui::TextWrapped(
                "For phones and tablets: a stick that presses the arrow keys and buttons that press keys, "
                "drawn over the game. Taps anywhere else still work like clicks.");
            {
                TouchSettings& t = settings_.touch;
                int mode = static_cast<int>(t.mode);
                ImGui::SetNextItemWidth(ui::px(260));
                if (ImGui::Combo("Show them", &mode,
                                 "When the screen is touched\0Always (try them with the mouse)\0Never\0")) {
                    t.mode = static_cast<TouchMode>(mode);
                    changed = true;
                }
                changed |= ImGui::Checkbox("Stick (arrow keys)", &t.stick);
                int remove = -1;
                for (size_t i = 0; i < t.buttons.size(); ++i) {
                    ImGui::PushID(static_cast<int>(i) + 7000);
                    ImGui::SetNextItemWidth(ui::px(120));
                    changed |= ImGui::InputTextWithHint("##label", "label", &t.buttons[i].label);
                    ImGui::SameLine();
                    ImGui::TextDisabled("presses");
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(ui::px(120));
                    std::string& key = t.buttons[i].key;
                    if (ImGui::BeginCombo("##key", key.c_str())) {
                        for (const char* k :
                             {"space", "enter", "x", "z", "c", "e", "f", "q", "r", "shift", "up", "escape"})
                            if (ImGui::Selectable(k, key == k)) {
                                key = k;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    if (Input::keyFromName(key) < 0) {
                        ImGui::SameLine();
                        ImGui::TextColored({1, 0.6f, 0.4f, 1}, "not a key");
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
                    ImGui::PopID();
                }
                if (remove >= 0) {
                    t.buttons.erase(t.buttons.begin() + remove);
                    changed = true;
                }
                ImGui::BeginDisabled(t.buttons.size() >= 4);
                if (ImGui::SmallButton("+ Button")) {
                    const char* labels[] = {"A", "B", "C", "D"};
                    const char* keys[] = {"space", "x", "z", "c"};
                    t.buttons.push_back({labels[t.buttons.size()], keys[t.buttons.size()]});
                    changed = true;
                }
                ImGui::EndDisabled();
                if (changed && game_) game_->touchControls().configure(t);
            }

            ImGui::EndTabItem();
        }
        if (tab("Audio", {"mixer", "audio"})) {
            ui::sectionHeader("Audio mixer");
            ImGui::TextWrapped(
                "Sounds play through buses: music through Music, play_sound() and Audio Sources through "
                "Effects unless you pick another. Changes are heard right away while playing.");
            changed |= drawMixer();
            ImGui::EndTabItem();
        }
        if (tab("Physics", {"layers", "physics"})) {
            ui::sectionHeader("Collision layers");
            ImGui::TextWrapped(
                "Put objects on layers (Inspector, under Tag) and choose which layers collide. "
                "Bullets that fly through their shooter, pickups only the player touches, "
                "raycast(a, b, layers=\"Ground\")...");
            changed |= drawLayerSettings();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if (changed) settings_.save(projectDir_);
    ImGui::End();
}

// Renames a collision layer in every scene and prefab of the project ("" removes it: objects go back to
// Default).
void Editor::renameLayer(const std::string& from, const std::string& to) {
    auto fix = [&](Json& data) {
        bool touched = false;
        for (size_t i = 0; i < data["entities"].size(); ++i) {
            Json& ej = data["entities"][i];
            if (ej["layer"].asString("") == from) {
                if (to.empty())
                    ej.erase("layer");
                else
                    ej["layer"] = to;
                touched = true;
            }
        }
        return touched;
    };
    for (auto& path : projectFiles({".scene", ".prefab"})) {
        if (path == scenePath_)
            continue; // the open scene is changed in memory below
        auto text = fs::readText(projectDir_ / path);
        if (!text)
            continue;
        Json data = Json::parse(*text);
        if (fix(data))
            fs::writeText(projectDir_ / path, data.dump(2) + "\n");
    }
    if (editingPrefab())
        fix(prefabReturnScene_); // (the scene waiting behind the prefab, which is saved from memory)
    bool any = false;
    scene().walk([&](Entity e, int) {
        any |= scene().info(e).layer == from;
        return true;
    });
    if (any) {
        recordUndo(to.empty() ? "Remove layer" : "Rename layer");
        scene().walk([&](Entity e, int) {
            if (scene().info(e).layer == from)
                scene().info(e).layer = to;
            return true;
        });
    }
    for (auto& [a, b] : settings_.layerIgnores) {
        if (a == from)
            a = to.empty() ? "" : to;
        if (b == from)
            b = to.empty() ? "" : to;
    }
    std::erase_if(settings_.layerIgnores, [](const auto& p) { return p.first.empty() || p.second.empty(); });
}

// One strip per bus: volume, mute, and the low-pass and echo effects.
bool Editor::drawMixer() {
    bool changed = false;
    auto& buses = settings_.audioBuses;
    int removeAt = -1;
    if (ImGui::BeginTable("##mixer", 6, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, ui::px(110));
        ImGui::TableSetupColumn("Volume");
        ImGui::TableSetupColumn("Mute", ImGuiTableColumnFlags_WidthFixed, ui::px(40));
        ImGui::TableSetupColumn("Low-pass (Hz)");
        ImGui::TableSetupColumn("Echo");
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ui::px(26));
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < buses.size(); ++i) {
            AudioBus& b = buses[i];
            bool fixed = b.name == "Music" || b.name == "Effects";
            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            if (fixed) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(b.name.c_str());
            } else if (ImGui::InputText("##name", &b.name, ImGuiInputTextFlags_EnterReturnsTrue) || ImGui::IsItemDeactivatedAfterEdit()) {
                // Sounds pick a bus by name, so each needs one of its own.
                b.name.erase(0, b.name.find_first_not_of(' '));
                b.name.erase(b.name.find_last_not_of(' ') + 1);
                auto taken = [&](const std::string& name) {
                    for (size_t k = 0; k < buses.size(); ++k)
                        if (k != i && buses[k].name == name)
                            return true;
                    return false;
                };
                std::string base = b.name.empty() ? "Bus" : b.name;
                for (int n = 2; b.name.empty() || taken(b.name); ++n)
                    b.name = base + " " + std::to_string(n);
                changed = true;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##vol", &b.volume, 0, 1, "%.2f");
            ImGui::TableSetColumnIndex(2);
            changed |= ImGui::Checkbox("##mute", &b.muted);
            ImGui::TableSetColumnIndex(3);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##lp", &b.lowpass, 0, 8000, b.lowpass > 0 ? "%.0f Hz" : "off");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Cuts high sounds: muffled, far away, underwater. 0 = off.");
            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::SliderFloat("##echo", &b.echo, 0, 1, b.echo > 0 ? "%.2f" : "off");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("An echo, like a cave or a hall. The delay (%.2f s) applies the next time the game starts.",
                                  b.echoDelay);
            ImGui::TableSetColumnIndex(5);
            if (!fixed && ImGui::SmallButton("x"))
                removeAt = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeAt >= 0) {
        buses.erase(buses.begin() + removeAt);
        changed = true;
    }
    if (ImGui::Button("+ Add bus")) {
        std::string name = "Bus " + std::to_string(buses.size() + 1);
        for (size_t n = buses.size() + 2; std::any_of(buses.begin(), buses.end(), [&](auto& b) { return b.name == name; }); ++n)
            name = "Bus " + std::to_string(n);
        buses.push_back({name});
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset mixer")) {
        buses = ProjectSettings::defaultAudioBuses();
        changed = true;
    }
    if (changed && playing_ && game_)
        game_->audio().applyMixer(buses);
    return changed;
}

bool Editor::drawLayerSettings() {
    bool changed = false;
    auto& layers = settings_.layers;
    ImGui::PushID("layers");
    int removeAt = -1;
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled(" 0");
    ImGui::SameLine(ui::px(40));
    ImGui::TextUnformatted("Default");
    ImGui::SameLine();
    ImGui::TextDisabled("(every object starts here)");
    for (size_t i = 0; i < layers.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%2d", static_cast<int>(i) + 1);
        ImGui::SameLine(ui::px(40));
        static std::string editing;
        static int editingIndex = -1;
        std::string& name = editingIndex == static_cast<int>(i) ? editing : layers[i];
        std::string before = layers[i];
        ImGui::SetNextItemWidth(ui::px(200));
        if (ImGui::InputText("##name", &name))
            if (editingIndex != static_cast<int>(i)) {
                editing = name;
                layers[i] = before;
                editingIndex = static_cast<int>(i);
            }
        if (ImGui::IsItemDeactivatedAfterEdit() && editingIndex == static_cast<int>(i)) {
            std::string to = editing;
            to.erase(0, to.find_first_not_of(' '));
            to.erase(to.find_last_not_of(' ') + 1);
            bool taken = to == "Default" || std::count(layers.begin(), layers.end(), to) > 0;
            if (!to.empty() && !taken && to != before) {
                renameLayer(before, to);
                layers[i] = to;
                changed = true;
            } else if (taken && to != before) {
                notify("There's already a layer called " + to + ".", true);
            }
            editingIndex = -1;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove"))
            removeAt = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeAt >= 0) {
        std::string gone = layers[static_cast<size_t>(removeAt)];
        renameLayer(gone, "");
        layers.erase(layers.begin() + removeAt);
        changed = true;
        notify("Removed the layer " + gone + ". Objects on it are back on Default.");
    }
    ImGui::BeginDisabled(static_cast<int>(layers.size()) >= ProjectSettings::kMaxLayers - 1);
    if (ImGui::Button("+ Add layer")) {
        std::string name = "Layer " + std::to_string(layers.size() + 1);
        for (int n = 2; std::count(layers.begin(), layers.end(), name); ++n)
            name = "Layer " + std::to_string(layers.size() + n);
        layers.push_back(name);
        changed = true;
    }
    ImGui::EndDisabled();

    // Which layers collide: one checkbox per pair, like Unity's collision matrix.
    auto names = settings_.layerNames();
    if (names.size() > 1) {
        ImGui::Spacing();
        ImGui::TextDisabled("Tick the pairs that collide:");
        int n = static_cast<int>(names.size());
        ImGuiTableFlags flags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_HighlightHoveredColumn;
        if (ImGui::BeginTable("##matrix", n + 1, flags)) {
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_NoHide | ImGuiTableColumnFlags_NoReorder);
            for (int c = n - 1; c >= 0; --c)
                ImGui::TableSetupColumn(names[static_cast<size_t>(c)].c_str(), ImGuiTableColumnFlags_AngledHeader);
            ImGui::TableAngledHeadersRow();
            for (int r = 0; r < n; ++r) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(names[static_cast<size_t>(r)].c_str());
                // Columns run from the last layer to the first, so the grid is a triangle.
                for (int c = n - 1; c >= r; --c) {
                    ImGui::TableSetColumnIndex(n - c);
                    ImGui::PushID(r * 64 + c);
                    const std::string &a = names[static_cast<size_t>(r)], &b = names[static_cast<size_t>(c)];
                    bool on = settings_.layersCollide(a, b);
                    if (ImGui::Checkbox("##c", &on)) {
                        settings_.setLayersCollide(a, b, on);
                        changed = true;
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s and %s %s", a.c_str(), b.c_str(), on ? "collide" : "pass through each other");
                    ImGui::PopID();
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopID();
    return changed;
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
    if (ImGui::Button("< Back", {ui::px(90), 0}))
        --learnStep_;
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(learnStep_ + 1 >= count);
    if (ImGui::Button("Next >", {ui::px(90), 0}))
        ++learnStep_;
    ImGui::EndDisabled();
    ImGui::End();
}

void Editor::drawReference() {
    ImGui::SetNextWindowSize(ui::fitted({560, 620}), ImGuiCond_FirstUseEver);
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
    filter = lowered(filter);
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
            hay = lowered(hay);
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
