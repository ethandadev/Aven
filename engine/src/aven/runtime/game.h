#pragma once

#include "aven/assets/assets.h"
#include "aven/platform/input.h"
#include "aven/render/scene_renderer.h"
#include "aven/runtime/project.h"
#include "aven/scene/scene.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace aven {

class ScriptSystem;
class Physics2D;
class Physics3D;
class AudioSystem;
class GameplaySystems;

// Runs a game: owns the current scene and every system that makes it move
// (scripts, physics, audio, animation, particles, UI). Used by both the
// standalone player and the editor's Play mode.
class Game {
public:
    Game(Assets& assets, Input& input);
    ~Game();
    Game(const Game&) = delete;
    Game& operator=(const Game&) = delete;

    bool loadProject(const std::filesystem::path& dir);
    ProjectSettings& settings() { return settings_; }
    const std::filesystem::path& projectDir() const { return projectDir_; }

    // Loads and starts a scene file (relative to the project folder).
    bool loadScene(const std::string& path);
    // Starts playing an already-loaded scene (the editor passes a copy of the scene being edited).
    void start(std::unique_ptr<Scene> scene, const std::string& path);
    void stop();
    bool running() const { return running_; }

    // Advances the game by dt seconds.
    void update(float dt);
    void render(SceneRenderer& renderer, int width, int height, const RenderOptions& options = {});

    Scene& scene() { return *scene_; }
    const std::string& scenePath() const { return scenePath_; }
    Assets& assets() { return assets_; }
    Input& input() { return input_; }
    float time() const { return time_; }
    CameraView camera(float aspect) const;
    Vec2 screenSize() const { return screenSize_; }
    void setScreenSize(Vec2 size) { screenSize_ = size; }

    ScriptSystem& scripts() { return *scripts_; }
    Physics2D& physics2D() { return *physics2D_; }
    Physics3D& physics3D() { return *physics3D_; }
    AudioSystem& audio() { return *audio_; }
    GameplaySystems& gameplay() { return *gameplay_; }

    // Requests from scripts that the host (player or editor) carries out.
    void requestSceneChange(const std::string& path) { pendingScene_ = path; }
    void requestQuit() { quitRequested_ = true; }
    bool quitRequested() const { return quitRequested_; }
    bool paused() const { return paused_; }
    void setPaused(bool p) { paused_ = p; }
    float timeScale = 1.0f;

    std::function<void(bool locked)> setCursorLocked;
    std::function<void(bool fullscreen)> setFullscreen;
    std::function<bool()> isFullscreen;

    // Destroys an entity the proper way (runs on_destroy, removes physics bodies).
    void destroyEntity(Entity e);
    // Creates a copy of a prefab file at a position. Returns the new root entity.
    Entity spawnPrefab(const std::string& path, Vec3 position, Entity parent = {});

private:
    Assets& assets_;
    Input& input_;
    ProjectSettings settings_;
    std::filesystem::path projectDir_;
    std::unique_ptr<Scene> scene_;
    std::string scenePath_;
    std::string pendingScene_;
    bool running_ = false;
    bool paused_ = false;
    bool quitRequested_ = false;
    float time_ = 0;
    float fixedAccumulator_ = 0;
    Vec2 screenSize_{1280, 720};
    std::unordered_map<std::string, Json> prefabCache_;

    std::unique_ptr<ScriptSystem> scripts_;
    std::unique_ptr<Physics2D> physics2D_;
    std::unique_ptr<Physics3D> physics3D_;
    std::unique_ptr<AudioSystem> audio_;
    std::unique_ptr<GameplaySystems> gameplay_;

    void startSystems();
    void stopSystems();
};

} // namespace aven
