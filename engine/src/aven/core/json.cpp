#include "aven/core/json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace aven {

namespace {

const Json kNull;
const std::string kEmptyString;
const std::vector<Json> kEmptyArray;
const std::vector<JsonMember> kEmptyObject;

void appendEscaped(std::string& out, const std::string& s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x", c);
                out += buf;
            } else {
                out += static_cast<char>(c);
            }
        }
    }
    out += '"';
}

void appendNumber(std::string& out, double v) {
    if (!std::isfinite(v)) {
        out += "0";
        return;
    }
    if (v == std::floor(v) && std::abs(v) < 1e15) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.0f", v);
        out += buf;
        return;
    }
    char buf[40];
    for (int precision : {6, 9, 15, 17}) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, v);
        if (std::strtod(buf, nullptr) == v || static_cast<float>(std::strtod(buf, nullptr)) == static_cast<float>(v))
            break;
    }
    out += buf;
}

class Parser {
public:
    explicit Parser(std::string_view text) : s_(text) {}

    Json parseDocument(std::string* error) {
        Json value = parseValue();
        skipWhitespace();
        if (ok_ && pos_ != s_.size())
            fail("unexpected characters after the end of the document");
        if (!ok_) {
            if (error)
                *error = error_;
            return {};
        }
        return value;
    }

private:
    std::string_view s_;
    size_t pos_ = 0;
    bool ok_ = true;
    std::string error_;
    int depth_ = 0;

    void fail(const std::string& msg) {
        if (!ok_)
            return;
        ok_ = false;
        int line = 1, col = 1;
        for (size_t i = 0; i < pos_ && i < s_.size(); ++i) {
            if (s_[i] == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        error_ = "line " + std::to_string(line) + ", column " + std::to_string(col) + ": " + msg;
    }

    void skipWhitespace() {
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (c == '/' && pos_ + 1 < s_.size() && s_[pos_ + 1] == '/') {
                // Allow // comments: hand-edited project files are common for beginners.
                while (pos_ < s_.size() && s_[pos_] != '\n')
                    ++pos_;
            } else {
                break;
            }
        }
    }

    bool consume(char c) {
        skipWhitespace();
        if (pos_ < s_.size() && s_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    Json parseValue() {
        if (!ok_)
            return {};
        if (++depth_ > 512) {
            fail("document is nested too deeply");
            return {};
        }
        skipWhitespace();
        Json result;
        if (pos_ >= s_.size()) {
            fail("unexpected end of document");
        } else {
            char c = s_[pos_];
            if (c == '{')
                result = parseObject();
            else if (c == '[')
                result = parseArray();
            else if (c == '"')
                result = Json(parseString());
            else if (c == '-' || (c >= '0' && c <= '9'))
                result = parseNumber();
            else if (s_.substr(pos_, 4) == "true") {
                pos_ += 4;
                result = Json(true);
            } else if (s_.substr(pos_, 5) == "false") {
                pos_ += 5;
                result = Json(false);
            } else if (s_.substr(pos_, 4) == "null") {
                pos_ += 4;
            } else {
                fail(std::string("unexpected character '") + c + "'");
            }
        }
        --depth_;
        return result;
    }

    Json parseObject() {
        Json obj = Json::object();
        ++pos_; // {
        if (consume('}'))
            return obj;
        while (ok_) {
            skipWhitespace();
            if (pos_ >= s_.size() || s_[pos_] != '"') {
                fail("expected a quoted key");
                break;
            }
            std::string key = parseString();
            if (!consume(':')) {
                fail("expected ':' after key");
                break;
            }
            obj[key] = parseValue();
            if (consume(',')) {
                if (consume('}')) // a trailing comma, easy to leave when editing by hand
                    break;
                continue;
            }
            if (consume('}'))
                break;
            fail("expected ',' or '}' in object");
        }
        return obj;
    }

    Json parseArray() {
        Json arr = Json::array();
        ++pos_; // [
        if (consume(']'))
            return arr;
        while (ok_) {
            arr.push(parseValue());
            if (consume(',')) {
                if (consume(']')) // a trailing comma
                    break;
                continue;
            }
            if (consume(']'))
                break;
            fail("expected ',' or ']' in array");
        }
        return arr;
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    uint32_t parseHex4() {
        if (pos_ + 4 > s_.size()) {
            fail("truncated \\u escape");
            return 0;
        }
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            char c = s_[pos_++];
            v <<= 4;
            if (c >= '0' && c <= '9')
                v |= c - '0';
            else if (c >= 'a' && c <= 'f')
                v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                v |= c - 'A' + 10;
            else {
                fail("invalid \\u escape");
                return 0;
            }
        }
        return v;
    }

    std::string parseString() {
        std::string out;
        ++pos_; // opening quote
        while (ok_) {
            if (pos_ >= s_.size()) {
                fail("unterminated string");
                break;
            }
            char c = s_[pos_++];
            if (c == '"')
                break;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= s_.size()) {
                fail("unterminated escape");
                break;
            }
            char e = s_[pos_++];
            switch (e) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'u': {
                uint32_t cp = parseHex4();
                if (cp >= 0xD800 && cp <= 0xDBFF && pos_ + 1 < s_.size() && s_[pos_] == '\\' &&
                    s_[pos_ + 1] == 'u') {
                    pos_ += 2;
                    uint32_t lo = parseHex4();
                    cp = lo >= 0xDC00 && lo <= 0xDFFF ? 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00) : 0xFFFD;
                }
                appendUtf8(out, cp);
                break;
            }
            default: fail(std::string("invalid escape '\\") + e + "'");
            }
        }
        return out;
    }

