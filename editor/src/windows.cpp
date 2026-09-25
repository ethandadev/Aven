// Tool windows: undo history, profiler, find in project, command palette and lighting presets.

#include "editor.h"

#include "aven/core/fs.h"
#include "aven/runtime/script_system.h"

#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace aven::editor {

namespace {

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Fuzzy match: every letter of the query appears in order. Higher scores for word starts and runs.
int fuzzyScore(const std::string& query, const std::string& text) {
    if (query.empty())
        return 1;
    std::string q = lower(query), t = lower(text);
    size_t pos = t.find(q);
    if (pos != std::string::npos)
        return 1000 - static_cast<int>(pos) * 2 - static_cast<int>(t.size());
    int score = 0, run = 0;
    size_t ti = 0;
    for (char c : q) {
        bool found = false;
        while (ti < t.size()) {
            if (t[ti] == c) {
                bool wordStart = ti == 0 || t[ti - 1] == ' ' || t[ti - 1] == '_' || t[ti - 1] == '/';
                score += 10 + (wordStart ? 15 : 0) + run * 5;
                ++run;
                ++ti;
                found = true;
                break;
            }
            run = 0;
            ++ti;
        }
        if (!found)
            return 0;
    }
    return score;
}

} // namespace

// ---------------------------------------------------------------- undo history

void Editor::drawHistory() {
    ui::placeWindow({320, 420}, {0.72f, 0.35f});
    if (!ImGui::Begin("Undo History", &showHistory_)) {
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Click a step to go back (or forward) to it.");
    ImGui::Separator();
    int jumpUndo = 0, jumpRedo = 0;
    ImGui::Selectable("(scene opened)", false, ImGuiSelectableFlags_Disabled);
    for (size_t i = 0; i < undo_.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Selectable(undo_[i].label.c_str()))
            jumpUndo = static_cast<int>(undo_.size() - i);
        ImGui::PopID();
    }
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_CheckMark));
    ImGui::Selectable("> Now", true);
    ImGui::PopStyleColor();
    for (int i = static_cast<int>(redo_.size()) - 1; i >= 0; --i) {
        ImGui::PushID(1000 + i);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(redo_[static_cast<size_t>(i)].label.c_str()))
            jumpRedo = static_cast<int>(redo_.size()) - i;
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::End();
    // Undo/redo after the list so it doesn't change while it's drawn. Only the last notice matters.
    for (int i = 0; i < jumpUndo - 1; ++i)
        undo();
    if (jumpUndo)
        undo();
    for (int i = 0; i < jumpRedo; ++i)
        redo();
}

// ---------------------------------------------------------------- profiler

void Editor::recordProfile() {
    auto push = [](std::vector<float>& v, float x) {
        if (v.size() >= 300)
            v.erase(v.begin());
        v.push_back(x);
    };
    push(profFrame_, ImGui::GetIO().DeltaTime * 1000.0f);
    push(profRender_, renderMs_);
    if (playing_ && game_) {
        const GameProfile& p = game_->profile();
        push(profScripts_, p.scripts);
        push(profPhysics_, p.physics);
        push(profGameplay_, p.gameplay);
    } else {
        push(profScripts_, 0);
        push(profPhysics_, 0);
        push(profGameplay_, 0);
    }
}

