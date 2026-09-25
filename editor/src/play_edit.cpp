// Play-and-edit: pause the game, click anything, change it live, and keep the changes you like.
//
// Most engines throw away everything changed while playing. Aven remembers exactly which settings
// you touched (not everything physics moved) and offers to copy them back into the scene.

#include "editor.h"

#include "aven/runtime/script_system.h"
#include "aven/script/vm.h"

#include <imgui.h>

namespace aven::editor {

void Editor::noteLiveChange(Entity e, const std::string& key) {
    if (!replayEditNoted_ && recorder_.recording()) {
        replayEditNoted_ = true;
        recorder_.addEvent("edit", "Changed " + key + " while playing");
    }
    if (!playing_ || !e)
        return;
    liveChanges_[scene().info(e).uuid.value].insert(key);
}

std::vector<Editor::LiveChange> Editor::collectLiveChanges() {
    std::vector<LiveChange> out;
    if (!game_)
        return out;
    Scene& live = game_->scene();
    for (auto& [id, keys] : liveChanges_) {
        Entity e = live.findByUUID({id});
        if (!e)
            continue; // destroyed while playing
        for (const std::string& key : keys) {
            size_t slash = key.find('/');
            LiveChange c{{id}, key.substr(0, slash), key.substr(slash + 1), {}};
            if (c.component == "script") {
                auto inst = game_->scripts().instanceOf(e);
                if (!inst)
                    continue;
                const script::Value* v = inst->find(script::intern(c.field));
                if (!v)
                    continue;
                c.value = script::VM::toJson(*v);
            } else {
                const ComponentInfo* ci = ComponentRegistry::find(c.component);
                if (!ci)
                    continue;
                void* data = ci->get(live.registry(), e);
                if (!data)
                    continue;
                bool found = false;
                for (auto& f : ci->fields)
                    if (f.name == c.field) {
                        c.value = saveField(f, data);
                        found = true;
                    }
                if (!found)
                    continue;
            }
            out.push_back(std::move(c));
        }
    }
    return out;
}

void Editor::applyLiveChanges(const std::vector<LiveChange>& changes) {
    if (changes.empty())
        return;
    // Recorded directly against the edit scene (recordUndo is ignored while playing).
    undo_.push_back({"Keep changes from play", scene_->save(), selection_});
    redo_.clear();
    snapshotValid_ = false;
    int applied = 0;
    auto& reg = scene_->registry();
    for (auto& c : changes) {
        Entity e = scene_->findByUUID(c.id);
        if (!e)
            continue; // made while playing; only existing objects can keep changes
        if (c.component == "script") {
            if (auto* sc = reg.tryGet<Script>(e)) {
                sc->overrides[c.field] = c.value;
                ++applied;
            }
            continue;
        }
        const ComponentInfo* ci = ComponentRegistry::find(c.component);
        if (!ci)
            continue;
        void* data = ci->get(reg, e);
        if (!data) {
            ci->add(reg, e);
            data = ci->get(reg, e);
        }
        for (auto& f : ci->fields)
            if (f.name == c.field) {
                loadField(f, data, c.value);
                ++applied;
            }
    }
    dirty_ = true;
    refreshTitle();
    notify("Kept " + std::to_string(applied) + (applied == 1 ? " change" : " changes") + " from playing.");
}

void Editor::drawLiveChangesBar(ImVec2 pos, ImVec2 size) {
    if (liveChanges_.empty())
        return;
    size_t count = 0;
    for (auto& [id, keys] : liveChanges_)
        count += keys.size();
    ImGui::SetCursorScreenPos({pos.x + 10, pos.y + 10});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(250, 200, 60, 235));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(30, 25, 10, 255));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6);
    float h = ImGui::GetFrameHeight() + 12;
    ImGui::BeginChild("##livebar", {std::min(size.x - 20, 560.0f), h}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("You changed %d %s while playing.", static_cast<int>(count), count == 1 ? "setting" : "settings");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(40, 35, 20, 255));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 245, 220, 255));
    if (ImGui::SmallButton("Keep them")) {
        applyLiveChanges(collectLiveChanges());
        liveChanges_.clear();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Forget them"))
        liveChanges_.clear();
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("They disappear when you stop playing, like in other engines.");
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

void Editor::drawKeepChangesDialog() {
    if (pendingKeep_.empty())
        return;
    if (!ImGui::IsPopupOpen("Keep your changes?"))
        ImGui::OpenPopup("Keep your changes?");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Keep your changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("You changed these while the game was running:");
        ImGui::Spacing();
        int shown = 0;
        for (auto& c : pendingKeep_) {
            if (++shown > 10) {
                ImGui::TextDisabled("...and %d more", static_cast<int>(pendingKeep_.size()) - 10);
                break;
            }
            Entity e = scene_->findByUUID(c.id);
            std::string name = e ? scene_->info(e).name : "(an object made while playing)";
            std::string what = c.component == "script" ? toLabel(c.field) : c.component + " > " + toLabel(c.field);
            ImGui::BulletText("%s: %s = %s", name.c_str(), what.c_str(), c.value.dump().c_str());
        }
        ImGui::Spacing();
        if (ImGui::Button("Keep them in my scene", {200, 0})) {
            applyLiveChanges(pendingKeep_);
            pendingKeep_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Throw them away", {160, 0})) {
            pendingKeep_.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace aven::editor
