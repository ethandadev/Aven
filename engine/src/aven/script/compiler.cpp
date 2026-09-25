#include "aven/script/ast.h"
#include "aven/script/bytecode.h"
#include "aven/script/errors.h"

#include <algorithm>
#include <unordered_map>

namespace aven::script {

bool Module::hasFunction(std::string_view name) const {
    return std::find(functionNames.begin(), functionNames.end(), name) != functionNames.end();
}

namespace {

// Names assigned anywhere at the top level (including inside if/for blocks there).
void collectScriptVariables(const std::vector<StmtPtr>& stmts, std::unordered_set<Symbol>& out);

void collectTargetNames(const Expr& e, std::unordered_set<Symbol>& out) {
    if (e.kind == ExprKind::Name)
        out.insert(e.sym);
    else if (e.kind == ExprKind::List)
        for (auto& item : e.items)
            collectTargetNames(*item, out);
}

void collectScriptVariables(const std::vector<StmtPtr>& stmts, std::unordered_set<Symbol>& out) {
    for (auto& s : stmts) {
        switch (s->kind) {
        case StmtKind::Assign:
        case StmtKind::AugAssign:
        case StmtKind::For:
            for (auto& t : s->targets)
                collectTargetNames(*t, out);
            break;
        case StmtKind::Def: out.insert(s->name); break;
        case StmtKind::Global:
            for (Symbol g : s->globals)
                out.insert(g);
            break;
        default: break;
        }
        if (s->kind != StmtKind::Def) {
            collectScriptVariables(s->body, out);
            collectScriptVariables(s->orelse, out);
        }
    }
}

// `global x` inside functions also declares script variables.
void collectGlobalsInFunctions(const std::vector<StmtPtr>& stmts, std::unordered_set<Symbol>& out, bool inFunction) {
    for (auto& s : stmts) {
        if (s->kind == StmtKind::Global && inFunction)
            for (Symbol g : s->globals)
                out.insert(g);
        collectGlobalsInFunctions(s->body, out, inFunction || s->kind == StmtKind::Def);
        collectGlobalsInFunctions(s->orelse, out, inFunction);
    }
}

class Compiler {
public:
    Compiler(Module& module, const std::unordered_set<Symbol>& scriptVars) : module_(module), scriptVars_(scriptVars) {}

    FunctionProto* compileTopLevel(const Program& program) {
        auto proto = std::make_unique<FunctionProto>();
        proto->name = "<script>";
        proto->isTopLevel = true;
        proto->module = &module_;
        fn_ = proto.get();
        module_.functions.push_back(std::move(proto));
        for (auto& s : program.statements)
            statement(*s);
        emit(Op::PushNone, 0);
        emit(Op::Return, 0);
        return fn_;
    }

private:
    Module& module_;
    const std::unordered_set<Symbol>& scriptVars_;
    FunctionProto* fn_ = nullptr;
    std::unordered_map<Symbol, int> locals_;
    std::unordered_set<Symbol> declaredGlobals_;

    struct Loop {
        bool isFor;
        size_t start;
        std::vector<size_t> breaks;
    };
    std::vector<Loop> loops_;

    size_t emit(Op op, int line, int32_t a = 0, int32_t b = 0, int32_t c = 0) {
        fn_->code.push_back({op, a, b, c});
        fn_->lines.push_back(line);
        return fn_->code.size() - 1;
    }

    size_t here() const { return fn_->code.size(); }
    void patch(size_t at, size_t target) { fn_->code[at].a = static_cast<int32_t>(target); }

    int32_t constant(Value v) {
        fn_->constants.push_back(std::move(v));
        return static_cast<int32_t>(fn_->constants.size() - 1);
    }

    bool inFunction() const { return !fn_->isTopLevel; }

    int localSlot(Symbol s) const {
        auto it = locals_.find(s);
        return it == locals_.end() ? -1 : it->second;
    }

    int declareLocal(Symbol s) {
        int existing = localSlot(s);
        if (existing >= 0)
            return existing;
        int slot = fn_->numLocals++;
        locals_[s] = slot;
        fn_->localNames.push_back(s);
        return slot;
    }

