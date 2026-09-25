// The Code ladder's rung that stays inside Aven: EasyScript translated into C against the native
// module API (sdk/include/aven.h). The output is a whole native module source file that builds as
// it is. EasyScript features C has no direct match for (lists, wait(), components...) become
// clearly marked comments plus a note, so the file always compiles.

#include "aven/script/translate.h"

#include "aven/script/ast.h"
#include "aven/script/errors.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

namespace aven::script {

namespace {

enum class CT { Num, Bool, Str, Ent, Vec, Unknown };

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
        return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string num(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::fabs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", v);
        return buf;
    }
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.9g", v);
    return buf;
}

std::string cQuote(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        default: out += c;
        }
    }
    return out + "\"";
}

// Text inside a printf format: % must be doubled.
std::string formatText(const std::string& s) {
    std::string q = cQuote(s);
    std::string out;
    for (char c : q.substr(1, q.size() - 2)) {
        out += c;
        if (c == '%')
            out += '%';
    }
    return out;
}

std::string commentSafe(std::string s) {
    for (size_t p; (p = s.find("*/")) != std::string::npos;)
        s.replace(p, 2, "* /");
    return s;
}

const std::set<std::string> kCReserved = {
    "auto", "break", "case", "char", "const", "continue", "default", "do", "double", "else", "enum", "extern", "float", "for",
    "goto", "if", "inline", "int", "long", "register", "restrict", "return", "short", "signed", "sizeof", "static", "struct",
    "switch", "typedef", "union", "unsigned", "void", "volatile", "while", "bool", "true", "false", "abs", "fabs", "floor",
    "ceil", "round", "sin", "cos", "tan", "sqrt", "pow", "exp", "log", "time", "rand", "random", "printf", "main", "index",
    "signal", "exit", "free", "malloc", "strcmp", "strlen", "snprintf", "self", "my", "data", "module", "setup",
    "fmin", "fmax", "fmod", "atan2", "sign", "clamp", "lerp", "min", "max", "y0", "y1", "j0", "j1", "hypot", "strncat",
    "distance_between", "direction_x_to", "direction_y_to", "move_toward_object", "turn_toward", "axis_value", "sign_of"};

struct EventInfo {
    const char* name;
    const char* field;
    std::vector<CT> params;
};

const EventInfo kEvents[] = {
    {"on_start", "on_start", {}},
    {"on_update", "on_update", {CT::Num}},
    {"on_fixed_update", "on_fixed_update", {CT::Num}},
    {"on_collide", "on_collide", {CT::Ent}},
    {"on_collide_end", "on_collide_end", {CT::Ent}},
    {"on_trigger", "on_trigger", {CT::Ent}},
    {"on_trigger_exit", "on_trigger_exit", {CT::Ent}},
    {"on_click", "on_click", {}},
    {"on_key_pressed", "on_key_pressed", {CT::Str}},
    {"on_message", "on_message", {CT::Str, CT::Num}},
    {"on_destroy", "on_destroy", {}},
};

const EventInfo* eventInfo(const std::string& name) {
    for (auto& e : kEvents)
        if (name == e.name)
            return &e;
    return nullptr;
}

// Vector properties and the number properties that make them up.
std::optional<std::vector<std::string>> vectorProperty(const std::string& p) {
    if (p == "position")
        return std::vector<std::string>{"x", "y", "z"};
    if (p == "world_position")
        return std::vector<std::string>{"world_x", "world_y", "world_z"};
    if (p == "velocity")
        return std::vector<std::string>{"velocity_x", "velocity_y", "velocity_z"};
    if (p == "scale")
        return std::vector<std::string>{"scale_x", "scale_y", "scale_z"};
    if (p == "rotation")
        return std::vector<std::string>{"rotation_x", "rotation_y", "rotation_z"};
    return std::nullopt;
}

bool stringProperty(const std::string& p) { return p == "name" || p == "tag" || p == "text" || p == "image" || p == "shape"; }
bool boolProperty(const std::string& p) {
    return p == "visible" || p == "active" || p == "on_ground" || p == "flip_x" || p == "flip_y" || p == "is_clone";
}

class CTranslator {
public:
    CTranslator(std::string_view source, TranslateOptions options) : source_(source), opt_(std::move(options)) {}

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
        generate();
        t.ok = true;
        t.code = out_;
        t.notes = notes_;
        return t;
    }