void Editor::drawProfiler() {
    ui::placeWindow({520, 480}, {0.62f, 0.45f});
    if (!ImGui::Begin("Profiler", &showProfiler_)) {
        ImGui::End();
        return;
    }
    auto avg = [](const std::vector<float>& v) {
        if (v.empty())
            return 0.0f;
        float s = 0;
        size_t n = std::min<size_t>(v.size(), 60);
        for (size_t i = v.size() - n; i < v.size(); ++i)
            s += v[i];
        return s / static_cast<float>(n);
    };
    auto peak = [](const std::vector<float>& v) { return v.empty() ? 0.0f : *std::max_element(v.begin(), v.end()); };
    float frame = avg(profFrame_);
    ImGui::Text("%.0f frames per second (%.2f ms per frame)", frame > 0 ? 1000.0f / frame : 0.0f, frame);
    ImGui::TextDisabled("A smooth game needs 16.7 ms or less per frame (60 FPS).");
    float top = std::max(33.4f, peak(profFrame_) * 1.1f);
    if (!profFrame_.empty())
        ImGui::PlotLines("##frame", profFrame_.data(), static_cast<int>(profFrame_.size()), 0, "frame time", 0, top,
                         {-1, 90});
    struct Row {
        const char* name;
        const std::vector<float>* data;
        ImU32 color;
        const char* help;
    };
    Row rows[] = {
        {"Scripts", &profScripts_, IM_COL32(251, 146, 60, 255), "Your blocks and EasyScript code."},
        {"Physics", &profPhysics_, IM_COL32(74, 222, 128, 255), "Collisions, gravity and characters."},
        {"Gameplay", &profGameplay_, IM_COL32(167, 139, 250, 255), "Particles, animations, UI, cameras, behaviors."},
        {"Rendering", &profRender_, IM_COL32(96, 165, 250, 255), "Drawing the scene, shadows and effects."},
    };
    if (ImGui::BeginTable("##prof", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Part");
        ImGui::TableSetupColumn("Average", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("Share", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        float total = 0;
        for (auto& r : rows)
            total += avg(*r.data);
        for (auto& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(r.color), "%s", r.name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", r.help);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.2f ms", avg(*r.data));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.2f ms", peak(*r.data));
            ImGui::TableSetColumnIndex(3);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, r.color);
            ImGui::ProgressBar(total > 0 ? avg(*r.data) / total : 0, {-1, 0}, "");
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
    ImGui::Spacing();
    Scene& s = scene();
    auto& reg = s.registry();
    const rhi::FrameStats& st = device_.stats();
    ImGui::SeparatorText("This frame");
    ImGui::Text("Objects: %d", static_cast<int>(reg.aliveCount()));
    ImGui::Text("Draw calls: %u    Triangles: %llu", st.drawCalls, static_cast<unsigned long long>(st.triangles));
    ImGui::Text("Scripts: %d   2D bodies: %d   3D bodies: %d", static_cast<int>(reg.count<Script>()),
                static_cast<int>(reg.count<RigidBody2D>()), static_cast<int>(reg.count<RigidBody>()));
    size_t particles = 0;
    reg.each<ParticleState>([&](Entity, ParticleState& ps) { particles += ps.particles.size(); });
    ImGui::Text("Particles: %d   Lights: %d", static_cast<int>(particles), static_cast<int>(reg.count<Light>()));
    if (!playing_)
        ImGui::TextDisabled("Press Play to measure scripts and physics.");
    ImGui::End();
}

// ---------------------------------------------------------------- find in project

void Editor::runFind() {
    findResults_.clear();
    if (findQuery_.empty())
        return;
    std::string needle = findCase_ ? findQuery_ : lower(findQuery_);
    for (auto& file : projectFiles({".es", ".blocks", ".scene", ".prefab", ".json", ".aven"})) {
        auto text = fs::readText(projectDir_ / file);
        if (!text)
            continue;
        int lineNo = 0;
        size_t start = 0;
        while (start <= text->size()) {
            size_t end = text->find('\n', start);
            if (end == std::string::npos)
                end = text->size();
            ++lineNo;
            std::string line = text->substr(start, end - start);
            std::string hay = findCase_ ? line : lower(line);
            if (hay.find(needle) != std::string::npos) {
                size_t first = line.find_first_not_of(" \t");
                findResults_.push_back({file, lineNo, first == std::string::npos ? line : line.substr(first)});
                if (findResults_.size() > 500)
                    return;
            }
            start = end + 1;
        }
    }
}

void Editor::drawFind() {
    ui::placeWindow({560, 420}, {0.5f, 0.4f});
    if (!ImGui::Begin("Find in Project", &showFind_)) {
        ImGui::End();
        return;
    }
    if (findFocus_) {
        ImGui::SetKeyboardFocusHere();
        findFocus_ = false;
    }
    ImGui::SetNextItemWidth(-150);
    bool changed = ImGui::InputTextWithHint("##find", "Search every script, scene and prefab...", &findQuery_);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Match case", &findCase_);
    if (changed)
        runFind();
    ImGui::Separator();
    ImGui::BeginChild("##results");
    std::string lastFile;
    for (size_t i = 0; i < findResults_.size(); ++i) {
        auto& r = findResults_[i];
        if (r.file != lastFile) {
            ImGui::PushFont(fonts.bold);
            ImGui::TextUnformatted(r.file.c_str());
            ImGui::PopFont();
            lastFile = r.file;
        }
        ImGui::PushID(static_cast<int>(i));
        std::string label = "   " + std::to_string(r.line) + ":  " + r.text;
        if (ImGui::Selectable(label.c_str())) {
            std::string ext = fs::extension(r.file);
            if (ext == ".es" || ext == ".blocks")
                openScript(r.file, r.line);
            else if (ext == ".scene")
                openScene(r.file);
            else if (ext == ".prefab")
                openPrefab(r.file);
        }
        ImGui::PopID();
    }
    if (findResults_.empty() && !findQuery_.empty())
        ImGui::TextDisabled("Nothing found.");
    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------- command palette

void Editor::drawCommandPalette() {
    if (!showPalette_)
        return;
    if (!ImGui::IsPopupOpen("##palette")) {
        ImGui::OpenPopup("##palette");
        paletteQuery_.clear();
        paletteIndex_ = 0;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + 90}, ImGuiCond_Always, {0.5f, 0});
    ImGui::SetNextWindowSize({620, 0});
    if (!ImGui::BeginPopup("##palette")) {
        showPalette_ = false;
        return;
    }
    if (ImGui::IsWindowAppearing())
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##q", "Type a command, object, file or component...", &paletteQuery_))
        paletteIndex_ = 0;

    struct Item {
        std::string label, kind;
        std::function<void()> run;
        int score;
    };
    std::vector<Item> items;
    auto add = [&](std::string label, std::string kind, std::function<void()> run) {
        int sc = fuzzyScore(paletteQuery_, label);
        if (sc > 0)
            items.push_back({std::move(label), std::move(kind), std::move(run), sc});
    };
    add("Play / Stop", "command", [this] { playing_ ? stop() : play(); });
    add("Save scene", "command", [this] { saveAllScripts(); saveScene(); });
    add("New 2D scene", "command", [this] { newScene(false); });
    add("New 3D scene", "command", [this] { newScene(true); });
    add("Preferences", "command", [this] { showPrefs_ = true; });
    add("Learn mode levels", "command", [this] { showLevels_ = true; });
    add("Undo history", "window", [this] { showHistory_ = true; });
    add("Profiler", "window", [this] { showProfiler_ = true; });
    add("Find in project", "window", [this] { showFind_ = true; findFocus_ = true; });
    add("Check my game (Error Doctor)", "command", [this] { runCheckup(); showDoctor_ = focusDoctor_ = true; });
    add("Explain my game", "window", [this] { showExplain_ = focusExplain_ = true; });
    if (unlocked(Feature::CodeLadder))
        add("Code Ladder (see the selected script in other engines' languages)", "window", [this] { openCodeLadderForSelection(); });
    add("Lighting presets", "window", [this] { showLighting_ = true; });
    add("Scripting reference", "window", [this] { showReference_ = true; });
    add("Project settings", "window", [this] { showSettings_ = true; });
    add("Build & export", "window", [this] { showExport_ = true; });
    for (auto& t : extraCommands_)
        add(t.first, "tool", t.second);
    for (const char* k : {"Square", "Circle", "Sprite", "Text", "Cube", "Sphere", "Player 3D", "Camera", "Particles", "Sound",
                          "UI Text", "UI Button", "Sun", "Point Light"})
        add(std::string("Create ") + k, "create", [this, k] { createEntity(k); });
    int objects = 0;
    scene().walk([&](Entity e, int) {
        if (++objects > 400)
            return false;
        UUID id = scene().info(e).uuid;
        add(scene().info(e).name, "object", [this, id] {
            select(scene().findByUUID(id));
            focusSelected();
        });
        return true;
    });
    for (auto& f : assetFiles_) {
        std::string ext = fs::extension(f);
        if (ext == ".es" || ext == ".blocks")
            add(f, "script", [this, f] { openScript(f); });
        else if (ext == ".scene")
            add(f, "scene", [this, f] { openScene(f); });
        else if (ext == ".prefab")
            add(f, "prefab", [this, f] { openPrefab(f); });
    }
    if (Entity sel = selected(); sel && !playing_)
        for (auto& ci : ComponentRegistry::all())
            if (!ci.get(scene_->registry(), sel) && componentUnlocked(ci) && (!ci.advanced || advanced()))
                add("Add component: " + ci.name, "component", [this, &ci] {
                    if (Entity e = selected()) {
                        recordUndo("Add " + ci.name);
                        ci.add(scene_->registry(), e);
                    }
                });
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.score > b.score; });
    if (items.size() > 12)
        items.resize(12);
    paletteIndex_ = std::clamp(paletteIndex_, 0, std::max(0, static_cast<int>(items.size()) - 1));
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        paletteIndex_ = std::min(paletteIndex_ + 1, static_cast<int>(items.size()) - 1);
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        paletteIndex_ = std::max(paletteIndex_ - 1, 0);
    std::function<void()> chosen;
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        ImGui::PushID(i);
        bool sel = i == paletteIndex_;
        if (ImGui::Selectable(items[static_cast<size_t>(i)].label.c_str(), sel, 0, {0, ImGui::GetFrameHeight()}))
            chosen = items[static_cast<size_t>(i)].run;
        ImGui::SameLine(ImGui::GetWindowWidth() - 90);
        ImGui::TextDisabled("%s", items[static_cast<size_t>(i)].kind.c_str());
        ImGui::PopID();
    }
    if (items.empty())
        ImGui::TextDisabled("No matches.");
    if (ImGui::IsKeyPressed(ImGuiKey_Enter) && !items.empty())
        chosen = items[static_cast<size_t>(paletteIndex_)].run;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) || chosen) {
        ImGui::CloseCurrentPopup();
        showPalette_ = false;
    }
    ImGui::EndPopup();
    if (chosen)
        chosen();
}