    // Inside functions, assigning to a name that is a script-level variable updates
    // that variable (no 'global' needed). Anything else becomes a local variable.
    void storeName(Symbol s, int line) {
        if (inFunction()) {
            int slot = localSlot(s);
            if (slot < 0 && !scriptVars_.count(s) && !declaredGlobals_.count(s))
                slot = declareLocal(s);
            if (slot >= 0) {
                emit(Op::StoreLocal, line, slot);
                return;
            }
        }
        emit(Op::StoreName, line, static_cast<int32_t>(s));
    }

    void loadName(Symbol s, int line) {
        int slot = inFunction() ? localSlot(s) : -1;
        if (slot >= 0)
            emit(Op::LoadLocal, line, slot);
        else
            emit(Op::LoadName, line, static_cast<int32_t>(s));
    }

    // Pre-declare every local of a function so slots are known before use.
    void declareLocals(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts) {
            if (s->kind == StmtKind::Global)
                for (Symbol g : s->globals)
                    declaredGlobals_.insert(g);
        }
        std::unordered_set<Symbol> assigned;
        collectAssigned(stmts, assigned);
        for (Symbol s : assigned)
            if (!scriptVars_.count(s) && !declaredGlobals_.count(s))
                declareLocal(s);
    }

    static void collectAssigned(const std::vector<StmtPtr>& stmts, std::unordered_set<Symbol>& out) {
        for (auto& s : stmts) {
            if (s->kind == StmtKind::Assign || s->kind == StmtKind::AugAssign || s->kind == StmtKind::For)
                for (auto& t : s->targets)
                    collectTargetNames(*t, out);
            collectAssigned(s->body, out);
            collectAssigned(s->orelse, out);
        }
    }

    // --- statements

    void block(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts)
            statement(*s);
    }

    void statement(const Stmt& s) {
        switch (s.kind) {
        case StmtKind::Expression:
            expression(*s.expr);
            emit(Op::Pop, s.line);
            break;
        case StmtKind::Assign:
            expression(*s.expr);
            for (size_t i = 0; i < s.targets.size(); ++i) {
                if (i + 1 < s.targets.size())
                    emit(Op::Dup, s.line);
                storeTarget(*s.targets[i]);
            }
            break;
        case StmtKind::AugAssign: augAssign(s); break;
        case StmtKind::If: ifStatement(s); break;
        case StmtKind::While: whileStatement(s); break;
        case StmtKind::For: forStatement(s); break;
        case StmtKind::Def: defStatement(s); break;
        case StmtKind::Return:
            if (s.expr)
                expression(*s.expr);
            else
                emit(Op::PushNone, s.line);
            emit(Op::Return, s.line);
            break;
        case StmtKind::Break: {
            Loop& loop = loops_.back();
            if (loop.isFor)
                emit(Op::Pop, s.line); // drop the iterator
            loop.breaks.push_back(emit(Op::Jump, s.line));
            break;
        }
        case StmtKind::Continue: emit(Op::Jump, s.line, static_cast<int32_t>(loops_.back().start)); break;
        case StmtKind::Pass: break;
        case StmtKind::Global:
            for (Symbol g : s.globals)
                declaredGlobals_.insert(g);
            break;
        }
    }

    void storeTarget(const Expr& t) {
        switch (t.kind) {
        case ExprKind::Name: storeName(t.sym, t.line); break;
        case ExprKind::Attr:
            expression(*t.a);
            emit(Op::Rot, t.line, 1);
            emit(Op::StoreAttr, t.line, static_cast<int32_t>(t.sym));
            break;
        case ExprKind::Index:
            expression(*t.a);
            expression(*t.b);
            emit(Op::Rot, t.line, 2);
            emit(Op::StoreIndex, t.line);
            break;
        case ExprKind::List:
            emit(Op::Unpack, t.line, static_cast<int32_t>(t.items.size()));
            for (auto& item : t.items)
                storeTarget(*item);
            break;
        default: throw ScriptError("You can't store a value here.", t.line);
        }
    }

