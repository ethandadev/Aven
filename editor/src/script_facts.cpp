#include "script_facts.h"

#include "aven/blocks/blocks.h"
#include "aven/core/json.h"
#include "aven/script/ast.h"
#include "aven/script/errors.h"

#include <cmath>
#include <cstdio>

namespace aven::editor {

using namespace script;

namespace {

std::string num(double v) {
    char buf[32];
    if (std::abs(v - std::round(v)) < 1e-9)
        std::snprintf(buf, sizeof buf, "%lld", static_cast<long long>(std::llround(v)));
    else
        std::snprintf(buf, sizeof buf, "%g", v);
    return buf;
}

const char* opText(Tok t) {
    switch (t) {
    case Tok::Plus: return "+";
    case Tok::Minus: return "-";
    case Tok::Star: return "*";
    case Tok::Slash: return "/";
    case Tok::SlashSlash: return "//";
    case Tok::Percent: return "%";
    case Tok::StarStar: return "**";
    case Tok::Eq: return "==";
    case Tok::NotEq: return "!=";
    case Tok::Lt: return "<";
    case Tok::Gt: return ">";
    case Tok::LtEq: return "<=";
    case Tok::GtEq: return ">=";
    case Tok::Not: return "not ";
    default: return "?";
    }
}

// Short source-like text for an expression.
std::string text(const Expr* e) {
    if (!e)
        return "";
    switch (e->kind) {
    case ExprKind::Number: return num(e->number);
    case ExprKind::String: return "\"" + e->text + "\"";
    case ExprKind::FString: return "a message";
    case ExprKind::True: return "True";
    case ExprKind::False: return "False";
    case ExprKind::None: return "None";
    case ExprKind::Name: return symbolName(e->sym);
    case ExprKind::Self: return "self";
    case ExprKind::List: return "a list";
    case ExprKind::Dict: return "a dictionary";
    case ExprKind::Unary: return std::string(e->op == Tok::Not ? "not " : opText(e->op)) + text(e->a.get());
    case ExprKind::Binary: return text(e->a.get()) + " " + opText(e->op) + " " + text(e->b.get());
    case ExprKind::And: return text(e->a.get()) + " and " + text(e->b.get());
    case ExprKind::Or: return text(e->a.get()) + " or " + text(e->b.get());
    case ExprKind::Compare: return text(e->a.get()) + " " + opText(e->op) + " " + text(e->b.get());
    case ExprKind::Call: {
        std::string s = text(e->a.get()) + "(";
        for (size_t i = 0; i < e->items.size(); ++i)
            s += (i ? ", " : "") + text(e->items[i].get());
        return s + ")";
    }
    case ExprKind::Attr: return text(e->a.get()) + "." + symbolName(e->sym);
    case ExprKind::Index: return text(e->a.get()) + "[" + text(e->b.get()) + "]";
    case ExprKind::Slice: return text(e->a.get()) + "[...]";
    case ExprKind::Ternary: return text(e->b.get()) + " or " + text(e->c.get());
    }
    return "";
}

std::string stringArg(const Expr* call, size_t i) {
    if (!call || i >= call->items.size() || call->items[i]->kind != ExprKind::String)
        return "";
    return call->items[i]->text;
}

// The function name of a call: "play_sound", "self.destroy" -> "destroy" (with `object` = "self").
std::string callName(const Expr* call, std::string* object = nullptr) {
    const Expr* f = call->a.get();
    if (f->kind == ExprKind::Name)
        return symbolName(f->sym);
    if (f->kind == ExprKind::Attr) {
        if (object)
            *object = text(f->a.get());
        return symbolName(f->sym);
    }
    return "";
}

struct Walker {
    ScriptFacts& facts;

