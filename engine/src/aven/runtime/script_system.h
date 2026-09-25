#pragma once

#include "aven/ecs/registry.h"
#include "aven/math/math.h"
#include "aven/script/vm.h"

#include <chrono>
#include <map>
#include <memory>
#include <tuple>
#include <string>
#include <unordered_map>
#include <vector>

namespace aven {

class Game;
class NativeRuntime;

enum class Easing { Linear, EaseIn, EaseOut, EaseInOut, Bounce, Elastic, Back };
bool parseEasing(const std::string& name, Easing& out);
float applyEasing(Easing e, float t);

// Runs EasyScript (and block) scripts attached to entities, and exposes the
// engine to them: self.x, find(), spawn(), key_down(), play_sound()...
class ScriptSystem {
public:
    explicit ScriptSystem(Game& game);
    ~ScriptSystem();

    script::VM& vm() { return vm_; }
    Game& game() { return game_; }
    NativeRuntime& native() { return *native_; } // C/C++ behaviors (NativeScript)

    void start();
    void stop();
    void update(float dt);
    void fixedUpdate(float dt);

    // --- events from other systems
    void onCollision(Entity a, Entity b, bool begin, bool trigger);
    void onClick(Entity e);
    void onDestroy(Entity e);
    void onSpawn(Entity root); // creates instances for a new entity and its children
    void broadcast(const std::string& message, const script::Value& data);

    script::Value entityValue(Entity e);
    std::shared_ptr<script::Instance> instanceOf(Entity e) const;
    Entity entityFromValue(const script::Value& v) const; // null if not a live entity

    // Loads (and caches) a script file; .blocks files are converted to EasyScript first.
    std::shared_ptr<script::Module> module(const std::string& path, bool reportErrors = true);
    void checkForChanges();

    // --- named properties shared by scripts, blocks and tweens
    bool getProperty(Entity e, const std::string& name, script::Value& out);
    bool setProperty(Entity e, const std::string& name, const script::Value& value);
    static std::vector<std::string> propertyNames();
    static std::vector<std::string> methodNames();
    static std::vector<std::string> methodSignatures(); // how to call each one, e.g. self.move(dx, dy)

    void addTween(Entity e, const std::string& property, float target, float duration, Easing easing,
                  script::Value onDone);

    script::Value& gameData() { return gameData_; }
    // Shared game variables (game.score...) for engine features like behaviors.
    script::Value gameValue(const std::string& name) const; // null if unset
    void setGameValue(const std::string& name, const script::Value& v);
    double gameNumber(const std::string& name, double fallback = 0) const;
    void addToGameNumber(const std::string& name, double amount);
    float deltaTime() const { return deltaTime_; }

    // Converts a scripted value to a vector/color, raising a friendly error on bad input.
    static Vec3 toVec3(const script::Value& v, const char* context, Vec3 fallback = {});
    static Color toColor(const script::Value& v, const char* context);
    static script::Value fromVec3(Vec3 v, int components = 3);
    static script::Value fromColor(Color c);

private:
    Game& game_;
    script::VM vm_;
    std::unique_ptr<NativeRuntime> native_;
    script::Value gameData_;
    std::unordered_map<Entity, std::shared_ptr<script::Instance>> instances_;
    std::unordered_map<Entity, script::Value> entityValues_;
    std::vector<Entity> startOrder_;
    std::vector<Entity> pendingStart_;
    bool started_ = false;
    float deltaTime_ = 0;
    float reloadTimer_ = 0;

    struct CachedModule {
        std::shared_ptr<script::Module> module;
        int64_t modified = 0;
        bool failed = false;
    };
    std::unordered_map<std::string, CachedModule> modules_;

    struct Tween {
        Entity entity;
        std::string property;
        float from = 0, to = 0, duration = 1, elapsed = 0;
        Easing easing = Easing::EaseOut;
        script::Value onDone;
        bool started = false;
    };
    std::vector<Tween> tweens_;

    struct ErrorKey {
        std::string file;
        int line;
        std::string message;
        bool operator<(const ErrorKey& o) const {
            return std::tie(file, line, message) < std::tie(o.file, o.line, o.message);
        }
    };
    std::map<ErrorKey, int> errorCounts_;

    void registerApi();
    void attach(Entity e);
    void applyOverrides(Entity e, script::Instance& inst);
    void callAll(script::Symbol event, const std::vector<script::Value>& args, bool skipIfWaiting);
    void updateTweens(float dt);
    std::string loadSource(const std::string& path, bool& ok);
};

} // namespace aven
