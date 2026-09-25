#include "aven/script/vm.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aven::script {

namespace {

// Marks local variable slots that haven't been assigned yet.
struct UnboundObj : Obj {};
const std::shared_ptr<Obj>& unboundMarker() {
    static const std::shared_ptr<Obj> marker = std::make_shared<UnboundObj>();
    return marker;
}
Value unbound() { return Value(Type::None, unboundMarker()); }
bool isUnbound(const Value& v) { return v.isNone() && v.obj() == unboundMarker(); }

struct TaskGuard {
    Task*& slot;
    Task* saved;
    TaskGuard(Task*& s, Task* t) : slot(s), saved(s) { slot = t; }
    ~TaskGuard() { slot = saved; }
};

std::string plural(size_t n, const char* word) {
    return std::to_string(n) + " " + word + (n == 1 ? "" : "s");
}

long long toIndex(const Value& v, const char* what) {
    if (!v.isNumber())
        raise(std::string(what) + " positions must be whole numbers, but got " + v.typeDescription() + ".");
    double d = v.number();
    if (d != std::floor(d))
        raise(std::string(what) + " positions must be whole numbers, but got " + formatNumber(d) + ".");
    return static_cast<long long>(d);
}

size_t resolveIndex(long long i, size_t size, const char* what) {
    long long n = static_cast<long long>(size);
    long long j = i < 0 ? i + n : i;
    if (j < 0 || j >= n) {
        if (n == 0)
            raise(std::string("The ") + what + " is empty, so there's nothing at position " + std::to_string(i) + ".");
        raise(std::string("The ") + what + " has " + plural(size, "item") + ", so position " + std::to_string(i) +
              " doesn't exist (positions go from 0 to " + std::to_string(n - 1) + ").");
    }
    return static_cast<size_t>(j);
}

const char* opSymbol(Op op) {
    switch (op) {
    case Op::Add: return "+";
    case Op::Sub: return "-";
    case Op::Mul: return "*";
    case Op::Div: return "/";
    case Op::FloorDiv: return "//";
    case Op::Mod: return "%";
    case Op::Pow: return "**";
    case Op::Lt: return "<";
    case Op::Gt: return ">";
    case Op::LtEq: return "<=";
    case Op::GtEq: return ">=";
    default: return "?";
    }
}

std::string formatWithSpec(const Value& v, const std::string& spec) {
    // Mini format spec: [0][width][.precision][f|d|%]
    size_t i = 0;
    bool zero = false;
    int width = 0, precision = -1;
    char type = 0;
    if (i < spec.size() && spec[i] == '0') {
        zero = true;
        ++i;
    }
    while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
        width = width * 10 + (spec[i++] - '0');
    if (i < spec.size() && spec[i] == '.') {
        ++i;
        precision = 0;
        while (i < spec.size() && std::isdigit(static_cast<unsigned char>(spec[i])))
            precision = precision * 10 + (spec[i++] - '0');
    }
    if (i < spec.size())
        type = spec[i++];
    if (i != spec.size())
        raise("I don't understand the format ':" + spec + "'. Try ':.2f' for 2 decimals or ':03' to pad with zeros.");
    std::string out;
    if (v.isNumber() || v.isBool()) {
        double n = v.isBool() ? (v.boolean() ? 1 : 0) : v.number();
        char buf[64];
        if (type == '%') {
            std::snprintf(buf, sizeof buf, "%.*f%%", precision < 0 ? 0 : precision, n * 100);
            out = buf;
        } else if (type == 'd' || (precision < 0 && type == 0 && n == std::floor(n))) {
            std::snprintf(buf, sizeof buf, "%.0f", std::trunc(n));
            out = buf;
        } else if (precision >= 0 || type == 'f') {
            std::snprintf(buf, sizeof buf, "%.*f", precision < 0 ? 6 : precision, n);
            out = buf;
        } else {
            out = formatNumber(n);
        }
    } else {
        out = v.toString();
    }
    if (static_cast<int>(out.size()) < width) {
        size_t pad = static_cast<size_t>(width) - out.size();
        if (zero && (v.isNumber()) && !out.empty() && out[0] == '-')
            out = "-" + std::string(pad, '0') + out.substr(1);
        else
            out = (zero ? std::string(pad, '0') : std::string(pad, ' ')) + out;
    }
    return out;
}

} // namespace

// ---------------------------------------------------------------- Instance

Value* Instance::find(Symbol s) {
    auto it = vars.find(s);
    return it == vars.end() ? nullptr : &it->second;
}

void Instance::set(Symbol s, Value v) {
    auto it = vars.find(s);
    if (it == vars.end()) {
        vars.emplace(s, std::move(v));
        order.push_back(s);
    } else {
        it->second = std::move(v);
    }
}

std::vector<std::string> Instance::varNames() const {
    std::vector<std::string> out;
    for (Symbol s : order)
        out.push_back(symbolName(s));
    return out;
}

// ---------------------------------------------------------------- VM setup

VM::VM() {
    registerBuiltins();
    registerStdlib(*this);
}

VM::~VM() = default;

void VM::setGlobal(std::string_view name, Value value) {
    globals_[intern(name)] = std::move(value);
}

void VM::defineFunction(std::string_view name, std::string signature, int minArgs, int maxArgs, NativeFn fn) {
    setGlobal(name, makeNative(std::string(name), std::move(signature), minArgs, maxArgs, std::move(fn)));
}

const Value* VM::global(Symbol s) const {
    auto it = globals_.find(s);
    return it == globals_.end() ? nullptr : &it->second;
}

std::vector<std::string> VM::globalNames() const {
    std::vector<std::string> out;
    for (auto& [s, v] : globals_)
        out.push_back(symbolName(s));
    std::sort(out.begin(), out.end());
    return out;
}

std::shared_ptr<Module> VM::compile(std::string_view source, const std::string& path) {
    try {
        return compileModule(source, path);
    } catch (ScriptError& e) {
        e.file = path;
        if (onError)
            onError(e);
        return nullptr;
    }
}

