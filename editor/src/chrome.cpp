// The editor's frame: dock layouts, menus, toolbar, status bar, Preferences and Learn mode levels.

#include "avatars.h"
#include "editor.h"
#include "menu.h"

#include "rynax/core/fs.h"
#include "block_editor.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <cmath>

namespace rynax::editor {

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
                             (menu::native() ? 0 : ImGuiWindowFlags_MenuBar) | ImGuiWindowFlags_NoDocking;
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
    if (!menu::beginBar())
        return;
    auto key = [this](const char* action) {
        static std::string s;
        s = chordName(prefs.chord(action));
        return s.c_str();
    };
    Entity sel = selected();
    if (menu::begin("File")) {
        if (menu::item("New 2D Scene"))
            newScene(false);
        if (menu::item("New 3D Scene"))
            newScene(true);
        if (menu::begin("Open Scene")) {
            for (auto& s : projectFiles({".scene"}))
                if (menu::item(s.c_str(), nullptr, s == scenePath_))
                    openScene(s);
            menu::separator();
            if (menu::item("Quick Open...", key("open_scene"))) {
                showPalette_ = true;
                paletteSeed_ = ".scene";
            }
            menu::end();
        }
        if (menu::item("Save Scene", key("save")))
            saveScene();
        if (menu::item("Save Scene As...", key("save_as"), false, !playing_ && !editingPrefab()))
            openSaveAs();
        menu::separator();
        if ((unlocked(Feature::Export) || unlocked(Feature::Share)) && menu::item("Build & Share Game..."))
            showExport_ = true;
        if (menu::item("Export Project as .zip")) {
            std::string message;
            bool ok = exportProjectZip(message);
            notify(message, !ok);
            if (ok)
                openExternal(projectDir_.parent_path().string());
        }
        menu::tooltip("The whole game (scenes, scripts, pictures, sounds) in one file, to send to someone\n"
                              "or keep. Open it in Rynax's Open a game list, or drop it on Rynax.");
        if (unlocked(Feature::ProjectSettings) && menu::item("Project Settings..."))
            showSettings_ = true;
        if (!menu::native() && menu::item("Preferences...", key("preferences")))
            showPrefs_ = true; // on a Mac: Rynax > Settings
        menu::separator();
        if (menu::item("Project Hub (new or open project)")) {
            saveAllScripts();
            showHub_ = true;
        }
        if (menu::item("Close Tab or Window", key("close_tab")))
            closeFocusedWindow(true);
        if (!menu::native() && menu::item("Quit", key("quit")))
            requestQuit(); // on a Mac: Rynax > Quit Rynax
        menu::end();
    }
    if (menu::begin("Edit")) {
        if (menu::item("Undo", key("undo"), false, !undo_.empty() && !playing_))
            undo();
        if (menu::item("Redo", key("redo"), false, !redo_.empty() && !playing_))
            redo();
        menu::separator();
        if (menu::item("Cut", key("cut"), false, sel && !playing_))
            copySelection(true);
        if (menu::item("Copy", key("copy"), false, sel && !playing_))
            copySelection(false);
        if (menu::item("Paste", key("paste"), false, !playing_))
            pasteClipboard();
        if (menu::item("Paste in Place", key("paste_in_place"), false, !playing_))
            pasteClipboard(true);
        if (menu::item("Duplicate", key("duplicate"), false, sel && !playing_))
            duplicateSelection();
        if (menu::item("Delete", key("delete"), false, sel && !playing_)) {
            recordUndo("Delete");
            for (Entity e : selectedEntities())
                scene_->destroy(e);
            selection_.clear();
        }
        menu::separator();
        if (menu::item("Select All", key("select_all"), false, !playing_)) {
            selection_.clear();
            for (Entity e : scene().roots())
                addToSelection(e);
        }
        if (menu::item("Select None", nullptr, false, !selection_.empty()))
            selection_.clear();
        menu::separator();
        if (unlocked(Feature::History) && menu::item("Undo History"))
            showHistory_ = true;
        if (unlocked(Feature::Find) && menu::item("Find in Project...", key("find"))) {
            showFind_ = true;
            findFocus_ = true;
        }
        if (unlocked(Feature::CommandPalette) && menu::item("Command Palette...", key("command_palette")))
            showPalette_ = true;
        if (!menu::native()) { // on a Mac: Rynax > Settings
            menu::separator();
            if (menu::item("Preferences...", key("preferences")))
                showPrefs_ = true;
        }
        menu::end();
    }
    if (menu::begin("Create")) {
        menu::beginDisabled(playing_);
        if (unlocked(Feature::Recipes)) {
            if (menu::item("Game from a Recipe..."))
                openRecipes();
            menu::separator();
        }
        if (menu::begin("2D Shape")) {
            for (const char* k : {"Square", "Circle", "Triangle", "Rounded Square", "Diamond", "Star", "Heart", "Sprite"})
                if (menu::item(k))
                    createEntity(k);
            menu::end();
        }
        if (menu::begin("3D Shape")) {
            for (const char* k : {"Cube", "Sphere", "Plane", "Cylinder", "Capsule", "Cone", "Torus"})
                if (menu::item(k))
                    createEntity(k);
            menu::end();
        }
        if (unlocked(Feature::Lighting) && menu::begin("Light")) {
            for (const char* k : {"Sun", "Point Light", "Spot Light", "Environment"})
                if (menu::item(k))
                    createEntity(k);
            menu::end();
        }
        if (unlocked(Feature::UI) && menu::begin("UI")) {
            for (const char* k : {"UI Text", "UI Button", "UI Panel", "UI Image", "Score Text", "Health Bar", "Start Menu", "Pause Menu"})
                if (menu::item(k))
                    createEntity(k);
            menu::end();
        }
        if (unlocked(Feature::Tilemap) && menu::item("Tilemap"))
            createEntity("Tilemap");
        for (const char* k : {"Text", "Camera", "Player 3D", "Terrain"})
            if (menu::item(k))
                createEntity(k);
        if (unlocked(Feature::Particles) && menu::item("Particles"))
            createEntity("Particles");
        if (unlocked(Feature::Audio) && menu::item("Sound"))
            createEntity("Sound");
        if (menu::item("Empty Object"))
            createEntity("Entity");
        menu::endDisabled();
        menu::end();
    }
    if (menu::begin("Window")) {
        menu::item("Hierarchy", nullptr, &showHierarchy_);
        menu::item("Inspector", nullptr, &showInspector_);
        if (unlocked(Feature::Assets))
            menu::item("Assets", nullptr, &showAssets_);
        if (unlocked(Feature::Console))
            menu::item("Console", nullptr, &showConsole_);
        menu::item("Learn (tutorial)", nullptr, &showLearn_, !tutorial_.isNull());
        menu::item("Scripting Reference", nullptr, &showReference_);
        if (unlocked(Feature::Explain))
            menu::item("Explain", chordName(prefs.chord("explain")).c_str(), &showExplain_);
        if (unlocked(Feature::CodeLadder) && menu::item("Code Ladder", nullptr, showLadder_)) {
            showLadder_ = !showLadder_;
            if (showLadder_)
                openCodeLadderForSelection();
        }
        if (unlocked(Feature::BugReplay) && menu::item("Bug Replay", nullptr, showReplay_)) {
            if (showReplay_)
                showReplay_ = false;
            else
                openBugReplay();
        }
        if (unlocked(Feature::Doctor) && menu::item("Error Doctor", nullptr, showDoctor_)) {
            showDoctor_ = !showDoctor_;
            if (showDoctor_) {
                runCheckup();
                focusDoctor_ = true;
            }
        }
        menu::separator();
        if (unlocked(Feature::History))
            menu::item("Undo History", nullptr, &showHistory_);
        if (unlocked(Feature::Profiler))
            menu::item("Profiler", nullptr, &showProfiler_);
        if (unlocked(Feature::Lighting))
            menu::item("Lighting Presets", nullptr, &showLighting_);
        menu::separator();
        if (menu::begin("Layout")) {
            for (const char* l : kLayouts)
                if (menu::item(l, nullptr, prefs.layout == l))
                    pendingLayout_ = l;
            if (!prefs.savedLayouts.empty()) {
                menu::separator();
                for (auto& [name, ini] : prefs.savedLayouts)
                    if (menu::item(name.c_str(), nullptr, prefs.layout == name))
                        pendingLayout_ = name;
            }
            menu::separator();
            if (menu::item("Save current layout...")) {
                size_t size = 0;
                const char* ini = ImGui::SaveIniSettingsToMemory(&size);
                std::string name = "My layout " + std::to_string(prefs.savedLayouts.size() + 1);
                prefs.savedLayouts[name] = std::string(ini, size);
                prefs.layout = name;
                prefs.save();
                notify("Saved the layout as '" + name + "'. Rename it in Preferences.");
            }
            if (menu::item("Reset layout"))
                resetLayout_ = true;
            menu::end();
        }
        menu::separator();
        menu::item("Show Grid", nullptr, &showGrid_);
        menu::end();
    }
    if (menu::begin("Tools")) {
        if (unlocked(Feature::Recipes) && menu::item("Game from a Recipe..."))
            openRecipes();
        if (menu::item("Asset Library (sprites, models, textures)"))
            openAssetLibrary();
        if (unlocked(Feature::SoundMaker) && menu::item("Sound Maker"))
            openSoundMaker();
        if (unlocked(Feature::PixelEditor) && menu::item("Pixel Editor"))
            openPixelEditor("");
        if (unlocked(Feature::Animation) && menu::item("Sprite Sheet and Animation"))
            openSpriteSheet();
        if (unlocked(Feature::Tilemap) && menu::item("Tile Painter"))
            openTilePainter();
        if (unlocked(Feature::NativeCode) && menu::item("Native Code (C/C++)"))
            openNativeCode();
        if (unlocked(Feature::Code) && menu::begin("Editor tools")) {
            drawEditorToolsMenu();
            menu::end();
        }
        menu::separator();
        if (unlocked(Feature::Capture)) {
            if (menu::item("Take a Screenshot", chordName(prefs.chord("screenshot")).c_str()))
                takeScreenshot();
            if (menu::item(gifRecording_ ? "Stop Recording GIF" : "Record a GIF", chordName(prefs.chord("record_gif")).c_str()))
                toggleGifRecording();
            menu::separator();
        }
        if (unlocked(Feature::CodeLadder) && menu::item("Code Ladder"))
            openCodeLadderForSelection();
        if (unlocked(Feature::Doctor) && menu::item("Check My Game (Error Doctor)", chordName(prefs.chord("doctor")).c_str())) {
            runCheckup();
            showDoctor_ = focusDoctor_ = true;
        }
        if (unlocked(Feature::BugReplay) && menu::item("Bug Replay"))
            openBugReplay();
        menu::end();
    }
    if (menu::begin("Help")) {
        if (menu::item("Learn mode levels..."))
            showLevels_ = true;
        menu::item("Learn (tutorial)", nullptr, &showLearn_, !tutorial_.isNull());
        menu::item("Recipe Card", nullptr, &showRecipeCard_, !recipeCard_.parts.empty());
        menu::separator();
        if (menu::item("Contributor Quests..."))
            openQuests();
        menu::tooltip("Small, guided ways to help build Rynax itself");
        menu::item("Scripting Reference", nullptr, &showReference_);
        if (menu::item("Welcome Tour..."))
            openOnboarding();
        menu::tooltip("Meet Pip again: your name and picture, how much help you'd like, keys and looks");
        if (menu::item("Keyboard shortcuts...")) {
            showPrefs_ = true;
            prefsSection_ = "Shortcuts";
        }
        if (!menu::native()) { // on a Mac these are in the Rynax menu
            menu::separator();
            if (menu::item("Check for Updates..."))
                checkForUpdates(true);
            if (menu::item("About Rynax"))
                showAbout_ = true;
        }
        menu::end();
    }
    if (menu::native()) {
        menu::endBar();
        return;
    }
    // Project name on the right, after the "Update to ..." button when there's a new Rynax.
    std::string label = settings_.name + (dirty_ ? "  (unsaved)" : "");
    float right = ImGui::GetWindowWidth() - ImGui::CalcTextSize(label.c_str()).x - 20;
    if (updateAvailable()) {
        ImGui::SameLine(right - updateBadgeWidth() - ImGui::GetStyle().ItemSpacing.x * 2);
        drawUpdateBadge();
    }
    ImGui::SameLine(right);
    ImGui::TextDisabled("%s", label.c_str());
    ImGui::EndMenuBar();
}

