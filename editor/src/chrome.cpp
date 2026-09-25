// The editor's frame: dock layouts, menus, toolbar, status bar, Preferences and Learn mode levels.

#include "editor.h"

#include "aven/core/fs.h"
#include "block_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

namespace aven::editor {

namespace {

const char* kLayouts[] = {"Default", "Beginner", "Big Scene", "Coder", "Artist"};

float statusBarHeight() { return ImGui::GetFrameHeight() + 4; }

} // namespace

// ---------------------------------------------------------------- dock space and layouts

void Editor::buildLayout(const std::string& name, unsigned int dockId) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, {vp->WorkSize.x, vp->WorkSize.y - 80});
    ImGuiID center = dockId, left = 0, right = 0, bottom = 0, leftBottom = 0, rightBottom = 0;
    auto dock = [](const char* window, ImGuiID node) { ImGui::DockBuilderDockWindow(window, node); };
    if (name == "Beginner") {
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.2f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.3f, nullptr, &center);
        rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.45f, nullptr, &right);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.22f, nullptr, &center);
        dock("Hierarchy", left);
        dock("Inspector", right);
        dock("Learn", rightBottom);
        dock("###Explain", rightBottom);
        dock("###RecipeCard", rightBottom);
        dock("Assets", bottom);
        dock("###Console", bottom);
        dock("###Doctor", bottom);
    } else if (name == "Big Scene") {
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
        rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.6f, nullptr, &right);
        dock("Hierarchy", right);
        dock("Inspector", rightBottom);
        dock("Learn", rightBottom);
        dock("###RecipeCard", rightBottom);
        dock("Assets", right);
        dock("###Console", right);
        dock("###Doctor", right);
    } else if (name == "Coder") {
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f, nullptr, &left);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.3f, nullptr, &center);
        dock("Hierarchy", left);
        dock("Assets", leftBottom);
        dock("Inspector", right);
        dock("Learn", right);
        dock("###RecipeCard", right);
        dock("###Console", bottom);
        dock("###Doctor", bottom);
        dock("Scripting Reference", right);
    } else if (name == "Artist") {
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.22f, nullptr, &center);
        leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.55f, nullptr, &left);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.18f, nullptr, &center);
        dock("Hierarchy", left);
        dock("Assets", leftBottom);
        dock("Inspector", right);
        dock("Learn", right);
        dock("###RecipeCard", right);
        dock("###Console", bottom);
        dock("###Doctor", bottom);
    } else {
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
        dock("Hierarchy", left);
        dock("Inspector", right);
        dock("Learn", right);
        dock("###Explain", right);
        dock("###RecipeCard", right);
        dock("Assets", bottom);
        dock("###Console", bottom);
        dock("###Doctor", bottom);
    }
    dock("###Viewport", center);
    ImGui::DockBuilderFinish(dockId);
}

void Editor::setupDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar(2);
    drawMenuBar();
    drawToolbar();
    ImGuiID dockId = ImGui::GetID("MainDock");
    if (resetLayout_ || !ImGui::DockBuilderGetNode(dockId)) {
        resetLayout_ = false;
        buildLayout(prefs.layout, dockId);
    }
    ImGui::DockSpace(dockId, {0, -statusBarHeight()}, ImGuiDockNodeFlags_None);
    drawStatusBar();
    ImGui::End();
}

// ---------------------------------------------------------------- menus

