// Preferences > Shortcuts: a keymap to start from (Rynax, Unity, Godot, Unreal), then any command's
// keys changed by clicking them and pressing new ones. Each command can have two sets of keys.

#include "editor.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <cstring>

namespace rynax::editor {

bool Editor::drawShortcutPrefs() {
    bool changed = false;
    ui::sectionHeader("Keymap");
    const char* preview = prefs.keymap.c_str();
    std::string custom;
    if (keymapCustomized(prefs)) {
        custom = prefs.keymap + " (with your changes)";
        preview = custom.c_str();
    }
    ImGui::SetNextItemWidth(ui::px(260));
    if (ImGui::BeginCombo("##keymap", preview)) {
        for (auto& k : keymaps()) {
            if (ImGui::Selectable(k.name, prefs.keymap == k.name)) {
                if (keymapCustomized(prefs)) {
                    pendingKeymap_ = k.name; // asked about below: it would undo their changes
                } else {
                    applyKeymap(prefs, k.name);
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", keyText(k.blurb).c_str());
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!keymapCustomized(prefs));
    if (ImGui::Button("Reset all"))
        pendingKeymap_ = prefs.keymap;
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Every shortcut back to the %s keymap", prefs.keymap.c_str());
    for (auto& k : keymaps())
        if (prefs.keymap == k.name)
            ImGui::TextDisabled("%s", keyText(k.blurb).c_str());

    if (!pendingKeymap_.empty() && !ImGui::IsPopupOpen("Start over?"))
        ImGui::OpenPopup("Start over?");
    if (ImGui::BeginPopupModal("Start over?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Use the %s keymap? The shortcuts you changed go back to its keys.", pendingKeymap_.c_str());
        if (ImGui::Button("Yes, use it", {ui::px(140), 0})) {
            applyKeymap(prefs, pendingKeymap_);
            pendingKeymap_.clear();
            changed = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Keep mine", {ui::px(140), 0}) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingKeymap_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ui::sectionHeader("Keyboard shortcuts");
    ImGui::TextDisabled("Click keys, then press new ones (Esc cancels). Right-click to remove them.");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##keysearch", "Search: a command or keys, like \"undo\" or \"F5\"", &keySearch_);
    if (!rebindProblem_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.55f, 0.35f, 1});
        ImGui::TextWrapped("%s", rebindProblem_.c_str());
        ImGui::PopStyleColor();
    }

    std::string query = lowered(keySearch_);
    auto matches = [&](const KeyAction& a) {
        if (query.empty())
            return true;
        std::string text = lowered(std::string(a.label) + " " + a.group + " " + chordName(boundChord(&prefs, a.id, 0)) + " " +
                                   chordName(boundChord(&prefs, a.id, 1)));
        return text.find(query) != std::string::npos;
    };

    // One keys button: click to change them, right-click to remove them or put the defaults back.
    auto keysButton = [&](const KeyAction& a, int slot) {
        std::string key = slot == 0 ? std::string(a.id) : std::string(a.id) + "/2";
        ImGuiKeyChord chord = boundChord(&prefs, a.id, slot);
        bool waiting = rebindAction_ == key;
        std::string text = waiting ? "Press keys..." : chord ? chordName(chord) : std::string(slot == 0 ? "(none)" : "+");
        ImGui::PushID(slot);
        if (waiting)
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::Button(text.c_str(), {-1, 0})) {
            rebindAction_ = key;
            rebindProblem_.clear();
        }
        if (waiting)
            ImGui::PopStyleColor();
        bool hovered = ImGui::IsItemHovered();
        if (hovered && !waiting)
            ImGui::SetTooltip(chord ? "Click, then press new keys. Right-click to remove them."
                                    : "Click, then press keys for this too");
        if (ImGui::BeginPopupContextItem("##keymenu")) {
            if (ImGui::MenuItem("Remove these keys", nullptr, false, chord != 0)) {
                prefs.keys[key] = 0;
                changed = true;
            }
            ImGuiKeyChord original = keymapChord(prefs.keymap, a.id, slot);
            if (ImGui::MenuItem(("Back to " + chordName(original)).c_str(), nullptr, false, chord != original)) {
                prefs.keys[key] = original;
                changed = true;
            }
            ImGui::EndPopup();
        }
        if (waiting) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !hovered)) {
                rebindAction_.clear();
            } else if (ImGuiKeyChord c = pressedChord()) {
                rebindAction_.clear();
                rebindProblem_ = chordProblem(a, c);
                if (rebindProblem_.empty()) {
                    prefs.keys[key] = c;
                    changed = true;
                } else {
                    rebindProblem_ = chordName(c) + " for \"" + a.label + "\": " + rebindProblem_;
                }
            }
        }
        ImGui::PopID();
    };

    if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Command", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, ui::px(150));
        ImGui::TableSetupColumn("Or", ImGuiTableColumnFlags_WidthFixed, ui::px(150));
        const char* group = nullptr;
        for (auto& a : keyActions()) {
            if (!matches(a))
                continue;
            if (!group || std::strcmp(group, a.group) != 0) {
                group = a.group;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Spacing();
                ImGui::TextColored(ImGui::GetStyleColorVec4(ImGuiCol_CheckMark), "%s", a.group);
            }
            ImGui::PushID(a.id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(a.label);
            // Keys that another command in the same place also uses: only one of them would happen.
            std::string clash;
            for (int slot = 0; slot < 2; ++slot)
                for (const KeyAction* other : keyClashes(&prefs, a.id, boundChord(&prefs, a.id, slot)))
                    if (clash.find(other->label) == std::string::npos)
                        clash += (clash.empty() ? "" : ", ") + std::string(other->label);
            if (!clash.empty()) {
                ImGui::SameLine();
                ImGui::TextColored({1.0f, 0.55f, 0.35f, 1}, "(same keys as %s)", clash.c_str());
            }
            ImGui::TableSetColumnIndex(1);
            keysButton(a, 0);
            ImGui::TableSetColumnIndex(2);
            keysButton(a, 1);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    // (Ctrl+Tab is Ctrl+Tab on a Mac too: Cmd+Tab switches apps there.)
    ImGui::TextDisabled("%s", (keyText("Text boxes keep your computer's usual keys (Ctrl+C, Ctrl+V, Ctrl+Z...).") +
                               " Ctrl+Tab switches between windows.")
                                  .c_str());
    return changed;
}

} // namespace rynax::editor
