#include "aven/blocks/blocks.h"

#include "aven/script/value.h"

#include <cctype>
#include <cstdlib>
#include <map>
#include <sstream>

namespace aven::blocks {

namespace {

std::string quote(const std::string& s) {
    return script::Value(s).repr();
}

std::string identifier(const std::string& raw, const std::string& fallback) {
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '_')
            out += c;
        else if (c == ' ' || c == '-')
            out += '_';
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])))
        out = fallback + out;
    return out;
}

bool parseNumber(const std::string& s, double& out) {
    if (s.empty())
        return false;
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && *end == '\0';
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::vector<std::string> splitLines(const std::string& s) {
    std::vector<std::string> lines;
    std::stringstream ss(s);
    std::string line;
    while (std::getline(ss, line))
        lines.push_back(line);
    return lines;
}

class Compiler {
public:
    std::string error;

    std::string value(const Input& in, const Json& v) {
        if (v.isObject() && v.contains("type"))
            return reporter(v);
        bool missing = v.isNull();
        switch (in.type) {
        case InputType::Number: {
            double n;
            if (v.isNumber())
                return script::formatNumber(v.asNumber());
            std::string s = missing ? in.defaultValue : v.asString();
            if (parseNumber(s, n))
                return script::formatNumber(n);
            return s.empty() ? "0" : quote(s);
        }
        case InputType::Condition:
            if (v.isBool())
                return v.asBool() ? "True" : "False";
            return "False";
        case InputType::Value: {
            if (missing)
                return in.defaultValue.empty() ? "0" : in.defaultValue;
            if (v.isNumber())
                return script::formatNumber(v.asNumber());
            if (v.isBool())
                return v.asBool() ? "True" : "False";
            double n;
            if (parseNumber(v.asString(), n))
                return script::formatNumber(n);
            return quote(v.asString());
        }
        case InputType::Variable:
        case InputType::Name: return identifier(missing ? in.defaultValue : v.asString(), "v");
        default: {
            std::string s = missing ? in.defaultValue : (v.isString() ? v.asString() : v.dump());
            return quote(s);
        }
        }
    }

    std::string fill(const BlockDef& def, const Json& block) {
        std::string code = def.code;
        const Json& inputs = block["inputs"];
        for (auto& in : def.inputs)
            code = replaceAll(code, "{" + in.name + "}", value(in, inputs[in.name]));
        return code;
    }

    std::string reporter(const Json& block) {
        const BlockDef* def = find(block["type"].asString());
        if (!def) {
            error = "unknown block '" + block["type"].asString() + "'";
            return "0";
        }
        if (def->shape != Shape::Reporter && def->shape != Shape::Boolean) {
            error = "the '" + def->id + "' block can't be used as a value";
            return "0";
        }
        return fill(*def, block);
    }

    void statements(const Json& list, int indent, std::vector<std::string>& out) {
        size_t before = out.size();
        for (auto& block : list.elements())
            statement(block, indent, out);
        if (out.size() == before)
            out.push_back(pad(indent) + "pass");
    }

    static std::string pad(int indent) { return std::string(static_cast<size_t>(indent) * 4, ' '); }

    void statement(const Json& block, int indent, std::vector<std::string>& out) {
        const BlockDef* def = find(block["type"].asString());
        if (!def) {
            out.push_back(pad(indent) + "# (unknown block '" + block["type"].asString() + "')");
            return;
        }
        if (def->shape == Shape::Hat)
            return;
        if (def->shape == Shape::Reporter || def->shape == Shape::Boolean) {
            out.push_back(pad(indent) + fill(*def, block));
            return;
        }
        std::string code = fill(*def, block);
        for (auto& line : splitLines(code))
            out.push_back(pad(indent) + line);
        if (def->shape == Shape::CBlock || def->shape == Shape::IfElse) {
            size_t before = out.size();
            for (auto& inner : block["body"].elements())
                statement(inner, indent + 1, out);
            if (def->loopWaits)
                out.push_back(pad(indent + 1) + "wait()  # let the game draw a frame");
            if (out.size() == before)
                out.push_back(pad(indent + 1) + "pass");
            if (def->shape == Shape::IfElse) {
                out.push_back(pad(indent) + "else:");
                statements(block["else"], indent + 1, out);
            }
        }
    }
};

bool usesClones(const Json& blocks) {
    for (auto& b : blocks.elements()) {
        std::string type = b["type"].asString();
        if (type == "clone" || type == "when_clone")
            return true;
        if (usesClones(b["body"]) || usesClones(b["else"]))
            return true;
    }
    return false;
}

std::string functionName(const std::string& event) {
    size_t p = event.find('(');
    return p == std::string::npos ? event : event.substr(0, p);
}

std::string parameters(const std::string& event) {
    size_t a = event.find('('), b = event.find(')');
    if (a == std::string::npos || b == std::string::npos)
        return "";
    return event.substr(a + 1, b - a - 1);
}

} // namespace

Json emptyDocument() {
    Json doc = Json::object();
    doc["aven"] = "blocks";
    doc["version"] = 1;
    doc["variables"] = Json::array();
    doc["scripts"] = Json::array();
    return doc;
}