// ---------------------------------------------------------------- lighting presets

namespace {

struct LightingPreset {
    const char* name;
    const char* description;
    uint32_t skyTop, skyHorizon, ground, fogColor, sunColor;
    float sunPitch, sunYaw, sunIntensity, ambient, fogDensity, exposure, saturation, bloom;
    bool fog;
    int sky; // 0 solid, 1 gradient, 2 procedural
};

const LightingPreset kPresets[] = {
    {"Sunny Day", "Bright blue sky and crisp shadows.", 0x3A78C9, 0xCFE3F5, 0x6B6150, 0xCFE3F5, 0xFFF5E0, -50, -30, 1.3f, 1.0f, 0.004f, 1.0f, 1.05f, 0.5f, false, 2},
    {"Golden Sunset", "Low orange sun, long shadows and warm haze.", 0x2E2A6B, 0xF59E6B, 0x3A2A2A, 0xB9788A, 0xFFB27A, -8, 60, 1.5f, 0.7f, 0.018f, 1.0f, 1.1f, 0.8f, true, 2},
    {"Moonlit Night", "Dark blue night with soft moonlight. Add point lights!", 0x050816, 0x1B2745, 0x0A0C12, 0x101830, 0x8FA8FF, -35, 140, 0.35f, 0.35f, 0.02f, 1.2f, 0.9f, 1.0f, true, 1},
    {"Overcast", "Grey clouds and soft, even light.", 0x8A949E, 0xC4CAD0, 0x5A5A55, 0xB8BEC4, 0xDDE2E8, -70, 10, 0.6f, 1.3f, 0.012f, 1.1f, 0.85f, 0.3f, true, 1},
    {"Foggy Morning", "Pale light and thick fog. Great for mysteries.", 0x9DB2BF, 0xDDE6EA, 0x6F7A72, 0xD0DADF, 0xFFF2D8, -20, -60, 0.8f, 1.1f, 0.06f, 1.0f, 0.9f, 0.4f, true, 1},
    {"Outer Space", "Black sky and one hard light, like the sun in space.", 0x000000, 0x05030F, 0x000000, 0x000000, 0xFFFFFF, -30, 45, 2.0f, 0.12f, 0.0f, 1.0f, 1.1f, 1.2f, false, 1},
    {"Underwater", "Blue-green murk that fades into the deep.", 0x0B3D5C, 0x1E7A8C, 0x0A2A35, 0x125E6E, 0x9BE3F0, -75, 0, 0.8f, 0.9f, 0.07f, 1.1f, 1.0f, 0.6f, true, 1},
    {"Neon Night", "Purple night for glowing, colorful scenes.", 0x0E0524, 0x3B0F5C, 0x0A0414, 0x2A0B45, 0xC084FC, -40, 200, 0.4f, 0.45f, 0.025f, 1.2f, 1.3f, 1.6f, true, 1},
    {"Candy Land", "Pastel pink sky and bright, happy colors.", 0xF9A8D4, 0xFDE2F3, 0xB9E6C9, 0xFDE2F3, 0xFFF1F8, -55, 20, 1.2f, 1.3f, 0.01f, 1.05f, 1.25f, 0.5f, true, 1},
};

} // namespace

