#include "aven/runtime/native.h"

#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/components.h"
#include "aven/scene/scene.h"
#include "aven/script/vm.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <dlfcn.h>
#endif

namespace aven {

namespace stdfs = std::filesystem;
using script::Value;

namespace {

// The game whose native code is running (one at a time, in the player and in the editor).
ScriptSystem* gScripts = nullptr;
NativeRuntime* gRuntime = nullptr;
std::set<std::string> gReported;

void reportOnce(const std::string& text) {
    if (gReported.insert(text).second)
        Log::error(text);
}

// Text handed back to C stays valid for the next 15 calls that return text.
const char* keepText(std::string text) {
    static std::string ring[16];
    static unsigned next = 0;
    std::string& slot = ring[next++ % 16];
    slot = std::move(text);
    return slot.c_str();
}

bool ready(const char* function) {
    if (gScripts)
        return true;
    reportOnce(std::string("aven_") + function + "() was called while no game is running.");
    return false;
}

Entity entityOf(AvenEntity handle) {
    if (!gScripts || !handle)
        return {};
    Entity e = Entity::fromHandle(handle);
    return gScripts->game().scene().valid(e) ? e : Entity{};
}

double toNumber(const Value& v) {
    if (v.isNumber())
        return v.number();
    if (v.isBool())
        return v.boolean() ? 1 : 0;
    return 0;
}

std::string safe(const char* s) { return s ? s : ""; }

// Calls an EasyScript built-in, so C and EasyScript behave exactly the same.
Value callGlobal(const char* name, std::vector<Value> args) {
    const Value* fn = gScripts->vm().global(script::intern(name));
    if (!fn) {
        reportOnce(std::string("aven: the engine has no function called ") + name + "().");
        return {};
    }
    try {
        return gScripts->vm().callNow(*fn, std::move(args));
    } catch (const script::ScriptError& e) {
        reportOnce(std::string("aven_") + name + "(): " + e.what());
        return {};
    }
}

// ---- the API table

AvenBehavior* apiAddBehavior(AvenModule*, const char* name, size_t dataSize) {
    return NativeModules::get().addBehavior(safe(name), dataSize);
}

void apiAddProperty(AvenBehavior* behavior, const char* name, AvenPropertyType type, size_t offset, double value,
                    const char* tooltip) {
    NativeProperty p;
    p.name = safe(name);
    p.tooltip = safe(tooltip);
    p.type = type;
    p.offset = offset;
    p.defaultValue = value;
    NativeModules::get().addProperty(behavior, std::move(p));
}

void apiPrint(const char* text) { Log::info(safe(text)); }
void apiWarn(const char* text) { Log::warn(safe(text)); }
void apiError(const char* text) { Log::error(safe(text)); }

AvenEntity apiFind(const char* name) {
    if (!ready("find"))
        return 0;
    return gScripts->game().scene().findByName(safe(name)).toHandle();
}

int apiFindAll(const char* tag, AvenEntity* out, int max) {
    if (!ready("find_all"))
        return 0;
    Scene& scene = gScripts->game().scene();
    int n = 0;
    for (Entity e : scene.findAllWithTag(safe(tag))) {
        if (!scene.isActive(e))
            continue;
        if (out && n < max)
            out[n] = e.toHandle();
        ++n;
    }
    return n;
}

AvenEntity apiSpawn(const char* prefab, float x, float y, float z) {
    if (!ready("spawn"))
        return 0;
    Entity e = gScripts->game().spawnPrefab(safe(prefab), {x, y, z});
    if (!e)
        reportOnce("aven_spawn(): couldn't make a copy of '" + safe(prefab) + "'. Check the prefab's path.");
    return e.toHandle();
}

void apiDestroy(AvenEntity handle) {
    if (!ready("destroy"))
        return;
    if (Entity e = entityOf(handle))
        callGlobal("destroy", {gScripts->entityValue(e)});
}

int apiExists(AvenEntity handle) { return entityOf(handle) ? 1 : 0; }

const char* apiName(AvenEntity handle) {
    Entity e = entityOf(handle);
    return keepText(e ? gScripts->game().scene().info(e).name : "");
}

const char* apiTag(AvenEntity handle) {
    Entity e = entityOf(handle);
    return keepText(e ? gScripts->game().scene().info(e).tag : "");
}

bool readProperty(AvenEntity handle, const char* property, Value& out, const char* function) {
    if (!ready(function))
        return false;
    Entity e = entityOf(handle);
    if (!e) {
        reportOnce(std::string("aven_") + function + "(\"" + safe(property) + "\") was given an object that doesn't exist (any more).");
        return false;
    }
    try {
        if (gScripts->getProperty(e, safe(property), out))
            return true;
        reportOnce(std::string("aven_") + function + "(): objects have no property called \"" + safe(property) +
                   "\". Try \"x\", \"y\", \"angle\", \"velocity_x\", \"alpha\"...");
    } catch (const script::ScriptError& err) {
        reportOnce(std::string("aven_") + function + "(\"" + safe(property) + "\"): " + err.what());
    }
    return false;
}

void writeProperty(AvenEntity handle, const char* property, const Value& value, const char* function) {
    if (!ready(function))
        return;
    Entity e = entityOf(handle);
    if (!e) {
        reportOnce(std::string("aven_") + function + "(\"" + safe(property) + "\") was given an object that doesn't exist (any more).");
        return;
    }
    try {
        if (!gScripts->setProperty(e, safe(property), value))
            reportOnce(std::string("aven_") + function + "(): objects have no property called \"" + safe(property) + "\".");
    } catch (const script::ScriptError& err) {
        reportOnce(std::string("aven_") + function + "(\"" + safe(property) + "\"): " + err.what());
    }
}

double apiGet(AvenEntity e, const char* property) {
    Value v;
    return readProperty(e, property, v, "get") ? toNumber(v) : 0;
}

void apiSet(AvenEntity e, const char* property, double value) { writeProperty(e, property, Value(value), "set"); }

const char* apiGetText(AvenEntity e, const char* property) {
    Value v;
    if (!readProperty(e, property, v, "get_text"))
        return keepText("");
    return keepText(v.isString() ? v.string() : v.isNone() ? "" : v.toString());
}

void apiSetText(AvenEntity e, const char* property, const char* value) {
    writeProperty(e, property, Value(safe(value)), "set_text");
}

double apiCall(AvenEntity handle, const char* method, const double* args, int count) {
    if (!ready("call"))
        return 0;
    Entity e = entityOf(handle);
    if (!e) {
        reportOnce("aven_call(\"" + safe(method) + "\") was given an object that doesn't exist (any more).");
        return 0;
    }
    Value self = gScripts->entityValue(e), fn;
    if (!self.nativeObject().getAttr(gScripts->vm(), safe(method), fn) || !fn.isCallable()) {
        reportOnce("aven_call(): objects have no method called \"" + safe(method) + "\".");
        return 0;
    }
    std::vector<Value> values;
    for (int i = 0; i < count && args; ++i)
        values.emplace_back(args[i]);
    try {
        return toNumber(gScripts->vm().callNow(fn, std::move(values)));
    } catch (const script::ScriptError& err) {
        reportOnce("aven_call(\"" + safe(method) + "\"): " + err.what());
        return 0;
    }
}

int apiKey(const char* function, const char* key) {
    if (!ready(function))
        return 0;
    return callGlobal(function, {Value(safe(key))}).truthy() ? 1 : 0;
}
int apiKeyDown(const char* key) { return apiKey("key_down", key); }
int apiKeyPressed(const char* key) { return apiKey("key_pressed", key); }
int apiKeyReleased(const char* key) { return apiKey("key_released", key); }
int apiMouseDown() { return ready("mouse_down") && callGlobal("mouse_down", {}).truthy() ? 1 : 0; }
double apiMouseX() { return ready("mouse_x") ? toNumber(callGlobal("mouse_x", {})) : 0; }
double apiMouseY() { return ready("mouse_y") ? toNumber(callGlobal("mouse_y", {})) : 0; }

double apiGameGet(const char* name, double fallback) {
    return ready("game_get") ? gScripts->gameNumber(safe(name), fallback) : fallback;
}
void apiGameSet(const char* name, double value) {
    if (ready("game_set"))
        gScripts->setGameValue(safe(name), Value(value));
}

void apiSend(AvenEntity target, const char* message, double value) {
    if (!ready("send"))
        return;
    if (!target) {
        gScripts->broadcast(safe(message), Value(value)); // reaches native behaviors too
        return;
    }
    Entity e = entityOf(target);
    if (!e)
        return;
    // EasyScript receives it as a call to the function with that name, like self.send().
    if (auto inst = gScripts->instanceOf(e))
        gScripts->vm().callFunction(inst, script::intern(safe(message)), {Value(value)});
    if (gRuntime)
        gRuntime->message(e, safe(message), value);
}

void apiPlaySound(const char* path) {
    if (ready("play_sound"))
        callGlobal("play_sound", {Value(safe(path))});
}
void apiLoadScene(const char* path) {
    if (ready("load_scene"))
        callGlobal("load_scene", {Value(safe(path))});
}
double apiTime() { return ready("time") ? gScripts->game().time() : 0; }
double apiDeltaTime() { return ready("delta_time") ? gScripts->deltaTime() : 0; }
double apiRandom(double low, double high) {
    return ready("random") ? toNumber(callGlobal("random_range", {Value(low), Value(high)})) : low;
}

const AvenApi* engineApi() {
    static const AvenApi api = [] {
        AvenApi a{};
        a.version = AVEN_API_VERSION;
        a.size = sizeof(AvenApi);
        a.add_behavior = apiAddBehavior;
        a.add_property = apiAddProperty;
        a.print = apiPrint;
        a.warn = apiWarn;
        a.error = apiError;
        a.find = apiFind;
        a.find_all = apiFindAll;
        a.spawn = apiSpawn;
        a.destroy = apiDestroy;
        a.exists = apiExists;
        a.name = apiName;
        a.tag = apiTag;
        a.get = apiGet;
        a.set = apiSet;
        a.get_text = apiGetText;
        a.set_text = apiSetText;
        a.call = apiCall;
        a.key_down = apiKeyDown;
        a.key_pressed = apiKeyPressed;
        a.key_released = apiKeyReleased;
        a.mouse_down = apiMouseDown;
        a.mouse_x = apiMouseX;
        a.mouse_y = apiMouseY;
        a.game_get = apiGameGet;
        a.game_set = apiGameSet;
        a.send = apiSend;
        a.play_sound = apiPlaySound;
        a.load_scene = apiLoadScene;
        a.time = apiTime;
        a.delta_time = apiDeltaTime;
        a.random = apiRandom;
        return a;
    }();
    return &api;
}

// ---- shared libraries

void* openLibrary(const stdfs::path& path, std::string& error) {
#if defined(_WIN32)
    HMODULE h = LoadLibraryW(path.wstring().c_str());
    if (!h)
        error = "Windows couldn't load it (error " + std::to_string(GetLastError()) + ")";
    return reinterpret_cast<void*>(h);
#elif defined(__EMSCRIPTEN__)
    (void)path;
    error = "native modules don't run in web builds";
    return nullptr;
#else
    void* h = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!h) {
        const char* e = dlerror();
        error = e ? e : "unknown error";
    }
    return h;
#endif
}

void* findSymbol(void* library, const char* name) {
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(reinterpret_cast<HMODULE>(library), name));
#elif defined(__EMSCRIPTEN__)
    (void)library;
    (void)name;
    return nullptr;
#else
    return dlsym(library, name);
#endif
}