    // Collect facts from every expression.
    void expr(const Expr* e) {
        if (!e)
            return;
        if (e->kind == ExprKind::Call) {
            std::string obj;
            std::string name = callName(e, &obj);
            std::string arg = stringArg(e, 0);
            if (name == "broadcast" && !arg.empty())
                facts.sends.insert(arg);
            else if (name == "find" && !arg.empty() && obj.empty())
                facts.findsNames.insert(arg);
            else if ((name == "find_all" || name == "count" || name == "is_touching") && !arg.empty())
                facts.tags.insert(arg);
            else if ((name == "key_down" || name == "key_pressed" || name == "key_released") && !arg.empty())
                facts.keys.insert(arg), facts.readsInput = true;
            else if (name == "axis")
                facts.keys.insert("arrow keys / WASD"), facts.readsInput = true;
            else if (name == "spawn" && !arg.empty())
                facts.spawns.insert(arg), facts.fileLines.emplace(arg, e->line);
            else if (name == "load_scene" && !arg.empty())
                facts.scenes.insert(arg), facts.fileLines.emplace(arg, e->line);
            else if ((name == "play_sound" || name == "play_music") && !arg.empty())
                facts.sounds.insert(arg), facts.fileLines.emplace(arg, e->line);
            else if (name == "restart_scene")
                facts.restartsScene = true;
            else if (name == "apply_force" || name == "apply_impulse")
                facts.usesPhysics = true;
            else if (name == "get_game" && !arg.empty())
                facts.gameVarsRead.insert(arg);
        }
        if (e->kind == ExprKind::Attr && e->a && e->a->kind == ExprKind::Name && symbolName(e->a->sym) == "game")
            facts.gameVarsRead.insert(symbolName(e->sym));
        if (e->kind == ExprKind::Attr && (symbolName(e->sym) == "velocity" || symbolName(e->sym) == "velocity_x" ||
                                          symbolName(e->sym) == "velocity_y" || symbolName(e->sym) == "on_ground"))
            facts.usesPhysics = true;
        if (e->kind == ExprKind::Compare && e->op == Tok::Eq) {
            // other.tag == "coin" / message == "win"
            const Expr* a = e->a.get();
            const Expr* b = e->b.get();
            if (b && b->kind == ExprKind::String && a && a->kind == ExprKind::Attr && symbolName(a->sym) == "tag")
                facts.tags.insert(b->text);
            if (b && b->kind == ExprKind::String && a && a->kind == ExprKind::Name &&
                (symbolName(a->sym) == "name" || symbolName(a->sym) == "message"))
                facts.receives.insert(b->text);
            // def on_key_pressed(key): if key == "space" (what "when key pressed" blocks become)
            if (b && b->kind == ExprKind::String && a && a->kind == ExprKind::Name && symbolName(a->sym) == "key")
                facts.keys.insert(b->text), facts.readsInput = true;
        }
        expr(e->a.get());
        expr(e->b.get());
        expr(e->c.get());
        for (auto& i : e->items)
            expr(i.get());
    }