// ---------------------------------------------------------------- running

void VM::report(ScriptError& e, Task& task) {
    if (e.line == 0 && !task.frames.empty()) {
        const CallFrame& f = task.frames.back();
        size_t ip = f.ip > 0 ? f.ip - 1 : 0;
        if (ip < f.proto->lines.size())
            e.line = f.proto->lines[ip];
        e.file = f.proto->module ? f.proto->module->path : "";
        std::string trace;
        for (auto it = task.frames.rbegin(); it != task.frames.rend(); ++it) {
            size_t fip = it->ip > 0 ? it->ip - 1 : 0;
            int line = fip < it->proto->lines.size() ? it->proto->lines[fip] : 0;
            std::string where = it->proto->isTopLevel ? "the top of the script" : it->proto->name + "()";
            trace += "  in " + where + ", line " + std::to_string(line);
            if (it->instance && !it->instance->name.empty())
                trace += " (on '" + it->instance->name + "')";
            trace += "\n";
        }
        e.trace = trace;
    }
}

VM::Result VM::runTask(std::unique_ptr<Task> task) {
    try {
        Status s = run(*task);
        if (s == Status::Waiting) {
            waiting_.push_back(std::move(task));
            return {true, true, {}};
        }
        return {true, false, task->result};
    } catch (ScriptError& e) {
        report(e, *task);
        if (onError)
            onError(e);
        return {false, false, {}};
    }
}

std::shared_ptr<Instance> VM::createInstance(std::shared_ptr<Module> module, Value self, std::string name) {
    if (!module || module->functions.empty())
        return nullptr;
    auto inst = std::make_shared<Instance>();
    inst->module = module;
    inst->self = std::move(self);
    inst->name = std::move(name);
    auto task = std::make_unique<Task>();
    task->module = module;
    task->canWait = false;
    task->stack.push_back(Value());
    task->frames.push_back({module->functions[0].get(), 0, 0, inst});
    Result r = runTask(std::move(task));
    if (!r.ok)
        return nullptr;
    for (Symbol s : inst->order) {
        const Value& v = inst->vars[s];
        if (v.type() != Type::Function)
            inst->initial[s] = v;
    }
    return inst;
}

bool VM::reload(const std::shared_ptr<Instance>& instance, std::shared_ptr<Module> module) {
    Instance& inst = *instance;
    auto fresh = createInstance(module, inst.self, inst.name);
    if (!fresh)
        return false;
    cancelTasks(&inst);
    for (Symbol s : fresh->order) {
        Value v = fresh->vars[s];
        if (v.type() == Type::Function) {
            // Rebind the new code to the existing object.
            auto fn = std::make_shared<FunctionObj>(*v.as<FunctionObj>());
            fn->instance = instance;
            inst.set(s, Value(Type::Function, fn));
            continue;
        }
        Value* current = inst.find(s);
        auto initial = inst.initial.find(s);
        bool untouched = !current || (initial != inst.initial.end() && *current == initial->second);
        if (untouched)
            inst.set(s, v);
    }
    inst.initial = fresh->initial;
    inst.module = module;
    return true;
}

VM::Result VM::call(const Value& fn, std::vector<Value> args, std::shared_ptr<Instance> owner, Symbol event,
                    bool allowWait) {
    auto task = std::make_unique<Task>();
    task->owner = owner;
    task->event = event;
    task->canWait = allowWait;
    task->stack.push_back(fn);
    for (auto& a : args)
        task->stack.push_back(std::move(a));
    int argc = static_cast<int>(task->stack.size() - 1);
    try {
        TaskGuard guard(current_, task.get());
        if (fn.type() == Type::Function) {
            prepareCall(*task, 0, argc, 0, nullptr);
        } else {
            callNative(*task, 0, argc, 0, nullptr);
            return {true, false, task->stack.back()};
        }
    } catch (ScriptError& e) {
        report(e, *task);
        if (onError)
            onError(e);
        return {false, false, {}};
    }
    return runTask(std::move(task));
}

VM::Result VM::callFunction(const std::shared_ptr<Instance>& instance, Symbol name, std::vector<Value> args,
                            bool allowWait) {
    if (!instance || !instance->alive)
        return {true, false, {}};
    Value* fn = instance->find(name);
    if (!fn || fn->type() != Type::Function)
        return {true, false, {}};
    // Callbacks can be written with or without parameters: def on_update(): also works.
    const FunctionProto* proto = fn->as<FunctionObj>()->proto;
    if (args.size() > proto->params.size())
        args.resize(proto->params.size());
    return call(*fn, std::move(args), instance, name, allowWait);
}

Value VM::callNow(const Value& fn, std::vector<Value> args) {
    Task task;
    task.canWait = false;
    task.stack.push_back(fn);
    for (auto& a : args)
        task.stack.push_back(std::move(a));
    int argc = static_cast<int>(task.stack.size() - 1);
    try {
        TaskGuard guard(current_, &task);
        if (fn.type() != Type::Function) {
            callNative(task, 0, argc, 0, nullptr);
            return task.stack.back();
        }
        prepareCall(task, 0, argc, 0, nullptr);
        run(task);
        return task.result;
    } catch (ScriptError& e) {
        report(e, task);
        throw;
    }
}

bool VM::isWaiting(const Instance* instance, Symbol event) const {
    for (auto& t : waiting_)
        if (t->event == event && t->owner.lock().get() == instance)
            return true;
    return false;
}