// The About window, and the Rynax menu's commands on a Mac. Every frame, also on the start screen.
void Editor::drawAppDialogs() {
    if (menu::native()) {
        if (!hasProject() || showHub_) { // the start screen has no menus of its own
            if (menu::beginBar())
                menu::endBar();
        }
        for (std::string c; !(c = menu::appCommand()).empty();) {
            if (c == "about")
                showAbout_ = true;
            else if (c == "settings")
                showPrefs_ = true;
            else if (c == "updates")
                checkForUpdates(true);
        }
    }
    if (showAbout_) {
        ImGui::OpenPopup("About Rynax");
        showAbout_ = false;
    }
    if (ImGui::BeginPopupModal("About Rynax", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(fonts.big);
        ImGui::Text("Rynax %s", RYNAX_VERSION);
        ImGui::PopFont();
        ImGui::Text("A beginner-friendly 2D and 3D game engine.");
        ImGui::TextDisabled("Blocks -> EasyScript -> C/C++ -> any engine you like");
        ImGui::Spacing();
        ImGui::TextDisabled("Renderer: %s", device_.description().c_str());
        ImGui::TextDisabled("Preferences: %s", fs::toUtf8(fs::userDataDir("Rynax Editor") / "preferences.json").c_str());
        if (ImGui::Button("Close", {ui::px(120), 0}))
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
    // Save, undo and redo, for the mouse (they're also Ctrl+S, Ctrl+Z and Ctrl+Y, or the user's keys).
    {
        static std::string saveTip, undoTip, redoTip;
        saveTip = "Save the scene and scripts (" + chordName(prefs.chord("save")) + ")";
        undoTip = undo_.empty() ? std::string("Nothing to undo") : "Undo: " + undo_.back().label + " (" + chordName(prefs.chord("undo")) + ")";
        redoTip = redo_.empty() ? std::string("Nothing to redo") : "Redo: " + redo_.back().label + " (" + chordName(prefs.chord("redo")) + ")";
        ImGui::BeginDisabled(playing_);
        if (ui::iconButton("save", ui::Save, saveTip.c_str(), false, b)) {
            saveScene();
            saveAllScripts();
        }
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        ImGui::BeginDisabled(undo_.empty());
        if (ui::iconButton("undo", ui::Undo, undoTip.c_str(), false, b))
            undo();
        ImGui::EndDisabled();
        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x * 0.5f);
        ImGui::BeginDisabled(redo_.empty());
        if (ui::iconButton("redo", ui::Redo, redoTip.c_str(), false, b))
            redo();
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        // A thin divider before the tools.
        ImGui::SameLine();
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddLine({p.x, p.y + b * 0.15f}, {p.x, p.y + b * 0.85f}, ImGui::GetColorU32(ImGuiCol_Separator), 1);
        ImGui::Dummy({ImGui::GetStyle().ItemSpacing.x * 0.5f, b});
        ImGui::SameLine();
    }
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
    if (ui::iconButton("snap", ui::Magnet, keyText("Snap to grid (or hold Ctrl while dragging)").c_str(), snap_, b))
        snap_ = !snap_;
    if (ImGui::BeginPopupContextItem("snap_settings")) {
        ImGui::TextDisabled("Snap steps");
        ImGui::SetNextItemWidth(ui::px(120));
        ImGui::DragFloat("Move", &prefs.moveSnap, 0.05f, 0.05f, 10.0f, "%.2f");
        ImGui::SetNextItemWidth(ui::px(120));
        ImGui::DragFloat("Rotate", &prefs.rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        ImGui::SetNextItemWidth(ui::px(120));
        ImGui::DragFloat("Scale", &prefs.scaleSnap, 0.05f, 0.05f, 2.0f, "%.2f");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ui::iconButton("grid", ui::Grid, "Show grid", showGrid_, b))
        showGrid_ = !showGrid_;
    ImGui::SameLine();
    // As wide as its text needs, plus the arrow (a fixed width cut off "3D" with Windows display scaling).
    ImGui::SetNextItemWidth(ImGui::CalcTextSize("3D").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetFrameHeight() + ui::px(6));
    int view = view3D_ ? 1 : 0;
    const char* views[] = {"2D", "3D"};
    if (ImGui::Combo("##view", &view, views, 2))
        view3D_ = view == 1;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Switch between the 2D and 3D scene view");

    // Play controls in the middle.
    float center = ImGui::GetWindowWidth() * 0.5f;
    ImGui::SameLine(center - b * 1.6f);
    if (ui::iconButton("play", playing_ ? ui::Stop : ui::Play, ((playing_ ? "Stop (" : "Play (") + chordName(prefs.chord("play")) + ")").c_str(), playing_, b))
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
    if (menu::native() && updateAvailable()) { // no menu bar in the window on a Mac
        drawUpdateBadge(false);
        ImGui::SameLine();
    }
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
    // (while resting, the editor draws a few frames a second on purpose: that isn't a slow editor)
    std::string right = resting_       ? std::string("Resting")
                        : prefs.showFps ? std::to_string(static_cast<int>(std::round(ImGui::GetIO().Framerate))) + " FPS"
                                        : std::string();
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
    if (!rebindAction_.empty() || showOnboarding_)
        return; // (Preferences is waiting for the keys of a shortcut, or the welcome tour is up)
    if (shortcut("quit"))
        requestQuit();
    if (shortcut("close_tab"))
        closeFocusedWindow();
    if (shortcut("play"))
        playing_ ? stop() : play();
    if (shortcut("stop") && playing_)
        stop();
    // The Pixel Editor has its own undo, save and tool keys.
    if (pixelEditorFocused_)
        return;
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
    if (shortcut("build_native") && unlocked(Feature::NativeCode) && fs::exists(projectDir_ / "native" / "CMakeLists.txt"))
        buildNativeModule();
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
    if (shortcut("open_scene")) {
        showPalette_ = true;
        paletteSeed_ = ".scene";
    }
    if (shortcut("save_as"))
        openSaveAs();
    if (ImGui::GetIO().WantTextInput || keyboardClaimed())
        return;
    if (shortcut("toggle_2d3d"))
        view3D_ = !view3D_;
    // The editor's size, like zooming a web page.
    float scale = prefs.uiScale;
    if (shortcut("ui_bigger"))
        scale += 0.125f;
    if (shortcut("ui_smaller"))
        scale -= 0.125f;
    if (shortcut("ui_reset"))
        scale = 1.0f;
    if (scale != prefs.uiScale) {
        prefs.uiScale = std::clamp(scale, 0.75f, 1.75f);
        styleDirty_ = true;
        prefs.save();
    }
    if (playing_)
        return;
    if (shortcut("new_scene"))
        newScene(view3D_);
    if (shortcut("undo"))
        undo();
    if (shortcut("redo"))
        redo();
    bool sceneFocus = viewportFocused_ || hierarchyFocused_;
    auto sel = selectedEntities();
    if (!sel.empty() && shortcut("duplicate"))
        duplicateSelection();
    if (!sel.empty() && shortcut("group") && sceneFocus)
        groupSelection();
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
    if (sceneFocus && shortcut("paste_in_place"))
        pasteClipboard(true);
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
    ImGui::SetNextWindowSize(ui::fitted({760, 560}), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Preferences", &showPrefs_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    const char* sections[] = {"Look", "Code", "Scene view", "Behavior", "Learning", "Shortcuts", "Profile"};
    ImGui::BeginChild("##sections", {ui::px(150), 0}, ImGuiChildFlags_Borders);
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
        ImGui::SameLine(ui::px(190));
        ImGui::SetNextItemWidth(-1);
    };

    if (prefsSection_ == "Look") {
        ui::sectionHeader("Theme");
        float w = ImGui::GetContentRegionAvail().x, k = ui::px(1);
        // A card: the theme in miniature (panel, field, accent) over its name.
        float cardW = 140 * k, cardH = 50 * k + ImGui::GetFontSize() + 10 * k;
        int perRow = std::max(1, static_cast<int>((w + ImGui::GetStyle().ItemSpacing.x) / (cardW + ImGui::GetStyle().ItemSpacing.x)));
        int i = 0;
        for (auto& t : themePresets()) {
            if (i++ % perRow)
                ImGui::SameLine();
            ImGui::PushID(t.name);
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (ImGui::InvisibleButton("##theme", {cardW, cardH})) {
                prefs.theme = t.name;
                prefs.customAccent = false;
                restyle = true;
            }
            bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto col = [](uint32_t c) { return IM_COL32((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, 255); };
            bool current = prefs.theme == t.name;
            dl->AddRectFilled(p, {p.x + cardW, p.y + cardH}, col(t.background), 6 * k);
            dl->AddRectFilled({p.x + 8 * k, p.y + 8 * k}, {p.x + 50 * k, p.y + 42 * k}, col(t.panel), 4 * k);
            dl->AddRectFilled({p.x + 56 * k, p.y + 8 * k}, {p.x + cardW - 8 * k, p.y + 20 * k}, col(t.frame), 3 * k);
            dl->AddRectFilled({p.x + 56 * k, p.y + 26 * k}, {p.x + cardW - 30 * k, p.y + 38 * k}, col(t.accent), 3 * k);
            dl->AddText({p.x + 8 * k, p.y + 48 * k}, col(t.text), ui::ellipsize(t.name, cardW - 16 * k).c_str());
            dl->AddRect(p, {p.x + cardW, p.y + cardH},
                        current ? ImGui::GetColorU32(ImGuiCol_CheckMark) : hovered ? col(t.accent) : col(t.border), 6 * k, 0,
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
                if (ImGui::ColorButton("##sw", {c.r, c.g, c.b, 1}, ImGuiColorEditFlags_NoTooltip, {ui::px(20), ui::px(20)})) {
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
        label("Colorblind-friendly axes", "Orange, sky blue and pink for X, Y and Z (move arrows, X/Y/Z fields, grid lines)\n"
                                          "instead of red, green and blue.");
        restyle |= ImGui::Checkbox("##colorblind", &prefs.colorblindSafe);
        label("Calm mode (less motion)", "No bouncing, wiggling text or confetti: Pip keeps still, and so does everything else.");
        changed |= ImGui::Checkbox("##calm", &prefs.reduceMotion);
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
            float cardH = ui::px(58);
            if (ImGui::InvisibleButton("##pal", {w, cardH})) {
                prefs.codeTheme = p.name;
                CodeEditor::palette = p;
                changed = true;
            }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(pos, {pos.x + w, pos.y + cardH}, p.background, 6);
            if (prefs.codeTheme == p.name)
                dl->AddRect(pos, {pos.x + w, pos.y + cardH}, ImGui::GetColorU32(ImGuiCol_CheckMark), 6, 0, 2.5f);
            ImFont* f = fonts.code;
            float fs = f ? f->FontSize : ImGui::GetFontSize();
            float x = pos.x + ui::px(12), y = pos.y + ui::px(8);
            auto word = [&](const char* text, ImU32 c) {
                dl->AddText(f, fs, {x, y}, c, text);
                x += ImGui::CalcTextSize(text).x * (f ? fs / ImGui::GetFontSize() : 1) + 2;
            };
            word("def ", p.keyword);
            word("on_update", p.function);
            word("(dt):  ", p.text);
            word("# ", p.comment);
            word(p.name.c_str(), p.comment);
            x = pos.x + ui::px(40);
            y += fs + ui::px(6);
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
        ui::sectionHeader("Typing");
        label("Close brackets and quotes", "Typing ( [ { or a quote also types its partner after the cursor.");
        changed |= ImGui::Checkbox("##autoclose", &prefs.autoClose);
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
        label("Resolution on sharp screens",
              "On Retina and other high-density screens, the scene and game view can draw every pixel\n"
              "(sharpest, but the most work for the graphics chip) or fewer, which keeps the editor smooth.\n"
              "The finished game isn't affected. On other screens, all three look the same.");
        {
            const char* choices[] = {"Sharpest", "Balanced", "Fastest"};
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::Combo("##viewres", &prefs.viewResolution, choices, 3);
        }
        label("Graphics quality while editing",
              "Shadows, lights and effects in the scene view: Low is lightest on your computer, Ultra has the\n"
              "sharpest shadows. When you press Play, the game uses its own quality (Project Settings > Game),\n"
              "so you see what players will.");
        {
            const char* levels[] = {"Low", "Medium", "High", "Ultra"};
            ImGui::SetNextItemWidth(-1);
            changed |= ImGui::Combo("##viewquality", &prefs.viewportQuality, levels, 4);
        }
        ui::sectionHeader("Camera and gizmos");
        label("3D fly speed");
        changed |= ImGui::SliderFloat("##fly", &prefs.flySpeed, 1.0f, 40.0f, "%.0f");
        label("Zoom speed", "How far each turn of the mouse wheel (or trackpad scroll) zooms.");
        changed |= ImGui::SliderFloat("##zoomspeed", &prefs.zoomSpeed, 0.25f, 4.0f, "%.2fx");
        label("Reverse zoom direction", "Scroll up to zoom out, like some other programs.");
        changed |= ImGui::Checkbox("##invertzoom", &prefs.invertZoom);
        label("Move snap step");
        changed |= ImGui::DragFloat("##ms", &prefs.moveSnap, 0.05f, 0.05f, 10.0f, "%.2f");
        label("Rotate snap step");
        changed |= ImGui::DragFloat("##rs", &prefs.rotateSnap, 1.0f, 1.0f, 90.0f, "%.0f deg");
        label("Scale snap step");
        changed |= ImGui::DragFloat("##ss", &prefs.scaleSnap, 0.05f, 0.05f, 2.0f, "%.2f");
        label("Gizmo uses local axes");
        changed |= ImGui::Checkbox("##local", &prefs.gizmoLocal);
    } else if (prefsSection_ == "Behavior") {
        ui::sectionHeader("Starting Rynax");
        label("Say hello on the start screen", "A greeting with your name and picture above the templates.");
        changed |= ImGui::Checkbox("##greeting", &prefs.showGreeting);
        label("Open my last game", "Start right in the game you had open last time, instead of the start screen.");
        changed |= ImGui::Checkbox("##openlast", &prefs.openLastProject);
        ui::sectionHeader("Fun");
        label("Little sounds", "Soft clicks and cheers in the welcome tour, and a cheer when you level up.");
        changed |= ImGui::Checkbox("##uisounds", &prefs.uiSounds);
        label("Show frames per second", "In the bottom right corner: how smoothly the editor is drawing.");
        changed |= ImGui::Checkbox("##fps", &prefs.showFps);
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
        ui::sectionHeader("Energy");
        label("Rest when nothing's happening",
              "When no game is playing and you're not touching the mouse or keyboard, the editor draws a\n"
              "few pictures a second instead of 60 or more: cooler, quieter and longer on battery. It\n"
              "wakes up the moment you move the mouse.");
        changed |= ImGui::Checkbox("##energy", &prefs.saveEnergy);
        ui::sectionHeader("Editing");
        label("Ask before deleting");
        changed |= ImGui::Checkbox("##confirmdel", &prefs.confirmDelete);
        label("Paste in the same place", "Pasted objects land exactly where the copied ones are, instead of a little to the side.\n"
                                         "(Edit > Paste in Place does this once, whatever this is set to.)");
        changed |= ImGui::Checkbox("##pasteinplace", &prefs.pasteInPlace);
        ui::sectionHeader("External code editor");
        ImGui::TextWrapped("Use another editor (VS Code, Sublime, Vim...) for scripts. {file} and {line} are filled in. "
                           "Saved changes reload while the game runs.");
        label("Command");
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputTextWithHint("##exteditor", "code -g {file}:{line}", &prefs.externalEditor);
        label("Open scripts there", "Double-clicking a script, or an error in the Console, opens it in that editor. "
                                    "Blocks still open in Rynax.");
        changed |= ImGui::Checkbox("##useext", &prefs.useExternalEditor);
        ImGui::SameLine();
        ImGui::BeginDisabled(prefs.externalEditor.empty() || !hasProject());
        if (ImGui::SmallButton("Open the project folder there")) {
            std::string cmd = prefs.externalEditor;
            size_t at = cmd.find(' ');
            launchCommand((at == std::string::npos ? cmd : cmd.substr(0, at)) + " \"" + projectDir_.string() + "\"");
        }
        ImGui::EndDisabled();
        ui::sectionHeader("Updates");
        label("Check for updates", "When Rynax starts, it asks GitHub whether there's a new version, and shows an\n"
                                   "\"Update to ...\" button if there is. Nothing is downloaded until you say so.");
        changed |= ImGui::Checkbox("##checkupdates", &prefs.checkUpdates);
        ImGui::SameLine();
        if (ImGui::SmallButton("Check now"))
            checkForUpdates(true);
        label("Include beta versions", "Try new features before they're finished. Betas can have more bugs.");
        changed |= ImGui::Checkbox("##betaupdates", &prefs.betaUpdates);
        if (std::string previous = previousVersion(); !previous.empty()) {
            label("Go back", "The version the last update replaced is kept until the next update. Going back\n"
                             "restarts Rynax with it (your projects aren't changed).");
            if (ImGui::SmallButton(("Go back to " + productName(previous) + " " + previous + "##rollback").c_str())) {
                rollbackPending_ = true;
                saveAndRestart();
            }
        }
        if (!prefs.skippedUpdate.empty()) {
            label("Skipped version");
            ImGui::TextDisabled("%s", prefs.skippedUpdate.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Don't skip it")) {
                prefs.skippedUpdate.clear();
                changed = true;
            }
        }
    } else if (prefsSection_ == "Learning") {
        ui::sectionHeader("Learn mode");
        ImGui::TextWrapped("The editor starts simple and shows more as you learn. You're at the %s level.", levelName(prefs.level));
        for (int lvl = 1; lvl <= 4; ++lvl) {
            ImGui::PushID(lvl);
            if (ImGui::RadioButton(levelName(lvl), prefs.level == lvl)) {
                prefs.level = lvl;
                changed = true;
            }
            ImGui::SameLine(ui::px(120));
            ImGui::TextDisabled("%s", levelBlurb(lvl));
            ImGui::PopID();
        }
        label("Level up automatically", "When you've practiced enough, Rynax offers to show more features.");
        changed |= ImGui::Checkbox("##autolevel", &prefs.autoLevelUp);
        ui::sectionHeader("Helpers");
        label("Beginner helpers", "The Ask Rynax box in the Inspector, Doctor buttons in the Console and tips in empty panels. "
                                  "Turn them off for a quieter editor.");
        changed |= ImGui::Checkbox("##helpers", &prefs.beginnerHelpers);
        ui::sectionHeader("Tips");
        if (ImGui::Button("Show all tips again")) {
            prefs.seenTips.clear();
            changed = true;
        }
    } else if (prefsSection_ == "Shortcuts") {
        changed |= drawShortcutPrefs();
    } else if (prefsSection_ == "Profile") {
        ui::sectionHeader("You");
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float r = ui::px(34);
            drawProfileAvatar(ImGui::GetWindowDrawList(), {p.x + r, p.y + r}, r, static_cast<float>(ImGui::GetTime()));
            ImGui::SetCursorScreenPos({p.x + r * 2 + ui::px(14), p.y + ui::px(6)});
            ImGui::BeginGroup();
            ImGui::TextWrapped("Your name and color appear on the game cards you share. Everything here stays on this computer.");
            if (ImGui::Button("Take the welcome tour again")) {
                showPrefs_ = false;
                openOnboarding();
            }
            ImGui::EndGroup();
            ImGui::SetCursorScreenPos({p.x, std::max(ImGui::GetCursorScreenPos().y, p.y + r * 2 + ui::px(8))});
        }
        label("Name");
        changed |= ImGui::InputTextWithHint("##name", "e.g. Sam", &prefs.profileName);
        label("Picture");
        {
            std::string current = prefs.avatar == "picture" ? std::string("My own picture") : prefs.avatar;
            for (auto& c : critters())
                if (prefs.avatar == c.id)
                    current = c.name;
            if (ImGui::BeginCombo("##avatar", current.c_str())) {
                for (auto& c : critters())
                    if (ImGui::Selectable(c.name, prefs.avatar == c.id)) {
                        prefs.avatar = c.id;
                        changed = true;
                    }
                if (ImGui::Selectable("My own picture...", prefs.avatar == "picture"))
                    openOnboarding(0); // (the picture chooser lives in the tour's first step)
                ImGui::EndCombo();
            }
        }
        label("Color");
        changed |= ImGui::ColorEdit3("##pcolor", &prefs.profileColor.r);
        label("Pronouns", "Optional. Shown only here.");
        changed |= ImGui::InputTextWithHint("##pronouns", "e.g. she/her", &prefs.pronouns);
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
    if (levelUpTo_ && !ImGui::IsPopupOpen("Level up!")) {
        ImGui::OpenPopup("Level up!");
        uiSound("powerup");
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, {0.5f, 0.5f});
    if (ImGui::BeginPopupModal("Level up!", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::PushFont(fonts.big);
        ImGui::Text("You're ready for %s!", levelName(levelUpTo_));
        ImGui::PopFont();
        ImGui::TextWrapped("You've been busy. Want to see more of Rynax? These will appear:");
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
        if (ImGui::Button("Show me the new features", {ui::px(240), 0})) {
            prefs.level = levelUpTo_;
            levelUpTo_ = 0;
            resetLayout_ = true;
            prefs.save();
            notify(std::string("Welcome to the ") + levelName(prefs.level) + " level!");
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Not yet", {ui::px(100), 0})) {
            prefs.counters["snoozed_level_" + std::to_string(levelUpTo_)] = 1;
            levelUpTo_ = 0;
            prefs.save();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!showLevels_)
        return;
    ImGui::SetNextWindowSize(ui::fitted({860, 580}), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver, {0.5f, 0.5f});
    if (!ImGui::Begin("Learn mode", &showLevels_, ImGuiWindowFlags_NoDocking)) {
        ImGui::End();
        return;
    }
    ImGui::TextWrapped("Rynax hides advanced tools until you need them, so the screen isn't overwhelming on day one. "
                       "Pick any level at any time: nothing in your project changes, only what the editor shows.");
    ImGui::Spacing();
    float w = (ImGui::GetContentRegionAvail().x - 3 * 8) / 4;
    // Room below the cards for the buttons and the hint line.
    float cardH = std::max(160.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.2f);
    for (int lvl = 1; lvl <= 4; ++lvl) {
        if (lvl > 1)
            ImGui::SameLine(0, 8);
        ImGui::BeginGroup(); // a card and its button form one column
        ImGui::PushID(lvl);
        bool current = prefs.level == lvl;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, current ? ImGui::GetStyleColorVec4(ImGuiCol_Header) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("##level", {w, cardH}, ImGuiChildFlags_Borders);
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
        ImGui::EndGroup();
    }
    std::string hint = nextLevelHint(prefs);
    if (!hint.empty())
        ImGui::TextDisabled("Next level: %s", hint.c_str());
    ImGui::End();
}

} // namespace rynax::editor
