#include "aven/script/value.h"

#include "aven/script/errors.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace aven::script {

// ---------------------------------------------------------------- symbols

namespace {
struct Interner {
    std::mutex mutex;
    std::unordered_map<std::string, Symbol> ids;
    std::vector<std::unique_ptr<std::string>> names;
};
Interner& interner() {
    static Interner i;
    return i;
}
} // namespace

Symbol intern(std::string_view name) {
    auto& in = interner();
    std::lock_guard lock(in.mutex);
    auto it = in.ids.find(std::string(name));
    if (it != in.ids.end())
        return it->second;
    Symbol id = static_cast<Symbol>(in.names.size());
    in.names.push_back(std::make_unique<std::string>(name));
    in.ids.emplace(std::string(name), id);
    return id;
}

const std::string& symbolName(Symbol s) {
    auto& in = interner();
    std::lock_guard lock(in.mutex);
    return *in.names.at(s);
}

// ---------------------------------------------------------------- errors

void raise(const std::string& message) {
    throw ScriptError(message);
}

static size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> prev(b.size() + 1), cur(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j)
        prev[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        cur[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            size_t cost = std::tolower(static_cast<unsigned char>(a[i - 1])) ==
                                  std::tolower(static_cast<unsigned char>(b[j - 1]))
                              ? 0
                              : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[b.size()];
}

std::string closestMatch(const std::string& word, const std::vector<std::string>& candidates) {
    std::string best;
    size_t bestDistance = std::max<size_t>(1, word.size() / 3 + 1);
    for (auto& c : candidates) {
        if (c.empty() || c[0] == '_')
            continue;
        size_t d = editDistance(word, c);
        if (d < bestDistance || (d == bestDistance && best.empty() && d <= 2)) {
            best = c;
            bestDistance = d;
        }
    }
    return best;
}

std::string didYouMean(const std::string& word, const std::vector<std::string>& candidates) {
    std::string match = closestMatch(word, candidates);
    return match.empty() ? "" : " Did you mean '" + match + "'?";
}

// ---------------------------------------------------------------- values

Value::Value(const char* s) : type_(Type::String), n_(0), obj_(std::make_shared<StringObj>(s)) {}
Value::Value(std::string s) : type_(Type::String), n_(0), obj_(std::make_shared<StringObj>(std::move(s))) {}
Value::Value(std::string_view s) : type_(Type::String), n_(0), obj_(std::make_shared<StringObj>(std::string(s))) {}

Value Value::list(std::vector<Value> items) {
    auto l = std::make_shared<ListObj>();
    l->items = std::move(items);
    return Value(Type::List, std::move(l));
}

Value Value::dict() {
    return Value(Type::Dict, std::make_shared<DictObj>());
}

Value Value::vec(double x, double y, double z, int components) {
    auto v = std::make_shared<VecObj>();
    v->v[0] = x;
    v->v[1] = y;
    v->v[2] = z;
    v->components = components;
    return Value(Type::Vec, std::move(v));
}

Value Value::color(double r, double g, double b, double a) {
    auto v = std::make_shared<VecObj>();
    v->v[0] = r;
    v->v[1] = g;
    v->v[2] = b;
    v->v[3] = a;
    v->components = 4;
    v->isColor = true;
    return Value(Type::Vec, std::move(v));
}

Value Value::object(std::shared_ptr<NativeObject> obj) {
    return Value(Type::Object, std::move(obj));
}

const std::string& Value::string() const {
    static const std::string empty;
    return type_ == Type::String ? static_cast<StringObj*>(obj_.get())->value : empty;
}
ListObj& Value::listObj() const { return *static_cast<ListObj*>(obj_.get()); }
DictObj& Value::dictObj() const { return *static_cast<DictObj*>(obj_.get()); }
VecObj& Value::vecObj() const { return *static_cast<VecObj*>(obj_.get()); }
NativeObject& Value::nativeObject() const { return *static_cast<NativeObject*>(obj_.get()); }

bool Value::truthy() const {
    switch (type_) {
    case Type::None: return false;
    case Type::Bool: return b_;
    case Type::Number: return n_ != 0;
    case Type::String: return !string().empty();
    case Type::List: return !listObj().items.empty();
    case Type::Dict: return !dictObj().entries.empty();
    case Type::Range: return as<RangeObj>()->length() > 0;
    case Type::Object: return nativeObject().truthy();
    default: return true;
    }
}

std::string formatNumber(double n) {
    if (std::isnan(n))
        return "nan";
    if (std::isinf(n))
        return n > 0 ? "inf" : "-inf";
    if (n == std::floor(n) && std::abs(n) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", n);
        return std::strcmp(buf, "-0") == 0 ? "0" : buf;
    }
    char buf[40];
    for (int precision = 15; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, n);
        if (std::strtod(buf, nullptr) == n)
            break;
    }
    return buf;
}

