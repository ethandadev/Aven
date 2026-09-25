#include "editor.h"

#include "aven/runtime/native.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"
#include "block_editor.h"
#include "code_editor.h"

#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <algorithm>
#include <mutex>
#include <random>

namespace aven::editor {

namespace {
std::mutex g_consoleMutex;
std::vector<ConsoleLine> g_pendingLines;
} // namespace

Editor::Editor(Window& window, rhi::Device& device)
    : window_(window), device_(device), assets_(&device), scene_(std::make_unique<Scene>()) {}

Editor::~Editor() {
    if (gifThread_.joinable())
        gifThread_.join();
    if (nativeThread_.joinable())
        nativeThread_.join();
    if (options_.screenshot.empty())
        prefs.save();
    if (playing_)
        stop();
    tabs_.clear();
    Log::removeSink(logSink_);
    renderer_.shutdown();
}

bool Editor::init(const EditorOptions& options) {
    options_ = options;
    if (options.screenshot.empty())
        prefs.load();
    else
        prefs.level = options.level > 0 ? options.level : 4; // automated screenshots show everything
    if (options.level > 0)
        prefs.level = options.level;
    if (!options.theme.empty())
        prefs.theme = options.theme;
    CodeEditor::palette = CodePalette::find(prefs.codeTheme);
    if (!renderer_.init(&device_, &assets_))
        return false;
    renderer_.sceneOverlay = [this](const CameraView& cam) { drawSceneOverlay(cam); };
    logSink_ = Log::addSink([](const LogMessage& m) {
        std::lock_guard lock(g_consoleMutex);
        g_pendingLines.push_back({m.level, m.text, m.file, m.line, 1});
    });
    window_.onFileDrop = [this](const std::vector<std::string>& files) { onFilesDropped(files); };
    loadRecent();
    buildApiReference();
    browsePath_ = fs::userDataDir("Projects").parent_path().parent_path();
    if (const char* home = std::getenv("HOME"))
        browsePath_ = stdfs::path(home);
    else if (const char* profile = std::getenv("USERPROFILE"))
        browsePath_ = stdfs::path(profile);
    if (!options.newProject.empty()) {
        const TemplateInfo* tmpl = nullptr;
        auto all = templates();
        for (auto& t : all)
            if (t.id == options.templateId)
                tmpl = &t;
        if (!options.templateId.empty() && !tmpl) {
            Log::error("No template called '", options.templateId, "'.");
            return false;
        }
        if (!createProject(options.newProject, options.newProject.filename().string(), tmpl))
            return false;
        options_.project = options.newProject;
    }
    if (!options.exportTo.empty()) {
        if (!hasProject() && !openProject(options.project))
            return false;
        std::string message;
        bool ok = exportGame(options.exportTo, message);
        if (ok)
            Log::info(message);
        else
            Log::error(message);
        quit_ = true;
        return ok;
    }
    if (!options_.project.empty() && (hasProject() || openProject(options_.project))) {
        showHub_ = false;
        if (!options.openFile.empty()) {
            if (fs::extension(options.openFile) == ".blocks")
                openBlocks(options.openFile);
            else if (fs::extension(options.openFile) == ".es")
                openScript(options.openFile);
        }
        if (!options.select.empty())
            select(scene_->findByName(options.select));
        openPanels(options.openPanel);
        if (options.play) {
            for (auto& t : tabs_)
                t->focus = false;
            play();
        }
    }
    if (options.openPanel == "hub")
        showHub_ = true;
    if (!hasProject())
        openPanels(options.openPanel);
    return true;
}

void Editor::beginFrame() {
    if (!pendingLayout_.empty()) {
        auto it = prefs.savedLayouts.find(pendingLayout_);
        if (it != prefs.savedLayouts.end()) {
            ImGui::LoadIniSettingsFromMemory(it->second.c_str(), it->second.size());
        } else {
            resetLayout_ = true;
        }
        prefs.layout = pendingLayout_;
        pendingLayout_.clear();
    }
    if (!styleDirty_)
        return;
    styleDirty_ = false;
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    buildFonts(prefs, dpiScale_, fonts);
    ImGui_ImplOpenGL3_CreateFontsTexture();
    applyStyle(prefs, dpiScale_);
    CodeEditor::palette = CodePalette::find(prefs.codeTheme);
    for (auto& t : tabs_) {
        if (t->code)
            t->code->font = fonts.code;
        if (t->blocks) {
            t->blocks->font = fonts.ui;
            t->blocks->codeFont = fonts.code;
        }
    }
}

void Editor::milestone(const std::string& key, int amount) {
    if (!options_.screenshot.empty())
        return;
    prefs.counters[key] += amount;
    checkLevelUp();
}

void Editor::checkLevelUp() {
    if (!prefs.autoLevelUp || levelUpTo_ || prefs.level >= 4)
        return;
    int earned = earnedLevel(prefs);
    std::string snooze = "snoozed_level_" + std::to_string(earned);
    if (earned > prefs.level && !prefs.counters.count(snooze))
        levelUpTo_ = earned;
}

bool Editor::shortcut(const char* action) {
    ImGuiKeyChord chord = prefs.chord(action);
    if (!chord || !ImGui::IsKeyChordPressed(chord))
        return false;
    for (auto& a : keyActions())
        if (std::string(a.id) == action && !a.whileTyping && ImGui::GetIO().WantTextInput)
            return false;
    return true;
}

void Editor::autosave(float dt) {
    if (prefs.autosaveMinutes <= 0 || playing_ || !hasProject())
        return;
    autosaveTimer_ += dt;
    if (autosaveTimer_ < prefs.autosaveMinutes * 60.0f)
        return;
    autosaveTimer_ = 0;
    bool anyScript = false;
    for (auto& t : tabs_)
        anyScript = anyScript || t->modified;
    if (!dirty_ && !anyScript)
        return;
    saveAllScripts();
    if (dirty_ && !scenePath_.empty()) {
        fs::writeText(projectDir_ / scenePath_, scene_->save().dump(2) + "\n");
        dirty_ = false;
        refreshTitle();
    }
    notify("Autosaved.");
}

// For automated screenshots and quick starts: --panel prefs,profiler,history...
void Editor::openPanels(const std::string& list) {
    size_t start = 0;
    while (start < list.size()) {
        size_t comma = list.find(',', start);
        std::string p = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        start = comma == std::string::npos ? list.size() : comma + 1;
        if (p.size() > 1 && p[0] == '@' && p.find(':') != std::string::npos) { // @N:command runs on frame N
            deferredPanels_.push_back({std::atoi(p.substr(1).c_str()), p.substr(p.find(':') + 1)});
            continue;
        }
        if (p == "settings") showSettings_ = true;
        else if (p == "reference") showReference_ = true;
        else if (p == "prefs") showPrefs_ = true;
        else if (p.rfind("prefs:", 0) == 0) { showPrefs_ = true; prefsSection_ = p.substr(6); }
        else if (p == "levels") showLevels_ = true;
        else if (p == "levelup") levelUpTo_ = std::min(4, prefs.level + 1);
        else if (p == "history") showHistory_ = true;
        else if (p == "profiler") showProfiler_ = true;
        else if (p == "find") showFind_ = true;
        else if (p.rfind("find:", 0) == 0) { showFind_ = true; findQuery_ = p.substr(5); runFind(); }
        else if (p == "lighting") showLighting_ = true;
        else if (p == "palette") showPalette_ = true;
        else if (p == "stats") showStats_ = true;
        else if (p == "export") showExport_ = true;
        else if (p == "share") openShare();
        else if (p == "screenshotview") takeScreenshot();
        else if (p == "gif") toggleGifRecording();
        else if (p == "quests") openQuests();
        else if (p == "soundmaker") openSoundMaker();
        else if (p == "pixel") openPixelEditor("");
        else if (p.rfind("pixel:", 0) == 0) openPixelEditor(p.substr(6));
        else if (p == "spritesheet") openSpriteSheet();
        else if (p == "tiles") openTilePainter();
        else if (p == "savescene") saveScene();
        else if (p.rfind("reference-md:", 0) == 0) writeApiReference(p.substr(13));
        else if (p == "native") openNativeCode();
        else if (p == "newnative") createNativeModule();
        else if (p == "buildnative") buildNativeModule();
        else if (p == "newtilemap") createEntity("Tilemap");
        else if (p == "startertiles") useStarterTileset(selected());
        else if (p.rfind("tilebox:", 0) == 0) { // tilebox:x0:y0:x1:y1:tile fills a box on the selected Tilemap
            int v[5] = {0, 0, 0, 0, 0};
            std::sscanf(p.c_str() + 8, "%d:%d:%d:%d:%d", &v[0], &v[1], &v[2], &v[3], &v[4]);
            if (auto* map = selected() ? scene().registry().tryGet<Tilemap>(selected()) : nullptr) {
                recordUndo("Paint tiles");
                for (int y = v[1]; y <= v[3]; ++y)
                    for (int x = v[0]; x <= v[2]; ++x)
                        map->set(x, y, v[4]);
            }
        }
        else if (p.rfind("quest:", 0) == 0) {
            openQuests();
            loadQuests();
            for (size_t i = 0; i < quests_.size(); ++i)
                if (quests_[i]["id"].asString("") == p.substr(6))
                    questIndex_ = static_cast<int>(i);
        }
        else if (p == "itchzip") { std::string m; makeItchZip(m); Log::info(m); }
        else if (p == "webexport") { std::string m; exportWeb(projectDir_ / "exports", m); Log::info(m); }
        else if (p == "card") { std::string m; makeGameCard(projectDir_ / "exports" / "card.png", "", m); Log::info(m); openShare(); }
        else if (p == "sharestart") { std::string m; startSharing(m); Log::info(m); openShare(); }
        else if (p == "learn") showLearn_ = true;
        else if (p == "explain") { showExplain_ = true; focusExplain_ = true; }
        else if (p == "inspector") { showInspector_ = true; focusInspector_ = true; }
        else if (p.rfind("ladder:", 0) == 0) { // ladder:<script path>[@rung] or ladder:<Component>@<object>
            std::string arg = p.substr(7);
            int rung = -1;
            if (size_t at = arg.find('#'); at != std::string::npos) {
                rung = std::atoi(arg.substr(at + 1).c_str());
                arg = arg.substr(0, at);
            }
            if (size_t at = arg.find('@'); at != std::string::npos) {
                if (Entity e = scene_->findByName(arg.substr(at + 1)))
                    openCodeLadderForBehavior(e, arg.substr(0, at));
            } else {
                openCodeLadderForScript(arg);
            }
            if (rung >= 0)
                ladder_.rung = rung;
        }
        else if (p.rfind("behavior2script:", 0) == 0) { // behavior2script:<Component>@<object>
            std::string arg = p.substr(16);
            size_t at = arg.find('@');
            if (at != std::string::npos)
                if (Entity e = scene_->findByName(arg.substr(at + 1)))
                    behaviorToScript(e, arg.substr(0, at));
        }
        else if (p == "recipes") { openRecipes(); }
        else if (p == "replay") { openBugReplay(); }
        else if (p == "bugreport") { endRecording(); openBugReplay(); std::string f = saveBugReport(); Log::info("Bug report: ", f); }
        else if (p.rfind("replayfile:", 0) == 0) { // replayfile:<path>[#fromFrame]
            std::string arg = p.substr(11);
            int from = 0;
            if (size_t at = arg.find('#'); at != std::string::npos) {
                from = std::atoi(arg.substr(at + 1).c_str());
                arg = arg.substr(0, at);
            }
            Replay r;
            std::string error;
            if (r.load(stdfs::path(arg).is_absolute() ? stdfs::path(arg) : projectDir_ / arg, &error)) {
                lastReplay_ = r;
                startReplay(r, from);
            } else {
                Log::error(error);
            }
        }
        else if (p.rfind("recipe:", 0) == 0) { // recipe:<kind>[:goal[:collect[:dangers[:difficulty]]]] cooks right away
            std::vector<std::string> parts;
            std::string rest = p.substr(7);
            for (size_t at; (at = rest.find(':')) != std::string::npos; rest = rest.substr(at + 1))
                parts.push_back(rest.substr(0, at));
            parts.push_back(rest);
            static const char* kinds[] = {"platformer", "adventure", "shooter", "dodge", "clicker"};
            RecipeChoices c;
            for (int i = 0; i < 5; ++i)
                if (parts[0] == kinds[i])
                    c.recipe = i;
            c.name = parts[0];
            c.name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(c.name[0])));
            if (parts.size() > 1) c.goal = std::atoi(parts[1].c_str());
            if (parts.size() > 2) c.collect = std::atoi(parts[2].c_str());
            if (parts.size() > 3) c.dangers = static_cast<unsigned>(std::atoi(parts[3].c_str()));
            if (parts.size() > 4) c.difficulty = std::atoi(parts[4].c_str());
            c.makeStartScene = true;
            cookRecipe(c);
        }
        else if (p == "doctor") { runCheckup(); showDoctor_ = true; focusDoctor_ = true; }
        else if (p == "doctorfix") { // automated test: apply the first fix of every problem found
            runCheckup();
            for (auto& d : doctorItems_)
                for (auto& fix : d.fixes)
                    if (fix.label.rfind("Open ", 0) != 0) {
                        Log::info("Doctor fix: ", fix.label);
                        fix.apply(*this);
                        break;
                    }
            runCheckup();
            showDoctor_ = true;
        }
        else if (p.rfind("prefab:", 0) == 0) openPrefab(p.substr(7));
        else if (p.rfind("aspect:", 0) == 0) gameAspect_ = std::atoi(p.substr(7).c_str());
        else if (p.rfind("layout:", 0) == 0) prefs.layout = p.substr(7);
        else if (p.rfind("ask:", 0) == 0) { assistantText_ = p.substr(4); askAven(assistantText_); }
        else if (p.rfind("askapply:", 0) == 0) {
            askAven(p.substr(9));
            recordUndo("Ask Aven");
            for (auto& prop : assistant_.proposals) {
                Log::info("Ask Aven: ", prop.text);
                prop.apply(*this);
            }
            assistant_ = {};
        }
        else if (!p.empty()) extraPanels_.push_back(p);
    }
}

