// The Code ladder: EasyScript -> Unity C#, Godot GDScript, Roblox Luau and Unreal C++.
//
// The translator walks the EasyScript syntax tree and writes each statement the way that
// engine usually does it. Aven's built-in API (self.x, key_down, find, play_sound...) maps to
// the other engine's API, events (on_update, on_trigger...) become that engine's callbacks,
// and names follow its conventions (jumpPower in Unity, JumpPower in Unreal). Comments and
// blank lines are kept. Where the engines differ, a note explains it.

#include "aven/script/translate.h"

#include "aven/script/ast.h"
#include "aven/script/errors.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace aven::script {

namespace {

using L = TargetLanguage;

std::vector<std::string> splitWords(const std::string& raw) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : raw) {
        if (c == '_') {
            if (!cur.empty())
                out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

std::string upperFirst(std::string s) {
    if (!s.empty())
        s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
}

std::string camel(const std::string& raw) {
    auto w = splitWords(raw);
    std::string s;
    for (size_t i = 0; i < w.size(); ++i)
        s += i ? upperFirst(w[i]) : w[i];
    return s.empty() ? raw : s;
}

std::string pascal(const std::string& raw) {
    std::string s;
    for (auto& w : splitWords(raw))
        s += upperFirst(w);
    return s.empty() ? raw : s;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r");
    return s.substr(a, b - a + 1);
}

std::string fmtNum(double v) {
    char buf[40];
    if (std::abs(v - std::round(v)) < 1e-9 && std::abs(v) < 1e15)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else
        std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

bool isInteger(double v) { return std::abs(v - std::round(v)) < 1e-9; }

// Joins pieces of a file with one blank line between the ones that aren't empty.
std::string joinSections(std::initializer_list<std::string> parts) {
    std::string out;
    for (std::string p : parts) {
        while (!p.empty() && p.front() == '\n')
            p.erase(0, 1);
        while (!p.empty() && p.back() == '\n')
            p.pop_back();
        if (p.empty())
            continue;
        if (!out.empty())
            out += "\n";
        out += p + "\n";
    }
    return out;
}

struct KeyName {
    const char* aven;
    const char* unity;
    const char* godot;
    const char* roblox;
    const char* unreal;
};

const KeyName kKeys[] = {
    {"space", "Space", "KEY_SPACE", "Space", "SpaceBar"},
    {"left", "LeftArrow", "KEY_LEFT", "Left", "Left"},
    {"right", "RightArrow", "KEY_RIGHT", "Right", "Right"},
    {"up", "UpArrow", "KEY_UP", "Up", "Up"},
    {"down", "DownArrow", "KEY_DOWN", "Down", "Down"},
    {"enter", "Return", "KEY_ENTER", "Return", "Enter"},
    {"return", "Return", "KEY_ENTER", "Return", "Enter"},
    {"escape", "Escape", "KEY_ESCAPE", "Escape", "Escape"},
    {"shift", "LeftShift", "KEY_SHIFT", "LeftShift", "LeftShift"},
    {"ctrl", "LeftControl", "KEY_CTRL", "LeftControl", "LeftControl"},
    {"alt", "LeftAlt", "KEY_ALT", "LeftAlt", "LeftAlt"},
    {"tab", "Tab", "KEY_TAB", "Tab", "Tab"},
    {"backspace", "Backspace", "KEY_BACKSPACE", "Backspace", "BackSpace"},
};

const char* kDigitNames[] = {"Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"};

struct NamedColor {
    const char* name;
    float r, g, b;
};

const NamedColor kColors[] = {
    {"red", 1, 0, 0},        {"green", 0, 1, 0},     {"blue", 0, 0, 1},     {"white", 1, 1, 1},
    {"black", 0, 0, 0},      {"yellow", 1, 0.92f, 0.016f}, {"cyan", 0, 1, 1}, {"magenta", 1, 0, 1},
    {"gray", 0.5f, 0.5f, 0.5f}, {"grey", 0.5f, 0.5f, 0.5f}, {"orange", 1, 0.5f, 0}, {"purple", 0.5f, 0, 0.5f},
    {"pink", 1, 0.6f, 0.8f}, {"brown", 0.55f, 0.35f, 0.2f},
};

const std::set<std::string> kEvents = {"on_start",  "on_update",       "on_fixed_update", "on_collide", "on_collide_end",
                                       "on_trigger", "on_trigger_exit", "on_click",        "on_key_pressed", "on_message",
                                       "on_destroy"};

const std::set<std::string> kEntityMethods = {
    "destroy", "damage",  "heal",  "move",  "move_forward", "turn", "look_at", "move_toward", "distance_to", "direction_to",
    "is_touching", "apply_force", "apply_impulse", "play_animation", "stop_animation", "tween", "clone", "hide", "show",
    "get_component", "add_component", "has_component", "remove_component", "find_child", "play_sound", "send", "say", "emit"};

const std::set<std::string> kListMethods = {"append", "remove", "pop", "insert", "index", "clear", "keys", "values",
                                            "contains", "upper", "lower", "split", "join", "strip", "replace"};

enum class Ty { Unknown, Num, Int, Str, Bool, Obj, Vec, List, Dict, None };

// An object an Aven property or method is used on: `self` or another object's expression.
struct Obj {
    bool self = true;
    std::string text;
};

// How to read and write one Aven property in the target engine.
struct Prop {
    std::string get;
    std::string set;          // "%s" = the new value; empty = read-only
    bool assignable = true;   // `get op= value` works
    // Vector components that can't be assigned on their own (Unity, Roblox, Unreal):
    bool component = false;
    std::string vecGet, vecSet, vecAdd, ctor;
    std::vector<std::string> comps;
    int index = 0;
};

std::string fill(const std::string& format, const std::string& value) {
    std::string out = format;
    size_t p = out.find("%s");
    if (p != std::string::npos)
        out.replace(p, 2, value);
    return out;
}

class Translator {
public:
    Translator(std::string_view source, L lang, TranslateOptions options)
        : lang_(lang), opt_(std::move(options)), source_(source) {}

    Translation run() {
        Translation t;
        try {
            prog_ = parse(source_);
        } catch (const ScriptError& e) {
            t.error = e.what();
            t.errorLine = e.line;
            return t;
        }
        scanComments();
        collect();
        switch (lang_) {
        case L::Unity: unity(); break;
        case L::Godot: godot(); break;
        case L::Roblox: roblox(); break;
        case L::Unreal: unreal(); break;
        }
        t.ok = true;
        t.code = result_;
        t.notes = notes_;
        return t;
    }

private:
    L lang_;
    TranslateOptions opt_;
    std::string source_;
    std::vector<std::string> sourceLines_;
    Program prog_;
    std::map<int, std::string> fullComments_, trailComments_;
    int commentCursor_ = 0;
    std::string result_;
    std::string* out_ = nullptr;
    int depth_ = 0;

    std::vector<std::string> notes_;
    std::set<std::string> helpers_;
    std::set<std::string> services_; // Roblox services, Unity "using" lines
    std::set<std::string> fieldNames_, functions_, coroutines_, gameVars_;
    std::map<std::string, Ty> fieldTypes_, localTypes_;
    std::set<std::string> scope_;                  // locals and parameters of the current function
    std::map<std::string, std::string> aliases_;   // raw name -> text (Unity: dt -> Time.deltaTime)
    std::map<std::string, std::string> returnTypes_;
    bool needsBody_ = false, needsSprite_ = false, needsTimer_ = false;
    bool inCoroutine_ = false;
    std::string keyParam_; // on_key_pressed's parameter while translating it

    struct Field {
        const Stmt* stmt;
        std::string raw;
        bool exported;
        bool literal;
    };
    std::vector<Field> fields_;
    std::vector<const Stmt*> defs_, loose_; // functions, and other top-level statements

    // ------------------------------------------------------------ notes, comments, output

    void note(const std::string& text) {
        if (std::find(notes_.begin(), notes_.end(), text) == notes_.end())
            notes_.push_back(text);
    }

    const char* engine() const { return languageEngine(lang_); }

    void scanComments() {
        std::istringstream in(source_);
        std::string l;
        int n = 0;
        while (std::getline(in, l)) {
            ++n;
            sourceLines_.push_back(l);
            char quote = 0;
            for (size_t i = 0; i < l.size(); ++i) {
                char c = l[i];
                if (quote) {
                    if (c == '\\')
                        ++i;
                    else if (c == quote)
                        quote = 0;
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '#') {
                    std::string text = trim(l.substr(i + 1));
                    bool full = l.find_first_not_of(" \t") == i;
                    (full ? fullComments_ : trailComments_)[n] = text;
                    break;
                }
            }
        }
    }

    bool cLike() const { return lang_ == L::Unity || lang_ == L::Unreal; }
    std::string semi() const { return cLike() ? ";" : ""; }
    std::string comment(const std::string& text) const {
        return (cLike() ? "// " : lang_ == L::Roblox ? "-- " : "# ") + text;
    }

    void emit(const std::string& s) { *out_ += (s.empty() ? "" : std::string(static_cast<size_t>(depth_) * 4, ' ') + s) + "\n"; }
    void blank() {
        if (out_->empty())
            return;
        size_t end = out_->size() - 1; // out_ always ends with '\n'
        size_t start = out_->rfind('\n', end - (end > 0 ? 1 : 0));
        std::string last = trim(out_->substr(start == std::string::npos ? 0 : start + 1, end - (start == std::string::npos ? 0 : start + 1)));
        if (last.empty() || last.back() == '{' || last.back() == ':' || last.back() == '(' ||
            (last.size() >= 5 && (last.compare(last.size() - 5, 5, " then") == 0)) ||
            (last.size() >= 3 && last.compare(last.size() - 3, 3, " do") == 0))
            return;
        *out_ += "\n";
    }
    void line(const std::string& s, int sourceLine) {
        auto t = trailComments_.find(sourceLine);
        emit(t == trailComments_.end() || sourceLine <= 0 ? s : s + "  " + comment(t->second));
    }
    // Comment-only lines (and a blank line) before `upto`.
    void flush(int upto) {
        upto = std::min(upto, static_cast<int>(sourceLines_.size()) + 1);
        bool gap = false;
        for (int l = commentCursor_ + 1; l < upto; ++l) {
            if (l - 1 < static_cast<int>(sourceLines_.size()) && trim(sourceLines_[static_cast<size_t>(l - 1)]).empty()) {
                gap = true;
                continue;
            }
            auto it = fullComments_.find(l);
            if (it != fullComments_.end()) {
                if (gap)
                    blank();
                gap = false;
                emit(comment(it->second));
            }
        }
        if (gap)
            blank();
        commentCursor_ = std::max(commentCursor_, upto - 1);
    }
    void skipComments(int upto) { commentCursor_ = std::max(commentCursor_, upto); }

    // Opens a block: `header` is "if (x)", "while x" or "for i = 1, 3 do" style without the block marker.
    void open(const std::string& header, int sourceLine) {
        if (cLike()) {
            line(header, sourceLine);
            emit("{");
        } else if (lang_ == L::Godot) {
            line(header + ":", sourceLine);
        } else {
            line(header, sourceLine);
        }
        ++depth_;
    }
    void close(bool luaEnd = true) {
        --depth_;
        if (cLike())
            emit("}");
        else if (lang_ == L::Roblox && luaEnd)
            emit("end");
    }

    // ------------------------------------------------------------ names

    std::string safe(std::string s) const {
        static const std::set<std::string> cs = {"string", "object", "base", "event", "new", "class", "default", "params", "lock",
                                                 "out", "ref", "fixed", "checked", "operator", "internal", "public", "private",
                                                 "static", "void", "float", "int", "bool", "var", "this", "is", "as", "delegate"};
        static const std::set<std::string> lua = {"end", "local", "function", "then", "repeat", "until", "nil", "do", "part",
                                                  "game", "workspace", "script", "elseif"};
        static const std::set<std::string> gd = {"func", "var", "signal", "class", "extends", "match", "const", "enum", "await",
                                                 "preload", "yield", "static", "super", "tool"};
        const auto& words = lang_ == L::Roblox ? lua : lang_ == L::Godot ? gd : cs;
        return words.count(s) ? s + "_" : s;
    }

    std::string var(const std::string& raw) const {
        auto a = aliases_.find(raw);
        if (a != aliases_.end())
            return a->second;
        std::string s = raw;
        switch (lang_) {
        case L::Godot: return safe(raw);
        case L::Unity:
        case L::Roblox:
            while (!s.empty() && s[0] == '_')
                s.erase(0, 1);
            return safe(camel(s.empty() ? raw : s));
        case L::Unreal:
            while (!s.empty() && s[0] == '_')
                s.erase(0, 1);
            return safe(pascal(s.empty() ? raw : s));
        }
        return raw;
    }

    std::string func(const std::string& raw) const {
        switch (lang_) {
        case L::Godot: return safe(raw);
        case L::Roblox: return safe(camel(raw));
        default: return safe(pascal(raw));
        }
    }

    std::string gameVar(const std::string& raw) {
        gameVars_.insert(raw);
        switch (lang_) {
        case L::Unity: return "GameState." + camel(raw);
        case L::Godot: return "GameState." + raw;
        case L::Roblox: return "GameState." + camel(raw);
        case L::Unreal: return "GetGameInstance<UMyGameInstance>()->" + pascal(raw);
        }
        return raw;
    }

    std::string quote(const std::string& s) const {
        std::string out;
        for (char c : s) {
            switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
            }
        }
        return lang_ == L::Unreal ? "TEXT(\"" + out + "\")" : "\"" + out + "\"";
    }

    std::string number(double v, bool forceFloat = false) const {
        std::string s = fmtNum(v);
        if (cLike() && (!isInteger(v) || forceFloat))
            return s + "f";
        if (lang_ == L::Godot && forceFloat && isInteger(v))
            return s + ".0";
        return s;
    }

    // ------------------------------------------------------------ first pass

    static bool literal(const Expr* e) {
        if (!e)
            return false;
        switch (e->kind) {
        case ExprKind::Number:
        case ExprKind::String:
        case ExprKind::True:
        case ExprKind::False: return true;
        case ExprKind::Unary: return e->op == Tok::Minus && e->a && e->a->kind == ExprKind::Number;
        default: return false;
        }
    }

    static bool callsWait(const std::vector<StmtPtr>& body) {
        std::function<bool(const Expr*)> inExpr = [&](const Expr* e) -> bool {
            if (!e)
                return false;
            if (e->kind == ExprKind::Call && e->a && e->a->kind == ExprKind::Name && e->a->text == "wait")
                return true;
            if (inExpr(e->a.get()) || inExpr(e->b.get()) || inExpr(e->c.get()))
                return true;
            for (auto& i : e->items)
                if (inExpr(i.get()))
                    return true;
            return false;
        };
        for (auto& s : body) {
            if (s->kind == StmtKind::Def)
                continue;
            if (inExpr(s->expr.get()) || callsWait(s->body) || callsWait(s->orelse))
                return true;
        }
        return false;
    }

    void collect() {
        for (auto& s : prog_.statements) {
            if (s->kind == StmtKind::Assign && s->targets.size() == 1 && s->targets[0]->kind == ExprKind::Name) {
                std::string raw = s->targets[0]->text;
                if (raw == "is_clone")
                    continue; // Aven's own flag for copies made with clone()
                if (!fieldNames_.count(raw)) {
                    fields_.push_back({s.get(), raw, !raw.empty() && raw[0] != '_' && literal(s->expr.get()), literal(s->expr.get())});
                    fieldNames_.insert(raw);
                    fieldTypes_[raw] = infer(s->expr.get());
                } else {
                    loose_.push_back(s.get());
                }
            } else if (s->kind == StmtKind::Def) {
                defs_.push_back(s.get());
                std::string name = symbolName(s->name);
                functions_.insert(name);
                if (callsWait(s->body))
                    coroutines_.insert(name);
            } else if (s->kind != StmtKind::Global && s->kind != StmtKind::Pass) {
                loose_.push_back(s.get());
            }
        }
    }

    const Stmt* handler(const std::string& name) const {
        for (auto* d : defs_)
            if (symbolName(d->name) == name)
                return d;
        return nullptr;
    }

    std::string param(const Stmt* def, size_t i, const std::string& fallback) const {
        return def && i < def->params.size() ? symbolName(def->params[i]) : fallback;
    }

    // ------------------------------------------------------------ types

    Ty infer(const Expr* e) const {
        if (!e)
            return Ty::Unknown;
        switch (e->kind) {
        case ExprKind::Number: return Ty::Num;
        case ExprKind::String:
        case ExprKind::FString: return Ty::Str;
        case ExprKind::True:
        case ExprKind::False:
        case ExprKind::Compare:
        case ExprKind::And:
        case ExprKind::Or: return Ty::Bool;
        case ExprKind::None: return Ty::None;
        case ExprKind::List: return Ty::List;
        case ExprKind::Dict: return Ty::Dict;
        case ExprKind::Self: return Ty::Obj;
        case ExprKind::Ternary: return infer(e->b.get());
        case ExprKind::Unary: return e->op == Tok::Not ? Ty::Bool : infer(e->a.get());
        case ExprKind::Name: {
            auto l = localTypes_.find(e->text);
            if (l != localTypes_.end())
                return l->second;
            auto f = fieldTypes_.find(e->text);
            if (f != fieldTypes_.end() && !scope_.count(e->text))
                return f->second;
            if (e->text == "dt")
                return Ty::Num;
            return Ty::Unknown;
        }
        case ExprKind::Binary: {
            Ty a = infer(e->a.get()), b = infer(e->b.get());
            if (e->op == Tok::Plus && (a == Ty::Str || b == Ty::Str))
                return Ty::Str;
            if (a == Ty::Vec || b == Ty::Vec)
                return Ty::Vec;
            return Ty::Num;
        }
        case ExprKind::Attr: {
            std::string p = symbolName(e->sym);
            if (e->a && e->a->kind == ExprKind::Name && e->a->text == "game")
                return Ty::Num;
            if (infer(e->a.get()) == Ty::Vec)
                return Ty::Num;
            static const std::set<std::string> nums = {"x", "y", "z", "world_x", "world_y", "world_z", "angle", "rotation_x",
                                                       "rotation_y", "rotation_z", "scale_x", "scale_y", "scale_z", "width",
                                                       "height", "alpha", "velocity_x", "velocity_y", "velocity_z", "frame", "order"};
            if (nums.count(p))
                return Ty::Num;
            if (p == "name" || p == "tag" || p == "text" || p == "image" || p == "shape")
                return Ty::Str;
            if (p == "position" || p == "world_position" || p == "velocity" || p == "scale" || p == "forward" || p == "right" ||
                p == "up" || p == "rotation" || p == "size")
                return Ty::Vec;
            if (p == "visible" || p == "active" || p == "on_ground" || p == "exists" || p == "flip_x" || p == "flip_y" || p == "is_clone")
                return Ty::Bool;
            if (p == "parent")
                return Ty::Obj;
            return Ty::Unknown;
        }
        case ExprKind::Call: {
            if (!e->a)
                return Ty::Unknown;
            if (e->a->kind == ExprKind::Attr) {
                std::string m = symbolName(e->a->sym);
                if (m == "distance_to")
                    return Ty::Num;
                if (m == "direction_to")
                    return Ty::Vec;
                if (m == "clone" || m == "find_child")
                    return Ty::Obj;
                if (m == "is_touching" || m == "has_component")
                    return Ty::Bool;
                return Ty::Unknown;
            }
            if (e->a->kind != ExprKind::Name)
                return Ty::Unknown;
            const std::string& f = e->a->text;
            static const std::set<std::string> nums = {"random", "random_range", "abs", "min", "max", "sqrt", "floor", "ceil", "round",
                                                       "clamp", "lerp", "sin", "cos", "tan", "atan2", "asin", "acos", "atan", "pow",
                                                       "sign", "distance", "time", "delta_time", "mouse_x", "mouse_y", "axis",
                                                       "float", "move_toward", "screen_width", "screen_height", "exp", "log",
                                                       "degrees", "radians"};
            if (nums.count(f))
                return Ty::Num;
            if (f == "len" || f == "count" || f == "int" || f == "random_int")
                return Ty::Int;
            if (f == "str")
                return Ty::Str;
            if (f == "find" || f == "spawn" || f == "create_sprite" || f == "create_text" || f == "camera")
                return Ty::Obj;
            if (f == "find_all")
                return Ty::List;
            if (f == "vec" || f == "mouse_position" || f == "direction")
                return Ty::Vec;
            if (f.rfind("key_", 0) == 0 || f.rfind("mouse_down", 0) == 0 || f == "mouse_pressed" || f == "mouse_released" ||
                f == "has_data" || f == "is_paused")
                return Ty::Bool;
            return Ty::Unknown;
        }
        default: return Ty::Unknown;
        }
    }

    std::string typeName(Ty t, bool field = false) const {
        switch (lang_) {
        case L::Unity:
            switch (t) {
            case Ty::Num: return "float";
            case Ty::Int: return "int";
            case Ty::Str: return "string";
            case Ty::Bool: return "bool";
            case Ty::Obj:
            case Ty::None: return "GameObject";
            case Ty::Vec: return opt_.is3D ? "Vector3" : "Vector2";
            case Ty::List: return field ? "List<object>" : "var";
            case Ty::Dict: return field ? "Dictionary<string, object>" : "var";
            default: return field ? "float" : "var";
            }
        case L::Unreal:
            switch (t) {
            case Ty::Num: return "float";
            case Ty::Int: return "int32";
            case Ty::Str: return "FString";
            case Ty::Bool: return "bool";
            case Ty::Obj:
            case Ty::None: return "AActor*";
            case Ty::Vec: return "FVector";
            case Ty::List: return field ? "TArray<float>" : "auto";
            case Ty::Dict: return field ? "TMap<FString, float>" : "auto";
            default: return field ? "float" : "auto";
            }
        default: return "";
        }
    }

    // A parameter's type in C#/C++, guessed from its name.
    std::string paramType(const std::string& raw) const {
        static const std::set<std::string> objects = {"other", "target", "obj", "enemy", "player", "who", "thing", "hit", "coin", "item"};
        static const std::set<std::string> strings = {"name", "message", "msg", "text", "key", "tag", "label", "path", "scene"};
        Ty t = objects.count(raw) ? Ty::Obj : strings.count(raw) ? Ty::Str : raw == "data" ? Ty::Unknown : Ty::Num;
        if (t == Ty::Unknown)
            return lang_ == L::Unity ? "object" : "float";
        return typeName(t, true);
    }

    // ------------------------------------------------------------ expressions

    static int prec(const Expr* e) {
        switch (e->kind) {
        case ExprKind::Ternary: return 1;
        case ExprKind::Or: return 2;
        case ExprKind::And: return 3;
        case ExprKind::Unary: return e->op == Tok::Not ? 4 : 8;
        case ExprKind::Compare: return 5;
        case ExprKind::Binary:
            switch (e->op) {
            case Tok::Plus:
            case Tok::Minus: return 6;
            case Tok::StarStar: return 9;
            default: return 7;
            }
        default: return 10;
        }
    }

    std::string ex(const Expr* e, int minPrec = 0) {
        if (!e)
            return "";
        std::string s = inner(e);
        if (prec(e) < minPrec)
            return "(" + s + ")";
        return s;
    }

    Obj objOf(const Expr* e) {
        if (!e || e->kind == ExprKind::Self)
            return {true, lang_ == L::Unity ? "gameObject" : lang_ == L::Roblox ? "part" : lang_ == L::Godot ? "self" : "this"};
        return {false, ex(e, 10)};
    }

    bool stringy(const Expr* e) const { return infer(e) == Ty::Str; }

    std::string inner(const Expr* e) {
        switch (e->kind) {
        case ExprKind::Number: return number(e->number);
        case ExprKind::String: return quote(e->text);
        case ExprKind::FString: return fstring(e);
        case ExprKind::True: return "true";
        case ExprKind::False: return "false";
        case ExprKind::None: return lang_ == L::Roblox ? "nil" : lang_ == L::Unreal ? "nullptr" : "null";
        case ExprKind::Name: return nameRef(e->text);
        case ExprKind::Self: return objOf(e).text;
        case ExprKind::List: return listLiteral(e);
        case ExprKind::Dict: return dictLiteral(e);
        case ExprKind::Unary:
            if (e->op == Tok::Minus)
                return "-" + ex(e->a.get(), 8);
            if (e->op == Tok::Plus)
                return ex(e->a.get(), 8);
            if (cLike())
                return "!" + ex(e->a.get(), 8);
            return "not " + ex(e->a.get(), lang_ == L::Roblox ? 8 : 4);
        case ExprKind::Binary: return binary(e);
        case ExprKind::And: return ex(e->a.get(), 3) + (cLike() ? " && " : " and ") + ex(e->b.get(), 3);
        case ExprKind::Or: return ex(e->a.get(), 2) + (cLike() ? " || " : " or ") + ex(e->b.get(), 2);
        case ExprKind::Compare: return compare(e);
        case ExprKind::Call: return call(e);
        case ExprKind::Attr: {
            if (e->a && e->a->kind == ExprKind::Name && e->a->text == "game")
                return gameVar(symbolName(e->sym));
            std::string p = symbolName(e->sym);
            if (infer(e->a.get()) == Ty::Vec) {
                std::string comp = lang_ == L::Roblox || lang_ == L::Unreal ? upperFirst(p) : p;
                return ex(e->a.get(), 10) + "." + comp;
            }
            return prop(objOf(e->a.get()), p).get;
        }
        case ExprKind::Index: {
            std::string idx = ex(e->b.get());
            if (lang_ == L::Roblox && infer(e->b.get()) != Ty::Str) {
                if (e->b->kind == ExprKind::Number)
                    idx = fmtNum(e->b->number + 1);
                else
                    idx = ex(e->b.get(), 6) + " + 1";
                note("Luau lists start at 1, not 0, so list[0] in EasyScript is list[1] in Roblox.");
            }
            return ex(e->a.get(), 10) + "[" + idx + "]";
        }
        case ExprKind::Slice:
            note("Slices like list[1:3] are written differently in " + std::string(engine()) + " (look up how to take part of a list or string).");
            return ex(e->a.get(), 10) + "[" + ex(e->b.get()) + ":" + ex(e->c.get()) + "]";
        case ExprKind::Ternary:
            if (cLike())
                return ex(e->a.get(), 2) + " ? " + ex(e->b.get(), 2) + " : " + ex(e->c.get(), 1);
            if (lang_ == L::Roblox)
                return "if " + ex(e->a.get()) + " then " + ex(e->b.get()) + " else " + ex(e->c.get());
            return ex(e->b.get(), 2) + " if " + ex(e->a.get(), 2) + " else " + ex(e->c.get(), 1);
        }
        return "";
    }

    std::string nameRef(const std::string& raw) {
        if (raw == "game") {
            note("'game' variables are shared by all scripts; the translation uses a GameState object for them.");
            return lang_ == L::Unreal ? "GetGameInstance<UMyGameInstance>()" : "GameState";
        }
        if (raw == "PI" || raw == "pi")
            return lang_ == L::Unity ? "Mathf.PI" : lang_ == L::Godot ? "PI" : lang_ == L::Roblox ? "math.pi" : "PI";
        if (functions_.count(raw) && !scope_.count(raw))
            return func(raw);
        return var(raw);
    }

    std::string binary(const Expr* e) {
        const Expr* a = e->a.get();
        const Expr* b = e->b.get();
        int p = prec(e);
        switch (e->op) {
        case Tok::Plus: {
            bool sa = stringy(a), sb = stringy(b);
            if (sa || sb) {
                if (lang_ == L::Roblox)
                    return ex(a, 6) + " .. " + ex(b, 7);
                if (lang_ == L::Godot)
                    return (sa ? ex(a, 6) : "str(" + ex(a) + ")") + " + " + (sb ? ex(b, 7) : "str(" + ex(b) + ")");
                if (lang_ == L::Unreal)
                    return (sa ? ex(a, 6) : "FString::SanitizeFloat(" + ex(a) + ")") + " + " +
                           (sb ? ex(b, 7) : "FString::SanitizeFloat(" + ex(b) + ")");
            }
            return ex(a, p) + " + " + ex(b, p + 1);
        }
        case Tok::Minus:
            if (a->kind == ExprKind::Number && a->number == 0) // blocks write "change by -x" as 0 - x
                return "-" + ex(b, 8);
            return ex(a, p) + " - " + ex(b, p + 1);
        case Tok::Star: return ex(a, p) + " * " + ex(b, p + 1);
        case Tok::Slash: {
            // EasyScript always divides as decimals; C#, C++ and GDScript divide whole numbers as whole numbers.
            if (a->kind == ExprKind::Number && b->kind == ExprKind::Number && lang_ != L::Roblox)
                return number(a->number, true) + " / " + ex(b, p + 1);
            return ex(a, p) + " / " + ex(b, p + 1);
        }
        case Tok::SlashSlash:
            switch (lang_) {
            case L::Unity: return "Mathf.Floor(" + ex(a) + " / " + ex(b, p + 1) + ")";
            case L::Godot: return "floor(" + ex(a) + " / " + ex(b, p + 1) + ")";
            case L::Roblox: return ex(a, p) + " // " + ex(b, p + 1);
            case L::Unreal: return "FMath::FloorToFloat(" + ex(a) + " / " + ex(b, p + 1) + ")";
            }
            break;
        case Tok::Percent:
            switch (lang_) {
            case L::Godot: return "fmod(" + ex(a) + ", " + ex(b) + ")";
            case L::Unreal: return "FMath::Fmod(" + ex(a) + ", " + ex(b) + ")";
            default: return ex(a, p) + " % " + ex(b, p + 1);
            }
        case Tok::StarStar:
            switch (lang_) {
            case L::Unity: return "Mathf.Pow(" + ex(a) + ", " + ex(b) + ")";
            case L::Godot: return "pow(" + ex(a) + ", " + ex(b) + ")";
            case L::Roblox: return ex(a, 10) + " ^ " + ex(b, 9);
            case L::Unreal: return "FMath::Pow(" + ex(a) + ", " + ex(b) + ")";
            }
            break;
        default: break;
        }
        return ex(a, p) + " ? " + ex(b, p + 1);
    }

    std::string keyCheck(const Expr* keyText) {
        std::string k = keyName(keyText);
        switch (lang_) {
        case L::Unity: return "Input.GetKeyDown(" + k + ")";
        case L::Godot: return "event.keycode == " + k;
        case L::Roblox: return "input.KeyCode == " + k;
        case L::Unreal: return "UGameplayStatics::GetPlayerController(this, 0)->WasInputKeyJustPressed(" + k + ")";
        }
        return k;
    }

    std::string compare(const Expr* e) {
        const Expr* a = e->a.get();
        const Expr* b = e->b.get();
        // Two fixed texts (blocks make these): the answer is known.
        if ((e->op == Tok::Eq || e->op == Tok::NotEq) && a && b && a->kind == ExprKind::String && b->kind == ExprKind::String)
            return (a->text == b->text) == (e->op == Tok::Eq) ? "true" : "false";
        // key == "space" inside on_key_pressed -> the engine's key check.
        if ((e->op == Tok::Eq || e->op == Tok::NotEq) && !keyParam_.empty() && a && b) {
            const Expr* name = a->kind == ExprKind::Name && a->text == keyParam_ ? a : b->kind == ExprKind::Name && b->text == keyParam_ ? b : nullptr;
            const Expr* text = name == a ? b : a;
            if (name && text->kind == ExprKind::String) {
                std::string check = keyCheck(text);
                if (e->op == Tok::NotEq)
                    return cLike() ? "!" + check : "not (" + check + ")";
                return check;
            }
        }
        // other.tag == "player" -> each engine's tag check.
        if ((e->op == Tok::Eq || e->op == Tok::NotEq)) {
            const Expr* attr = a && a->kind == ExprKind::Attr && symbolName(a->sym) == "tag" ? a
                               : b && b->kind == ExprKind::Attr && symbolName(b->sym) == "tag" ? b
                                                                                               : nullptr;
            const Expr* value = attr == a ? b : a;
            if (attr && value && value->kind == ExprKind::String) {
                std::string check = tagCheck(objOf(attr->a.get()), quote(value->text));
                if (e->op == Tok::NotEq)
                    return (cLike() ? "!" : "not ") + check;
                return check;
            }
        }
        if (e->op == Tok::In || e->op == Tok::Not || e->notIn) {
            std::string c;
            switch (lang_) {
            case L::Unity: c = ex(b, 10) + ".Contains(" + ex(a) + ")"; break;
            case L::Godot: c = ex(a, 6) + " in " + ex(b, 6); break;
            case L::Roblox: c = "table.find(" + ex(b) + ", " + ex(a) + ") ~= nil"; break;
            case L::Unreal: c = ex(b, 10) + ".Contains(" + ex(a) + ")"; break;
            }
            if (e->notIn)
                return lang_ == L::Godot ? ex(a, 6) + " not in " + ex(b, 6) : (cLike() ? "!" : "not (") + c + (cLike() ? "" : ")");
            return c;
        }
        const char* op = "==";
        switch (e->op) {
        case Tok::Eq: op = "=="; break;
        case Tok::NotEq: op = lang_ == L::Roblox ? "~=" : "!="; break;
        case Tok::Lt: op = "<"; break;
        case Tok::Gt: op = ">"; break;
        case Tok::LtEq: op = "<="; break;
        case Tok::GtEq: op = ">="; break;
        default: break;
        }
        return ex(a, 6) + " " + op + " " + ex(b, 6);
    }

    std::string tagCheck(const Obj& o, const std::string& tag) {
        switch (lang_) {
        case L::Unity: return (o.self ? "" : o.text + ".") + "CompareTag(" + tag + ")";
        case L::Godot:
            note("Godot uses groups instead of tags: add objects to a group in the Node panel, then check is_in_group().");
            return (o.self ? "" : o.text + ".") + "is_in_group(" + tag + ")";
        case L::Roblox:
            note("Roblox tags are set with the Tag Editor (CollectionService). A touching part is often a limb of a character, "
                 "so check other.Parent for the character.");
            return (o.self ? "part" : o.text) + ":HasTag(" + tag + ")";
        case L::Unreal: return (o.self ? "" : o.text + "->") + "ActorHasTag(" + tag + ")";
        }
        return tag;
    }

    std::string fstring(const Expr* e) {
        std::string out;
        auto spec = [&](size_t i) { return i < e->formatSpecs.size() ? e->formatSpecs[i] : std::string(); };
        auto decimals = [](const std::string& s) -> int { // ".2f" -> 2
            if (s.size() >= 3 && s[0] == '.' && s.back() == 'f')
                return std::atoi(s.substr(1, s.size() - 2).c_str());
            return -1;
        };
        auto text = [&](const std::string& t, const char* escapeBraces) {
            std::string r;
            for (char c : t) {
                if (c == '"' && lang_ != L::Roblox)
                    r += "\\\"";
                else if (c == '`' && lang_ == L::Roblox)
                    r += "\\`";
                else if (c == '\\')
                    r += "\\\\";
                else if (c == '\n')
                    r += "\\n";
                else if ((c == '{' || c == '}') && escapeBraces)
                    r += escapeBraces[0] == 'd' ? std::string(2, c) : std::string("\\") + c;
                else if (c == '%' && lang_ == L::Godot)
                    r += "%%";
                else
                    r += c;
            }
            return r;
        };
        switch (lang_) {
        case L::Unity: {
            out = "$\"";
            for (size_t i = 0; i < e->literalParts.size(); ++i) {
                out += text(e->literalParts[i], "d");
                if (i < e->items.size()) {
                    int d = decimals(spec(i));
                    out += "{" + ex(e->items[i].get()) + (d >= 0 ? ":F" + std::to_string(d) : "") + "}";
                }
            }
            return out + "\"";
        }
        case L::Godot: {
            std::string args;
            out = "\"";
            for (size_t i = 0; i < e->literalParts.size(); ++i) {
                out += text(e->literalParts[i], nullptr);
                if (i < e->items.size()) {
                    int d = decimals(spec(i));
                    out += d >= 0 ? "%." + std::to_string(d) + "f" : "%s";
                    args += (args.empty() ? "" : ", ") + ex(e->items[i].get());
                }
            }
            return out + "\" % [" + args + "]";
        }
        case L::Roblox: {
            out = "`";
            for (size_t i = 0; i < e->literalParts.size(); ++i) {
                out += text(e->literalParts[i], "b");
                if (i < e->items.size()) {
                    int d = decimals(spec(i));
                    std::string v = ex(e->items[i].get());
                    out += "{" + (d >= 0 ? "string.format(\"%." + std::to_string(d) + "f\", " + v + ")" : v) + "}";
                }
            }
            return out + "`";
        }
        case L::Unreal: {
            std::string args;
            out = "FString::Format(TEXT(\"";
            for (size_t i = 0; i < e->literalParts.size(); ++i) {
                out += text(e->literalParts[i], "d");
                if (i < e->items.size()) {
                    int d = decimals(spec(i));
                    std::string v = ex(e->items[i].get());
                    out += "{" + std::to_string(i) + "}";
                    args += (args.empty() ? "" : ", ") +
                            (d >= 0 ? "FString::Printf(TEXT(\"%." + std::to_string(d) + "f\"), " + v + ")" : v);
                }
            }
            return out + "\"), { " + args + " })";
        }
        }
        return out;
    }

    std::string listLiteral(const Expr* e) {
        std::string items;
        Ty common = Ty::Unknown;
        bool mixed = false;
        for (size_t i = 0; i < e->items.size(); ++i) {
            items += (i ? ", " : "") + ex(e->items[i].get());
            Ty t = infer(e->items[i].get());
            if (i == 0)
                common = t;
            else if (t != common)
                mixed = true;
        }
        switch (lang_) {
        case L::Unity: {
            std::string t = mixed || common == Ty::Unknown ? "object" : typeName(common, true);
            return "new List<" + t + ">" + (items.empty() ? "()" : " { " + items + " }");
        }
        case L::Unreal: {
            std::string t = mixed || common == Ty::Unknown ? "float" : typeName(common, true);
            return "TArray<" + t + ">{" + items + "}";
        }
        case L::Roblox: return "{" + items + "}";
        default: return "[" + items + "]";
        }
    }

    std::string dictLiteral(const Expr* e) {
        std::string out;
        for (size_t i = 0; i + 1 < e->items.size(); i += 2) {
            const Expr* k = e->items[i].get();
            std::string v = ex(e->items[i + 1].get());
            std::string sep = i ? ", " : "";
            switch (lang_) {
            case L::Unity: out += sep + "{ " + ex(k) + ", " + v + " }"; break;
            case L::Unreal: out += sep + "{ " + ex(k) + ", " + v + " }"; break;
            case L::Godot: out += sep + ex(k) + ": " + v; break;
            case L::Roblox: {
                bool ident = k->kind == ExprKind::String && !k->text.empty() &&
                             std::all_of(k->text.begin(), k->text.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }) &&
                             !std::isdigit(static_cast<unsigned char>(k->text[0]));
                out += sep + (ident ? k->text : "[" + ex(k) + "]") + " = " + v;
                break;
            }
            }
        }
        switch (lang_) {
        case L::Unity: return "new Dictionary<string, object>" + (out.empty() ? std::string("()") : " { " + out + " }");
        case L::Unreal: return "TMap<FString, float>{" + out + "}";
        default: return "{" + out + "}";
        }
    }

    // ------------------------------------------------------------ properties

    Prop plain(const std::string& get, bool readOnly = false) {
        Prop p;
        p.get = get;
        if (!readOnly)
            p.set = get + " = %s";
        return p;
    }
    Prop vecComp(const std::string& vecGet, const std::string& vecSet, const std::string& vecAdd, const std::string& ctor,
                 std::vector<std::string> comps, int index) {
        Prop p;
        p.component = true;
        p.assignable = false;
        p.vecGet = vecGet;
        p.vecSet = vecSet;
        p.vecAdd = vecAdd;
        p.ctor = ctor;
        p.comps = std::move(comps);
        p.index = index;
        p.get = vecGet + "." + p.comps[static_cast<size_t>(index)];
        p.set = "%s";
        return p;
    }
    Prop getSet(const std::string& get, const std::string& set) {
        Prop p;
        p.get = get;
        p.set = set;
        p.assignable = false;
        return p;
    }

    static int axisIndex(const std::string& p) {
        char last = p.back();
        return last == 'x' ? 0 : last == 'y' ? 1 : 2;
    }

    std::string unityT(const Obj& o) const { return o.self ? "transform" : o.text + ".transform"; }
    std::string unityBody(const Obj& o) {
        if (o.self) {
            needsBody_ = true;
            return "rb";
        }
        return o.text + (opt_.is3D ? ".GetComponent<Rigidbody>()" : ".GetComponent<Rigidbody2D>()");
    }
    std::string unitySprite(const Obj& o) {
        if (opt_.is3D)
            return (o.self ? "" : o.text + ".") + "GetComponent<Renderer>().material";
        if (o.self) {
            needsSprite_ = true;
            return "sr";
        }
        return o.text + ".GetComponent<SpriteRenderer>()";
    }
    std::string unrealBody(const Obj& o) {
        if (o.self) {
            needsBody_ = true;
            return "Body";
        }
        return "Cast<UPrimitiveComponent>(" + o.text + "->GetRootComponent())";
    }

    Prop prop(const Obj& o, const std::string& p) {
        bool xyz = p == "x" || p == "y" || p == "z" || p == "world_x" || p == "world_y" || p == "world_z";
        bool velocity = p == "velocity_x" || p == "velocity_y" || p == "velocity_z";
        bool scale = p == "scale_x" || p == "scale_y" || p == "scale_z";
        bool rotation = p == "rotation_x" || p == "rotation_y" || p == "rotation_z";
        if (p == "on_ground") {
            if (!o.self)
                note("Checking whether another object is on the ground needs code on that object in " + std::string(engine()) + ".");
            helpers_.insert("grounded");
            switch (lang_) {
            case L::Unity: needsBody_ = true; return plain("IsGrounded()", true);
            case L::Godot: return plain("is_on_ground()", true);
            case L::Roblox: return plain("isOnGround()", true);
            case L::Unreal: return plain("IsOnGround()", true);
            }
        }
        if (p == "tag") {
            switch (lang_) {
            case L::Unity: return plain(o.self ? "tag" : o.text + ".tag");
            case L::Godot:
                note("Godot uses groups instead of tags (add_to_group, is_in_group).");
                return getSet((o.self ? "" : o.text + ".") + "get_groups()[0]", (o.self ? "" : o.text + ".") + "add_to_group(%s)");
            case L::Roblox: return getSet((o.self ? "part" : o.text) + ":GetTags()[1]", (o.self ? "part" : o.text) + ":AddTag(%s)");
            case L::Unreal: return getSet((o.self ? "" : o.text + "->") + "Tags[0]", (o.self ? "" : o.text + "->") + "Tags.Add(%s)");
            }
        }
        switch (lang_) {
        case L::Unity: {
            std::string T = unityT(o);
            std::string GO = o.self ? "gameObject" : o.text;
            if (xyz)
                return vecComp(T + ".position", T + ".position = %s", T + ".position += %s", "new Vector3", {"x", "y", "z"}, axisIndex(p));
            if (p == "position" || p == "world_position")
                return plain(T + ".position");
            if (p == "angle")
                return vecComp(T + ".eulerAngles", T + ".eulerAngles = %s", T + ".Rotate(%s)", "new Vector3", {"x", "y", "z"}, opt_.is3D ? 1 : 2);
            if (p == "rotation")
                return plain(T + ".eulerAngles");
            if (rotation)
                return vecComp(T + ".eulerAngles", T + ".eulerAngles = %s", T + ".Rotate(%s)", "new Vector3", {"x", "y", "z"}, axisIndex(p));
            if (p == "scale")
                return plain(T + ".localScale");
            if (scale)
                return vecComp(T + ".localScale", T + ".localScale = %s", T + ".localScale += %s", "new Vector3", {"x", "y", "z"}, axisIndex(p));
            if (velocity || p == "velocity") {
                std::string rb = unityBody(o);
                note("Unity 6 renamed Rigidbody.velocity to linearVelocity; older versions use velocity.");
                if (p == "velocity")
                    return plain(rb + ".velocity");
                if (opt_.is3D)
                    return vecComp(rb + ".velocity", rb + ".velocity = %s", rb + ".velocity += %s", "new Vector3", {"x", "y", "z"}, axisIndex(p));
                return vecComp(rb + ".velocity", rb + ".velocity = %s", rb + ".velocity += %s", "new Vector2", {"x", "y"},
                               std::min(axisIndex(p), 1));
            }
            if (p == "name")
                return plain(o.self ? "name" : o.text + ".name");
            if (p == "visible")
                return plain((o.self ? "" : o.text + ".") + "GetComponent<Renderer>().enabled");
            if (p == "active")
                return getSet(GO + ".activeSelf", GO + ".SetActive(%s)");
            if (p == "exists")
                return plain(o.self ? "true" : o.text + " != null", true);
            if (p == "color")
                return plain(unitySprite(o) + ".color");
            if (p == "alpha") {
                std::string sr = unitySprite(o);
                return vecComp(sr + ".color", sr + ".color = %s", "", "new Color", {"r", "g", "b", "a"}, 3);
            }
            if (p == "text") {
                note("Unity shows text with TextMeshPro (TMP_Text).");
                return plain((o.self ? "" : o.text + ".") + "GetComponent<TMPro.TMP_Text>().text");
            }
            if (p == "flip_x" || p == "flip_y")
                return plain(unitySprite(o) + (p == "flip_x" ? ".flipX" : ".flipY"));
            if (p == "order")
                return plain(unitySprite(o) + ".sortingOrder");
            if (p == "parent")
                return plain(T + ".parent.gameObject", true);
            if (p == "forward")
                return plain(T + (opt_.is3D ? ".forward" : ".right"), true);
            if (p == "right")
                return plain(T + ".right", true);
            if (p == "up")
                return plain(T + ".up", true);
            return plain((o.self ? "" : o.text + ".") + camel(p));
        }
        case L::Godot: {
            std::string P = o.self ? "" : o.text + ".";
            if (xyz)
                return plain(P + (p.rfind("world_", 0) == 0 ? "global_position." : "position.") + p.substr(p.size() - 1));
            if (p == "position")
                return plain(P + "position");
            if (p == "world_position")
                return plain(P + "global_position");
            if (p == "angle")
                return plain(P + (opt_.is3D ? "rotation_degrees.y" : "rotation_degrees"));
            if (p == "rotation")
                return plain(P + "rotation_degrees");
            if (rotation)
                return plain(P + "rotation_degrees." + p.substr(p.size() - 1));
            if (p == "scale")
                return plain(P + "scale");
            if (scale)
                return plain(P + "scale." + p.substr(p.size() - 1));
            if (velocity)
                return plain(P + "linear_velocity." + p.substr(p.size() - 1));
            if (p == "velocity")
                return plain(P + "linear_velocity");
            if (p == "name" || p == "visible")
                return plain(P + p);
            if (p == "active")
                return getSet(P + "visible", P + "set_process(%s)");
            if (p == "exists")
                return plain(o.self ? "true" : "is_instance_valid(" + o.text + ")", true);
            if (p == "color")
                return plain(P + "modulate");
            if (p == "alpha")
                return plain(P + "modulate.a");
            if (p == "text") {
                note("In Godot, text is shown by a Label node; the script should be on the Label.");
                return plain(P + "text");
            }
            if (p == "flip_x" || p == "flip_y")
                return plain(P + (p == "flip_x" ? "flip_h" : "flip_v"));
            if (p == "order")
                return plain(P + "z_index");
            if (p == "parent")
                return plain(P + "get_parent()", true);
            return plain(P + p);
        }
        case L::Roblox: {
            std::string O = o.self ? "part" : o.text;
            if (xyz)
                return vecComp(O + ".Position", O + ".Position = %s", O + ".Position += %s", "Vector3.new", {"X", "Y", "Z"}, axisIndex(p));
            if (p == "position" || p == "world_position")
                return plain(O + ".Position");
            if (p == "angle")
                return vecComp(O + ".Orientation", O + ".Orientation = %s", O + ".Orientation += %s", "Vector3.new", {"X", "Y", "Z"},
                               opt_.is3D ? 1 : 2);
            if (p == "rotation")
                return plain(O + ".Orientation");
            if (rotation)
                return vecComp(O + ".Orientation", O + ".Orientation = %s", O + ".Orientation += %s", "Vector3.new", {"X", "Y", "Z"}, axisIndex(p));
            if (p == "scale" || scale) {
                note("Roblox parts have a Size instead of a scale.");
                if (p == "scale")
                    return plain(O + ".Size");
                return vecComp(O + ".Size", O + ".Size = %s", O + ".Size += %s", "Vector3.new", {"X", "Y", "Z"}, axisIndex(p));
            }
            if (velocity)
                return vecComp(O + ".AssemblyLinearVelocity", O + ".AssemblyLinearVelocity = %s", O + ".AssemblyLinearVelocity += %s",
                               "Vector3.new", {"X", "Y", "Z"}, axisIndex(p));
            if (p == "velocity")
                return plain(O + ".AssemblyLinearVelocity");
            if (p == "name")
                return plain(O + ".Name");
            if (p == "visible")
                return getSet(O + ".Transparency < 1", O + ".Transparency = if %s then 0 else 1");
            if (p == "exists")
                return plain(o.self ? "true" : O + ".Parent ~= nil", true);
            if (p == "color")
                return plain(O + ".Color");
            if (p == "alpha")
                return getSet("1 - " + O + ".Transparency", O + ".Transparency = 1 - (%s)");
            if (p == "text") {
                note("In Roblox, text is shown by a TextLabel (in a BillboardGui or ScreenGui).");
                return plain(O + ".Text");
            }
            if (p == "parent")
                return plain(O + ".Parent");
            return plain(O + "." + pascal(p));
        }
        case L::Unreal: {
            std::string Pre = o.self ? "" : o.text + "->";
            if (xyz)
                return vecComp(Pre + "GetActorLocation()", Pre + "SetActorLocation(%s)", Pre + "AddActorWorldOffset(%s)", "FVector",
                               {"X", "Y", "Z"}, axisIndex(p));
            if (p == "position" || p == "world_position")
                return getSet(Pre + "GetActorLocation()", Pre + "SetActorLocation(%s)");
            if (p == "angle")
                return vecComp(Pre + "GetActorRotation()", Pre + "SetActorRotation(%s)", Pre + "AddActorWorldRotation(%s)", "FRotator",
                               {"Pitch", "Yaw", "Roll"}, 1);
            if (p == "rotation")
                return getSet(Pre + "GetActorRotation()", Pre + "SetActorRotation(%s)");
            if (rotation)
                return vecComp(Pre + "GetActorRotation()", Pre + "SetActorRotation(%s)", Pre + "AddActorWorldRotation(%s)", "FRotator",
                               {"Roll", "Pitch", "Yaw"}, axisIndex(p));
            if (p == "scale")
                return getSet(Pre + "GetActorScale3D()", Pre + "SetActorScale3D(%s)");
            if (scale)
                return vecComp(Pre + "GetActorScale3D()", Pre + "SetActorScale3D(%s)", "", "FVector", {"X", "Y", "Z"}, axisIndex(p));
            if (velocity || p == "velocity") {
                std::string b = unrealBody(o);
                if (p == "velocity")
                    return getSet(Pre + "GetVelocity()", b + "->SetPhysicsLinearVelocity(%s)");
                return vecComp(Pre + "GetVelocity()", b + "->SetPhysicsLinearVelocity(%s)", b + "->SetPhysicsLinearVelocity(%s, true)",
                               "FVector", {"X", "Y", "Z"}, axisIndex(p));
            }
            if (p == "name")
                return plain(Pre + "GetActorNameOrLabel()", true);
            if (p == "visible")
                return getSet("!" + Pre + "IsHidden()", Pre + "SetActorHiddenInGame(!(%s))");
            if (p == "exists")
                return plain(o.self ? "true" : "IsValid(" + o.text + ")", true);
            if (p == "parent")
                return plain(Pre + "GetAttachParentActor()", true);
            if (p == "color" || p == "alpha" || p == "text" || p == "flip_x" || p == "order")
                note("In Unreal, " + p + " belongs to a component (a sprite, mesh material or text render), not the actor.");
            return plain(Pre + pascal(p));
        }
        }
        return plain(o.text + "." + p);
    }

    // A color written as "red" becomes that engine's color value.
    std::string colorValue(const std::string& name) {
        for (auto& c : kColors) {
            if (name != c.name)
                continue;
            auto f = [&](float v) { return number(static_cast<double>(v), true); };
            switch (lang_) {
            case L::Unity: {
                static const std::set<std::string> builtIn = {"red", "green", "blue", "white", "black", "yellow", "cyan", "magenta", "gray", "grey"};
                if (builtIn.count(name))
                    return "Color." + name;
                return "new Color(" + f(c.r) + ", " + f(c.g) + ", " + f(c.b) + ")";
            }
            case L::Godot: return "Color(\"" + name + "\")";
            case L::Roblox:
                return "Color3.fromRGB(" + fmtNum(std::round(c.r * 255)) + ", " + fmtNum(std::round(c.g * 255)) + ", " +
                       fmtNum(std::round(c.b * 255)) + ")";
            case L::Unreal: return "FLinearColor(" + f(c.r) + ", " + f(c.g) + ", " + f(c.b) + ")";
            }
        }
        if (lang_ == L::Godot)
            return "Color(" + quote(name) + ")";
        note("Colors like \"" + name + "\" are written as color values in " + std::string(engine()) + ".");
        return quote(name);
    }

    // `target.x = value`, `target.x += value`...
    std::string setProp(const Obj& o, const std::string& p, const std::string& op, const Expr* valueExpr) {
        std::string value = ex(valueExpr, op == "=" ? 0 : 7);
        if (p == "color" && valueExpr && valueExpr->kind == ExprKind::String)
            value = colorValue(valueExpr->text);
        Prop pr = prop(o, p);
        if (pr.set.empty()) {
            note(p + " can't be changed directly in " + std::string(engine()) + ".");
            return comment(pr.get + " " + op + " " + value);
        }
        if (!pr.component) {
            if (op == "=")
                return fill(pr.set, value);
            if (pr.assignable)
                return pr.get + " " + op + " " + value;
            return fill(pr.set, pr.get + " " + op.substr(0, 1) + " " + value);
        }
        size_t n = pr.comps.size();
        auto vector = [&](const std::function<std::string(size_t)>& part) {
            std::string args;
            for (size_t i = 0; i < n; ++i)
                args += (i ? ", " : "") + part(i);
            return pr.ctor + "(" + args + ")";
        };
        auto idx = static_cast<size_t>(pr.index);
        if ((op == "+=" || op == "-=") && !pr.vecAdd.empty()) {
            std::string v = op == "-=" ? "-" + ex(valueExpr, 8) : value;
            return fill(pr.vecAdd, vector([&](size_t i) { return i == idx ? v : std::string("0"); }));
        }
        std::string newValue = op == "=" ? value : pr.get + " " + op.substr(0, 1) + " " + value;
        return fill(pr.vecSet, vector([&](size_t i) { return i == idx ? newValue : pr.vecGet + "." + pr.comps[i]; }));
    }

    // ------------------------------------------------------------ calls

    struct Args {
        std::vector<const Expr*> pos;
        std::map<std::string, const Expr*> named;
        const Expr* at(size_t i, const char* name = nullptr) const {
            if (name) {
                auto it = named.find(name);
                if (it != named.end())
                    return it->second;
            }
            return i < pos.size() ? pos[i] : nullptr;
        }
        size_t size() const { return pos.size() + named.size(); }
    };

    Args argsOf(const Expr* call) const {
        Args a;
        size_t kw = call->kwNames.size();
        size_t positional = call->items.size() - kw;
        for (size_t i = 0; i < call->items.size(); ++i) {
            if (i < positional)
                a.pos.push_back(call->items[i].get());
            else
                a.named[symbolName(call->kwNames[i - positional])] = call->items[i].get();
        }
        return a;
    }

    std::string arg(const Args& a, size_t i, const std::string& fallback = "", const char* name = nullptr, int minPrec = 0) {
        const Expr* e = a.at(i, name);
        return e ? ex(e, minPrec) : fallback;
    }

    std::string argList(const Args& a) {
        std::string s;
        for (size_t i = 0; i < a.pos.size(); ++i)
            s += (i ? ", " : "") + ex(a.pos[i]);
        for (auto& [k, v] : a.named)
            s += (s.empty() ? "" : ", ") + ex(v);
        return s;
    }

    // "sounds/coin.wav" -> the way the engine names that asset.
    std::string assetPath(const Expr* e, const std::string& newExtension = "") {
        if (!e || e->kind != ExprKind::String)
            return ex(e);
        std::string p = e->text;
        std::string stem = p.substr(0, p.rfind('.') == std::string::npos ? p.size() : p.rfind('.'));
        switch (lang_) {
        case L::Unity: return quote(stem);
        case L::Godot: return quote("res://" + (newExtension.empty() ? p : stem + newExtension));
        case L::Roblox: return quote(stem.substr(stem.rfind('/') == std::string::npos ? 0 : stem.rfind('/') + 1));
        case L::Unreal: {
            std::string name = stem.substr(stem.rfind('/') == std::string::npos ? 0 : stem.rfind('/') + 1);
            return "TEXT(\"/Game/" + stem + "." + name + "\")";
        }
        }
        return quote(p);
    }

    std::string keyName(const Expr* e) {
        if (!e || e->kind != ExprKind::String) {
            note("Keys stored in variables are written as key codes in " + std::string(engine()) + ".");
            return ex(e);
        }
        std::string k = e->text;
        std::transform(k.begin(), k.end(), k.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (auto& kn : kKeys)
            if (k == kn.aven) {
                switch (lang_) {
                case L::Unity: return std::string("KeyCode.") + kn.unity;
                case L::Godot: return kn.godot;
                case L::Roblox: return std::string("Enum.KeyCode.") + kn.roblox;
                case L::Unreal: return std::string("EKeys::") + kn.unreal;
                }
            }
        if (k.size() == 1 && std::isalpha(static_cast<unsigned char>(k[0]))) {
            std::string up(1, static_cast<char>(std::toupper(static_cast<unsigned char>(k[0]))));
            switch (lang_) {
            case L::Unity: return "KeyCode." + up;
            case L::Godot: return "KEY_" + up;
            case L::Roblox: return "Enum.KeyCode." + up;
            case L::Unreal: return "EKeys::" + up;
            }
        }
        if (k.size() == 1 && std::isdigit(static_cast<unsigned char>(k[0]))) {
            int d = k[0] - '0';
            switch (lang_) {
            case L::Unity: return "KeyCode.Alpha" + k;
            case L::Godot: return "KEY_" + k;
            case L::Roblox: return std::string("Enum.KeyCode.") + kDigitNames[d];
            case L::Unreal: return std::string("EKeys::") + kDigitNames[d];
            }
        }
        return ex(e);
    }

    std::string position(const Expr* target) {
        if (infer(target) == Ty::Vec)
            return ex(target);
        std::string t = ex(target, 10);
        if (target && target->kind == ExprKind::Self)
            t = objOf(target).text;
        switch (lang_) {
        case L::Unity: return target && target->kind == ExprKind::Self ? "transform.position" : t + ".transform.position";
        case L::Godot: return target && target->kind == ExprKind::Self ? "global_position" : t + ".global_position";
        case L::Roblox: return t + ".Position";
        case L::Unreal: return target && target->kind == ExprKind::Self ? "GetActorLocation()" : t + "->GetActorLocation()";
        }
        return t;
    }

    std::string vec(const std::string& x, const std::string& y, const std::string& z = "0") const {
        switch (lang_) {
        case L::Unity: return opt_.is3D || z != "0" ? "new Vector3(" + x + ", " + y + ", " + z + ")" : "new Vector2(" + x + ", " + y + ")";
        case L::Godot: return opt_.is3D || z != "0" ? "Vector3(" + x + ", " + y + ", " + z + ")" : "Vector2(" + x + ", " + y + ")";
        case L::Roblox: return "Vector3.new(" + x + ", " + y + ", " + z + ")";
        case L::Unreal: return "FVector(" + x + ", " + y + ", " + z + ")";
        }
        return "";
    }

    std::string log(const Args& a) {
        std::string joined;
        for (size_t i = 0; i < a.pos.size(); ++i) {
            if (lang_ == L::Unity)
                joined += (i ? " + \" \" + " : "") + ex(a.pos[i], 7);
            else
                joined += (i ? ", " : "") + ex(a.pos[i]);
        }
        switch (lang_) {
        case L::Unity: return "Debug.Log(" + joined + ")";
        case L::Godot:
        case L::Roblox: return "print(" + joined + ")";
        case L::Unreal: {
            const Expr* first = a.at(0);
            if (!first)
                return "UE_LOG(LogTemp, Log, TEXT(\"\"))";
            if (first->kind == ExprKind::String && a.pos.size() == 1)
                return "UE_LOG(LogTemp, Log, " + quote(first->text) + ")";
            if (infer(first) == Ty::Str)
                return "UE_LOG(LogTemp, Log, TEXT(\"%s\"), *(" + ex(first) + "))";
            return "UE_LOG(LogTemp, Log, TEXT(\"%f\"), " + ex(first) + ")";
        }
        }
        return joined;
    }

    std::string unsupported(const std::string& name, const std::string& generic) {
        note(name + "() has no single matching command in " + std::string(engine()) + "; the translation keeps the name so you can "
             "look up how that engine does it.");
        return generic;
    }

    std::string call(const Expr* e) {
        Args a = argsOf(e);
        const Expr* callee = e->a.get();
        if (callee && callee->kind == ExprKind::Attr) {
            std::string m = symbolName(callee->sym);
            const Expr* target = callee->a.get();
            Ty t = infer(target);
            if ((kListMethods.count(m) && !kEntityMethods.count(m)) || t == Ty::List || t == Ty::Str || t == Ty::Dict)
                return listMethod(target, m, a);
            return method(objOf(target), m, a);
        }
        if (!callee || callee->kind != ExprKind::Name)
            return ex(callee, 10) + "(" + argList(a) + ")";
        const std::string& f = callee->text;
        if (functions_.count(f)) {
            std::string c = func(f) + "(" + argList(a) + ")";
            if (lang_ == L::Unity && coroutines_.count(f))
                return "StartCoroutine(" + c + ")";
            return c;
        }
        return global(f, a, e);
    }

    std::string listMethod(const Expr* target, const std::string& m, const Args& a) {
        std::string t = ex(target, 10);
        std::string x = arg(a, 0);
        if (m == "append") {
            switch (lang_) {
            case L::Unity:
            case L::Unreal: return t + ".Add(" + x + ")";
            case L::Godot: return t + ".append(" + x + ")";
            case L::Roblox: return "table.insert(" + ex(target) + ", " + x + ")";
            }
        }
        if (m == "remove") {
            switch (lang_) {
            case L::Unity: return t + ".Remove(" + x + ")";
            case L::Unreal: return t + ".Remove(" + x + ")";
            case L::Godot: return t + ".erase(" + x + ")";
            case L::Roblox: return "table.remove(" + ex(target) + ", table.find(" + ex(target) + ", " + x + "))";
            }
        }
        if (m == "pop") {
            switch (lang_) {
            case L::Unity: note("C# lists don't have pop(): read the last item, then RemoveAt."); return t + "[" + t + ".Count - 1]";
            case L::Unreal: return t + ".Pop()";
            case L::Godot: return t + ".pop_back()";
            case L::Roblox: return "table.remove(" + ex(target) + ")";
            }
        }
        if (m == "clear") {
            switch (lang_) {
            case L::Unity: return t + ".Clear()";
            case L::Unreal: return t + ".Empty()";
            case L::Godot: return t + ".clear()";
            case L::Roblox: return "table.clear(" + ex(target) + ")";
            }
        }
        if (m == "upper" || m == "lower") {
            switch (lang_) {
            case L::Unity: return t + (m == "upper" ? ".ToUpper()" : ".ToLower()");
            case L::Unreal: return t + (m == "upper" ? ".ToUpper()" : ".ToLower()");
            case L::Godot: return t + (m == "upper" ? ".to_upper()" : ".to_lower()");
            case L::Roblox: return t + (m == "upper" ? ":upper()" : ":lower()");
            }
        }
        std::string name = lang_ == L::Godot ? m : lang_ == L::Roblox ? camel(m) : pascal(m);
        return t + (lang_ == L::Roblox ? ":" : ".") + name + "(" + argList(a) + ")";
    }

    std::string method(const Obj& o, const std::string& m, const Args& a) {
        std::string a0 = arg(a, 0), a1 = arg(a, 1);
        switch (lang_) {
        case L::Unity: {
            std::string T = unityT(o), GO = o.self ? "gameObject" : o.text;
            std::string self = o.self ? "" : o.text + ".";
            if (m == "destroy") return "Destroy(" + GO + ")";
            if (m == "move") return T + ".position += new Vector3(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + ")";
            if (m == "move_forward") return T + ".position += " + T + (opt_.is3D ? ".forward" : ".right") + " * " + arg(a, 0, "1", nullptr, 7);
            if (m == "turn") return T + (opt_.is3D ? ".Rotate(0, " + a0 + ", 0)" : ".Rotate(0, 0, " + a0 + ")");
            if (m == "look_at") {
                if (opt_.is3D)
                    return T + ".LookAt(" + position(a.at(0)) + ")";
                return T + ".right = " + position(a.at(0)) + " - " + T + ".position";
            }
            if (m == "move_toward") return T + ".position = Vector3.MoveTowards(" + T + ".position, " + position(a.at(0)) + ", " + a1 + ")";
            if (m == "distance_to") return "Vector3.Distance(" + T + ".position, " + position(a.at(0)) + ")";
            if (m == "direction_to") return "(" + position(a.at(0)) + " - " + T + ".position).normalized";
            if (m == "is_touching") {
                if (a.at(0) && a.at(0)->kind == ExprKind::String)
                    note("Unity checks touching with OnCollisionEnter2D/OnTriggerEnter2D and CompareTag, not by tag name.");
                return self + "GetComponent<Collider2D>().IsTouching(" + a0 + ".GetComponent<Collider2D>())";
            }
            if (m == "apply_force" || m == "apply_impulse") {
                std::string v = opt_.is3D ? "new Vector3(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + ")" : "new Vector2(" + a0 + ", " + a1 + ")";
                std::string mode = m == "apply_impulse" ? (opt_.is3D ? ", ForceMode.Impulse" : ", ForceMode2D.Impulse") : "";
                return unityBody(o) + ".AddForce(" + v + mode + ")";
            }
            if (m == "hide" || m == "show") return self + "GetComponent<Renderer>().enabled = " + (m == "show" ? "true" : "false");
            if (m == "clone") return "Instantiate(" + GO + ")";
            if (m == "play_sound") return global("play_sound", a, nullptr);
            if (m == "emit") return self + "GetComponent<ParticleSystem>().Emit(" + a0 + ")";
            if (m == "say") return "Debug.Log(" + GO + ".name + \": \" + " + ex(a.at(0), 7) + ")";
            if (m == "send") return GO + ".SendMessage(\"OnMessage\", " + a0 + ")";
            if (m == "damage" || m == "heal") {
                note("Health is your own script in Unity (a Health component with TakeDamage and Heal methods).");
                return self + "GetComponent<Health>()." + (m == "damage" ? "TakeDamage(" : "Heal(") + a0 + ")";
            }
            if (m == "find_child") return T + ".Find(" + a0 + ").gameObject";
            if (m == "get_component") return self + "GetComponent(" + a0 + ")";
            if (m == "has_component") return self + "GetComponent(" + a0 + ") != null";
            if (m == "play_animation" || m == "stop_animation") {
                note("Unity plays animations with an Animator and animation clips instead of frame numbers.");
                return self + "GetComponent<Animator>()." + (m == "play_animation" ? "Play(\"Walk\")" : "StopPlayback()");
            }
            return unsupported(m, self + pascal(m) + "(" + argList(a) + ")");
        }
        case L::Godot: {
            std::string P = o.self ? "" : o.text + ".";
            if (m == "destroy") return P + "queue_free()";
            if (m == "move") return P + "position += " + vec(a0, a1, arg(a, 2, "0"));
            if (m == "move_forward") {
                if (opt_.is3D)
                    return P + "translate_object_local(Vector3.FORWARD * " + arg(a, 0, "1", nullptr, 7) + ")";
                return P + "position += Vector2.RIGHT.rotated(" + P + "rotation) * " + arg(a, 0, "1", nullptr, 7);
            }
            if (m == "turn") return P + (opt_.is3D ? "rotation_degrees.y += " : "rotation_degrees += ") + a0;
            if (m == "look_at") return P + "look_at(" + position(a.at(0)) + ")";
            if (m == "move_toward") return P + "global_position = " + P + "global_position.move_toward(" + position(a.at(0)) + ", " + a1 + ")";
            if (m == "distance_to") return P + "global_position.distance_to(" + position(a.at(0)) + ")";
            if (m == "direction_to") return P + "global_position.direction_to(" + position(a.at(0)) + ")";
            if (m == "is_touching") {
                if (a.at(0) && a.at(0)->kind == ExprKind::String)
                    return P + "get_colliding_bodies().any(func(body): return body.is_in_group(" + a0 + "))";
                return P + "get_colliding_bodies().has(" + a0 + ")";
            }
            if (m == "apply_force") return P + "apply_central_force(" + vec(a0, a1, arg(a, 2, "0")) + ")";
            if (m == "apply_impulse") return P + "apply_central_impulse(" + vec(a0, a1, arg(a, 2, "0")) + ")";
            if (m == "hide" || m == "show") return P + m + "()";
            if (m == "clone") {
                note("In Godot, duplicate() makes a copy; add it to the scene with add_child().");
                return P + "duplicate()";
            }
            if (m == "play_sound") return global("play_sound", a, nullptr);
            if (m == "emit") {
                note("Godot particles are a GPUParticles2D or CPUParticles2D child node.");
                return P + "get_node(\"GPUParticles2D\").restart()";
            }
            if (m == "say") return "print(" + (o.self ? std::string("name") : o.text + ".name") + ", \": \", " + a0 + ")";
            if (m == "send") return P + "on_message(" + a0 + ", " + arg(a, 1, "null") + ")";
            if (m == "damage" || m == "heal") return P + m + "(" + a0 + ")";
            if (m == "find_child") return P + "find_child(" + a0 + ")";
            if (m == "has_component") return P + "has_node(" + a0 + ")";
            if (m == "get_component") return P + "get_node(" + a0 + ")";
            if (m == "play_animation" || m == "stop_animation") {
                note("Godot plays animations with an AnimatedSprite2D or AnimationPlayer node.");
                return P + "get_node(\"AnimatedSprite2D\")." + (m == "play_animation" ? "play()" : "stop()");
            }
            return unsupported(m, P + m + "(" + argList(a) + ")");
        }
        case L::Roblox: {
            std::string O = o.self ? "part" : o.text;
            if (m == "destroy") return O + ":Destroy()";
            if (m == "move") return O + ".Position += Vector3.new(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + ")";
            if (m == "move_forward") return O + ".CFrame += " + O + ".CFrame.LookVector * " + arg(a, 0, "1", nullptr, 7);
            if (m == "turn") return O + ".CFrame *= CFrame.Angles(0, " + (opt_.is3D ? "math.rad(" + a0 + "), 0)" : "0, math.rad(" + a0 + "))");
            if (m == "look_at") return O + ".CFrame = CFrame.lookAt(" + O + ".Position, " + position(a.at(0)) + ")";
            if (m == "move_toward") {
                helpers_.insert("moveToward");
                return "moveToward(" + O + ", " + position(a.at(0)) + ", " + a1 + ")";
            }
            if (m == "distance_to") return "(" + position(a.at(0)) + " - " + O + ".Position).Magnitude";
            if (m == "direction_to") return "(" + position(a.at(0)) + " - " + O + ".Position).Unit";
            if (m == "is_touching") {
                if (a.at(0) && a.at(0)->kind == ExprKind::String) {
                    helpers_.insert("touchingTag");
                    return "isTouchingTag(" + O + ", " + a0 + ")";
                }
                return "table.find(" + O + ":GetTouchingParts(), " + a0 + ") ~= nil";
            }
            if (m == "apply_force" || m == "apply_impulse") {
                if (m == "apply_force")
                    note("Steady forces in Roblox use a VectorForce constraint; ApplyImpulse gives a single push.");
                return O + ":ApplyImpulse(Vector3.new(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + "))";
            }
            if (m == "hide") return O + ".Transparency = 1";
            if (m == "show") return O + ".Transparency = 0";
            if (m == "clone") return O + ":Clone()";
            if (m == "play_sound") return global("play_sound", a, nullptr);
            if (m == "emit") return O + ".ParticleEmitter:Emit(" + a0 + ")";
            if (m == "say") return "game:GetService(\"TextChatService\"):DisplayBubble(" + O + ", " + a0 + ")";
            if (m == "damage") {
                note("In Roblox, players have a Humanoid with Health; TakeDamage lowers it. Touching parts belong to the character (other.Parent).");
                return O + ".Parent.Humanoid:TakeDamage(" + a0 + ")";
            }
            if (m == "heal") return O + ".Parent.Humanoid.Health += " + a0;
            if (m == "find_child") return O + ":FindFirstChild(" + a0 + ")";
            if (m == "has_component") return O + ":FindFirstChildOfClass(" + a0 + ") ~= nil";
            if (m == "get_component") return O + ":FindFirstChildOfClass(" + a0 + ")";
            if (m == "send") {
                helpers_.insert("messages");
                return "messages:Fire(" + a0 + ", " + arg(a, 1, "nil") + ")";
            }
            return unsupported(m, O + ":" + pascal(m) + "(" + argList(a) + ")");
        }
        case L::Unreal: {
            std::string Pre = o.self ? "" : o.text + "->";
            std::string self = o.self ? "this" : o.text;
            if (m == "destroy") return Pre + "Destroy()";
            if (m == "move") return Pre + "AddActorWorldOffset(FVector(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + "))";
            if (m == "move_forward") return Pre + "AddActorWorldOffset(" + Pre + "GetActorForwardVector() * " + arg(a, 0, "1", nullptr, 7) + ")";
            if (m == "turn") return Pre + "AddActorWorldRotation(FRotator(0, " + a0 + ", 0))";
            if (m == "look_at") return Pre + "SetActorRotation((" + position(a.at(0)) + " - " + Pre + "GetActorLocation()).Rotation())";
            if (m == "move_toward")
                return Pre + "SetActorLocation(FMath::VInterpConstantTo(" + Pre + "GetActorLocation(), " + position(a.at(0)) + ", 1.f, " + a1 + "))";
            if (m == "distance_to") return "FVector::Dist(" + Pre + "GetActorLocation(), " + position(a.at(0)) + ")";
            if (m == "direction_to") return "(" + position(a.at(0)) + " - " + Pre + "GetActorLocation()).GetSafeNormal()";
            if (m == "is_touching") return Pre + "IsOverlappingActor(" + a0 + ")";
            if (m == "apply_force") return unrealBody(o) + "->AddForce(FVector(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + "))";
            if (m == "apply_impulse") return unrealBody(o) + "->AddImpulse(FVector(" + a0 + ", " + a1 + ", " + arg(a, 2, "0") + "))";
            if (m == "hide" || m == "show") return Pre + "SetActorHiddenInGame(" + (m == "hide" ? "true" : "false") + ")";
            if (m == "play_sound") return global("play_sound", a, nullptr);
            if (m == "say") return log(a);
            if (m == "damage") return "UGameplayStatics::ApplyDamage(" + self + ", " + a0 + ", nullptr, this, nullptr)";
            if (m == "clone" || m == "emit" || m == "send" || m == "heal")
                note("In Unreal, " + m + "() is done with Blueprints or your own C++ functions.");
            return unsupported(m, Pre + pascal(m) + "(" + argList(a) + ")");
        }
        }
        return "";
    }

    std::string math1(const std::string& f, const Args& a) {
        struct M {
            const char* aven;
            const char* unity;
            const char* godot;
            const char* roblox;
            const char* unreal;
        };
        static const M table[] = {
            {"abs", "Mathf.Abs", "abs", "math.abs", "FMath::Abs"},
            {"min", "Mathf.Min", "min", "math.min", "FMath::Min"},
            {"max", "Mathf.Max", "max", "math.max", "FMath::Max"},
            {"sqrt", "Mathf.Sqrt", "sqrt", "math.sqrt", "FMath::Sqrt"},
            {"floor", "Mathf.Floor", "floor", "math.floor", "FMath::FloorToFloat"},
            {"ceil", "Mathf.Ceil", "ceil", "math.ceil", "FMath::CeilToFloat"},
            {"round", "Mathf.Round", "round", "math.round", "FMath::RoundToFloat"},
            {"clamp", "Mathf.Clamp", "clamp", "math.clamp", "FMath::Clamp"},
            {"lerp", "Mathf.Lerp", "lerp", "", "FMath::Lerp"},
            {"sign", "Mathf.Sign", "sign", "math.sign", "FMath::Sign"},
            {"pow", "Mathf.Pow", "pow", "math.pow", "FMath::Pow"},
            {"exp", "Mathf.Exp", "exp", "math.exp", "FMath::Exp"},
            {"log", "Mathf.Log", "log", "math.log", "FMath::Loge"},
            {"move_toward", "Mathf.MoveTowards", "move_toward", "", "FMath::FInterpConstantTo"},
        };
        for (auto& m : table) {
            if (f != m.aven)
                continue;
            const char* name = lang_ == L::Unity ? m.unity : lang_ == L::Godot ? m.godot : lang_ == L::Roblox ? m.roblox : m.unreal;
            if (lang_ == L::Roblox && f == "lerp")
                return ex(a.at(0), 6) + " + (" + arg(a, 1) + " - " + ex(a.at(0), 6) + ") * " + arg(a, 2, "0", nullptr, 7);
            if (lang_ == L::Roblox && f == "move_toward") {
                helpers_.insert("moveTowardNumber");
                return "moveTowardNumber(" + argList(a) + ")";
            }
            if (lang_ == L::Unreal && f == "move_toward")
                return "FMath::FInterpConstantTo(" + arg(a, 0) + ", " + arg(a, 1) + ", 1.f, " + arg(a, 2) + ")";
            return std::string(name) + "(" + argList(a) + ")";
        }
        return "";
    }

    std::string trig(const std::string& f, const Args& a) {
        std::string x = arg(a, 0);
        bool inverse = f == "asin" || f == "acos" || f == "atan" || f == "atan2";
        std::string inner = f == "atan2" ? arg(a, 0) + ", " + arg(a, 1) : x;
        switch (lang_) {
        case L::Unity: {
            std::string fn = "Mathf." + upperFirst(f);
            return inverse ? fn + "(" + inner + ") * Mathf.Rad2Deg" : fn + "(" + ex(a.at(0), 7) + " * Mathf.Deg2Rad)";
        }
        case L::Godot: return inverse ? "rad_to_deg(" + f + "(" + inner + "))" : f + "(deg_to_rad(" + x + "))";
        case L::Roblox: return inverse ? "math.deg(math." + f + "(" + inner + "))" : "math." + f + "(math.rad(" + x + "))";
        case L::Unreal: {
            std::string fn = "FMath::" + upperFirst(f);
            if (f == "atan2")
                fn = "FMath::Atan2";
            return inverse ? "FMath::RadiansToDegrees(" + fn + "(" + inner + "))" : fn + "(FMath::DegreesToRadians(" + x + "))";
        }
        }
        return "";
    }

    std::string global(const std::string& f, const Args& a, const Expr* e) {
        std::string a0 = arg(a, 0), a1 = arg(a, 1);
        auto pick = [&](const std::string& u, const std::string& g, const std::string& r, const std::string& ue) {
            return lang_ == L::Unity ? u : lang_ == L::Godot ? g : lang_ == L::Roblox ? r : ue;
        };
        if (f == "print")
            return log(a);
        if (std::string m = math1(f, a); !m.empty())
            return m;
        if (f == "sin" || f == "cos" || f == "tan" || f == "asin" || f == "acos" || f == "atan" || f == "atan2")
            return trig(f, a);
        if (f == "radians")
            return pick(ex(a.at(0), 7) + " * Mathf.Deg2Rad", "deg_to_rad(" + a0 + ")", "math.rad(" + a0 + ")", "FMath::DegreesToRadians(" + a0 + ")");
        if (f == "degrees")
            return pick(ex(a.at(0), 7) + " * Mathf.Rad2Deg", "rad_to_deg(" + a0 + ")", "math.deg(" + a0 + ")", "FMath::RadiansToDegrees(" + a0 + ")");
        if (f == "random")
            return pick("Random.value", "randf()", "math.random()", "FMath::FRand()");
        if (f == "random_range") {
            auto flt = [&](size_t i) {
                const Expr* x = a.at(i);
                return x && x->kind == ExprKind::Number ? number(x->number, true) : arg(a, i);
            };
            return pick("Random.Range(" + flt(0) + ", " + flt(1) + ")", "randf_range(" + a0 + ", " + a1 + ")",
                        "math.random() * (" + a1 + " - " + ex(a.at(0), 6) + ") + " + ex(a.at(0), 6),
                        "FMath::FRandRange(" + a0 + ", " + a1 + ")");
        }
        if (f == "random_int") {
            const Expr* hi = a.at(1);
            std::string hiPlus = hi && hi->kind == ExprKind::Number ? fmtNum(hi->number + 1) : ex(hi, 6) + " + 1";
            if (lang_ == L::Unity)
                note("Unity's Random.Range(min, max) with whole numbers never returns max, so the translation adds 1.");
            return pick("Random.Range(" + a0 + ", " + hiPlus + ")", "randi_range(" + a0 + ", " + a1 + ")",
                        "math.random(" + a0 + ", " + a1 + ")", "FMath::RandRange(" + a0 + ", " + a1 + ")");
        }
        if (f == "choice") {
            std::string l = ex(a.at(0), 10);
            return pick(l + "[Random.Range(0, " + l + ".Count)]", l + ".pick_random()", l + "[math.random(1, #" + l + ")]",
                        l + "[FMath::RandRange(0, " + l + ".Num() - 1)]");
        }
        if (f == "len") {
            std::string l = ex(a.at(0), 10);
            bool str = stringy(a.at(0));
            return pick(l + (str ? ".Length" : ".Count"), "len(" + a0 + ")", "#" + l, l + (str ? ".Len()" : ".Num()"));
        }
        if (f == "str" && a.at(0) && stringy(a.at(0)))
            return ex(a.at(0), 6);
        if (f == "str")
            return pick(ex(a.at(0), 10) + ".ToString()", "str(" + a0 + ")", "tostring(" + a0 + ")", "FString::SanitizeFloat(" + a0 + ")");
        if (f == "int")
            return pick("(int)" + ex(a.at(0), 10), "int(" + a0 + ")", "math.floor(" + a0 + ")", "(int32)" + ex(a.at(0), 10));
        if (f == "float")
            return pick("(float)" + ex(a.at(0), 10), "float(" + a0 + ")", "tonumber(" + a0 + ")", "(float)" + ex(a.at(0), 10));
        if (f == "vec")
            return vec(a0, a1, arg(a, 2, "0"));
        if (f == "rgb")
            return pick("new Color(" + ex(a.at(0), 7) + " / 255f, " + ex(a.at(1), 7) + " / 255f, " + arg(a, 2, "0", nullptr, 7) + " / 255f)",
                        "Color8(" + argList(a) + ")", "Color3.fromRGB(" + argList(a) + ")", "FColor(" + argList(a) + ")");
        if (f == "color" && a.at(0) && a.at(0)->kind == ExprKind::String)
            return colorValue(a.at(0)->text);
        if (f == "key_down" || f == "key_pressed" || f == "key_released") {
            std::string k = keyName(a.at(0));
            if (lang_ == L::Unity)
                return std::string("Input.") + (f == "key_down" ? "GetKey(" : f == "key_pressed" ? "GetKeyDown(" : "GetKeyUp(") + k + ")";
            if (lang_ == L::Godot) {
                if (f == "key_down")
                    return "Input.is_key_pressed(" + k + ")";
                note("Godot notices single key presses through input actions (Project Settings > Input Map). Make one per key.");
                std::string action = a.at(0) && a.at(0)->kind == ExprKind::String ? quote(a.at(0)->text) : a0;
                return std::string(f == "key_pressed" ? "Input.is_action_just_pressed(" : "Input.is_action_just_released(") + action + ")";
            }
            if (lang_ == L::Roblox) {
                services_.insert("UserInputService");
                note("Keyboard input in Roblox runs in a LocalScript (on the player's computer).");
                if (f != "key_down")
                    note("Roblox reacts to single key presses with UserInputService.InputBegan:Connect(...).");
                return "UserInputService:IsKeyDown(" + k + ")";
            }
            std::string pc = "UGameplayStatics::GetPlayerController(this, 0)->";
            note("Unreal projects usually use Enhanced Input actions instead of checking keys directly.");
            return pc + (f == "key_down" ? "IsInputKeyDown(" : f == "key_pressed" ? "WasInputKeyJustPressed(" : "WasInputKeyJustReleased(") + k + ")";
        }
        if (f == "mouse_down" || f == "mouse_pressed" || f == "mouse_released") {
            std::string button = a.at(0) && a.at(0)->kind == ExprKind::String ? a.at(0)->text : "left";
            int index = button == "right" ? 1 : button == "middle" ? 2 : 0;
            switch (lang_) {
            case L::Unity:
                return std::string("Input.") + (f == "mouse_down" ? "GetMouseButton(" : f == "mouse_pressed" ? "GetMouseButtonDown(" : "GetMouseButtonUp(") +
                       std::to_string(index) + ")";
            case L::Godot: {
                std::string b = index == 1 ? "MOUSE_BUTTON_RIGHT" : index == 2 ? "MOUSE_BUTTON_MIDDLE" : "MOUSE_BUTTON_LEFT";
                if (f != "mouse_down")
                    note("Godot notices single clicks through an input action or _input(event).");
                return f == "mouse_down" ? "Input.is_mouse_button_pressed(" + b + ")" : "Input.is_action_just_pressed(\"click\")";
            }
            case L::Roblox:
                services_.insert("UserInputService");
                return "UserInputService:IsMouseButtonPressed(Enum.UserInputType.MouseButton" + std::to_string(index + 1) + ")";
            case L::Unreal: {
                std::string k = index == 1 ? "EKeys::RightMouseButton" : index == 2 ? "EKeys::MiddleMouseButton" : "EKeys::LeftMouseButton";
                return "UGameplayStatics::GetPlayerController(this, 0)->" +
                       std::string(f == "mouse_down" ? "IsInputKeyDown(" : f == "mouse_pressed" ? "WasInputKeyJustPressed(" : "WasInputKeyJustReleased(") + k + ")";
            }
            }
        }
        if (f == "mouse_x" || f == "mouse_y" || f == "mouse_position") {
            std::string c = f == "mouse_position" ? "" : f == "mouse_x" ? "x" : "y";
            switch (lang_) {
            case L::Unity: return "Camera.main.ScreenToWorldPoint(Input.mousePosition)" + (c.empty() ? "" : "." + c);
            case L::Godot: return "get_global_mouse_position()" + (c.empty() ? "" : "." + c);
            case L::Roblox:
                helpers_.insert("mouse");
                return "mouse.Hit.Position" + (c.empty() ? "" : "." + upperFirst(c));
            case L::Unreal: helpers_.insert("mouseWorld"); return "MouseWorld()" + (c.empty() ? "" : "." + upperFirst(c));
            }
        }
        if (f == "axis") {
            bool vertical = a.at(0) && a.at(0)->kind == ExprKind::String && a.at(0)->text == "vertical";
            switch (lang_) {
            case L::Unity: return std::string("Input.GetAxisRaw(") + (vertical ? "\"Vertical\"" : "\"Horizontal\"") + ")";
            case L::Godot:
                if (vertical)
                    note("In Godot 2D, y grows downward, so moving up means a smaller y.");
                return vertical ? "Input.get_axis(\"ui_down\", \"ui_up\")" : "Input.get_axis(\"ui_left\", \"ui_right\")";
            case L::Roblox: helpers_.insert("axis"); services_.insert("UserInputService"); return "axis(" + a0 + ")";
            case L::Unreal: helpers_.insert("axis"); return "Axis(" + a0 + ")";
            }
        }
        if (f == "find") {
            if (lang_ == L::Unreal)
                helpers_.insert("findActor");
            return pick("GameObject.Find(" + a0 + ")", "get_tree().current_scene.find_child(" + a0 + ", true, false)",
                        "workspace:FindFirstChild(" + a0 + ", true)", "FindActorByName(" + a0 + ")");
        }
        if (f == "find_all" || f == "count") {
            std::string list;
            switch (lang_) {
            case L::Unity: list = "GameObject.FindGameObjectsWithTag(" + a0 + ")"; break;
            case L::Godot: list = "get_tree().get_nodes_in_group(" + a0 + ")"; break;
            case L::Roblox: services_.insert("CollectionService"); list = "CollectionService:GetTagged(" + a0 + ")"; break;
            case L::Unreal: helpers_.insert("findTagged"); list = "FindActorsWithTag(" + a0 + ")"; break;
            }
            if (f == "find_all")
                return list;
            return pick(list + ".Length", list + ".size()", "#" + list, list + ".Num()");
        }
        if (f == "destroy") {
            const Expr* x = a.at(0);
            return method(objOf(x), "destroy", Args{});
        }
        if (f == "distance") {
            std::string p0 = position(a.at(0)), p1 = position(a.at(1));
            return pick("Vector3.Distance(" + p0 + ", " + p1 + ")", p0 + ".distance_to(" + p1 + ")", "(" + p0 + " - " + p1 + ").Magnitude",
                        "FVector::Dist(" + p0 + ", " + p1 + ")");
        }
        if (f == "direction") {
            std::string p0 = position(a.at(0)), p1 = position(a.at(1));
            return pick("(" + p1 + " - " + p0 + ").normalized", p0 + ".direction_to(" + p1 + ")", "(" + p1 + " - " + p0 + ").Unit",
                        "(" + p1 + " - " + p0 + ").GetSafeNormal()");
        }
        if (f == "spawn") {
            std::string x = arg(a, 1, "0"), y = arg(a, 2, "0");
            switch (lang_) {
            case L::Unity:
                note("Unity's Resources.Load finds prefabs in a folder named Resources; a public GameObject field works too.");
                return "Instantiate(Resources.Load<GameObject>(" + assetPath(a.at(0)) + "), new Vector3(" + x + ", " + y + ", 0), Quaternion.identity)";
            case L::Godot: helpers_.insert("spawn"); return "spawn(" + assetPath(a.at(0), ".tscn") + ", " + vec(x, y) + ")";
            case L::Roblox:
                helpers_.insert("spawn");
                note("Roblox copies objects kept in ReplicatedStorage with :Clone() instead of prefabs.");
                return "spawnCopy(" + assetPath(a.at(0)) + ", " + x + ", " + y + ")";
            case L::Unreal:
                note("Unreal spawns Blueprint classes; a TSubclassOf<AActor> property is the usual way to pick one.");
                return "GetWorld()->SpawnActor<AActor>(LoadClass<AActor>(nullptr, " + assetPath(a.at(0)) + "), FVector(" + x + ", " + y +
                       ", 0), FRotator::ZeroRotator)";
            }
        }
        if (f == "play_sound" || f == "play_music") {
            switch (lang_) {
            case L::Unity:
                note("Unity plays sounds with an AudioSource; AudioClips loaded from a Resources folder work like Aven's sound paths.");
                return "AudioSource.PlayClipAtPoint(Resources.Load<AudioClip>(" + assetPath(a.at(0)) + "), transform.position)";
            case L::Godot: helpers_.insert("play_sound"); return "play_sound(" + assetPath(a.at(0)) + ")";
            case L::Roblox:
                helpers_.insert("playSound");
                note("Roblox sounds are uploaded to Roblox and used by id, like \"rbxassetid://1234\".");
                return "playSound(" + assetPath(a.at(0)) + ")";
            case L::Unreal:
                return "UGameplayStatics::PlaySoundAtLocation(this, LoadObject<USoundBase>(nullptr, " + assetPath(a.at(0)) + "), GetActorLocation())";
            }
        }
        if (f == "load_scene") {
            const Expr* s = a.at(0);
            std::string name = s && s->kind == ExprKind::String ? s->text : "";
            std::string stem = name.substr(name.rfind('/') == std::string::npos ? 0 : name.rfind('/') + 1);
            stem = stem.substr(0, stem.rfind('.') == std::string::npos ? stem.size() : stem.rfind('.'));
            switch (lang_) {
            case L::Unity: services_.insert("UnityEngine.SceneManagement"); return "SceneManager.LoadScene(" + (stem.empty() ? a0 : quote(stem)) + ")";
            case L::Godot: return "get_tree().change_scene_to_file(" + assetPath(s, ".tscn") + ")";
            case L::Roblox:
                note("A Roblox experience is one place; other places are reached with TeleportService.");
                return "-- go to " + (stem.empty() ? a0 : stem) + ": use TeleportService";
            case L::Unreal: return "UGameplayStatics::OpenLevel(this, FName(" + (stem.empty() ? a0 : quote(stem)) + "))";
            }
        }
        if (f == "restart_scene") {
            switch (lang_) {
            case L::Unity: services_.insert("UnityEngine.SceneManagement"); return "SceneManager.LoadScene(SceneManager.GetActiveScene().buildIndex)";
            case L::Godot: return "get_tree().reload_current_scene()";
            case L::Roblox: return "for _, player in game.Players:GetPlayers() do player:LoadCharacter() end";
            case L::Unreal: return "UGameplayStatics::OpenLevel(this, FName(*UGameplayStatics::GetCurrentLevelName(this)))";
            }
        }
        if (f == "get_game") {
            const Expr* n = a.at(0);
            if (n && n->kind == ExprKind::String)
                return gameVar(n->text);
            return unsupported(f, "get_game(" + argList(a) + ")");
        }
        if (f == "broadcast") {
            switch (lang_) {
            case L::Unity: helpers_.insert("broadcast"); return "Broadcast(" + argList(a) + ")";
            case L::Godot: return "get_tree().call_group(\"listeners\", \"on_message\", " + a0 + ", " + arg(a, 1, "null") + ")";
            case L::Roblox: helpers_.insert("messages"); return "messages:Fire(" + a0 + ", " + arg(a, 1, "nil") + ")";
            case L::Unreal:
                note("Unreal sends messages between actors with event dispatchers (delegates) or interfaces.");
                return "OnMessage.Broadcast(" + a0 + ")";
            }
        }
        if (f == "time")
            return pick("Time.time", "Time.get_ticks_msec() / 1000.0", "os.clock()", "GetWorld()->GetTimeSeconds()");
        if (f == "delta_time")
            return pick("Time.deltaTime", "get_process_delta_time()", "task.wait()", "GetWorld()->GetDeltaSeconds()");
        if (f == "quit")
            return pick("Application.Quit()", "get_tree().quit()", "-- games can't quit themselves on Roblox",
                        "UKismetSystemLibrary::QuitGame(this, nullptr, EQuitPreference::Quit, false)");
        if (f == "pause_game" || f == "resume_game") {
            bool p = f == "pause_game";
            return pick(std::string("Time.timeScale = ") + (p ? "0" : "1"), std::string("get_tree().paused = ") + (p ? "true" : "false"),
                        "-- pausing isn't built into Roblox", std::string("UGameplayStatics::SetGamePaused(this, ") + (p ? "true" : "false") + ")");
        }
        if (f == "set_time_scale")
            return pick("Time.timeScale = " + a0, "Engine.time_scale = " + a0, "-- Roblox has no time scale",
                        "UGameplayStatics::SetGlobalTimeDilation(this, " + a0 + ")");
        if (f == "set_gravity")
            return pick((opt_.is3D ? "Physics.gravity = new Vector3(" : "Physics2D.gravity = new Vector2(") + a0 + ", " + a1 + ")",
                        "PhysicsServer2D.area_set_param(get_world_2d().space, PhysicsServer2D.AREA_PARAM_GRAVITY_VECTOR, " + vec(a0, a1) + ")",
                        "workspace.Gravity = -(" + a1 + ")", "-- set gravity in Project Settings > Physics");
        if (f == "camera")
            return pick("Camera.main", "get_viewport().get_camera_2d()", "workspace.CurrentCamera", "UGameplayStatics::GetPlayerCameraManager(this, 0)");
        if (f == "screen_width" || f == "screen_height") {
            bool w = f == "screen_width";
            return pick(w ? "Screen.width" : "Screen.height", std::string("get_viewport_rect().size.") + (w ? "x" : "y"),
                        std::string("workspace.CurrentCamera.ViewportSize.") + (w ? "X" : "Y"),
                        std::string("GEngine->GameViewport->Viewport->GetSizeXY().") + (w ? "X" : "Y"));
        }
        if (f == "save_data" || f == "load_data" || f == "has_data" || f == "delete_data") {
            if (lang_ == L::Unity) {
                if (f == "save_data") return "PlayerPrefs.SetFloat(" + a0 + ", " + a1 + ")";
                if (f == "load_data") return "PlayerPrefs.GetFloat(" + a0 + ", " + arg(a, 1, "0") + ")";
                if (f == "has_data") return "PlayerPrefs.HasKey(" + a0 + ")";
                return "PlayerPrefs.DeleteKey(" + a0 + ")";
            }
            note(std::string("Saving data works differently in ") + engine() + ": " +
                 (lang_ == L::Godot ? "use a ConfigFile or FileAccess." : lang_ == L::Roblox ? "use DataStoreService on the server." : "use a USaveGame object."));
            return unsupported(f, (cLike() ? pascal(f) : lang_ == L::Roblox ? camel(f) : f) + "(" + argList(a) + ")");
        }
        if (f == "after" || f == "every") {
            const Expr* fn = a.at(1);
            std::string fnName = fn && fn->kind == ExprKind::Name ? fn->text : "";
            bool repeat = f == "every";
            switch (lang_) {
            case L::Unity:
                if (!fnName.empty())
                    return (repeat ? "InvokeRepeating(nameof(" + func(fnName) + "), " + a0 + ", " + a0 + ")" : "Invoke(nameof(" + func(fnName) + "), " + a0 + ")");
                break;
            case L::Godot:
                if (repeat) {
                    helpers_.insert("every");
                    return "every(" + a0 + ", " + a1 + ")";
                }
                return "get_tree().create_timer(" + a0 + ").timeout.connect(" + a1 + ")";
            case L::Roblox:
                if (repeat)
                    return "task.spawn(function() while true do task.wait(" + a0 + ") " + a1 + "() end end)";
                return "task.delay(" + a0 + ", " + a1 + ")";
            case L::Unreal:
                if (!fnName.empty()) {
                    needsTimer_ = true;
                    return "GetWorldTimerManager().SetTimer(Timer, this, &" + className() + "::" + func(fnName) + ", " + a0 + ", " +
                           (repeat ? "true" : "false") + ")";
                }
                break;
            }
            return unsupported(f, f + "(" + argList(a) + ")");
        }
        if (f == "wait") {
            // wait() as part of an expression (it's normally its own statement).
            return pick("new WaitForSeconds(" + a0 + ")", "await get_tree().create_timer(" + a0 + ").timeout", "task.wait(" + a0 + ")", "/* wait */ 0");
        }
        if (f == "camera_shake" || f == "tween" || f == "create_sprite" || f == "create_text" || f == "raycast" || f == "stop_music" ||
            f == "set_volume" || f == "lock_mouse" || f == "set_fullscreen" || f == "start_task" || f == "stop_timer") {
            std::string name = lang_ == L::Godot ? f : lang_ == L::Roblox ? camel(f) : pascal(f);
            return unsupported(f, name + "(" + argList(a) + ")");
        }
        if (f == "range" || f == "list" || f == "sorted" || f == "reversed" || f == "sum" || f == "type" || f == "bool" || f == "dict" ||
            f == "shuffle" || f == "hsv" || f == "random_seed")
            return unsupported(f, (lang_ == L::Godot ? f : cLike() ? pascal(f) : camel(f)) + "(" + argList(a) + ")");
        (void)e;
        // Something defined elsewhere (or unknown): keep it, in the engine's naming style.
        return func(f) + "(" + argList(a) + ")";
    }

    // ------------------------------------------------------------ statements

    void body(const std::vector<StmtPtr>& stmts) {
        size_t before = out_->size();
        for (auto& s : stmts)
            stmt(*s);
        if (lang_ == L::Godot && out_->size() == before)
            emit("pass");
    }

    void declare(const std::string& raw, const Expr* value, int sourceLine) {
        scope_.insert(raw);
        Ty t = infer(value);
        localTypes_[raw] = t;
        std::string v = ex(value);
        switch (lang_) {
        case L::Unity:
        case L::Unreal: {
            std::string type = typeName(t);
            if (t == Ty::None)
                type = typeName(Ty::Obj);
            line(type + " " + var(raw) + " = " + v + ";", sourceLine);
            break;
        }
        case L::Godot: line("var " + var(raw) + " = " + v, sourceLine); break;
        case L::Roblox: line("local " + var(raw) + " = " + v, sourceLine); break;
        }
    }

    std::string opText(Tok op) const {
        switch (op) {
        case Tok::PlusAssign: return "+=";
        case Tok::MinusAssign: return "-=";
        case Tok::StarAssign: return "*=";
        case Tok::SlashAssign: return "/=";
        case Tok::PercentAssign: return "%=";
        default: return "=";
        }
    }

    void assign(const Expr* target, const std::string& op, const Expr* value, int sourceLine) {
        if (target->kind == ExprKind::Name) {
            const std::string& raw = target->text;
            if (op == "=" && !scope_.count(raw) && !fieldNames_.count(raw)) {
                declare(raw, value, sourceLine);
                return;
            }
            if (op == "%=" && (lang_ == L::Godot || lang_ == L::Unreal)) {
                std::string n = var(raw);
                line(n + " = " + (lang_ == L::Godot ? "fmod(" : "FMath::Fmod(") + n + ", " + ex(value) + ")" + semi(), sourceLine);
                return;
            }
            line(var(raw) + " " + op + " " + ex(value) + semi(), sourceLine);
            return;
        }
        if (target->kind == ExprKind::Attr) {
            std::string p = symbolName(target->sym);
            if (target->a && target->a->kind == ExprKind::Name && target->a->text == "game") {
                line(gameVar(p) + " " + op + " " + ex(value) + semi(), sourceLine);
                return;
            }
            if (infer(target->a.get()) == Ty::Vec) {
                std::string comp = lang_ == L::Roblox || lang_ == L::Unreal ? upperFirst(p) : p;
                if (lang_ == L::Roblox)
                    note("Roblox vectors can't be changed one part at a time; make a new Vector3 instead.");
                line(ex(target->a.get(), 10) + "." + comp + " " + op + " " + ex(value) + semi(), sourceLine);
                return;
            }
            std::string s = setProp(objOf(target->a.get()), p, op, value);
            line(s + (s.rfind("//", 0) == 0 || s.rfind("--", 0) == 0 || s.rfind("#", 0) == 0 ? "" : semi()), sourceLine);
            return;
        }
        line(ex(target) + " " + op + " " + ex(value) + semi(), sourceLine);
    }

    void fallback(const Stmt& s, const std::string& why) {
        note(why);
        int l = s.line;
        std::string src = l > 0 && l <= static_cast<int>(sourceLines_.size()) ? trim(sourceLines_[static_cast<size_t>(l - 1)]) : "";
        emit(comment("EasyScript: " + src));
    }

    void ifChain(const Stmt& s) {
        auto branch = [&](const std::string& header, const std::vector<StmtPtr>& b, int sourceLine, bool first) {
            if (cLike()) {
                line(header, sourceLine);
                emit("{");
                ++depth_;
                body(b);
                --depth_;
                emit("}");
            } else if (lang_ == L::Godot) {
                line(header + ":", sourceLine);
                ++depth_;
                body(b);
                --depth_;
            } else {
                line(header, sourceLine);
                ++depth_;
                body(b);
                --depth_;
            }
            (void)first;
        };
        auto cond = [&](const Stmt& st, bool elif) {
            std::string c = ex(st.expr.get());
            switch (lang_) {
            case L::Unity:
            case L::Unreal: return std::string(elif ? "else if (" : "if (") + c + ")";
            case L::Godot: return std::string(elif ? "elif " : "if ") + c;
            case L::Roblox: return std::string(elif ? "elseif " : "if ") + c + " then";
            }
            return c;
        };
        branch(cond(s, false), s.body, s.line, true);
        const Stmt* cur = &s;
        while (cur->orelse.size() == 1 && cur->orelse[0]->kind == StmtKind::If) {
            cur = cur->orelse[0].get();
            flush(cur->line);
            branch(cond(*cur, true), cur->body, cur->line, false);
        }
        if (!cur->orelse.empty()) {
            int elseLine = cur->orelse.front()->line - 1;
            flush(elseLine);
            branch(lang_ == L::Roblox ? "else" : "else", cur->orelse, elseLine, false);
        }
        if (lang_ == L::Roblox)
            emit("end");
    }

    void forLoop(const Stmt& s) {
        const Expr* target = s.targets.empty() ? nullptr : s.targets[0].get();
        if (!target || target->kind != ExprKind::Name) {
            fallback(s, "Loops over several variables at once (for a, b in ...) are written differently in " + std::string(engine()) + ".");
            return;
        }
        std::string raw = target->text;
        scope_.insert(raw);
        std::string i = var(raw);
        const Expr* it = s.expr.get();
        bool isRange = it && it->kind == ExprKind::Call && it->a && it->a->kind == ExprKind::Name && it->a->text == "range" &&
                       !it->items.empty() && it->items.size() <= 3;
        std::string header;
        if (isRange) {
            localTypes_[raw] = Ty::Int;
            const Expr* e0 = it->items[0].get();
            std::string from = it->items.size() >= 2 ? ex(e0) : "0";
            const Expr* toE = it->items.size() >= 2 ? it->items[1].get() : e0;
            std::string to = ex(toE, 6);
            std::string step = it->items.size() == 3 ? ex(it->items[2].get()) : "";
            bool down = it->items.size() == 3 && it->items[2]->kind == ExprKind::Unary && it->items[2]->op == Tok::Minus;
            switch (lang_) {
            case L::Unity:
            case L::Unreal: {
                std::string type = lang_ == L::Unity ? "int" : "int32";
                std::string inc = step.empty() ? (lang_ == L::Unity ? i + "++" : "++" + i) : i + " += " + step;
                header = "for (" + type + " " + i + " = " + from + "; " + i + (down ? " > " : " < ") + to + "; " + inc + ")";
                break;
            }
            case L::Godot: header = "for " + i + " in " + ex(it); break;
            case L::Roblox: {
                std::string last = toE->kind == ExprKind::Number ? fmtNum(toE->number + (down ? 1 : -1)) : to + (down ? " + 1" : " - 1");
                header = "for " + i + " = " + from + ", " + last + (step.empty() ? "" : ", " + step) + " do";
                break;
            }
            }
        } else {
            localTypes_[raw] = infer(it) == Ty::List && it->kind == ExprKind::Call ? Ty::Obj : Ty::Unknown;
            switch (lang_) {
            case L::Unity: header = "foreach (var " + i + " in " + ex(it) + ")"; break;
            case L::Unreal: header = "for (auto& " + i + " : " + ex(it) + ")"; break;
            case L::Godot: header = "for " + i + " in " + ex(it); break;
            case L::Roblox: header = "for _, " + i + " in " + ex(it) + " do"; break;
            }
        }
        open(header, s.line);
        body(s.body);
        close();
    }

    void stmt(const Stmt& s) {
        flush(s.line);
        switch (s.kind) {
        case StmtKind::Expression: {
            const Expr* e = s.expr.get();
            // wait() is a statement of its own in the other engines.
            if (e && e->kind == ExprKind::Call && e->a && e->a->kind == ExprKind::Name && e->a->text == "wait") {
                Args a = argsOf(e);
                std::string sec = arg(a, 0, "0");
                switch (lang_) {
                case L::Unity:
                    if (inCoroutine_) {
                        line("yield return new WaitForSeconds(" + sec + ");", s.line);
                    } else {
                        note("Unity can only wait inside a coroutine (a function that returns IEnumerator).");
                        line("// wait(" + sec + "): move this into a coroutine", s.line);
                    }
                    return;
                case L::Godot: line("await get_tree().create_timer(" + sec + ").timeout", s.line); return;
                case L::Roblox: line("task.wait(" + sec + ")", s.line); return;
                case L::Unreal:
                    note("Unreal waits with timers (GetWorldTimerManager().SetTimer) or Delay nodes in Blueprints.");
                    line("// wait(" + sec + "): continue from a timer instead", s.line);
                    return;
                }
            }
            std::string text = ex(e);
            bool isComment = text.rfind("--", 0) == 0 || text.rfind("//", 0) == 0;
            line(text + (isComment ? "" : semi()), s.line);
            return;
        }
        case StmtKind::Assign:
            if (s.targets.size() == 1 && s.targets[0]->kind == ExprKind::List) {
                fallback(s, "Setting several variables at once (a, b = 1, 2) is written one at a time in " + std::string(engine()) + ".");
                return;
            }
            for (auto& t : s.targets)
                assign(t.get(), "=", s.expr.get(), s.line);
            return;
        case StmtKind::AugAssign: assign(s.targets[0].get(), opText(s.op), s.expr.get(), s.line); return;
        case StmtKind::If: ifChain(s); return;
        case StmtKind::While: {
            std::string c = ex(s.expr.get());
            open(cLike() ? "while (" + c + ")" : lang_ == L::Roblox ? "while " + c + " do" : "while " + c, s.line);
            body(s.body);
            close();
            return;
        }
        case StmtKind::For: forLoop(s); return;
        case StmtKind::Return:
            if (lang_ == L::Unity && inCoroutine_) {
                line(s.expr ? "yield break; // (a coroutine can't return a value)" : "yield break;", s.line);
                return;
            }
            line(s.expr ? "return " + ex(s.expr.get()) + semi() : "return" + semi(), s.line);
            return;
        case StmtKind::Break: line("break" + semi(), s.line); return;
        case StmtKind::Continue: line("continue" + semi(), s.line); return;
        case StmtKind::Pass:
            if (lang_ == L::Godot)
                line("pass", s.line);
            return;
        case StmtKind::Global: return;
        case StmtKind::Def: fallback(s, "Functions inside functions aren't translated; move them to the top level."); return;
        }
    }

    // ------------------------------------------------------------ functions

    std::string returnType(const Stmt* def) {
        std::function<const Expr*(const std::vector<StmtPtr>&)> find = [&](const std::vector<StmtPtr>& b) -> const Expr* {
            for (auto& s : b) {
                if (s->kind == StmtKind::Return && s->expr)
                    return s->expr.get();
                if (auto* r = find(s->body))
                    return r;
                if (auto* r = find(s->orelse))
                    return r;
            }
            return nullptr;
        };
        const Expr* r = find(def->body);
        if (!r)
            return "void";
        Ty t = infer(r);
        return typeName(t == Ty::Unknown ? Ty::Num : t, true);
    }

    std::string params(const Stmt* def, bool typed) {
        std::string s;
        size_t firstDefault = def->params.size() - def->defaults.size();
        for (size_t i = 0; i < def->params.size(); ++i) {
            std::string raw = symbolName(def->params[i]);
            s += (i ? ", " : "") + (typed ? paramType(raw) + " " : "") + var(raw);
            if (i >= firstDefault)
                s += " = " + ex(def->defaults[i - firstDefault].get());
        }
        return s;
    }

    void enterFunction(const Stmt* def) {
        scope_.clear();
        localTypes_.clear();
        if (def)
            for (Symbol p : def->params) {
                std::string raw = symbolName(p);
                scope_.insert(raw);
                static const std::set<std::string> objects = {"other", "target", "obj", "enemy", "player", "who", "thing", "hit"};
                if (objects.count(raw))
                    localTypes_[raw] = Ty::Obj;
            }
    }

    // Statements that run at load time, placed at the start of the first event.
    void looseStatements() {
        for (auto* s : loose_)
            stmt(*s);
        for (auto& f : fields_)
            if (!f.literal && !(lang_ == L::Godot || lang_ == L::Roblox)) {
                flush(f.stmt->line);
                line(var(f.raw) + " = " + ex(f.stmt->expr.get()) + semi(), f.stmt->line);
            }
    }

    std::string className() const {
        return lang_ == L::Unreal ? "A" + opt_.className : opt_.className;
    }

    // ------------------------------------------------------------ Unity

    void unity() {
        std::string fields, methods, header;
        out_ = &header;
        int first = prog_.statements.empty() ? 1 << 30 : prog_.statements.front()->line;
        flush(first);
        std::string top = header;

        out_ = &fields;
        depth_ = 1;
        for (auto& f : fields_) {
            flush(f.stmt->line);
            Ty t = fieldTypes_[f.raw];
            std::string type = typeName(t == Ty::None ? Ty::Obj : t, true);
            std::string value = f.literal ? " = " + ex(f.stmt->expr.get()) : "";
            if (f.exported) {
                auto tip = trailComments_.find(f.stmt->line);
                if (tip != trailComments_.end())
                    emit("[Tooltip(" + quote(tip->second) + ")]");
                emit("public " + type + " " + var(f.raw) + value + ";");
            } else {
                line(type + " " + var(f.raw) + value + ";", f.stmt->line);
            }
        }

        out_ = &methods;
        const Stmt* start = handler("on_start");
        const Stmt* keys = handler("on_key_pressed");
        bool needStart = !loose_.empty() || std::any_of(fields_.begin(), fields_.end(), [](const Field& f) { return !f.literal; });
        if (needStart && !start) {
            blank();
            emit("void Start()");
            emit("{");
            ++depth_;
            enterFunction(nullptr);
            looseStatements();
            --depth_;
            emit("}");
        }
        for (auto* d : defs_) {
            std::string name = symbolName(d->name);
            flush(d->line);
            blank();
            enterFunction(d);
            inCoroutine_ = coroutines_.count(name) > 0;
            std::string sig;
            std::string prologue;
            std::string p0 = param(d, 0, "other");
            if (name == "on_start")
                sig = inCoroutine_ ? "IEnumerator Start()" : "void Start()";
            else if (name == "on_update") {
                sig = "void Update()";
                aliases_[param(d, 0, "dt")] = "Time.deltaTime";
            } else if (name == "on_fixed_update") {
                sig = "void FixedUpdate()";
                aliases_[param(d, 0, "dt")] = "Time.fixedDeltaTime";
            } else if (name == "on_collide" || name == "on_collide_end") {
                std::string ev = name == "on_collide" ? "OnCollisionEnter" : "OnCollisionExit";
                std::string c = p0 == "collision" ? "hit" : "collision";
                sig = "void " + ev + (opt_.is3D ? "(Collision " : "2D(Collision2D ") + c + ")";
                prologue = "GameObject " + var(p0) + " = " + c + ".gameObject;";
            } else if (name == "on_trigger" || name == "on_trigger_exit") {
                std::string ev = name == "on_trigger" ? "OnTriggerEnter" : "OnTriggerExit";
                std::string c = p0 == "col" ? "hit" : "col";
                sig = "void " + ev + (opt_.is3D ? "(Collider " : "2D(Collider2D ") + c + ")";
                prologue = "GameObject " + var(p0) + " = " + c + ".gameObject;";
            } else if (name == "on_click") {
                sig = "void OnMouseDown()";
            } else if (name == "on_message") {
                sig = "void OnMessage(string " + var(param(d, 0, "message")) + ")";
                if (d->params.size() > 1)
                    prologue = "object " + var(param(d, 1, "data")) + " = null;";
                note("Unity's SendMessage passes one value, so on_message gets the message name only.");
            } else if (name == "on_key_pressed") {
                sig = "void HandleKeys()";
                keyParam_ = param(d, 0, "key");
                aliases_[keyParam_] = "Input.inputString";
                note("Unity checks keys every frame with Input.GetKeyDown, so on_key_pressed became HandleKeys(), called from Update().");
            } else if (name == "on_destroy") {
                sig = "void OnDestroy()";
            } else {
                std::string ret = inCoroutine_ ? "IEnumerator" : returnType(d);
                sig = ret + " " + func(name) + "(" + params(d, true) + ")";
            }
            line(sig, d->line);
            emit("{");
            ++depth_;
            if (!prologue.empty())
                emit(prologue);
            if (name == "on_start")
                looseStatements();
            if (name == "on_update" && keys)
                emit("HandleKeys();");
            body(d->body);
            --depth_;
            emit("}");
            aliases_.clear();
            keyParam_.clear();
            inCoroutine_ = false;
        }
        if (keys && !handler("on_update")) {
            blank();
            emit("void Update()");
            emit("{");
            emit("    HandleKeys();");
            emit("}");
        }
        flush(1 << 30);

        // Helpers found while translating.
        out_ = &methods;
        depth_ = 1;
        if (helpers_.count("grounded")) {
            blank();
            emit("// True when touching something below (contacts pointing up).");
            emit("bool IsGrounded()");
            emit("{");
            emit("    var below = new ContactFilter2D();");
            emit("    below.SetNormalAngle(45, 135);");
            emit("    return rb.IsTouching(below);");
            emit("}");
        }
        if (helpers_.count("broadcast")) {
            blank();
            emit("// Calls OnMessage on every object that has it (like Aven's broadcast).");
            emit("void Broadcast(string message, object data = null)");
            emit("{");
            emit("    foreach (var behaviour in FindObjectsByType<MonoBehaviour>(FindObjectsSortMode.None))");
            emit("        behaviour.SendMessage(\"OnMessage\", message, SendMessageOptions.DontRequireReceiver);");
            emit("}");
        }

        std::string out;
        out += "using System.Collections;\nusing System.Collections.Generic;\nusing UnityEngine;\n";
        for (auto& u : services_)
            out += "using " + u + ";\n";
        out += "\n" + top;
        out += "public class " + opt_.className + " : MonoBehaviour\n{\n" + fields;
        if (needsBody_ || needsSprite_) {
            if (!fields.empty())
                out += "\n";
            if (needsBody_)
                out += std::string("    ") + (opt_.is3D ? "Rigidbody" : "Rigidbody2D") + " rb;\n";
            if (needsSprite_)
                out += "    SpriteRenderer sr;\n";
            out += "\n    void Awake()\n    {\n";
            if (needsBody_)
                out += std::string("        rb = GetComponent<") + (opt_.is3D ? "Rigidbody" : "Rigidbody2D") + ">();\n";
            if (needsSprite_)
                out += "        sr = GetComponent<SpriteRenderer>();\n";
            out += "    }\n";
        }
        if ((!fields.empty() || needsBody_ || needsSprite_) && !methods.empty())
            out += "\n";
        out += methods;
        out += "}\n";
        if (!gameVars_.empty()) {
            out += "\n// Shared by all scripts, like Aven's game variables.\npublic static class GameState\n{\n";
            for (auto& g : gameVars_)
                out += "    public static float " + camel(g) + ";\n";
            out += "}\n";
        }
        result_ = out;
    }

    // ------------------------------------------------------------ Godot

    void godot() {
        std::string top, fields, methods;
        out_ = &top;
        int first = prog_.statements.empty() ? 1 << 30 : prog_.statements.front()->line;
        flush(first);

        out_ = &fields;
        for (auto& f : fields_) {
            flush(f.stmt->line);
            std::string value = ex(f.stmt->expr.get());
            if (f.exported) {
                Ty t = fieldTypes_[f.raw];
                std::string type = t == Ty::Num ? ": float" : t == Ty::Str ? ": String" : t == Ty::Bool ? ": bool" : "";
                auto tip = trailComments_.find(f.stmt->line);
                emit("@export var " + var(f.raw) + type + " = " + value + (tip != trailComments_.end() ? "  ## " + tip->second : ""));
            } else {
                // Values that need the scene (like find()) are set once the node is ready.
                line(std::string(f.literal || f.stmt->expr->kind == ExprKind::List || f.stmt->expr->kind == ExprKind::Dict ||
                                         f.stmt->expr->kind == ExprKind::None
                                     ? "var "
                                     : "@onready var ") +
                         var(f.raw) + " = " + value,
                     f.stmt->line);
            }
        }

        const Stmt* collide = handler("on_collide");
        const Stmt* trigger = handler("on_trigger");
        const Stmt* collideEnd = handler("on_collide_end");
        const Stmt* triggerExit = handler("on_trigger_exit");
        bool physics = false;
        // Decide the node type from what the script uses.
        std::string code = source_;
        physics = code.find("velocity") != std::string::npos || code.find("apply_force") != std::string::npos ||
                  code.find("apply_impulse") != std::string::npos || code.find("on_ground") != std::string::npos || collide;
        std::string base = physics ? (opt_.is3D ? "RigidBody3D" : "RigidBody2D")
                           : (trigger || triggerExit) ? (opt_.is3D ? "Area3D" : "Area2D")
                                                      : (opt_.is3D ? "Node3D" : "Node2D");
        bool touches = collide || trigger || collideEnd || triggerExit;
        const Stmt* message = handler("on_message");

        out_ = &methods;
        const Stmt* start = handler("on_start");
        bool needReady = !start && (touches || message || !loose_.empty());
        auto readyExtras = [&] {
            if (physics && touches) {
                emit("contact_monitor = true  # report touches");
                emit("max_contacts_reported = 4");
            }
            if (collide || trigger)
                emit("body_entered.connect(_on_body_entered)");
            if (collideEnd || triggerExit)
                emit("body_exited.connect(_on_body_exited)");
            if (message)
                emit("add_to_group(\"listeners\")  # receives broadcasts");
        };
        if (needReady) {
            blank();
            emit("func _ready():");
            ++depth_;
            enterFunction(nullptr);
            readyExtras();
            looseStatements();
            --depth_;
        }
        for (auto* d : defs_) {
            std::string name = symbolName(d->name);
            flush(d->line);
            blank();
            enterFunction(d);
            std::string sig;
            bool clickWrap = false, keyWrap = false;
            if (name == "on_start")
                sig = "func _ready():";
            else if (name == "on_update" || name == "on_fixed_update") {
                std::string p = param(d, 0, "delta");
                if (p == "dt")
                    aliases_["dt"] = "delta";
                sig = std::string(name == "on_update" ? "func _process(" : "func _physics_process(") + (p == "dt" ? "delta" : var(p)) + "):";
            } else if (name == "on_collide" || name == "on_trigger")
                sig = "func _on_body_entered(" + var(param(d, 0, "body")) + "):";
            else if (name == "on_collide_end" || name == "on_trigger_exit")
                sig = "func _on_body_exited(" + var(param(d, 0, "body")) + "):";
            else if (name == "on_click") {
                sig = std::string("func _input_event(") + (opt_.is3D ? "camera, event, position, normal, shape_idx" : "viewport, event, shape_idx") + "):";
                clickWrap = true;
                note("For clicks, the Godot object needs a collision shape and Input Pickable turned on.");
            } else if (name == "on_key_pressed") {
                sig = "func _unhandled_key_input(event):";
                keyWrap = true;
                keyParam_ = param(d, 0, "key");
            } else if (name == "on_destroy")
                sig = "func _exit_tree():";
            else if (name == "on_message")
                sig = "func on_message(" + params(d, false) + "):";
            else
                sig = "func " + func(name) + "(" + params(d, false) + "):";
            if ((name == "on_collide" && trigger) || (name == "on_collide_end" && triggerExit)) {
                // Both handlers map to the same signal: keep the trigger one, show this one as a plain function.
                sig = "func " + name + "(" + params(d, false) + "):";
            }
            line(sig, d->line);
            ++depth_;
            if (name == "on_start")
                readyExtras();
            if (name == "on_start")
                looseStatements();
            if (clickWrap) {
                emit("if not (event is InputEventMouseButton and event.pressed):");
                emit("    return");
            }
            if (keyWrap) {
                emit("if not event.pressed:");
                emit("    return");
                emit("var " + var(param(d, 0, "key")) + " = OS.get_keycode_string(event.keycode).to_lower()");
            }
            body(d->body);
            --depth_;
            aliases_.clear();
            keyParam_.clear();
        }
        flush(1 << 30);

        if (helpers_.count("grounded")) {
            blank();
            emit("# True when touching something below (y grows downward in Godot 2D).");
            emit("func is_on_ground() -> bool:");
            emit("    for body in get_colliding_bodies():");
            emit("        if body.global_position.y > global_position.y:");
            emit("            return true");
            emit("    return false");
            if (!touches)
                note("is_on_ground() needs Contact Monitor turned on for the RigidBody2D.");
        }
        if (helpers_.count("spawn")) {
            blank();
            emit("func spawn(path: String, at: Vector2) -> Node:");
            emit("    var thing = load(path).instantiate()");
            emit("    thing.position = at");
            emit("    get_parent().add_child(thing)");
            emit("    return thing");
        }
        if (helpers_.count("play_sound")) {
            blank();
            emit("func play_sound(path: String) -> void:");
            emit("    var player := AudioStreamPlayer.new()");
            emit("    player.stream = load(path)");
            emit("    add_child(player)");
            emit("    player.play()");
            emit("    player.finished.connect(player.queue_free)");
        }
        if (helpers_.count("every")) {
            blank();
            emit("func every(seconds: float, callback: Callable) -> void:");
            emit("    var timer := Timer.new()");
            emit("    timer.wait_time = seconds");
            emit("    timer.timeout.connect(callback)");
            emit("    add_child(timer)");
            emit("    timer.start()");
        }

        std::string gv;
        if (!gameVars_.empty()) {
            gv = "# game variables live in an Autoload script named GameState (Project Settings > Autoload):\n";
            for (auto& g : gameVars_)
                gv += "#     var " + g + " = 0\n";
        }
        result_ = joinSections({top, "extends " + base, fields, methods, gv});
    }

    // ------------------------------------------------------------ Roblox

    void roblox() {
        std::string top, fields, functions, startCode, events;
        out_ = &top;
        int first = prog_.statements.empty() ? 1 << 30 : prog_.statements.front()->line;
        flush(first);

        out_ = &fields;
        for (auto& f : fields_) {
            flush(f.stmt->line);
            std::string value = ex(f.stmt->expr.get());
            if (f.exported)
                line("local " + var(f.raw) + " = part:GetAttribute(\"" + f.raw + "\") or " + value, f.stmt->line);
            else
                line("local " + var(f.raw) + " = " + value, f.stmt->line);
        }
        if (std::any_of(fields_.begin(), fields_.end(), [](const Field& f) { return f.exported; }))
            note("Settings you change in Roblox Studio are Attributes (Properties > Attributes).");

        // Events become connections, after the functions they may call.
        for (auto* d : defs_) {
            std::string name = symbolName(d->name);
            enterFunction(d);
            if (!kEvents.count(name)) {
                out_ = &functions;
                flush(d->line);
                blank();
                line("local function " + func(name) + "(" + params(d, false) + ")", d->line);
                ++depth_;
                body(d->body);
                --depth_;
                emit("end");
                continue;
            }
            if (name == "on_start") {
                out_ = &startCode;
                flush(d->line);
                skipComments(d->line);
                emit("-- When the game starts:");
                looseStatements();
                body(d->body);
                continue;
            }
            out_ = &events;
            flush(d->line);
            blank();
            std::string p0 = var(param(d, 0, "other"));
            std::string header;
            if (name == "on_update" || name == "on_fixed_update") {
                services_.insert("RunService");
                header = std::string("RunService.") + (name == "on_update" ? "Heartbeat" : "Stepped") + ":Connect(function(" +
                         (name == "on_update" ? var(param(d, 0, "dt")) : "_, " + var(param(d, 0, "dt"))) + ")";
            } else if (name == "on_collide" || name == "on_trigger") {
                header = "part.Touched:Connect(function(" + p0 + ")";
                if (name == "on_trigger")
                    note("A Roblox trigger is a part with CanCollide turned off; Touched still fires.");
            } else if (name == "on_collide_end" || name == "on_trigger_exit") {
                header = "part.TouchEnded:Connect(function(" + p0 + ")";
            } else if (name == "on_click") {
                helpers_.insert("click");
                header = "clickDetector.MouseClick:Connect(function(player)";
            } else if (name == "on_key_pressed") {
                services_.insert("UserInputService");
                header = "UserInputService.InputBegan:Connect(function(input, typing)";
                note("Keyboard input in Roblox runs in a LocalScript (on the player's computer).");
            } else if (name == "on_message") {
                helpers_.insert("messages");
                header = "messages.Event:Connect(function(" + params(d, false) + ")";
            } else if (name == "on_destroy") {
                header = "part.Destroying:Connect(function()";
            }
            line(header, d->line);
            ++depth_;
            if (name == "on_key_pressed") {
                emit("if typing then return end");
                emit("local " + var(param(d, 0, "key")) + " = input.KeyCode.Name:lower()");
                keyParam_ = param(d, 0, "key");
            }
            body(d->body);
            --depth_;
            emit("end)");
            keyParam_.clear();
        }
        if (!handler("on_start") && !loose_.empty()) {
            out_ = &startCode;
            enterFunction(nullptr);
            blank();
            looseStatements();
        }
        out_ = &events;
        flush(1 << 30);

        std::string helpers;
        out_ = &helpers;
        auto add = [&](std::initializer_list<const char*> lines) {
            blank();
            for (const char* l : lines)
                emit(l);
        };
        if (helpers_.count("mouse"))
            add({"local mouse = game.Players.LocalPlayer:GetMouse()"});
        if (helpers_.count("messages"))
            add({"-- A BindableEvent named Messages in ReplicatedStorage carries broadcasts between scripts.",
                 "local messages = game.ReplicatedStorage:WaitForChild(\"Messages\")"});
        if (helpers_.count("click"))
            add({"local clickDetector = part:FindFirstChildOfClass(\"ClickDetector\") or Instance.new(\"ClickDetector\", part)"});
        if (helpers_.count("grounded"))
            add({"local function isOnGround()", "    local params = RaycastParams.new()", "    params.FilterDescendantsInstances = { part }",
                 "    return workspace:Raycast(part.Position, Vector3.new(0, -(part.Size.Y / 2 + 0.2), 0), params) ~= nil", "end"});
        if (helpers_.count("playSound"))
            add({"local function playSound(soundId)", "    local sound = Instance.new(\"Sound\")", "    sound.SoundId = soundId",
                 "    sound.Parent = part", "    sound:Play()", "    sound.Ended:Connect(function() sound:Destroy() end)", "end"});
        if (helpers_.count("spawn"))
            add({"local function spawnCopy(name, x, y)", "    local copy = game.ReplicatedStorage[name]:Clone()",
                 "    copy:PivotTo(CFrame.new(x, y, 0))", "    copy.Parent = workspace", "    return copy", "end"});
        if (helpers_.count("moveToward"))
            add({"local function moveToward(thing, target, step)", "    local offset = target - thing.Position",
                 "    if offset.Magnitude <= step then", "        thing.Position = target", "    else",
                 "        thing.Position += offset.Unit * step", "    end", "end"});
        if (helpers_.count("moveTowardNumber"))
            add({"local function moveTowardNumber(value, target, step)", "    if math.abs(target - value) <= step then return target end",
                 "    return value + math.sign(target - value) * step", "end"});
        if (helpers_.count("touchingTag"))
            add({"local function isTouchingTag(thing, tag)", "    for _, other in thing:GetTouchingParts() do",
                 "        if other:HasTag(tag) then return true end", "    end", "    return false", "end"});
        if (helpers_.count("axis"))
            add({"local function axis(name)", "    local function key(code) return UserInputService:IsKeyDown(code) and 1 or 0 end",
                 "    if name == \"vertical\" then", "        return key(Enum.KeyCode.W) - key(Enum.KeyCode.S)", "    end",
                 "    return key(Enum.KeyCode.D) - key(Enum.KeyCode.A)", "end"});

        std::string header = "local part = script.Parent\n";
        for (auto& s : services_)
            header += "local " + s + " = game:GetService(\"" + s + "\")\n";
        if (!gameVars_.empty()) {
            header += "local GameState = require(game.ReplicatedStorage.GameState) -- a ModuleScript that returns {}\n";
            note("Game variables shared by scripts live in a ModuleScript (GameState) that every script requires.");
        }
        result_ = joinSections({top, header, helpers, fields, functions, startCode, events});
    }

    // ------------------------------------------------------------ Unreal

    void unreal() {
        std::string top, props, privates, methods;
        out_ = &top;
        int first = prog_.statements.empty() ? 1 << 30 : prog_.statements.front()->line;
        flush(first);
        std::string cls = className();

        depth_ = 1;
        for (auto& f : fields_) {
            Ty t = fieldTypes_[f.raw];
            std::string type = typeName(t == Ty::None ? Ty::Obj : t, true);
            std::string value = f.literal ? " = " + ex(f.stmt->expr.get()) : "";
            if (f.exported) {
                out_ = &props;
                blank();
                flush(f.stmt->line);
                auto tip = trailComments_.find(f.stmt->line);
                if (tip != trailComments_.end())
                    emit("UPROPERTY(EditAnywhere, meta = (ToolTip = " + quote(tip->second).substr(5, quote(tip->second).size() - 6) + "))");
                else
                    emit("UPROPERTY(EditAnywhere)");
                emit(type + " " + var(f.raw) + value + ";");
            } else {
                out_ = &privates;
                flush(f.stmt->line);
                line(type + " " + var(f.raw) + value + ";", f.stmt->line);
            }
        }

        out_ = &methods;
        const Stmt* start = handler("on_start");
        bool needBegin = !start && (!loose_.empty() || std::any_of(fields_.begin(), fields_.end(), [](const Field& f) { return !f.literal; }));
        std::string beginPlaceholder = "@@BODY@@";
        auto beginPlay = [&](const Stmt* d) {
            emit("virtual void BeginPlay() override");
            emit("{");
            ++depth_;
            emit("Super::BeginPlay();");
            emit(beginPlaceholder);
            looseStatements();
            if (d)
                body(d->body);
            --depth_;
            emit("}");
        };
        if (needBegin) {
            enterFunction(nullptr);
            beginPlay(nullptr);
        }
        bool ticks = false;
        for (auto* d : defs_) {
            std::string name = symbolName(d->name);
            flush(d->line);
            blank();
            enterFunction(d);
            std::string p0 = param(d, 0, "other");
            if (name == "on_start") {
                beginPlay(d);
                continue;
            }
            std::string sig, superCall;
            if (name == "on_update" || name == "on_fixed_update") {
                ticks = true;
                aliases_[param(d, 0, "dt")] = "DeltaTime";
                sig = "virtual void Tick(float DeltaTime) override";
                superCall = "Super::Tick(DeltaTime);";
                if (name == "on_fixed_update")
                    note("Unreal has no separate fixed update for scripts; physics code usually goes in Tick.");
            } else if (name == "on_trigger") {
                sig = "virtual void NotifyActorBeginOverlap(AActor* " + var(p0) + ") override";
                superCall = "Super::NotifyActorBeginOverlap(" + var(p0) + ");";
            } else if (name == "on_trigger_exit") {
                sig = "virtual void NotifyActorEndOverlap(AActor* " + var(p0) + ") override";
                superCall = "Super::NotifyActorEndOverlap(" + var(p0) + ");";
            } else if (name == "on_collide") {
                sig = "virtual void NotifyHit(UPrimitiveComponent* MyComp, AActor* " + var(p0) +
                      ", UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, "
                      "const FHitResult& Hit) override";
                superCall = "Super::NotifyHit(MyComp, " + var(p0) + ", OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);";
            } else if (name == "on_click") {
                sig = "virtual void NotifyActorOnClicked(FKey ButtonPressed) override";
                superCall = "Super::NotifyActorOnClicked(ButtonPressed);";
            } else if (name == "on_destroy") {
                sig = "virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override";
                superCall = "Super::EndPlay(EndPlayReason);";
            } else if (name == "on_message") {
                sig = "void OnMessage(const FString& " + var(param(d, 0, "Message")) + ")";
            } else if (name == "on_collide_end") {
                sig = "void OnCollideEnd(AActor* " + var(p0) + ")";
                note("Unreal reports the end of a hit with overlap events (NotifyActorEndOverlap).");
            } else if (name == "on_key_pressed") {
                sig = "void HandleKeys()";
                keyParam_ = param(d, 0, "key");
                ticks = true;
                note("Unreal checks keys each frame here (called from Tick); bigger projects use Enhanced Input actions.");
            } else {
                sig = returnType(d) + " " + func(name) + "(" + params(d, true) + ")";
            }
            line(sig, d->line);
            emit("{");
            ++depth_;
            if (!superCall.empty())
                emit(superCall);
            if ((name == "on_update" || name == "on_fixed_update") && handler("on_key_pressed"))
                emit("HandleKeys();");
            body(d->body);
            --depth_;
            emit("}");
            aliases_.clear();
            keyParam_.clear();
        }
        if (handler("on_key_pressed") && !handler("on_update") && !handler("on_fixed_update")) {
            blank();
            emit("virtual void Tick(float DeltaTime) override");
            emit("{");
            emit("    Super::Tick(DeltaTime);");
            emit("    HandleKeys();");
            emit("}");
        }
        flush(1 << 30);

        depth_ = 1;
        if (helpers_.count("grounded")) {
            blank();
            emit("// True when something is just below the actor.");
            emit("bool IsOnGround() const");
            emit("{");
            emit("    FHitResult Hit;");
            emit("    const FVector Start = GetActorLocation();");
            emit("    return GetWorld()->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 60), ECC_Visibility,");
            emit("                                                FCollisionQueryParams(NAME_None, false, this));");
            emit("}");
        }
        if (helpers_.count("findActor")) {
            blank();
            emit("AActor* FindActorByName(const FString& Name) const");
            emit("{");
            emit("    TArray<AActor*> Actors;");
            emit("    UGameplayStatics::GetAllActorsOfClass(GetWorld(), AActor::StaticClass(), Actors);");
            emit("    for (AActor* Actor : Actors)");
            emit("        if (Actor->GetActorNameOrLabel() == Name)");
            emit("            return Actor;");
            emit("    return nullptr;");
            emit("}");
        }
        if (helpers_.count("findTagged")) {
            blank();
            emit("TArray<AActor*> FindActorsWithTag(const FName Tag) const");
            emit("{");
            emit("    TArray<AActor*> Actors;");
            emit("    UGameplayStatics::GetAllActorsWithTag(GetWorld(), Tag, Actors);");
            emit("    return Actors;");
            emit("}");
        }
        if (helpers_.count("mouseWorld")) {
            blank();
            emit("FVector MouseWorld() const");
            emit("{");
            emit("    FVector Location, Direction;");
            emit("    UGameplayStatics::GetPlayerController(this, 0)->DeprojectMousePositionToWorld(Location, Direction);");
            emit("    return Location;");
            emit("}");
        }
        if (helpers_.count("axis")) {
            blank();
            emit("float Axis(const FString& Name) const");
            emit("{");
            emit("    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);");
            emit("    if (Name == TEXT(\"vertical\"))");
            emit("        return (PC->IsInputKeyDown(EKeys::W) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);");
            emit("    return (PC->IsInputKeyDown(EKeys::D) ? 1.f : 0.f) - (PC->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);");
            emit("}");
        }

        // Fill in BeginPlay setup for the physics body.
        std::string setup = needsBody_ ? std::string(8, ' ') + "Body = Cast<UPrimitiveComponent>(GetRootComponent());" : "";
        size_t ph = methods.find(beginPlaceholder);
        if (ph != std::string::npos) {
            size_t lineStart = methods.rfind('\n', ph);
            size_t lineEnd = methods.find('\n', ph);
            methods.replace(lineStart + 1, lineEnd - lineStart, setup.empty() ? "" : setup + "\n");
        } else if (needsBody_) {
            methods = "    virtual void BeginPlay() override\n    {\n        Super::BeginPlay();\n" + setup + "\n    }\n\n" + methods;
        }

        std::string out = top;
        out += "#pragma once\n\n#include \"CoreMinimal.h\"\n#include \"GameFramework/Actor.h\"\n#include \"Kismet/GameplayStatics.h\"\n";
        out += "#include \"" + opt_.className + ".generated.h\"\n\n";
        out += "UCLASS()\nclass " + cls + " : public AActor\n{\n    GENERATED_BODY()\n\npublic:\n";
        out += "    " + cls + "()\n    {\n        PrimaryActorTick.bCanEverTick = " + (ticks ? "true" : "false") + ";\n    }\n";
        if (!props.empty())
            out += "\n" + props;
        if (!privates.empty() || needsBody_ || needsTimer_) {
            out += "\nprivate:\n" + privates;
            if (needsBody_)
                out += "    UPrimitiveComponent* Body = nullptr;\n";
            if (needsTimer_)
                out += "    FTimerHandle Timer;\n";
        }
        out += "\npublic:\n" + methods + "};\n";
        if (!gameVars_.empty()) {
            out += "\n// Game variables live in a GameInstance, which lasts for the whole game:\n";
            out += "// UCLASS() class UMyGameInstance : public UGameInstance { GENERATED_BODY() public:\n";
            for (auto& g : gameVars_)
                out += "//     UPROPERTY() float " + pascal(g) + " = 0;\n";
            out += "// };\n";
        }
        note("In Unreal, a C++ class is split into a header (.h) and a source file (.cpp); here it's shown as one header.");
        result_ = out;
    }
};

} // namespace