void VM::update(double dt) {
    time_ += dt;
    ++frame_;

    // Timers created by after() and every().
    std::vector<Timer> due;
    for (auto it = timers_.begin(); it != timers_.end();) {
        bool dead = it->hasOwner && (it->owner.expired() || !it->owner.lock()->alive);
        if (dead) {
            it = timers_.erase(it);
        } else if (it->fireAt <= time_) {
            due.push_back(*it);
            if (it->interval > 0) {
                it->fireAt += it->interval;
                if (it->fireAt <= time_)
                    it->fireAt = time_ + it->interval;
                ++it;
            } else {
                it = timers_.erase(it);
            }
        } else {
            ++it;
        }
    }
    for (auto& t : due)
        call(t.fn, t.args, t.owner.lock());

    std::vector<std::unique_ptr<Task>> ready;
    for (auto it = waiting_.begin(); it != waiting_.end();) {
        Task& t = **it;
        auto owner = t.owner.lock();
        if (owner && !owner->alive) {
            it = waiting_.erase(it);
        } else if (frame_ > t.waitFrame && time_ >= t.wakeAt) {
            ready.push_back(std::move(*it));
            it = waiting_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& t : ready)
        runTask(std::move(t));
}

void VM::cancelTasks(const Instance* instance) {
    std::erase_if(waiting_, [&](const std::unique_ptr<Task>& t) {
        auto owner = t->owner.lock();
        if (owner.get() == instance)
            return true;
        for (auto& f : t->frames)
            if (f.instance.get() == instance)
                return true;
        return false;
    });
    std::erase_if(timers_, [&](const Timer& t) { return t.hasOwner && t.owner.lock().get() == instance; });
}

void VM::clearTasks() {
    waiting_.clear();
    timers_.clear();
}

void VM::wait(double seconds) {
    if (!current_)
        raise("wait() can only be used while a script is running.");
    if (!current_->canWait)
        raise("wait() can't be used here. Use it inside functions like on_start() or on_update(), not at the "
              "top of the script or inside helper calls like sorted().");
    current_->waitRequested = true;
    current_->waitSeconds = std::max(0.0, seconds);
}

int VM::addTimer(double delay, double interval, Value fn, std::vector<Value> args) {
    Timer t;
    t.id = nextTimerId_++;
    t.fireAt = time_ + std::max(0.0, delay);
    t.interval = interval;
    t.fn = std::move(fn);
    t.args = std::move(args);
    Instance* inst = currentInstance();
    t.hasOwner = false;
    if (inst && current_) {
        for (auto it = current_->frames.rbegin(); it != current_->frames.rend(); ++it)
            if (it->instance.get() == inst) {
                t.owner = it->instance;
                t.hasOwner = true;
                break;
            }
    }
    timers_.push_back(std::move(t));
    return timers_.back().id;
}

bool VM::stopTimer(int id) {
    return std::erase_if(timers_, [&](const Timer& t) { return t.id == id; }) > 0;
}

Instance* VM::currentInstance() const {
    return current_ && !current_->frames.empty() ? current_->frames.back().instance.get() : nullptr;
}

std::shared_ptr<Instance> VM::currentInstanceShared() const {
    return current_ && !current_->frames.empty() ? current_->frames.back().instance : nullptr;
}

int VM::currentLine() const {
    if (!current_ || current_->frames.empty())
        return 0;
    const CallFrame& f = current_->frames.back();
    size_t ip = f.ip > 0 ? f.ip - 1 : 0;
    return ip < f.proto->lines.size() ? f.proto->lines[ip] : 0;
}

std::string VM::currentFile() const {
    if (!current_ || current_->frames.empty() || !current_->frames.back().proto->module)
        return "";
    return current_->frames.back().proto->module->path;
}

// ---------------------------------------------------------------- calls

void VM::prepareCall(Task& task, size_t calleeIndex, int positional, int keywords, const std::vector<Symbol>* names) {
    auto& st = task.stack;
    auto* fn = st[calleeIndex].as<FunctionObj>();
    auto inst = fn->instance.lock();
    if (!inst)
        raise("This function belonged to an object that has been destroyed, so it can't run anymore.");
    const FunctionProto& p = *fn->proto;
    const size_t paramCount = p.params.size();
    std::string fname = p.name + "()";
    if (static_cast<size_t>(positional) > paramCount) {
        raise(fname + " takes " + plural(paramCount, "value") + ", but got " + std::to_string(positional) + ".");
    }
    std::vector<Value> locals(static_cast<size_t>(p.numLocals), unbound());
    for (int i = 0; i < positional; ++i)
        locals[static_cast<size_t>(i)] = st[calleeIndex + 1 + static_cast<size_t>(i)];
    for (int k = 0; k < keywords; ++k) {
        Symbol name = (*names)[static_cast<size_t>(k)];
        auto it = std::find(p.params.begin(), p.params.end(), name);
        if (it == p.params.end()) {
            std::vector<std::string> candidates;
            for (Symbol s : p.params)
                candidates.push_back(symbolName(s));
            raise(fname + " doesn't have a value called '" + symbolName(name) + "'." +
                  didYouMean(symbolName(name), candidates));
        }
        size_t j = static_cast<size_t>(it - p.params.begin());
        if (!isUnbound(locals[j]))
            raise(fname + " got the value '" + symbolName(name) + "' twice.");
        locals[j] = st[calleeIndex + 1 + static_cast<size_t>(positional + k)];
    }
    for (size_t j = 0; j < paramCount; ++j) {
        if (!isUnbound(locals[j]))
            continue;
        if (static_cast<int>(j) >= p.requiredParams)
            locals[j] = fn->defaults[j - static_cast<size_t>(p.requiredParams)];
        else
            raise(fname + " is missing a value for '" + symbolName(p.params[j]) + "'.");
    }
    if (static_cast<int>(task.frames.size()) >= maxCallDepth)
        raise("Functions called each other too many times (more than " + std::to_string(maxCallDepth) +
              " deep). Does a function keep calling itself forever?");
    st.resize(calleeIndex + 1);
    for (auto& l : locals)
        st.push_back(std::move(l));
    task.frames.push_back({&p, 0, calleeIndex, std::move(inst)});
}

void VM::callNative(Task& task, size_t calleeIndex, int positional, int keywords, const std::vector<Symbol>* names) {
    auto& st = task.stack;
    Value callee = st[calleeIndex];
    Value self;
    NativeFunctionObj* nf = nullptr;
    if (callee.type() == Type::NativeFunction) {
        nf = callee.as<NativeFunctionObj>();
    } else if (callee.type() == Type::BoundMethod) {
        auto* bm = callee.as<BoundMethodObj>();
        self = bm->self;
        if (bm->method.type() == Type::Function) {
            // Script method: call it with its own instance (e.g. other.take_damage(5)).
            st[calleeIndex] = bm->method;
            prepareCall(task, calleeIndex, positional, keywords, names);
            return;
        }
        nf = bm->method.as<NativeFunctionObj>();
    } else {
        std::string what = callee.typeDescription();
        if (callee.isNone())
            raise("Tried to call something that is None (nothing). Did a name get misspelled, or did a "
                  "function return nothing?");
        raise("This is " + what + " (" + callee.repr() + "), not a function, so it can't be called with ( ).");
    }
    int maxA = nf->maxArgs;
    if (positional < nf->minArgs || (maxA >= 0 && positional > maxA)) {
        std::string need;
        if (nf->minArgs == maxA)
            need = maxA == 0 ? "doesn't take any values" : "needs " + plural(static_cast<size_t>(maxA), "value");
        else if (maxA < 0)
            need = "needs at least " + plural(static_cast<size_t>(nf->minArgs), "value");
        else
            need = "needs " + std::to_string(nf->minArgs) + " to " + std::to_string(maxA) + " values";
        raise(nf->name + "() " + need + ", but got " + std::to_string(positional) + "." +
              (nf->signature.empty() ? "" : " Use it like: " + nf->signature));
    }
    CallArgs args{*this, nf->name.c_str(), {}, {}};
    args.args.reserve(static_cast<size_t>(positional) + 1);
    if (callee.type() == Type::BoundMethod)
        args.args.push_back(self);
    for (int i = 0; i < positional; ++i)
        args.args.push_back(st[calleeIndex + 1 + static_cast<size_t>(i)]);
    for (int k = 0; k < keywords; ++k)
        args.kwargs.emplace_back((*names)[static_cast<size_t>(k)],
                                 st[calleeIndex + 1 + static_cast<size_t>(positional + k)]);
    Value result = nf->fn(args);
    st.resize(calleeIndex);
    st.push_back(std::move(result));
}

Value VM::loadName(const CallFrame& frame, Symbol s) {
    if (frame.instance)
        if (Value* v = frame.instance->find(s))
            return *v;
    if (const Value* g = global(s))
        return *g;
    const std::string& name = symbolName(s);
    if (frame.instance && frame.instance->module && frame.instance->module->scriptVariables.count(s))
        raise("'" + name + "' doesn't have a value yet. Give it a starting value at the top of the script, like: " +
              name + " = 0");
    if (name == "this")
        raise("EasyScript uses 'self' instead of 'this'.");
    std::vector<std::string> candidates = globalNames();
    if (frame.instance)
        for (auto& n : frame.instance->varNames())
            candidates.push_back(n);
    for (Symbol l : frame.proto->localNames)
        candidates.push_back(symbolName(l));
    raise("I don't know what '" + name + "' is." + didYouMean(name, candidates));
}

// ---------------------------------------------------------------- main loop

VM::Status VM::run(Task& task) {
    TaskGuard guard(current_, &task);
    uint64_t budget = instructionBudget;
    auto& st = task.stack;
    auto pop = [&st]() {
        Value v = std::move(st.back());
        st.pop_back();
        return v;
    };
    auto tooLong = [] {
        raise("This script ran for too long without stopping, so it was paused to keep the game from "
              "freezing. Is there a 'while' loop that never ends? Add wait() inside it, or use on_update().");
    };

    while (true) {
        CallFrame& f = task.frames.back();
        const Instr& in = f.proto->code[f.ip++];
        switch (in.op) {
        case Op::Const: st.push_back(f.proto->constants[static_cast<size_t>(in.a)]); break;
        case Op::PushNone: st.emplace_back(); break;
        case Op::PushTrue: st.emplace_back(true); break;
        case Op::PushFalse: st.emplace_back(false); break;
        case Op::Pop: st.pop_back(); break;
        case Op::Dup: {
            Value v = st.back();
            st.push_back(std::move(v));
            break;
        }
        case Op::Dup2: {
            Value a = st[st.size() - 2], b = st.back();
            st.push_back(std::move(a));
            st.push_back(std::move(b));
            break;
        }
        case Op::Rot: {
            size_t from = st.size() - 1 - static_cast<size_t>(in.a);
            Value v = std::move(st[from]);
            st.erase(st.begin() + static_cast<std::ptrdiff_t>(from));
            st.push_back(std::move(v));
            break;
        }
        case Op::LoadLocal: {
            Value v = st[f.base + 1 + static_cast<size_t>(in.a)];
            if (isUnbound(v)) {
                const std::string& n = symbolName(f.proto->localNames[static_cast<size_t>(in.a)]);
                raise("'" + n + "' is used before it has been given a value. Set it first, like: " + n + " = 0");
            }
            st.push_back(std::move(v));
            break;
        }
        case Op::StoreLocal: st[f.base + 1 + static_cast<size_t>(in.a)] = pop(); break;
        case Op::LoadName: {
            Value v = loadName(f, static_cast<Symbol>(in.a));
            st.push_back(std::move(v));
            break;
        }
        case Op::StoreName: {
            if (!f.instance)
                raise("Can't store '" + symbolName(static_cast<Symbol>(in.a)) + "' here.");
            f.instance->set(static_cast<Symbol>(in.a), pop());
            break;
        }
        case Op::LoadSelf: {
            if (!f.instance || f.instance->self.isNone())
                raise("'self' isn't available here because this script isn't attached to an object.");
            st.push_back(f.instance->self);
            break;
        }
        case Op::LoadAttr: {
            Value obj = pop();
            st.push_back(getAttr(obj, static_cast<Symbol>(in.a)));
            break;
        }
        case Op::StoreAttr: {
            Value v = pop();
            Value obj = pop();
            setAttr(obj, static_cast<Symbol>(in.a), v);
            break;
        }
        case Op::LoadIndex: {
            Value idx = pop();
            Value obj = pop();
            st.push_back(getIndex(obj, idx));
            break;
        }
        case Op::StoreIndex: {
            Value v = pop();
            Value idx = pop();
            Value obj = pop();
            setIndex(obj, idx, v);
            break;
        }
        case Op::LoadSlice: {
            Value stop = (in.a & 2) ? pop() : Value();
            Value start = (in.a & 1) ? pop() : Value();
            Value obj = pop();
            size_t size = obj.isList() ? obj.listObj().items.size() : obj.isString() ? obj.string().size() : 0;
            if (!obj.isList() && !obj.isString())
                raise("Only lists and text can be sliced with [a:b], not " + obj.typeDescription() + ".");
            auto clampIndex = [&](const Value& v, long long fallback) {
                if (v.isNone())
                    return fallback;
                long long i = toIndex(v, "Slice");
                long long n = static_cast<long long>(size);
                if (i < 0)
                    i += n;
                return std::clamp(i, 0LL, n);
            };
            long long a = clampIndex(start, 0), b = clampIndex(stop, static_cast<long long>(size));
            if (b < a)
                b = a;
            if (obj.isString()) {
                st.emplace_back(obj.string().substr(static_cast<size_t>(a), static_cast<size_t>(b - a)));
            } else {
                auto& items = obj.listObj().items;
                st.push_back(Value::list(std::vector<Value>(items.begin() + a, items.begin() + b)));
            }
            break;
        }
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::FloorDiv:
        case Op::Mod:
        case Op::Pow: {
            Value b = pop();
            Value a = pop();
            st.push_back(binary(in.op, a, b));
            break;
        }
        case Op::Neg: {
            Value a = pop();
            if (a.isNumber())
                st.emplace_back(-a.number());
            else if (a.isVec())
                st.push_back(binary(Op::Mul, a, Value(-1.0)));
            else
                raise("Can't make " + a.typeDescription() + " negative.");
            break;
        }
        case Op::Pos: {
            if (!st.back().isNumber())
                raise("'+' only works on numbers here.");
            break;
        }
        case Op::Not: {
            Value a = pop();
            st.emplace_back(!a.truthy());
            break;
        }
        case Op::Eq: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(a == b || (a.isNumber() && b.isBool() && a.number() == (b.boolean() ? 1 : 0)) ||
                            (a.isBool() && b.isNumber() && b.number() == (a.boolean() ? 1 : 0)));
            break;
        }
        case Op::NotEq: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(!(a == b));
            break;
        }
        case Op::Lt: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(lessThan(a, b));
            break;
        }
        case Op::Gt: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(lessThan(b, a));
            break;
        }
        case Op::LtEq: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(!lessThan(b, a));
            break;
        }
        case Op::GtEq: {
            Value b = pop();
            Value a = pop();
            st.emplace_back(!lessThan(a, b));
            break;
        }
        case Op::In:
        case Op::NotIn: {
            Value container = pop();
            Value item = pop();
            bool r = contains(container, item);
            st.emplace_back(in.op == Op::In ? r : !r);
            break;
        }
        case Op::Jump:
            if (static_cast<size_t>(in.a) < f.ip && --budget == 0)
                tooLong();
            f.ip = static_cast<size_t>(in.a);
            break;
        case Op::JumpIfFalse: {
            Value c = pop();
            if (!c.truthy())
                f.ip = static_cast<size_t>(in.a);
            break;
        }
        case Op::JumpIfFalseOrPop:
            if (!st.back().truthy())
                f.ip = static_cast<size_t>(in.a);
            else
                st.pop_back();
            break;
        case Op::JumpIfTrueOrPop:
            if (st.back().truthy())
                f.ip = static_cast<size_t>(in.a);
            else
                st.pop_back();
            break;
        case Op::BuildList: {
            size_t n = static_cast<size_t>(in.a);
            std::vector<Value> items(std::make_move_iterator(st.end() - static_cast<std::ptrdiff_t>(n)),
                                     std::make_move_iterator(st.end()));
            st.resize(st.size() - n);
            st.push_back(Value::list(std::move(items)));
            break;
        }
        case Op::BuildDict: {
            size_t n = static_cast<size_t>(in.a) * 2;
            Value d = Value::dict();
            size_t first = st.size() - n;
            for (size_t i = first; i < st.size(); i += 2)
                if (!d.dictObj().set(st[i], st[i + 1]))
                    raise(st[i].typeDescription() + " can't be used as a dictionary key. Use text or numbers.");
            st.resize(first);
            st.push_back(std::move(d));
            break;
        }
        case Op::BuildString: {
            size_t n = static_cast<size_t>(in.a);
            std::string out;
            for (size_t i = st.size() - n; i < st.size(); ++i)
                out += st[i].toString();
            st.resize(st.size() - n);
            st.emplace_back(std::move(out));
            break;
        }
        case Op::Format: {
            Value v = pop();
            st.emplace_back(formatWithSpec(v, f.proto->constants[static_cast<size_t>(in.a)].string()));
            break;
        }
        case Op::Call: {
            int positional = in.a, keywords = in.b;
            const std::vector<Symbol>* names =
                keywords ? &f.proto->keywordLists[static_cast<size_t>(in.c)] : nullptr;
            size_t calleeIndex = st.size() - static_cast<size_t>(positional + keywords) - 1;
            const Value& callee = st[calleeIndex];
            bool scriptCall = callee.type() == Type::Function;
            if (scriptCall) {
                if (--budget == 0)
                    tooLong();
                prepareCall(task, calleeIndex, positional, keywords, names);
            } else {
                size_t depth = task.frames.size();
                callNative(task, calleeIndex, positional, keywords, names);
                if (task.frames.size() != depth)
                    break; // a bound script method (e.g. other.take_damage) pushed its frame
                if (task.waitRequested) {
                    task.waitRequested = false;
                    task.wakeAt = time_ + task.waitSeconds;
                    task.waitFrame = frame_;
                    return Status::Waiting;
                }
            }
            break;
        }
        case Op::Return: {
            Value result = pop();
            size_t base = f.base;
            task.frames.pop_back();
            st.resize(base);
            if (task.frames.empty()) {
                task.result = std::move(result);
                return Status::Done;
            }
            st.push_back(std::move(result));
            break;
        }
        case Op::MakeFunction: {
            auto fn = std::make_shared<FunctionObj>();
            size_t defaults = static_cast<size_t>(in.b);
            fn->defaults.assign(st.end() - static_cast<std::ptrdiff_t>(defaults), st.end());
            st.resize(st.size() - defaults);
            fn->proto = f.proto->module->functions[static_cast<size_t>(in.a)].get();
            fn->instance = f.instance;
            fn->module = f.instance ? f.instance->module : task.module;
            st.push_back(Value(Type::Function, fn));
            break;
        }
        case Op::GetIter: {
            Value v = pop();
            st.push_back(iterate(v));
            break;
        }
        case Op::ForIter: {
            Value out;
            if (next(st.back(), out)) {
                st.push_back(std::move(out));
            } else {
                st.pop_back();
                f.ip = static_cast<size_t>(in.a);
            }
            break;
        }
        case Op::Unpack: {
            Value v = pop();
            size_t n = static_cast<size_t>(in.a);
            std::vector<Value> items;
            if (v.isList())
                items = v.listObj().items;
            else if (v.isVec())
                for (int i = 0; i < v.vecObj().components; ++i)
                    items.emplace_back(v.vecObj().v[i]);
            else
                raise("Expected " + std::to_string(n) + " values to unpack, but got " + v.typeDescription() + ".");
            if (items.size() != n)
                raise("Expected " + std::to_string(n) + " values to unpack, but there were " +
                      std::to_string(items.size()) + ".");
            for (size_t i = n; i-- > 0;)
                st.push_back(items[i]);
            break;
        }
        }
    }
}