void closeLibrary(void* library) {
    if (!library)
        return;
#if defined(_WIN32)
    FreeLibrary(reinterpret_cast<HMODULE>(library));
#elif !defined(__EMSCRIPTEN__)
    dlclose(library);
#endif
}

std::map<std::string, int64_t> libraryFiles(const stdfs::path& projectDir) {
    std::map<std::string, int64_t> files;
    std::error_code ec;
    stdfs::path dir = NativeModules::binDir(projectDir);
    if (!stdfs::is_directory(dir, ec))
        return files;
    for (auto& entry : stdfs::directory_iterator(dir, ec))
        if (entry.is_regular_file(ec) && entry.path().extension() == NativeModules::libraryExtension())
            files[entry.path().string()] = fs::modifiedTime(entry.path());
    return files;
}

} // namespace

// ---------------------------------------------------------------- NativeModules

NativeModules& NativeModules::get() {
    static NativeModules instance;
    return instance;
}

const char* NativeModules::libraryExtension() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

bool NativeModules::changedOnDisk(const stdfs::path& projectDir) const {
    return !loadedOnce_ || projectDir != projectDir_ || libraryFiles(projectDir) != seen_;
}

bool NativeModules::refresh(const stdfs::path& projectDir) {
    if (activeGames_ > 0 || !changedOnDisk(projectDir))
        return false;
    loadAll(projectDir);
    return true;
}

