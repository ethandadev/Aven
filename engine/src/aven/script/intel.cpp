#include "aven/script/intel.h"

#include "aven/assets/assets.h"
#include "aven/platform/input.h"
#include "aven/runtime/game.h"
#include "aven/runtime/script_system.h"
#include "aven/scene/reflection.h"
#include "aven/script/ast.h"
#include "aven/script/errors.h"
#include "aven/script/stdlib.h"
#include "aven/script/vm.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <unordered_set>

namespace aven::script {

namespace {

bool isWord(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

std::string lower(std::string s) {
    for (char& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

int indentOf(const std::string& s) {
    int n = 0;
    while (n < static_cast<int>(s.size()) && s[static_cast<size_t>(n)] == ' ')
        ++n;
    return n;
}

bool blankOrComment(const std::string& s, CodeKind kind) {
    size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return true;
    return kind == CodeKind::C ? s.compare(first, 2, "//") == 0 : s[first] == '#';
}

// The word around a column: [from, to).
bool wordAt(const std::string& s, int col, int& from, int& to) {
    from = to = std::clamp(col, 0, static_cast<int>(s.size()));
    while (from > 0 && isWord(s[static_cast<size_t>(from - 1)]))
        --from;
    while (to < static_cast<int>(s.size()) && isWord(s[static_cast<size_t>(to)]))
        ++to;
    return to > from && !std::isdigit(static_cast<unsigned char>(s[static_cast<size_t>(from)]));
}

// Where the text before `col` is: in code, in a string (and where it opened), or in a comment.
struct LineState {
    bool comment = false;
    bool string = false;
    int quote = -1; // column of the opening quote
};

LineState stateAt(const std::string& s, int col, CodeKind kind) {
    LineState st;
    char q = 0;
    for (int i = 0; i < col && i < static_cast<int>(s.size()); ++i) {
        char c = s[static_cast<size_t>(i)];
        if (q) {
            if (c == '\\')
                ++i;
            else if (c == q)
                q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
            st.quote = i;
        } else if ((kind == CodeKind::EasyScript && c == '#') ||
                   (kind == CodeKind::C && c == '/' && i + 1 < static_cast<int>(s.size()) && s[static_cast<size_t>(i + 1)] == '/')) {
            st.comment = true;
            return st;
        }
    }
    st.string = q != 0;
    return st;
}

// The parameters written between the parentheses of a signature like "move(dx, dy)".
// Each is returned with its byte range in `sig`.
std::vector<std::pair<int, int>> paramRanges(const std::string& sig) {
    std::vector<std::pair<int, int>> out;
    size_t open = sig.find('(');
    if (open == std::string::npos)
        return out;
    int depth = 0;
    int start = static_cast<int>(open) + 1;
    char q = 0;
    for (size_t i = open + 1; i < sig.size(); ++i) {
        char c = sig[i];
        if (q) {
            if (c == q)
                q = 0;
            continue;
        }
        if (c == '"' || c == '\'')
            q = c;
        else if (c == '(' || c == '[' || c == '{')
            ++depth;
        else if ((c == ')' || c == ']' || c == '}') && depth > 0)
            --depth;
        else if ((c == ',' || c == ')') && depth == 0) {
            int from = start, to = static_cast<int>(i);
            while (from < to && sig[static_cast<size_t>(from)] == ' ')
                ++from;
            if (to > from)
                out.emplace_back(from, to);
            start = static_cast<int>(i) + 1;
            if (c == ')')
                break;
        }
    }
    return out;
}

// "move(dx, dy) or self.move(dx, dy, dz)" -> the alternatives.
std::vector<std::string> variants(const std::string& sig) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t at = sig.find(" or ", start);
        std::string part = sig.substr(start, at == std::string::npos ? std::string::npos : at - start);
        if (part.find('(') != std::string::npos)
            out.push_back(part);
        if (at == std::string::npos)
            break;
        start = at + 4;
    }
    if (out.empty())
        out.push_back(sig);
    return out;
}

// What to insert for a function: "name($0)", or "name()" when it takes nothing.
std::string callInsert(const std::string& name, const std::string& signature) {
    auto v = variants(signature);
    bool takesNothing = v.size() == 1 && paramRanges(v.front()).empty() && signature.find("(") != std::string::npos;
    return takesNothing ? name + "()" : name + "($0)";
}

// ---------------------------------------------------------------- what's in this file

struct FileFunction {
    std::string name;
    std::vector<std::string> params;
    int line = 0, end = 0; // lines of the def and the last line of its body
    std::string doc;       // comment lines just above
    std::string signature;
};

struct FileVar {
    std::string name, value, doc;
    int line = 0;
};

struct FileInfo {
    std::vector<FileFunction> functions;
    std::vector<FileVar> vars; // top-level
    std::set<std::string> selfAssigned; // self.X = ... anywhere
};

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

// The comment at the end of a line ("speed = 5  # how fast"), outside strings.
std::string trailingComment(const std::string& s) {
    char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (q) {
            if (c == '\\')
                ++i;
            else if (c == q)
                q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '#') {
            return trim(s.substr(i + 1));
        }
    }
    return "";
}

std::string commentsAbove(const std::vector<std::string>& lines, int line) {
    std::string doc;
    for (int l = line - 1; l >= 0; --l) {
        std::string t = trim(lines[static_cast<size_t>(l)]);
        if (t.empty() || t[0] != '#')
            break;
        doc = trim(t.substr(1)) + (doc.empty() ? "" : " " + doc);
    }
    return doc;
}

// Reads assignments of a line: "a = 1", "a, b = ...", "a += 1" (not "a == 1").
std::vector<std::string> assignedNames(const std::string& t) {
    std::vector<std::string> out;
    size_t eq = std::string::npos;
    char q = 0;
    int depth = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (q) {
            if (c == q)
                q = 0;
            continue;
        }
        if (c == '"' || c == '\'') {
            q = c;
        } else if (c == '(' || c == '[' || c == '{') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}') {
            --depth;
        } else if (c == '=' && depth == 0) {
            char prev = i > 0 ? t[i - 1] : 0, next = i + 1 < t.size() ? t[i + 1] : 0;
            if (next == '=' || prev == '=' || prev == '!' || prev == '<' || prev == '>')
                continue;
            eq = (prev == '+' || prev == '-' || prev == '*' || prev == '/' || prev == '%') ? i - 1 : i;
            break;
        }
    }
    if (eq == std::string::npos)
        return out;
    std::string lhs = t.substr(0, eq);
    size_t start = 0;
    while (start <= lhs.size()) {
        size_t comma = lhs.find(',', start);
        std::string part = trim(lhs.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        bool plain = !part.empty() && !std::isdigit(static_cast<unsigned char>(part[0])) &&
                     std::all_of(part.begin(), part.end(), isWord);
        if (plain)
            out.push_back(part);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

// "for a, b in ..." -> a, b
std::vector<std::string> loopNames(const std::string& t) {
    std::vector<std::string> out;
    if (t.rfind("for ", 0) != 0)
        return out;
    size_t in = t.find(" in ");
    if (in == std::string::npos)
        return out;
    std::string names = t.substr(4, in - 4);
    size_t start = 0;
    while (true) {
        size_t comma = names.find(',', start);
        std::string n = trim(names.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!n.empty())
            out.push_back(n);
        if (comma == std::string::npos)
            break;
        start = comma + 1;
    }
    return out;
}

FileInfo analyze(const std::vector<std::string>& lines) {
    FileInfo info;
    for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
        const std::string& s = lines[static_cast<size_t>(l)];
        std::string t = trim(s);
        for (size_t at = s.find("self."); at != std::string::npos; at = s.find("self.", at + 5)) {
            size_t b = at + 5, e = b;
            while (e < s.size() && isWord(s[e]))
                ++e;
            std::string rest = trim(s.substr(e));
            if (e > b && !rest.empty() && rest[0] == '=' && (rest.size() < 2 || rest[1] != '='))
                info.selfAssigned.insert(s.substr(b, e - b));
        }
        if (t.rfind("def ", 0) == 0) {
            FileFunction f;
            size_t open = t.find('(');
            f.name = trim(t.substr(4, open == std::string::npos ? std::string::npos : open - 4));
            if (open != std::string::npos) {
                size_t close = t.find(')', open);
                std::string ps = t.substr(open + 1, close == std::string::npos ? std::string::npos : close - open - 1);
                size_t start = 0;
                while (true) {
                    size_t comma = ps.find(',', start);
                    std::string p = trim(ps.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
                    size_t eq = p.find('=');
                    if (eq != std::string::npos)
                        p = trim(p.substr(0, eq));
                    if (!p.empty())
                        f.params.push_back(p);
                    if (comma == std::string::npos)
                        break;
                    start = comma + 1;
                }
                f.signature = f.name + "(" + (close == std::string::npos ? ps : t.substr(open + 1, close - open - 1)) + ")";
            } else {
                f.signature = f.name + "()";
            }
            f.line = l;
            f.doc = commentsAbove(lines, l);
            int base = indentOf(s);
            f.end = l;
            for (int k = l + 1; k < static_cast<int>(lines.size()); ++k) {
                const std::string& b = lines[static_cast<size_t>(k)];
                if (blankOrComment(b, CodeKind::EasyScript))
                    continue;
                if (indentOf(b) <= base)
                    break;
                f.end = k;
            }
            if (!f.name.empty() && base == 0)
                info.functions.push_back(std::move(f));
            continue;
        }
        if (indentOf(s) == 0 && !t.empty() && t[0] != '#') {
            for (auto& n : assignedNames(t)) {
                bool known = std::any_of(info.vars.begin(), info.vars.end(), [&](const FileVar& v) { return v.name == n; });
                if (known)
                    continue;
                FileVar v;
                v.name = n;
                v.line = l;
                size_t eq = t.find('=');
                std::string value = trim(t.substr(eq + 1));
                size_t hash = value.find(" #");
                v.value = trim(hash == std::string::npos ? value : value.substr(0, hash));
                v.doc = trailingComment(s);
                if (v.doc.empty())
                    v.doc = commentsAbove(lines, l);
                info.vars.push_back(std::move(v));
            }
            for (auto& n : loopNames(t))
                info.vars.push_back({n, "", "", l});
        }
    }
    return info;
}

const FileFunction* functionAt(const FileInfo& info, int line) {
    for (auto& f : info.functions)
        if (line > f.line && line <= std::max(f.end, f.line + 1))
            return &f;
    return nullptr;
}

// Names local to a function: its parameters, and what its body assigns or loops over.
std::vector<std::string> localsOf(const std::vector<std::string>& lines, const FileFunction& f, int uptoLine) {
    std::vector<std::string> out = f.params;
    for (int l = f.line + 1; l <= std::min(f.end, uptoLine) && l < static_cast<int>(lines.size()); ++l) {
        std::string t = trim(lines[static_cast<size_t>(l)]);
        for (auto& n : assignedNames(t))
            out.push_back(n);
        for (auto& n : loopNames(t))
            out.push_back(n);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// What kind of value a name holds, judging by how it was last set before `line`.
std::string guessType(const std::vector<std::string>& lines, const std::string& name, int line, const FileFunction* fn) {
    if (name == "other" || name == "obj" || name == "target" || name == "enemy" || name == "player")
        return "object";
    for (int l = std::min(line, static_cast<int>(lines.size()) - 1); l >= 0; --l) {
        std::string t = trim(lines[static_cast<size_t>(l)]);
        for (auto& n : loopNames(t))
            if (n == name) {
                if (t.find("find_all(") != std::string::npos || t.find(".children") != std::string::npos)
                    return "object";
                if (t.find("range(") != std::string::npos)
                    return "number";
                return "";
            }
        if (t.rfind(name, 0) != 0)
            continue;
        std::string rest = trim(t.substr(name.size()));
        if (rest.empty() || rest[0] != '=' || (rest.size() > 1 && rest[1] == '='))
            continue;
        std::string v = trim(rest.substr(1));
        auto starts = [&](const char* p) { return v.rfind(p, 0) == 0; };
        if (starts("[") || starts("list(") || starts("sorted(") || starts("find_all(") || starts("reversed("))
            return starts("find_all(") ? "objects" : "list";
        if (starts("{") || starts("dict("))
            return "dict";
        if (starts("\"") || starts("'") || starts("f\"") || starts("f'") || starts("str("))
            return "text";
        if (starts("vec(") || starts("rgb(") || starts("hsv(") || starts("color(") || starts("mouse_position(") ||
            starts("direction("))
            return "vec";
        if (starts("find(") || starts("spawn(") || starts("create_") || starts("camera(") || v.find(".clone(") != std::string::npos ||
            v.find(".find_child(") != std::string::npos || v.find(".parent") != std::string::npos)
            return "object";
        if (starts("self.position") || starts("self.world_position") || starts("self.velocity") || starts("other.position"))
            return "vec";
        return "";
    }
    if (fn)
        for (auto& p : fn->params)
            if (p == name && (name == "other" || name == "obj"))
                return "object";
    return "";
}

void add(std::vector<Suggestion>& out, std::set<std::string>& seen, const std::string& typed, Suggestion s, int bonus) {
    if (!seen.insert(s.label).second)
        return;
    int m = typed.empty() ? 1 : matchScore(s.label, typed);
    if (m < 0)
        return;
    if (!typed.empty() && s.label == typed)
        m += 5000; // typed in full: it's the one (and the editor closes the list for it)
    s.score = m + bonus;
    if (s.insert.empty())
        s.insert = s.label;
    out.push_back(std::move(s));
}

size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j)
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

// A near miss: one letter off for short words, two for long ones, and starting the same.
std::string typoOf(const std::string& word, const std::vector<std::string>& candidates) {
    std::string best;
    size_t bestDistance = word.size() <= 5 ? 1 : 2;
    for (auto& c : candidates) {
        if (c.empty() || c[0] != word[0] || c == word)
            continue;
        size_t d = editDistance(word, c);
        if (d <= bestDistance) {
            best = c;
            bestDistance = d;
        }
    }
    return best;
}

const char* kEasyKeywords[] = {"if", "elif", "else", "for", "in", "while", "def", "return", "break", "continue", "pass",
                               "and", "or", "not", "True", "False", "None", "global"};

const char* kCKeywords[] = {"if", "else", "for", "while", "do", "return", "break", "continue", "switch", "case", "default",
                            "struct", "typedef", "static", "const", "void", "int", "float", "double", "char", "unsigned",
                            "sizeof", "enum", "NULL", "offsetof", "bool", "true", "false"};

} // namespace

// ---------------------------------------------------------------- matching

int matchScore(const std::string& candidate, const std::string& typed) {
    if (typed.empty())
        return 1;
    if (candidate.compare(0, typed.size(), typed) == 0)
        return 1000 - static_cast<int>(candidate.size() - typed.size());
    std::string c = lower(candidate), t = lower(typed);
    if (c.compare(0, t.size(), t) == 0)
        return 900 - static_cast<int>(candidate.size() - typed.size());
    // First letters of the words: kp -> key_pressed, pa -> play_animation
    std::string initials;
    for (size_t i = 0; i < c.size(); ++i)
        if (i == 0 || c[i - 1] == '_' || c[i - 1] == '.')
            if (c[i] != '_')
                initials += c[i];
    if (initials.compare(0, t.size(), t) == 0)
        return 700 - static_cast<int>(candidate.size());
    // A word inside it starts with what was typed: "sound" finds play_sound
    for (size_t i = 1; i < c.size(); ++i)
        if ((c[i - 1] == '_' || c[i - 1] == '/' || c[i - 1] == '.') && c.compare(i, t.size(), t) == 0)
            return 600 - static_cast<int>(candidate.size());
    if (t.size() >= 3) {
        size_t j = 0;
        for (size_t i = 0; i < c.size() && j < t.size(); ++i)
            if (c[i] == t[j])
                ++j;
        if (j == t.size() && t[0] == c[0])
            return 300 - static_cast<int>(candidate.size());
    }
    return -1;
}

// ---------------------------------------------------------------- the index

const ProjectIndex::Function* ProjectIndex::global(const std::string& name) const {
    for (auto& f : globals)
        if (f.name == name)
            return &f;
    return nullptr;
}

std::string ProjectIndex::doc(const std::string& key) const {
    auto it = docs.find(key);
    return it == docs.end() ? "" : it->second;
}

void ProjectIndex::fillFromEngine() {
    // A throwaway game knows every function scripts can call, with its signature and limits.
    Assets assets;
    Input input;
    Game probe(assets, input);
    VM& vm = probe.scripts().vm();
    globals.clear();
    globalValues.clear();
    for (auto& name : vm.globalNames()) {
        const Value* v = vm.global(intern(name));
        if (v && v->type() == Type::NativeFunction) {
            auto* nf = v->as<NativeFunctionObj>();
            globals.push_back({name, nf->signature.empty() ? name + "()" : nf->signature, nf->minArgs, nf->maxArgs});
        } else {
            globalValues.push_back(name);
        }
    }
    selfProperties = ScriptSystem::propertyNames();
    selfMethods.clear();
    auto names = ScriptSystem::methodNames();
    auto sigs = ScriptSystem::methodSignatures();
    auto limits = ScriptSystem::methodLimits();
    for (size_t i = 0; i < names.size(); ++i)
        selfMethods.push_back({names[i], sigs[i], limits[i].minArgs, limits[i].maxArgs});
    typeMethods.clear();
    for (auto [type, key] : {std::pair{Type::List, "list"}, std::pair{Type::String, "text"}, std::pair{Type::Dict, "dict"},
                             std::pair{Type::Vec, "vec"}})
        for (auto* m : vm.methodsOf(type))
            typeMethods[key].push_back({m->name, m->signature, m->minArgs, m->maxArgs});
    componentAliases = ScriptSystem::componentAliases();
    components.clear();
    for (auto& c : ComponentRegistry::all()) {
        auto& fields = components[c.name];
        for (auto& f : c.fields) {
            if (f.options.runtime)
                continue;
            const char* type = "value";
            switch (f.type) {
            case FieldType::Bool: type = "True/False"; break;
            case FieldType::Int: type = "whole number"; break;
            case FieldType::Float: type = "number"; break;
            case FieldType::Vec2:
            case FieldType::Vec3: type = "vec"; break;
            case FieldType::Color: type = "color"; break;
            case FieldType::String: type = "text"; break;
            case FieldType::Asset: type = "file"; break;
            case FieldType::Enum: type = "choice"; break;
            case FieldType::EntityRef: type = "object"; break;
            }
            fields.emplace_back(f.name, type);
        }
    }
    events = {{"on_start", "on_start()"},           {"on_update", "on_update(dt)"},
              {"on_fixed_update", "on_fixed_update(dt)"}, {"on_collide", "on_collide(other)"},
              {"on_collide_end", "on_collide_end(other)"}, {"on_trigger", "on_trigger(other)"},
              {"on_trigger_exit", "on_trigger_exit(other)"}, {"on_click", "on_click()"},
              {"on_key_pressed", "on_key_pressed(key)"}, {"on_message", "on_message(message, data)"},
              {"on_destroy", "on_destroy()"}};
    keyNames = Input::allNames();
    for (auto& a : Input::defaultActions())
        keyNames.push_back(a.name);
    colors = colorNames();
    shapes.clear();
    if (const ComponentInfo* sr = ComponentRegistry::find("SpriteRenderer"))
        if (const FieldInfo* f = sr->findField("shape"))
            for (auto& n : f->options.enumNames)
                shapes.push_back(toSnakeCase(n));
    easings = {"linear", "ease_in", "ease_out", "ease_in_out", "bounce", "elastic", "back"};
    tweenProperties = {"x", "y", "z", "angle", "scale", "scale_x", "scale_y", "alpha", "width", "height",
                       "rotation_x", "rotation_y", "rotation_z", "world_x", "world_y", "frame"};
    cBehaviorFields = {"on_start", "on_update", "on_fixed_update", "on_collide", "on_collide_end", "on_trigger",
                       "on_trigger_exit", "on_click", "on_key_pressed", "on_message", "on_destroy"};
}

void ProjectIndex::readCApi(const std::string& header) {
    cFunctions.clear();
    size_t pos = 0;
    while ((pos = header.find("static inline ", pos)) != std::string::npos) {
        size_t end = header.find(')', pos);
        size_t nameAt = header.find("aven_", pos);
        if (end == std::string::npos || nameAt == std::string::npos || nameAt > end) {
            pos += 14;
            continue;
        }
        size_t nameEnd = nameAt;
        while (nameEnd < header.size() && isWord(header[nameEnd]))
            ++nameEnd;
        std::string ret = trim(header.substr(pos + 14, nameAt - pos - 14));
        std::string params = header.substr(nameEnd, end - nameEnd + 1);
        std::string collapsed;
        for (char c : params)
            if (c != '\n' && !(c == ' ' && !collapsed.empty() && collapsed.back() == ' '))
                collapsed += c;
        std::string name = header.substr(nameAt, nameEnd - nameAt);
        cFunctions.push_back({name, ret + " " + name + collapsed, 0, -1});
        pos = end;
    }
}

void ProjectIndex::scanScript(const std::string& source) {
    auto grab = [&](const std::string& before, std::vector<std::string>& into) {
        for (size_t at = source.find(before); at != std::string::npos; at = source.find(before, at + 1)) {
            size_t b = at + before.size();
            size_t e = source.find('"', b);
            if (e == std::string::npos || source.find('\n', b) < e)
                continue;
            std::string v = source.substr(b, e - b);
            if (!v.empty() && std::find(into.begin(), into.end(), v) == into.end())
                into.push_back(v);
        }
    };
    grab("broadcast(\"", messages);
    grab("message == \"", messages);
    grab("save_data(\"", savedKeys);
    grab("load_data(\"", savedKeys);
    grab("get_game(\"", gameValues);
    for (size_t at = source.find("game."); at != std::string::npos; at = source.find("game.", at + 1)) {
        if (at > 0 && (isWord(source[at - 1]) || source[at - 1] == '.'))
            continue;
        size_t b = at + 5, e = b;
        while (e < source.size() && isWord(source[e]))
            ++e;
        std::string v = source.substr(b, e - b);
        if (!v.empty() && std::find(gameValues.begin(), gameValues.end(), v) == gameValues.end())
            gameValues.push_back(v);
    }
    for (size_t at = source.find("def "); at != std::string::npos; at = source.find("def ", at + 1)) {
        if (at > 0 && source[at - 1] != '\n' && source[at - 1] != ' ')
            continue;
        size_t b = at + 4, e = b;
        while (e < source.size() && isWord(source[e]))
            ++e;
        std::string v = source.substr(b, e - b);
        if (!v.empty() && v.rfind("on_", 0) != 0 &&
            std::find(scriptFunctions.begin(), scriptFunctions.end(), v) == scriptFunctions.end())
            scriptFunctions.push_back(v);
    }
}

// ---------------------------------------------------------------- suggestions

namespace {

// The call a string argument belongs to: `play_sound("|` -> ("play_sound", 0).
bool callForString(const std::string& s, int quote, std::string& callee, int& arg, std::string& receiver) {
    int depth = 0;
    arg = 0;
    for (int i = quote - 1; i >= 0; --i) {
        char c = s[static_cast<size_t>(i)];
        if (c == ')' || c == ']')
            ++depth;
        else if (c == '[' && depth > 0)
            --depth;
        else if (c == '(') {
            if (depth > 0) {
                --depth;
                continue;
            }
            int e = i;
            while (e > 0 && s[static_cast<size_t>(e - 1)] == ' ')
                --e;
            int b = e;
            while (b > 0 && isWord(s[static_cast<size_t>(b - 1)]))
                --b;
            callee = s.substr(static_cast<size_t>(b), static_cast<size_t>(e - b));
            receiver.clear();
            if (b > 0 && s[static_cast<size_t>(b - 1)] == '.') {
                int r = b - 1;
                while (r > 0 && isWord(s[static_cast<size_t>(r - 1)]))
                    --r;
                receiver = s.substr(static_cast<size_t>(r), static_cast<size_t>(b - 1 - r));
            }
            return !callee.empty();
        } else if (c == ',' && depth == 0) {
            ++arg;
        } else if (c == '"' || c == '\'') {
            // skip a whole earlier string argument
            char q = c;
            --i;
            while (i >= 0 && s[static_cast<size_t>(i)] != q)
                --i;
        }
    }
    return false;
}

std::vector<std::string> filesWith(const ProjectIndex& ix, std::initializer_list<const char*> exts) {
    std::vector<std::string> out;
    for (auto& f : ix.files) {
        std::string l = lower(f);
        for (const char* e : exts)
            if (l.size() > std::string(e).size() && l.compare(l.size() - std::string(e).size(), std::string::npos, e) == 0)
                out.push_back(f);
    }
    return out;
}

} // namespace

std::vector<Suggestion> CodeIntel::suggest(const std::vector<std::string>& lines, int line, int col, int& from,
                                           bool manual) const {
    std::vector<Suggestion> out;
    std::set<std::string> seen;
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return out;
    const std::string& s = lines[static_cast<size_t>(line)];
    col = std::clamp(col, 0, static_cast<int>(s.size()));
    LineState st = stateAt(s, col, kind_);
    if (st.comment)
        return out;
    const ProjectIndex& ix = index_;
    bool easy = kind_ == CodeKind::EasyScript;

    // ---- inside a string: file names, keys, tags... depending on the call
    if (st.string) {
        from = st.quote + 1;
        std::string typed = s.substr(static_cast<size_t>(from), static_cast<size_t>(col - from));
        std::string callee, receiver;
        int arg = 0;
        std::vector<std::string> values;
        SuggestionKind kind = SuggestionKind::Text;
        std::string what;
        std::string before = trim(s.substr(0, static_cast<size_t>(st.quote)));
        if (callForString(s, st.quote, callee, arg, receiver)) {
            std::string c = callee.rfind("aven_", 0) == 0 ? callee.substr(5) : callee;
            if ((c == "key_down" || c == "key_pressed" || c == "key_released") && arg == 0)
                values = ix.keyNames, what = "key";
            else if ((c == "mouse_down" || c == "mouse_pressed" || c == "mouse_released") && arg == 0)
                values = {"left", "right", "middle"}, what = "mouse button";
            else if (c == "axis" && arg == 0)
                values = {"horizontal", "vertical", "look_x", "look_y"}, what = "axis";
            else if ((c == "play_sound" || c == "play_music") && arg == 0)
                values = filesWith(ix, {".wav", ".ogg", ".mp3", ".flac"}), kind = SuggestionKind::File, what = "sound";
            else if (c == "spawn" && arg == 0)
                values = filesWith(ix, {".prefab"}), kind = SuggestionKind::File, what = "prefab";
            else if (c == "load_scene" && arg == 0)
                values = filesWith(ix, {".scene"}), kind = SuggestionKind::File, what = "scene";
            else if ((c == "get_component" || c == "add_component" || c == "has_component" || c == "remove_component") && arg == 0) {
                for (auto& [name, fields] : ix.components)
                    values.push_back(name);
                kind = SuggestionKind::Component, what = "component";
            } else if (c == "find" && arg == 0)
                values = ix.objectNames, what = "object";
            else if ((c == "find_all" || c == "count" || c == "is_touching") && arg == 0)
                values = ix.tags, what = "tag";
            else if (c == "broadcast" && arg == 0)
                values = ix.messages, what = "message";
            else if (c == "send" && arg == 0) {
                values = ix.scriptFunctions;
                what = "function in the other object's script";
            } else if (c == "tween" && ((receiver.empty() && arg == 1) || (!receiver.empty() && arg == 0)))
                values = ix.tweenProperties, what = "property";
            else if (c == "tween" && ((receiver.empty() && arg == 4) || (!receiver.empty() && arg == 3)))
                values = ix.easings, what = "easing";
            else if (c == "color" && arg == 0)
                values = ix.colors, what = "color";
            else if (c == "create_sprite" && arg == 0) {
                values = ix.shapes;
                for (auto& f : filesWith(ix, {".png", ".jpg", ".jpeg"}))
                    values.push_back(f);
                what = "shape or picture";
            } else if ((c == "save_data" || c == "load_data" || c == "has_data" || c == "delete_data") && arg == 0)
                values = ix.savedKeys, what = "saved value";
            else if (c == "get_game" && arg == 0)
                values = ix.gameValues, what = "game value";
            else if ((c == "get" || c == "get_text" || c == "set" || c == "set_text") && callee.rfind("aven_", 0) == 0 && arg == 1)
                values = ix.selfProperties, what = "property";
            else if (c == "game_get" || c == "game_set")
                values = ix.gameValues, what = "game value";
        } else if (before.size() > 1 && before.back() == '=' && before[before.size() - 2] != '=') {
            // self.image = "|", self.shape = "|"
            std::string lhs = trim(before.substr(0, before.size() - 1));
            if (lhs.size() >= 6 && lhs.compare(lhs.size() - 6, 6, ".image") == 0)
                values = filesWith(ix, {".png", ".jpg", ".jpeg"}), kind = SuggestionKind::File, what = "picture";
            else if (lhs.size() >= 6 && lhs.compare(lhs.size() - 6, 6, ".shape") == 0)
                values = ix.shapes, what = "shape";
            else if (lhs.size() >= 6 && lhs.compare(lhs.size() - 6, 6, ".color") == 0)
                values = ix.colors, what = "color";
        } else if (before.size() > 2 && before.compare(before.size() - 2, 2, "==") == 0) {
            // other.tag == "|", key == "|", message == "|"
            std::string lhs = trim(before.substr(0, before.size() - 2));
            if (lhs.size() >= 4 && lhs.compare(lhs.size() - 4, 4, ".tag") == 0)
                values = ix.tags, what = "tag";
            else if (lhs == "key")
                values = ix.keyNames, what = "key";
            else if (lhs == "message" || lhs == "name")
                values = ix.messages, what = "message";
            else if (lhs.size() >= 5 && lhs.compare(lhs.size() - 5, 5, ".name") == 0)
                values = ix.objectNames, what = "object";
        }
        for (auto& v : values)
            add(out, seen, typed, {v, v, what, "", kind, 0}, 0);
        std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.score != b.score ? a.score > b.score : a.label < b.label; });
        return out;
    }

    // ---- after a dot: members of what's before it
    int wordFrom = col;
    while (wordFrom > 0 && isWord(s[static_cast<size_t>(wordFrom - 1)]))
        --wordFrom;
    from = wordFrom;
    std::string typed = s.substr(static_cast<size_t>(wordFrom), static_cast<size_t>(col - wordFrom));
    bool afterDot = wordFrom > 0 && s[static_cast<size_t>(wordFrom - 1)] == '.';
    bool afterArrow = !easy && wordFrom > 1 && s.compare(static_cast<size_t>(wordFrom - 2), 2, "->") == 0;
    FileInfo info = easy ? analyze(lines) : FileInfo{};
    const FileFunction* fn = easy ? functionAt(info, line) : nullptr;

    auto addFunctions = [&](const std::vector<ProjectIndex::Function>& fns, SuggestionKind kind, const std::string& docPrefix, int bonus) {
        for (auto& f : fns)
            add(out, seen, typed, {f.name, callInsert(f.name, f.signature), f.signature, ix.doc(docPrefix + f.name), kind, 0}, bonus);
    };
    auto addObjectMembers = [&](bool self) {
        for (auto& p : ix.selfProperties)
            add(out, seen, typed, {p, p, "property", ix.doc("self." + p), SuggestionKind::Property, 0}, 40);
        addFunctions(ix.selfMethods, SuggestionKind::Method, "self.", 30);
        for (auto& [alias, comp] : ix.componentAliases)
            add(out, seen, typed, {alias, alias, comp, "Its " + comp + " component (if it has one).", SuggestionKind::Component, 0}, 10);
        if (self)
            for (auto& v : info.vars)
                add(out, seen, typed, {v.name, v.name, "variable", v.doc, SuggestionKind::Variable, 0}, 50);
    };

    if (afterArrow) {
        for (auto& f : ix.cBehaviorFields)
            add(out, seen, typed, {f, f, "event", ix.doc(f), SuggestionKind::Event, 0}, 0);
    } else if (afterDot && easy) {
        int e = wordFrom - 1;
        int b = e;
        while (b > 0 && (isWord(s[static_cast<size_t>(b - 1)]) || s[static_cast<size_t>(b - 1)] == '.'))
            --b;
        std::string chain = s.substr(static_cast<size_t>(b), static_cast<size_t>(e - b));
        std::string last = chain.substr(chain.rfind('.') == std::string::npos ? 0 : chain.rfind('.') + 1);
        std::string type;
        if (chain == "self")
            type = "self";
        else if (chain == "game")
            type = "game";
        else if (b > 0 && (s[static_cast<size_t>(b - 1)] == '"' || s[static_cast<size_t>(b - 1)] == '\''))
            type = "text";
        else if (chain.find('.') != std::string::npos) {
            // self.sprite.| -> the component's fields
            for (auto& [alias, comp] : ix.componentAliases)
                if (alias == last)
                    type = "component:" + comp;
            if (type.empty() && (last == "parent" || last == "camera"))
                type = "object";
            if (type.empty() && (last == "position" || last == "velocity" || last == "world_position" || last == "color" ||
                                 last == "forward" || last == "right" || last == "up" || last == "scale" || last == "size"))
                type = "vec";
            if (type.empty() && last == "children")
                type = "list";
        } else {
            type = guessType(lines, chain, line, fn);
            if (type.empty()) {
                // A component variable like `body = self.get_component("RigidBody2D")`
                type = "object";
            }
        }
        if (type == "self") {
            addObjectMembers(true);
        } else if (type == "object" || type == "objects") {
            if (type == "objects")
                addFunctions(ix.typeMethods.count("list") ? ix.typeMethods.at("list") : std::vector<ProjectIndex::Function>{},
                             SuggestionKind::Method, "list.", 0);
            else
                addObjectMembers(false);
        } else if (type == "game") {
            for (auto& g : ix.gameValues)
                add(out, seen, typed, {g, g, "game value", "Shared by every script, and kept when the scene changes.", SuggestionKind::Variable, 0}, 0);
        } else if (type.rfind("component:", 0) == 0) {
            auto it = ix.components.find(type.substr(10));
            if (it != ix.components.end())
                for (auto& [field, ftype] : it->second)
                    add(out, seen, typed, {field, field, ftype, "", SuggestionKind::Property, 0}, 0);
        } else if (ix.typeMethods.count(type)) {
            addFunctions(ix.typeMethods.at(type), SuggestionKind::Method, type + ".", 0);
            if (type == "vec")
                for (const char* p : {"x", "y", "z", "r", "g", "b", "a"})
                    add(out, seen, typed, {p, p, "number", ix.doc(std::string("vec.") + p), SuggestionKind::Property, 0}, 10);
        }
    } else if (afterDot) {
        // C: struct fields after '.'
        for (auto& f : ix.cBehaviorFields)
            add(out, seen, typed, {f, f, "event", ix.doc(f), SuggestionKind::Event, 0}, 0);
    } else if (easy) {
        if (typed.empty() && !manual)
            return out;
        std::string head = trim(s.substr(0, static_cast<size_t>(wordFrom)));
        if (head == "def") {
            // Events Aven calls, written out with their parameters.
            for (auto& [name, sig] : ix.events)
                add(out, seen, typed, {name, sig + ":\n\t$0", "def " + sig, ix.doc(name), SuggestionKind::Event, 0}, 100);
        } else {
            bool lineStart = head.empty();
            if (fn)
                for (auto& n : localsOf(lines, *fn, line))
                    add(out, seen, typed, {n, n, "local", "", SuggestionKind::Variable, 0}, 70);
            for (auto& v : info.vars)
                add(out, seen, typed,
                    {v.name, v.name, v.value.empty() ? "variable" : "= " + v.value, v.doc, SuggestionKind::Variable, 0}, 60);
            for (auto& f : info.functions)
                if (f.name.rfind("on_", 0) != 0 || lineStart)
                    add(out, seen, typed, {f.name, callInsert(f.name, f.signature), f.signature, f.doc, SuggestionKind::Function, 0}, 55);
            add(out, seen, typed, {"self", "self", "this object", ix.doc("self"), SuggestionKind::Keyword, 0}, 45);
            for (auto& g : ix.globalValues)
                add(out, seen, typed, {g, g, g == "game" ? "shared values" : "value", ix.doc(g), SuggestionKind::Variable, 0}, 35);
            addFunctions(ix.globals, SuggestionKind::Function, "", 30);
            for (const char* k : kEasyKeywords)
                add(out, seen, typed, {k, k, "keyword", ix.doc(k), SuggestionKind::Keyword, 0}, 20);
            if (lineStart) {
                // Ready-made pieces of code.
                add(out, seen, typed, {"for (repeat)", "for i in range($0):\n\t", "for i in range(10):", "Repeats the lines below a number of times.", SuggestionKind::Snippet, 0}, 15);
                add(out, seen, typed, {"for (each object)", "for enemy in find_all(\"$0\"):\n\t", "for enemy in find_all(\"enemy\"):", "Does something to every object with a tag.", SuggestionKind::Snippet, 0}, 15);
                add(out, seen, typed, {"if (key pressed)", "if key_pressed(\"$0\"):\n\t", "if key_pressed(\"space\"):", "Reacts to a key press.", SuggestionKind::Snippet, 0}, 15);
                add(out, seen, typed, {"def (function)", "def $0():\n\t", "def my_function():", "Makes your own function.", SuggestionKind::Snippet, 0}, 15);
                add(out, seen, typed, {"while (loop with wait)", "while True:\n\t$0\n\twait(1)", "while True: ... wait(1)", "Repeats forever, pausing each time.", SuggestionKind::Snippet, 0}, 15);
            }
        }
    } else {
        // C and C++ native code.
        if (typed.empty() && !manual)
            return out;
        for (auto& f : ix.cFunctions)
            add(out, seen, typed, {f.name, f.name + "($0)", f.signature, ix.doc(f.name.substr(5)), SuggestionKind::Function, 0}, 30);
        for (const char* m : {"AVEN_MODULE", "AVEN_API_VERSION", "AvenBehavior", "AvenEntity", "AvenModule"})
            add(out, seen, typed, {m, m, "Aven C API", "", SuggestionKind::Keyword, 0}, 25);
        for (const char* k : kCKeywords)
            add(out, seen, typed, {k, k, "keyword", "", SuggestionKind::Keyword, 0}, 10);
        // Words already in the file.
        std::set<std::string> words;
        for (auto& l : lines)
            for (size_t i = 0; i < l.size();) {
                if (isWord(l[i]) && !std::isdigit(static_cast<unsigned char>(l[i]))) {
                    size_t e = i;
                    while (e < l.size() && isWord(l[e]))
                        ++e;
                    if (e - i >= 3)
                        words.insert(l.substr(i, e - i));
                    i = e;
                } else {
                    ++i;
                }
            }
        for (auto& w : words)
            if (w != typed)
                add(out, seen, typed, {w, w, "in this file", "", SuggestionKind::Variable, 0}, 5);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.score != b.score ? a.score > b.score : a.label < b.label; });
    if (out.size() > 60)
        out.resize(60);
    return out;
}

