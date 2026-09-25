#pragma once

#include "aven/assets/assets.h"
#include "aven/core/log.h"
#include "aven/platform/window.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/game.h"
#include "aven/runtime/project.h"
#include "aven/scene/reflection.h"
#include "aven/scene/scene.h"

#include "code_editor.h"
#include "learn_mode.h"
#include "prefs.h"
#include "script_facts.h"

#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <unordered_set>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>


namespace aven::editor {

namespace stdfs = std::filesystem;

class CodeEditor;
class BlockEditor;

struct TemplateInfo {
    std::string id;
    std::string name;
    std::string description;
    std::string style; // "Blocks", "EasyScript", "Blocks + EasyScript"
    std::string difficulty;
    bool is3D = false;
    Color color;
    stdfs::path folder;
};

struct ConsoleLine {
    LogLevel level;
    std::string text;
    std::string file;
    int line = 0;
    int count = 1;
};

struct Fonts {
    ImFont* ui = nullptr;
    ImFont* bold = nullptr;
    ImFont* big = nullptr;
    ImFont* code = nullptr;
};

struct EditorOptions {
    stdfs::path project;
    std::string screenshot;
    int frames = 30;
    std::string openPanel; // for automated screenshots: "blocks", "script", "hub"
    std::string openFile;
    std::string select;
    bool play = false;
    stdfs::path newProject;    // --new: create a project here from --template, then open it
    std::string templateId;
    stdfs::path exportTo;      // --export: export the project to this folder and quit
    int level = 0;             // --level 1-4: Learn mode level (0 = saved preference)
    std::string theme;         // --theme name
};

// The Aven editor: project hub, scene editing, scripting, block coding, play mode and export.
class Editor {
public:
    Editor(Window& window, rhi::Device& device);
    ~Editor();

    bool init(const EditorOptions& options);
    // Called before ImGui::NewFrame(): rebuilds fonts and style when preferences changed.
    void beginFrame();
    void frame(float dt);
    void setDpiScale(float scale) { dpiScale_ = scale; styleDirty_ = true; }
    bool wantsQuit() const { return quit_; }
    void requestQuit();
    SceneRenderer& renderer() { return renderer_; }

    // --- projects
    bool openProject(const stdfs::path& dir);
    bool createProject(const stdfs::path& dir, const std::string& name, const TemplateInfo* tmpl);
    void closeProject();
    bool hasProject() const { return !projectDir_.empty(); }
    const stdfs::path& projectDir() const { return projectDir_; }
    std::vector<TemplateInfo> templates() const;

    // --- scenes
    bool openScene(const std::string& path);
    bool saveScene();
    void newScene(bool is3D);
    Scene& scene() { return playing() ? game_->scene() : *scene_; }
    Scene& editScene() { return *scene_; }
    bool is3D() const { return view3D_; }

    // --- selection and undo
    Entity selected(); // the main selected object (shown in the Inspector)
    void select(Entity e); // selects only `e` (or nothing)
    void addToSelection(Entity e);
    void toggleSelection(Entity e);
    bool isSelected(Entity e);
    std::vector<Entity> selectedEntities();
    void recordUndo(const std::string& label);
    void undo();
    void redo();
    void markDirty() { dirty_ = true; }

    // --- play mode
    void play();
    void stop();
    bool playing() const { return playing_; }

    // --- scripts
    void openScript(const std::string& path, int line = 0);
    void openBlocks(const std::string& path);
    std::string newScriptFile(const std::string& baseName, bool blocks);
    void attachScript(Entity e, const std::string& path);

    // --- creating things
    Entity createEntity(const std::string& kind, Entity parent = {});
    Entity instantiatePrefab(const std::string& path, Vec3 position);
    void savePrefab(Entity e);
    void copySelection(bool cut);
    // A setting changed while playing, which can be copied back into the edited scene.
    struct LiveChange {
        UUID id;
        std::string component, field;
        Json value;
    };
    void noteLiveChange(Entity e, const std::string& key);