// ---------------------------------------------------------------- operators

Value VM::binary(Op op, const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber()) {
        double x = a.number(), y = b.number();
        switch (op) {
        case Op::Add: return x + y;
        case Op::Sub: return x - y;
        case Op::Mul: return x * y;
        case Op::Div:
            if (y == 0)
                raise("Can't divide by zero.");
            return x / y;
        case Op::FloorDiv:
            if (y == 0)
                raise("Can't divide by zero.");
            return std::floor(x / y);
        case Op::Mod: {
            if (y == 0)
                raise("Can't use % with zero (it would divide by zero).");
            double r = std::fmod(x, y);
            if (r != 0 && ((r < 0) != (y < 0)))
                r += y;
            return r;
        }
        case Op::Pow: return std::pow(x, y);
        default: break;
        }
    }
    if (a.isBool() || b.isBool()) {
        // True/False act like 1/0 in arithmetic (useful for counting).
        Value x = a.isBool() ? Value(a.boolean() ? 1.0 : 0.0) : a;
        Value y = b.isBool() ? Value(b.boolean() ? 1.0 : 0.0) : b;
        if (x.isNumber() && y.isNumber())
            return binary(op, x, y);
    }
    // Text joins with anything using +, like C# and JavaScript: "Score: " + 5
    if (op == Op::Add && (a.isString() || b.isString()))
        return Value(a.toString() + b.toString());
    if (op == Op::Mul && ((a.isString() && b.isNumber()) || (a.isNumber() && b.isString()))) {
        const std::string& s = a.isString() ? a.string() : b.string();
        double n = a.isNumber() ? a.number() : b.number();
        std::string out;
        for (int i = 0; i < static_cast<int>(n); ++i)
            out += s;
        return Value(out);
    }
    if (a.isList() && b.isList() && op == Op::Add) {
        std::vector<Value> items = a.listObj().items;
        items.insert(items.end(), b.listObj().items.begin(), b.listObj().items.end());
        return Value::list(std::move(items));
    }
    if (op == Op::Mul && ((a.isList() && b.isNumber()) || (a.isNumber() && b.isList()))) {
        const auto& src = a.isList() ? a.listObj().items : b.listObj().items;
        double n = a.isNumber() ? a.number() : b.number();
        std::vector<Value> items;
        for (int i = 0; i < static_cast<int>(n); ++i)
            items.insert(items.end(), src.begin(), src.end());
        return Value::list(std::move(items));
    }
    if (a.isVec() || b.isVec()) {
        const VecObj* va = a.isVec() ? &a.vecObj() : nullptr;
        const VecObj* vb = b.isVec() ? &b.vecObj() : nullptr;
        if ((va || a.isNumber()) && (vb || b.isNumber())) {
            auto out = std::make_shared<VecObj>();
            const VecObj& shape = va ? *va : *vb;
            out->components = std::max(va ? va->components : 0, vb ? vb->components : 0);
            out->isColor = shape.isColor;
            for (int i = 0; i < 4; ++i) {
                double x = va ? va->v[i] : a.number();
                double y = vb ? vb->v[i] : b.number();
                bool colorAlpha = out->isColor && i == 3 && !(va && vb);
                switch (op) {
                case Op::Add: out->v[i] = x + y; break;
                case Op::Sub: out->v[i] = x - y; break;
                case Op::Mul: out->v[i] = colorAlpha ? (va ? x : y) : x * y; break;
                case Op::Div:
                    if (y == 0 && i < out->components)
                        raise("Can't divide by zero.");
                    out->v[i] = y == 0 ? 0 : x / y;
                    break;
                default: raise(std::string("Can't use '") + opSymbol(op) + "' with vectors.");
                }
            }
            return Value(Type::Vec, out);
        }
    }
    std::string hint;
    if (op == Op::Sub && (a.isString() || b.isString()))
        hint = " Text can only be joined with '+'.";
    if (a.isNone() || b.isNone())
        hint = " One side is None (nothing) — maybe a variable was never set, or a function didn't return anything.";
    raise(std::string("Can't use '") + opSymbol(op) + "' between " + a.typeDescription() + " and " +
          b.typeDescription() + " (" + a.repr() + " " + opSymbol(op) + " " + b.repr() + ")." + hint);
}