    Json parseNumber() {
        size_t start = pos_;
        if (s_[pos_] == '-')
            ++pos_;
        while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' ||
                                    s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-'))
            ++pos_;
        std::string num(s_.substr(start, pos_ - start));
        char* end = nullptr;
        double v = std::strtod(num.c_str(), &end);
        if (end != num.c_str() + num.size())
            fail("invalid number '" + num + "'");
        return Json(v);
    }
};

} // namespace

Json Json::array() {
    Json j;
    j.value_ = Array{};
    return j;
}

Json Json::object() {
    Json j;
    j.value_ = Object{};
    return j;
}

bool Json::asBool(bool fallback) const {
    if (auto* b = std::get_if<bool>(&value_))
        return *b;
    if (auto* n = std::get_if<double>(&value_))
        return *n != 0;
    return fallback;
}

double Json::asNumber(double fallback) const {
    if (auto* n = std::get_if<double>(&value_))
        return *n;
    if (auto* b = std::get_if<bool>(&value_))
        return *b ? 1 : 0;
    return fallback;
}

const std::string& Json::asString() const {
    if (auto* s = std::get_if<std::string>(&value_))
        return *s;
    return kEmptyString;
}

std::string Json::asString(std::string_view fallback) const {
    if (auto* s = std::get_if<std::string>(&value_))
        return *s;
    return std::string(fallback);
}

size_t Json::size() const {
    if (auto* a = std::get_if<Array>(&value_))
        return a->size();
    if (auto* o = std::get_if<Object>(&value_))
        return o->size();
    return 0;
}

const Json& Json::operator[](size_t index) const {
    if (auto* a = std::get_if<Array>(&value_); a && index < a->size())
        return (*a)[index];
    return kNull;
}

Json& Json::operator[](size_t index) {
    if (!isArray())
        value_ = Array{};
    auto& a = std::get<Array>(value_);
    if (index >= a.size())
        a.resize(index + 1);
    return a[index];
}

void Json::push(Json value) {
    if (!isArray())
        value_ = Array{};
    std::get<Array>(value_).push_back(std::move(value));
}

const std::vector<Json>& Json::elements() const {
    if (auto* a = std::get_if<Array>(&value_))
        return *a;
    return kEmptyArray;
}

bool Json::contains(std::string_view key) const {
    if (auto* o = std::get_if<Object>(&value_))
        for (auto& m : *o)
            if (m.key == key)
                return true;
    return false;
}

const Json& Json::operator[](std::string_view key) const {
    if (auto* o = std::get_if<Object>(&value_))
        for (auto& m : *o)
            if (m.key == key)
                return m.value;
    return kNull;
}

Json& Json::operator[](std::string_view key) {
    if (!isObject())
        value_ = Object{};
    auto& o = std::get<Object>(value_);
    for (auto& m : o)
        if (m.key == key)
            return m.value;
    o.push_back({std::string(key), Json()});
    return o.back().value;
}

const std::vector<JsonMember>& Json::members() const {
    if (auto* o = std::get_if<Object>(&value_))
        return *o;
    return kEmptyObject;
}

bool Json::erase(std::string_view key) {
    if (auto* o = std::get_if<Object>(&value_))
        return std::erase_if(*o, [&](const JsonMember& m) { return m.key == key; }) > 0;
    return false;
}

bool Json::operator==(const Json& other) const {
    return value_ == other.value_;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    auto newline = [&](int d) {
        if (indent < 0)
            return;
        out += '\n';
        out.append(static_cast<size_t>(indent * d), ' ');
    };
    switch (type()) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += std::get<bool>(value_) ? "true" : "false"; break;
    case Type::Number: appendNumber(out, std::get<double>(value_)); break;
    case Type::String: appendEscaped(out, std::get<std::string>(value_)); break;
    case Type::Array: {
        auto& a = std::get<Array>(value_);
        if (a.empty()) {
            out += "[]";
            break;
        }
        // Short arrays of numbers (vectors, colors) stay on one line for readability.
        bool inlineArray = a.size() <= 4;
        for (auto& v : a)
            inlineArray = inlineArray && v.isNumber();
        out += '[';
        for (size_t i = 0; i < a.size(); ++i) {
            if (i)
                out += inlineArray && indent >= 0 ? ", " : ",";
            if (!inlineArray)
                newline(depth + 1);
            a[i].dumpTo(out, indent, depth + 1);
        }
        if (!inlineArray)
            newline(depth);
        out += ']';
        break;
    }
    case Type::Object: {
        auto& o = std::get<Object>(value_);
        if (o.empty()) {
            out += "{}";
            break;
        }
        out += '{';
        for (size_t i = 0; i < o.size(); ++i) {
            if (i)
                out += ',';
            newline(depth + 1);
            appendEscaped(out, o[i].key);
            out += indent >= 0 ? ": " : ":";
            o[i].value.dumpTo(out, indent, depth + 1);
        }
        newline(depth);
        out += '}';
        break;
    }
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

Json Json::parse(std::string_view text, std::string* error) {
    return Parser(text).parseDocument(error);
}

} // namespace aven