void NativeModules::unloadAll() {
    if (activeGames_ > 0) {
        Log::warn("Native modules can't be unloaded while the game is running.");
        return;
    }
    behaviors_.clear();
    std::error_code ec;
    for (auto& lib : libraries_) {
        closeLibrary(lib.handle);
        if (lib.copy != lib.source)
            stdfs::remove(lib.copy, ec);
    }
    libraries_.clear();
    modules_.clear();
    loadedOnce_ = false;
}

void NativeModules::loadAll(const stdfs::path& projectDir) {
    unloadAll();
    projectDir_ = projectDir;
    loadedOnce_ = true;
    auto files = libraryFiles(projectDir);
    seen_ = files;
    if (files.empty())
        return;
    std::error_code ec;
    // Load copies, so the originals can be rebuilt while the editor has them open.
    stdfs::path copies = projectDir / ".aven" / "native";
    stdfs::create_directories(copies, ec);
    for (auto& old : stdfs::directory_iterator(copies, ec))
        stdfs::remove(old.path(), ec); // left over from last time (fails harmlessly if still in use)

    for (auto& [file, modified] : files) {
        stdfs::path source(file);
        Module m;
        m.file = "native/bin/" + source.filename().string();
        Library lib;
        lib.source = source;
        lib.modified = modified;
        lib.copy = copies / (source.stem().string() + "-" + std::to_string(++copyCounter_) + libraryExtension());
        stdfs::copy_file(source, lib.copy, stdfs::copy_options::overwrite_existing, ec);
        if (ec)
            lib.copy = source; // e.g. an installed game can't write its folder: load the original
        lib.handle = openLibrary(lib.copy, m.error);
        if (lib.handle) {
            auto entry = reinterpret_cast<AvenModuleEntry>(findSymbol(lib.handle, "aven_module_entry"));
            if (!entry) {
                m.error = "it has no AVEN_MODULE(setup) line, so Aven doesn't know what's inside";
            } else {
                size_t before = behaviors_.size();
                loadingModule_ = m.file;
                int version = entry(engineApi(), reinterpret_cast<AvenModule*>(this));
                loadingModule_.clear();
                if (version <= 0) {
                    m.error = "it was built for a newer version of Aven";
                    behaviors_.resize(before);
                } else {
                    m.behaviors = static_cast<int>(behaviors_.size() - before);
                }
            }
            if (!m.error.empty()) {
                closeLibrary(lib.handle);
                lib.handle = nullptr;
            }
        }
        if (lib.handle) {
            libraries_.push_back(std::move(lib));
            std::string names;
            for (auto& b : behaviors_)
                if (b->module == m.file)
                    names += (names.empty() ? "" : ", ") + b->name;
            Log::info("Loaded native module ", m.file, m.behaviors ? " (" + names + ")" : " (no behaviors)");
        } else {
            if (lib.copy != lib.source)
                stdfs::remove(lib.copy, ec);
            Log::error("The native module ", m.file, " didn't load: ", m.error);
        }
        modules_.push_back(std::move(m));
    }
}