    // --- Ask Aven (assistant.cpp)
    struct AssistantProposal {
        std::string text;
        std::function<void(Editor&)> apply;
        bool enabled = true;
    };
    struct AssistantResult {
        std::string request;
        std::vector<AssistantProposal> proposals;
        std::vector<std::string> understood, unknown, notes;
    };
    void askAven(const std::string& request);
    const AssistantResult& assistantResult() const { return assistant_; }
    // Reflection helpers used by the assistant, recipes and explanations.
    Json fieldValue(Entity e, const std::string& component, const std::string& field);
    void setFieldValue(Entity e, const std::string& component, const std::string& field, const Json& value);
    bool hasComponent(Entity e, const std::string& component);
    void addComponentByName(Entity e, const std::string& component);
    void ensureRequirements(Entity e, const std::string& component);
    std::vector<std::string> scriptVariables(Entity e);
    void ensureBulletPrefab();

    // --- Explain (explain.cpp)
    struct ExplainSection {
        std::string title;
        std::vector<std::string> lines;
    };
    std::vector<ExplainSection> explainEntity(Entity e);
    std::vector<ExplainSection> explainGame();
    ScriptFacts& factsFor(const std::string& scriptPath); // cached per file

    // --- Error doctor (doctor.cpp)
    struct DoctorFix {
        std::string label;
        std::function<void(Editor&)> apply;
    };
    struct Diagnosis {
        std::string title, explanation, original; // original = the engine's message, if any
        std::string file;                         // script involved, if any
        int line = 0;
        std::string entity;                       // object involved, if any
        std::vector<DoctorFix> fixes;
        bool warning = false;
    };
    // Explains one error message; `file`/`line` say where it happened.
    std::vector<Diagnosis> diagnose(const std::string& message, const std::string& file, int line);
    // Looks through the whole scene for mistakes that don't show up as errors.
    std::vector<Diagnosis> checkup();
    void openDoctorFor(const std::string& message, const std::string& file, int line);
    void runCheckup();
    bool rewriteScriptLine(const std::string& file, int line, const std::function<std::string(const std::string&)>& change);
    bool replaceWordInLine(const std::string& file, int line, const std::string& from, const std::string& to);
    void addPhysics2D(Entity e, bool trigger);

    // --- Game recipes (recipes.cpp)
    struct RecipeGoal {
        enum { CollectAll, ReachExit, Survive, ReachScore };
    };
    struct RecipeChoices {
        int recipe = 0; // platformer, adventure, shooter, dodge, clicker
        std::string heroShape = "Circle";
        Color heroColor{0.22f, 0.55f, 0.97f, 1};
        std::string heroImage;
        int collect = 1;        // nothing, coins, gems, stars
        unsigned dangers = 3;   // spikes 1, walkers 2, chasers 4, falling rocks 8
        int goal = RecipeGoal::ReachExit;
        int difficulty = 1;     // easy, normal, hard
        std::string name = "Platformer";
        bool makeStartScene = false;
    };
    struct RecipePart {
        std::string object, what, tryThis;
    };
    struct RecipeCard {
        std::string title, summary, scene;
        std::vector<RecipePart> parts;
    };
    // Builds a small working game from behaviors; returns the new scene's path.
    std::string cookRecipe(const RecipeChoices& choices);
    void loadRecipeCard(const std::string& scenePath);
    void openRecipes() { showRecipes_ = focusRecipes_ = true; }