    static Op binaryOp(Tok t) {
        switch (t) {
        case Tok::Plus:
        case Tok::PlusAssign: return Op::Add;
        case Tok::Minus:
        case Tok::MinusAssign: return Op::Sub;
        case Tok::Star:
        case Tok::StarAssign: return Op::Mul;
        case Tok::Slash:
        case Tok::SlashAssign: return Op::Div;
        case Tok::SlashSlash: return Op::FloorDiv;
        case Tok::Percent:
        case Tok::PercentAssign: return Op::Mod;
        case Tok::StarStar: return Op::Pow;
        default: return Op::Add;
        }
    }

    void augAssign(const Stmt& s) {
        const Expr& t = *s.targets[0];
        Op op = binaryOp(s.op);
        switch (t.kind) {
        case ExprKind::Name:
            loadName(t.sym, t.line);
            expression(*s.expr);
            emit(op, s.line);
            storeName(t.sym, t.line);
            break;
        case ExprKind::Attr:
            expression(*t.a);
            emit(Op::Dup, s.line);
            emit(Op::LoadAttr, s.line, static_cast<int32_t>(t.sym));
            expression(*s.expr);
            emit(op, s.line);
            emit(Op::StoreAttr, s.line, static_cast<int32_t>(t.sym));
            break;
        case ExprKind::Index:
            expression(*t.a);
            expression(*t.b);
            emit(Op::Dup2, s.line);
            emit(Op::LoadIndex, s.line);
            expression(*s.expr);
            emit(op, s.line);
            emit(Op::StoreIndex, s.line);
            break;
        default: throw ScriptError("You can't use this operator here.", s.line);
        }
    }

    void ifStatement(const Stmt& s) {
        expression(*s.expr);
        size_t toElse = emit(Op::JumpIfFalse, s.line);
        block(s.body);
        if (s.orelse.empty()) {
            patch(toElse, here());
            return;
        }
        size_t toEnd = emit(Op::Jump, s.line);
        patch(toElse, here());
        block(s.orelse);
        patch(toEnd, here());
    }

    void whileStatement(const Stmt& s) {
        size_t start = here();
        loops_.push_back({false, start, {}});
        expression(*s.expr);
        size_t toEnd = emit(Op::JumpIfFalse, s.line);
        block(s.body);
        emit(Op::Jump, s.line, static_cast<int32_t>(start));
        patch(toEnd, here());
        for (size_t b : loops_.back().breaks)
            patch(b, here());
        loops_.pop_back();
    }

    void forStatement(const Stmt& s) {
        expression(*s.expr);
        emit(Op::GetIter, s.line);
        size_t start = here();
        loops_.push_back({true, start, {}});
        size_t iter = emit(Op::ForIter, s.line);
        storeTarget(*s.targets[0]);
        block(s.body);
        emit(Op::Jump, s.line, static_cast<int32_t>(start));
        patch(iter, here());
        for (size_t b : loops_.back().breaks)
            patch(b, here());
        loops_.pop_back();
    }

    void defStatement(const Stmt& s) {
        // Defaults are evaluated once, when the function is created.
        for (auto& d : s.defaults)
            expression(*d);

        auto proto = std::make_unique<FunctionProto>();
        proto->name = symbolName(s.name);
        proto->line = s.line;
        proto->params = s.params;
        proto->requiredParams = static_cast<int>(s.params.size() - s.defaults.size());
        proto->module = &module_;
        FunctionProto* raw = proto.get();
        module_.functions.push_back(std::move(proto));
        int32_t index = static_cast<int32_t>(module_.functions.size() - 1);
        module_.functionNames.push_back(raw->name);

        FunctionProto* savedFn = fn_;
        auto savedLocals = std::move(locals_);
        auto savedGlobals = std::move(declaredGlobals_);
        auto savedLoops = std::move(loops_);
        fn_ = raw;
        locals_.clear();
        declaredGlobals_.clear();
        loops_.clear();
        for (Symbol p : s.params)
            declareLocal(p);
        declareLocals(s.body);
        block(s.body);
        emit(Op::PushNone, s.endLine);
        emit(Op::Return, s.endLine);
        fn_ = savedFn;
        locals_ = std::move(savedLocals);
        declaredGlobals_ = std::move(savedGlobals);
        loops_ = std::move(savedLoops);

        emit(Op::MakeFunction, s.line, index, static_cast<int32_t>(s.defaults.size()));
        storeName(s.name, s.line);
    }