    // A plain-English sentence for one statement (empty = not worth mentioning).
    std::string sentence(const Stmt& s) {
        switch (s.kind) {
        case StmtKind::Expression: {
            const Expr* e = s.expr.get();
            if (!e || e->kind != ExprKind::Call)
                return "";
            std::string obj;
            std::string n = callName(e, &obj);
            std::string arg = stringArg(e, 0);
            std::string first = e->items.empty() ? "" : text(e->items[0].get());
            bool onSelf = obj.empty() || obj == "self";
            if (n == "destroy")
                return onSelf ? "removes itself" : "removes " + obj;
            if (n == "play_sound")
                return "plays the sound " + (arg.empty() ? first : arg);
            if (n == "play_music")
                return "plays the music " + (arg.empty() ? first : arg);
            if (n == "broadcast")
                return "sends the message \"" + (arg.empty() ? first : arg) + "\" to every object";
            if (n == "spawn")
                return "creates a copy of " + (arg.empty() ? first : arg);
            if (n == "load_scene")
                return "goes to the scene " + (arg.empty() ? first : arg);
            if (n == "restart_scene")
                return "restarts the level";
            if (n == "print")
                return "writes " + first + " in the Console";
            if (n == "wait")
                return "waits " + first + " seconds";
            if (n == "camera_shake")
                return "shakes the camera";
            if (n == "emit")
                return "bursts some particles";
            if (n == "send")
                return "asks " + obj + " to run " + arg + "()";
            if (n == "damage")
                return "hurts " + (onSelf ? "itself" : obj) + " (" + first + ")";
            if (n == "tween")
                return "smoothly animates its " + arg;
            if (n == "every")
                return "every " + first + " seconds, runs " + (e->items.size() > 1 ? text(e->items[1].get()) : "a function") + "()";
            if (n == "after")
                return "after " + first + " seconds, runs " + (e->items.size() > 1 ? text(e->items[1].get()) : "a function") + "()";
            if (n == "move_toward")
                return "moves toward " + first;
            if (n == "look_at")
                return "turns to face " + first;
            if (n == "apply_impulse" || n == "apply_force")
                return "gives it a push";
            if (n == "hide")
                return "hides " + (onSelf ? "itself" : obj);
            if (n == "show")
                return "shows " + (onSelf ? "itself" : obj);
            if (n == "move")
                return "moves by " + first;
            if (n == "turn")
                return "turns by " + first + " degrees";
            if (n == "lock_mouse")
                return "captures the mouse";
            if (n == "say")
                return "says " + first;
            return "runs " + n + "()";
        }
        case StmtKind::Assign:
        case StmtKind::AugAssign: {
            if (s.targets.empty())
                return "";
            const Expr* t = s.targets[0].get();
            std::string value = text(s.expr.get());
            bool add = s.kind == StmtKind::AugAssign;
            if (t->kind == ExprKind::Attr && t->a && t->a->kind == ExprKind::Name && symbolName(t->a->sym) == "game") {
                std::string var = symbolName(t->sym);
                facts.gameVarsWritten.insert(var);
                if (add)
                    return std::string(s.op == Tok::MinusAssign ? "takes " : "adds ") + value + (s.op == Tok::MinusAssign ? " from game." : " to game.") + var;
                return "sets game." + var + " to " + value;
            }
            if (t->kind == ExprKind::Attr) {
                std::string who = text(t->a.get());
                std::string p = symbolName(t->sym);
                std::string whose = who == "self" ? "its" : who + "'s";
                if (p == "x" && add)
                    return "moves " + std::string(who == "self" ? "it" : who) + " sideways";
                if (p == "y" && add)
                    return "moves " + std::string(who == "self" ? "it" : who) + " up or down";
                if (p == "velocity_x")
                    return "sets " + whose + " sideways speed to " + value;
                if (p == "velocity_y")
                    return s.expr && s.expr->kind == ExprKind::Number && s.expr->number > 0 ? "makes " + std::string(who == "self" ? "it" : who) + " jump" : "sets " + whose + " up/down speed to " + value;
                if (p == "velocity")
                    return "sets " + whose + " speed and direction";
                if (p == "angle" || p == "rotation" || p.rfind("rotation_", 0) == 0)
                    return "turns " + std::string(who == "self" ? "it" : who);
                if (p == "text")
                    return "changes " + whose + " text";
                if (p == "alpha")
                    return "changes how see-through " + std::string(who == "self" ? "it is" : who + " is");
                if (p == "visible")
                    return (value == "False" ? "hides " : "shows ") + std::string(who == "self" ? "itself" : who);
                if (p == "flip_x")
                    return "flips " + std::string(who == "self" ? "it" : who) + " to face the way it moves";
                if (p == "color")
                    return "changes " + whose + " color";
                if (p == "scale" || p == "scale_x" || p == "scale_y")
                    return "resizes " + std::string(who == "self" ? "it" : who);
                if (p == "position" || p == "x" || p == "y" || p == "z")
                    return "moves " + std::string(who == "self" ? "it" : who) + " to a new spot";
                return "changes " + whose + " " + p;
            }
            if (t->kind == ExprKind::Name) {
                std::string var = symbolName(t->sym);
                if (!var.empty() && var[0] == '_')
                    var = var.substr(1);
                return add ? std::string("updates ") + var : "remembers " + var;
            }
            return "";
        }
        case StmtKind::If: {
            std::string cond = condition(s.expr.get());
            std::vector<std::string> inner;
            for (auto& b : s.body) {
                std::string x = sentence(*b);
                if (!x.empty())
                    inner.push_back(x);
            }
            std::string what = inner.empty() ? "does something" : inner[0];
            for (size_t i = 1; i < inner.size() && i < 3; ++i)
                what += ", then " + inner[i];
            if (inner.size() > 3)
                what += "...";
            return "if " + cond + ", it " + what;
        }
        case StmtKind::For:
        case StmtKind::While: {
            for (auto& b : s.body)
                sentence(*b); // still collect facts
            return s.kind == StmtKind::For ? "goes through " + text(s.expr.get()) + " one at a time" : "repeats while " + condition(s.expr.get());
        }
        default: return "";
        }
    }