void Editor::refreshTitle() {
    std::string title = "Aven";
    if (hasProject())
        title = settings_.name + " - " + (scenePath_.empty() ? "untitled" : scenePath_) + (dirty_ ? " *" : "") + " - Aven";
    window_.setTitle(title);
}

void Editor::notify(const std::string& message, bool error) {
    notification_ = message;
    notificationError_ = error;
    notificationTime_ = 4.0f;
}

// ---------------------------------------------------------------- recent projects

void Editor::loadRecent() {
    auto text = fs::readText(fs::userDataDir("Aven Editor") / "recent.json");
    if (!text)
        return;
    Json j = Json::parse(*text);
    for (auto& e : j["recent"].elements())
        if (ProjectSettings::isProject(e.asString()))
            recentProjects_.push_back(e.asString());
}

void Editor::saveRecent() {
    Json j = Json::object();
    Json list = Json::array();
    for (auto& r : recentProjects_)
        list.push(r);
    j["recent"] = list;
    fs::writeText(fs::userDataDir("Aven Editor") / "recent.json", j.dump(2));
}

// ---------------------------------------------------------------- projects

std::vector<TemplateInfo> Editor::templates() const {
    std::vector<TemplateInfo> out;
    std::vector<stdfs::path> roots = {fs::executableDir() / "templates", stdfs::path(AVEN_TEMPLATES_DIR)};
    for (auto& root : roots) {
        std::error_code ec;
        if (!stdfs::is_directory(root, ec))
            continue;
        for (auto& entry : stdfs::directory_iterator(root, ec)) {
            auto text = fs::readText(entry.path() / "template.json");
            if (!text)
                continue;
            Json j = Json::parse(*text);
            TemplateInfo t;
            t.id = entry.path().filename().string();
            t.name = j["name"].asString(t.id);
            t.description = j["description"].asString();
            t.style = j["style"].asString("EasyScript");
            t.difficulty = j["difficulty"].asString("Beginner");
            t.is3D = j["3d"].asBool();
            t.color = Color::fromHex(static_cast<uint32_t>(std::strtoul(j["color"].asString("3B82F6").c_str(), nullptr, 16)));
            t.folder = entry.path();
            out.push_back(t);
        }
        if (!out.empty())
            break;
    }
    std::sort(out.begin(), out.end(), [](const TemplateInfo& a, const TemplateInfo& b) {
        auto rank = [](const TemplateInfo& t) { return t.id.rfind("blank", 0) == 0 ? 0 : 1; };
        return rank(a) != rank(b) ? rank(a) < rank(b) : a.name < b.name;
    });
    return out;
}