AvenBehavior* NativeModules::addBehavior(const std::string& name, size_t dataSize) {
    static AvenBehavior ignored{};
    if (loadingModule_.empty()) {
        Log::error("aven_behavior(\"", name, "\") can only be used in the module's setup function.");
        ignored = {};
        return &ignored;
    }
    if (find(name))
        Log::warn("Two native behaviors are called '", name, "'; the first one is used.");
    auto info = std::make_unique<NativeBehaviorInfo>();
    info->name = name;
    info->module = loadingModule_;
    info->dataSize = dataSize;
    behaviors_.push_back(std::move(info));
    return &behaviors_.back()->callbacks;
}

void NativeModules::addProperty(AvenBehavior* behavior, NativeProperty p) {
    for (auto& b : behaviors_) {
        if (&b->callbacks != behavior)
            continue;
        size_t size = p.type == AVEN_PROPERTY_NUMBER ? sizeof(float) : sizeof(int32_t);
        if (p.offset + size > b->dataSize) {
            Log::error("The property '", p.name, "' of ", b->name, " doesn't fit in its data (", b->dataSize,
                       " bytes). Use offsetof() and pass sizeof() of your struct to aven_behavior().");
            return;
        }
        b->properties.push_back(std::move(p));
        return;
    }
    Log::error("aven_number/aven_integer/aven_flag need a behavior made by aven_behavior().");
}