void Editor::applyLighting(int index) {
    const LightingPreset& p = kPresets[index];
    recordUndo(std::string("Lighting: ") + p.name);
    Scene& s = *scene_;
    auto& reg = s.registry();
    Entity env;
    reg.each<Environment>([&](Entity e, Environment&) { env = env ? env : e; });
    if (!env) {
        env = s.create("Environment");
        reg.emplace<Environment>(env);
    }
    auto& en = reg.get<Environment>(env);
    en.sky = static_cast<SkyMode>(p.sky);
    en.skyTop = Color::fromHex(p.skyTop);
    en.skyHorizon = Color::fromHex(p.skyHorizon);
    en.ground = Color::fromHex(p.ground);
    en.ambientIntensity = 0.4f * p.ambient;
    en.fog = p.fog;
    en.fogColor = Color::fromHex(p.fogColor);
    en.fogDensity = p.fogDensity;
    Entity sun;
    reg.each<Light>([&](Entity e, Light& l) {
        if (!sun && l.type == LightType::Directional)
            sun = e;
    });
    if (!sun) {
        sun = s.create("Sun");
        reg.emplace<Light>(sun).type = LightType::Directional;
    }
    auto& l = reg.get<Light>(sun);
    l.color = Color::fromHex(p.sunColor);
    l.intensity = p.sunIntensity;
    l.castShadows = true;
    s.transform(sun).rotation = {p.sunPitch, p.sunYaw, 0};
    if (Entity cam = SceneRenderer::findCamera(s)) {
        auto& pp = reg.getOrEmplace<PostProcessing>(cam);
        pp.exposure = p.exposure;
        pp.saturation = p.saturation;
        pp.bloom = true;
        pp.bloomIntensity = p.bloom;
        reg.get<Camera>(cam).background = Color::fromHex(p.skyHorizon);
    }
    notify(std::string("Lighting set to ") + p.name + ".");
}