    std::string condition(const Expr* e) {
        if (!e)
            return "something is true";
        if (e->kind == ExprKind::Compare && e->b && e->b->kind == ExprKind::String) {
            std::string a = text(e->a.get());
            if (a.size() > 4 && a.substr(a.size() - 4) == ".tag")
                return (e->op == Tok::Eq ? "it's touching a " : "it's not a ") + e->b->text;
            if (a == "key")
                return "the key is " + e->b->text;
            if (a == "name" || a == "message")
                return "the message is \"" + e->b->text + "\"";
        }
        if (e->kind == ExprKind::Call) {
            std::string n = callName(e);
            std::string arg = stringArg(e, 0);
            if (n == "key_pressed")
                return arg + " was just pressed";
            if (n == "key_down")
                return arg + " is held down";
            if (n == "mouse_pressed")
                return "the mouse was clicked";
            if (n == "is_touching")
                return "it's touching " + arg;
        }
        std::string t = text(e);
        if (t == "self.on_ground")
            return "it's on the ground";
        if (t == "not self.on_ground")
            return "it's in the air";
        if (e->kind == ExprKind::And || e->kind == ExprKind::Or)
            return condition(e->a.get()) + (e->kind == ExprKind::And ? " and " : " or ") + condition(e->b.get());
        return t;
    }
};

const char* handlerTitle(const std::string& name) {
    if (name == "on_start") return "When the game starts";
    if (name == "on_update") return "Every frame";
    if (name == "on_fixed_update") return "Every physics step";
    if (name == "on_collide") return "When it bumps into something";
    if (name == "on_collide_end") return "When it stops touching something";
    if (name == "on_trigger") return "When something touches it";
    if (name == "on_trigger_exit") return "When something leaves it";
    if (name == "on_click") return "When it's clicked";
    if (name == "on_key_pressed") return "When a key is pressed";
    if (name == "on_message") return "When it gets a message";
    if (name == "on_destroy") return "Just before it disappears";
    return nullptr;
}

// Blocks read almost like English already: "when space key pressed" -> "if on the ground? then jump with power 14".
std::string blockLabel(const Json& b) {
    const blocks::BlockDef* def = blocks::find(b["type"].asString());
    if (!def)
        return "";
    std::string out = def->label;
    for (auto& in : def->inputs) {
        std::string key = "{" + in.name + "}";
        size_t at = out.find(key);
        if (at == std::string::npos)
            continue;
        const Json& v = b["inputs"][in.name];
        std::string value = v.isObject() ? "(" + blockLabel(v) + ")" : v.isNull() ? in.defaultValue : v.isString() ? v.asString() : num(v.asNumber());
        out.replace(at, key.size(), value);
    }
    return out;
}

void blockSentences(const Json& list, std::vector<std::string>& out, const std::string& indent) {
    for (auto& b : list.elements()) {
        out.push_back(indent + blockLabel(b));
        if (b.contains("body"))
            blockSentences(b["body"], out, indent + "    ");
        if (b.contains("else")) {
            out.push_back(indent + "otherwise:");
            blockSentences(b["else"], out, indent + "    ");
        }
    }
}

} // namespace

ScriptFacts analyzeScript(const std::string& source, const std::string& blocksJson) {
    ScriptFacts facts;
    Program program;
    try {
        program = parse(source);
    } catch (ScriptError& e) {
        facts.error = e.what();
        facts.errorLine = e.line;
        return facts;
    }
    facts.parsed = true;
    Walker w{facts};
    std::function<void(const std::vector<StmtPtr>&)> collect = [&](const std::vector<StmtPtr>& list) {
        for (auto& s : list) {
            w.expr(s->expr.get());
            for (auto& t : s->targets) {
                w.expr(t.get());
                if (t->kind == ExprKind::Attr && t->a && t->a->kind == ExprKind::Name && symbolName(t->a->sym) == "game")
                    facts.gameVarsWritten.insert(symbolName(t->sym));
            }
            collect(s->body);
            collect(s->orelse);
        }
    };
    collect(program.statements);
    for (auto& s : program.statements) {
        if (s->kind != StmtKind::Def)
            continue;
        std::string name = symbolName(s->name);
        facts.functions.insert(name);
        if (!blocksJson.empty())
            continue;
        const char* title = handlerTitle(name);
        std::vector<std::string> lines;
        for (auto& b : s->body) {
            std::string x = w.sentence(*b);
            if (!x.empty())
                lines.push_back(x);
        }
        if (lines.empty())
            continue;
        facts.handlers.push_back({title ? title : "Its " + name + "() function", lines});
    }
    if (!blocksJson.empty()) {
        Json doc = Json::parse(blocksJson);
        for (auto& stack : doc["scripts"].elements()) {
            const Json& list = stack["blocks"];
            if (!list.size())
                continue;
            std::vector<std::string> lines;
            Json rest = Json::array();
            for (size_t i = 1; i < list.size(); ++i)
                rest.push(list[i]);
            blockSentences(rest, lines, "");
            std::string hat = blockLabel(list[0]);
            if (!hat.empty())
                hat[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(hat[0])));
            facts.handlers.push_back({hat, lines});
        }
    }
    return facts;
}

} // namespace aven::editor