private:
    std::string source_;
    TranslateOptions opt_;
    Program prog_;
    std::vector<std::string> lines_;
    std::map<int, std::string> fullComments_, trailComments_;
    int commentCursor_ = 0;
    std::vector<std::string> notes_;
    std::set<std::string> helpers_;
    std::string out_;

    struct Field {
        std::string raw, c;
        CT type = CT::Num;
        const Stmt* stmt = nullptr;
        bool property = false; // shown in the Inspector
    };
    std::vector<Field> fields_;
    std::map<std::string, size_t> fieldIndex_;
    std::vector<const Stmt*> defs_, loose_;
    std::map<std::string, const Stmt*> userFns_;
    std::map<std::string, CT> fnReturn_;
    std::string cls_, prefix_;

    // The function being written.
    std::string* body_ = nullptr;
    int depth_ = 1;
    std::map<std::string, CT> locals_;
    std::map<std::string, std::string> rename_; // EasyScript name -> C name
    std::map<std::string, CT> paramTypes_;
    std::vector<std::string> pre_; // statements the current one needs first
    int temp_ = 0;
    bool usesMy_ = false;
    bool inUserFn_ = false;
    CT returnType_ = CT::Unknown;
    std::set<std::string> loopInts_;          // range() loop counters (C ints)
    std::map<const Stmt*, int> commentStart_;  // where each def's leading comments begin

    // ------------------------------------------------------------ notes and comments

    void note(const std::string& text) {
        if (std::find(notes_.begin(), notes_.end(), text) == notes_.end())
            notes_.push_back(text);
    }

    void scanComments() {
        std::istringstream in(source_);
        std::string l;
        int n = 0;
        while (std::getline(in, l)) {
            ++n;
            lines_.push_back(l);
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

    std::string sourceLine(int line) const {
        if (line < 1 || line > static_cast<int>(lines_.size()))
            return "";
        std::string s = lines_[static_cast<size_t>(line - 1)];
        size_t hash = s.find('#');
        if (hash != std::string::npos && trailComments_.count(line))
            s = s.substr(0, hash);
        return commentSafe(trim(s));
    }

    void emit(const std::string& s) { *body_ += (s.empty() ? "" : std::string(static_cast<size_t>(depth_) * 4, ' ') + s) + "\n"; }

    // Comment lines between the last statement and `line`.
    void flush(int line) {
        bool gap = false;
        for (int l = commentCursor_ + 1; l < line; ++l) {
            if (l - 1 < static_cast<int>(lines_.size()) && trim(lines_[static_cast<size_t>(l - 1)]).empty()) {
                gap = true;
                continue;
            }
            auto it = fullComments_.find(l);
            if (it != fullComments_.end()) {
                if (gap && !body_->empty() && body_->back() == '\n' && body_->size() > 1 && (*body_)[body_->size() - 2] != '{')
                    *body_ += "\n";
                gap = false;
                emit("// " + it->second);
            }
        }
        commentCursor_ = std::max(commentCursor_, line - 1);
    }

    void line(const std::string& s, int sourceLineNo) {
        for (auto& p : pre_)
            emit(p);
        pre_.clear();
        auto t = trailComments_.find(sourceLineNo);
        emit(t == trailComments_.end() ? s : s + "  // " + t->second);
        commentCursor_ = std::max(commentCursor_, sourceLineNo);
    }

    std::string todo(int line, const std::string& why) {
        note(why);
        return "/* not in C: " + sourceLine(line) + " */";
    }

    // ------------------------------------------------------------ names

    static std::string snake(const std::string& s) {
        std::string out;
        for (size_t i = 0; i < s.size(); ++i) {
            char c = s[i];
            if (std::isupper(static_cast<unsigned char>(c))) {
                if (i && !out.empty() && out.back() != '_')
                    out += '_';
                out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            } else {
                out += std::isalnum(static_cast<unsigned char>(c)) ? c : '_';
            }
        }
        return out.empty() ? "behavior" : out;
    }

    static std::string cName(const std::string& raw) {
        std::string s = raw;
        while (s.size() > 1 && s[0] == '_')
            s.erase(0, 1);
        if (kCReserved.count(s))
            s += "_";
        return s;
    }

    std::string fnName(const std::string& raw) const {
        std::string s = cName(raw);
        return kCReserved.count(raw) || s != raw ? prefix_ + "_" + s : s;
    }

    const Field* field(const std::string& raw) const {
        auto it = fieldIndex_.find(raw);
        return it == fieldIndex_.end() ? nullptr : &fields_[it->second];
    }

    bool isLocal(const std::string& raw) const { return locals_.count(raw) || paramTypes_.count(raw); }

    std::string ref(const std::string& raw) {
        auto r = rename_.find(raw);
        if (r != rename_.end())
            return r->second;
        if (isLocal(raw))
            return cName(raw);
        if (const Field* f = field(raw)) {
            usesMy_ = true;
            return "my->" + f->c;
        }
        return cName(raw);
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

    void collect() {
        cls_ = opt_.className.empty() ? "MyScript" : opt_.className;
        prefix_ = snake(cls_);
        std::set<std::string> cNames;
        int previousEnd = prog_.statements.empty() ? 0 : prog_.statements[0]->line - 1;
        for (auto& st : prog_.statements) {
            commentStart_[st.get()] = previousEnd;
            previousEnd = std::max(st->endLine, st->line);
        }
        for (auto& s : prog_.statements) {
            if (s->kind == StmtKind::Assign && s->targets.size() == 1 && s->targets[0]->kind == ExprKind::Name) {
                const std::string& raw = s->targets[0]->text;
                if (raw == "is_clone")
                    continue;
                if (fieldIndex_.count(raw)) {
                    loose_.push_back(s.get());
                    continue;
                }
                Field f;
                f.raw = raw;
                f.c = cName(raw);
                if (cNames.count(f.c)) // `_speed` next to `speed`
                    f.c = "private_" + f.c;
                cNames.insert(f.c);
                f.stmt = s.get();
                f.type = ty(s->expr.get());
                f.property = !raw.empty() && raw[0] != '_' && literal(s->expr.get()) && (f.type == CT::Num || f.type == CT::Bool);
                fieldIndex_[raw] = fields_.size();
                fields_.push_back(f);
            } else if (s->kind == StmtKind::Def) {
                defs_.push_back(s.get());
                std::string name = symbolName(s->name);
                if (!eventInfo(name))
                    userFns_[name] = s.get();
            } else if (s->kind != StmtKind::Global && s->kind != StmtKind::Pass) {
                loose_.push_back(s.get());
            }
        }
        for (auto& [name, def] : userFns_)
            fnReturn_[name] = returnTypeOf(def->body);
    }

    CT returnTypeOf(const std::vector<StmtPtr>& body) {
        for (auto& s : body) {
            if (s->kind == StmtKind::Return && s->expr) {
                CT t = ty(s->expr.get());
                return t == CT::Unknown ? CT::Num : t;
            }
            CT inner = returnTypeOf(s->body);
            if (inner != CT::Unknown)
                return inner;
            inner = returnTypeOf(s->orelse);
            if (inner != CT::Unknown)
                return inner;
        }
        return CT::Unknown;
    }

    // ------------------------------------------------------------ types

    CT ty(const Expr* e) const {
        if (!e)
            return CT::Unknown;
        switch (e->kind) {
        case ExprKind::Number: return CT::Num;
        case ExprKind::String:
        case ExprKind::FString: return CT::Str;
        case ExprKind::True:
        case ExprKind::False:
        case ExprKind::Compare:
        case ExprKind::And:
        case ExprKind::Or: return CT::Bool;
        case ExprKind::None: return CT::Ent;
        case ExprKind::Self: return CT::Ent;
        case ExprKind::Ternary: return ty(e->b.get());
        case ExprKind::Unary: return e->op == Tok::Not ? CT::Bool : ty(e->a.get());
        case ExprKind::Binary: {
            CT a = ty(e->a.get()), b = ty(e->b.get());
            if (e->op == Tok::Plus && (a == CT::Str || b == CT::Str))
                return CT::Str;
            if (a == CT::Vec || b == CT::Vec)
                return CT::Vec;
            return CT::Num;
        }
        case ExprKind::Name: {
            if (auto l = locals_.find(e->text); l != locals_.end())
                return l->second;
            if (auto p = paramTypes_.find(e->text); p != paramTypes_.end())
                return p->second;
            if (const Field* f = field(e->text))
                return f->type;
            return e->text == "dt" ? CT::Num : CT::Unknown;
        }
        case ExprKind::Attr: {
            std::string p = symbolName(e->sym);
            if (e->a && e->a->kind == ExprKind::Name && e->a->text == "game")
                return CT::Num;
            if (ty(e->a.get()) == CT::Vec)
                return CT::Num;
            if (stringProperty(p))
                return CT::Str;
            if (vectorProperty(p))
                return CT::Vec;
            if (boolProperty(p) || p == "exists")
                return CT::Bool;
            if (p == "parent")
                return CT::Ent;
            return CT::Num;
        }
        case ExprKind::Call: {
            if (!e->a)
                return CT::Unknown;
            if (e->a->kind == ExprKind::Attr) {
                std::string m = symbolName(e->a->sym);
                if (m == "direction_to")
                    return CT::Vec;
                if (m == "clone" || m == "find_child")
                    return CT::Ent;
                if (m == "is_touching" || m == "has_component")
                    return CT::Bool;
                return CT::Num;
            }
            if (e->a->kind != ExprKind::Name)
                return CT::Unknown;
            const std::string& f = e->a->text;
            if (f == "vec" || f == "mouse_position" || f == "direction")
                return CT::Vec;
            if (f == "find" || f == "spawn" || f == "create_sprite" || f == "create_text" || f == "camera")
                return CT::Ent;
            if (f == "str")
                return CT::Str;
            if (f.rfind("key_", 0) == 0 || f.rfind("mouse_down", 0) == 0 || f == "mouse_pressed" || f == "mouse_released" ||
                f == "has_data" || f == "is_paused")
                return CT::Bool;
            if (auto r = fnReturn_.find(f); r != fnReturn_.end())
                return r->second == CT::Unknown ? CT::Num : r->second;
            return CT::Num;
        }
        default: return CT::Unknown;
        }
    }

    static std::string cType(CT t) {
        switch (t) {
        case CT::Bool: return "int";
        case CT::Ent: return "AvenEntity";
        case CT::Str: return "const char*";
        default: return "double";
        }
    }

    // A parameter's type from its name (and default), like the other rungs.
    static CT paramType(const std::string& raw, const Expr* def) {
        static const std::set<std::string> objects = {"other", "target", "obj", "enemy", "player", "who", "thing", "hit", "coin", "item"};
        static const std::set<std::string> strings = {"name", "message", "msg", "text", "key", "tag", "label", "path", "scene"};
        if (def && (def->kind == ExprKind::String || def->kind == ExprKind::FString))
            return CT::Str;
        if (def && (def->kind == ExprKind::True || def->kind == ExprKind::False))
            return CT::Bool;
        return objects.count(raw) ? CT::Ent : strings.count(raw) ? CT::Str : CT::Num;
    }

    // ------------------------------------------------------------ expressions

    static int prec(const Expr* e) {
        switch (e->kind) {
        case ExprKind::Ternary: return 1;
        case ExprKind::Or: return 2;
        case ExprKind::And: return 3;
        case ExprKind::Unary: return e->op == Tok::Not ? 8 : 8;
        case ExprKind::Compare: return 5;
        case ExprKind::Binary:
            switch (e->op) {
            case Tok::Plus:
            case Tok::Minus: return 6;
            case Tok::StarStar:
            case Tok::SlashSlash:
            case Tok::Percent: return 10; // become function calls
            default: return 7;
            }
        default: return 10;
        }
    }

    std::string ex(const Expr* e, int minPrec = 0) {
        if (!e)
            return "0";
        std::string s = inner(e);
        if (prec(e) < minPrec)
            return "(" + s + ")";
        return s;
    }

    std::string inner(const Expr* e) {
        switch (e->kind) {
        case ExprKind::Number: return num(e->number);
        case ExprKind::True: return "1";
        case ExprKind::False: return "0";
        case ExprKind::None: return "0";
        case ExprKind::String:
        case ExprKind::FString: return sx(e);
        case ExprKind::Self: return "self";
        case ExprKind::Name: {
            if (e->text == "PI" || e->text == "pi") {
                helpers_.insert("deg");
                return "AVEN_PI";
            }
            if (e->text == "game")
                return todo(e->line, "'game' on its own isn't a value in C; use aven_game_get(\"name\", 0) for each value.");
            if (ty(e) == CT::Vec)
                return todo(e->line, "Vectors are split into x, y and z in C.");
            return ref(e->text);
        }
        case ExprKind::Unary:
            if (e->op == Tok::Minus)
                return "-" + ex(e->a.get(), 8);
            if (e->op == Tok::Plus)
                return ex(e->a.get(), 8);
            return "!" + ex(e->a.get(), 8);
        case ExprKind::Binary: return binary(e);
        case ExprKind::And: return ex(e->a.get(), 3) + " && " + ex(e->b.get(), 3);
        case ExprKind::Or: return ex(e->a.get(), 2) + " || " + ex(e->b.get(), 2);
        case ExprKind::Compare: return compare(e);
        case ExprKind::Call: return call(e);
        case ExprKind::Attr: return attr(e);
        case ExprKind::Ternary: return ex(e->a.get(), 2) + " ? " + ex(e->b.get(), 2) + " : " + ex(e->c.get(), 1);
        case ExprKind::List:
        case ExprKind::Dict:
        case ExprKind::Index:
        case ExprKind::Slice:
            return "0 " + todo(e->line, "C has no built-in lists or dictionaries. Use a fixed-size array (float values[10];) "
                                        "and a count, or keep the list in EasyScript.");
        }
        return "0";
    }

    std::string binary(const Expr* e) {
        const Expr* a = e->a.get();
        const Expr* b = e->b.get();
        if (e->op == Tok::Plus && (ty(a) == CT::Str || ty(b) == CT::Str))
            return sx(e);
        if (ty(a) == CT::Vec || ty(b) == CT::Vec)
            return "0 " + todo(e->line, "Vector maths is done one axis at a time in C (x, then y).");
        int p = prec(e);
        switch (e->op) {
        case Tok::Plus: return ex(a, p) + " + " + ex(b, p + 1);
        case Tok::Minus:
            if (a->kind == ExprKind::Number && a->number == 0) // blocks write "change by -x" as 0 - x
                return "-" + ex(b, 8);
            return ex(a, p) + " - " + ex(b, p + 1);
        case Tok::Star: return ex(a, p) + " * " + ex(b, p + 1);
        case Tok::Slash:
            // EasyScript always divides as decimals; C divides whole numbers as whole numbers.
            if (a->kind == ExprKind::Number && b->kind == ExprKind::Number && a->number == std::floor(a->number))
                return num(a->number) + ".0 / " + ex(b, p + 1);
            if (a->kind == ExprKind::Name && loopInts_.count(a->text))
                return "(double)" + ex(a, 10) + " / " + ex(b, p + 1);
            return ex(a, p) + " / " + ex(b, p + 1);
        case Tok::SlashSlash: return "floor(" + ex(a) + " / " + ex(b, 8) + ")";
        case Tok::Percent: return "fmod(" + ex(a) + ", " + ex(b) + ")";
        case Tok::StarStar: return "pow(" + ex(a) + ", " + ex(b) + ")";
        default: return ex(a, p) + " /* ? */ " + ex(b, p + 1);
        }
    }

    std::string compare(const Expr* e) {
        const Expr* a = e->a.get();
        const Expr* b = e->b.get();
        if (e->op == Tok::In || e->op == Tok::Not || e->notIn)
            return "0 " + todo(e->line, "'in' checks a list, and C has no built-in lists.");
        const char* op = "==";
        switch (e->op) {
        case Tok::Eq: op = "=="; break;
        case Tok::NotEq: op = "!="; break;
        case Tok::Lt: op = "<"; break;
        case Tok::Gt: op = ">"; break;
        case Tok::LtEq: op = "<="; break;
        case Tok::GtEq: op = ">="; break;
        default: break;
        }
        if (ty(a) == CT::Str || ty(b) == CT::Str) {
            note("C compares text with strcmp(a, b), which is 0 when they're the same.");
            return std::string("strcmp(") + sx(a) + ", " + sx(b) + ") " + op + " 0";
        }
        return ex(a, 6) + " " + op + " " + ex(b, 6);
    }

    // An object expression (AvenEntity).
    std::string obj(const Expr* e) {
        if (!e || e->kind == ExprKind::Self)
            return "self";
        return ex(e, 10);
    }

    std::string attr(const Expr* e) {
        std::string p = symbolName(e->sym);
        const Expr* target = e->a.get();
        if (target && target->kind == ExprKind::Name && target->text == "game")
            return "aven_game_get(" + cQuote(p) + ", 0)";
        if (ty(target) == CT::Vec) {
            auto parts = vecParts(target);
            int i = p == "x" ? 0 : p == "y" ? 1 : p == "z" ? 2 : -1;
            if (parts && i >= 0 && i < static_cast<int>(parts->size()))
                return (*parts)[static_cast<size_t>(i)];
            return "0 " + todo(e->line, "Only .x, .y and .z of a vector can be read in C.");
        }
        if (ty(target) != CT::Ent && target && target->kind != ExprKind::Self)
            return "0 " + todo(e->line, "C reads properties of objects (AvenEntity), not of components or other values.");
        std::string o = obj(target);
        if (p == "name")
            return "aven_name(" + o + ")";
        if (p == "tag")
            return "aven_tag(" + o + ")";
        if (p == "exists")
            return "aven_exists(" + o + ")";
        if (stringProperty(p))
            return "aven_get_text(" + o + ", " + cQuote(p) + ")";
        if (vectorProperty(p))
            return "0 " + todo(e->line, "Vectors are split into x, y and z in C: aven_get(self, \"x\")...");
        if (p == "parent")
            return "0 " + todo(e->line, "The C API can't get an object's parent yet.");
        return "aven_get(" + o + ", " + cQuote(p) + ")";
    }

    // The x, y (and z) of a vector expression, if it can be split.
    std::optional<std::vector<std::string>> vecParts(const Expr* e) {
        if (!e)
            return std::nullopt;
        switch (e->kind) {
        case ExprKind::Call: {
            if (e->a && e->a->kind == ExprKind::Name) {
                const std::string& f = e->a->text;
                if (f == "vec") {
                    std::vector<std::string> parts;
                    for (size_t i = 0; i < e->items.size() && i < 3; ++i)
                        parts.push_back(ex(e->items[i].get()));
                    while (parts.size() < 2)
                        parts.push_back("0");
                    return parts;
                }
                if (f == "mouse_position")
                    return std::vector<std::string>{"aven_mouse_x()", "aven_mouse_y()"};
            }
            if (e->a && e->a->kind == ExprKind::Attr && symbolName(e->a->sym) == "direction_to" && !e->items.empty()) {
                helpers_.insert("direction");
                std::string from = obj(e->a->a.get()), to = ex(e->items[0].get());
                return std::vector<std::string>{"direction_x_to(" + from + ", " + to + ")", "direction_y_to(" + from + ", " + to + ")"};
            }
            return std::nullopt;
        }
        case ExprKind::Attr: {
            auto comps = vectorProperty(symbolName(e->sym));
            if (!comps || (e->a && ty(e->a.get()) != CT::Ent && e->a->kind != ExprKind::Self))
                return std::nullopt;
            std::string o = obj(e->a.get());
            std::vector<std::string> parts;
            for (size_t i = 0; i < (opt_.is3D ? 3u : 2u); ++i)
                parts.push_back("aven_get(" + o + ", " + cQuote((*comps)[i]) + ")");
            return parts;
        }
        case ExprKind::Name: {
            if (ty(e) != CT::Vec)
                return std::nullopt;
            std::string n = ref(e->text);
            return std::vector<std::string>{n + "[0]", n + "[1]", n + "[2]"};
        }
        case ExprKind::Unary: {
            auto a = vecParts(e->a.get());
            if (!a || e->op != Tok::Minus)
                return a;
            for (auto& p : *a)
                p = "-(" + p + ")";
            return a;
        }
        case ExprKind::Binary: {
            CT ta = ty(e->a.get()), tb = ty(e->b.get());
            if ((e->op == Tok::Plus || e->op == Tok::Minus) && ta == CT::Vec && tb == CT::Vec) {
                auto a = vecParts(e->a.get()), b = vecParts(e->b.get());
                if (!a || !b)
                    return std::nullopt;
                std::vector<std::string> parts;
                for (size_t i = 0; i < std::min(a->size(), b->size()); ++i)
                    parts.push_back((*a)[i] + (e->op == Tok::Plus ? " + " : " - ") + "(" + (*b)[i] + ")");
                return parts;
            }
            if ((e->op == Tok::Star || e->op == Tok::Slash) && (ta == CT::Vec) != (tb == CT::Vec)) {
                const Expr* v = ta == CT::Vec ? e->a.get() : e->b.get();
                const Expr* s = ta == CT::Vec ? e->b.get() : e->a.get();
                auto parts = vecParts(v);
                if (!parts || (e->op == Tok::Slash && ta != CT::Vec))
                    return std::nullopt;
                std::string k = ex(s, 8);
                for (auto& p : *parts)
                    p = "(" + p + ")" + (e->op == Tok::Star ? " * " : " / ") + k;
                return parts;
            }
            return std::nullopt;
        }
        default: return std::nullopt;
        }
    }

    // A text expression (const char*). f-strings and joined text are written into a buffer first.
    std::string sx(const Expr* e) {
        if (!e)
            return "\"\"";
        switch (e->kind) {
        case ExprKind::String: return cQuote(e->text);
        case ExprKind::Name:
            if (ty(e) == CT::Str)
                return ref(e->text);
            break;
        case ExprKind::Attr:
            if (ty(e) == CT::Str)
                return attr(e);
            break;
        case ExprKind::Ternary: return "(" + ex(e->a.get(), 2) + " ? " + sx(e->b.get()) + " : " + sx(e->c.get()) + ")";
        default: break;
        }
        // Anything else: printf it into a buffer.
        std::string format, args;
        pieces(e, format, args);
        if (args.empty())
            return "\"" + format + "\"";
        std::string buffer = "text" + std::to_string(++temp_);
        pre_.push_back("char " + buffer + "[256];");
        pre_.push_back("snprintf(" + buffer + ", sizeof " + buffer + ", \"" + format + "\"" + args + ");");
        return buffer;
    }

    void pieces(const Expr* e, std::string& format, std::string& args) {
        auto value = [&](const Expr* v, const std::string& spec) {
            CT t = ty(v);
            if (t == CT::Str) {
                format += "%s";
                args += ", " + sx(v);
            } else if (spec.size() >= 3 && spec[0] == '.' && spec.back() == 'f') {
                format += "%" + spec;
                args += ", (double)(" + ex(v) + ")";
            } else {
                format += "%g";
                args += ", (double)(" + ex(v) + ")";
            }
        };
        switch (e->kind) {
        case ExprKind::String: format += formatText(e->text); return;
        case ExprKind::FString:
            for (size_t i = 0; i < e->literalParts.size(); ++i) {
                format += formatText(e->literalParts[i]);
                if (i < e->items.size())
                    value(e->items[i].get(), i < e->formatSpecs.size() ? e->formatSpecs[i] : "");
            }
            return;
        case ExprKind::Binary:
            if (e->op == Tok::Plus && (ty(e->a.get()) == CT::Str || ty(e->b.get()) == CT::Str)) {
                pieces(e->a.get(), format, args);
                pieces(e->b.get(), format, args);
                return;
            }
            break;
        case ExprKind::Call:
            if (e->a && e->a->kind == ExprKind::Name && e->a->text == "str" && !e->items.empty()) {
                value(e->items[0].get(), "");
                return;
            }
            break;
        default: break;
        }
        value(e, "");
    }

    std::vector<std::string> numbers(const Expr* call, size_t from = 0) {
        std::vector<std::string> out;
        for (size_t i = from; i < call->items.size() - call->kwNames.size(); ++i) {
            const Expr* a = call->items[i].get();
            if (ty(a) == CT::Vec) {
                if (auto parts = vecParts(a)) {
                    for (auto& p : *parts)
                        out.push_back(p);
                    continue;
                }
            }
            out.push_back(ex(a));
        }
        return out;
    }

    static std::string doubles(const std::vector<std::string>& values) {
        if (values.empty())
            return "NULL, 0";
        std::string list;
        for (size_t i = 0; i < values.size(); ++i)
            list += (i ? ", " : "") + values[i];
        return "(const double[]){" + list + "}, " + std::to_string(values.size());
    }

    std::string argN(const Expr* call, size_t i, const std::string& fallback) {
        size_t positional = call->items.size() - call->kwNames.size();
        if (i >= positional)
            return fallback;
        const Expr* a = call->items[i].get();
        CT t = ty(a);
        if (t == CT::Str || t == CT::Ent || t == CT::Vec)
            return "0 " + todo(call->line, "This C function takes a number here, and EasyScript passed text, an object or a vector. "
                                           "Share other values through game values (aven_game_set) or object properties.");
        return ex(a);
    }

    std::string call(const Expr* e) {
        const Expr* callee = e->a.get();
        if (!e->kwNames.empty())
            note("Named arguments (like volume=0.5) are left out in C; the C functions take the main values only.");
        if (callee && callee->kind == ExprKind::Attr) {
            if (callee->a && callee->a->kind == ExprKind::Name && callee->a->text == "game")
                return "0 " + todo(e->line, "game values are numbers in C (aven_game_get/aven_game_set).");
            return method(e, symbolName(callee->sym), callee->a.get());
        }
        if (!callee || callee->kind != ExprKind::Name)
            return "0 " + todo(e->line, "C can't call this.");
        return global(e, callee->text);
    }

    std::string method(const Expr* e, const std::string& m, const Expr* target) {
        if (ty(target) != CT::Ent && target && target->kind != ExprKind::Self) {
            static const std::set<std::string> text = {"upper", "lower", "split", "join", "strip", "replace", "append", "pop",
                                                       "insert", "index", "clear", "keys", "values", "contains", "remove"};
            if (text.count(m))
                return "0 " + todo(e->line, "Lists and text methods like ." + m + "() aren't built into C.");
            return "0 " + todo(e->line, "In C, methods work on objects (AvenEntity); ." + m + "() on this value isn't available.");
        }
        std::string o = obj(target);
        size_t n = e->items.size() - e->kwNames.size();
        if (m == "destroy")
            return "aven_destroy(" + o + ")";
        if (m == "send")
            return "aven_send(" + o + ", " + (n ? sx(e->items[0].get()) : "\"\"") + ", " + argN(e, 1, "0") + ")";
        if (m == "say") {
            note("say() speech bubbles aren't in the C API yet; the translation prints the words to the Console.");
            return "aven_print(" + (n ? sx(e->items[0].get()) : "\"\"") + ")";
        }
        if (m == "play_sound")
            return "aven_play_sound(" + (n ? sx(e->items[0].get()) : "\"\"") + ")";
        if (m == "distance_to" && n) {
            helpers_.insert("distance");
            return "distance_between(" + o + ", " + ex(e->items[0].get()) + ")";
        }
        if (m == "move_toward" && n >= 2 && ty(e->items[0].get()) == CT::Ent) {
            helpers_.insert("move_toward");
            return "move_toward_object(" + o + ", " + ex(e->items[0].get()) + ", " + ex(e->items[1].get()) + ")";
        }
        if (m == "look_at" && n && ty(e->items[0].get()) == CT::Ent) {
            helpers_.insert("look_at");
            return "turn_toward(" + o + ", " + ex(e->items[0].get()) + ")";
        }
        static const std::set<std::string> objectOnly = {"is_touching", "clone", "find_child", "get_component", "add_component",
                                                         "has_component", "remove_component", "tween", "direction_to"};
        if (objectOnly.count(m))
            return "0 " + todo(e->line, "." + m + "() isn't in the C API yet (it needs objects, components or text).");
        // Everything else takes numbers: aven_call runs the same method EasyScript would.
        for (size_t i = 0; i < n; ++i) {
            CT t = ty(e->items[i].get());
            if (t == CT::Str || t == CT::Ent)
                return "0 " + todo(e->line, "aven_call() passes numbers only, and ." + m + "() is given text or an object here.");
        }
        return "aven_call(" + o + ", " + cQuote(m) + ", " + doubles(numbers(e)) + ")";
    }

    std::string global(const Expr* e, const std::string& f) {
        size_t n = e->items.size() - e->kwNames.size();
        auto a0 = [&] { return argN(e, 0, "0"); };
        auto s0 = [&] { return n ? sx(e->items[0].get()) : std::string("\"\""); };
        // The script's own functions.
        if (userFns_.count(f)) {
            usesMy_ = usesMy_ || !fields_.empty();
            const Stmt* def = userFns_[f];
            std::string args = fields_.empty() ? "self, NULL" : "self, my";
            for (size_t i = 0; i < def->params.size(); ++i) {
                const Expr* given = i < n ? e->items[i].get() : nullptr;
                size_t firstDefault = def->params.size() - def->defaults.size();
                const Expr* fallback = i >= firstDefault ? def->defaults[i - firstDefault].get() : nullptr;
                const Expr* value = given ? given : fallback;
                CT t = paramType(symbolName(def->params[i]), fallback);
                args += ", " + (value ? (t == CT::Str ? sx(value) : ex(value)) : std::string("0"));
            }
            return fnName(f) + "(" + args + ")";
        }
        if (f == "print") {
            std::string format, args;
            for (size_t i = 0; i < n; ++i) {
                if (i)
                    format += " ";
                pieces(e->items[i].get(), format, args);
            }
            if (args.empty())
                return "aven_print(\"" + format + "\")";
            std::string buffer = "text" + std::to_string(++temp_);
            pre_.push_back("char " + buffer + "[256];");
            pre_.push_back("snprintf(" + buffer + ", sizeof " + buffer + ", \"" + format + "\"" + args + ");");
            return "aven_print(" + buffer + ")";
        }
        if (f == "key_down" || f == "key_pressed" || f == "key_released")
            return "aven_" + f + "(" + s0() + ")";
        if (f == "mouse_down")
            return "aven_mouse_down()";
        if (f == "mouse_pressed" || f == "mouse_released") {
            note("The C API has aven_mouse_down() (held); remember last frame's value to spot a new click.");
            return "aven_mouse_down()";
        }
        if (f == "mouse_x" || f == "mouse_y")
            return "aven_" + f + "()";
        if (f == "find")
            return "aven_find(" + s0() + ")";
        if (f == "count")
            return "aven_find_all(" + s0() + ", NULL, 0)";
        if (f == "spawn") {
            std::vector<std::string> pos = numbers(e, 1);
            while (pos.size() < 3)
                pos.push_back("0");
            return "aven_spawn(" + s0() + ", " + pos[0] + ", " + pos[1] + ", " + pos[2] + ")";
        }
        if (f == "destroy")
            return "aven_destroy(" + a0() + ")";
        if (f == "play_sound" || f == "load_scene")
            return "aven_" + f + "(" + s0() + ")";
        if (f == "broadcast")
            return "aven_send(0, " + s0() + ", " + argN(e, 1, "0") + ")";
        if (f == "time")
            return "aven_time()";
        if (f == "delta_time")
            return "aven_delta_time()";
        if (f == "random")
            return "aven_random(0, 1)";
        if (f == "random_range")
            return "aven_random(" + a0() + ", " + argN(e, 1, "1") + ")";
        if (f == "random_int")
            return "floor(aven_random(" + a0() + ", " + argN(e, 1, "1") + " + 1))";
        if (f == "get_game")
            return "aven_game_get(" + s0() + ", " + argN(e, 1, "0") + ")";
        if (f == "axis") {
            helpers_.insert("axis");
            return "axis_value(" + s0() + ")";
        }
        if (f == "abs")
            return "fabs(" + a0() + ")";
        if ((f == "min" || f == "max") && n >= 2) {
            std::string out = argN(e, 0, "0");
            for (size_t i = 1; i < n; ++i)
                out = std::string(f == "min" ? "fmin(" : "fmax(") + out + ", " + ex(e->items[i].get()) + ")";
            return out;
        }
        if (f == "sqrt" || f == "floor" || f == "ceil" || f == "round" || f == "exp")
            return f + "(" + a0() + ")";
        if (f == "log")
            return "log(" + a0() + ")";
        if (f == "pow")
            return "pow(" + a0() + ", " + argN(e, 1, "1") + ")";
        if (f == "sin" || f == "cos" || f == "tan") {
            helpers_.insert("deg");
            return f + "((" + a0() + ") * AVEN_DEG)"; // EasyScript's angles are in degrees
        }
        if (f == "asin" || f == "acos" || f == "atan") {
            helpers_.insert("deg");
            return f + "(" + a0() + ") / AVEN_DEG";
        }
        if (f == "atan2") {
            helpers_.insert("deg");
            return "atan2(" + a0() + ", " + argN(e, 1, "1") + ") / AVEN_DEG";
        }
        if (f == "degrees" || f == "radians") {
            helpers_.insert("deg");
            return "(" + a0() + (f == "degrees" ? ") / AVEN_DEG" : ") * AVEN_DEG");
        }
        if (f == "clamp")
            return "fmin(fmax(" + a0() + ", " + argN(e, 1, "0") + "), " + argN(e, 2, "1") + ")";
        if (f == "lerp")
            return "(" + argN(e, 0, "0") + " + (" + argN(e, 1, "0") + " - " + argN(e, 0, "0") + ") * " + argN(e, 2, "0") + ")";
        if (f == "sign") {
            helpers_.insert("sign");
            return "sign_of(" + a0() + ")";
        }
        if (f == "int")
            return "(double)(long long)(" + a0() + ")";
        if (f == "float")
            return "(double)(" + a0() + ")";
        if (f == "len" && n && ty(e->items[0].get()) == CT::Str)
            return "(double)strlen(" + s0() + ")";
        if (f == "distance" && n >= 2 && ty(e->items[0].get()) == CT::Ent) {
            helpers_.insert("distance");
            return "distance_between(" + a0() + ", " + argN(e, 1, "0") + ")";
        }
        if (f == "str")
            return sx(e);
        if (f == "wait")
            return "0 " + todo(e->line, "C behaviors can't pause with wait(). Count time in on_update instead (the Bobber in "
                                        "native/src/behaviors.c keeps its own clock).");
        return "0 " + todo(e->line, f + "() isn't in the C API yet.");
    }

    // ------------------------------------------------------------ statements

    void body(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts)
            stmt(*s);
    }

    void assignTo(const Expr* target, const std::string& opText, const Expr* value, int lineNo) {
        bool aug = !opText.empty();
        // `x op= v` for text and vectors is spelled out; for numbers C has the same operators.
        auto combined = [&](const std::string& current) -> std::string {
            if (!aug)
                return ex(value);
            if (opText == "//")
                return "floor(" + current + " / " + ex(value, 8) + ")";
            if (opText == "%")
                return "fmod(" + current + ", " + ex(value) + ")";
            if (opText == "**")
                return "pow(" + current + ", " + ex(value) + ")";
            return current + " " + opText + " " + ex(value, opText == "+" || opText == "-" ? 7 : 8);
        };
        if (target->kind == ExprKind::Name) {
            const std::string& raw = target->text;
            CT t = ty(target);
            std::string name = ref(raw);
            if (t == CT::Str) {
                std::string v = sx(value);
                if (aug) {
                    line("strncat(" + name + ", " + v + ", sizeof " + name + " - strlen(" + name + ") - 1);", lineNo);
                    return;
                }
                if (isLocal(raw) && !locals_.count(raw)) { // a text parameter: point it elsewhere
                    line(name + " = " + v + ";", lineNo);
                    return;
                }
                line("snprintf(" + name + ", sizeof " + name + ", \"%s\", " + v + ");", lineNo);
                return;
            }
            if (t == CT::Vec) {
                auto parts = vecParts(value);
                if (!parts || aug) {
                    line(todo(lineNo, "Vector maths is done one axis at a time in C (x, then y)."), lineNo);
                    return;
                }
                for (size_t i = 0; i < parts->size() && i < 3; ++i)
                    line(name + "[" + std::to_string(i) + "] = " + (*parts)[i] + ";", i == 0 ? lineNo : 0);
                return;
            }
            if (aug && (opText == "+" || opText == "-" || opText == "*" || opText == "/")) {
                line(name + " " + opText + "= " + ex(value) + ";", lineNo);
                return;
            }
            line(name + " = " + combined(name) + ";", lineNo);
            return;
        }
        if (target->kind == ExprKind::Attr) {
            std::string p = symbolName(target->sym);
            const Expr* o = target->a.get();
            if (o && o->kind == ExprKind::Name && o->text == "game") {
                std::string current = "aven_game_get(" + cQuote(p) + ", 0)";
                line("aven_game_set(" + cQuote(p) + ", " + combined(current) + ");", lineNo);
                return;
            }
            if (ty(o) == CT::Vec && o->kind == ExprKind::Attr) { // self.position.x = 3
                auto comps = vectorProperty(symbolName(o->sym));
                int i = p == "x" ? 0 : p == "y" ? 1 : p == "z" ? 2 : -1;
                if (comps && i >= 0) {
                    std::string ob = obj(o->a.get());
                    std::string prop = cQuote((*comps)[static_cast<size_t>(i)]);
                    line("aven_set(" + ob + ", " + prop + ", " + combined("aven_get(" + ob + ", " + prop + ")") + ");", lineNo);
                    return;
                }
            }
            if (ty(o) != CT::Ent && o && o->kind != ExprKind::Self) {
                line(todo(lineNo, "C sets properties of objects (AvenEntity), not of components or other values."), lineNo);
                return;
            }
            std::string ob = obj(o);
            if (stringProperty(p) || (ty(value) == CT::Str && !aug)) { // text for any property, e.g. a color "#22c55e"
                std::string v = sx(value);
                if (aug)
                    v = sx(target); // rare: text += text; keep it simple
                line("aven_set_text(" + ob + ", " + cQuote(p) + ", " + v + ");", lineNo);
                return;
            }
            if (auto comps = vectorProperty(p)) {
                if (!aug && p == "scale" && (ty(value) == CT::Num || ty(value) == CT::Unknown)) { // a size for every axis
                    std::string v = ex(value);
                    for (size_t i = 0; i < (opt_.is3D ? 3u : 2u); ++i)
                        line("aven_set(" + ob + ", " + cQuote((*comps)[i]) + ", " + v + ");", i == 0 ? lineNo : 0);
                    return;
                }
                auto parts = vecParts(value);
                if (!parts || aug) {
                    line(todo(lineNo, "Vector maths is done one axis at a time in C (x, then y)."), lineNo);
                    return;
                }
                for (size_t i = 0; i < parts->size() && i < 3; ++i)
                    line("aven_set(" + ob + ", " + cQuote((*comps)[i]) + ", " + (*parts)[i] + ");", i == 0 ? lineNo : 0);
                return;
            }
            std::string current = "aven_get(" + ob + ", " + cQuote(p) + ")";
            line("aven_set(" + ob + ", " + cQuote(p) + ", " + combined(current) + ");", lineNo);
            return;
        }
        line(todo(lineNo, "C can't assign to this (lists and dictionaries aren't built into C)."), lineNo);
    }

    void ifChain(const Stmt& s, bool elseIf) {
        std::string cond = ex(s.expr.get());
        // An else-if whose condition needs work first (a text buffer) becomes else { work; if ... }.
        bool nested = elseIf && !pre_.empty();
        if (nested) {
            emit("} else {");
            ++depth_;
            elseIf = false;
        }
        line(std::string(elseIf ? "} else if (" : "if (") + cond + ") {", s.line);
        ++depth_;
        body(s.body);
        --depth_;
        if (s.orelse.size() == 1 && s.orelse[0]->kind == StmtKind::If && s.orelse[0]->line != s.line) {
            flush(s.orelse[0]->line);
            pre_.clear();
            ifChain(*s.orelse[0], true);
        } else {
            if (!s.orelse.empty()) {
                emit("} else {");
                ++depth_;
                body(s.orelse);
                --depth_;
            }
            emit("}");
        }
        if (nested) {
            --depth_;
            emit("}");
        }
    }

    void forLoop(const Stmt& s) {
        if (s.targets.size() != 1 || s.targets[0]->kind != ExprKind::Name) {
            line(todo(s.line, "C loops count with a number; this loop needs lists."), s.line);
            return;
        }
        const std::string raw = s.targets[0]->text;
        std::string var = ref(raw);
        bool declared = locals_.count(raw) > 0;
        // The loop variable's type while the loop runs.
        struct Scoped {
            CTranslator& t;
            std::string raw;
            bool had;
            ~Scoped() {
                if (!had)
                    t.paramTypes_.erase(raw);
                t.loopInts_.erase(raw);
            }
        };
        const Expr* it = s.expr.get();
        if (it && it->kind == ExprKind::Call && it->a && it->a->kind == ExprKind::Name && it->a->text == "range") {
            size_t n = it->items.size();
            std::string from = n >= 2 ? ex(it->items[0].get()) : "0";
            std::string to = n >= 2 ? ex(it->items[1].get()) : n ? ex(it->items[0].get()) : "0";
            std::string step = n >= 3 ? ex(it->items[2].get()) : "1";
            bool down = n >= 3 && it->items[2]->kind == ExprKind::Unary && it->items[2]->op == Tok::Minus;
            Scoped scoped{*this, raw, paramTypes_.count(raw) > 0};
            if (!declared) {
                paramTypes_[raw] = CT::Num;
                loopInts_.insert(raw);
            }
            std::string init = declared ? var + " = " + from : "int " + var + " = " + from;
            std::string next = step == "1" ? "++" + var : var + " += " + step;
            line("for (" + init + "; " + var + (down ? " > " : " < ") + to + "; " + next + ") {", s.line);
            ++depth_;
            body(s.body);
            --depth_;
            emit("}");
            return;
        }
        if (it && it->kind == ExprKind::Call && it->a && it->a->kind == ExprKind::Name && it->a->text == "find_all" && !it->items.empty()) {
            Scoped scoped{*this, raw, paramTypes_.count(raw) > 0};
            if (!declared)
                paramTypes_[raw] = CT::Ent;
            std::string found = "found" + std::to_string(++temp_), count = "count" + std::to_string(temp_);
            line("AvenEntity " + found + "[256];", 0);
            emit("int " + count + " = aven_find_all(" + sx(it->items[0].get()) + ", " + found + ", 256);");
            emit("for (int i" + std::to_string(temp_) + " = 0; i" + std::to_string(temp_) + " < " + count + " && i" +
                 std::to_string(temp_) + " < 256; ++i" + std::to_string(temp_) + ") {");
            ++depth_;
            emit(std::string(declared ? "" : "AvenEntity ") + var + " = " + found + "[i" + std::to_string(temp_) + "];");
            body(s.body);
            --depth_;
            emit("}");
            return;
        }
        line(todo(s.line, "C loops count with a number: for (int i = 0; i < n; ++i). Looping over lists needs arrays."), s.line);
    }

    void stmt(const Stmt& s) {
        flush(s.line);
        pre_.clear();
        switch (s.kind) {
        case StmtKind::Expression: {
            const Expr* e = s.expr.get();
            if (e && e->kind == ExprKind::Call && e->a && e->a->kind == ExprKind::Name && e->a->text == "wait") {
                line(todo(s.line, "C behaviors can't pause with wait(). Count time in on_update instead (the Bobber in "
                                  "native/src/behaviors.c keeps its own clock)."),
                     s.line);
                return;
            }
            std::string text = ex(e);
            if (text.rfind("0 /*", 0) == 0) // untranslatable: just the comment
                text = text.substr(2);
            else
                text += ";";
            line(text, s.line);
            return;
        }
        case StmtKind::Assign:
            if (s.targets.size() != 1 || (s.targets[0]->kind == ExprKind::List && s.targets[0]->isTuple)) {
                line(todo(s.line, "C assigns one value at a time."), s.line);
                return;
            }
            assignTo(s.targets[0].get(), "", s.expr.get(), s.line);
            return;
        case StmtKind::AugAssign: {
            std::string op;
            switch (s.op) {
            case Tok::PlusAssign: op = "+"; break;
            case Tok::MinusAssign: op = "-"; break;
            case Tok::StarAssign: op = "*"; break;
            case Tok::SlashAssign: op = "/"; break;
            case Tok::PercentAssign: op = "%"; break;
            default: op = "+"; break;
            }
            assignTo(s.targets[0].get(), op, s.expr.get(), s.line);
            return;
        }
        case StmtKind::If: ifChain(s, false); return;
        case StmtKind::While: {
            std::string cond = ex(s.expr.get());
            if (!pre_.empty()) {
                // The condition needs work each time round.
                auto work = pre_;
                pre_.clear();
                line("while (1) {", s.line);
                ++depth_;
                for (auto& w : work)
                    emit(w);
                emit("if (!(" + cond + "))");
                emit("    break;");
            } else {
                line("while (" + cond + ") {", s.line);
                ++depth_;
            }
            body(s.body);
            --depth_;
            emit("}");
            return;
        }
        case StmtKind::For: forLoop(s); return;
        case StmtKind::Return:
            if (s.expr && inUserFn_ && returnType_ != CT::Unknown) {
                std::string v = returnType_ == CT::Str ? sx(s.expr.get()) : ex(s.expr.get());
                if (returnType_ == CT::Str)
                    note("Returning text from a C function needs a buffer that lives on (static or passed in).");
                line("return " + v + ";", s.line);
            } else {
                line(inUserFn_ && returnType_ != CT::Unknown ? "return 0;" : "return;", s.line);
            }
            return;
        case StmtKind::Break: line("break;", s.line); return;
        case StmtKind::Continue: line("continue;", s.line); return;
        case StmtKind::Pass: line(";", s.line); return;
        case StmtKind::Def: line(todo(s.line, "C doesn't put functions inside functions."), s.line); return;
        case StmtKind::Global: return;
        }
    }

    // Locals are declared at the top of each function, like classic C.
    void hoistStmt(const Stmt& s) {
        auto consider = [&](const Expr* t, const Expr* value) {
            if (!t || t->kind != ExprKind::Name || field(t->text) || paramTypes_.count(t->text) || locals_.count(t->text))
                return;
            CT type = value ? ty(value) : CT::Num;
            locals_[t->text] = type == CT::Unknown ? CT::Num : type;
        };
        if (s.kind == StmtKind::Assign && s.targets.size() == 1)
            consider(s.targets[0].get(), s.expr.get());
        if (s.kind == StmtKind::AugAssign && !s.targets.empty())
            consider(s.targets[0].get(), nullptr);
        // range() and find_all() loops declare their own variable.
        hoistLocals(s.body);
        hoistLocals(s.orelse);
    }

    void hoistLocals(const std::vector<StmtPtr>& stmts) {
        for (auto& s : stmts)
            hoistStmt(*s);
    }

    std::string localDecl(const std::string& raw, CT t) {
        std::string n = cName(raw);
        switch (t) {
        case CT::Str: return "char " + n + "[128] = \"\";";
        case CT::Vec: return "double " + n + "[3] = {0, 0, 0};";
        case CT::Bool: return "int " + n + " = 0;";
        case CT::Ent: return "AvenEntity " + n + " = 0;";
        default: return "double " + n + " = 0;";
        }
    }

    // Writes a function body; returns it with its local declarations (and `my`) on top.
    std::string function(const std::vector<StmtPtr>& stmts, const std::function<void()>& before = {}) {
        std::string text;
        body_ = &text;
        depth_ = 1;
        usesMy_ = false;
        temp_ = 0;
        if (before)
            before();
        body(stmts);
        std::string head;
        if (usesMy_ && !fields_.empty() && !inUserFn_)
            head += "    " + cls_ + "* my = (" + cls_ + "*)data;\n";
        for (auto& [raw, t] : locals_)
            head += "    " + localDecl(raw, t) + "\n";
        if (!head.empty() && !text.empty())
            head += "\n";
        return head + text;
    }

    // ------------------------------------------------------------ the file

    std::string fieldType(const Field& f) const {
        switch (f.type) {
        case CT::Bool: return "int32_t";
        case CT::Ent: return "AvenEntity";
        case CT::Str: return "char";
        case CT::Vec: return "double";
        default: return "float";
        }
    }

    std::string fieldDecl(const Field& f) const {
        switch (f.type) {
        case CT::Str: return "char " + f.c + "[64];";
        case CT::Vec: return "double " + f.c + "[3];";
        default: return fieldType(f) + " " + f.c + ";";
        }
    }

    std::string eventParams(const EventInfo& ev, const Stmt* def) {
        std::string out = "AvenEntity self, void* data";
        for (size_t i = 0; i < ev.params.size(); ++i) {
            std::string raw = def && i < def->params.size() ? symbolName(def->params[i]) : "";
            std::string fallback;
            switch (ev.params[i]) {
            case CT::Num: fallback = i == 1 ? "value" : "dt"; out += ", " + std::string(i == 1 ? "double " : "float "); break;
            case CT::Ent: fallback = "other"; out += ", AvenEntity "; break;
            case CT::Str: fallback = std::string(ev.name) == "on_key_pressed" ? "key" : "message"; out += ", const char* "; break;
            default: break;
            }
            std::string c = raw.empty() ? fallback : cName(raw);
            if (c == "data" || c == "self" || c == "my" || c == "data_")
                c = fallback;
            if (!raw.empty()) {
                rename_[raw] = c;
                paramTypes_[raw] = ev.params[i];
            }
            out += c;
        }
        return out;
    }

    void generate() {
        std::string& o = out_;
        // Header comment: the script's own opening comments, then how to use the file.
        o += "/*\n * " + cls_ + ": the EasyScript script translated into C by Aven's Code Ladder.\n";
        int firstCode = prog_.statements.empty() ? static_cast<int>(lines_.size()) + 1 : prog_.statements[0]->line;
        for (int l = 1; l < firstCode; ++l)
            if (auto it = fullComments_.find(l); it != fullComments_.end())
                o += " * " + commentSafe(it->second) + "\n";
        commentCursor_ = firstCode - 1;
        o += " *\n * To use it: save it in native/src (the Code Ladder's Save button does that), press Build in\n"
             " * Tools > Native Code, then give the object a NativeScript component with the behavior \"" +
             cls_ + "\".\n */\n\n";
        o += "#include \"aven.h\"\n\n#include <math.h>\n#include <stdio.h>\n#include <string.h>\n\n";

        // Everything each object remembers.
        std::string fns, protos;
        std::string start, starting;
        {
            // Starting values that aren't Inspector properties are set when the object starts.
            std::vector<StmtPtr> none;
            locals_.clear();
            rename_.clear();
            paramTypes_.clear();
            const Stmt* userStart = nullptr;
            for (auto* d : defs_)
                if (symbolName(d->name) == "on_start")
                    userStart = d;
            hoistLocalsFor(userStart);
            std::string text = function(userStart ? userStart->body : none, [&] {
                bool any = false;
                for (auto& f : fields_) {
                    if (f.property)
                        continue;
                    const Expr* init = f.stmt->expr.get();
                    bool zero = (init->kind == ExprKind::Number && init->number == 0) || init->kind == ExprKind::False ||
                                init->kind == ExprKind::None || (init->kind == ExprKind::String && init->text.empty());
                    if (zero)
                        continue; // the data starts zeroed
                    commentCursor_ = std::max(commentCursor_, f.stmt->line);
                    Expr target(ExprKind::Name, f.stmt->line);
                    target.text = f.raw;
                    assignTo(&target, "", init, f.stmt->line);
                    any = true;
                }
                for (auto* s : loose_) {
                    commentCursor_ = std::max(commentCursor_, s->line - 1);
                    stmt(*s);
                    any = true;
                }
                if (any && userStart && !userStart->body.empty())
                    *body_ += "\n";
                if (userStart) {
                    commentCursor_ = commentStart_[userStart];
                    flush(userStart->line);
                    commentCursor_ = userStart->line;
                }
            });
            if (!trim(text).empty())
                start = text;
        }

        // Struct.
        std::string structText;
        if (!fields_.empty()) {
            structText += "typedef struct {\n";
            for (auto& f : fields_) {
                std::string decl = "    " + fieldDecl(f);
                auto t = trailComments_.find(f.stmt->line);
                if (t != trailComments_.end())
                    decl += " /* " + commentSafe(t->second) + " */";
                else if (f.property)
                    decl += " /* set in the Inspector */";
                structText += decl + "\n";
            }
            structText += "} " + cls_ + ";\n\n";
        }

        // The script's own functions.
        std::vector<std::string> messageTargets;
        for (auto* def : defs_) {
            std::string name = symbolName(def->name);
            if (!userFns_.count(name))
                continue;
            locals_.clear();
            rename_.clear();
            paramTypes_.clear();
            inUserFn_ = true;
            returnType_ = fnReturn_[name];
            std::string sig = (returnType_ == CT::Unknown ? std::string("void") : cType(returnType_)) + " " + fnName(name) + "(AvenEntity self, " +
                              (fields_.empty() ? std::string("void") : cls_) + "* my";
            size_t firstDefault = def->params.size() - def->defaults.size();
            for (size_t i = 0; i < def->params.size(); ++i) {
                std::string raw = symbolName(def->params[i]);
                const Expr* fallback = i >= firstDefault ? def->defaults[i - firstDefault].get() : nullptr;
                CT t = paramType(raw, fallback);
                paramTypes_[raw] = t;
                std::string c = cName(raw);
                rename_[raw] = c;
                sig += ", " + cType(t) + " " + c;
            }
            sig += ")";
            commentCursor_ = commentStart_[def];
            std::string lead;
            {
                std::string tmp;
                body_ = &tmp;
                depth_ = 0;
                flush(def->line);
                lead = tmp;
            }
            commentCursor_ = def->line;
            hoistLocals(def->body);
            std::string text = function(def->body);
            protos += "static " + sig + ";\n";
            if (returnType_ != CT::Unknown)
                text += "    return 0;\n";
            fns += lead + "static " + sig + " {\n" + text + "}\n\n";
            inUserFn_ = false;
            if (def->params.size() <= 1)
                messageTargets.push_back(name);
        }

        // Events.
        std::vector<std::pair<std::string, std::string>> wired; // callback field -> function
        if (!start.empty()) {
            fns += "static void " + prefix_ + "_start(AvenEntity self, void* data) {\n" + start + "}\n\n";
            wired.push_back({"on_start", prefix_ + "_start"});
        }
        const Stmt* userMessage = nullptr;
        for (auto* d : defs_) {
            std::string name = symbolName(d->name);
            const EventInfo* ev = eventInfo(name);
            if (!ev || name == "on_start")
                continue;
            if (name == "on_message") {
                userMessage = d;
                continue;
            }
            locals_.clear();
            rename_.clear();
            paramTypes_.clear();
            std::string params = eventParams(*ev, d);
            std::string lead;
            {
                std::string tmp;
                body_ = &tmp;
                depth_ = 0;
                commentCursor_ = commentStart_[d];
                flush(d->line);
                lead = tmp;
            }
            commentCursor_ = d->line;
            hoistLocals(d->body);
            std::string text = function(d->body);
            std::string fn = prefix_ + "_" + name.substr(3);
            fns += lead + "static void " + fn + "(" + params + ") {\n" + text + "}\n\n";
            wired.push_back({ev->field, fn});
        }
        // Messages: the script's on_message, plus functions other scripts call with send().
        if (userMessage || !messageTargets.empty()) {
            locals_.clear();
            rename_.clear();
            paramTypes_.clear();
            std::string params = eventParams(*eventInfo("on_message"), userMessage);
            std::string messageName = userMessage && !userMessage->params.empty() ? rename_[symbolName(userMessage->params[0])] : "message";
            std::string valueName = userMessage && userMessage->params.size() > 1 ? rename_[symbolName(userMessage->params[1])] : "value";
            std::string lead;
            if (userMessage) {
                std::string tmp;
                body_ = &tmp;
                depth_ = 0;
                commentCursor_ = commentStart_[userMessage];
                flush(userMessage->line);
                lead = tmp;
                commentCursor_ = userMessage->line;
                hoistLocals(userMessage->body);
            }
            std::vector<StmtPtr> none;
            std::string text = function(userMessage ? userMessage->body : none, [&] {
                if (messageTargets.empty())
                    return;
                emit("// Other scripts call this script's functions with send(\"name\", value).");
                for (auto& t : messageTargets) {
                    const Stmt* def = userFns_[t];
                    std::string arg;
                    if (!def->params.empty()) {
                        size_t firstDefault = def->params.size() - def->defaults.size();
                        CT pt = paramType(symbolName(def->params[0]), firstDefault == 0 && !def->defaults.empty() ? def->defaults[0].get() : nullptr);
                        arg = pt == CT::Ent ? ", 0" : pt == CT::Str ? ", \"\"" : ", " + valueName;
                    }
                    emit("if (strcmp(" + messageName + ", " + cQuote(t) + ") == 0)");
                    emit("    " + fnName(t) + "(self, " + (fields_.empty() ? std::string("NULL") : "my") + arg + ");");
                    usesMy_ = usesMy_ || !fields_.empty();
                }
                if (userMessage && !userMessage->body.empty())
                    *body_ += "\n";
            });
            fns += lead + "static void " + prefix_ + "_message(" + params + ") {\n" + text + "}\n\n";
            wired.push_back({"on_message", prefix_ + "_message"});
        }

        // Small helpers the translation needed.
        std::string help;
        if (helpers_.count("deg"))
            help += "#define AVEN_PI 3.14159265358979323846\n#define AVEN_DEG (AVEN_PI / 180.0) /* EasyScript's angles are in degrees */\n\n";
        if (helpers_.count("distance") || helpers_.count("direction") || helpers_.count("move_toward"))
            help += "static double distance_between(AvenEntity a, AvenEntity b) {\n"
                    "    return hypot(aven_get(b, \"x\") - aven_get(a, \"x\"), aven_get(b, \"y\") - aven_get(a, \"y\"));\n}\n\n";
        if (helpers_.count("direction"))
            help += "static double direction_x_to(AvenEntity a, AvenEntity b) {\n"
                    "    double d = distance_between(a, b);\n    return d > 0 ? (aven_get(b, \"x\") - aven_get(a, \"x\")) / d : 0;\n}\n\n"
                    "static double direction_y_to(AvenEntity a, AvenEntity b) {\n"
                    "    double d = distance_between(a, b);\n    return d > 0 ? (aven_get(b, \"y\") - aven_get(a, \"y\")) / d : 0;\n}\n\n";
        if (helpers_.count("move_toward"))
            help += "/* Moves `a` up to `step` units toward `b`. */\n"
                    "static void move_toward_object(AvenEntity a, AvenEntity b, double step) {\n"
                    "    double d = distance_between(a, b);\n    if (d <= step || d <= 0) {\n"
                    "        aven_set(a, \"x\", aven_get(b, \"x\"));\n        aven_set(a, \"y\", aven_get(b, \"y\"));\n"
                    "        return;\n    }\n"
                    "    aven_set(a, \"x\", aven_get(a, \"x\") + (aven_get(b, \"x\") - aven_get(a, \"x\")) / d * step);\n"
                    "    aven_set(a, \"y\", aven_get(a, \"y\") + (aven_get(b, \"y\") - aven_get(a, \"y\")) / d * step);\n}\n\n";
        if (helpers_.count("look_at"))
            help += "/* Turns `a` so its right side points at `b`. */\n"
                    "static void turn_toward(AvenEntity a, AvenEntity b) {\n"
                    "    aven_set(a, \"angle\", atan2(aven_get(b, \"y\") - aven_get(a, \"y\"), aven_get(b, \"x\") - aven_get(a, \"x\")) "
                    "* 180.0 / 3.14159265358979323846);\n}\n\n";
        if (helpers_.count("axis"))
            help += "/* -1, 0 or 1 from the arrow keys or WASD, like EasyScript's axis(). */\n"
                    "static double axis_value(const char* which) {\n"
                    "    if (strcmp(which, \"vertical\") == 0)\n"
                    "        return (aven_key_down(\"up\") || aven_key_down(\"w\")) - (aven_key_down(\"down\") || aven_key_down(\"s\"));\n"
                    "    return (aven_key_down(\"right\") || aven_key_down(\"d\")) - (aven_key_down(\"left\") || aven_key_down(\"a\"));\n}\n\n";
        if (helpers_.count("sign"))
            help += "static double sign_of(double v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }\n\n";

        o += structText + help;
        if (!protos.empty())
            o += protos + "\n";
        o += fns;

        // Registration.
        o += "static void setup(AvenModule* module) {\n";
        o += "    AvenBehavior* b = aven_behavior(module, " + cQuote(cls_) + ", " + (fields_.empty() ? std::string("0") : "sizeof(" + cls_ + ")") + ");\n";
        for (auto& f : fields_) {
            if (!f.property)
                continue;
            const Expr* init = f.stmt->expr.get();
            double v = init->kind == ExprKind::Number ? init->number
                       : init->kind == ExprKind::True  ? 1
                       : init->kind == ExprKind::Unary ? -init->a->number
                                                       : 0;
            auto t = trailComments_.find(f.stmt->line);
            std::string tip = t == trailComments_.end() ? "NULL" : cQuote(t->second);
            o += std::string("    ") + (f.type == CT::Bool ? "aven_flag" : "aven_number") + "(b, " + cQuote(f.raw) + ", offsetof(" + cls_ +
                 ", " + f.c + "), " + num(v) + ", " + tip + ");\n";
        }
        for (auto& [fieldName, fn] : wired)
            o += "    b->" + fieldName + " = " + fn + ";\n";
        o += "}\n\nAVEN_MODULE(setup)\n";

        if (!fields_.empty())
            note("Each object keeps its own copy of the " + cls_ + " struct; the Inspector sets the numbers registered with "
                 "aven_number().");
    }

    void hoistLocalsFor(const Stmt* def) {
        if (def)
            hoistLocals(def->body);
        for (auto* st : loose_)
            hoistStmt(*st);
    }
};

} // namespace

Translation translateToAvenC(std::string_view source, const TranslateOptions& options) {
    return CTranslator(source, options).run();
}

} // namespace aven::script