    // --- Code ladder (code_ladder.cpp)
    void openCodeLadder(const std::string& title, const std::string& easyScript, const std::string& path);
    void openCodeLadderForScript(const std::string& path);
    void openCodeLadderForBehavior(Entity e, const std::string& component);
    void openCodeLadderForSelection(); // the selected object's script, or its first behavior
    bool behaviorToScript(Entity e, const std::string& component); // replaces a behavior with the same logic as a script
    std::string blocksToScript(const std::string& blocksPath);    // makes an EasyScript copy of a blocks file
    std::vector<LiveChange> collectLiveChanges();
    void applyLiveChanges(const std::vector<LiveChange>& changes);
    void drawLiveChangesBar(ImVec2 pos, ImVec2 size);
    // Prefab edit mode: a prefab opens like a small scene of its own.
    void openPrefab(const std::string& path);
    void closePrefab(bool save);
    bool editingPrefab() const { return !prefabPath_.empty(); }
    void applyToPrefab(Entity instance);
    void revertToPrefab(Entity instance);
    // Tools can add entries to the command palette.
    std::vector<std::pair<std::string, std::function<void()>>> extraCommands_;
    void drawKeepChangesDialog();
    void pasteClipboard();

    Fonts fonts;
    Prefs prefs;
    bool advanced() const { return prefs.level >= 4 || settings_.advancedMode; }
    bool unlocked(Feature f) const { return prefs.level >= featureLevel(f); }
    // Counts progress toward the next Learn mode level ("plays", "objects_added"...).
    void milestone(const std::string& key, int amount = 1);
    bool shortcut(const char* action);
    void notify(const std::string& message, bool error = false);
    std::vector<std::string> projectFiles(const std::vector<std::string>& extensions) const;
    Assets& assets() { return assets_; }
    ProjectSettings& settings() { return settings_; }

private:
    Window& window_;
    rhi::Device& device_;
    Assets assets_;
    SceneRenderer renderer_;
    Input gameInput_;
    std::unique_ptr<Game> game_;
    ProjectSettings settings_;
    stdfs::path projectDir_;
    std::unique_ptr<Scene> scene_;
    std::string scenePath_;
    bool dirty_ = false;
    bool playing_ = false;
    bool paused_ = false;
    bool stepOnce_ = false;
    bool quit_ = false;
    bool confirmQuit_ = false;
    std::vector<UUID> selection_; // last = main selection
    EditorOptions options_;
    int frameCount_ = 0;

    struct Snapshot {
        std::string label;
        Json scene;
        std::vector<UUID> selection;
    };
    std::vector<Snapshot> undo_, redo_;
    Json cachedSnapshot_;
    bool snapshotValid_ = false;
    bool editInProgress_ = false;
    bool gizmoWasUsing_ = false;
    std::vector<CodeEditor::Completion> completions_;
    std::unordered_set<std::string> apiWords_;
    struct ApiEntry {
        std::string group, name, signature, help;
    };
    std::vector<ApiEntry> api_;
    char referenceFilter_[64] = {};
    std::vector<std::string> assetFiles_;
    float assetScanTimer_ = 0;
    std::string selectedAsset_;
    std::string assetSearch_;
    float assetCell_ = 92;
    bool showInfo_ = true, showWarnings_ = true, showErrors_ = true;
    std::string consoleFilter_;
    float dpiScale_ = 1.0f;
    bool styleDirty_ = true;
    float autosaveTimer_ = 0;
    int levelUpTo_ = 0; // a level-up offer waiting to be shown
    bool showPrefs_ = false;
    bool showLevels_ = false;
    std::string pendingLayout_; // applied at the start of the next frame
    std::string prefsSection_ = "Look";
    std::string rebindAction_;

    // Editor camera
    bool view3D_ = false;
    Vec3 cam2D_{0, 0, 10};
    float camZoom_ = 6.0f;
    Vec3 cam3D_{6, 5, 9};
    float camYaw_ = 35.0f, camPitch_ = -22.0f;
    int gizmoOp_ = 0; // 0 move, 1 rotate, 2 scale
    bool snap_ = false;
    bool showGrid_ = true;
    Vec2 viewportPos_, viewportSize_{1, 1};
    bool viewportHovered_ = false, viewportFocused_ = false, hierarchyFocused_ = false;
    bool focusViewport_ = false;
    CameraView editorCamera() const;
    CameraView viewportCamera() const;
    Entity pickEntity(Scene& s, const CameraView& cam, Vec2 local);
    void drawGizmo(const CameraView& cam, ImVec2 pos, ImVec2 size, bool& usingGizmo, bool& overGizmo);
    void drawStats(ImVec2 pos);
    void focusSelected();