// ---------------------------------------------------------------- parameter hints

bool CodeIntel::signature(const std::vector<std::string>& lines, int line, int col, SignatureHelp& out) const {
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return false;
    const std::string& s = lines[static_cast<size_t>(line)];
    col = std::clamp(col, 0, static_cast<int>(s.size()));
    if (stateAt(s, col, kind_).comment)
        return false;
    // Find the unclosed '(' before the cursor, counting commas at its level.
    int depth = 0, arg = 0;
    char q = 0;
    // Strings are skipped by scanning forward first.
    std::vector<int> stringMask(static_cast<size_t>(col), 0);
    for (int i = 0; i < col; ++i) {
        char c = s[static_cast<size_t>(i)];
        if (q) {
            stringMask[static_cast<size_t>(i)] = 1;
            if (c == '\\' && i + 1 < col)
                stringMask[static_cast<size_t>(++i)] = 1;
            else if (c == q)
                q = 0;
        } else if (c == '"' || c == '\'') {
            q = c;
            stringMask[static_cast<size_t>(i)] = 1;
        }
    }
    int open = -1;
    for (int i = col - 1; i >= 0; --i) {
        if (stringMask[static_cast<size_t>(i)])
            continue;
        char c = s[static_cast<size_t>(i)];
        if (c == ')' || c == ']' || c == '}')
            ++depth;
        else if (c == '(' || c == '[' || c == '{') {
            if (depth == 0) {
                if (c != '(')
                    return false;
                open = i;
                break;
            }
            --depth;
        } else if (c == ',' && depth == 0)
            ++arg;
    }
    if (open <= 0)
        return false;
    int e = open;
    while (e > 0 && s[static_cast<size_t>(e - 1)] == ' ')
        --e;
    int b = e;
    while (b > 0 && isWord(s[static_cast<size_t>(b - 1)]))
        --b;
    if (b == e)
        return false;
    std::string name = s.substr(static_cast<size_t>(b), static_cast<size_t>(e - b));
    bool method = b > 0 && s[static_cast<size_t>(b - 1)] == '.';
    std::string receiver;
    if (method) {
        int r = b - 1;
        while (r > 0 && isWord(s[static_cast<size_t>(r - 1)]))
            --r;
        receiver = s.substr(static_cast<size_t>(r), static_cast<size_t>(b - 1 - r));
    }
    std::string sig, doc;
    const ProjectIndex& ix = index_;
    if (kind_ == CodeKind::C) {
        for (auto& f : ix.cFunctions)
            if (f.name == name) {
                sig = f.signature;
                doc = ix.doc(name.substr(5));
            }
    } else if (!method) {
        if (auto* g = ix.global(name)) {
            sig = g->signature;
            doc = ix.doc(name);
        } else {
            FileInfo info = analyze(lines);
            for (auto& f : info.functions)
                if (f.name == name) {
                    sig = f.signature;
                    doc = f.doc;
                }
        }
    } else {
        std::string type = receiver == "self" ? "object" : guessType(lines, receiver, line, nullptr);
        if (ix.typeMethods.count(type))
            for (auto& m : ix.typeMethods.at(type))
                if (m.name == name)
                    sig = m.signature, doc = ix.doc(type + "." + name);
        if (sig.empty())
            for (auto& m : ix.selfMethods)
                if (m.name == name)
                    sig = m.signature, doc = ix.doc("self." + name);
        if (sig.empty())
            for (auto& [t, methods] : ix.typeMethods)
                for (auto& m : methods)
                    if (m.name == name && sig.empty())
                        sig = m.signature, doc = ix.doc(t + "." + name);
    }
    if (sig.empty())
        return false;
    // Pick the variant that has room for the value being typed.
    auto vs = variants(sig);
    std::string chosen = vs.back();
    for (auto& v : vs)
        if (static_cast<int>(paramRanges(v).size()) > arg) {
            chosen = v;
            break;
        }
    out.label = chosen;
    out.params = paramRanges(chosen);
    out.active = arg;
    out.doc = doc;
    return true;
}