bool Editor::createProject(const stdfs::path& dir, const std::string& name, const TemplateInfo* tmpl) {
    std::error_code ec;
    if (stdfs::exists(dir / ProjectSettings::kFileName, ec)) {
        notify("There's already a project in that folder.", true);
        return false;
    }
    stdfs::create_directories(dir, ec);
    if (tmpl) {
        for (auto& entry : stdfs::recursive_directory_iterator(tmpl->folder, ec)) {
            stdfs::path rel = stdfs::relative(entry.path(), tmpl->folder, ec);
            if (rel == "template.json" || rel == "thumbnail.png")
                continue;
            if (entry.is_directory())
                stdfs::create_directories(dir / rel, ec);
            else
                stdfs::copy_file(entry.path(), dir / rel, stdfs::copy_options::overwrite_existing, ec);
        }
    }
    ProjectSettings s;
    s.load(dir);
    s.name = name;
    if (tmpl)
        s.templateName = tmpl->id;
    s.save(dir);
    for (const char* folder : {"scenes", "scripts", "images", "sounds", "prefabs"})
        stdfs::create_directories(dir / folder, ec);
    if (!openProject(dir))
        return false;
    if (!fs::exists(dir / s.startScene)) {
        newScene(tmpl && tmpl->is3D);
        scene_->name = "Main";
        scenePath_ = s.startScene;
        saveScene();
    }
    loadTutorial();
    showLearn_ = !tutorial_.isNull();
    return true;
}

bool Editor::openProject(const stdfs::path& dir) {
    ProjectSettings s;
    std::string error;
    if (!s.load(dir, &error)) {
        notify(error, true);
        return false;
    }
    if (playing_)
        stop();
    tabs_.clear();
    settings_ = s;
    projectDir_ = stdfs::absolute(dir);
    assets_.clear();
    assets_.setRoot(projectDir_);
    window_.input().loadActions(settings_.inputActions);
    assetFolder_.clear();
    console_.clear();
    std::string key = projectDir_.string();
    std::erase(recentProjects_, key);
    recentProjects_.insert(recentProjects_.begin(), key);
    if (recentProjects_.size() > 10)
        recentProjects_.resize(10);
    saveRecent();
    if (!openScene(settings_.startScene))
        newScene(false);
    scanAssets();
    loadTutorial();
    showHub_ = false;
    lastReplay_ = {};
    clearThumbnails();
    checkLastSession();
    NativeModules::get().refresh(projectDir_); // C/C++ behaviors, if the project has any
    Log::info("Opened project '", settings_.name, "'");
    return true;
}

void Editor::closeProject() {
    if (playing_)
        stop();
    tabs_.clear();
    projectDir_.clear();
    scene_ = std::make_unique<Scene>();
    scenePath_.clear();
    showHub_ = true;
    refreshTitle();
}

void Editor::loadTutorial() {
    tutorial_ = Json();
    learnStep_ = 0;
    auto text = fs::readText(projectDir_ / "tutorial.json");
    if (text)
        tutorial_ = Json::parse(*text);
}

// ---------------------------------------------------------------- scenes

bool Editor::openScene(const std::string& path) {
    auto text = fs::readText(projectDir_ / path);
    if (!text)
        return false;
    std::string error;
    Json data = Json::parse(*text, &error);
    auto scene = std::make_unique<Scene>();
    if (!error.empty() || !scene->load(data, &error)) {
        notify("Couldn't open " + path + ": " + error, true);
        return false;
    }
    if (playing_)
        stop();
    scene_ = std::move(scene);
    scenePath_ = path;
    loadRecipeCard(path);
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    snapshotValid_ = false;
    selection_.clear();
    // Pick the 2D or 3D view to match the scene.
    Entity cam = SceneRenderer::findCamera(*scene_);
    view3D_ = scene_->registry().count<MeshRenderer>() > 0 ||
              (cam && scene_->registry().get<Camera>(cam).projection == Projection::Perspective);
    if (cam) {
        Vec3 p = scene_->worldPosition(cam);
        if (view3D_) {
            cam3D_ = p;
            Vec3 rot = scene_->transform(cam).rotation;
            camYaw_ = rot.y;
            camPitch_ = rot.x;
        } else {
            cam2D_ = {p.x, p.y, 10};
            camZoom_ = scene_->registry().get<Camera>(cam).size * 1.3f;
        }
    }
    refreshTitle();
    return true;
}

bool Editor::saveScene() {
    if (!hasProject())
        return false;
    if (editingPrefab()) {
        Json data = scene_->saveEntities(scene_->roots());
        if (!fs::writeText(projectDir_ / prefabPath_, data.dump(2) + "\n")) {
            notify("Couldn't save the prefab.", true);
            return false;
        }
        dirty_ = false;
        refreshTitle();
        notify("Saved prefab " + prefabPath_);
        return true;
    }
    if (scenePath_.empty())
        scenePath_ = uniqueName("scenes", "scene", ".scene");
    if (!fs::writeText(projectDir_ / scenePath_, scene_->save().dump(2) + "\n")) {
        notify("Couldn't save the scene. Is the folder read-only?", true);
        return false;
    }
    dirty_ = false;
    refreshTitle();
    notify("Saved " + scenePath_);
    return true;
}

