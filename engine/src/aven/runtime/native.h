#pragma once

// Native modules: behaviors written in C or C++ against the C API in aven.h, built into a shared
// library in the project's native/bin folder and attached to objects with a NativeScript component.

#include "aven.h"
#include "aven/core/json.h"
#include "aven/ecs/registry.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace aven {

class ScriptSystem;

struct NativeProperty {
    std::string name, tooltip;
    AvenPropertyType type = AVEN_PROPERTY_NUMBER;
    size_t offset = 0;
    double defaultValue = 0;
};

struct NativeBehaviorInfo {
    std::string name;
    std::string module; // the library file it came from
    size_t dataSize = 0;
    AvenBehavior callbacks{};
    std::vector<NativeProperty> properties;
};

// Every loaded native library. Shared by the editor and the games it runs.
class NativeModules {
public:
    static NativeModules& get();

    struct Module {
        std::string file;
        std::string error; // why it didn't load
        int behaviors = 0;
    };

    // Loads the libraries in <projectDir>/native/bin when the project or any library changed since
    // the last call. Libraries are copied before loading, so they can be rebuilt while loaded.
    // Must not be called while a game is running native code. Returns true if anything (re)loaded.
    bool refresh(const std::filesystem::path& projectDir);
    void unloadAll();

    const NativeBehaviorInfo* find(const std::string& name) const;
    std::vector<const NativeBehaviorInfo*> behaviors() const;
    const std::vector<Module>& modules() const { return modules_; }
    bool running() const { return activeGames_ > 0; }

    static const char* libraryExtension(); // ".dll", ".so" or ".dylib"
    static std::filesystem::path binDir(const std::filesystem::path& projectDir) { return projectDir / "native" / "bin"; }

    // Used while a module's setup function runs.
    AvenBehavior* addBehavior(const std::string& name, size_t dataSize);
    void addProperty(AvenBehavior* behavior, NativeProperty property);

private:
    friend class NativeRuntime;
    struct Library {
        void* handle = nullptr;
        std::filesystem::path source, copy;
        int64_t modified = 0;
    };
    std::vector<Library> libraries_;
    std::vector<Module> modules_;
    std::vector<std::unique_ptr<NativeBehaviorInfo>> behaviors_;
    std::filesystem::path projectDir_;
    std::map<std::string, int64_t> seen_; // every library file and its time when last loaded
    std::string loadingModule_;
    int copyCounter_ = 0;
    int activeGames_ = 0;
    bool loadedOnce_ = false;

    bool changedOnDisk(const std::filesystem::path& projectDir) const;
    void loadAll(const std::filesystem::path& projectDir);
};

// Runs the NativeScript behaviors of one game. Owned by its ScriptSystem, which forwards events.
class NativeRuntime {
public:
    explicit NativeRuntime(ScriptSystem& scripts);
    ~NativeRuntime();

    void start();
    void stop();
    void update(float dt);
    void fixedUpdate(float dt);
    void onSpawn(Entity root);
    void onDestroy(Entity e);
    void onCollision(Entity a, Entity b, bool begin, bool trigger);
    void onClick(Entity e);
    void onKeyPressed(const std::string& key);
    void message(Entity e, const std::string& message, double value); // e null: everyone
    bool has(Entity e) const { return instances_.count(e) > 0; }

    // Property values for a behavior's data: the defaults, then the object's overrides.
    static void applyProperties(const NativeBehaviorInfo& info, void* data, const Json& overrides);
    static double readProperty(const NativeProperty& p, const void* data);

private:
    struct Instance {
        const NativeBehaviorInfo* info = nullptr;
        std::unique_ptr<std::max_align_t[]> data;
    };
    ScriptSystem& scripts_;
    std::unordered_map<Entity, Instance> instances_;
    std::vector<Entity> order_, pendingStart_;
    std::vector<std::string> reported_;
    bool started_ = false;

    void attach(Entity e);
    template <class Fn>
    void forEach(Fn fn);
};

} // namespace aven