    // --- expressions

    void expression(const Expr& e) {
        switch (e.kind) {
        case ExprKind::Number: emit(Op::Const, e.line, constant(Value(e.number))); break;
        case ExprKind::String: emit(Op::Const, e.line, constant(Value(e.text))); break;
        case ExprKind::True: emit(Op::PushTrue, e.line); break;
        case ExprKind::False: emit(Op::PushFalse, e.line); break;
        case ExprKind::None: emit(Op::PushNone, e.line); break;
        case ExprKind::Name: loadName(e.sym, e.line); break;
        case ExprKind::Self: emit(Op::LoadSelf, e.line); break;
        case ExprKind::FString: {
            int count = 0;
            for (size_t i = 0; i < e.items.size(); ++i) {
                if (!e.literalParts[i].empty()) {
                    emit(Op::Const, e.line, constant(Value(e.literalParts[i])));
                    ++count;
                }
                expression(*e.items[i]);
                if (!e.formatSpecs[i].empty())
                    emit(Op::Format, e.line, constant(Value(e.formatSpecs[i])));
                ++count;
            }
            if (!e.literalParts.back().empty()) {
                emit(Op::Const, e.line, constant(Value(e.literalParts.back())));
                ++count;
            }
            emit(Op::BuildString, e.line, count);
            break;
        }
        case ExprKind::List:
            for (auto& item : e.items)
                expression(*item);
            emit(Op::BuildList, e.line, static_cast<int32_t>(e.items.size()));
            break;
        case ExprKind::Dict:
            for (auto& item : e.items)
                expression(*item);
            emit(Op::BuildDict, e.line, static_cast<int32_t>(e.items.size() / 2));
            break;
        case ExprKind::Unary:
            expression(*e.a);
            emit(e.op == Tok::Minus ? Op::Neg : e.op == Tok::Plus ? Op::Pos : Op::Not, e.line);
            break;
        case ExprKind::Binary:
            expression(*e.a);
            expression(*e.b);
            emit(binaryOp(e.op), e.line);
            break;
        case ExprKind::And: {
            expression(*e.a);
            size_t j = emit(Op::JumpIfFalseOrPop, e.line);
            expression(*e.b);
            patch(j, here());
            break;
        }
        case ExprKind::Or: {
            expression(*e.a);
            size_t j = emit(Op::JumpIfTrueOrPop, e.line);
            expression(*e.b);
            patch(j, here());
            break;
        }
        case ExprKind::Compare: {
            expression(*e.a);
            expression(*e.b);
            Op op = Op::Eq;
            switch (e.op) {
            case Tok::Eq: op = Op::Eq; break;
            case Tok::NotEq: op = Op::NotEq; break;
            case Tok::Lt: op = Op::Lt; break;
            case Tok::Gt: op = Op::Gt; break;
            case Tok::LtEq: op = Op::LtEq; break;
            case Tok::GtEq: op = Op::GtEq; break;
            case Tok::In: op = e.notIn ? Op::NotIn : Op::In; break;
            default: break;
            }
            emit(op, e.line);
            break;
        }
        case ExprKind::Call: {
            expression(*e.a);
            for (auto& arg : e.items)
                expression(*arg);
            int32_t kwCount = static_cast<int32_t>(e.kwNames.size());
            int32_t kwIndex = -1;
            if (kwCount) {
                fn_->keywordLists.push_back(e.kwNames);
                kwIndex = static_cast<int32_t>(fn_->keywordLists.size() - 1);
            }
            emit(Op::Call, e.line, static_cast<int32_t>(e.items.size()) - kwCount, kwCount, kwIndex);
            break;
        }
        case ExprKind::Attr:
            expression(*e.a);
            emit(Op::LoadAttr, e.line, static_cast<int32_t>(e.sym));
            break;
        case ExprKind::Index:
            expression(*e.a);
            expression(*e.b);
            emit(Op::LoadIndex, e.line);
            break;
        case ExprKind::Slice: {
            expression(*e.a);
            int flags = 0;
            if (e.b) {
                expression(*e.b);
                flags |= 1;
            }
            if (e.c) {
                expression(*e.c);
                flags |= 2;
            }
            emit(Op::LoadSlice, e.line, flags);
            break;
        }
        case ExprKind::Ternary: {
            expression(*e.a);
            size_t toElse = emit(Op::JumpIfFalse, e.line);
            expression(*e.b);
            size_t toEnd = emit(Op::Jump, e.line);
            patch(toElse, here());
            expression(*e.c);
            patch(toEnd, here());
            break;
        }
        }
    }
};