void Editor::newScene(bool is3D) {
    if (playing_)
        stop();
    scene_ = std::make_unique<Scene>();
    scene_->name = "New Scene";
    scenePath_ = uniqueName("scenes", "new_scene", ".scene");
    Entity cam = scene_->create("Camera");
    auto& c = scene_->registry().emplace<Camera>(cam);
    if (is3D) {
        c.projection = Projection::Perspective;
        scene_->transform(cam).position = {0, 3, 8};
        scene_->transform(cam).rotation = {-15, 0, 0};
        auto& pp = scene_->registry().emplace<PostProcessing>(cam);
        pp.ssao = true;
        Entity env = scene_->create("Environment");
        scene_->registry().emplace<Environment>(env);
        Entity sun = scene_->create("Sun");
        scene_->transform(sun).rotation = {-50, -30, 0};
        auto& l = scene_->registry().emplace<Light>(sun);
        l.type = LightType::Directional;
        Entity ground = scene_->create("Ground");
        scene_->transform(ground).scale = {20, 1, 20};
        auto& mr = scene_->registry().emplace<MeshRenderer>(ground);
        mr.mesh = MeshShape::Plane;
        mr.color = Color::fromHex(0x7A9A6A);
        scene_->registry().emplace<BoxCollider>(ground).size = {1, 0.02f, 1};
        cam3D_ = {6, 5, 9};
        camYaw_ = 35;
        camPitch_ = -22;
    } else {
        scene_->transform(cam).position = {0, 0, 10};
        cam2D_ = {0, 0, 10};
        camZoom_ = 6.5f;
    }
    view3D_ = is3D;
    dirty_ = true;
    undo_.clear();
    redo_.clear();
    snapshotValid_ = false;
    selection_.clear();
    refreshTitle();
}

std::string Editor::uniqueName(const std::string& folder, const std::string& base, const std::string& ext) const {
    std::string prefix = folder.empty() ? "" : folder + "/";
    for (int i = 0; i < 1000; ++i) {
        std::string name = prefix + base + (i ? "_" + std::to_string(i + 1) : "") + ext;
        if (!fs::exists(projectDir_ / name))
            return name;
    }
    return prefix + base + ext;
}

// ---------------------------------------------------------------- selection and undo

Entity Editor::selected() {
    while (!selection_.empty()) {
        if (Entity e = scene().findByUUID(selection_.back()))
            return e;
        selection_.pop_back(); // deleted objects drop out of the selection
    }
    return {};
}

void Editor::select(Entity e) {
    selection_.clear();
    if (e)
        selection_.push_back(scene().info(e).uuid);
}

void Editor::addToSelection(Entity e) {
    if (!e)
        return;
    UUID id = scene().info(e).uuid;
    std::erase(selection_, id);
    selection_.push_back(id);
}

void Editor::toggleSelection(Entity e) {
    if (!e)
        return;
    UUID id = scene().info(e).uuid;
    if (std::find(selection_.begin(), selection_.end(), id) != selection_.end())
        std::erase(selection_, id);
    else
        selection_.push_back(id);
}

bool Editor::isSelected(Entity e) {
    if (!e)
        return false;
    UUID id = scene().info(e).uuid;
    return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
}

std::vector<Entity> Editor::selectedEntities() {
    std::vector<Entity> out;
    for (UUID id : selection_)
        if (Entity e = scene().findByUUID(id))
            out.push_back(e);
    return out;
}

void Editor::recordUndo(const std::string& label) {
    if (playing_)
        return;
    undo_.push_back({label, scene_->save(), selection_});
    if (undo_.size() > 100)
        undo_.erase(undo_.begin());
    redo_.clear();
    snapshotValid_ = false;
    dirty_ = true;
    refreshTitle();
}

void Editor::edited(const std::string& label) {
    if (playing_)
        return;
    if (!editInProgress_) {
        if (!snapshotValid_) {
            cachedSnapshot_ = scene_->save();
            snapshotValid_ = true;
        }
        undo_.push_back({label, cachedSnapshot_, selection_});
        if (undo_.size() > 100)
            undo_.erase(undo_.begin());
        redo_.clear();
        editInProgress_ = true;
    }
    snapshotValid_ = false;
    if (!dirty_) {
        dirty_ = true;
        refreshTitle();
    }
}

void Editor::undo() {
    if (playing_ || undo_.empty())
        return;
    redo_.push_back({undo_.back().label, scene_->save(), selection_});
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    scene_->load(s.scene);
    selection_ = s.selection;
    snapshotValid_ = false;
    dirty_ = true;
    notify("Undo: " + s.label);
    refreshTitle();
}

void Editor::redo() {
    if (playing_ || redo_.empty())
        return;
    undo_.push_back({redo_.back().label, scene_->save(), selection_});
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    scene_->load(s.scene);
    selection_ = s.selection;
    snapshotValid_ = false;
    dirty_ = true;
    notify("Redo: " + s.label);
    refreshTitle();
}

// ---------------------------------------------------------------- play mode

void Editor::play() {
    if (playing_ || !hasProject())
        return;
    saveAllScripts();
    settings_.save(projectDir_);
    if (prefs.clearConsoleOnPlay)
        console_.clear();
    milestone("plays");
    game_ = makeGame();
    Json start = scene_->save();
    auto copy = std::make_unique<Scene>();
    copy->load(start);
    gameInput_.reset();
    game_->setScreenSize(viewportSize_);
    // A known random seed makes the session replayable (Bug replay).
    uint32_t seed = std::random_device{}();
    game_->setRandomSeed(seed);
    game_->start(std::move(copy), scenePath_);
    beginRecording(seed, start);
    replayEditNoted_ = false;
    playing_ = true;
    paused_ = false;
    pausedOnError_ = false;
    errorPauses_.clear();
    focusViewport_ = true;
}

std::unique_ptr<Game> Editor::makeGame() {
    auto game = std::make_unique<Game>(assets_, gameInput_);
    game->loadProject(projectDir_);
    game->setCursorLocked = [this](bool locked) { window_.setCursorLocked(locked); };
    game->setFullscreen = [this](bool) { notify("Fullscreen works when the game runs on its own (Build & Export)."); };
    game->isFullscreen = [] { return false; };
    return game;
}

void Editor::stop() {
    if (!playing_)
        return;
    endRecording();
    replaying_ = false;
    if (!liveChanges_.empty()) {
        pendingKeep_ = collectLiveChanges();
        liveChanges_.clear();
    }
    game_->stop();
    game_.reset();
    playing_ = false;
    paused_ = false;
    pausedOnError_ = false;
    window_.setCursorLocked(false);
    // Assets like textures may have been reloaded while playing; keep them.
}

// ---------------------------------------------------------------- creating things

