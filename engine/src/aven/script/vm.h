#pragma once

#include "aven/core/json.h"
#include "aven/script/bytecode.h"
#include "aven/script/errors.h"
#include "aven/script/value.h"

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace aven::script {

// One running copy of a script, attached to one object. Script-level variables
// live here, so every enemy with enemy.es has its own `health`.
struct Instance {
    std::shared_ptr<Module> module;
    std::unordered_map<Symbol, Value> vars;
    std::vector<Symbol> order;
    std::unordered_map<Symbol, Value> initial; // values right after the script first ran
    Value self;
    std::string name; // object name, for error messages
    bool alive = true;

    Value* find(Symbol s);
    void set(Symbol s, Value v);
    std::vector<std::string> varNames() const;
};

struct FunctionObj : Obj {
    std::shared_ptr<Module> module;
    const FunctionProto* proto = nullptr;
    std::weak_ptr<Instance> instance;
    std::vector<Value> defaults;
};

struct CallFrame {
    const FunctionProto* proto = nullptr;
    size_t ip = 0;
    size_t base = 0; // stack slot of the callee; locals start at base + 1
    std::shared_ptr<Instance> instance;
    int debugLine = 0; // the debugger: the line this call is on (a breakpoint stops once per visit)
};

struct Task {
    std::vector<Value> stack;
    std::vector<CallFrame> frames;
    std::shared_ptr<Module> module; // keeps top-level code alive
    std::weak_ptr<Instance> owner;
    Symbol event = 0;
    bool canWait = true;
    bool waitRequested = false;
    double waitSeconds = 0;
    double wakeAt = 0;
    uint64_t waitFrame = 0;
    Value result;
};

// Where a paused script is, for the editor's debugger: one entry per function call, innermost first.
struct DebugFrame {
    std::string function; // "on_update", or "" for the top of the script
    std::string file;
    int line = 0;
    std::string object;                                     // the object it runs on
    std::vector<std::pair<std::string, std::string>> locals; // name, value as text
    std::vector<std::pair<std::string, std::string>> vars;   // script variables
};

class VM {
public:
    VM();
    ~VM();
    VM(const VM&) = delete;
    VM& operator=(const VM&) = delete;

    // Engine functions and objects visible to every script.
    void setGlobal(std::string_view name, Value value);
    void defineFunction(std::string_view name, std::string signature, int minArgs, int maxArgs, NativeFn fn);
    const Value* global(Symbol s) const;
    std::vector<std::string> globalNames() const;
    // The methods of lists, text, dictionaries and vectors (for editor tools).
    std::vector<const NativeFunctionObj*> methodsOf(Type t) const;

    // Compiles a script. Reports errors through onError and returns null on failure.
    std::shared_ptr<Module> compile(std::string_view source, const std::string& path);

    // Runs the script's top-level code for a new object. Null on error (reported).
    std::shared_ptr<Instance> createInstance(std::shared_ptr<Module> module, Value self, std::string name);

    // Swaps in edited code while the game runs. Variables the game changed keep their
    // values; variables still at their starting value pick up the new starting value.
    bool reload(const std::shared_ptr<Instance>& instance, std::shared_ptr<Module> module);

    struct Result {
        bool ok = true;
        bool waiting = false;
        Value value;
    };
    // Calls a function. If it uses wait(), it keeps running over the next frames.
    Result call(const Value& fn, std::vector<Value> args, std::shared_ptr<Instance> owner = nullptr, Symbol event = 0,
                bool allowWait = true);
    // Calls `name` on an instance if the script defines it.
    Result callFunction(const std::shared_ptr<Instance>& instance, Symbol name, std::vector<Value> args,
                        bool allowWait = true);
    // Synchronous call for native code (e.g. sorted(key=...)). Throws ScriptError.
    Value callNow(const Value& fn, std::vector<Value> args);

    bool isWaiting(const Instance* instance, Symbol event) const;
    void update(double dt);
    void cancelTasks(const Instance* instance);
    void clearTasks();
    size_t waitingCount() const { return waiting_.size(); }
    double time() const { return time_; }

    // --- used by native functions
    void wait(double seconds);
    int addTimer(double delay, double interval, Value fn, std::vector<Value> args);
    bool stopTimer(int id);
    Instance* currentInstance() const;
    std::shared_ptr<Instance> currentInstanceShared() const;
    int currentLine() const;
    std::string currentFile() const;

    Value getAttr(const Value& obj, Symbol name);
    void setAttr(const Value& obj, Symbol name, const Value& value);
    Value getIndex(const Value& obj, const Value& index);
    void setIndex(const Value& obj, const Value& index, const Value& value);
    Value binary(Op op, const Value& a, const Value& b);
    bool lessThan(const Value& a, const Value& b);
    int compareLists(const Value& a, const Value& b); // -1, 0 or 1
    bool contains(const Value& container, const Value& item);
    Value iterate(const Value& v); // returns an iterator value
    bool next(const Value& iterator, Value& out);

    static Value fromJson(const Json& j);
    static Json toJson(const Value& v);

    // import: which script a name means, from the script doing the import ("" if there's none),
    // and compiling it (errors reported as usual; null on failure). Set by the game.
    std::function<std::string(const std::string& fromFile, const std::string& name)> resolveImport;
    std::function<std::shared_ptr<Module>(const std::string& path)> loadImport;
    // The shared copy of an imported script (null if nothing imported it), e.g. to reload it.
    std::shared_ptr<Instance> importedInstance(const std::string& path) const;
    Value importModule(const std::string& name); // what `import name` does

    // --- the debugger. A breakpoint is a line of a script ("scripts/player.es", 12). Code that Aven
    // runs (an event like on_update, a timer, a message) pauses when it reaches one, the way wait()
    // does, until debugResume(); whoever runs the game stops it meanwhile. The top of a script and
    // helper calls (sorted(key=...)) run straight through.
    enum class Step { Continue, Over, Into, Out };
    void setBreakpoints(std::map<std::string, std::set<int>> lines);
    const std::map<std::string, std::set<int>>& breakpoints() const { return breakpoints_; }
    bool debugPaused() const { return paused_ != nullptr; }
    std::vector<DebugFrame> debugFrames() const; // empty unless paused
    // Carries on now: to the end (or the next breakpoint), or one line (Over: not into calls,
    // Into: into them, Out: back to the caller). Stepping past the end of the function stops at
    // the next line of script that runs.
    void debugResume(Step step);
    // Drops the paused code without running the rest of it, and stops stepping.
    void debugStop();

    std::function<void(const ScriptError&)> onError;
    std::function<void(const std::string& text, const std::string& file, int line)> onPrint;

    uint64_t instructionBudget = 20'000'000; // per resume, protects against endless loops
    int maxCallDepth = 200;
    // Script -> engine -> script calls inside each other (sorted(key=...) calling sorted, start_task
    // starting itself): each one is a C++ call, so they're limited to keep the stack safe.
    int maxNativeDepth = 48;

private:
    enum class Status { Done, Waiting, Paused };
    Status run(Task& task);
    bool debugStopsHere(Task& task);
    void updateDebugging() { debugging_ = !breakpoints_.empty() || step_ != Step::Continue; }
    void prepareCall(Task& task, size_t calleeIndex, int positional, int keywords, const std::vector<Symbol>* names);
    void callNative(Task& task, size_t calleeIndex, int positional, int keywords, const std::vector<Symbol>* names);
    Value loadName(const CallFrame& frame, Symbol s);
    void report(ScriptError& e, Task& task);
    Result runTask(std::unique_ptr<Task> task);
    std::vector<std::string> attrCandidates(const Value& obj) const;
    void registerBuiltins();

    std::unordered_map<Symbol, Value> globals_;
    // Imported scripts, one shared copy each (null while one is still running its top-level code).
    std::unordered_map<std::string, std::shared_ptr<Instance>> imports_;
    std::unordered_map<Symbol, Value> listMethods_, stringMethods_, dictMethods_, vecMethods_;
    std::vector<std::unique_ptr<Task>> waiting_;
    Task* current_ = nullptr;
    int nativeDepth_ = 0;
    struct NativeDepth; // counts nativeDepth_ while a call runs

    std::map<std::string, std::set<int>> breakpoints_;
    bool debugging_ = false; // breakpoints set or stepping: run() checks each new line
    std::unique_ptr<Task> paused_;
    Step step_ = Step::Continue;
    const Task* stepTask_ = nullptr; // the code being stepped through (null: whatever runs next)
    size_t stepDepth_ = 0;
    double time_ = 0;
    uint64_t frame_ = 0;

    struct Timer {
        int id;
        double fireAt;
        double interval; // 0 = once
        Value fn;
        std::vector<Value> args;
        std::weak_ptr<Instance> owner;
        bool hasOwner;
    };
    std::vector<Timer> timers_;
    int nextTimerId_ = 1;

    friend void registerStdlib(VM& vm);
    friend struct StdlibAccess;
};

void registerStdlib(VM& vm);

} // namespace aven::script