bool VM::lessThan(const Value& a, const Value& b) {
    if (a.isNumber() && b.isNumber())
        return a.number() < b.number();
    if (a.isString() && b.isString())
        return a.string() < b.string();
    if ((a.isNumber() || a.isBool()) && (b.isNumber() || b.isBool())) {
        double x = a.isBool() ? a.boolean() : a.number(), y = b.isBool() ? b.boolean() : b.number();
        return x < y;
    }
    if (a.isList() && b.isList()) {
        auto& x = a.listObj().items;
        auto& y = b.listObj().items;
        for (size_t i = 0; i < std::min(x.size(), y.size()); ++i) {
            if (lessThan(x[i], y[i]))
                return true;
            if (lessThan(y[i], x[i]))
                return false;
        }
        return x.size() < y.size();
    }
    std::string tip = (a.isString() && b.isNumber()) || (a.isNumber() && b.isString())
                          ? " Tip: use int(text) to turn text into a number."
                          : "";
    raise("Can't compare " + a.typeDescription() + " with " + b.typeDescription() + " using < or >." + tip);
}

bool VM::contains(const Value& c, const Value& item) {
    switch (c.type()) {
    case Type::List:
        for (auto& v : c.listObj().items)
            if (v == item)
                return true;
        return false;
    case Type::Dict: return c.dictObj().find(item) != nullptr;
    case Type::String:
        if (!item.isString())
            raise("To check inside text with 'in', the left side must be text too.");
        return c.string().find(item.string()) != std::string::npos;
    case Type::Range: {
        if (!item.isNumber())
            return false;
        auto* r = c.as<RangeObj>();
        double n = item.number();
        if (r->step > 0 ? (n < r->start || n >= r->stop) : (n > r->start || n <= r->stop))
            return false;
        double k = (n - r->start) / r->step;
        return k == std::floor(k);
    }
    default: raise("Can't use 'in' with " + c.typeDescription() + ". It works with lists, dictionaries and text.");
    }
}