Entity Editor::createEntity(const std::string& kind, Entity parent) {
    recordUndo("Create " + kind);
    Scene& s = *scene_;
    auto& reg = s.registry();
    Entity e = s.create(kind, parent);
    Vec3 spawn = view3D_ ? cam3D_ + rotate(Quat::fromEuler({camPitch_, camYaw_, 0}), {0, 0, -8}) : Vec3(cam2D_.x, cam2D_.y, 0);
    if (view3D_)
        spawn.y = std::max(spawn.y, 0.5f);
    if (!parent)
        s.transform(e).position = {std::round(spawn.x * 2) / 2, std::round(spawn.y * 2) / 2, view3D_ ? std::round(spawn.z * 2) / 2 : 0};
    static const std::pair<const char*, Shape2D> shapes2D[] = {
        {"Square", Shape2D::Square},   {"Circle", Shape2D::Circle}, {"Triangle", Shape2D::Triangle},
        {"Rounded Square", Shape2D::RoundedSquare}, {"Diamond", Shape2D::Diamond}, {"Star", Shape2D::Star},
        {"Heart", Shape2D::Heart}};
    for (auto& [name, shape] : shapes2D)
        if (kind == name) {
            auto& sr = reg.emplace<SpriteRenderer>(e);
            sr.shape = shape;
            sr.color = Color::fromHex(0x60A5FA);
        }
    static const std::pair<const char*, MeshShape> shapes3D[] = {
        {"Cube", MeshShape::Cube},         {"Sphere", MeshShape::Sphere},   {"Plane", MeshShape::Plane},
        {"Cylinder", MeshShape::Cylinder}, {"Capsule", MeshShape::Capsule}, {"Cone", MeshShape::Cone},
        {"Torus", MeshShape::Torus}};
    for (auto& [name, shape] : shapes3D)
        if (kind == name) {
            reg.emplace<MeshRenderer>(e).mesh = shape;
            if (shape == MeshShape::Plane)
                s.transform(e).scale = {10, 1, 10};
        }
    if (kind == "Sprite")
        reg.emplace<SpriteRenderer>(e);
    else if (kind == "Text")
        reg.emplace<TextRenderer>(e);
    else if (kind == "Camera") {
        auto& c = reg.emplace<Camera>(e);
        c.primary = SceneRenderer::findCamera(s) == e;
        if (view3D_)
            c.projection = Projection::Perspective;
        else
            s.transform(e).position.z = 10;
    } else if (kind == "Point Light")
        reg.emplace<Light>(e).type = LightType::Point;
    else if (kind == "Spot Light") {
        reg.emplace<Light>(e).type = LightType::Spot;
        s.transform(e).rotation = {-90, 0, 0};
    } else if (kind == "Sun") {
        reg.emplace<Light>(e).type = LightType::Directional;
        s.transform(e).rotation = {-50, -30, 0};
    } else if (kind == "Particles")
        reg.emplace<ParticleEmitter>(e);
    else if (kind == "Sound")
        reg.emplace<AudioSource>(e);
    else if (kind == "UI Text") {
        reg.emplace<UIElement>(e).size = {400, 60};
        reg.emplace<UIText>(e).text = "Score: 0";
    } else if (kind == "UI Button") {
        reg.emplace<UIElement>(e).size = {220, 64};
        reg.emplace<UIButton>(e);
    } else if (kind == "UI Panel") {
        reg.emplace<UIElement>(e).size = {400, 300};
        reg.emplace<UIImage>(e).color = {0, 0, 0, 0.5f};
    } else if (kind == "Player 3D") {
        reg.emplace<MeshRenderer>(e).mesh = MeshShape::Capsule;
        reg.get<MeshRenderer>(e).color = Color::fromHex(0x3B82F6);
        reg.emplace<CharacterController>(e);
        s.transform(e).position.y = 1.5f;
    } else if (kind == "Environment")
        reg.emplace<Environment>(e);
    else if (kind == "Tilemap") {
        // At the origin, so tiles line up with the grid.
        reg.emplace<Tilemap>(e);
        if (!parent)
            s.transform(e).position = {0, 0, 0};
    }
    s.info(e).name = kind == "Rounded Square" ? "RoundedSquare" : kind;
    select(e);
    if (kind == "Tilemap")
        openTilePainter();
    milestone("objects_added");
    return e;
}

Entity Editor::instantiatePrefab(const std::string& path, Vec3 position) {
    auto text = fs::readText(projectDir_ / path);
    if (!text)
        return {};
    recordUndo("Add prefab");
    auto roots = scene_->instantiate(Json::parse(*text));
    if (roots.empty())
        return {};
    scene_->registry().getOrEmplace<PrefabInstance>(roots[0]).path = path;
    scene_->setWorldPosition(roots[0], position);
    select(roots[0]);
    return roots[0];
}

void Editor::copySelection(bool cut) {
    auto sel = selectedEntities();
    if (sel.empty())
        return;
    clipboard_ = scene_->saveEntities(sel);
    clipboard_["aven"] = "objects";
    ImGui::SetClipboardText(clipboard_.dump(2).c_str());
    if (cut) {
        recordUndo("Cut");
        for (Entity e : sel)
            if (scene_->valid(e))
                scene_->destroy(e);
        selection_.clear();
    }
    notify(std::to_string(sel.size()) + (sel.size() == 1 ? " object " : " objects ") + (cut ? "cut." : "copied."));
}

void Editor::pasteClipboard() {
    // Objects copied in another project (or another Aven window) arrive through the system clipboard.
    if (const char* text = ImGui::GetClipboardText()) {
        std::string error;
        Json j = Json::parse(text, &error);
        if (error.empty() && j["aven"].asString() == "objects")
            clipboard_ = j;
    }
    if (clipboard_.isNull())
        return;
    recordUndo("Paste");
    Entity parent;
    auto roots = scene_->instantiate(clipboard_, parent);
    selection_.clear();
    for (Entity e : roots) {
        // Nudge copies so they don't sit exactly on top of the originals.
        Vec3 p = scene_->worldPosition(e);
        scene_->setWorldPosition(e, view3D_ ? p + Vec3{0.5f, 0, 0.5f} : p + Vec3{0.5f, -0.5f, 0});
        addToSelection(e);
    }
    notify("Pasted " + std::to_string(roots.size()) + (roots.size() == 1 ? " object." : " objects."));
}

void Editor::openPrefab(const std::string& path) {
    if (playing_)
        stop();
    if (editingPrefab())
        closePrefab(true);
    auto text = fs::readText(projectDir_ / path);
    if (!text) {
        notify("Couldn't open " + path, true);
        return;
    }
    prefabReturnScene_ = scene_->save();
    prefabReturnPath_ = scenePath_;
    prefabReturnDirty_ = dirty_;
    auto s = std::make_unique<Scene>();
    s->name = stdfs::path(path).stem().string();
    s->instantiate(Json::parse(*text));
    scene_ = std::move(s);
    scenePath_ = path;
    prefabPath_ = path;
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    snapshotValid_ = false;
    selection_.clear();
    view3D_ = scene_->registry().count<MeshRenderer>() > 0;
    if (!scene_->roots().empty()) {
        select(scene_->roots().front());
        focusSelected();
    }
    refreshTitle();
    notify("Editing prefab " + path + ". Changes apply to every copy.");
}

void Editor::closePrefab(bool save) {
    if (!editingPrefab())
        return;
    if (save && dirty_)
        saveScene();
    std::string path = prefabPath_;
    prefabPath_.clear();
    auto s = std::make_unique<Scene>();
    s->load(prefabReturnScene_);
    scene_ = std::move(s);
    scenePath_ = prefabReturnPath_;
    dirty_ = prefabReturnDirty_;
    undo_.clear();
    redo_.clear();
    snapshotValid_ = false;
    selection_.clear();
    Entity cam = SceneRenderer::findCamera(*scene_);
    view3D_ = scene_->registry().count<MeshRenderer>() > 0 ||
              (cam && scene_->registry().get<Camera>(cam).projection == Projection::Perspective);
    // Copies of the prefab in this scene pick up the changes.
    if (save) {
        std::vector<Entity> instances;
        scene_->registry().each<PrefabInstance>([&](Entity e, PrefabInstance& pi) {
            if (pi.path == path)
                instances.push_back(e);
        });
        for (Entity e : instances)
            revertToPrefab(e);
        if (!instances.empty()) {
            dirty_ = true;
            notify("Updated " + std::to_string(instances.size()) + " copies of " + path + ".");
        }
    }
    selection_.clear();
    refreshTitle();
}