// ---------------------------------------------------------------- hover

std::string CodeIntel::hover(const std::vector<std::string>& lines, int line, int col, int& from, int& to) const {
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return "";
    const std::string& s = lines[static_cast<size_t>(line)];
    if (!wordAt(s, col, from, to))
        return "";
    LineState st = stateAt(s, from, kind_);
    if (st.comment || st.string)
        return "";
    std::string word = s.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
    const ProjectIndex& ix = index_;
    auto join = [](const std::string& a, const std::string& b) { return b.empty() ? a : a.empty() ? b : a + "\n" + b; };
    if (kind_ == CodeKind::C) {
        for (auto& f : ix.cFunctions)
            if (f.name == word)
                return join(f.signature, ix.doc(word.substr(5)));
        return "";
    }
    std::string receiver;
    if (from > 0 && s[static_cast<size_t>(from - 1)] == '.') {
        int r = from - 1;
        while (r > 0 && isWord(s[static_cast<size_t>(r - 1)]))
            --r;
        receiver = s.substr(static_cast<size_t>(r), static_cast<size_t>(from - 1 - r));
    }
    FileInfo info = analyze(lines);
    if (!receiver.empty()) {
        if (receiver == "game")
            return "game." + word + "\nA game value: shared by every script, and kept when the scene changes.";
        for (auto& m : ix.selfMethods)
            if (m.name == word)
                return join(m.signature, ix.doc("self." + word));
        for (auto& p : ix.selfProperties)
            if (p == word)
                return join(receiver + "." + word, ix.doc("self." + word));
        for (auto& [alias, comp] : ix.componentAliases)
            if (alias == word)
                return receiver + "." + word + "\nIts " + comp + " component. Read or change its settings, like " + receiver + "." +
                       word + "." + (ix.components.count(comp) && !ix.components.at(comp).empty() ? ix.components.at(comp).front().first : "field");
        for (auto& [t, methods] : ix.typeMethods)
            for (auto& m : methods)
                if (m.name == word)
                    return join(m.signature, ix.doc(t + "." + word));
        if (receiver == "self")
            for (auto& v : info.vars)
                if (v.name == word)
                    return join("self." + word + " (a variable of this script)", v.doc);
        return "";
    }
    for (auto& f : info.functions)
        if (f.name == word) {
            std::string d = "def " + f.signature;
            for (auto& [name, sig] : ix.events)
                if (name == word)
                    return join(d, ix.doc(word));
            return join(d, f.doc);
        }
    for (auto& v : info.vars)
        if (v.name == word)
            return join(word + (v.value.empty() ? "" : " = " + v.value) + "  (script variable" +
                            (v.value.empty() || v.value.find('(') != std::string::npos ? ")" : ", shown in the Inspector)"),
                        v.doc);
    if (auto* g = ix.global(word))
        return join(g->signature, ix.doc(word));
    std::string d = ix.doc(word);
    if (!d.empty())
        return join(word, d);
    if (const FileFunction* fn = functionAt(info, line)) {
        for (auto& p : fn->params)
            if (p == word)
                return word + "\nA value given to " + fn->name + "().";
        for (auto& n : localsOf(lines, *fn, line))
            if (n == word)
                return word + "\nA variable inside " + fn->name + "(). It's forgotten when the function ends.";
    }
    return "";
}

