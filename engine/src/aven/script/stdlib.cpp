// Built-in functions available in every EasyScript. Names follow Python where
// Python has one, so skills carry over; game helpers use plain English names.

#include "aven/script/vm.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <random>

namespace aven::script {

namespace {

std::mt19937& rng() {
    static std::mt19937 engine{std::random_device{}()};
    return engine;
}

double requireNumber(const CallArgs& a, size_t i, const char* param) {
    return a.number(i, param);
}

ListObj& requireList(const CallArgs& a, size_t i, const char* param) {
    if (i >= a.size() || !a[i].isList())
        raise(std::string(a.functionName) + "(): '" + param + "' should be a list.");
    return a[i].listObj();
}

std::vector<Value> toItems(VM& vm, const Value& v, const char* fn) {
    if (v.isList())
        return v.listObj().items;
    std::vector<Value> out;
    Value it = vm.iterate(v);
    Value item;
    while (vm.next(it, item))
        out.push_back(item);
    (void)fn;
    return out;
}

struct NamedColor {
    const char* name;
    uint32_t hex;
};
const NamedColor kColors[] = {
    {"red", 0xEF4444},    {"orange", 0xF97316}, {"yellow", 0xFACC15}, {"lime", 0x84CC16},  {"green", 0x22C55E},
    {"teal", 0x14B8A6},   {"cyan", 0x22D3EE},   {"sky", 0x38BDF8},    {"blue", 0x3B82F6},  {"navy", 0x1E3A8A},
    {"purple", 0xA855F7}, {"magenta", 0xD946EF}, {"pink", 0xEC4899},  {"brown", 0x92400E}, {"gold", 0xEAB308},
    {"white", 0xFFFFFF},  {"black", 0x000000},  {"gray", 0x6B7280},   {"grey", 0x6B7280},  {"silver", 0xC0C0C0},
};

} // namespace

// Parses "red", "#ff8800", "#f80" or "ff8800". Returns false if unknown.
bool parseColorName(const std::string& text, double out[4]) {
    std::string s;
    for (char c : text)
        if (!std::isspace(static_cast<unsigned char>(c)))
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    out[3] = 1;
    if (s == "transparent" || s == "clear") {
        out[0] = out[1] = out[2] = out[3] = 0;
        return true;
    }
    for (auto& c : kColors) {
        if (s == c.name) {
            out[0] = ((c.hex >> 16) & 0xFF) / 255.0;
            out[1] = ((c.hex >> 8) & 0xFF) / 255.0;
            out[2] = (c.hex & 0xFF) / 255.0;
            return true;
        }
    }
    if (!s.empty() && s[0] == '#')
        s = s.substr(1);
    if (s.size() == 3)
        s = {s[0], s[0], s[1], s[1], s[2], s[2]};
    if (s.size() != 6 && s.size() != 8)
        return false;
    for (char c : s)
        if (!std::isxdigit(static_cast<unsigned char>(c)))
            return false;
    unsigned long v = std::strtoul(s.c_str(), nullptr, 16);
    if (s.size() == 8) {
        out[3] = (v & 0xFF) / 255.0;
        v >>= 8;
    }
    out[0] = ((v >> 16) & 0xFF) / 255.0;
    out[1] = ((v >> 8) & 0xFF) / 255.0;
    out[2] = (v & 0xFF) / 255.0;
    return true;
}

// Accepts a color value, a color name / hex text, or a list [r, g, b(, a)] in 0..255.
Value toColor(const Value& v, const char* context) {
    if (v.isVec() && v.vecObj().isColor)
        return v;
    if (v.isString()) {
        double c[4];
        if (!parseColorName(v.string(), c))
            raise(std::string(context) + ": I don't know the color \"" + v.string() +
                  "\". Try a name like \"red\" or a code like \"#ff8800\".");
        return Value::color(c[0], c[1], c[2], c[3]);
    }
    if (v.isList() && (v.listObj().items.size() == 3 || v.listObj().items.size() == 4)) {
        auto& items = v.listObj().items;
        return Value::color(items[0].number() / 255.0, items[1].number() / 255.0, items[2].number() / 255.0,
                            items.size() > 3 ? items[3].number() : 1.0);
    }
    raise(std::string(context) + " should be a color, like \"red\" or rgb(255, 0, 0), but got " + v.typeDescription() +
          ".");
}

void VM::registerBuiltins() {
    auto method = [](std::unordered_map<Symbol, Value>& table, const char* name, int minArgs, int maxArgs,
                     std::string sig, NativeFn fn) {
        table[intern(name)] = makeNative(name, std::move(sig), minArgs, maxArgs, std::move(fn));
    };

    // --- list methods (args[0] is the list)
    auto& L = listMethods_;
    method(L, "append", 1, 1, "my_list.append(item)", [](CallArgs& a) {
        a[0].listObj().items.push_back(a[1]);
        return Value();
    });
    method(L, "add", 1, 1, "my_list.add(item)", [](CallArgs& a) {
        a[0].listObj().items.push_back(a[1]);
        return Value();
    });
    method(L, "insert", 2, 2, "my_list.insert(position, item)", [](CallArgs& a) {
        auto& items = a[0].listObj().items;
        long long i = static_cast<long long>(a.number(1, "position"));
        long long n = static_cast<long long>(items.size());
        if (i < 0)
            i += n;
        i = std::clamp(i, 0LL, n);
        items.insert(items.begin() + i, a[2]);
        return Value();
    });
    method(L, "pop", 0, 1, "my_list.pop() or my_list.pop(position)", [](CallArgs& a) {
        auto& items = a[0].listObj().items;
        if (items.empty())
            raise("Can't pop() from an empty list.");
        long long n = static_cast<long long>(items.size());
        long long i = a.has(1) ? static_cast<long long>(a.number(1, "position")) : n - 1;
        if (i < 0)
            i += n;
        if (i < 0 || i >= n)
            raise("pop(): position " + formatNumber(a[1].number()) + " is outside the list.");
        Value v = items[static_cast<size_t>(i)];
        items.erase(items.begin() + i);
        return v;
    });
    method(L, "remove", 1, 1, "my_list.remove(item)", [](CallArgs& a) {
        auto& items = a[0].listObj().items;
        auto it = std::find(items.begin(), items.end(), a[1]);
        if (it == items.end())
            raise("remove(): " + a[1].repr() + " isn't in the list. Check with 'if item in my_list:' first.");
        items.erase(it);
        return Value();
    });
    method(L, "index", 1, 1, "my_list.index(item)", [](CallArgs& a) {
        auto& items = a[0].listObj().items;
        auto it = std::find(items.begin(), items.end(), a[1]);
        return it == items.end() ? Value(-1) : Value(static_cast<double>(it - items.begin()));
    });
    method(L, "count", 1, 1, "my_list.count(item)", [](CallArgs& a) {
        auto& items = a[0].listObj().items;
        return Value(static_cast<double>(std::count(items.begin(), items.end(), a[1])));
    });
    method(L, "clear", 0, 0, "my_list.clear()", [](CallArgs& a) {
        a[0].listObj().items.clear();
        return Value();
    });
    method(L, "extend", 1, 1, "my_list.extend(other_list)", [](CallArgs& a) {
        auto more = toItems(a.vm, a[1], "extend");
        auto& items = a[0].listObj().items;
        items.insert(items.end(), more.begin(), more.end());
        return Value();
    });
    method(L, "reverse", 0, 0, "my_list.reverse()", [](CallArgs& a) {
        std::reverse(a[0].listObj().items.begin(), a[0].listObj().items.end());
        return Value();
    });
    method(L, "copy", 0, 0, "my_list.copy()", [](CallArgs& a) { return Value::list(a[0].listObj().items); });
    method(L, "sort", 0, 0, "my_list.sort(key=None, reverse=False)", [](CallArgs& a) {
        VM& vm = a.vm;
        auto& items = a[0].listObj().items;
        const Value* key = a.keyword("key");
        bool reverse = a.keyword("reverse") && a.keyword("reverse")->truthy();
        std::vector<std::pair<Value, Value>> keyed;
        for (auto& item : items)
            keyed.emplace_back(key && !key->isNone() ? vm.callNow(*key, {item}) : item, item);
        std::stable_sort(keyed.begin(), keyed.end(),
                         [&](auto& x, auto& y) { return reverse ? vm.lessThan(y.first, x.first) : vm.lessThan(x.first, y.first); });
        for (size_t i = 0; i < items.size(); ++i)
            items[i] = keyed[i].second;
        return Value();
    });

    // --- text methods
    auto& S = stringMethods_;
    auto transform = [](int (*fn)(int)) {
        return [fn](CallArgs& a) {
            std::string s = a[0].string();
            for (char& c : s)
                c = static_cast<char>(fn(static_cast<unsigned char>(c)));
            return Value(s);
        };
    };
    method(S, "upper", 0, 0, "text.upper()", transform(::toupper));
    method(S, "lower", 0, 0, "text.lower()", transform(::tolower));
    method(S, "strip", 0, 0, "text.strip()", [](CallArgs& a) {
        const std::string& s = a[0].string();
        size_t b = s.find_first_not_of(" \t\n\r"), e = s.find_last_not_of(" \t\n\r");
        return b == std::string::npos ? Value("") : Value(s.substr(b, e - b + 1));
    });
    method(S, "split", 0, 1, "text.split() or text.split(\",\")", [](CallArgs& a) {
        const std::string& s = a[0].string();
        std::vector<Value> parts;
        if (!a.has(1) || a[1].isNone()) {
            std::string cur;
            for (char c : s) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    if (!cur.empty())
                        parts.emplace_back(cur);
                    cur.clear();
                } else {
                    cur += c;
                }
            }
            if (!cur.empty())
                parts.emplace_back(cur);
        } else {
            const std::string& sep = a.string(1, "separator");
            if (sep.empty())
                raise("split(): the separator can't be empty.");
            size_t start = 0, pos;
            while ((pos = s.find(sep, start)) != std::string::npos) {
                parts.emplace_back(s.substr(start, pos - start));
                start = pos + sep.size();
            }
            parts.emplace_back(s.substr(start));
        }
        return Value::list(std::move(parts));
    });
    method(S, "join", 1, 1, "\", \".join(my_list)", [](CallArgs& a) {
        std::string out;
        auto items = toItems(a.vm, a[1], "join");
        for (size_t i = 0; i < items.size(); ++i) {
            if (i)
                out += a[0].string();
            out += items[i].toString();
        }
        return Value(out);
    });
    method(S, "replace", 2, 2, "text.replace(old, new)", [](CallArgs& a) {
        std::string s = a[0].string();
        const std::string& from = a.string(1, "old");
        const std::string& to = a.string(2, "new");
        if (from.empty())
            return Value(s);
        size_t pos = 0;
        while ((pos = s.find(from, pos)) != std::string::npos) {
            s.replace(pos, from.size(), to);
            pos += to.size();
        }
        return Value(s);
    });
    method(S, "startswith", 1, 1, "text.startswith(prefix)", [](CallArgs& a) {
        return Value(a[0].string().rfind(a.string(1, "prefix"), 0) == 0);
    });
    method(S, "endswith", 1, 1, "text.endswith(suffix)", [](CallArgs& a) {
        const std::string& s = a[0].string();
        const std::string& suf = a.string(1, "suffix");
        return Value(s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0);
    });
    method(S, "find", 1, 1, "text.find(part)", [](CallArgs& a) {
        size_t p = a[0].string().find(a.string(1, "part"));
        return p == std::string::npos ? Value(-1) : Value(static_cast<double>(p));
    });
    method(S, "count", 1, 1, "text.count(part)", [](CallArgs& a) {
        const std::string& s = a[0].string();
        const std::string& part = a.string(1, "part");
        if (part.empty())
            return Value(0);
        int n = 0;
        for (size_t p = s.find(part); p != std::string::npos; p = s.find(part, p + part.size()))
            ++n;
        return Value(n);
    });
    method(S, "isdigit", 0, 0, "text.isdigit()", [](CallArgs& a) {
        const std::string& s = a[0].string();
        return Value(!s.empty() && std::all_of(s.begin(), s.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }));
    });
    method(S, "capitalize", 0, 0, "text.capitalize()", [](CallArgs& a) {
        std::string s = a[0].string();
        if (!s.empty())
            s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
        return Value(s);
    });
    method(S, "title", 0, 0, "text.title()", [](CallArgs& a) {
        std::string s = a[0].string();
        bool start = true;
        for (char& c : s) {
            c = static_cast<char>(start ? std::toupper(static_cast<unsigned char>(c)) : std::tolower(static_cast<unsigned char>(c)));
            start = !std::isalpha(static_cast<unsigned char>(c));
        }
        return Value(s);
    });
    method(S, "zfill", 1, 1, "text.zfill(width)", [](CallArgs& a) {
        std::string s = a[0].string();
        size_t w = static_cast<size_t>(std::max(0.0, a.number(1, "width")));
        if (s.size() < w)
            s = std::string(w - s.size(), '0') + s;
        return Value(s);
    });

    // --- dictionary methods
    auto& D = dictMethods_;
    method(D, "keys", 0, 0, "my_dict.keys()", [](CallArgs& a) {
        std::vector<Value> out;
        for (auto& [k, v] : a[0].dictObj().entries)
            out.push_back(k);
        return Value::list(std::move(out));
    });
    method(D, "values", 0, 0, "my_dict.values()", [](CallArgs& a) {
        std::vector<Value> out;
        for (auto& [k, v] : a[0].dictObj().entries)
            out.push_back(v);
        return Value::list(std::move(out));
    });
    method(D, "items", 0, 0, "my_dict.items()", [](CallArgs& a) {
        std::vector<Value> out;
        for (auto& [k, v] : a[0].dictObj().entries)
            out.push_back(Value::list({k, v}));
        return Value::list(std::move(out));
    });
    method(D, "get", 1, 2, "my_dict.get(key, default)", [](CallArgs& a) {
        Value* v = a[0].dictObj().find(a[1]);
        return v ? *v : (a.has(2) ? a[2] : Value());
    });
    method(D, "pop", 1, 2, "my_dict.pop(key)", [](CallArgs& a) {
        Value* v = a[0].dictObj().find(a[1]);
        if (!v) {
            if (a.has(2))
                return a[2];
            raise("pop(): the dictionary doesn't have the key " + a[1].repr() + ".");
        }
        Value out = *v;
        a[0].dictObj().erase(a[1]);
        return out;
    });
    method(D, "clear", 0, 0, "my_dict.clear()", [](CallArgs& a) {
        a[0].dictObj().entries.clear();
        a[0].dictObj().index.clear();
        return Value();
    });
    method(D, "copy", 0, 0, "my_dict.copy()", [](CallArgs& a) {
        Value d = Value::dict();
        d.dictObj() = a[0].dictObj();
        return d;
    });
    method(D, "update", 1, 1, "my_dict.update(other)", [](CallArgs& a) {
        if (!a[1].isDict())
            raise("update() needs another dictionary.");
        for (auto& [k, v] : a[1].dictObj().entries)
            a[0].dictObj().set(k, v);
        return Value();
    });

    // --- vector methods
    auto& V = vecMethods_;
    method(V, "length", 0, 0, "v.length()", [](CallArgs& a) {
        auto& v = a[0].vecObj();
        return Value(std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]));
    });
    method(V, "normalized", 0, 0, "v.normalized()", [](CallArgs& a) {
        auto& v = a[0].vecObj();
        double l = std::sqrt(v.v[0] * v.v[0] + v.v[1] * v.v[1] + v.v[2] * v.v[2]);
        if (l < 1e-12)
            return Value::vec(0, 0, 0, v.components);
        return Value::vec(v.v[0] / l, v.v[1] / l, v.v[2] / l, v.components);
    });
    method(V, "dot", 1, 1, "v.dot(other)", [](CallArgs& a) {
        if (!a[1].isVec())
            raise("dot() needs another vector.");
        auto& x = a[0].vecObj();
        auto& y = a[1].vecObj();
        return Value(x.v[0] * y.v[0] + x.v[1] * y.v[1] + x.v[2] * y.v[2]);
    });
    method(V, "distance_to", 1, 1, "v.distance_to(other)", [](CallArgs& a) {
        if (!a[1].isVec())
            raise("distance_to() needs another vector.");
        auto& x = a[0].vecObj();
        auto& y = a[1].vecObj();
        double dx = x.v[0] - y.v[0], dy = x.v[1] - y.v[1], dz = x.v[2] - y.v[2];
        return Value(std::sqrt(dx * dx + dy * dy + dz * dz));
    });
}