Value VM::iterate(const Value& v) {
    auto it = std::make_shared<IteratorObj>();
    it->source = v;
    switch (v.type()) {
    case Type::List:
    case Type::String:
    case Type::Range: break;
    case Type::Dict:
        for (auto& [k, val] : v.dictObj().entries)
            it->snapshot.push_back(k);
        break;
    case Type::Iterator: return v;
    case Type::Number:
        raise("Can't loop over a number. To repeat something " + formatNumber(v.number()) +
              " times, write: for i in range(" + formatNumber(v.number()) + "):");
    default: raise("Can't loop over " + v.typeDescription() + ". Loops work with lists, text, dictionaries and range().");
    }
    return Value(Type::Iterator, it);
}

bool VM::next(const Value& iterator, Value& out) {
    auto* it = iterator.as<IteratorObj>();
    const Value& src = it->source;
    switch (src.type()) {
    case Type::List: {
        auto& items = src.listObj().items;
        if (it->index >= items.size())
            return false;
        out = items[it->index++];
        return true;
    }
    case Type::String: {
        const std::string& s = src.string();
        if (it->index >= s.size())
            return false;
        // Step over whole UTF-8 characters.
        size_t start = it->index;
        unsigned char c = static_cast<unsigned char>(s[start]);
        size_t len = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        it->index = std::min(s.size(), start + len);
        out = Value(s.substr(start, it->index - start));
        return true;
    }
    case Type::Range: {
        auto* r = src.as<RangeObj>();
        if (it->index >= r->length())
            return false;
        out = Value(r->at(it->index++));
        return true;
    }
    case Type::Dict: {
        if (it->index >= it->snapshot.size())
            return false;
        out = it->snapshot[it->index++];
        return true;
    }
    default: return false;
    }
}