// ---------------------------------------------------------------- problems

namespace {

struct Checker {
    const ProjectIndex& ix;
    const std::vector<std::string>& lines;
    std::vector<Diagnostic>& out;
    std::unordered_set<std::string> moduleNames;
    std::set<std::string> selfAssigned;
    std::set<std::pair<int, std::string>> reported;

    // Column of a word on a line: the recorded one when it matches, else the first whole-word match.
    void place(Diagnostic& d, int line1, int col1, const std::string& word) {
        d.line = std::max(0, line1 - 1);
        const std::string& s = d.line < static_cast<int>(lines.size()) ? lines[static_cast<size_t>(d.line)] : std::string();
        int c = col1 - 1;
        bool ok = c >= 0 && c + static_cast<int>(word.size()) <= static_cast<int>(s.size()) &&
                  s.compare(static_cast<size_t>(c), word.size(), word) == 0;
        if (!ok) {
            c = 0;
            for (size_t at = s.find(word); at != std::string::npos; at = s.find(word, at + 1)) {
                bool left = at == 0 || !isWord(s[at - 1]);
                bool right = at + word.size() >= s.size() || !isWord(s[at + word.size()]);
                if (left && right) {
                    c = static_cast<int>(at);
                    break;
                }
            }
        }
        d.col = c;
        d.endCol = c + static_cast<int>(word.size());
    }

