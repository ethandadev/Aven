#include "editor.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"
#include "block_editor.h"
#include "code_editor.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <mutex>

namespace aven::editor {

namespace {
std::mutex g_consoleMutex;
std::vector<ConsoleLine> g_pendingLines;
} // namespace

Editor::Editor(Window& window, rhi::Device& device)
    : window_(window), device_(device), assets_(&device), scene_(std::make_unique<Scene>()) {}

Editor::~Editor() {
    if (playing_)
        stop();
    tabs_.clear();
    Log::removeSink(logSink_);
    renderer_.shutdown();
}

bool Editor::init(const EditorOptions& options) {
    options_ = options;
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
        if (options.openPanel == "settings")
            showSettings_ = true;
        if (options.openPanel == "reference")
            showReference_ = true;
        if (options.play) {
            for (auto& t : tabs_)
                t->focus = false;
            play();
        }
    }
    if (options.openPanel == "hub")
        showHub_ = true;
    return true;
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
            if (rel == "template.json")
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
    dirty_ = false;
    undo_.clear();
    redo_.clear();
    snapshotValid_ = false;
    selected_ = {};
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
    selected_ = {};
    refreshTitle();
}

std::string Editor::uniqueName(const std::string& folder, const std::string& base, const std::string& ext) const {
    for (int i = 0; i < 1000; ++i) {
        std::string name = folder + "/" + base + (i ? "_" + std::to_string(i + 1) : "") + ext;
        if (!fs::exists(projectDir_ / name))
            return name;
    }
    return folder + "/" + base + ext;
}

// ---------------------------------------------------------------- selection and undo

Entity Editor::selected() {
    return selected_ ? scene().findByUUID(selected_) : Entity{};
}

void Editor::select(Entity e) {
    selected_ = e ? scene().info(e).uuid : UUID{};
}

void Editor::recordUndo(const std::string& label) {
    if (playing_)
        return;
    undo_.push_back({label, scene_->save(), selected_});
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
        undo_.push_back({label, cachedSnapshot_, selected_});
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
    redo_.push_back({undo_.back().label, scene_->save(), selected_});
    Snapshot s = std::move(undo_.back());
    undo_.pop_back();
    scene_->load(s.scene);
    selected_ = s.selected;
    snapshotValid_ = false;
    dirty_ = true;
    notify("Undo: " + s.label);
    refreshTitle();
}

void Editor::redo() {
    if (playing_ || redo_.empty())
        return;
    undo_.push_back({redo_.back().label, scene_->save(), selected_});
    Snapshot s = std::move(redo_.back());
    redo_.pop_back();
    scene_->load(s.scene);
    selected_ = s.selected;
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
    if (clearOnPlay_)
        console_.clear();
    game_ = std::make_unique<Game>(assets_, gameInput_);
    game_->loadProject(projectDir_);
    game_->setCursorLocked = [this](bool locked) { window_.setCursorLocked(locked); };
    game_->setFullscreen = [this](bool) { notify("Fullscreen works when the game runs on its own (Build & Export)."); };
    game_->isFullscreen = [] { return false; };
    auto copy = std::make_unique<Scene>();
    copy->load(scene_->save());
    gameInput_.reset();
    game_->setScreenSize(viewportSize_);
    game_->start(std::move(copy), scenePath_);
    playing_ = true;
    paused_ = false;
    focusViewport_ = true;
}

void Editor::stop() {
    if (!playing_)
        return;
    game_->stop();
    game_.reset();
    playing_ = false;
    paused_ = false;
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
    s.info(e).name = kind == "Rounded Square" ? "RoundedSquare" : kind;
    selected_ = s.info(e).uuid;
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
    tab->code->completions = completions_;
    tab->code->highlightWords = apiWords_;
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
        setupDockspace();
        drawHierarchy();
        drawInspector();
        drawViewport(dt);
        drawAssets();
        drawConsole();
        drawScriptTabs();
        if (showSettings_)
            drawSettings();
        if (showLearn_)
            drawLearn();
        if (showReference_)
            drawReference();
        if (showExport_)
            drawExport();
        handleShortcuts();
    }
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

void Editor::setupDockspace() {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float toolbarHeight = ImGui::GetFrameHeight() + 14;
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
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, {vp->WorkSize.x, vp->WorkSize.y - toolbarHeight});
        ImGuiID left, right, bottom, center = dockId;
        left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.28f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Learn", right);
        ImGui::DockBuilderDockWindow("Assets", bottom);
        ImGui::DockBuilderDockWindow("###Console", bottom);
        ImGui::DockBuilderDockWindow("###Viewport", center);
        ImGui::DockBuilderFinish(dockId);
    }
    ImGui::DockSpace(dockId, {0, 0}, ImGuiDockNodeFlags_None);
    ImGui::End();
}

