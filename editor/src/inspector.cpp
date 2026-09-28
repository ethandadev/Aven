// The Inspector: the selected objects' settings, components, script variables and click actions.

#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/scene/reflection.h"
#include "aven/script/vm.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace aven::editor {

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

} // namespace

namespace {

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
        case FieldType::Color: {
            // Narrow: just the swatch (click it for the picker) rather than four squeezed numbers.
            ImGuiColorEditFlags flags = ImGuiColorEditFlags_AlphaBar;
            if (ImGui::GetContentRegionAvail().x < ui::px(190))
                flags |= ImGuiColorEditFlags_NoInputs;
            c |= ImGui::ColorEdit4("##v", &f.ref<Color>(data).r, flags);
            break;
        }
        case FieldType::String:
            if (info.name == "AudioSource" && f.name == "bus") {
                std::string& bus = f.ref<std::string>(data);
                if (ImGui::BeginCombo("##v", bus.c_str())) {
                    for (auto& b : settings_.audioBuses)
                        if (ImGui::Selectable(b.name.c_str(), b.name == bus)) {
                            bus = b.name;
                            c = true;
                        }
                    ImGui::Separator();
                    if (ImGui::Selectable("Edit the mixer...")) {
                        showSettings_ = true;
                        settingsSection_ = "mixer";
                    }
                    ImGui::EndCombo();
                }
                break;
            }
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
            // With several objects selected, the change applies to all of them. For vectors and
            // colors, only the parts that changed: dragging X moves them all along X, keeping
            // each one's own Y and Z.
            Json value = saveField(f, data);
            for (Entity other : inspected_) {
                if (other == e)
                    continue;
                if (void* od = info.get(scene().registry(), other)) {
                    Json theirs = saveField(f, od);
                    if (value.isArray() && current.isArray() && theirs.isArray() && value.size() == current.size() &&
                        theirs.size() == value.size()) {
                        for (size_t k = 0; k < value.size(); ++k)
                            if (!jsonNear(value[k], current[k]))
                                theirs[k] = value[k];
                        loadField(f, od, theirs);
                    } else {
                        loadField(f, od, value);
                    }
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
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ui::px(110));
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
    // Compiled once per change of the file, not every frame.
    ScriptVarsCache& cache = scriptVarsCache_[sc->path];
    if (!cache.vm || cache.source != *text) {
        cache.source = *text;
        std::string source = fs::extension(sc->path) == ".blocks" ? blocks::compileFile(*text) : *text;
        cache.vm = std::make_shared<script::VM>();
        cache.vm->onError = [](const script::ScriptError&) {};
        cache.module = cache.vm->compile(source, sc->path);
    }
    auto mod = cache.module;
    // With several objects selected, a change goes to each one that has this script.
    std::vector<Entity> sharing;
    for (Entity other : inspected_.empty() ? std::vector<Entity>{e} : inspected_)
        if (auto* o = scene().registry().tryGet<Script>(other); o && o->path == sc->path)
            sharing.push_back(other);
    if (sharing.empty())
        sharing.push_back(e);
    if (!mod) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(1);
        ImGui::TextColored({1, 0.45f, 0.45f, 1}, "This script has an error.");
        return;
    }
    // While playing, show the live values of this object's variables.
    std::shared_ptr<script::Instance> live = playing_ ? game_->scripts().instanceOf(e) : nullptr;
    for (auto& ex : mod->exports) {
        if (ex.name == "is_clone" || ex.hidden) // set by clone(), or hidden with # @hide
            continue;
        ImGui::PushID(ex.name.c_str());
        if (!ex.header.empty()) {
            // "# @header Movement" above the variable.
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Dummy({0, ui::px(2)});
            ImGui::PushFont(fonts.bold);
            ImGui::TextUnformatted(ex.header.c_str());
            ImGui::PopFont();
        }
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
                for (Entity other : sharing)
                    scene().registry().get<Script>(other).overrides.erase(ex.name);
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
        if (value.isNumber() && ex.rangeMax > ex.rangeMin) {
            // # @range(min, max): a slider. Whole-number ranges step in whole numbers.
            bool whole = ex.rangeMin == std::floor(ex.rangeMin) && ex.rangeMax == std::floor(ex.rangeMax) &&
                         value.number() == std::floor(value.number());
            if (whole) {
                // (clamped first: a number outside int's range can't be converted)
                double low = std::clamp(ex.rangeMin, -1e9, 1e9), high = std::clamp(ex.rangeMax, -1e9, 1e9);
                int i = static_cast<int>(std::clamp(value.number(), low, high));
                if ((c = ImGui::SliderInt("##v", &i, static_cast<int>(low), static_cast<int>(high))))
                    result = script::Value(static_cast<double>(i));
            } else {
                float f = static_cast<float>(value.number());
                if ((c = ImGui::SliderFloat("##v", &f, static_cast<float>(ex.rangeMin), static_cast<float>(ex.rangeMax), "%.2f")))
                    result = script::Value(static_cast<double>(f));
            }
        } else if (value.isNumber()) {
            float f = static_cast<float>(value.number());
            if ((c = ImGui::DragFloat("##v", &f, 0.1f)))
                result = script::Value(static_cast<double>(f));
        } else if (value.isString() && !ex.fileKind.empty()) {
            // # @sound, @image, @prefab or @scene: pick a file.
            AssetKind kind = ex.fileKind == "sound"   ? AssetKind::Audio
                           : ex.fileKind == "image"   ? AssetKind::Image
                           : ex.fileKind == "prefab"  ? AssetKind::Prefab
                                                      : AssetKind::Scene;
            std::string file = value.string();
            if ((c = ui::assetField("##v", file, projectFiles(extensionsFor(kind)), "ASSET_PATH")))
                result = script::Value(file);
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
            if ((c = ImGui::ColorEdit4("##v", col, ImGui::GetContentRegionAvail().x < ui::px(190) ? ImGuiColorEditFlags_NoInputs : 0)))
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
            if (!live)
                edited("Change " + ex.name);
            for (Entity other : sharing) {
                if (live) {
                    if (auto inst = game_->scripts().instanceOf(other)) {
                        inst->set(script::intern(ex.name), result);
                        noteLiveChange(other, "script/" + ex.name);
                    }
                } else {
                    scene().registry().get<Script>(other).overrides[ex.name] = script::VM::toJson(result);
                }
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
    if (info.name == "SpriteAnimator" || info.name == "Animator")
        return unlocked(Feature::Animation);
    if (info.name == "Tilemap")
        return unlocked(Feature::Tilemap);
    if (info.name == "NativeScript")
        return unlocked(Feature::NativeCode);
    return true;
}

void Editor::drawAddComponent(Entity e) {
    static std::string filter;
    // With several objects selected, a component goes on every one that doesn't have it yet.
    std::vector<Entity> targets = inspected_.empty() ? std::vector<Entity>{e} : inspected_;
    ImGui::Spacing();
    float w = ImGui::GetContentRegionAvail().x;
    std::string label = targets.size() > 1 ? "+ Add Component to " + std::to_string(targets.size()) + " objects" : "+ Add Component";
    if (ImGui::Button(label.c_str(), {w, ui::px(30)})) {
        filter.clear();
        ImGui::OpenPopup("add_component");
    }
    ImGui::SetNextWindowSize({w, ui::px(420)});
    if (ImGui::BeginPopup("add_component")) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::IsWindowAppearing())
            ImGui::SetKeyboardFocusHere();
        ImGui::InputTextWithHint("##search", "Search components...", &filter);
        std::string lastCategory;
        auto& reg = scene().registry();
        auto missing = [&](const ComponentInfo& info) {
            std::vector<Entity> out;
            for (Entity t : targets)
                if (reg.valid(t) && !info.get(reg, t))
                    out.push_back(t);
            return out;
        };
        // A component copied from another object (right-click a component > Copy values).
        if (const ComponentInfo* copied = componentClipboardType_.empty() ? nullptr : ComponentRegistry::find(componentClipboardType_);
            copied && !missing(*copied).empty() && filter.empty()) {
            if (ImGui::Selectable(("   Paste " + displayName(copied->name) + " (copied)").c_str())) {
                recordUndo("Paste " + copied->name);
                for (Entity t : missing(*copied)) {
                    loadComponent(*copied, copied->add(reg, t), componentClipboard_);
                    ensureRequirements(t, copied->name);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
        }
        for (auto& info : ComponentRegistry::all()) {
            if ((info.advanced && !advanced()) || !componentUnlocked(info))
                continue;
            std::vector<Entity> into = missing(info);
            if (into.empty())
                continue;
            std::string hay = lowered(info.name + " " + displayName(info.name) + " " + info.description + " " + info.category);
            std::string needle = lowered(filter);
            if (!needle.empty() && hay.find(needle) == std::string::npos)
                continue;
            if (info.category != lastCategory) {
                ImGui::TextDisabled("%s", info.category.c_str());
                lastCategory = info.category;
            }
            std::string item = "   " + displayName(info.name);
            if (targets.size() > 1 && into.size() < targets.size())
                item += "  (" + std::to_string(into.size()) + " of " + std::to_string(targets.size()) + " need it)";
            if (ImGui::Selectable(item.c_str())) {
                recordUndo("Add " + info.name);
                std::string newScript; // several objects share one new script
                for (Entity t : into) {
                    info.add(reg, t);
                    ensureRequirements(t, info.name);
                    if (info.name == "Script") {
                        if (newScript.empty())
                            newScript = newScriptFile(scene().info(t).name, !advanced());
                        reg.get<Script>(t).path = newScript;
                    }
                    if (info.name == "BoxCollider2D")
                        if (auto* sr = reg.tryGet<SpriteRenderer>(t))
                            reg.get<BoxCollider2D>(t).size = sr->size;
                    if (info.name == "CircleCollider2D")
                        if (auto* sr = reg.tryGet<SpriteRenderer>(t))
                            reg.get<CircleCollider2D>(t).radius = std::max(sr->size.x, sr->size.y) * 0.5f;
                }
                // A new script is ready to edit straight away.
                if (!newScript.empty())
                    fs::extension(newScript) == ".blocks" ? openBlocks(newScript) : openScript(newScript);
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
        if (prefs.beginnerHelpers)
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
        ImGui::TextDisabled("- edits change all of them");
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }
    if (ImGui::Checkbox("##active", &info.active)) {
        edited("Toggle active");
        for (Entity other : selection)
            s.info(other).active = info.active;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Active: turn off to hide this object and stop its scripts.");
    ImGui::SameLine();
    float small = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetNextItemWidth(-small);
    ImGui::PushFont(fonts.bold);
    if (ImGui::InputText("##name", &info.name)) {
        edited("Rename");
        for (Entity other : selection)
            s.info(other).name = info.name;
    }
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
    if (ImGui::InputTextWithHint("##tag", "e.g. enemy, coin, player", &info.tag)) {
        edited("Change tag");
        for (Entity other : selection)
            s.info(other).tag = info.tag;
    }
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
            edited("Change tag");
            for (Entity other : selection)
                s.info(other).tag.clear();
        }
        for (auto& t : tags)
            if (ImGui::Selectable(t.c_str(), t == info.tag)) {
                edited("Change tag");
                for (Entity other : selection)
                    s.info(other).tag = t;
            }
        ImGui::EndPopup();
    }
    // Collision layer: shown once the project has layers (or in advanced mode).
    if (!settings_.layers.empty() || advanced()) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Layer");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1);
        std::string current = info.layer.empty() ? "Default" : info.layer;
        bool unknown = current != "Default" && settings_.layerIndex(current) == 0;
        if (ImGui::BeginCombo("##layer", unknown ? (current + " (missing)").c_str() : current.c_str())) {
            for (auto& name : settings_.layerNames())
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    for (Entity other : selection)
                        s.info(other).layer = name == "Default" ? "" : name;
                    edited("Change layer");
                }
            ImGui::Separator();
            if (ImGui::Selectable("Edit layers...")) {
                showSettings_ = true;
                settingsSection_ = "layers";
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(unknown ? "This layer isn't in Project Settings > Collision layers, so the object acts as Default."
                                      : "Collision layer: which other objects this one collides with (Project Settings).");
    }
    if (!playing_ && prefs.beginnerHelpers)
        drawAssistant();
    if (auto* pi = s.registry().tryGet<PrefabInstance>(e)) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Prefab: %s", fs::toUtf8(fs::fromUtf8(pi->path).stem()).c_str());
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
                ImGui::SetTooltip(selection.size() > 1 ? "Save %s's changes into the prefab (all copies update)."
                                                       : "Save this object's changes into the prefab (all copies update).%s",
                                  selection.size() > 1 ? info.name.c_str() : "");
            ImGui::SameLine();
            if (ImGui::SmallButton("Revert")) {
                // Every selected copy goes back to the prefab (ids are kept, so the selection stays).
                recordUndo("Revert to prefab");
                std::vector<UUID> ids;
                for (Entity other : selection)
                    if (s.registry().has<PrefabInstance>(other))
                        ids.push_back(s.info(other).uuid);
                for (UUID id : ids)
                    revertToPrefab(s.findByUUID(id));
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
        // "Reset to defaults" removes and adds it again, which can move it in memory.
        data = ci.get(reg, e);
        if (open && data) {
            if (ci.name == "ParticleEmitter")
                drawParticlePresets(e, selection);
            if (ci.name == "Tilemap" && unlocked(Feature::Tilemap)) {
                bool painting = showTilePainter_;
                if (ImGui::Button(painting ? "Stop painting" : "Paint tiles", {-1, ui::px(28)})) {
                    if (painting)
                        showTilePainter_ = false;
                    else
                        openTilePainter();
                }
            }
            if (ci.name == "ValueBar")
                drawValueBarHint(e);
            if (ci.name == "Terrain") {
                if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ui::px(110));
                    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
                    if (drawComponent(e, ci, data))
                        ++reg.get<Terrain>(e).revision; // size, height... change the mesh
                    ImGui::EndTable();
                }
                drawTerrainInspector(e);
            } else if (ci.name == "Animator") {
                drawAnimator(e, selection);
            } else if (ci.name == "ClickActions") {
                drawClickActions(e, selection);
            } else if (ci.name == "NativeScript") {
                drawNativeScriptInspector(e);
            } else if (ImGui::BeginTable("##fields", 2, ImGuiTableFlags_SizingStretchProp)) {
                ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, ui::px(110));
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

} // namespace aven::editor
