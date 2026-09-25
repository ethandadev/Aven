#pragma once

// EasyScript values. Numbers are doubles (like Python floats, but whole numbers
// print without ".0"); strings, lists and dicts are reference types.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aven::script {

using Symbol = uint32_t;
Symbol intern(std::string_view name);
const std::string& symbolName(Symbol s);

enum class Type : uint8_t {
    None,
    Bool,
    Number,
    String,
    List,
    Dict,
    Vec,
    Range,
    Function,
    NativeFunction,
    BoundMethod,
    Iterator,
    Object,
};

struct Obj {
    virtual ~Obj() = default;
};

struct ListObj;
struct DictObj;
struct VecObj;
struct NativeObject;

class Value {
public:
    Value() : n_(0) {}
    Value(std::nullptr_t) : n_(0) {}
    Value(bool b) : type_(Type::Bool), b_(b) {}
    Value(double n) : type_(Type::Number), n_(n) {}
    Value(float n) : type_(Type::Number), n_(n) {}
    Value(int n) : type_(Type::Number), n_(n) {}
    Value(int64_t n) : type_(Type::Number), n_(static_cast<double>(n)) {}
    Value(size_t n) : type_(Type::Number), n_(static_cast<double>(n)) {}
    Value(const char* s);
    Value(std::string s);
    Value(std::string_view s);
    Value(Type t, std::shared_ptr<Obj> obj) : type_(t), n_(0), obj_(std::move(obj)) {}

    static Value list(std::vector<Value> items = {});
    static Value dict();
    static Value vec(double x, double y, double z = 0, int components = 3);
    static Value color(double r, double g, double b, double a = 1);
    static Value object(std::shared_ptr<NativeObject> obj);

    Type type() const { return type_; }
    bool isNone() const { return type_ == Type::None; }
    bool isBool() const { return type_ == Type::Bool; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isString() const { return type_ == Type::String; }
    bool isList() const { return type_ == Type::List; }
    bool isDict() const { return type_ == Type::Dict; }
    bool isVec() const { return type_ == Type::Vec; }
    bool isObject() const { return type_ == Type::Object; }
    bool isCallable() const {
        return type_ == Type::Function || type_ == Type::NativeFunction || type_ == Type::BoundMethod;
    }

    bool boolean() const { return b_; }
    double number() const { return n_; }
    const std::string& string() const;
    ListObj& listObj() const;
    DictObj& dictObj() const;
    VecObj& vecObj() const;
    NativeObject& nativeObject() const;
    template <class T> T* as() const { return static_cast<T*>(obj_.get()); }
    const std::shared_ptr<Obj>& obj() const { return obj_; }

    bool truthy() const;
    // User-facing text: print(x), str(x), f-strings.
    std::string toString() const;
    // Debug text: strings are quoted (used inside lists and error messages).
    std::string repr() const;
    // Friendly type name for error messages: "a number", "some text", "a list".
    std::string typeDescription() const;
    static std::string typeDescription(Type t);

    bool operator==(const Value& other) const;
    bool operator!=(const Value& other) const { return !(*this == other); }

    // Stable key for dict lookups; empty if the value can't be a dict key.
    std::string hashKey() const;

private:
    Type type_ = Type::None;
    union {
        bool b_;
        double n_;
    };
    std::shared_ptr<Obj> obj_;
};

std::string formatNumber(double n);

struct StringObj : Obj {
    std::string value;
    explicit StringObj(std::string s) : value(std::move(s)) {}
};

struct ListObj : Obj {
    std::vector<Value> items;
};

struct DictObj : Obj {
    std::vector<std::pair<Value, Value>> entries;
    std::unordered_map<std::string, size_t> index;

    Value* find(const Value& key);
    // Returns false if the key type can't be used as a dict key.
    bool set(const Value& key, Value value);
    bool erase(const Value& key);
};

struct VecObj : Obj {
    double v[4] = {0, 0, 0, 1};
    int components = 3;
    bool isColor = false;
};

struct RangeObj : Obj {
    double start = 0, stop = 0, step = 1;
    size_t length() const;
    double at(size_t i) const { return start + step * static_cast<double>(i); }
};

struct IteratorObj : Obj {
    Value source;
    size_t index = 0;
    std::vector<Value> snapshot; // dict keys, captured when iteration starts
};

class VM;

struct CallArgs {
    VM& vm;
    const char* functionName;
    std::vector<Value> args;
    std::vector<std::pair<Symbol, Value>> kwargs;

    size_t size() const { return args.size(); }
    const Value& operator[](size_t i) const { return args[i]; }
    bool has(size_t i) const { return i < args.size(); }

    // Typed accessors raise friendly errors that name the function and parameter.
    double number(size_t i, const char* param) const;
    const std::string& string(size_t i, const char* param) const;
    double numberOr(size_t i, const char* param, double fallback) const;
    const Value* keyword(std::string_view name) const;
    double keywordNumber(std::string_view name, double fallback) const;
};

using NativeFn = std::function<Value(CallArgs&)>;

struct NativeFunctionObj : Obj {
    std::string name;
    std::string signature; // e.g. "key_down(key)" for error messages and docs
    int minArgs = 0;
    int maxArgs = -1; // -1 = any
    NativeFn fn;
};

struct BoundMethodObj : Obj {
    Value self;
    Value method;
};

// Base for engine objects exposed to scripts (entities, components, the game object...).
struct NativeObject : Obj {
    virtual std::string typeName() const = 0;
    virtual bool getAttr(VM& vm, const std::string& name, Value& out) { return false; }
    virtual bool setAttr(VM& vm, const std::string& name, const Value& value) { return false; }
    virtual std::vector<std::string> attrNames() const { return {}; }
    virtual std::string repr() const { return "<" + typeName() + ">"; }
    virtual bool equals(const NativeObject& other) const { return this == &other; }
    virtual std::string hashKey() const { return ""; }
    virtual bool truthy() const { return true; }
};

Value makeNative(std::string name, std::string signature, int minArgs, int maxArgs, NativeFn fn);

} // namespace aven::script