// ---------------------------------------------------------------- attributes and indexing

std::vector<std::string> VM::attrCandidates(const Value& obj) const {
    std::vector<std::string> out;
    auto addAll = [&](const std::unordered_map<Symbol, Value>& table) {
        for (auto& [s, v] : table)
            out.push_back(symbolName(s));
    };
    switch (obj.type()) {
    case Type::List: addAll(listMethods_); break;
    case Type::String: addAll(stringMethods_); break;
    case Type::Dict: addAll(dictMethods_); break;
    case Type::Vec:
        addAll(vecMethods_);
        for (const char* n : {"x", "y", "z", "r", "g", "b", "a"})
            out.push_back(n);
        break;
    case Type::Object: out = obj.nativeObject().attrNames(); break;
    default: break;
    }
    return out;
}

Value VM::getAttr(const Value& obj, Symbol name) {
    const std::string& n = symbolName(name);
    auto method = [&](const std::unordered_map<Symbol, Value>& table) -> Value {
        auto it = table.find(name);
        if (it == table.end())
            return Value();
        auto bm = std::make_shared<BoundMethodObj>();
        bm->self = obj;
        bm->method = it->second;
        return Value(Type::BoundMethod, bm);
    };
    Value result;
    switch (obj.type()) {
    case Type::Object:
        if (obj.nativeObject().getAttr(*this, n, result))
            return result;
        raise(obj.nativeObject().repr() + " doesn't have '" + n + "'." + didYouMean(n, attrCandidates(obj)));
    case Type::List:
        result = method(listMethods_);
        if (result.isNone() && (n == "length" || n == "size"))
            raise("Use len(my_list) to get the number of items in a list.");
        break;
    case Type::String:
        result = method(stringMethods_);
        if (n == "length")
            raise("Use len(text) to get the number of characters.");
        break;
    case Type::Dict: result = method(dictMethods_); break;
    case Type::Vec: {
        auto& v = obj.vecObj();
        static const std::string xs[] = {"x", "y", "z", "w"}, cs[] = {"r", "g", "b", "a"};
        for (int i = 0; i < 4; ++i)
            if (n == xs[i] || n == cs[i])
                return Value(v.v[i]);
        result = method(vecMethods_);
        break;
    }
    case Type::None:
        raise("Can't get '." + n + "' from None (nothing). This often means something wasn't found — for example "
              "find() with a misspelled name — or a variable was never set.");
    default: break;
    }
    if (!result.isNone())
        return result;
    raise(obj.typeDescription() + " doesn't have '" + n + "'." + didYouMean(n, attrCandidates(obj)));
}