    void report(int line1, int col1, const std::string& word, const std::string& message, bool error) {
        if (!reported.insert({line1, message}).second)
            return;
        Diagnostic d;
        place(d, line1, col1, word);
        d.message = message;
        d.error = error;
        out.push_back(std::move(d));
    }

    std::vector<std::string> visibleNames(const std::unordered_set<std::string>& locals) const {
        std::vector<std::string> v(moduleNames.begin(), moduleNames.end());
        v.insert(v.end(), locals.begin(), locals.end());
        for (auto& g : ix.globals)
            v.push_back(g.name);
        for (auto& g : ix.globalValues)
            v.push_back(g);
        return v;
    }

    bool known(const std::string& n, const std::unordered_set<std::string>& locals) const {
        if (locals.count(n) || moduleNames.count(n) || ix.global(n))
            return true;
        return std::find(ix.globalValues.begin(), ix.globalValues.end(), n) != ix.globalValues.end();
    }

    static void assignedIn(const std::vector<StmtPtr>& stmts, std::unordered_set<std::string>& out, bool intoDefs) {
        std::function<void(const Expr&)> target = [&](const Expr& e) {
            if (e.kind == ExprKind::Name)
                out.insert(e.text);
            else if (e.kind == ExprKind::List)
                for (auto& i : e.items)
                    target(*i);
        };
        for (auto& s : stmts) {
            if (s->kind == StmtKind::Assign || s->kind == StmtKind::AugAssign || s->kind == StmtKind::For)
                for (auto& t : s->targets)
                    target(*t);
            if (s->kind == StmtKind::Def)
                out.insert(symbolName(s->name));
            if (s->kind == StmtKind::Global)
                for (Symbol g : s->globals)
                    out.insert(symbolName(g));
            if (s->kind != StmtKind::Def || intoDefs) {
                assignedIn(s->body, out, intoDefs);
                assignedIn(s->orelse, out, intoDefs);
            }
        }
    }