    // Panels
    bool showHub_ = true;
    bool showSettings_ = false;
    bool showLearn_ = false;
    bool showReference_ = false;
    bool showExport_ = false;
    bool showAbout_ = false;
    bool resetLayout_ = true;
    bool showHierarchy_ = true, showInspector_ = true, showAssets_ = true, showConsole_ = true;
    std::string hierarchyFilter_;
    std::vector<UUID> hierarchyOrder_, lastHierarchyOrder_; // visible rows, for Shift-click ranges
    UUID hierarchyAnchor_;
    UUID renaming_;
    std::string renameEntityBuffer_;
    bool renameFocus_ = false;
    Json clipboard_; // copied objects (also placed on the system clipboard as text)
    Json componentClipboard_;
    std::string componentClipboardType_;
    // Play-and-edit: which fields ("Component/field" or "script/variable") changed while playing.
    std::map<uint64_t, std::set<std::string>> liveChanges_;
    std::vector<LiveChange> pendingKeep_; // offered when the game stops
    AssistantResult assistant_;
    std::string assistantText_;
    bool assistantFocus_ = false;
    std::map<std::string, float> flashFields_; // "Component/field" highlighted after an automatic change
    std::map<std::string, std::pair<stdfs::file_time_type, ScriptFacts>> factsCache_;
    bool showExplain_ = true, focusExplain_ = false;
    std::vector<Diagnosis> doctorItems_;
    std::string doctorTitle_;
    bool showDoctor_ = false, focusDoctor_ = false, doctorChecked_ = false;
    struct LadderState {
        std::string title, easy, path, behavior, className, error;
        UUID entity;
        bool fromBlocks = false, dirty = true, sideBySide = false;
        int rung = 1; // 0 behavior/blocks, 1 EasyScript, 2+ other engines
        std::vector<std::string> notes;
        std::unique_ptr<CodeEditor> view, left;
    };
    LadderState ladder_;
    RecipeChoices recipeChoices_;
    RecipeCard recipeCard_;
    bool showRecipes_ = false, focusRecipes_ = false, showRecipeCard_ = false, focusRecipeCard_ = false;
    bool showLadder_ = false, focusLadder_ = false;
    bool pausedOnError_ = false; // the game paused itself because of an error
    std::set<std::string> errorPauses_; // "file:line" of errors that already paused this play session
    int gameAspect_ = 0;
    bool showStats_ = false;
    bool muteGame_ = false;
    bool boxSelecting_ = false;
    Vec2 boxStart_;
    float renderMs_ = 0;
    bool showHistory_ = false, showProfiler_ = false, showFind_ = false, showPalette_ = false, showLighting_ = false;
    std::vector<float> profFrame_, profScripts_, profPhysics_, profGameplay_, profRender_;
    struct FindResult {
        std::string file;
        int line;
        std::string text;
    };
    std::string findQuery_;
    bool findCase_ = false, findFocus_ = false;
    std::vector<FindResult> findResults_;
    std::string paletteQuery_;
    int paletteIndex_ = 0;
    std::string prefabPath_;       // prefab being edited ("" = normal scene)
    Json prefabReturnScene_;       // the scene to go back to
    std::string prefabReturnPath_;
    bool prefabReturnDirty_ = false;
    std::string assetFolder_;
    std::vector<ConsoleLine> console_;
    int logSink_ = 0;
    bool consoleScrollToBottom_ = false;
    int errorCount_ = 0;
    std::string notification_;
    float notificationTime_ = 0;
    bool notificationError_ = false;
    std::string renameTarget_;
    char renameBuffer_[256] = {};
    stdfs::path browsePath_;
    char newProjectName_[128] = "My Game";
    std::string newProjectTemplate_;
    std::vector<std::string> recentProjects_;
    int hubPage_ = 0; // 0 new project, 1 open project
    std::vector<TemplateInfo> templateCache_;
    bool templatesScanned_ = false;
    bool hubPickFolder_ = false;
    int learnStep_ = 0;
    Json tutorial_;
    std::string exportFolder_;
    std::string exportResult_;