std::string Value::toString() const {
    switch (type_) {
    case Type::None: return "None";
    case Type::Bool: return b_ ? "True" : "False";
    case Type::Number: return formatNumber(n_);
    case Type::String: return string();
    default: return repr();
    }
}

std::string Value::repr() const {
    switch (type_) {
    case Type::String: {
        std::string out = "\"";
        for (char c : string()) {
            if (c == '"' || c == '\\')
                out += '\\';
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out += c;
        }
        return out + "\"";
    }
    case Type::List: {
        std::string out = "[";
        auto& items = listObj().items;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i)
                out += ", ";
            out += items[i].obj_.get() == obj_.get() ? "[...]" : items[i].repr();
        }
        return out + "]";
    }
    case Type::Dict: {
        std::string out = "{";
        auto& entries = dictObj().entries;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i)
                out += ", ";
            out += entries[i].first.repr() + ": " +
                   (entries[i].second.obj_.get() == obj_.get() ? "{...}" : entries[i].second.repr());
        }
        return out + "}";
    }
    case Type::Vec: {
        auto& v = vecObj();
        std::string out = v.isColor ? "rgb(" : "vec(";
        for (int i = 0; i < v.components; ++i) {
            if (i)
                out += ", ";
            out += formatNumber(v.isColor && i < 3 ? std::round(v.v[i] * 255.0) : v.v[i]);
        }
        return out + ")";
    }
    case Type::Range: {
        auto* r = as<RangeObj>();
        return "range(" + formatNumber(r->start) + ", " + formatNumber(r->stop) + ")";
    }
    case Type::Function: return "<function>";
    case Type::NativeFunction: return "<function " + as<NativeFunctionObj>()->name + ">";
    case Type::BoundMethod: return "<method>";
    case Type::Iterator: return "<iterator>";
    case Type::Object: return nativeObject().repr();
    default: return toString();
    }
}

std::string Value::typeDescription(Type t) {
    switch (t) {
    case Type::None: return "nothing (None)";
    case Type::Bool: return "True/False";
    case Type::Number: return "a number";
    case Type::String: return "text";
    case Type::List: return "a list";
    case Type::Dict: return "a dictionary";
    case Type::Vec: return "a vector";
    case Type::Range: return "a range";
    case Type::Function:
    case Type::NativeFunction:
    case Type::BoundMethod: return "a function";
    case Type::Iterator: return "an iterator";
    case Type::Object: return "an object";
    }
    return "a value";
}

std::string Value::typeDescription() const {
    if (type_ == Type::Vec && vecObj().isColor)
        return "a color";
    if (type_ == Type::Object) {
        std::string name = nativeObject().typeName();
        bool vowel = !name.empty() && std::strchr("aeiouAEIOU", name[0]);
        return (vowel ? "an " : "a ") + name;
    }
    return typeDescription(type_);
}