    // col1: 1-based column of the opening quote.
    void checkFile(const std::string& path, int line, int col1) {
        if (path.empty() || path.find('{') != std::string::npos)
            return;
        if (std::find(ix.files.begin(), ix.files.end(), path) != ix.files.end())
            return;
        std::vector<std::string> same;
        std::string ext = path.substr(path.rfind('.') == std::string::npos ? path.size() : path.rfind('.'));
        for (auto& f : ix.files)
            if (f.size() >= ext.size() && f.compare(f.size() - ext.size(), ext.size(), ext) == 0)
                same.push_back(f);
        report(line, col1, "\"" + path + "\"", "There's no file \"" + path + "\" in the project." + didYouMean(path, same), false);
    }

    void checkCall(const Expr& e, const std::unordered_set<std::string>& locals) {
        const Expr& callee = *e.a;
        int positional = static_cast<int>(e.items.size() - e.kwNames.size());
        const ProjectIndex::Function* f = nullptr;
        std::string name;
        if (callee.kind == ExprKind::Name && !locals.count(callee.text) && !moduleNames.count(callee.text)) {
            f = ix.global(callee.text);
            name = callee.text;
        } else if (callee.kind == ExprKind::Attr && callee.a && callee.a->kind == ExprKind::Self) {
            for (auto& m : ix.selfMethods)
                if (m.name == callee.text)
                    f = &m;
            name = callee.text;
        }
        if (f && (positional < f->minArgs || (f->maxArgs >= 0 && positional > f->maxArgs))) {
            std::string need = f->minArgs == f->maxArgs
                                   ? (f->maxArgs == 0 ? "doesn't take any values" : "needs " + std::to_string(f->maxArgs) + (f->maxArgs == 1 ? " value" : " values"))
                               : f->maxArgs < 0 ? "needs at least " + std::to_string(f->minArgs) + (f->minArgs == 1 ? " value" : " values")
                                                : "needs " + std::to_string(f->minArgs) + " to " + std::to_string(f->maxArgs) + " values";
            report(callee.line, callee.col, name,
                   name + "() " + need + ", but gets " + std::to_string(positional) + ". Use it like: " + f->signature, true);
        }
        // String arguments that must name something that exists.
        std::string last = callee.kind == ExprKind::Name || callee.kind == ExprKind::Attr ? callee.text : "";
        const Expr* arg0 = !e.items.empty() && e.items[0]->kind == ExprKind::String ? e.items[0].get() : nullptr;
        if (!arg0)
            return;
        if (last == "key_down" || last == "key_pressed" || last == "key_released") {
            const std::string& k = arg0->text;
            bool ok = Input::isValidName(k) || std::find(ix.keyNames.begin(), ix.keyNames.end(), k) != ix.keyNames.end();
            if (!ok)
                report(arg0->line, arg0->col + 1, k, "\"" + k + "\" isn't a key name." + didYouMean(k, ix.keyNames) +
                                                          " Keys are written like \"space\", \"left\", \"a\" or \"enter\".", false);
        } else if (last == "play_sound" || last == "play_music" || last == "spawn") {
            checkFile(arg0->text, arg0->line, arg0->col);
        } else if (last == "load_scene") {
            std::string p = arg0->text.find('.') == std::string::npos ? "scenes/" + arg0->text + ".scene" : arg0->text;
            checkFile(p, arg0->line, arg0->col);
        } else if (last == "get_component" || last == "add_component" || last == "has_component" || last == "remove_component") {
            if (!ComponentRegistry::find(arg0->text)) {
                std::vector<std::string> names;
                for (auto& [n, fields] : ix.components)
                    names.push_back(n);
                report(arg0->line, arg0->col + 1, arg0->text,
                       "There's no component called \"" + arg0->text + "\"." + didYouMean(arg0->text, names), false);
            }
        }
    }

