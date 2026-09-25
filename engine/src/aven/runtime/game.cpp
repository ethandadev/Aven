#include "aven/runtime/game.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/script_system.h"
#include "aven/runtime/systems.h"
#include "aven/script/stdlib.h"

#include <chrono>

namespace aven {

Game::Game(Assets& assets, Input& input)
    : assets_(assets), input_(input), scene_(std::make_unique<Scene>()) {
    scripts_ = std::make_unique<ScriptSystem>(*this);
    physics2D_ = std::make_unique<Physics2D>(*this);
    physics3D_ = std::make_unique<Physics3D>(*this);
    audio_ = std::make_unique<AudioSystem>(*this);
    gameplay_ = std::make_unique<GameplaySystems>(*this);
}

Game::~Game() {
    stopSystems();
}

bool Game::loadProject(const std::filesystem::path& dir) {
    std::string error;
    if (!settings_.load(dir, &error)) {
        Log::error(error);
        return false;
    }
    projectDir_ = dir;
    assets_.setRoot(dir);
    input_.loadActions(settings_.inputActions);
    return true;
}

bool Game::loadScene(const std::string& path) {
    auto text = fs::readText(assets_.resolve(path));
    if (!text) {
        Log::error("Can't find the scene '", path, "'.");
        return false;
    }
    std::string error;
    Json data = Json::parse(*text, &error);
    auto scene = std::make_unique<Scene>();
    if (!error.empty() || !scene->load(data, &error)) {
        Log::error("The scene '", path, "' couldn't be loaded: ", error);
        return false;
    }
    start(std::move(scene), path);
    return true;
}

void Game::start(std::unique_ptr<Scene> scene, const std::string& path) {
    stopSystems();
    scene_ = std::move(scene);
    scenePath_ = path;
    time_ = 0;
    fixedAccumulator_ = 0;
    paused_ = false;
    timeScale = 1.0f;
    prefabCache_.clear();
    scene_->updateTransforms();
    startSystems();
}

void Game::stop() {
    stopSystems();
}

void Game::startSystems() {
    physics2D_->start();
    physics3D_->start();
    gameplay_->start();
    audio_->start();
    scripts_->start();
    running_ = true;
}

void Game::stopSystems() {
    if (!running_)
        return;
    scripts_->stop();
    gameplay_->stop();
    audio_->stop();
    physics3D_->stop();
    physics2D_->stop();
    if (setCursorLocked)
        setCursorLocked(false);
    running_ = false;
}

void Game::update(float dt) {
    if (!running_)
        return;
    if (!pendingScene_.empty()) {
        std::string next = std::move(pendingScene_);
        pendingScene_.clear();
        loadScene(next);
    }
    float scaled = paused_ ? 0.0f : dt * timeScale;
    time_ += scaled;

    using Clock = std::chrono::steady_clock;
    auto ms = [](Clock::time_point a, Clock::time_point b) {
        return std::chrono::duration<float, std::milli>(b - a).count();
    };
    GameProfile prof;
    auto t0 = Clock::now();
    gameplay_->preUpdate(dt);
    auto t1 = Clock::now();
    scripts_->update(scaled);
    gameplay_->updateBehaviors(scaled);
    auto t2 = Clock::now();
    physics3D_->updateCharacters(scaled);
    auto t3 = Clock::now();
    prof.gameplay += ms(t0, t1);
    prof.scripts += ms(t1, t2);
    prof.physics += ms(t2, t3);

    const float fixedStep = 1.0f / 60.0f;
    fixedAccumulator_ += scaled;
    int steps = 0;
    while (fixedAccumulator_ >= fixedStep && steps < 5) {
        auto a = Clock::now();
        scripts_->fixedUpdate(fixedStep);
        auto b = Clock::now();
        physics2D_->step(fixedStep);
        physics3D_->step(fixedStep);
        auto c = Clock::now();
        prof.scripts += ms(a, b);
        prof.physics += ms(b, c);
        fixedAccumulator_ -= fixedStep;
        ++steps;
    }
    if (steps == 5)
        fixedAccumulator_ = 0; // the game fell behind; don't try to catch up forever

    auto t4 = Clock::now();
    gameplay_->update(scaled);
    auto t5 = Clock::now();
    audio_->update(dt);
    auto t6 = Clock::now();
    scene_->flushDestroyed();
    scene_->updateTransforms();
    auto t7 = Clock::now();
    prof.gameplay += ms(t4, t5) + ms(t6, t7);
    prof.audio = ms(t5, t6);
    prof.total = ms(t0, t7);
    prof.fixedSteps = steps;
    profile_ = prof;
}

CameraView Game::camera(float aspect) const {
    return SceneRenderer::sceneCamera(*scene_, aspect);
}

void Game::render(SceneRenderer& renderer, int width, int height, const RenderOptions& options) {
    screenSize_ = {static_cast<float>(width), static_cast<float>(height)};
    renderer.cameraShake = gameplay_->shakeOffset();
    CameraView cam = camera(static_cast<float>(width) / std::max(height, 1));
    renderer.render(*scene_, cam, width, height, options);
    renderer.cameraShake = {};
}

void Game::setRandomSeed(uint32_t seed) {
    gameplay_->seedRandom(seed);
    script::seedRandom(seed);
}

void Game::destroyEntity(Entity e) {
    if (!scene_->valid(e))
        return;
    std::function<void(Entity)> visit = [&](Entity x) {
        for (Entity c : std::vector<Entity>(scene_->children(x)))
            visit(c);
        scripts_->onDestroy(x);
        physics2D_->onDestroy(x);
        physics3D_->onDestroy(x);
        audio_->onDestroy(x);
    };
    visit(e);
    if (scene_->valid(e)) {
        scene_->info(e).active = false;
        scene_->destroyLater(e);
    }
}

Entity Game::spawnPrefab(const std::string& path, Vec3 position, Entity parent) {
    auto it = prefabCache_.find(path);
    if (it == prefabCache_.end()) {
        auto text = fs::readText(assets_.resolve(path));
        if (!text) {
            Log::error("Can't find the prefab '", path, "'.");
            return {};
        }
        std::string error;
        Json data = Json::parse(*text, &error);
        if (!error.empty()) {
            Log::error("The prefab '", path, "' is damaged: ", error);
            return {};
        }
        it = prefabCache_.emplace(path, std::move(data)).first;
    }
    auto roots = scene_->instantiate(it->second, parent);
    if (roots.empty())
        return {};
    Entity root = roots.front();
    auto& link = scene_->registry().getOrEmplace<PrefabInstance>(root);
    link.path = path;
    scene_->setWorldPosition(root, position);
    scene_->updateTransforms();
    scripts_->onSpawn(root);
    return root;
}

} // namespace aven