const NativeBehaviorInfo* NativeModules::find(const std::string& name) const {
    for (auto& b : behaviors_)
        if (b->name == name)
            return b.get();
    return nullptr;
}

std::vector<const NativeBehaviorInfo*> NativeModules::behaviors() const {
    std::vector<const NativeBehaviorInfo*> out;
    for (auto& b : behaviors_)
        out.push_back(b.get());
    return out;
}

// ---------------------------------------------------------------- NativeRuntime

NativeRuntime::NativeRuntime(ScriptSystem& scripts) : scripts_(scripts) {}

NativeRuntime::~NativeRuntime() { stop(); }

void NativeRuntime::applyProperties(const NativeBehaviorInfo& info, void* data, const Json& overrides) {
    auto* bytes = static_cast<unsigned char*>(data);
    for (auto& p : info.properties) {
        double v = p.defaultValue;
        const Json& o = overrides[p.name];
        if (o.isNumber())
            v = o.asNumber();
        else if (o.isBool())
            v = o.asBool() ? 1 : 0;
        if (p.type == AVEN_PROPERTY_NUMBER) {
            float f = static_cast<float>(v);
            std::memcpy(bytes + p.offset, &f, sizeof f);
        } else {
            int32_t i = p.type == AVEN_PROPERTY_FLAG ? (v != 0 ? 1 : 0) : static_cast<int32_t>(std::llround(v));
            std::memcpy(bytes + p.offset, &i, sizeof i);
        }
    }
}

double NativeRuntime::readProperty(const NativeProperty& p, const void* data) {
    auto* bytes = static_cast<const unsigned char*>(data);
    if (p.type == AVEN_PROPERTY_NUMBER) {
        float f;
        std::memcpy(&f, bytes + p.offset, sizeof f);
        return f;
    }
    int32_t i;
    std::memcpy(&i, bytes + p.offset, sizeof i);
    return i;
}

void NativeRuntime::start() {
    stop();
    Game& game = scripts_.game();
    NativeModules& modules = NativeModules::get();
    modules.refresh(game.projectDir().empty() ? game.assets().root() : game.projectDir());
    started_ = true;
    ++modules.activeGames_;
    gScripts = &scripts_;
    gRuntime = this;
    gReported.clear();
    game.scene().walk([&](Entity e, int) {
        attach(e);
        return true;
    });
}

void NativeRuntime::stop() {
    instances_.clear();
    order_.clear();
    pendingStart_.clear();
    if (started_) {
        started_ = false;
        --NativeModules::get().activeGames_;
    }
    if (gRuntime == this) {
        gRuntime = nullptr;
        gScripts = nullptr;
    }
}

void NativeRuntime::attach(Entity e) {
    auto* ns = scripts_.game().scene().registry().tryGet<NativeScript>(e);
    if (!ns || ns->className.empty() || instances_.count(e))
        return;
    const NativeBehaviorInfo* info = NativeModules::get().find(ns->className);
    if (!info) {
#ifdef __EMSCRIPTEN__
        reportOnce("Native (C/C++) behaviors don't run in web builds, so '" + ns->className + "' (used by " +
                   scripts_.game().scene().info(e).name + ") does nothing here. It works in desktop builds.");
        return;
#endif
        reportOnce("There's no native behavior called '" + ns->className + "' (used by " +
                   scripts_.game().scene().info(e).name + "). Build your native module in Tools > Native Code, and "
                   "check the name given to aven_behavior().");
        return;
    }
    Instance inst;
    inst.info = info;
    size_t words = (std::max<size_t>(info->dataSize, 1) + sizeof(std::max_align_t) - 1) / sizeof(std::max_align_t);
    inst.data = std::make_unique<std::max_align_t[]>(words); // zeroed
    applyProperties(*info, inst.data.get(), ns->overrides);
    instances_.emplace(e, std::move(inst));
    order_.push_back(e);
    pendingStart_.push_back(e);
}