void Editor::applyToPrefab(Entity instance) {
    auto* pi = scene_->registry().tryGet<PrefabInstance>(instance);
    if (!pi)
        return;
    std::string path = pi->path;
    Json data = scene_->saveEntities({instance});
    if (data["entities"].size()) {
        auto& t = data["entities"][0]["components"]["Transform"];
        t["position"] = Json::parse("[0, 0, 0]");
        data["entities"][0]["components"].erase("PrefabInstance");
    }
    fs::writeText(projectDir_ / path, data.dump(2) + "\n");
    recordUndo("Apply to prefab");
    std::vector<Entity> others;
    UUID self = scene_->info(instance).uuid;
    scene_->registry().each<PrefabInstance>([&](Entity e, PrefabInstance& p) {
        if (p.path == path && scene_->info(e).uuid != self)
            others.push_back(e);
    });
    for (Entity e : others)
        revertToPrefab(e);
    notify("Saved to " + path + (others.empty() ? "." : " and updated " + std::to_string(others.size()) + " other copies."));
}

void Editor::revertToPrefab(Entity instance) {
    auto* pi = scene_->registry().tryGet<PrefabInstance>(instance);
    if (!pi)
        return;
    std::string path = pi->path;
    auto text = fs::readText(projectDir_ / path);
    if (!text)
        return;
    Transform keep = scene_->transform(instance);
    std::string name = scene_->info(instance).name;
    Entity parent = scene_->parent(instance);
    int index = scene_->siblingIndex(instance);
    bool wasSelected = isSelected(instance);
    scene_->destroy(instance);
    auto roots = scene_->instantiate(Json::parse(*text), parent);
    if (roots.empty())
        return;
    Entity e = roots[0];
    scene_->setParent(e, parent, false, index);
    Transform& t = scene_->transform(e);
    t.position = keep.position;
    t.rotation = keep.rotation;
    t.scale = keep.scale;
    scene_->info(e).name = name;
    scene_->registry().getOrEmplace<PrefabInstance>(e).path = path;
    if (wasSelected)
        addToSelection(e);
}

void Editor::drawPrefabBar() {
    if (!editingPrefab())
        return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(40, 110, 90, 255));
    ImGui::BeginChild("##prefabbar", {0, ImGui::GetFrameHeight() + 10}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored({1, 1, 1, 1}, "Editing prefab: %s", prefabPath_.c_str());
    ImGui::SameLine();
    ImGui::TextColored({0.8f, 1, 0.9f, 0.8f}, "(every copy changes too)");
    ImGui::SameLine(ImGui::GetWindowWidth() - 280);
    if (ImGui::Button("Save", {80, 0}))
        saveScene();
    ImGui::SameLine();
    if (ImGui::Button("Save and go back", {170, 0}))
        closePrefab(true);
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void Editor::savePrefab(Entity e) {
    std::string name;
    for (char c : scene_->info(e).name)
        name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    std::string path = uniqueName("prefabs", name.empty() ? "prefab" : name, ".prefab");
    Json data = scene_->saveEntities({e});
    // Prefabs spawn at the position you choose, so store them at the origin.
    if (data["entities"].size())
        data["entities"][0]["components"]["Transform"]["position"] = Json::parse("[0, 0, 0]");
    fs::writeText(projectDir_ / path, data.dump(2));
    recordUndo("Link prefab");
    scene_->registry().getOrEmplace<PrefabInstance>(e).path = path;
    notify("Saved prefab " + path + ". Use spawn(\"" + path + "\", x, y) to create copies.");
    scanAssets();
}

// ---------------------------------------------------------------- scripts

std::string Editor::newScriptFile(const std::string& baseName, bool blocksScript) {
    std::string name;
    for (char c : baseName)
        name += std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(static_cast<unsigned char>(c))) : '_';
    if (name.empty())
        name = "script";
    std::string path = uniqueName("scripts", name, blocksScript ? ".blocks" : ".es");
    if (blocksScript) {
        Json doc = blocks::emptyDocument();
        Json stack = Json::object();
        stack["x"] = 30;
        stack["y"] = 30;
        Json list = Json::array();
        Json hat = Json::object();
        hat["type"] = "when_start";
        list.push(hat);
        stack["blocks"] = list;
        doc["scripts"].push(stack);
        fs::writeText(projectDir_ / path, doc.dump(2));
    } else {
        std::string text = "# " + baseName + "\n"
                           "# Variables here can be changed in the Inspector.\n"
                           "speed = 5  # how fast this object moves\n\n"
                           "def on_start():\n"
                           "    print(\"Hello from \" + self.name)\n\n"
                           "def on_update(dt):\n"
                           "    # Runs every frame. dt is the time since the last frame.\n"
                           "    self.x += axis(\"horizontal\") * speed * dt\n";
        fs::writeText(projectDir_ / path, text);
    }
    scanAssets();
    return path;
}

void Editor::attachScript(Entity e, const std::string& path) {
    recordUndo("Attach script");
    scene_->registry().getOrEmplace<Script>(e).path = path;
}

void Editor::openScript(const std::string& path, int line) {
    for (auto& t : tabs_)
        if (t->path == path) {
            t->focus = true;
            if (t->code && line > 0)
                t->code->gotoLine(line);
            return;
        }
    if (fs::extension(path) == ".blocks") {
        openBlocks(path);
        return;
    }
    auto text = fs::readText(projectDir_ / path);
    if (!text) {
        notify("Couldn't open " + path, true);
        return;
    }
    auto tab = std::make_unique<ScriptTab>();
    tab->path = path;
    tab->code = std::make_unique<CodeEditor>();
    tab->code->setText(*text);
    tab->code->font = fonts.code;
    if (isNativeSource(path)) {
        tab->code->language = CodeLanguage::Cpp;
    } else {
        tab->code->completions = completions_;
        tab->code->highlightWords = apiWords_;
    }
    if (line > 0)
        tab->code->gotoLine(line);
    tab->focus = true;
    checkScript(*tab);
    tabs_.push_back(std::move(tab));
}

void Editor::openBlocks(const std::string& path) {
    for (auto& t : tabs_)
        if (t->path == path) {
            t->focus = true;
            return;
        }
    auto text = fs::readText(projectDir_ / path);
    if (!text) {
        notify("Couldn't open " + path, true);
        return;
    }
    auto tab = std::make_unique<ScriptTab>();
    tab->path = path;
    tab->blocks = std::make_unique<BlockEditor>();
    tab->blocks->load(Json::parse(*text));
    tab->blocks->font = fonts.ui;
    tab->blocks->codeFont = fonts.code;
    tab->blocks->assetOptions = [this](blocks::InputType t) {
        switch (t) {
        case blocks::InputType::Image: return projectFiles({".png", ".jpg", ".jpeg", ".bmp", ".tga"});
        case blocks::InputType::Sound: return projectFiles({".wav", ".mp3", ".ogg", ".flac"});
        case blocks::InputType::Prefab: return projectFiles({".prefab"});
        case blocks::InputType::Scene: return projectFiles({".scene"});
        default: return std::vector<std::string>{};
        }
    };
    ScriptTab* raw = tab.get();
    tab->blocks->onConvertToCode = [this, raw]() {
        std::string code = raw->blocks->code();
        std::string base = stdfs::path(raw->path).stem().string();
        std::string path = uniqueName("scripts", base, ".es");
        fs::writeText(projectDir_ / path, "# Converted from " + raw->path + "\n" + code);
        // Objects using the blocks switch to the new code.
        int switched = 0;
        recordUndo("Switch to code");
        scene_->registry().each<Script>([&](Entity, Script& s) {
            if (s.path == raw->path) {
                s.path = path;
                ++switched;
            }
        });
        scanAssets();
        openScript(path);
        notify("Made " + path + (switched ? " and switched " + std::to_string(switched) + " object(s) to it." : "."));
    };
    tab->focus = true;
    tabs_.push_back(std::move(tab));
}