    struct ScriptTab {
        std::string path;
        std::unique_ptr<CodeEditor> code;
        std::unique_ptr<BlockEditor> blocks;
        bool open = true;
        bool focus = false;
        bool modified = false;
    };
    std::vector<std::unique_ptr<ScriptTab>> tabs_;

    void loadRecent();
    void saveRecent();
    void setupDockspace();
    void buildLayout(const std::string& name, unsigned int dockId);
    void drawStatusBar();
    void drawHistory();
    void recordProfile();
    void drawProfiler();
    void runFind();
    void drawFind();
    void drawCommandPalette();
    void applyLighting(int preset);
    void drawLighting();
    void drawPrefabBar();
    void drawPreferences();
    void drawLevels();
    void checkLevelUp();
    void openPanels(const std::string& list);
    std::vector<std::string> extraPanels_; // panel names for tools added later
    void autosave(float dt);
    void drawMenuBar();
    void drawToolbar();
    void drawHub();
    bool drawFolderBrowser(bool projectsOnly, stdfs::path* picked);
    void drawHierarchy();
    void drawInspector();
    void drawViewport(float dt);
    void drawAssets();
    void drawConsole();
    void drawScriptTabs();
    void drawSettings();
    void drawLearn();
    void drawReference();
    void drawExport();
    void drawNotification(float dt);
    void drawQuitDialog();
    void drawSceneOverlay(const CameraView& camera);
    void handleShortcuts();
    void updateViewportCamera(float dt);
    void pickInViewport(Vec2 localMouse);
    void handleViewportDrop();
    void drawEntityNode(Entity e);
    bool drawComponent(Entity e, const ComponentInfo& info, void* data);
    void drawScriptVariables(Entity e);
    void drawAddComponent(Entity e);
    void drawAssistant();
    void drawExplain();
    void drawDoctor();
    void drawErrorBar(ImVec2 pos, ImVec2 size);
    void drawCodeLadder();
    void drawRecipes();
    void drawRecipeCard();
    bool componentUnlocked(const ComponentInfo& info) const;
    void saveAllScripts();
    bool exportGame(const stdfs::path& folder, std::string& message);
    void loadTutorial();
    void onFilesDropped(const std::vector<std::string>& files);
    std::string uniqueName(const std::string& folder, const std::string& base, const std::string& ext) const;
    void refreshTitle();
    // Call after the user changed the scene through the UI. The first change of an
    // interaction (e.g. the start of a drag) becomes one undo step.
    void edited(const std::string& label);
    void buildApiReference();
    void scanAssets();
    void checkScript(ScriptTab& tab);
};

// Small shared UI helpers.
namespace ui {
void panelClass();
void placeWindow(ImVec2 size, ImVec2 where);
void helpMarker(const char* text);
bool iconButton(const char* id, int icon, const char* tooltip, bool active = false, float size = 0);
void sectionHeader(const char* text);
bool assetField(const char* label, std::string& value, const std::vector<std::string>& options, const char* dragType);
enum Icon { Play, Pause, Stop, Step, Move, Rotate, Scale, Grid, Magnet, Folder, File, Plus, Blocks, Code, Image, Sound,
            Model, Scene, Prefab };
} // namespace ui

} // namespace aven::editor