void VM::setAttr(const Value& obj, Symbol name, const Value& value) {
    const std::string& n = symbolName(name);
    if (obj.isObject()) {
        if (obj.nativeObject().setAttr(*this, n, value))
            return;
        raise("Can't set '" + n + "' on " + obj.nativeObject().repr() + "." + didYouMean(n, attrCandidates(obj)));
    }
    if (obj.isVec())
        raise("Vectors and colors can't be changed piece by piece. Make a new one instead, like: v = vec(1, 2)");
    if (obj.isNone())
        raise("Can't set '." + n + "' on None (nothing). This often means something wasn't found or never set.");
    raise("Can't set '" + n + "' on " + obj.typeDescription() + ".");
}

Value VM::getIndex(const Value& obj, const Value& index) {
    switch (obj.type()) {
    case Type::List: {
        auto& items = obj.listObj().items;
        return items[resolveIndex(toIndex(index, "List"), items.size(), "list")];
    }
    case Type::String: {
        const std::string& s = obj.string();
        return Value(std::string(1, s[resolveIndex(toIndex(index, "Text"), s.size(), "text")]));
    }
    case Type::Dict: {
        Value* v = obj.dictObj().find(index);
        if (!v)
            raise("The dictionary doesn't have the key " + index.repr() + ". Use .get(" + index.repr() +
                  ", default) if it might be missing.");
        return *v;
    }
    case Type::Vec: {
        size_t i = resolveIndex(toIndex(index, "Vector"), static_cast<size_t>(obj.vecObj().components), "vector");
        return Value(obj.vecObj().v[i]);
    }
    case Type::Range: {
        auto* r = obj.as<RangeObj>();
        return Value(r->at(resolveIndex(toIndex(index, "Range"), r->length(), "range")));
    }
    case Type::None: raise("Can't use [ ] on None (nothing). Maybe a variable was never set?");
    default: raise("Can't use [ ] on " + obj.typeDescription() + ".");
    }
}

void VM::setIndex(const Value& obj, const Value& index, const Value& value) {
    switch (obj.type()) {
    case Type::List: {
        auto& items = obj.listObj().items;
        items[resolveIndex(toIndex(index, "List"), items.size(), "list")] = value;
        return;
    }
    case Type::Dict:
        if (!obj.dictObj().set(index, value))
            raise(index.typeDescription() + " can't be used as a dictionary key. Use text or numbers.");
        return;
    case Type::String: raise("Text can't be changed letter by letter. Build new text instead.");
    default: raise("Can't use [ ] = on " + obj.typeDescription() + ".");
    }
}

// ---------------------------------------------------------------- JSON conversion

Value VM::fromJson(const Json& j) {
    switch (j.type()) {
    case Json::Type::Null: return Value();
    case Json::Type::Bool: return Value(j.asBool());
    case Json::Type::Number: return Value(j.asNumber());
    case Json::Type::String: return Value(j.asString());
    case Json::Type::Array: {
        std::vector<Value> items;
        for (auto& e : j.elements())
            items.push_back(fromJson(e));
        return Value::list(std::move(items));
    }
    case Json::Type::Object: {
        // {"$color": [r,g,b,a]} and {"$vec": [...]} round-trip colors and vectors.
        if (j.size() == 1 && j.contains("$color")) {
            auto& c = j["$color"];
            return Value::color(c[0].asNumber(), c[1].asNumber(), c[2].asNumber(), c[3].asNumber(1));
        }
        if (j.size() == 1 && j.contains("$vec")) {
            auto& c = j["$vec"];
            return Value::vec(c[0].asNumber(), c[1].asNumber(), c[2].asNumber(), static_cast<int>(c.size()));
        }
        Value d = Value::dict();
        for (auto& m : j.members())
            d.dictObj().set(Value(m.key), fromJson(m.value));
        return d;
    }
    }
    return Value();
}

Json VM::toJson(const Value& v) {
    switch (v.type()) {
    case Type::None: return Json();
    case Type::Bool: return Json(v.boolean());
    case Type::Number: return Json(v.number());
    case Type::String: return Json(v.string());
    case Type::List: {
        Json a = Json::array();
        for (auto& e : v.listObj().items)
            a.push(toJson(e));
        return a;
    }
    case Type::Dict: {
        Json o = Json::object();
        for (auto& [k, val] : v.dictObj().entries)
            o[k.toString()] = toJson(val);
        return o;
    }
    case Type::Vec: {
        auto& vec = v.vecObj();
        Json arr = Json::array();
        for (int i = 0; i < vec.components; ++i)
            arr.push(vec.v[i]);
        Json o = Json::object();
        o[vec.isColor ? "$color" : "$vec"] = arr;
        return o;
    }
    default: return Json();
    }
}

} // namespace aven::script