void Editor::checkScript(ScriptTab& tab) {
    if (isNativeSource(tab.path))
        return; // the C compiler checks these (Build)
    std::string source;
    if (tab.code)
        source = tab.code->text();
    else if (tab.blocks)
        source = tab.blocks->code();
    script::VM vm;
    script::ScriptError error("");
    bool failed = false;
    vm.onError = [&](const script::ScriptError& e) {
        error = e;
        failed = true;
    };
    vm.compile(source, tab.path);
    if (tab.code)
        tab.code->setError(failed ? error.line : 0, failed ? error.what() : "");
    if (tab.blocks)
        tab.blocks->setCodeError(failed ? error.line : 0, failed ? error.what() : "");
}

void Editor::saveAllScripts() {
    for (auto& t : tabs_) {
        if (!t->modified)
            continue;
        if (t->code)
            fs::writeText(projectDir_ / t->path, t->code->text());
        else if (t->blocks)
            fs::writeText(projectDir_ / t->path, t->blocks->save().dump(2));
        t->modified = false;
    }
}

// ---------------------------------------------------------------- misc

std::vector<std::string> Editor::projectFiles(const std::vector<std::string>& extensions) const {
    std::vector<std::string> out;
    for (auto& f : assetFiles_) {
        std::string ext = fs::extension(f);
        if (std::find(extensions.begin(), extensions.end(), ext) != extensions.end())
            out.push_back(f);
    }
    return out;
}

void Editor::scanAssets() {
    assetFiles_.clear();
    std::error_code ec;
    if (projectDir_.empty())
        return;
    for (auto it = stdfs::recursive_directory_iterator(projectDir_, stdfs::directory_options::skip_permission_denied, ec);
         it != stdfs::recursive_directory_iterator(); it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (!name.empty() && name[0] == '.') {
            if (it->is_directory())
                it.disable_recursion_pending();
            continue;
        }
        if (it->is_regular_file())
            assetFiles_.push_back(fs::relativePath(it->path(), projectDir_));
    }
    std::sort(assetFiles_.begin(), assetFiles_.end());
}

void Editor::onFilesDropped(const std::vector<std::string>& files) {
    if (!hasProject())
        return;
    int copied = 0;
    for (auto& f : files) {
        stdfs::path src(f);
        std::string ext = fs::extension(src);
        std::string folder = assetFolder_;
        if (folder.empty()) {
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".bmp" || ext == ".tga")
                folder = "images";
            else if (ext == ".wav" || ext == ".mp3" || ext == ".ogg" || ext == ".flac")
                folder = "sounds";
            else if (ext == ".gltf" || ext == ".glb" || ext == ".bin")
                folder = "models";
            else if (ext == ".ttf" || ext == ".otf")
                folder = "fonts";
        }
        std::error_code ec;
        stdfs::create_directories(projectDir_ / folder, ec);
        stdfs::copy_file(src, projectDir_ / folder / src.filename(), stdfs::copy_options::overwrite_existing, ec);
        if (!ec)
            ++copied;
    }
    scanAssets();
    notify(copied ? "Added " + std::to_string(copied) + " file(s) to the project." : "Couldn't copy those files.", copied == 0);
}

// docs/easyscript-api.md is made from the same list the Reference panel shows (tools/docs/make_api_reference.sh).
void Editor::writeApiReference(const std::string& path) {
    std::string md = "# EasyScript API\n\nEverything scripts can use, generated from the engine itself (the same list as the "
                     "editor's Scripting Reference). Blocks turn into these calls, and the C API (sdk/include/aven.h) uses "
                     "`aven_` plus the same names.\n";
    std::vector<std::string> groups;
    for (auto& e : api_)
        if (std::find(groups.begin(), groups.end(), e.group) == groups.end())
            groups.push_back(e.group);
    for (auto& g : groups) {
        md += "\n## " + g + "\n\n";
        for (auto& e : api_)
            if (e.group == g)
                md += "- `" + e.signature + "`\n";
    }
    fs::writeText(path, md);
    Log::info("Wrote ", path, " (", api_.size(), " entries)");
}

void Editor::buildApiReference() {
    // Ask a throwaway game which functions scripts can use, with their signatures.
    Input input;
    Game probe(assets_, input);
    auto& vm = probe.scripts().vm();
    auto groupOf = [](const std::string& n) -> std::string {
        static const std::pair<const char*, const char*> groups[] = {
            {"key_", "Input"},      {"mouse_", "Input"},     {"axis", "Input"},          {"find", "Objects"},
            {"spawn", "Objects"},   {"create_", "Objects"},  {"destroy", "Objects"},     {"count", "Objects"},
            {"camera", "Camera"},   {"distance", "Math"},    {"direction", "Math"},      {"tween", "Animation"},
            {"broadcast", "Events"}, {"load_scene", "Scenes"}, {"restart_scene", "Scenes"}, {"quit", "Scenes"},
            {"pause_", "Scenes"},   {"resume_", "Scenes"},   {"is_paused", "Scenes"},    {"set_gravity", "Physics"},
            {"raycast", "Physics"}, {"play_", "Audio"},      {"stop_music", "Audio"},    {"set_volume", "Audio"},
            {"save_data", "Saving"}, {"load_data", "Saving"}, {"has_data", "Saving"},    {"delete_data", "Saving"},
            {"wait", "Time"},       {"after", "Time"},       {"every", "Time"},          {"stop_timer", "Time"},
            {"start_task", "Time"}, {"time", "Time"},        {"delta_time", "Time"},     {"set_time_scale", "Time"},
            {"random", "Random"},   {"choice", "Random"},    {"shuffle", "Random"},      {"vec", "Vectors & colors"},
            {"rgb", "Vectors & colors"}, {"color", "Vectors & colors"}, {"hsv", "Vectors & colors"},
            {"screen_", "Screen"},  {"set_fullscreen", "Screen"}, {"is_fullscreen", "Screen"}, {"lock_mouse", "Screen"},
        };
        for (auto& [prefix, group] : groups)
            if (n.rfind(prefix, 0) == 0)
                return group;
        return "Basics";
    };
    for (auto& name : vm.globalNames()) {
        const script::Value* v = vm.global(script::intern(name));
        std::string sig = name;
        if (v && v->type() == script::Type::NativeFunction) {
            auto* nf = v->as<script::NativeFunctionObj>();
            if (!nf->signature.empty())
                sig = nf->signature;
        } else if (name == "game") {
            sig = "game.anything = value  (shared by all scripts and scenes)";
        }
        api_.push_back({groupOf(name), name, sig, ""});
        completions_.push_back({name, sig});
        apiWords_.insert(name);
    }
    for (auto& p : ScriptSystem::propertyNames())
        api_.push_back({"self properties", "self." + p, "self." + p, ""});
    for (auto& m : ScriptSystem::methodNames())
        api_.push_back({"self actions", "self." + m, "self." + m + "(...)", ""});
    const char* callbacks[][2] = {
        {"on_start", "def on_start():  runs once when the object starts"},
        {"on_update", "def on_update(dt):  runs every frame"},
        {"on_fixed_update", "def on_fixed_update(dt):  runs at a steady 60 times per second (physics)"},
        {"on_collide", "def on_collide(other):  bumped into another object"},
        {"on_collide_end", "def on_collide_end(other):  stopped touching"},
        {"on_trigger", "def on_trigger(other):  something entered this trigger"},
        {"on_trigger_exit", "def on_trigger_exit(other):  something left this trigger"},
        {"on_click", "def on_click():  the player clicked this object or button"},
        {"on_key_pressed", "def on_key_pressed(key):  any key was pressed"},
        {"on_message", "def on_message(message, data):  a broadcast was sent"},
        {"on_destroy", "def on_destroy():  about to be removed"},
    };
    for (auto& cb : callbacks) {
        api_.push_back({"Events you can write", cb[0], cb[1], ""});
        completions_.push_back({cb[0], cb[1]});
    }
    for (const char* kw : {"self", "def", "return", "while", "elif", "else", "True", "False", "None", "continue", "break"})
        completions_.push_back({kw, "keyword"});
}