void Editor::drawLighting() {
    ui::placeWindow({420, 540}, {0.3f, 0.5f});
    if (!ImGui::Begin("Lighting Presets", &showLighting_)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("One click sets the sky, sun, fog and camera effects. Everything stays editable afterwards "
                       "(select Environment, Sun or Camera).");
    ImGui::Spacing();
    for (int i = 0; i < static_cast<int>(IM_ARRAYSIZE(kPresets)); ++i) {
        const LightingPreset& p = kPresets[i];
        ImGui::PushID(i);
        ImVec2 pos = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("##preset", {w, 52}) && !playing_)
            applyLighting(i);
        bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        auto col = [](uint32_t c) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255); };
        dl->AddRectFilledMultiColor(pos, {pos.x + 90, pos.y + 52}, col(p.skyTop), col(p.skyTop), col(p.skyHorizon), col(p.skyHorizon));
        dl->AddCircleFilled({pos.x + 64, pos.y + 18}, 7, col(p.sunColor));
        dl->AddRect(pos, {pos.x + w, pos.y + 52}, ImGui::GetColorU32(hovered ? ImGuiCol_SliderGrab : ImGuiCol_Border), 4);
        ImGui::GetWindowDrawList()->AddText(fonts.bold, fonts.bold ? fonts.bold->FontSize : ImGui::GetFontSize(),
                                            {pos.x + 102, pos.y + 6}, ImGui::GetColorU32(ImGuiCol_Text), p.name);
        dl->AddText({pos.x + 102, pos.y + 28}, ImGui::GetColorU32(ImGuiCol_TextDisabled), p.description);
        ImGui::PopID();
    }
    if (playing_)
        ImGui::TextDisabled("Stop playing to change the lighting.");
    ImGui::End();
}

} // namespace aven::editor