Translation translate(std::string_view source, TargetLanguage language, const TranslateOptions& options) {
    return Translator(source, language, options).run();
}

const char* languageName(TargetLanguage language) {
    switch (language) {
    case TargetLanguage::Unity: return "C#";
    case TargetLanguage::Godot: return "GDScript";
    case TargetLanguage::Roblox: return "Luau";
    case TargetLanguage::Unreal: return "C++";
    }
    return "";
}

const char* languageEngine(TargetLanguage language) {
    switch (language) {
    case TargetLanguage::Unity: return "Unity";
    case TargetLanguage::Godot: return "Godot";
    case TargetLanguage::Roblox: return "Roblox";
    case TargetLanguage::Unreal: return "Unreal";
    }
    return "";
}

const char* languageExtension(TargetLanguage language) {
    switch (language) {
    case TargetLanguage::Unity: return ".cs";
    case TargetLanguage::Godot: return ".gd";
    case TargetLanguage::Roblox: return ".lua";
    case TargetLanguage::Unreal: return ".h";
    }
    return "";
}

std::string classNameFor(const std::string& path) {
    std::string name = path.substr(path.find_last_of("/\\") == std::string::npos ? 0 : path.find_last_of("/\\") + 1);
    name = name.substr(0, name.find('.'));
    std::string out;
    for (auto& w : splitWords(name)) {
        std::string clean;
        for (char c : w)
            if (std::isalnum(static_cast<unsigned char>(c)))
                clean += c;
        out += upperFirst(clean);
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out = "My" + out + "Script";
    return out;
}

} // namespace aven::script
