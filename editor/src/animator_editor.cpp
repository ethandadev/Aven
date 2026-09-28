// The Animator's Inspector: a picture of the state machine (the current state lights up while
// playing), then its parameters, states and transitions as editable lists, and presets that set
// up common characters in one click.

#include "editor.h"

#include "aven/render/model.h"
#include "aven/render/renderer3d.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

namespace {

const char* const kBuiltinParams[] = {"speed", "vertical_speed", "on_ground", "time_in_state"};

bool combo(const char* id, std::string& value, const std::vector<std::string>& options, const char* empty = "(choose)") {
    bool changed = false;
    if (ImGui::BeginCombo(id, value.empty() ? empty : (value == "*" ? "Any state" : value.c_str()))) {
        for (auto& o : options)
            if (ImGui::Selectable(o == "*" ? "Any state" : o.c_str(), o == value)) {
                value = o;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// Presets: the usual states and transitions for common characters.
void platformerPreset(Animator& a, int frames) {
    int last = std::max(0, frames - 1);
    a.states = {{"Idle", 0, 0, 6, true, "", 1},
                {"Run", std::min(1, last), last, 10, true, "", 1},
                {"Jump", std::min(1, last), std::min(1, last), 8, false, "", 1},
                {"Fall", last, last, 8, false, "", 1}};
    a.params.clear();
    a.transitions = {{"*", "Jump", "vertical_speed", AnimCondition::Greater, 0.5f},
                     {"*", "Fall", "vertical_speed", AnimCondition::Less, -0.5f},
                     {"Idle", "Run", "speed", AnimCondition::Greater, 0.2f},
                     {"Run", "Idle", "speed", AnimCondition::Less, 0.2f},
                     {"Fall", "Idle", "on_ground", AnimCondition::IsTrue, 0},
                     {"Jump", "Idle", "on_ground", AnimCondition::IsTrue, 0}};
    a.startState = "Idle";
}

void topDownPreset(Animator& a, int frames) {
    int last = std::max(0, frames - 1);
    a.states = {{"Idle", 0, 0, 6, true, "", 1}, {"Walk", 0, last, 8, true, "", 1}};
    a.params.clear();
    a.transitions = {{"Idle", "Walk", "speed", AnimCondition::Greater, 0.2f}, {"Walk", "Idle", "speed", AnimCondition::Less, 0.2f}};
    a.startState = "Idle";
}

void characterPreset3D(Animator& a, const std::vector<std::string>& clips) {
    auto pick = [&](std::initializer_list<const char*> words) {
        for (auto& c : clips) {
            std::string lower = c;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            for (const char* w : words)
                if (lower.find(w) != std::string::npos)
                    return c;
        }
        return clips.empty() ? std::string() : clips.front();
    };
    a.states = {{"Idle", 0, 0, 8, true, pick({"idle", "stand"}), 1},
                {"Walk", 0, 0, 8, true, pick({"walk", "run"}), 1},
                {"Jump", 0, 0, 8, false, pick({"jump"}), 1}};
    a.params.clear();
    a.transitions = {{"*", "Jump", "on_ground", AnimCondition::IsFalse, 0},
                     {"Idle", "Walk", "speed", AnimCondition::Greater, 0.3f},
                     {"Walk", "Idle", "speed", AnimCondition::Less, 0.3f},
                     {"Jump", "Idle", "on_ground", AnimCondition::IsTrue, 0}};
    a.startState = "Idle";
}

} // namespace

void Editor::drawAnimator(Entity e, const std::vector<Entity>& selection) {
    auto& reg = scene().registry();
    auto& a = reg.get<Animator>(e);
    bool changed = false;
    bool locked = playing_;
    bool is3D = reg.has<MeshRenderer>(e) && reg.get<MeshRenderer>(e).mesh == MeshShape::Model;
    int frames = 1;
    if (auto* sr = reg.tryGet<SpriteRenderer>(e))
        frames = std::clamp(sr->columns, 1, 4096) * std::clamp(sr->rows, 1, 4096);
    std::vector<std::string> clips;
    if (is3D)
        if (Model* m = renderer_.renderer3D().model(reg.get<MeshRenderer>(e).model))
            for (auto& c : m->animations)
                clips.push_back(c.name);
    std::vector<std::string> stateNames, fromNames{"*"};
    for (auto& st : a.states) {
        stateNames.push_back(st.name);
        fromNames.push_back(st.name);
    }
    std::vector<std::string> paramNames;
    for (auto& p : a.params)
        paramNames.push_back(p.name);
    for (const char* b : kBuiltinParams)
        paramNames.push_back(b);

    // --- the picture: states in a row, arrows for transitions
    std::string current = a.current;
    if (playing_ && game_) {
        Entity live = game_->scene().findByUUID(scene().info(e).uuid);
        if (live && game_->scene().registry().has<Animator>(live))
            current = game_->scene().registry().get<Animator>(live).current;
    }
    ImGui::BeginDisabled(locked);
    if (!a.states.empty()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Starts in");
        ImGui::SameLine(ui::px(110));
        ImGui::SetNextItemWidth(-1);
        changed |= combo("##start", a.startState, stateNames, a.states.front().name.c_str());
    }
    ImGui::EndDisabled();
    if (!a.states.empty()) {
        float width = ImGui::GetContentRegionAvail().x, height = 128;
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, {origin.x + width, origin.y + height}, ImGui::GetColorU32(ImGuiCol_FrameBg), 6);
        size_t n = a.states.size();
        float slot = (width - 16) / static_cast<float>(n);
        float boxW = std::min(110.0f, slot - 8), boxH = 26;
        auto center = [&](size_t i) {
            return ImVec2{origin.x + 8 + (static_cast<float>(i) + 0.5f) * slot, origin.y + 62};
        };
        ImU32 line = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        bool anyFromAny = false;
        for (auto& t : a.transitions) {
            auto to = std::find(stateNames.begin(), stateNames.end(), t.to);
            if (to == stateNames.end())
                continue;
            ImVec2 b = center(static_cast<size_t>(to - stateNames.begin()));
            float top = b.y - boxH * 0.5f;
            if (t.from == "*") {
                // From "any state": an arrow straight down from the top edge.
                anyFromAny = true;
                dl->AddLine({b.x, origin.y + 6}, {b.x, top - 5}, line, 1.2f);
                dl->AddTriangleFilled({b.x - 4, top - 6}, {b.x + 4, top - 6}, {b.x, top - 1}, line);
                continue;
            }
            auto from = std::find(stateNames.begin(), stateNames.end(), t.from);
            if (from == stateNames.end())
                continue;
            ImVec2 p = center(static_cast<size_t>(from - stateNames.begin()));
            // Left-to-right curves over the boxes, right-to-left under them, so pairs don't overlap;
            // longer jumps bend further so they clear the boxes in between.
            float dir = b.x > p.x ? -1.0f : 1.0f;
            float span = std::abs(b.x - p.x) / slot;
            float bend = boxH * 0.5f + 10 + 8 * span;
            float sx = p.x + (b.x > p.x ? 1 : -1) * boxW * 0.2f, ex = b.x - (b.x > p.x ? 1 : -1) * boxW * 0.2f;
            ImVec2 start{sx, p.y + dir * boxH * 0.5f}, end{ex, b.y + dir * (boxH * 0.5f + 1)};
            ImVec2 c1{sx, p.y + dir * bend * 1.3f}, c2{ex, b.y + dir * bend * 1.3f};
            dl->AddBezierCubic(start, c1, c2, end, line, 1.2f);
            dl->AddTriangleFilled({end.x - 4, end.y + dir * 6}, {end.x + 4, end.y + dir * 6}, end, line);
        }
        ImU32 accent = ImGui::ColorConvertFloat4ToU32({prefs.accentColor().r, prefs.accentColor().g, prefs.accentColor().b, 1});
        ImU32 fill = ImGui::GetColorU32(ImGuiCol_FrameBgActive), border = ImGui::GetColorU32(ImGuiCol_Border);
        for (size_t i = 0; i < n; ++i) {
            ImVec2 c = center(i);
            bool on = a.states[i].name == current;
            bool start = a.states[i].name == (a.startState.empty() ? a.states.front().name : a.startState);
            ImVec2 lo{c.x - boxW * 0.5f, c.y - boxH * 0.5f}, hi{c.x + boxW * 0.5f, c.y + boxH * 0.5f};
            dl->AddRectFilled(lo, hi, on ? accent : fill, 5);
            dl->AddRect(lo, hi, start ? ImGui::GetColorU32(ImGuiCol_Text) : border, 5, 0, start ? 2.0f : 1.0f);
            std::string label = a.states[i].name;
            while (label.size() > 2 && ImGui::CalcTextSize(label.c_str()).x > boxW - 8)
                label.pop_back();
            ImVec2 ts = ImGui::CalcTextSize(label.c_str());
            dl->AddText({c.x - ts.x * 0.5f, c.y - ts.y * 0.5f}, ImGui::GetColorU32(ImGuiCol_Text), label.c_str());
        }
        std::string caption = playing_ ? "Lit: playing now" : "Outlined: start";
        if (anyFromAny)
            caption += "  |  From the top: any state";
        ImVec2 cs = ImGui::CalcTextSize(caption.c_str());
        dl->AddText({origin.x + 8, origin.y + height - cs.y - 5}, line, caption.c_str());
        ImGui::Dummy({width, height});
        ImGui::Spacing();
    }

    ImGui::BeginDisabled(locked);
    // --- presets
    if (ImGui::Button("Presets...", {-1, 0}))
        ImGui::OpenPopup("anim_presets");
    if (ImGui::BeginPopup("anim_presets")) {
        if (!is3D && ImGui::MenuItem("Platformer character (idle, run, jump, fall)")) {
            platformerPreset(a, frames);
            changed = true;
        }
        if (!is3D && ImGui::MenuItem("Top-down character (idle, walk)")) {
            topDownPreset(a, frames);
            changed = true;
        }
        if (is3D && ImGui::MenuItem("3D character (idle, walk, jump)")) {
            characterPreset3D(a, clips);
            changed = true;
        }
        ImGui::TextDisabled(is3D ? "Uses the model's clips by their names." : "Uses the sprite sheet's %d frames.", frames);
        ImGui::EndPopup();
    }

    auto header = [](const char* text, const char* tip) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", text);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
    };

    // --- parameters
    header("Parameters", "Values scripts set (set_param, trigger). speed, vertical_speed, on_ground and time_in_state "
                         "are always there, read from the object itself.");
    int removeParam = -1;
    for (size_t i = 0; i < a.params.size(); ++i) {
        AnimParam& p = a.params[i];
        ImGui::PushID(static_cast<int>(i) + 1000);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.42f);
        changed |= ImGui::InputText("##pname", &p.name);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(ImGui::CalcTextSize("Trigger").x + ImGui::GetFrameHeight() + 12,
                                         ImGui::GetContentRegionAvail().x * 0.45f));
        int kind = p.trigger ? 1 : 0;
        if (ImGui::Combo("##ptype", &kind, "Value\0Trigger\0")) {
            p.trigger = kind == 1;
            changed = true;
        }
        ImGui::SameLine();
        if (!p.trigger) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - 6);
            changed |= ImGui::DragFloat("##pval", &p.value, 0.05f, 0, 0, "%g");
            ImGui::SameLine();
        }
        if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0}))
            removeParam = static_cast<int>(i);
        ImGui::PopID();
    }
    if (removeParam >= 0) {
        a.params.erase(a.params.begin() + removeParam);
        changed = true;
    }
    if (ImGui::SmallButton("+ Parameter")) {
        a.params.push_back({"param" + std::to_string(a.params.size() + 1), false, 0});
        changed = true;
    }

    // --- states
    header("States", is3D ? "Each state plays one of the model's animations." : "Each state plays a range of the sprite sheet's frames.");
    int removeState = -1;
    if (!a.states.empty() && ImGui::BeginTable("##states", 5, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn(is3D ? "Clip" : "Frames", ImGuiTableColumnFlags_WidthStretch, is3D ? 1.4f : 1.1f);
        ImGui::TableSetupColumn(is3D ? "Speed" : "FPS", ImGuiTableColumnFlags_WidthStretch, 0.7f);
        ImGui::TableSetupColumn("Loop", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < a.states.size(); ++i) {
            AnimState& st = a.states[i];
            ImGui::PushID(static_cast<int>(i) + 2000);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            std::string before = st.name;
            if (ImGui::InputText("##sname", &st.name)) {
                // Transitions follow the rename (unless another state still has the old name).
                bool shared = std::any_of(a.states.begin(), a.states.end(), [&](const AnimState& o) { return o.name == before; });
                if (!shared) {
                    for (auto& t : a.transitions) {
                        if (t.from == before)
                            t.from = st.name;
                        if (t.to == before)
                            t.to = st.name;
                    }
                    if (a.startState == before)
                        a.startState = st.name;
                }
                changed = true;
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if (is3D) {
                changed |= combo("##clip", st.clip, clips, clips.empty() ? "(no clips)" : "(first clip)");
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::DragFloat("##speed", &st.speed, 0.02f, 0, 5, "%.2fx");
            } else {
                int range[2] = {st.firstFrame, st.lastFrame};
                if (ImGui::DragInt2("##frames", range, 0.1f, 0, frames - 1)) {
                    st.firstFrame = std::clamp(range[0], 0, frames - 1);
                    st.lastFrame = std::clamp(range[1], st.firstFrame, frames - 1);
                    changed = true;
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("First and last frame of the sprite sheet (it has %d)", frames);
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(-1);
                changed |= ImGui::DragFloat("##fps", &st.fps, 0.2f, 1, 60, "%.0f");
            }
            ImGui::TableNextColumn();
            changed |= ImGui::Checkbox("##loop", &st.loop);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Loop (off: play once, then \"animation finished\" transitions can fire)");
            ImGui::TableNextColumn();
            if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0}))
                removeState = static_cast<int>(i);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (removeState >= 0) {
        std::string gone = a.states[static_cast<size_t>(removeState)].name;
        a.states.erase(a.states.begin() + removeState);
        std::erase_if(a.transitions, [&](const AnimTransition& t) { return t.from == gone || t.to == gone; });
        changed = true;
    }
    if (ImGui::SmallButton("+ State")) {
        a.states.push_back({"State " + std::to_string(a.states.size() + 1), 0, frames - 1, 8, true, "", 1});
        changed = true;
    }

    // --- transitions
    header("Transitions", "Checked from the top every frame; the first one whose condition holds switches the state.");
    int removeT = -1, moveUp = -1;
    for (size_t i = 0; i < a.transitions.size(); ++i) {
        AnimTransition& t = a.transitions[i];
        ImGui::PushID(static_cast<int>(i) + 3000);
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth(w * 0.3f);
        changed |= combo("##from", t.from, fromNames);
        ImGui::SameLine();
        ImGui::TextDisabled("->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(w * 0.3f);
        changed |= combo("##to", t.to, stateNames);
        ImGui::SameLine();
        if (ImGui::ArrowButton("##up", ImGuiDir_Up) && i > 0)
            moveUp = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::Button("x", {ImGui::GetFrameHeight(), 0}))
            removeT = static_cast<int>(i);
        // Second line: the condition.
        ImGui::Indent(18);
        bool needsParam = t.when != AnimCondition::Finished && t.when != AnimCondition::Always;
        if (needsParam) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.38f);
            changed |= combo("##param", t.param, paramNames);
            ImGui::SameLine();
        }
        bool hasValue = t.when == AnimCondition::Greater || t.when == AnimCondition::Less || t.when == AnimCondition::Finished;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * (hasValue ? 0.55f : 1.0f));
        int when = std::clamp(static_cast<int>(t.when), 0, static_cast<int>(AnimCondition::Count) - 1); // (from a file: anything)
        auto& labels = animConditionLabels();
        if (ImGui::BeginCombo("##when", labels[static_cast<size_t>(when)].c_str())) {
            for (int k = 0; k < static_cast<int>(AnimCondition::Count); ++k)
                if (ImGui::Selectable(labels[static_cast<size_t>(k)].c_str(), k == when)) {
                    t.when = static_cast<AnimCondition>(k);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if (t.when == AnimCondition::Greater || t.when == AnimCondition::Less) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat("##value", &t.value, 0.05f, 0, 0, "%g");
        } else if (t.when == AnimCondition::Finished) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::DragFloat("##after", &t.value, 0.05f, 0, 60, t.value > 0 ? "or after %.2f s" : "when it ends");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Fires when a state that doesn't loop reaches its last frame, or after this many seconds "
                                  "(use seconds for 3D clips and looping states).");
        }
        ImGui::Unindent(18);
        ImGui::PopID();
    }
    if (removeT >= 0) {
        a.transitions.erase(a.transitions.begin() + removeT);
        changed = true;
    }
    if (moveUp > 0) {
        std::swap(a.transitions[static_cast<size_t>(moveUp)], a.transitions[static_cast<size_t>(moveUp) - 1]);
        changed = true;
    }
    if (ImGui::SmallButton("+ Transition")) {
        a.transitions.push_back({"*", a.states.empty() ? "" : a.states.front().name, "speed", AnimCondition::Greater, 0.1f});
        changed = true;
    }
    ImGui::EndDisabled();

    if (changed) {
        edited("Change animator");
        for (Entity other : selection)
            if (other != e)
                if (auto* o = reg.tryGet<Animator>(other)) {
                    o->states = a.states;
                    o->transitions = a.transitions;
                    o->params = a.params;
                    o->startState = a.startState;
                }
    }
}

} // namespace aven::editor