void Editor::drawMenuBar() {
    if (!ImGui::BeginMenuBar())
        return;
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
        if (ImGui::MenuItem("Save Scene", "Ctrl+S"))
            saveScene();
        ImGui::Separator();
        if (ImGui::MenuItem("Build & Export Game..."))
            showExport_ = true;
        if (ImGui::MenuItem("Project Settings..."))
            showSettings_ = true;
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
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, !undo_.empty() && !playing_))
            undo();
        if (ImGui::MenuItem("Redo", "Ctrl+Y", false, !redo_.empty() && !playing_))
            redo();
        ImGui::Separator();
        Entity sel = selected();
        if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, sel && !playing_)) {
            recordUndo("Duplicate");
            select(scene_->duplicate(sel));
        }
        if (ImGui::MenuItem("Delete", "Del", false, sel && !playing_)) {
            recordUndo("Delete");
            scene_->destroy(sel);
            selected_ = {};
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Create")) {
        bool disabled = playing_;
        ImGui::BeginDisabled(disabled);
        if (ImGui::BeginMenu("2D Shape"))
        {
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
        if (ImGui::BeginMenu("Light")) {
            for (const char* k : {"Sun", "Point Light", "Spot Light", "Environment"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("UI")) {
            for (const char* k : {"UI Text", "UI Button", "UI Panel"})
                if (ImGui::MenuItem(k))
                    createEntity(k);
            ImGui::EndMenu();
        }
        for (const char* k : {"Text", "Camera", "Particles", "Sound", "Player 3D"})
            if (ImGui::MenuItem(k))
                createEntity(k);
        if (ImGui::MenuItem("Empty Object"))
            createEntity("Entity");
        ImGui::EndDisabled();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("Learn (tutorial)", nullptr, &showLearn_, !tutorial_.isNull());
        ImGui::MenuItem("Scripting Reference", nullptr, &showReference_);
        ImGui::MenuItem("Show Grid", nullptr, &showGrid_);
        if (ImGui::MenuItem("Advanced Mode", nullptr, &settings_.advancedMode))
            settings_.save(projectDir_);
        if (ImGui::MenuItem("Reset Layout"))
            resetLayout_ = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("Scripting Reference", nullptr, &showReference_);
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
        ImGui::TextDisabled("Blocks -> EasyScript -> C/C++ -> C#");
        ImGui::Spacing();
        ImGui::TextDisabled("Renderer: %s", device_.description().c_str());
        if (ImGui::Button("Close", {120, 0}))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

// The toolbar lives inside the root window, between the menu bar and the dock space.
void Editor::drawToolbar() {
    float height = ImGui::GetFrameHeight() + 14;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10, 7});
    ImGui::BeginChild("##toolbar", {0, height}, ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();
    float b = ImGui::GetFrameHeight();
    // Tools
    if (ui::iconButton("move", ui::Move, "Move (W)", gizmoOp_ == 0, b))
        gizmoOp_ = 0;
    ImGui::SameLine();
    if (ui::iconButton("rotate", ui::Rotate, "Rotate (E)", gizmoOp_ == 1, b))
        gizmoOp_ = 1;
    ImGui::SameLine();
    if (ui::iconButton("scale", ui::Scale, "Scale (R)", gizmoOp_ == 2, b))
        gizmoOp_ = 2;
    ImGui::SameLine();
    if (ui::iconButton("snap", ui::Magnet, "Snap to grid (hold Ctrl)", snap_, b))
        snap_ = !snap_;
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
    if (ui::iconButton("pause", ui::Pause, "Pause", paused_, b))
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
    if (ImGui::Checkbox("Advanced", &settings_.advancedMode))
        settings_.save(projectDir_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show advanced components and settings (shaders, post-processing, C++ scripts...)");
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(34, 150, 90, 255));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(44, 175, 105, 255));
    if (ImGui::Button("Build & Export"))
        showExport_ = true;
    ImGui::PopStyleColor(2);
    rightWidth = ImGui::GetItemRectMax().x - rightStart;
    ImGui::EndChild();
}

void Editor::handleShortcuts() {
    ImGuiIO& io = ImGui::GetIO();
    bool ctrl = io.KeyCtrl || io.KeySuper;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        bool scriptFocused = false;
        for (auto& t : tabs_)
            if (t->modified)
                scriptFocused = true;
        saveAllScripts();
        if (!playing_)
            saveScene();
        else if (scriptFocused)
            notify("Scripts saved.");
    }
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_P, false))
        playing_ ? stop() : play();
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Q, false))
        requestQuit();
    if (io.WantTextInput || playing_)
        return;
    if (ctrl && ImGui::IsKeyPressed(ImGuiKey_Z, false) && !io.KeyShift)
        undo();
    if (ctrl && (ImGui::IsKeyPressed(ImGuiKey_Y, false) || (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z, false))))
        redo();
    Entity sel = selected();
    if (sel && ctrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
        recordUndo("Duplicate");
        select(scene_->duplicate(sel));
    }
    if (sel && ImGui::IsKeyPressed(ImGuiKey_Delete, false) && (viewportFocused_ || hierarchyFocused_)) {
        recordUndo("Delete");
        scene_->destroy(sel);
        selected_ = {};
    }
    if (viewportHovered_ && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
            gizmoOp_ = 0;
        if (ImGui::IsKeyPressed(ImGuiKey_E, false))
            gizmoOp_ = 1;
        if (ImGui::IsKeyPressed(ImGuiKey_R, false))
            gizmoOp_ = 2;
        if (ImGui::IsKeyPressed(ImGuiKey_F, false))
            focusSelected();
    }
}

} // namespace aven::editor