    void expr(const Expr& e, const std::unordered_set<std::string>& locals, bool store = false) {
        switch (e.kind) {
        case ExprKind::Name:
            if (!store && !known(e.text, locals)) {
                std::string hint = didYouMean(e.text, visibleNames(locals));
                if (e.text == "this")
                    hint = " EasyScript uses 'self' instead of 'this'.";
                else if (e.text == "true" || e.text == "false")
                    hint = "";
                report(e.line, e.col, e.text, "I don't know what '" + e.text + "' is." + hint, false);
            }
            return;
        case ExprKind::Attr:
            if (e.a && e.a->kind == ExprKind::Self && !store) {
                const std::string& n = e.text;
                bool ok = std::find(ix.selfProperties.begin(), ix.selfProperties.end(), n) != ix.selfProperties.end() ||
                          std::any_of(ix.selfMethods.begin(), ix.selfMethods.end(), [&](auto& m) { return m.name == n; }) ||
                          std::any_of(ix.componentAliases.begin(), ix.componentAliases.end(), [&](auto& a) { return a.first == n; }) ||
                          ComponentRegistry::find(n) || moduleNames.count(n) || selfAssigned.count(n) || n == "alive";
                if (!ok) {
                    std::vector<std::string> options = ix.selfProperties;
                    for (auto& m : ix.selfMethods)
                        options.push_back(m.name);
                    options.insert(options.end(), moduleNames.begin(), moduleNames.end());
                    report(e.line, e.col, n, "self doesn't have '" + n + "'." + didYouMean(n, options), false);
                }
            }
            if (e.a)
                expr(*e.a, locals);
            return;
        case ExprKind::Call:
            checkCall(e, locals);
            break;
        case ExprKind::String:
            return;
        default: break;
        }
        if (e.a)
            expr(*e.a, locals);
        if (e.b)
            expr(*e.b, locals);
        if (e.c)
            expr(*e.c, locals);
        for (auto& i : e.items)
            expr(*i, locals);
    }

    void target(const Expr& t, const std::unordered_set<std::string>& locals) {
        if (t.kind == ExprKind::Name)
            return;
        if (t.kind == ExprKind::List) {
            for (auto& i : t.items)
                target(*i, locals);
            return;
        }
        if (t.kind == ExprKind::Attr) {
            if (t.a)
                expr(*t.a, locals);
            if (t.a && t.a->kind == ExprKind::Self) {
                // self.anything = ... makes a new value on the object, which is fine, unless it's
                // one letter away from a real property: then it's almost surely a typo.
                const std::string& n = t.text;
                bool real = std::find(ix.selfProperties.begin(), ix.selfProperties.end(), n) != ix.selfProperties.end() ||
                            moduleNames.count(n) || ComponentRegistry::find(n);
                std::string near = real ? "" : typoOf(n, ix.selfProperties);
                if (!near.empty())
                    report(t.line, t.col, n,
                           "self doesn't have '" + n + "', so this makes a new value instead of changing one. Did you mean '" +
                               near + "'?",
                           false);
            }
            return;
        }
        expr(t, locals);
    }