void registerStdlib(VM& vm) {
    vm.setGlobal("pi", Value(3.14159265358979323846));

    vm.defineFunction("print", "print(value, ...)", 0, -1, [](CallArgs& a) {
        std::string out;
        const Value* sep = a.keyword("sep");
        for (size_t i = 0; i < a.size(); ++i) {
            if (i)
                out += sep ? sep->toString() : " ";
            out += a[i].toString();
        }
        if (a.vm.onPrint)
            a.vm.onPrint(out, a.vm.currentFile(), a.vm.currentLine());
        return Value();
    });

    vm.defineFunction("len", "len(list_or_text)", 1, 1, [](CallArgs& a) {
        const Value& v = a[0];
        switch (v.type()) {
        case Type::List: return Value(v.listObj().items.size());
        case Type::String: return Value(v.string().size());
        case Type::Dict: return Value(v.dictObj().entries.size());
        case Type::Range: return Value(v.as<RangeObj>()->length());
        default: raise("len() works with lists, text and dictionaries, not " + v.typeDescription() + ".");
        }
    });

    vm.defineFunction("range", "range(stop) or range(start, stop) or range(start, stop, step)", 1, 3, [](CallArgs& a) {
        auto r = std::make_shared<RangeObj>();
        if (a.size() == 1) {
            r->stop = a.number(0, "stop");
        } else {
            r->start = a.number(0, "start");
            r->stop = a.number(1, "stop");
            if (a.size() > 2)
                r->step = a.number(2, "step");
        }
        if (r->step == 0)
            raise("range(): the step can't be zero.");
        return Value(Type::Range, r);
    });

    vm.defineFunction("str", "str(value)", 1, 1, [](CallArgs& a) { return Value(a[0].toString()); });

    vm.defineFunction("int", "int(value)", 1, 1, [](CallArgs& a) {
        const Value& v = a[0];
        if (v.isNumber())
            return Value(std::trunc(v.number()));
        if (v.isBool())
            return Value(v.boolean() ? 1 : 0);
        if (v.isString()) {
            const std::string& s = v.string();
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            while (end && *end && std::isspace(static_cast<unsigned char>(*end)))
                ++end;
            if (s.empty() || !end || *end)
                raise("int(): \"" + s + "\" isn't a number.");
            return Value(std::trunc(d));
        }
        raise("int() can't turn " + v.typeDescription() + " into a number.");
    });

    vm.defineFunction("float", "float(value)", 1, 1, [](CallArgs& a) {
        const Value& v = a[0];
        if (v.isNumber())
            return v;
        if (v.isBool())
            return Value(v.boolean() ? 1.0 : 0.0);
        if (v.isString()) {
            const std::string& s = v.string();
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            if (s.empty() || !end || *end)
                raise("float(): \"" + s + "\" isn't a number.");
            return Value(d);
        }
        raise("float() can't turn " + v.typeDescription() + " into a number.");
    });

    vm.defineFunction("bool", "bool(value)", 1, 1, [](CallArgs& a) { return Value(a[0].truthy()); });

    vm.defineFunction("type", "type(value)", 1, 1, [](CallArgs& a) {
        switch (a[0].type()) {
        case Type::None: return Value("none");
        case Type::Bool: return Value("bool");
        case Type::Number: return Value("number");
        case Type::String: return Value("text");
        case Type::List: return Value("list");
        case Type::Dict: return Value("dict");
        case Type::Vec: return Value(a[0].vecObj().isColor ? "color" : "vec");
        case Type::Object: return Value(a[0].nativeObject().typeName());
        default: return Value("function");
        }
    });

    // --- math
    auto math1 = [&vm](const char* name, double (*fn)(double)) {
        vm.defineFunction(name, std::string(name) + "(x)", 1, 1,
                          [fn](CallArgs& a) { return Value(fn(requireNumber(a, 0, "x"))); });
    };
    math1("sqrt", [](double x) {
        if (x < 0)
            raise("sqrt() can't take the square root of a negative number.");
        return std::sqrt(x);
    });
    math1("abs", [](double x) { return std::abs(x); });
    math1("floor", [](double x) { return std::floor(x); });
    math1("ceil", [](double x) { return std::ceil(x); });
    math1("sin", [](double x) { return std::sin(x * 3.14159265358979323846 / 180.0); });
    math1("cos", [](double x) { return std::cos(x * 3.14159265358979323846 / 180.0); });
    math1("tan", [](double x) { return std::tan(x * 3.14159265358979323846 / 180.0); });
    math1("asin", [](double x) { return std::asin(x) * 180.0 / 3.14159265358979323846; });
    math1("acos", [](double x) { return std::acos(x) * 180.0 / 3.14159265358979323846; });
    math1("atan", [](double x) { return std::atan(x) * 180.0 / 3.14159265358979323846; });
    math1("sign", [](double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; });
    math1("exp", [](double x) { return std::exp(x); });
    math1("log", [](double x) { return std::log(x); });
    math1("radians", [](double x) { return x * 3.14159265358979323846 / 180.0; });
    math1("degrees", [](double x) { return x * 180.0 / 3.14159265358979323846; });

    vm.defineFunction("atan2", "atan2(y, x)", 2, 2, [](CallArgs& a) {
        return Value(std::atan2(a.number(0, "y"), a.number(1, "x")) * 180.0 / 3.14159265358979323846);
    });
    vm.defineFunction("pow", "pow(x, power)", 2, 2,
                      [](CallArgs& a) { return Value(std::pow(a.number(0, "x"), a.number(1, "power"))); });
    vm.defineFunction("round", "round(x) or round(x, digits)", 1, 2, [](CallArgs& a) {
        double x = a.number(0, "x");
        if (!a.has(1))
            return Value(std::round(x));
        double f = std::pow(10.0, a.number(1, "digits"));
        return Value(std::round(x * f) / f);
    });
    vm.defineFunction("clamp", "clamp(value, low, high)", 3, 3, [](CallArgs& a) {
        double v = a.number(0, "value"), lo = a.number(1, "low"), hi = a.number(2, "high");
        return Value(std::min(std::max(v, lo), hi));
    });
    vm.defineFunction("lerp", "lerp(a, b, t)", 3, 3, [](CallArgs& a) {
        double t = a.number(2, "t");
        if (a[0].isVec() || a[1].isVec())
            return a.vm.binary(Op::Add, a[0], a.vm.binary(Op::Mul, a.vm.binary(Op::Sub, a[1], a[0]), Value(t)));
        double x = a.number(0, "a"), y = a.number(1, "b");
        return Value(x + (y - x) * t);
    });
    vm.defineFunction("move_toward", "move_toward(current, target, step)", 3, 3, [](CallArgs& a) {
        double c = a.number(0, "current"), t = a.number(1, "target"), s = std::abs(a.number(2, "step"));
        if (std::abs(t - c) <= s)
            return Value(t);
        return Value(c + (t > c ? s : -s));
    });

    auto minmax = [&vm](const char* name, bool wantMax) {
        vm.defineFunction(name, std::string(name) + "(a, b, ...) or " + name + "(my_list)", 1, -1,
                          [wantMax, name](CallArgs& a) {
                              std::vector<Value> items = a.size() == 1 ? toItems(a.vm, a[0], name) : a.args;
                              if (items.empty())
                                  raise(std::string(name) + "() needs at least one value.");
                              Value best = items[0];
                              for (size_t i = 1; i < items.size(); ++i)
                                  if (wantMax ? a.vm.lessThan(best, items[i]) : a.vm.lessThan(items[i], best))
                                      best = items[i];
                              return best;
                          });
    };
    minmax("min", false);
    minmax("max", true);

    vm.defineFunction("sum", "sum(my_list)", 1, 1, [](CallArgs& a) {
        Value total(0);
        for (auto& v : toItems(a.vm, a[0], "sum"))
            total = a.vm.binary(Op::Add, total, v);
        return total;
    });

    // --- random
    vm.defineFunction("random", "random()", 0, 0, [](CallArgs&) {
        return Value(std::uniform_real_distribution<double>(0.0, 1.0)(rng()));
    });
    vm.defineFunction("random_range", "random_range(low, high)", 2, 2, [](CallArgs& a) {
        double lo = a.number(0, "low"), hi = a.number(1, "high");
        if (hi < lo)
            std::swap(lo, hi);
        return Value(lo == hi ? lo : std::uniform_real_distribution<double>(lo, hi)(rng()));
    });
    vm.defineFunction("random_int", "random_int(low, high)", 2, 2, [](CallArgs& a) {
        long long lo = static_cast<long long>(std::floor(a.number(0, "low")));
        long long hi = static_cast<long long>(std::floor(a.number(1, "high")));
        if (hi < lo)
            std::swap(lo, hi);
        return Value(static_cast<double>(std::uniform_int_distribution<long long>(lo, hi)(rng())));
    });
    vm.defineFunction("choice", "choice(my_list)", 1, 1, [](CallArgs& a) {
        auto items = toItems(a.vm, a[0], "choice");
        if (items.empty())
            raise("choice() can't pick from an empty list.");
        return items[std::uniform_int_distribution<size_t>(0, items.size() - 1)(rng())];
    });
    vm.defineFunction("shuffle", "shuffle(my_list)", 1, 1, [](CallArgs& a) {
        auto& items = requireList(a, 0, "my_list").items;
        std::shuffle(items.begin(), items.end(), rng());
        return Value();
    });
    vm.defineFunction("random_seed", "random_seed(number)", 1, 1, [](CallArgs& a) {
        rng().seed(static_cast<unsigned>(a.number(0, "number")));
        return Value();
    });

    // --- collections
    vm.defineFunction("list", "list(things)", 0, 1, [](CallArgs& a) {
        return a.size() ? Value::list(toItems(a.vm, a[0], "list")) : Value::list();
    });
    vm.defineFunction("dict", "dict()", 0, 0, [](CallArgs&) { return Value::dict(); });
    vm.defineFunction("sorted", "sorted(my_list, key=None, reverse=False)", 1, 1, [](CallArgs& a) {
        VM& v = a.vm;
        auto items = toItems(v, a[0], "sorted");
        const Value* key = a.keyword("key");
        bool reverse = a.keyword("reverse") && a.keyword("reverse")->truthy();
        std::vector<std::pair<Value, Value>> keyed;
        for (auto& item : items)
            keyed.emplace_back(key && !key->isNone() ? v.callNow(*key, {item}) : item, item);
        std::stable_sort(keyed.begin(), keyed.end(),
                         [&](auto& x, auto& y) { return reverse ? v.lessThan(y.first, x.first) : v.lessThan(x.first, y.first); });
        std::vector<Value> out;
        for (auto& k : keyed)
            out.push_back(k.second);
        return Value::list(std::move(out));
    });
    vm.defineFunction("reversed", "reversed(my_list)", 1, 1, [](CallArgs& a) {
        auto items = toItems(a.vm, a[0], "reversed");
        std::reverse(items.begin(), items.end());
        return Value::list(std::move(items));
    });

    // --- vectors and colors
    vm.defineFunction("vec", "vec(x, y) or vec(x, y, z)", 2, 3, [](CallArgs& a) {
        return Value::vec(a.number(0, "x"), a.number(1, "y"), a.numberOr(2, "z", 0), static_cast<int>(a.size()));
    });
    vm.defineFunction("rgb", "rgb(red, green, blue) with values from 0 to 255", 3, 4, [](CallArgs& a) {
        return Value::color(a.number(0, "red") / 255.0, a.number(1, "green") / 255.0, a.number(2, "blue") / 255.0,
                            a.numberOr(3, "alpha", 1.0));
    });
    vm.defineFunction("color", "color(\"red\") or color(\"#ff8800\")", 1, 1,
                      [](CallArgs& a) { return toColor(a[0], "color()"); });
    vm.defineFunction("hsv", "hsv(hue 0-360, saturation 0-1, value 0-1)", 3, 4, [](CallArgs& a) {
        double h = std::fmod(a.number(0, "hue"), 360.0), s = a.number(1, "saturation"), v = a.number(2, "value");
        if (h < 0)
            h += 360;
        double c = v * s, x = c * (1 - std::abs(std::fmod(h / 60.0, 2.0) - 1)), m = v - c;
        double r = 0, g = 0, b = 0;
        if (h < 60) { r = c; g = x; }
        else if (h < 120) { r = x; g = c; }
        else if (h < 180) { g = c; b = x; }
        else if (h < 240) { g = x; b = c; }
        else if (h < 300) { r = x; b = c; }
        else { r = c; b = x; }
        return Value::color(r + m, g + m, b + m, a.numberOr(3, "alpha", 1.0));
    });

    // --- time and waiting
    vm.defineFunction("wait", "wait(seconds)", 0, 1, [](CallArgs& a) {
        a.vm.wait(a.numberOr(0, "seconds", 0));
        return Value();
    });
    vm.defineFunction("after", "after(seconds, function)", 2, -1, [](CallArgs& a) {
        if (!a[1].isCallable())
            raise("after(): the second value should be a function name (without the parentheses), like: after(2, explode)");
        std::vector<Value> extra(a.args.begin() + 2, a.args.end());
        return Value(a.vm.addTimer(a.number(0, "seconds"), 0, a[1], std::move(extra)));
    });
    vm.defineFunction("every", "every(seconds, function)", 2, -1, [](CallArgs& a) {
        if (!a[1].isCallable())
            raise("every(): the second value should be a function name (without the parentheses), like: every(1, spawn_enemy)");
        double interval = a.number(0, "seconds");
        if (interval <= 0)
            raise("every(): the time between calls must be more than 0 seconds.");
        std::vector<Value> extra(a.args.begin() + 2, a.args.end());
        return Value(a.vm.addTimer(interval, interval, a[1], std::move(extra)));
    });
    vm.defineFunction("stop_timer", "stop_timer(timer)", 1, 1,
                      [](CallArgs& a) { return Value(a.vm.stopTimer(static_cast<int>(a.number(0, "timer")))); });
}

} // namespace aven::script