// Top-level `name = <literal>` assignments become inspector-editable variables.
bool literalValue(const Expr& e, Value& out) {
    switch (e.kind) {
    case ExprKind::Number: out = Value(e.number); return true;
    case ExprKind::String: out = Value(e.text); return true;
    case ExprKind::True: out = Value(true); return true;
    case ExprKind::False: out = Value(false); return true;
    case ExprKind::Call: {
        if (e.a->kind != ExprKind::Name || !e.kwNames.empty())
            return false;
        const std::string& fn = e.a->text;
        std::vector<double> nums;
        for (auto& item : e.items) {
            if (item->kind != ExprKind::Number)
                return false;
            nums.push_back(item->number);
        }
        if (fn == "vec" && (nums.size() == 2 || nums.size() == 3)) {
            out = Value::vec(nums[0], nums[1], nums.size() > 2 ? nums[2] : 0, static_cast<int>(nums.size()));
            return true;
        }
        if (fn == "rgb" && (nums.size() == 3 || nums.size() == 4)) {
            out = Value::color(nums[0] / 255.0, nums[1] / 255.0, nums[2] / 255.0, nums.size() > 3 ? nums[3] : 1.0);
            return true;
        }
        return false;
    }
    default: return false;
    }
}

std::string trailingComment(std::string_view source, int line) {
    int current = 1;
    size_t start = 0;
    while (current < line && start < source.size()) {
        size_t nl = source.find('\n', start);
        if (nl == std::string_view::npos)
            return {};
        start = nl + 1;
        ++current;
    }
    size_t end = source.find('\n', start);
    std::string_view text = source.substr(start, end == std::string_view::npos ? source.size() - start : end - start);
    char quote = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (quote) {
            if (c == '\\')
                ++i;
            else if (c == quote)
                quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '#') {
            std::string comment(text.substr(i + 1));
            size_t first = comment.find_first_not_of(" \t");
            return first == std::string::npos ? "" : comment.substr(first);
        }
    }
    return {};
}

} // namespace

std::shared_ptr<Module> compileModule(std::string_view source, const std::string& path) {
    auto module = std::make_shared<Module>();
    module->path = path;
    module->source = std::string(source);
    try {
        Program program = parse(source);
        collectScriptVariables(program.statements, module->scriptVariables);
        collectGlobalsInFunctions(program.statements, module->scriptVariables, false);
        Compiler(*module, module->scriptVariables).compileTopLevel(program);
        for (auto& s : program.statements) {
            if (s->kind != StmtKind::Assign || s->targets.size() != 1 || s->targets[0]->kind != ExprKind::Name)
                continue;
            const std::string& name = s->targets[0]->text;
            if (name.empty() || name[0] == '_')
                continue;
            Value v;
            if (!literalValue(*s->expr, v))
                continue;
            auto existing = std::find_if(module->exports.begin(), module->exports.end(),
                                         [&](const ExportedVar& x) { return x.name == name; });
            if (existing != module->exports.end())
                continue;
            module->exports.push_back({name, v, trailingComment(source, s->line), s->line});
        }
    } catch (ScriptError& e) {
        e.file = path;
        throw;
    }
    return module;
}

} // namespace aven::script