    void stmts(const std::vector<StmtPtr>& list, const std::unordered_set<std::string>& locals) {
        for (auto& s : list) {
            switch (s->kind) {
            case StmtKind::Def: {
                std::unordered_set<std::string> inner = locals;
                for (Symbol p : s->params)
                    inner.insert(symbolName(p));
                assignedIn(s->body, inner, false);
                for (auto& d : s->defaults)
                    expr(*d, locals);
                stmts(s->body, inner);
                continue;
            }
            case StmtKind::Assign:
                if (s->expr)
                    expr(*s->expr, locals);
                for (auto& t : s->targets)
                    target(*t, locals);
                continue;
            case StmtKind::AugAssign:
                if (s->expr)
                    expr(*s->expr, locals);
                for (auto& t : s->targets)
                    if (t->kind == ExprKind::Name)
                        expr(*t, locals);
                    else
                        target(*t, locals);
                continue;
            case StmtKind::For:
                if (s->expr)
                    expr(*s->expr, locals);
                break;
            default:
                if (s->expr)
                    expr(*s->expr, locals);
                break;
            }
            stmts(s->body, locals);
            stmts(s->orelse, locals);
        }
    }
};

// Function names people bring from other engines, and what Aven calls them.
const std::pair<const char*, const char*> kOtherEngineEvents[] = {
    {"update", "on_update"},          {"Update", "on_update"},           {"_process", "on_update"},
    {"start", "on_start"},            {"Start", "on_start"},             {"_ready", "on_start"},
    {"Awake", "on_start"},            {"awake", "on_start"},             {"FixedUpdate", "on_fixed_update"},
    {"_physics_process", "on_fixed_update"}, {"OnCollisionEnter", "on_collide"}, {"OnCollisionEnter2D", "on_collide"},
    {"OnTriggerEnter", "on_trigger"}, {"OnTriggerEnter2D", "on_trigger"}, {"OnMouseDown", "on_click"},
    {"OnDestroy", "on_destroy"},      {"on_click_pressed", "on_click"},
};

} // namespace

std::vector<Diagnostic> CodeIntel::diagnose(const std::string& source) const {
    std::vector<Diagnostic> out;
    if (kind_ != CodeKind::EasyScript)
        return out;
    std::vector<std::string> lines;
    for (size_t start = 0;;) {
        size_t nl = source.find('\n', start);
        lines.push_back(source.substr(start, nl == std::string::npos ? std::string::npos : nl - start));
        if (nl == std::string::npos)
            break;
        start = nl + 1;
    }
    Program program;
    try {
        program = parse(source);
    } catch (const ScriptError& e) {
        Diagnostic d;
        d.line = std::max(0, e.line - 1);
        const std::string& s = d.line < static_cast<int>(lines.size()) ? lines[static_cast<size_t>(d.line)] : std::string();
        d.col = indentOf(s);
        d.endCol = std::max(d.col + 1, static_cast<int>(s.size()));
        d.message = e.message;
        d.error = true;
        out.push_back(std::move(d));
        return out;
    }
    Checker c{index_, lines, out, {}, {}, {}};
    std::unordered_set<std::string> top;
    Checker::assignedIn(program.statements, top, false);
    // `global x` inside functions also makes script variables.
    std::function<void(const std::vector<StmtPtr>&)> globalsIn = [&](const std::vector<StmtPtr>& list) {
        for (auto& s : list) {
            if (s->kind == StmtKind::Global)
                for (Symbol g : s->globals)
                    top.insert(symbolName(g));
            globalsIn(s->body);
            globalsIn(s->orelse);
        }
    };
    globalsIn(program.statements);
    c.moduleNames = top;
    FileInfo info = analyze(lines);
    c.selfAssigned = info.selfAssigned;
    c.stmts(program.statements, {});

    // Events with a typo, and function names from other engines.
    for (auto& s : program.statements) {
        if (s->kind != StmtKind::Def)
            continue;
        std::string name = symbolName(s->name);
        bool isEvent = std::any_of(index_.events.begin(), index_.events.end(), [&](auto& e) { return e.first == name; });
        if (isEvent)
            continue;
        if (name.rfind("on_", 0) == 0) {
            std::vector<std::string> events;
            for (auto& e : index_.events)
                events.push_back(e.first);
            std::string near = closestMatch(name, events);
            bool calledHere = source.find(name + "(") != source.rfind(name + "(") || source.find("\"" + name + "\"") != std::string::npos;
            if (!near.empty() && !calledHere)
                c.report(s->line, 0, name, "Aven never calls '" + name + "' by itself. Did you mean '" + near + "'?", false);
            continue;
        }
        for (auto& [other, ours] : kOtherEngineEvents)
            if (name == other) {
                bool calledHere = source.find(name + "(") != source.rfind(name + "(");
                if (!calledHere)
                    c.report(s->line, 0, name,
                             "Aven calls " + std::string(ours) + "() for this, not " + name + "(). Rename it to " + ours + "?", false);
            }
    }
    // Pictures set by assignments: self.image = "images/x.png"
    for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
        const std::string& s = lines[static_cast<size_t>(l)];
        size_t at = s.find(".image = \"");
        if (at == std::string::npos)
            continue;
        size_t b = at + 10, e = s.find('"', b);
        if (e != std::string::npos)
            c.checkFile(s.substr(b, e - b), l + 1, static_cast<int>(b));
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.line != b.line ? a.line < b.line : a.col < b.col; });
    return out;
}

// ---------------------------------------------------------------- navigation

bool CodeIntel::definition(const std::vector<std::string>& lines, int line, int col, int& outLine, int& outCol) const {
    if (line < 0 || line >= static_cast<int>(lines.size()))
        return false;
    int from, to;
    const std::string& s = lines[static_cast<size_t>(line)];
    if (!wordAt(s, col, from, to))
        return false;
    std::string word = s.substr(static_cast<size_t>(from), static_cast<size_t>(to - from));
    bool afterSelf = from >= 5 && s.compare(static_cast<size_t>(from - 5), 5, "self.") == 0;
    bool afterDot = from > 0 && s[static_cast<size_t>(from - 1)] == '.';
    if (afterDot && !afterSelf)
        return false;
    if (kind_ == CodeKind::C) {
        // A function or struct defined in this file.
        for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
            const std::string& t = lines[static_cast<size_t>(l)];
            size_t at = t.find(word + "(");
            bool isDef = at != std::string::npos && at > 0 && t.find(';') == std::string::npos && indentOf(t) == 0 &&
                         (t.find("static") == 0 || t.find("void") == 0 || t.find("int") == 0 || t.find("float") == 0 ||
                          t.find("double") == 0);
            if (isDef || (t.find("} " + word + ";") != std::string::npos)) {
                outLine = l;
                outCol = static_cast<int>(isDef ? at : t.find(word));
                return true;
            }
        }
        return false;
    }
    FileInfo info = analyze(lines);
    if (!afterSelf)
        if (const FileFunction* fn = functionAt(info, line)) {
            // A local: the first line in the function that sets it.
            for (int l = fn->line; l <= fn->end && l < static_cast<int>(lines.size()); ++l) {
                std::string t = trim(lines[static_cast<size_t>(l)]);
                auto names = l == fn->line ? fn->params : assignedNames(t);
                if (l != fn->line)
                    for (auto& n : loopNames(t))
                        names.push_back(n);
                if (std::find(names.begin(), names.end(), word) != names.end()) {
                    outLine = l;
                    outCol = static_cast<int>(lines[static_cast<size_t>(l)].find(word));
                    return true;
                }
            }
        }
    for (auto& f : info.functions)
        if (f.name == word) {
            outLine = f.line;
            outCol = static_cast<int>(lines[static_cast<size_t>(f.line)].find(word));
            return true;
        }
    for (auto& v : info.vars)
        if (v.name == word) {
            outLine = v.line;
            outCol = static_cast<int>(lines[static_cast<size_t>(v.line)].find(word));
            return true;
        }
    return false;
}

std::vector<OutlineItem> CodeIntel::outline(const std::vector<std::string>& lines) const {
    std::vector<OutlineItem> out;
    if (kind_ == CodeKind::C) {
        for (int l = 0; l < static_cast<int>(lines.size()); ++l) {
            const std::string& t = lines[static_cast<size_t>(l)];
            if (indentOf(t) != 0 || t.find('(') == std::string::npos || t.find(';') != std::string::npos || t.rfind("#", 0) == 0 ||
                t.rfind("//", 0) == 0 || t.rfind("AVEN_MODULE", 0) == 0)
                continue;
            size_t open = t.find('(');
            size_t b = open;
            while (b > 0 && isWord(t[b - 1]))
                --b;
            if (b < open)
                out.push_back({t.substr(b, open - b), trim(t), l, true});
        }
        return out;
    }
    FileInfo info = analyze(lines);
    for (auto& v : info.vars)
        out.push_back({v.name, v.value.empty() ? "" : "= " + v.value, v.line, false});
    for (auto& f : info.functions)
        out.push_back({f.name, "def " + f.signature, f.line, true});
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.line < b.line; });
    return out;
}

} // namespace aven::script
