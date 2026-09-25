// The Code ladder: the same logic shown as a behavior or blocks, then EasyScript, then the
// languages of Unity, Godot, Roblox and Unreal, so learners can climb from blocks to "real"
// code and see what carries over to other engines.

#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/runtime/behavior_code.h"
#include "aven/script/translate.h"
#include "block_editor.h"
#include "code_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>

namespace aven::editor {

namespace {

struct Rung {
    const char* label;
    const char* engine;
    const char* about;
    CodeLanguage language;
    script::TargetLanguage target;
};

// Rungs 2+ (rung 0 is the behavior or the blocks, rung 1 is EasyScript).
const Rung kTranslations[] = {
    {"C#", "Unity",
     "Unity scripts are C# classes. Variables have types (float, bool), lines end with ';', and events have "
     "names like Update() and OnTriggerEnter2D().",
     CodeLanguage::CSharp, script::TargetLanguage::Unity},
    {"GDScript", "Godot",
     "GDScript looks a lot like EasyScript: indentation, 'func' instead of 'def', and events like _process(delta). "
     "Each script extends a node type.",
     CodeLanguage::GDScript, script::TargetLanguage::Godot},
    {"Luau", "Roblox",
     "Roblox uses Luau. Blocks end with 'end', variables start with 'local', and events are connected with :Connect().",
     CodeLanguage::Luau, script::TargetLanguage::Roblox},
    {"C++", "Unreal",
     "Unreal uses C++, the most advanced rung: classes, pointers (->), UPROPERTY for settings and Tick() for every frame.",
     CodeLanguage::Cpp, script::TargetLanguage::Unreal},
};

std::string lowerName(std::string s) {
    for (auto& c : s)
        c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    return s;
}

} // namespace

void Editor::openCodeLadder(const std::string& title, const std::string& easyScript, const std::string& path) {
    ladder_ = {};
    ladder_.title = title;
    ladder_.easy = easyScript;
    ladder_.path = path;
    ladder_.fromBlocks = fs::extension(path) == ".blocks";
    ladder_.className = script::classNameFor(path.empty() ? title : path);
    ladder_.rung = 2;
    ladder_.dirty = true;
    showLadder_ = true;
    focusLadder_ = true;
}

void Editor::openCodeLadderForScript(const std::string& path) {
    std::string source;
    std::string title = path;
    // Use the open tab's text when there is one (it may not be saved yet).
    for (auto& t : tabs_)
        if (t->path == path) {
            if (t->code)
                source = t->code->text();
            else if (t->blocks)
                source = blocks::compile(t->blocks->save());
        }
    if (source.empty()) {
        auto text = fs::readText(projectDir_ / path);
        if (!text) {
            notify("Couldn't read " + path, true);
            return;
        }
        source = fs::extension(path) == ".blocks" ? blocks::compileFile(*text) : *text;
    }
    openCodeLadder(title, source, path);
}

void Editor::openCodeLadderForBehavior(Entity e, const std::string& component) {
    const ComponentInfo* info = ComponentRegistry::find(component);
    void* data = info ? info->get(scene().registry(), e) : nullptr;
    if (!data)
        return;
    std::string code = behaviorAsEasyScript(component, saveComponent(*info, data));
    if (code.empty())
        return;
    openCodeLadder(component + " on " + scene().info(e).name, code, "");
    ladder_.behavior = component;
    ladder_.entity = scene().info(e).uuid;
    ladder_.className = script::classNameFor(scene().info(e).name + "_" + component);
    ladder_.rung = 1;
}

void Editor::openCodeLadderForSelection() {
    showLadder_ = focusLadder_ = true;
    Entity e = selected();
    if (!e)
        return;
    auto& reg = scene().registry();
    if (auto* sc = reg.tryGet<Script>(e); sc && !sc->path.empty()) {
        openCodeLadderForScript(sc->path);
        return;
    }
    for (auto& ci : ComponentRegistry::all())
        if (ci.category == "Behaviors" && ci.get(reg, e)) {
            openCodeLadderForBehavior(e, ci.name);
            return;
        }
}

bool Editor::behaviorToScript(Entity e, const std::string& component) {
    Scene& s = editScene();
    auto& reg = s.registry();
    const ComponentInfo* info = ComponentRegistry::find(component);
    void* data = info ? info->get(reg, e) : nullptr;
    if (!data || playing_)
        return false;
    if (reg.has<Script>(e)) {
        notify("This object already has a script. Copy the code from the Code Ladder into it instead.", true);
        return false;
    }
    std::string code = behaviorAsEasyScript(component, saveComponent(*info, data));
    if (code.empty())
        return false;
    std::string path = uniqueName("scripts", lowerName(s.info(e).name + "_" + component), ".es");
    fs::writeText(projectDir_ / path, code);
    recordUndo("Turn " + component + " into a script");
    info->remove(reg, e);
    reg.getOrEmplace<Script>(e).path = path;
    scanAssets();
    openScript(path);
    milestone("code_saved");
    notify("Made " + path + ". It does what the " + component + " behavior did, and now you can change every line.");
    return true;
}

std::string Editor::blocksToScript(const std::string& blocksPath) {
    auto text = fs::readText(projectDir_ / blocksPath);
    if (!text)
        return "";
    std::string error;
    std::string code = blocks::compileFile(*text, &error);
    if (!error.empty()) {
        notify("These blocks have a problem: " + error, true);
        return "";
    }
    std::string base = stdfs::path(blocksPath).stem().string();
    std::string path = uniqueName("scripts", base, ".es");
    fs::writeText(projectDir_ / path, code);
    // Objects using the blocks now use the code.
    recordUndo("Switch to EasyScript");
    int switched = 0;
    auto& reg = editScene().registry();
    editScene().walk([&](Entity e, int) {
        if (auto* sc = reg.tryGet<Script>(e); sc && sc->path == blocksPath) {
            sc->path = path;
            ++switched;
        }
        return true;
    });
    scanAssets();
    openScript(path);
    milestone("code_saved");
    notify("Made " + path + (switched ? " and switched " + std::to_string(switched) + " object(s) to it." : ".") +
           " The blocks file is still there if you want to go back.");
    return path;
}

void Editor::drawCodeLadder() {
    // Opens as a tab next to the scene view, like scripts.
    ImGuiWindow* sceneWindow = ImGui::FindWindowByName("###Viewport");
    if (sceneWindow && sceneWindow->DockId)
        ImGui::SetNextWindowDockID(sceneWindow->DockId, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({820, 620}, ImGuiCond_FirstUseEver);
    if (focusLadder_) {
        ImGui::SetNextWindowFocus();
        focusLadder_ = false;
    }
    if (!ImGui::Begin("Code Ladder###CodeLadder", &showLadder_)) {
        ImGui::End();
        return;
    }
    if (ladder_.easy.empty()) {
        ImGui::TextWrapped("The Code Ladder shows a behavior or script as EasyScript, then as the code Unity, Godot, Roblox and "
                           "Unreal use. Right-click a behavior in the Inspector and choose 'Show as code', or press 'Code "
                           "Ladder' above a script.");
        ImGui::End();
        return;
    }

    ImGui::PushFont(fonts.bold);
    ImGui::TextUnformatted(ladder_.title.c_str());
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::TextDisabled("  the same logic, one rung at a time");

    // The ladder: first rung, EasyScript, then each engine.
    auto rungButton = [&](int index, const std::string& label, const char* tip) {
        bool current = ladder_.rung == index;
        ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_SliderGrab);
        if (current) {
            ImGui::PushStyleColor(ImGuiCol_Button, accent);
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        }
        if (ImGui::Button(label.c_str())) {
            ladder_.rung = index;
            ladder_.dirty = true;
        }
        if (current)
            ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tip);
    };
    ImGui::Spacing();
    std::string first = !ladder_.behavior.empty() ? "1  Behavior" : ladder_.fromBlocks ? "1  Blocks" : "";
    if (!first.empty()) {
        rungButton(0, first, ladder_.fromBlocks ? "The blocks you snap together" : "Settings in the Inspector, no code");
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
    }
    rungButton(1, std::string(first.empty() ? "1" : "2") + "  EasyScript", "Aven's own language: simple and Python-like");
    ImGui::SameLine();
    ImGui::TextDisabled(">");
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s  Other engines:", first.empty() ? "2" : "3");
    for (int i = 0; i < 4; ++i) {
        ImGui::SameLine();
        rungButton(2 + i, std::string(kTranslations[i].label) + " (" + kTranslations[i].engine + ")", kTranslations[i].about);
    }

