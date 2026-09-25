#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace aven {

struct JsonMember;

// A JSON value. Objects keep insertion order so saved scene files diff cleanly.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : value_(b) {}
    Json(double n) : value_(n) {}
    Json(float n) : value_(static_cast<double>(n)) {}
    Json(int n) : value_(static_cast<double>(n)) {}
    Json(int64_t n) : value_(static_cast<double>(n)) {}
    Json(uint32_t n) : value_(static_cast<double>(n)) {}
    Json(const char* s) : value_(std::string(s)) {}
    Json(std::string s) : value_(std::move(s)) {}
    Json(std::string_view s) : value_(std::string(s)) {}

    static Json array();
    static Json object();

    Type type() const { return static_cast<Type>(value_.index()); }
    bool isNull() const { return type() == Type::Null; }
    bool isBool() const { return type() == Type::Bool; }
    bool isNumber() const { return type() == Type::Number; }
    bool isString() const { return type() == Type::String; }
    bool isArray() const { return type() == Type::Array; }
    bool isObject() const { return type() == Type::Object; }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0) const;
    float asFloat(float fallback = 0) const { return static_cast<float>(asNumber(fallback)); }
    int asInt(int fallback = 0) const { return static_cast<int>(asNumber(fallback)); }
    const std::string& asString() const;
    std::string asString(std::string_view fallback) const;

    // Arrays
    size_t size() const;
    const Json& operator[](size_t index) const;
    Json& operator[](size_t index);
    const Json& operator[](int index) const { return (*this)[static_cast<size_t>(index)]; }
    Json& operator[](int index) { return (*this)[static_cast<size_t>(index)]; }
    void push(Json value);
    const std::vector<Json>& elements() const;

    // Objects
    bool contains(std::string_view key) const;
    const Json& operator[](std::string_view key) const; // null if missing
    Json& operator[](std::string_view key);             // inserts if missing
    const Json& operator[](const char* key) const { return (*this)[std::string_view(key)]; }
    Json& operator[](const char* key) { return (*this)[std::string_view(key)]; }
    const std::vector<JsonMember>& members() const;
    bool erase(std::string_view key);

    std::string dump(int indent = -1) const;
    static Json parse(std::string_view text, std::string* error = nullptr);

    bool operator==(const Json& other) const;

private:
    using Array = std::vector<Json>;
    using Object = std::vector<JsonMember>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value_;

    void dumpTo(std::string& out, int indent, int depth) const;
};

struct JsonMember {
    std::string key;
    Json value;
    bool operator==(const JsonMember& o) const { return key == o.key && value == o.value; }
};

} // namespace aven