void Editor::drawMenuBar() {
    if (!ImGui::BeginMenuBar())
        return;
    auto key = [this](const char* action) {
        static std::string s;
        s = chordName(prefs.chord(action));
        return s.c_str();
    };
    Entity sel = selected();
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("New 2D Scene"))
            newScene(false);
        if (ImGui::MenuItem("New 3D Scene"))
            newScene(true);
        if (ImGui::BeginMenu("Open Scene")) {
            for (auto& s : projectFiles({".scene"}))
                if (ImGui::MenuItem(s.c_str(), nullptr, s == scenePath_))
                    openScene(s);
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Save Scene", key("save")))
            saveScene();
        ImGui::Separator();
        if ((unlocked(Feature::Export) || unlocked(Feature::Share)) && ImGui::MenuItem("Build & Share Game..."))
            showExport_ = true;
        if (unlocked(Feature::ProjectSettings) && ImGui::MenuItem("Project Settings..."))
            showSettings_ = true;
        if (ImGui::MenuItem("Preferences...", key("preferences")))
            showPrefs_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Project Hub (new or open project)")) {
            saveAllScripts();
            showHub_ = true;
        }
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
            requestQuit();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", key("undo"), false, !undo_.empty() && !playing_))
            undo();
        if (ImGui::MenuItem("Redo", key("redo"), false, !redo_.empty() && !playing_))
            redo();
        ImGui::Separator();
        if (ImGui::MenuItem("Cut", key("cut"), false, sel && !playing_))
            copySelection(true);
        if (ImGui::MenuItem("Copy", key("copy"), false, sel && !playing_))
            copySelection(false);
        if (ImGui::MenuItem("Paste", key("paste"), false, !playing_))
            pasteClipboard();
        if (ImGui::MenuItem("Duplicate", key("duplicate"), false, sel && !playing_)) {
            recordUndo("Duplicate");
            select(scene_->duplicate(sel));
        }
        if (ImGui::MenuItem("Delete", key("delete"), false, sel && !playing_)) {
            recordUndo("Delete");
            for (Entity e : selectedEntities())
                scene_->destroy(e);
            selection_.clear();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Select All", key("select_all"), false, !playing_)) {
            selection_.clear();
            for (Entity e : scene().roots())
                addToSelection(e);
        }
        if (ImGui::MenuItem("Select None", nullptr, false, !selection_.empty()))
            selection_.clear();
        ImGui::Separator();
        if (unlocked(Feature::History) && ImGui::MenuItem("Undo History"))
            showHistory_ = true;
        if (unlocked(Feature::Find) && ImGui::MenuItem("Find in Project...", key("find"))) {
            showFind_ = true;
            findFocus_ = true;
        }
        if (unlocked(Feature::CommandPalette) && ImGui::MenuItem("Command Palette...", key("command_palette")))
            showPalette_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Preferences...", key("preferences")))
            showPrefs_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        ImGui::BeginDisabled(playing_);
        if (unlocked(Feature::Recipes)) {
            if (ImGui::MenuItem("Game from a Recipe..."))
                openRecipes();
            ImGui::Separator();
        }
        if (ImGui::BeginMenu("2D Shape")) {
            for (const char* k : {"Square", "Circle", "Triangle", "Rounded Square", "Diamond", "Star", "Heart", "Sprite"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("3D Shape")) {
            for (const char* k : {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Cone", "Torus"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        if (unlocked(Feature::Lighting) && ImGui::BeginMenu("Light")) {
            for (const char* k : {"Sun", "Point Light", "Spot Light", "Environment"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        if (unlocked(Feature::UI) && ImGui::BeginMenu("UI")) {
            for (const char* k : {"UI Text", "UI Button", "UI Panel"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        for (const char* k : {"Text", "Camera", "Player 3D"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        if (unlocked(Feature::Particles) && ImGui::MenuItem("Particles"))
            createEntity("Particles");
        if (unlocked(Feature::Audio) && ImGui::MenuItem("Sound"))
            createEntity("Sound");
        if (ImGui::MenuItem("Empty Object"))
            createEntity("Entity");
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
        ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy_);
        ImGui::MenuItem("Inspector", nullptr, &showInspector_);
        if (unlocked(Feature::Assets))
            ImGui::MenuItem("Assets", nullptr, &showAssets_);
        if (unlocked(Feature::Console))
            ImGui::MenuItem("Console", nullptr, &showConsole_);
        ImGui::MenuItem("Learn (tutorial)", nullptr, &showLearn_, !tutorial_.isNull());
        ImGui::MenuItem("Scripting Reference", nullptr, &showReference_);
        if (unlocked(Feature::Explain))
            ImGui::MenuItem("Explain", chordName(prefs.chord("explain")).c_str(), &showExplain_);
        if (unlocked(Feature::CodeLadder) && ImGui::MenuItem("Code Ladder", nullptr, showLadder_)) {
            showLadder_ = !showLadder_;
            if (showLadder_)
                openCodeLadderForSelection();
        }
        if (unlocked(Feature::BugReplay) && ImGui::MenuItem("Bug Replay", nullptr, showReplay_)) {
            if (showReplay_)
                showReplay_ = false;
            else
                openBugReplay();
        }
        if (unlocked(Feature::Doctor) && ImGui::MenuItem("Error Doctor", nullptr, showDoctor_)) {
            showDoctor_ = !showDoctor_;
            if (showDoctor_) {
                runCheckup();
                focusDoctor_ = true;
            }
        }
        ImGui::Separator();
        if (unlocked(Feature::History))
            ImGui::MenuItem("Undo History", nullptr, &showHistory_);
        if (unlocked(Feature::Profiler))
            ImGui::MenuItem("Profiler", nullptr, &showProfiler_);
        if (unlocked(Feature::Lighting))
            ImGui::MenuItem("Lighting Presets", nullptr, &showLighting_);
        ImGui::Separator();
        if (ImGui::BeginMenu("Layout")) {
            for (const char* l : kLayouts)
                if (ImGui::MenuItem(l, nullptr, prefs.layout == l))
                    pendingLayout_ = l;
            if (!prefs.savedLayouts.empty()) {
                ImGui::Separator();
                for (auto& [name, ini] : prefs.savedLayouts)
                    if (ImGui::MenuItem(name.c_str(), nullptr, prefs.layout == name))
                        pendingLayout_ = name;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save current layout...")) {
                size_t size = 0;
                const char* ini = ImGui::SaveIniSettingsToMemory(&size);
                std::string name = "My layout " + std::to_string(prefs.savedLayouts.size() + 1);
                prefs.savedLayouts[name] = std::string(ini, size);
                prefs.layout = name;
                prefs.save();
                notify("Saved the layout as '" + name + "'. Rename it in Preferences.");
            }
            if (ImGui::MenuItem("Reset layout"))
                resetLayout_ = true;
            ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Learn mode levels..."))
            showLevels_ = true;
        ImGui::MenuItem("Learn (tutorial)", nullptr, &showLearn_, !tutorial_.isNull());
        ImGui::MenuItem("Recipe Card", nullptr, &showRecipeCard_, !recipeCard_.parts.empty());
        ImGui::Separator();
        if (ImGui::MenuItem("Contributor Quests..."))
            openQuests();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Small, guided ways to help build Aven itself");
        ImGui::MenuItem("Scripting Reference", nullptr, &showReference_);
        if (ImGui::MenuItem("Keyboard shortcuts...")) {
            showPrefs_ = true;
            prefsSection_ = "Shortcuts";
        }
        ImGui::Separator();
        if (ImGui::MenuItem("About Aven"))
            showAbout_ = true;
        ImGui::EndMenu();
    }
    // Project name on the right.
    std::string label = settings_.name + (dirty_ ? "  (unsaved)" : "");
    ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::CalcTextSize(label.c_str()).x - 20);
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::EndMenuBar();
    if (showAbout_) {
        ImGui::OpenPopup("About Aven");
        showAbout_ = false;
    }
    if (ImGui::BeginPopupModal("About Aven", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(fonts.big);
        ImGui::Text("Aven %s", AVEN_VERSION);
        ImGui::PopFont();
        ImGui::Text("A beginner-friendly 2D and 3D game engine.");
        ImGui::TextDisabled("Blocks -> EasyScript -> C/C++ -> any engine you like");
        ImGui::Spacing();
        ImGui::TextDisabled("Renderer: %s", device_.description().c_str());
        ImGui::TextDisabled("Preferences: %s", (fs::userDataDir("Aven Editor") / "preferences.json").string().c_str());
        if (ImGui::Button("Close", {120, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- toolbar

// The toolbar lives inside the root window, between the menu bar and the dock space.
void Editor::drawToolbar() {
    float height = ImGui::GetFrameHeight() + 14;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 7});
    ImGui::BeginChild("##toolbar", {0, height}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    float b = ImGui::GetFrameHeight();
    if (ui::iconButton("move", ui::Move, "Move (W)", gizmoOp_ == 0, b))
        gizmoOp_ = 0;
    ImGui::SameLine();
    if (ui::iconButton("rotate", ui::Rotate, "Rotate (E)", gizmoOp_ == 1, b))
        gizmoOp_ = 1;
    ImGui::SameLine();
    if (ui::iconButton("scale", ui::Scale, "Scale (R)", gizmoOp_ == 2, b))
        gizmoOp_ = 2;
    ImGui::SameLine();
    if (ImGui::Button(prefs.gizmoLocal ? "Local" : "World", {b * 2.3f, b}))
        prefs.gizmoLocal = !prefs.gizmoLocal;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Move and scale along the world axes or the object's own axes.");
    ImGui::SameLine();
    if (ui::iconButton("snap", ui::Magnet, "Snap to grid (or hold Ctrl while dragging)", snap_, b))
        snap_ = !snap_;
    if (ImGui::BeginPopupContextItem("snap_settings")) {
        ImGui::TextDisabled("Snap steps");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Move", &prefs.moveSnap, 0.05f, 0.05f, 10.0f, "%.2f");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Rotate", &prefs.rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        ImGui::SetNextItemWidth(120);
        ImGui::DragFloat("Scale", &prefs.scaleSnap, 0.05f, 0.05f, 2.0f, "%.2f");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ui::iconButton("grid", ui::Grid, "Show grid", showGrid_, b))
        showGrid_ = !showGrid_;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70);
    int view = view3D_ ? 1 : 0;
    const char* views[] = {"2D", "3D"};
    if (ImGui::Combo("##view", &view, views, 2))
        view3D_ = view == 1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Switch between the 2D and 3D scene view");

    // Play controls in the middle.
    float center = ImGui::GetWindowWidth() * 0.5f;
    ImGui::SameLine(center - b * 1.6f);
    if (ui::iconButton("play", playing_ ? ui::Stop : ui::Play, playing_ ? "Stop (Ctrl+P)" : "Play (Ctrl+P)", playing_, b))
        playing_ ? stop() : play();
    ImGui::SameLine();
    ImGui::BeginDisabled(!playing_);
    if (ui::iconButton("pause", ui::Pause, "Pause: then click objects in the game to inspect and change them", paused_, b))
        paused_ = !paused_;
    ImGui::SameLine();
    if (ui::iconButton("step", ui::Step, "Next frame", false, b)) {
        paused_ = true;
        stepOnce_ = true;
    }
    ImGui::EndDisabled();

    // Right side, aligned using the width measured last frame.
    static float rightWidth = 400;
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 20, ImGui::GetWindowWidth() - rightWidth - 10));
    float rightStart = ImGui::GetCursorScreenPos().x;
    if (!tutorial_.isNull()) {
        if (ImGui::Button("Learn"))
            showLearn_ = !showLearn_;
        ImGui::SameLine();
    }
    if (ImGui::Button("Reference"))
        showReference_ = !showReference_;
    ImGui::SameLine();
    // Learn mode level badge.
    {
        std::string badge = std::string("Level: ") + levelName(prefs.level);
        Color a = prefs.accentColor();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(a.r, a.g, a.b, 0.25f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(a.r, a.g, a.b, 0.4f));
        if (ImGui::Button(badge.c_str()))
            showLevels_ = true;
        ImGui::PopStyleColor(2);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Learn mode: the editor shows more as you learn. Click to choose a level.");
    }
    if (unlocked(Feature::Share) || unlocked(Feature::Export)) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(34, 150, 90, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 175, 105, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
        if (ImGui::Button(unlocked(Feature::Export) ? "Build & Share" : "Share"))
            unlocked(Feature::Export) ? void(showExport_ = true) : openShare();
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Make a version anyone can play: on a computer, in a web browser, or on your Wi-Fi");
    }
    rightWidth = ImGui::GetItemRectMax().x - rightStart;
    ImGui::EndChild();
}

// ---------------------------------------------------------------- status bar

void Editor::drawStatusBar() {
    float h = statusBarHeight();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(pos, {pos.x + ImGui::GetWindowWidth(), pos.y + h},
                                              ImGui::GetColorU32(ImGuiCol_MenuBarBg));
    ImGui::SetCursorScreenPos({pos.x + 10, pos.y + 2});
    ImGui::AlignTextToFramePadding();
    if (playing_)
        ImGui::TextColored({0.4f, 0.9f, 0.55f, 1}, paused_ ? "Paused" : "Playing");
    else
        ImGui::TextDisabled("Editing");
    ImGui::SameLine(0, 18);
    auto sel = selectedEntities();
    if (sel.size() == 1)
        ImGui::TextDisabled("Selected: %s", scene().info(sel[0]).name.c_str());
    else if (sel.size() > 1)
        ImGui::TextDisabled("%d objects selected", static_cast<int>(sel.size()));
    else
        ImGui::TextDisabled("%s", scenePath_.empty() ? "Unsaved scene" : scenePath_.c_str());

    drawCaptureStatus();
    // Right side: errors, level progress, frame rate.
    std::string right = std::to_string(static_cast<int>(std::round(ImGui::GetIO().Framerate))) + " FPS";
    std::string hint = nextLevelHint(prefs);
    float x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - 16;
    if (errorCount_ > 0) {
        std::string err = std::to_string(errorCount_) + (errorCount_ == 1 ? " error" : " errors");
        float w = ImGui::CalcTextSize(err.c_str()).x + 16;
        x -= w + 12;
        ImGui::SameLine(x);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(150, 40, 45, 255));
        if (ImGui::SmallButton(err.c_str())) {
            if (unlocked(Feature::Doctor)) {
                runCheckup();
                showDoctor_ = focusDoctor_ = true;
            } else {
                showConsole_ = true;
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(unlocked(Feature::Doctor) ? "Open the Error Doctor" : "Show the Console");
        ImGui::PopStyleColor();
        x = ImGui::GetWindowWidth() - ImGui::CalcTextSize(right.c_str()).x - 16;
    }
    ImGui::SameLine(x);
    ImGui::TextDisabled("%s", right.c_str());
}

// ---------------------------------------------------------------- shortcuts

void Editor::handleShortcuts() {
    if (shortcut("save")) {
        bool scriptChanged = false;
        for (auto& t : tabs_)
            scriptChanged = scriptChanged || t->modified;
        saveAllScripts();
        if (!playing_)
            saveScene();
        else if (scriptChanged)
            notify("Scripts saved.");
    }
    if (shortcut("play"))
        playing_ ? stop() : play();
    if (shortcut("pause") && playing_)
        paused_ = !paused_;
    if (shortcut("step") && playing_) {
        paused_ = true;
        stepOnce_ = true;
    }
    if (shortcut("preferences"))
        showPrefs_ = !showPrefs_;
    if (shortcut("explain") && unlocked(Feature::Explain)) {
        showExplain_ = true;
        focusExplain_ = true;
    }
    if (shortcut("screenshot") && unlocked(Feature::Capture))
        takeScreenshot();
    if (shortcut("record_gif") && unlocked(Feature::Capture))
        toggleGifRecording();
    if (shortcut("doctor") && unlocked(Feature::Doctor)) {
        runCheckup();
        showDoctor_ = focusDoctor_ = true;
    }
    if (shortcut("ask") && unlocked(Feature::Assistant)) {
        showInspector_ = true;
        assistantFocus_ = true;
    }
    if (shortcut("command_palette") && unlocked(Feature::CommandPalette))
        showPalette_ = true;
    if (shortcut("find") && unlocked(Feature::Find)) {
        showFind_ = true;
        findFocus_ = true;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Q))
        requestQuit();
    if (ImGui::GetIO().WantTextInput)
        return;
    if (shortcut("toggle_2d3d"))
        view3D_ = !view3D_;
    if (playing_)
        return;
    if (shortcut("undo"))
        undo();
    if (shortcut("redo") || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))
        redo();
    bool sceneFocus = viewportFocused_ || hierarchyFocused_;
    auto sel = selectedEntities();
    if (!sel.empty() && shortcut("duplicate")) {
        recordUndo("Duplicate");
        std::vector<Entity> copies;
        for (Entity e : sel)
            copies.push_back(scene_->duplicate(e));
        selection_.clear();
        for (Entity c : copies)
            addToSelection(c);
    }
    if (!sel.empty() && shortcut("delete") && sceneFocus) {
        recordUndo("Delete");
        for (Entity e : sel)
            if (scene_->valid(e))
                scene_->destroy(e);
        selection_.clear();
    }
    if (sceneFocus && shortcut("copy"))
        copySelection(false);
    if (sceneFocus && shortcut("cut"))
        copySelection(true);
    if (sceneFocus && shortcut("paste"))
        pasteClipboard();
    if (sceneFocus && shortcut("select_all")) {
        selection_.clear();
        for (Entity e : scene_->roots())
            addToSelection(e);
    }
    if (viewportHovered_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (shortcut("tool_move"))
            gizmoOp_ = 0;
        if (shortcut("tool_rotate"))
            gizmoOp_ = 1;
        if (shortcut("tool_scale"))
            gizmoOp_ = 2;
        if (shortcut("focus"))
            focusSelected();
    }
}

// ---------------------------------------------------------------- preferences

void Editor::drawPreferences() {
    ImGui::SetNextWindowSize({760, 560}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Preferences", &showPrefs_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    const char* sections[] = {"Look", "Code", "Scene view", "Behavior", "Learning", "Shortcuts", "Profile"};
    ImGui::BeginChild("##sections", {150, 0}, ImGuiChildFlags_Borders);
    for (const char* s : sections)
        if (ImGui::Selectable(s, prefsSection_ == s, 0, {0, ImGui::GetFrameHeight()}))
            prefsSection_ = s;
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##page");
    bool changed = false, restyle = false;
    auto label = [](const char* text, const char* help = nullptr) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(text);
        if (help && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", help);
        ImGui::SameLine(190);
        ImGui::SetNextItemWidth(-1);
    };

    if (prefsSection_ == "Look") {
        ui::sectionHeader("Theme");
        float w = ImGui::GetContentRegionAvail().x;
        int perRow = std::max(1, static_cast<int>(w / 150));
        int i = 0;
        for (auto& t : themePresets()) {
            if (i++ % perRow)
                ImGui::SameLine();
            ImGui::PushID(t.name);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##theme", {140, 74})) {
                prefs.theme = t.name;
                prefs.customAccent = false;
                restyle = true;
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto col = [](uint32_t c) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255); };
            bool current = prefs.theme == t.name;
            dl->AddRectFilled(p, {p.x + 140, p.y + 74}, col(t.background), 6);
            dl->AddRectFilled({p.x + 8, p.y + 8}, {p.x + 50, p.y + 50}, col(t.panel), 4);
            dl->AddRectFilled({p.x + 56, p.y + 8}, {p.x + 132, p.y + 22}, col(t.frame), 3);
            dl->AddRectFilled({p.x + 56, p.y + 28}, {p.x + 110, p.y + 42}, col(t.accent), 3);
            dl->AddText({p.x + 8, p.y + 54}, col(t.text), t.name);
            dl->AddRect(p, {p.x + 140, p.y + 74}, current ? ImGui::GetColorU32(ImGuiCol_CheckMark) : col(t.border), 6, 0,
                        current ? 2.5f : 1.0f);
            ImGui::PopID();
        }
        ui::sectionHeader("Colors and size");
        label("Custom accent color", "Buttons, highlights and selection use this color.");
        if (ImGui::Checkbox("##customaccent", &prefs.customAccent))
            restyle = true;
        if (prefs.customAccent) {
            ImGui::SameLine();
            if (ImGui::ColorEdit3("##accent", &prefs.accent.r, ImGuiColorEditFlags_NoInputs))
                restyle = true;
            ImGui::SameLine();
            static const uint32_t swatches[] = {0x3B82F6, 0x8B5CF6, 0xEC4899, 0xEF4444, 0xF97316, 0xEAB308, 0x22C55E, 0x06B6D4};
            for (uint32_t sw : swatches) {
                ImGui::SameLine();
                Color c = Color::fromHex(sw);
                ImGui::PushID(static_cast<int>(sw));
                if (ImGui::ColorButton("##sw", {c.r, c.g, c.b, 1}, ImGuiColorEditFlags_NoTooltip, {20, 20})) {
                    prefs.accent = c;
                    restyle = true;
                }
                ImGui::PopID();
            }
        }
        label("UI size", "Makes everything bigger or smaller.");
        if (ImGui::SliderFloat("##scale", &prefs.uiScale, 0.75f, 1.75f, "%.2fx"))
            changed = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
            restyle = true;
        label("Text size");
        if (ImGui::SliderInt("##font", &prefs.fontSize, 12, 24, "%d px"))
            changed = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
            restyle = true;
        label("Rounded corners");
        restyle |= ImGui::Checkbox("##rounded", &prefs.rounded);
        label("Compact spacing", "Fit more on the screen.");
        restyle |= ImGui::Checkbox("##compact", &prefs.compact);
        ui::sectionHeader("Layout");
        label("Panel layout");
        if (ImGui::BeginCombo("##layout", prefs.layout.c_str())) {
            for (const char* l : kLayouts)
                if (ImGui::Selectable(l, prefs.layout == l))
                    pendingLayout_ = l;
            for (auto& [name, ini] : prefs.savedLayouts)
                if (ImGui::Selectable(name.c_str(), prefs.layout == name))
                    pendingLayout_ = name;
            ImGui::EndCombo();
        }
        std::string removeLayout;
        for (auto& [name, ini] : prefs.savedLayouts) {
            ImGui::PushID(name.c_str());
            ImGui::TextDisabled("Saved: %s", name.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Delete"))
                removeLayout = name;
            ImGui::PopID();
        }
        if (!removeLayout.empty()) {
            prefs.savedLayouts.erase(removeLayout);
            changed = true;
        }
    } else if (prefsSection_ == "Code") {
        ui::sectionHeader("Code colors");
        for (auto& p : CodePalette::presets()) {
            ImGui::PushID(p.name.c_str());
            ImVec2 pos = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            if (ImGui::InvisibleButton("##pal", {w, 58})) {
                prefs.codeTheme = p.name;
                CodeEditor::palette = p;
                changed = true;
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, {pos.x + w, pos.y + 58}, p.background, 6);
            if (prefs.codeTheme == p.name)
                dl->AddRect(pos, {pos.x + w, pos.y + 58}, ImGui::GetColorU32(ImGuiCol_CheckMark), 6, 0, 2.5f);
            ImFont* f = fonts.code;
            float fs = f ? f->FontSize : ImGui::GetFontSize();
            float x = pos.x + 12, y = pos.y + 8;
            auto word = [&](const char* text, ImU32 c) {
                dl->AddText(f, fs, {x, y}, c, text);
                x += ImGui::CalcTextSize(text).x * (f ? fs / ImGui::GetFontSize() : 1) + 2;
            };
            word("def ", p.keyword);
            word("on_update", p.function);
            word("(dt):  ", p.text);
            word("# ", p.comment);
            word(p.name.c_str(), p.comment);
            x = pos.x + 40;
            y += fs + 6;
            word("self", p.self);
            word(".x += ", p.text);
            word("axis", p.builtin);
            word("(", p.text);
            word("\"horizontal\"", p.string);
            word(") * ", p.text);
            word("5", p.number);
            ImGui::PopID();
            ImGui::Spacing();
        }
        ui::sectionHeader("Size");
        label("Code text size");
        if (ImGui::SliderInt("##codefont", &prefs.codeFontSize, 11, 26, "%d px"))
            changed = true;
        if (ImGui::IsItemDeactivatedAfterEdit())
            restyle = true;
    } else if (prefsSection_ == "Scene view") {
        ui::sectionHeader("Grid and selection");
        label("Grid color");
        changed |= ImGui::ColorEdit3("##grid", &prefs.gridColor.r);
        label("Grid strength");
        changed |= ImGui::SliderFloat("##gridop", &prefs.gridOpacity, 0.0f, 2.0f, "%.1f");
        label("Selection outline");
        changed |= ImGui::ColorEdit3("##selcol", &prefs.selectionColor.r);
        label("Show icons", "Small icons for lights, cameras, sounds and particles.");
        changed |= ImGui::Checkbox("##icons", &prefs.showIcons);
        label("Show colliders", "Green outlines of the collision shapes of the selected object.");
        changed |= ImGui::Checkbox("##colliders", &prefs.showColliders);
        label("Show control hints");
        changed |= ImGui::Checkbox("##hints", &prefs.showHints);
        ui::sectionHeader("Camera and gizmos");
        label("3D fly speed");
        changed |= ImGui::SliderFloat("##fly", &prefs.flySpeed, 1.0f, 40.0f, "%.0f");
        label("Move snap step");
        changed |= ImGui::DragFloat("##ms", &prefs.moveSnap, 0.05f, 0.05f, 10.0f, "%.2f");
        label("Rotate snap step");
        changed |= ImGui::DragFloat("##rs", &prefs.rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        label("Scale snap step");
        changed |= ImGui::DragFloat("##ss", &prefs.scaleSnap, 0.05f, 0.05f, 2.0f, "%.2f");
        label("Gizmo uses local axes");
        changed |= ImGui::Checkbox("##local", &prefs.gizmoLocal);
    } else if (prefsSection_ == "Behavior") {
        ui::sectionHeader("Saving");
        label("Autosave every", "0 turns autosave off.");
        changed |= ImGui::SliderInt("##autosave", &prefs.autosaveMinutes, 0, 30, prefs.autosaveMinutes ? "%d min" : "off");
        ui::sectionHeader("Playing");
        label("Clear console on Play");
        changed |= ImGui::Checkbox("##clear", &prefs.clearConsoleOnPlay);
        label("Pause when an error happens", "Stops the game at the moment of an error so you can look around.");
        changed |= ImGui::Checkbox("##pauseerr", &prefs.pauseOnError);
        label("Record bug replays", "Keeps the last 20 seconds of play so you can see what led to an error.");
        changed |= ImGui::Checkbox("##replay", &prefs.recordReplays);
        ui::sectionHeader("Editing");
        label("Ask before deleting");
        changed |= ImGui::Checkbox("##confirmdel", &prefs.confirmDelete);
    } else if (prefsSection_ == "Learning") {
        ui::sectionHeader("Learn mode");
        ImGui::TextWrapped("The editor starts simple and shows more as you learn. You're at the %s level.", levelName(prefs.level));
        for (int lvl = 1; lvl <= 4; ++lvl) {
            ImGui::PushID(lvl);
            if (ImGui::RadioButton(levelName(lvl), prefs.level == lvl)) {
                prefs.level = lvl;
                changed = true;
            }
            ImGui::SameLine(120);
            ImGui::TextDisabled("%s", levelBlurb(lvl));
            ImGui::PopID();
        }
        label("Level up automatically", "When you've practiced enough, Aven offers to show more features.");
        changed |= ImGui::Checkbox("##autolevel", &prefs.autoLevelUp);
        ui::sectionHeader("Tips");
        if (ImGui::Button("Show all tips again")) {
            prefs.seenTips.clear();
            changed = true;
        }
    } else if (prefsSection_ == "Shortcuts") {
        ui::sectionHeader("Keyboard shortcuts");
        ImGui::TextDisabled("Click a shortcut, then press the new keys. Esc cancels, Backspace removes it.");
        if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthFixed, 200);
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 60);
            for (auto& a : keyActions()) {
                ImGui::PushID(a.id);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(a.label);
                ImGui::TableSetColumnIndex(1);
                bool waiting = rebindAction_ == a.id;
                std::string text = waiting ? "Press keys..." : chordName(prefs.chord(a.id));
                if (ImGui::Button(text.c_str(), {-1, 0}))
                    rebindAction_ = a.id;
                if (waiting) {
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                        rebindAction_.clear();
                    } else if (ImGui::IsKeyPressed(ImGuiKey_Backspace)) {
                        prefs.keys[a.id] = 0;
                        rebindAction_.clear();
                        changed = true;
                    } else {
                        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
                            ImGuiKey key = static_cast<ImGuiKey>(k);
                            if (ImGui::IsLRModKey(key) || key >= ImGuiKey_MouseLeft || !ImGui::IsKeyPressed(key, false))
                                continue;
                            ImGuiIO& io = ImGui::GetIO();
                            ImGuiKeyChord c = key | (io.KeyCtrl ? ImGuiMod_Ctrl : 0) | (io.KeyShift ? ImGuiMod_Shift : 0) |
                                              (io.KeyAlt ? ImGuiMod_Alt : 0);
                            prefs.keys[a.id] = c;
                            rebindAction_.clear();
                            changed = true;
                            break;
                        }
                    }
                }
                ImGui::TableSetColumnIndex(2);
                if (prefs.keys.count(a.id) && ImGui::SmallButton("Reset")) {
                    prefs.keys.erase(a.id);
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    } else if (prefsSection_ == "Profile") {
        ui::sectionHeader("You");
        ImGui::TextWrapped("Your name and color appear on the game cards you share. They stay on this computer.");
        label("Name");
        changed |= ImGui::InputTextWithHint("##name", "e.g. Sam", &prefs.profileName);
        label("Color");
        changed |= ImGui::ColorEdit3("##pcolor", &prefs.profileColor.r);
        ui::sectionHeader("Your progress");
        for (auto& [k, v] : prefs.counters) {
            if (k.rfind("snoozed", 0) == 0)
                continue;
            ImGui::BulletText("%s: %d", toLabel(k).c_str(), v);
        }
        if (prefs.counters.empty())
            ImGui::TextDisabled("Nothing yet. Go make something!");
    }
    ImGui::EndChild();
    if (restyle) {
        styleDirty_ = true;
        changed = true;
    }
    if (changed)
        prefs.save();
    ImGui::End();
}

// ---------------------------------------------------------------- Learn mode levels

void Editor::drawLevels() {
    if (levelUpTo_ && !ImGui::IsPopupOpen("Level up!"))
        ImGui::OpenPopup("Level up!");
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Level up!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(fonts.big);
        ImGui::Text("You're ready for %s!", levelName(levelUpTo_));
        ImGui::PopFont();
        ImGui::TextWrapped("You've been busy. Want to see more of Aven? These will appear:");
        ImGui::Spacing();
        for (int lvl = prefs.level + 1; lvl <= levelUpTo_; ++lvl)
            for (auto* f : featuresUnlockedAt(lvl)) {
                ImGui::Bullet();
                ImGui::PushFont(fonts.bold);
                ImGui::TextUnformatted(f->name);
                ImGui::PopFont();
                ImGui::SameLine();
                ImGui::TextDisabled("%s", f->description);
            }
        ImGui::Spacing();
        if (ImGui::Button("Show me the new features", {240, 0})) {
            prefs.level = levelUpTo_;
            levelUpTo_ = 0;
            resetLayout_ = true;
            prefs.save();
            notify(std::string("Welcome to the ") + levelName(prefs.level) + " level!");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not yet", {100, 0})) {
            prefs.counters["snoozed_level_" + std::to_string(levelUpTo_)] = 1;
            levelUpTo_ = 0;
            prefs.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!showLevels_)
        return;
    ImGui::SetNextWindowSize({820, 520}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Learn mode", &showLevels_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Aven hides advanced tools until you need them, so the screen isn't overwhelming on day one. "
                       "Pick any level at any time: nothing in your project changes, only what the editor shows.");
    ImGui::Spacing();
    float w = (ImGui::GetContentRegionAvail().x - 3 * 8) / 4;
    for (int lvl = 1; lvl <= 4; ++lvl) {
        if (lvl > 1)
            ImGui::SameLine(0, 8);
        ImGui::PushID(lvl);
        bool current = prefs.level == lvl;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, current ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##level", {w, -ImGui::GetFrameHeightWithSpacing() * 1.5f}, ImGuiChildFlags_Borders);
        ImGui::PushFont(fonts.bold);
        ImGui::Text("%d. %s", lvl, levelName(lvl));
        ImGui::PopFont();
        ImGui::PushTextWrapPos(0);
        ImGui::TextDisabled("%s", levelBlurb(lvl));
        ImGui::PopTextWrapPos();
        ImGui::Separator();
        for (auto* f : featuresUnlockedAt(lvl)) {
            ImGui::BulletText("%s", f->name);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", f->description);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        if (current)
            ImGui::TextColored({0.5f, 0.9f, 0.6f, 1}, "You are here");
        else if (ImGui::Button("Switch to this level", {w, 0})) {
            prefs.level = lvl;
            resetLayout_ = true;
            prefs.save();
        }
        ImGui::PopID();
    }
    std::string hint = nextLevelHint(prefs);
    if (!hint.empty())
        ImGui::TextDisabled("Next level: %s", hint.c_str());
    ImGui::End();
}

} // namespace aven::editor