std::string compile(const Json& doc, std::string* error) {
    if (!doc.isObject() || !doc["scripts"].isArray()) {
        if (error)
            *error = "this isn't a blocks file";
        return "";
    }
    Compiler c;
    bool clones = false;
    for (auto& s : doc["scripts"].elements())
        clones = clones || usesClones(s["blocks"]);

    std::vector<std::string> out;
    out.push_back("# Made with blocks. Reading this is a great way to learn EasyScript!");
    out.push_back("is_clone = False");
    for (auto& v : doc["variables"].elements()) {
        std::string name = identifier(v["name"].asString(), "v");
        const Json& val = v["value"];
        std::string literal = val.isNumber() ? script::formatNumber(val.asNumber())
                              : val.isBool() ? (val.asBool() ? "True" : "False")
                                             : quote(val.asString());
        out.push_back(name + " = " + literal);
    }

    // Group hats by the callback they belong to.
    struct Hat {
        std::string filter;
        std::vector<std::string> body;
    };
    std::map<std::string, std::string> signatures;
    std::map<std::string, std::vector<Hat>> groups;
    std::vector<std::string> groupOrder;
    std::vector<std::string> timerLines;
    std::vector<std::vector<std::string>> timerBodies;

    for (auto& script : doc["scripts"].elements()) {
        const Json& stack = script["blocks"];
        if (!stack.isArray() || stack.size() == 0)
            continue;
        const Json& hatBlock = stack[0];
        const BlockDef* def = find(hatBlock["type"].asString());
        if (!def || def->shape != Shape::Hat)
            continue; // loose blocks don't run, like in Scratch
        std::vector<std::string> body;
        for (size_t i = 1; i < stack.size(); ++i)
            c.statement(stack[i], 1, body);

        if (def->event == "@timer") {
            std::string name = "_every_" + std::to_string(timerBodies.size() + 1);
            std::string seconds = c.value(def->inputs[0], hatBlock["inputs"][def->inputs[0].name]);
            timerLines.push_back("    every(" + seconds + ", " + name + ")");
            timerBodies.push_back(body);
            continue;
        }
        std::string fname = functionName(def->event);
        std::string filter = def->filter;
        for (auto& in : def->inputs)
            filter = replaceAll(filter, "{" + in.name + "}", c.value(in, hatBlock["inputs"][in.name]));
        if (def->id == "when_start" && !clones)
            filter.clear();
        if (def->id == "when_touch") {
            std::string tag = hatBlock["inputs"]["tag"].asString("anything");
            if (tag.empty() || tag == "anything")
                filter.clear();
        }
        if (!groups.count(fname)) {
            groupOrder.push_back(fname);
            signatures[fname] = def->event;
        }
        groups[fname].push_back({filter, body});
    }
    if (!timerLines.empty() && !groups.count("on_start")) {
        groupOrder.insert(groupOrder.begin(), "on_start");
        signatures["on_start"] = "on_start()";
        groups["on_start"];
    }

    for (size_t i = 0; i < timerBodies.size(); ++i) {
        out.push_back("");
        out.push_back("def _every_" + std::to_string(i + 1) + "():");
        for (auto& l : timerBodies[i])
            out.push_back(l);
        if (timerBodies[i].empty())
            out.push_back("    pass");
    }

    for (auto& fname : groupOrder) {
        auto& hats = groups[fname];
        std::string params = parameters(signatures[fname]);
        bool timers = fname == "on_start" && !timerLines.empty();
        if (hats.size() == 1 && !timers) {
            // The simple case produces the code a person would write.
            out.push_back("");
            out.push_back("def " + signatures[fname] + ":");
            Hat& h = hats[0];
            if (h.filter.empty()) {
                for (auto& l : h.body)
                    out.push_back(l);
                if (h.body.empty())
                    out.push_back("    pass");
            } else {
                out.push_back("    if " + h.filter + ":");
                for (auto& l : h.body)
                    out.push_back("    " + l);
                if (h.body.empty())
                    out.push_back("        pass");
            }
            continue;
        }
        // Several hats for the same event run side by side, like separate Scratch scripts.
        for (size_t i = 0; i < hats.size(); ++i) {
            out.push_back("");
            out.push_back("def _" + fname + "_" + std::to_string(i + 1) + "(" + params + "):");
            for (auto& l : hats[i].body)
                out.push_back(l);
            if (hats[i].body.empty())
                out.push_back("    pass");
        }
        out.push_back("");
        out.push_back("def " + signatures[fname] + ":");
        if (timers)
            for (auto& l : timerLines)
                out.push_back(l);
        for (size_t i = 0; i < hats.size(); ++i) {
            std::string call = "start_task(_" + fname + "_" + std::to_string(i + 1) + (params.empty() ? "" : ", " + params) + ")";
            if (hats[i].filter.empty()) {
                out.push_back("    " + call);
            } else {
                out.push_back("    if " + hats[i].filter + ":");
                out.push_back("        " + call);
            }
        }
        if (hats.empty() && !timers)
            out.push_back("    pass");
    }

    if (error)
        *error = c.error;
    std::string source;
    for (auto& l : out)
        source += l + "\n";
    return source;
}

std::string compileFile(const std::string& text, std::string* error) {
    std::string parseError;
    Json doc = Json::parse(text, &parseError);
    if (!parseError.empty()) {
        if (error)
            *error = parseError;
        return "";
    }
    return compile(doc, error);
}

} // namespace aven::blocks