// ---------------------------------------------------------------- frame

void Editor::frame(float dt) {
    ++frameCount_;
    {
        std::lock_guard lock(g_consoleMutex);
        for (auto& l : g_pendingLines) {
            // Pause on the first error while playing, and let the doctor explain it.
            if (playing_ && l.level != LogLevel::Info && recorder_.recording())
                recorder_.addEvent(l.level == LogLevel::Error ? "error" : "warning", l.text, l.file, l.line);
            if (l.level == LogLevel::Error && playing_ && !replaying_ && !paused_ && prefs.pauseOnError &&
                errorPauses_.insert(l.file + ":" + std::to_string(l.line)).second) {
                paused_ = true;
                pausedOnError_ = true;
                replayReason_ = "The last 20 seconds before: " + l.text;
                if (unlocked(Feature::Doctor))
                    openDoctorFor(l.text, l.file, l.line);
            }
            if (!console_.empty() && console_.back().text == l.text && console_.back().file == l.file &&
                console_.back().line == l.line)
                ++console_.back().count;
            else
                console_.push_back(l);
            consoleScrollToBottom_ = true;
        }
        g_pendingLines.clear();
        if (console_.size() > 2000)
            console_.erase(console_.begin(), console_.begin() + 500);
    }
    for (auto it = deferredPanels_.begin(); it != deferredPanels_.end();) {
        if (it->first == frameCount_) {
            std::string command = it->second;
            it = deferredPanels_.erase(it);
            openPanels(command);
        } else {
            ++it;
        }
    }
    for (auto it = flashFields_.begin(); it != flashFields_.end();)
        it = (it->second -= dt) <= 0 ? flashFields_.erase(it) : std::next(it);
    if (!projectDir_.empty())
        updateNativeCode(dt);
    errorCount_ = 0;
    for (auto& l : console_)
        if (l.level == LogLevel::Error)
            ++errorCount_;

    // Interaction bookkeeping for undo.
    if (!ImGui::IsAnyItemActive() && !gizmoWasUsing_)
        editInProgress_ = false;

    if (!hasProject() || showHub_) {
        drawHub();
    } else {
        assetScanTimer_ += dt;
        if (assetScanTimer_ > 2.0f) {
            assetScanTimer_ = 0;
            scanAssets();
            if (!playing_ && assets_.reloadChanged())
                notify("Images updated from disk.");
        }
        autosave(dt);
        setupDockspace();
        if (showHierarchy_)
            drawHierarchy();
        if (showInspector_)
            drawInspector();
        drawViewport(dt);
        if (showAssets_ && unlocked(Feature::Assets))
            drawAssets();
        if (showConsole_ && unlocked(Feature::Console))
            drawConsole();
        if (showExplain_ && unlocked(Feature::Explain))
            drawExplain();
        if (showDoctor_ && unlocked(Feature::Doctor))
            drawDoctor();
        if (showLadder_ && unlocked(Feature::CodeLadder))
            drawCodeLadder();
        if (showRecipes_ && unlocked(Feature::Recipes))
            drawRecipes();
        if (showRecipeCard_)
            drawRecipeCard();
        if (showReplay_ && unlocked(Feature::BugReplay))
            drawBugReplay();
        if (showQuests_)
            drawQuests();
        if (showSoundMaker_ && unlocked(Feature::SoundMaker))
            drawSoundMaker();
        if (showPixelEditor_ && unlocked(Feature::PixelEditor))
            drawPixelEditor();
        else
            pixelEditorFocused_ = false;
        if (showSpriteSheet_ && unlocked(Feature::Animation))
            drawSpriteSheet();
        if (showTilePainter_ && unlocked(Feature::Tilemap))
            drawTilePainter();
        if (showNativeCode_ && unlocked(Feature::NativeCode))
            drawNativeCode();
        drawScriptTabs();
        if (showSettings_)
            drawSettings();
        if (showLearn_)
            drawLearn();
        if (showReference_)
            drawReference();
        if (showExport_)
            drawExport();
        if (showHistory_)
            drawHistory();
        recordProfile();
        if (showProfiler_)
            drawProfiler();
        if (showFind_)
            drawFind();
        if (showLighting_)
            drawLighting();
        drawCommandPalette();
        handleShortcuts();
    }
    if (showPrefs_)
        drawPreferences();
    if (showLevels_ || levelUpTo_)
        drawLevels();
    drawKeepChangesDialog();
    drawNotification(dt);
    drawQuitDialog();

    if (!options_.screenshot.empty() && frameCount_ == options_.frames) {
        // Screenshot of the whole editor window is taken by main after rendering.
    }
}

void Editor::requestQuit() {
    bool unsavedScripts = false;
    for (auto& t : tabs_)
        unsavedScripts = unsavedScripts || t->modified;
    if ((dirty_ || unsavedScripts) && hasProject())
        confirmQuit_ = true;
    else
        quit_ = true;
}

void Editor::drawQuitDialog() {
    if (confirmQuit_) {
        ImGui::OpenPopup("Save changes?");
        confirmQuit_ = false;
    }
    if (ImGui::BeginPopupModal("Save changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("You have unsaved changes. Save them before closing?");
        ImGui::Spacing();
        if (ImGui::Button("Save and close", {150, 0})) {
            if (playing_)
                stop();
            saveScene();
            saveAllScripts();
            quit_ = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't save", {110, 0}))
            quit_ = true;
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {90, 0})) {
            window_.cancelClose();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void Editor::drawNotification(float dt) {
    if (notificationTime_ <= 0)
        return;
    notificationTime_ -= dt;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImVec2 textSize = ImGui::CalcTextSize(notification_.c_str(), nullptr, false, 520);
    ImVec2 size{textSize.x + 32, textSize.y + 20};
    ImVec2 pos{vp->Pos.x + vp->Size.x * 0.5f - size.x * 0.5f, vp->Pos.y + vp->Size.y - size.y - 40};
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    float a = std::min(1.0f, notificationTime_ * 2);
    fg->AddRectFilled(pos, {pos.x + size.x, pos.y + size.y},
                      notificationError_ ? IM_COL32(150, 40, 45, static_cast<int>(235 * a)) : IM_COL32(35, 42, 55, static_cast<int>(235 * a)), 8);
    fg->AddText(ImGui::GetFont(), ImGui::GetFontSize(), {pos.x + 16, pos.y + 10}, IM_COL32(255, 255, 255, static_cast<int>(255 * a)),
                notification_.c_str(), nullptr, 520);
}

} // namespace aven::editor