template <class Fn>
void NativeRuntime::forEach(Fn fn) {
    // Callbacks may spawn or destroy objects, so walk a copy and look each one up again.
    std::vector<Entity> order = order_;
    Scene& scene = scripts_.game().scene();
    for (Entity e : order) {
        auto it = instances_.find(e);
        if (it == instances_.end() || !scene.valid(e) || !scene.isActive(e))
            continue;
        fn(e, it->second);
    }
}

void NativeRuntime::update(float dt) {
    if (!started_)
        return;
    gScripts = &scripts_;
    gRuntime = this;
    while (!pendingStart_.empty()) {
        std::vector<Entity> batch = std::move(pendingStart_);
        pendingStart_.clear();
        for (Entity e : batch) {
            auto it = instances_.find(e);
            if (it != instances_.end() && it->second.info->callbacks.on_start)
                it->second.info->callbacks.on_start(e.toHandle(), it->second.data.get());
        }
    }
    forEach([&](Entity e, Instance& inst) {
        if (inst.info->callbacks.on_update)
            inst.info->callbacks.on_update(e.toHandle(), inst.data.get(), dt);
    });
}

void NativeRuntime::fixedUpdate(float dt) {
    forEach([&](Entity e, Instance& inst) {
        if (inst.info->callbacks.on_fixed_update)
            inst.info->callbacks.on_fixed_update(e.toHandle(), inst.data.get(), dt);
    });
}

void NativeRuntime::onSpawn(Entity root) {
    if (!started_)
        return;
    Scene& scene = scripts_.game().scene();
    std::function<void(Entity)> visit = [&](Entity e) {
        attach(e);
        for (Entity c : scene.children(e))
            visit(c);
    };
    visit(root);
}

void NativeRuntime::onDestroy(Entity e) {
    auto it = instances_.find(e);
    if (it == instances_.end())
        return;
    Instance inst = std::move(it->second);
    instances_.erase(it);
    std::erase(order_, e);
    std::erase(pendingStart_, e);
    if (inst.info->callbacks.on_destroy)
        inst.info->callbacks.on_destroy(e.toHandle(), inst.data.get());
}

void NativeRuntime::onCollision(Entity a, Entity b, bool begin, bool trigger) {
    Scene& scene = scripts_.game().scene();
    for (auto [self, other] : {std::pair{a, b}, std::pair{b, a}}) {
        auto it = instances_.find(self);
        if (it == instances_.end() || !scene.valid(self) || !scene.valid(other))
            continue;
        const AvenBehavior& cb = it->second.info->callbacks;
        auto fn = trigger ? (begin ? cb.on_trigger : cb.on_trigger_exit) : (begin ? cb.on_collide : cb.on_collide_end);
        // Like EasyScript: without on_trigger, touching a trigger counts as on_collide.
        if (!fn && trigger && begin)
            fn = cb.on_collide;
        if (fn)
            fn(self.toHandle(), it->second.data.get(), other.toHandle());
    }
}

void NativeRuntime::onClick(Entity e) {
    auto it = instances_.find(e);
    if (it != instances_.end() && it->second.info->callbacks.on_click)
        it->second.info->callbacks.on_click(e.toHandle(), it->second.data.get());
}

void NativeRuntime::onKeyPressed(const std::string& key) {
    forEach([&](Entity e, Instance& inst) {
        if (inst.info->callbacks.on_key_pressed)
            inst.info->callbacks.on_key_pressed(e.toHandle(), inst.data.get(), key.c_str());
    });
}

void NativeRuntime::message(Entity target, const std::string& text, double value) {
    if (target) {
        auto it = instances_.find(target);
        if (it != instances_.end() && it->second.info->callbacks.on_message)
            it->second.info->callbacks.on_message(target.toHandle(), it->second.data.get(), text.c_str(), value);
        return;
    }
    forEach([&](Entity e, Instance& inst) {
        if (inst.info->callbacks.on_message)
            inst.info->callbacks.on_message(e.toHandle(), inst.data.get(), text.c_str(), value);
    });
}

} // namespace aven