    // Translate when the rung changes.
    if (ladder_.dirty) {
        ladder_.dirty = false;
        ladder_.notes.clear();
        ladder_.error.clear();
        if (!ladder_.view) {
            ladder_.view = std::make_unique<CodeEditor>();
            ladder_.view->readOnly = true;
            ladder_.left = std::make_unique<CodeEditor>();
            ladder_.left->readOnly = true;
        }
        ladder_.view->font = fonts.code;
        ladder_.left->font = fonts.code;
        ladder_.left->setText(ladder_.easy);
        if (ladder_.rung <= 1) {
            ladder_.view->language = CodeLanguage::EasyScript;
            ladder_.view->setText(ladder_.easy);
        } else {
            const Rung& r = kTranslations[ladder_.rung - 2];
            script::TranslateOptions options;
            options.className = ladder_.className;
            options.is3D = view3D_;
            script::Translation t = script::translate(ladder_.easy, r.target, options);
            ladder_.view->language = r.language;
            if (t.ok) {
                ladder_.view->setText(t.code);
                ladder_.notes = t.notes;
            } else {
                ladder_.error = t.error;
                ladder_.view->setText("");
            }
        }
    }

    ImGui::Separator();
    // What this rung is about.
    ImGui::PushTextWrapPos(0);
    if (ladder_.rung == 0) {
        if (ladder_.fromBlocks)
            ImGui::TextUnformatted("Blocks are the first rung: each block is one instruction, and the shapes only fit where "
                                   "they make sense. Next rung: the same thing as EasyScript text.");
        else
            ImGui::TextUnformatted("A behavior is the first rung: it works with no code at all, and you change it with "
                                   "settings in the Inspector. Next rung: the EasyScript that does the same job.");
    } else if (ladder_.rung == 1) {
        ImGui::TextUnformatted("EasyScript is Aven's own language. It reads like Python: 'def' makes a function, indentation "
                               "groups lines, and events like on_update(dt) run by themselves.");
    } else {
        ImGui::TextUnformatted(kTranslations[ladder_.rung - 2].about);
    }
    ImGui::PopTextWrapPos();