bool Value::operator==(const Value& o) const {
    if (type_ != o.type_) {
        return false;
    }
    switch (type_) {
    case Type::None: return true;
    case Type::Bool: return b_ == o.b_;
    case Type::Number: return n_ == o.n_;
    case Type::String: return string() == o.string();
    case Type::List: {
        auto& a = listObj().items;
        auto& b = o.listObj().items;
        if (&a == &b)
            return true;
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }
    case Type::Dict: {
        auto& a = dictObj();
        auto& b = o.dictObj();
        if (&a == &b)
            return true;
        if (a.entries.size() != b.entries.size())
            return false;
        for (auto& [k, v] : a.entries) {
            Value* other = b.find(k);
            if (!other || *other != v)
                return false;
        }
        return true;
    }
    case Type::Vec: {
        auto& a = vecObj();
        auto& b = o.vecObj();
        for (int i = 0; i < 4; ++i)
            if (a.v[i] != b.v[i])
                return false;
        return a.components == b.components;
    }
    case Type::Object: return nativeObject().equals(o.nativeObject());
    default: return obj_ == o.obj_;
    }
}

std::string Value::hashKey() const {
    switch (type_) {
    case Type::None: return "z";
    case Type::Bool: return b_ ? "n1" : "n0"; // True == 1 as a key, like Python
    case Type::Number: {
        char buf[48];
        std::snprintf(buf, sizeof buf, "n%.17g", n_);
        return buf;
    }
    case Type::String: return "s" + string();
    case Type::Object: {
        std::string k = nativeObject().hashKey();
        return k.empty() ? "" : "o" + k;
    }
    default: return "";
    }
}

Value* DictObj::find(const Value& key) {
    auto it = index.find(key.hashKey());
    return it == index.end() ? nullptr : &entries[it->second].second;
}

bool DictObj::set(const Value& key, Value value) {
    std::string k = key.hashKey();
    if (k.empty())
        return false;
    auto it = index.find(k);
    if (it != index.end()) {
        entries[it->second].second = std::move(value);
    } else {
        index.emplace(k, entries.size());
        entries.emplace_back(key, std::move(value));
    }
    return true;
}

bool DictObj::erase(const Value& key) {
    auto it = index.find(key.hashKey());
    if (it == index.end())
        return false;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(it->second));
    index.clear();
    for (size_t i = 0; i < entries.size(); ++i)
        index.emplace(entries[i].first.hashKey(), i);
    return true;
}

size_t RangeObj::length() const {
    if (step == 0)
        return 0;
    double n = std::ceil((stop - start) / step);
    return n > 0 ? static_cast<size_t>(n) : 0;
}

// ---------------------------------------------------------------- native calls

double CallArgs::number(size_t i, const char* param) const {
    if (i >= args.size())
        raise(std::string(functionName) + "() is missing its '" + param + "' value.");
    const Value& v = args[i];
    if (v.isNumber())
        return v.number();
    if (v.isBool())
        return v.boolean() ? 1 : 0;
    raise(std::string(functionName) + "(): '" + param + "' should be a number, but it is " + v.typeDescription() +
          " (" + v.repr() + ").");
}

const std::string& CallArgs::string(size_t i, const char* param) const {
    if (i >= args.size())
        raise(std::string(functionName) + "() is missing its '" + param + "' value.");
    const Value& v = args[i];
    if (!v.isString())
        raise(std::string(functionName) + "(): '" + param + "' should be text in quotes, like \"" + param +
              "\", but it is " + v.typeDescription() + ".");
    return v.string();
}

double CallArgs::numberOr(size_t i, const char* param, double fallback) const {
    if (i >= args.size() || args[i].isNone())
        return fallback;
    return number(i, param);
}

const Value* CallArgs::keyword(std::string_view name) const {
    for (auto& [sym, v] : kwargs)
        if (symbolName(sym) == name)
            return &v;
    return nullptr;
}

double CallArgs::keywordNumber(std::string_view name, double fallback) const {
    const Value* v = keyword(name);
    if (!v)
        return fallback;
    if (!v->isNumber())
        raise(std::string(functionName) + "(): '" + std::string(name) + "' should be a number.");
    return v->number();
}

Value makeNative(std::string name, std::string signature, int minArgs, int maxArgs, NativeFn fn) {
    auto f = std::make_shared<NativeFunctionObj>();
    f->name = std::move(name);
    f->signature = std::move(signature);
    f->minArgs = minArgs;
    f->maxArgs = maxArgs;
    f->fn = std::move(fn);
    return Value(Type::NativeFunction, std::move(f));
}

} // namespace aven::script