    // Actions.
    if (ladder_.rung == 0 && ladder_.fromBlocks && ImGui::Button("Open the blocks"))
        openScript(ladder_.path);
    if (ladder_.rung == 0 && !ladder_.behavior.empty()) {
        if (Entity e = scene().findByUUID(ladder_.entity); e && ImGui::Button("Select the object"))
            select(e);
    }
    if (ladder_.rung >= 1) {
        if (ImGui::Button("Copy code"))
            ImGui::SetClipboardText(ladder_.view->text().c_str());
        if (ladder_.rung >= 2) {
            ImGui::SameLine();
            ImGui::Checkbox("Side by side with EasyScript", &ladder_.sideBySide);
        }
        if (ladder_.rung == 1 && !ladder_.behavior.empty() && unlocked(Feature::Code)) {
            ImGui::SameLine();
            Entity e = editScene().findByUUID(ladder_.entity);
            ImGui::BeginDisabled(!e || playing_);
            if (ImGui::Button("Turn the behavior into this script") && e && behaviorToScript(e, ladder_.behavior)) {
                ladder_.behavior.clear();
                ladder_.title = "Script on " + editScene().info(e).name;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Replaces the behavior with a script you can edit line by line.");
        }
        if (ladder_.rung == 1 && ladder_.fromBlocks && unlocked(Feature::Code)) {
            ImGui::SameLine();
            ImGui::BeginDisabled(playing_);
            if (ImGui::Button("Switch to EasyScript")) {
                std::string made = blocksToScript(ladder_.path);
                if (!made.empty()) {
                    ladder_.path = made;
                    ladder_.fromBlocks = false;
                }
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Makes an EasyScript file from these blocks and uses it instead.");
        }
        if (ladder_.rung == 1 && !ladder_.path.empty() && !ladder_.fromBlocks) {
            ImGui::SameLine();
            if (ImGui::Button("Open in the code editor"))
                openScript(ladder_.path);
        }
    }
    ImGui::Spacing();

    if (!ladder_.error.empty()) {
        ImGui::TextColored({1, 0.45f, 0.45f, 1}, "The EasyScript has an error, so it can't be translated yet:");
        ImGui::TextWrapped("%s", ladder_.error.c_str());
        ImGui::End();
        return;
    }
    if (ladder_.rung == 0) {
        // The first rung: the behavior's settings, or a reminder of what blocks are.
        if (!ladder_.behavior.empty()) {
            Entity e = scene().findByUUID(ladder_.entity);
            const ComponentInfo* info = ComponentRegistry::find(ladder_.behavior);
            void* data = e && info ? info->get(scene().registry(), e) : nullptr;
            if (data) {
                Json values = saveComponent(*info, data);
                if (ImGui::BeginTable("##settings", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
                    for (auto& f : info->fields) {
                        if (f.options.runtime)
                            continue;
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(f.label.c_str());
                        ImGui::TableNextColumn();
                        std::string v;
                        if (values.contains(f.name))
                            v = values[f.name].isString() ? values[f.name].asString() : values[f.name].dump();
                        ImGui::TextUnformatted(v.c_str());
                    }
                    ImGui::EndTable();
                }
            } else {
                ImGui::TextDisabled("The behavior was removed or turned into a script.");
            }
        } else {
            ImGui::TextDisabled("Open the blocks to change them; the next rung shows the same script as text.");
        }
        ImGui::End();
        return;
    }

    float notesHeight = ladder_.notes.empty() ? 0 : std::min(130.0f, 28.0f + ImGui::GetTextLineHeightWithSpacing() * static_cast<float>(ladder_.notes.size() + 1));
    ImVec2 avail = ImGui::GetContentRegionAvail();
    avail.y -= notesHeight;
    if (ladder_.sideBySide && ladder_.rung >= 2) {
        float w = (avail.x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        ImGui::BeginGroup();
        ImGui::TextDisabled("EasyScript");
        ladder_.left->draw("##ladder_left", {w, avail.y - ImGui::GetTextLineHeightWithSpacing()});
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::TextDisabled("%s (%s)", kTranslations[ladder_.rung - 2].label, kTranslations[ladder_.rung - 2].engine);
        ladder_.view->draw("##ladder_code", {w, avail.y - ImGui::GetTextLineHeightWithSpacing()});
        ImGui::EndGroup();
    } else {
        ladder_.view->draw("##ladder_code", avail);
    }
    if (!ladder_.notes.empty()) {
        ImGui::BeginChild("##ladder_notes", {0, 0}, ImGuiChildFlags_None);
        ImGui::PushFont(fonts.bold);
        ImGui::Text("How %s is different", kTranslations[ladder_.rung - 2].engine);
        ImGui::PopFont();
        for (auto& note : ladder_.notes) {
            ImGui::Bullet();
            ImGui::PushTextWrapPos(0);
            ImGui::TextUnformatted(note.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace aven::editor
