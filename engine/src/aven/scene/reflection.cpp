#include "aven/scene/reflection.h"

#include "aven/core/uuid.h"
#include "aven/math/math.h"

#include "aven/core/log.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace aven {

namespace {
// Each problem in hand-edited files is pointed out once, not every time a scene loads.
bool firstTime(const std::string& key) {
    static std::set<std::string> seen;
    return seen.insert(key).second;
}
} // namespace

std::string toSnakeCase(std::string_view s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        bool upper = std::isupper(static_cast<unsigned char>(c));
        bool digit = std::isdigit(static_cast<unsigned char>(c));
        if (i > 0) {
            char p = s[i - 1];
            bool prevLower = std::islower(static_cast<unsigned char>(p));
            bool prevUpper = std::isupper(static_cast<unsigned char>(p));
            bool prevAlpha = std::isalpha(static_cast<unsigned char>(p));
            bool nextLower = i + 1 < s.size() && std::islower(static_cast<unsigned char>(s[i + 1]));
            if ((upper && (prevLower || (prevUpper && nextLower))) || (digit && prevAlpha))
                out += '_';
        }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::string toLabel(std::string_view snake) {
    std::string out;
    bool capitalize = true;
    for (char c : snake) {
        if (c == '_') {
            out += ' ';
            capitalize = true;
            continue;
        }
        out += capitalize ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
        capitalize = false;
    }
    return out;
}

const FieldInfo* ComponentInfo::findField(std::string_view fieldName) const {
    for (auto& f : fields)
        if (f.name == fieldName)
            return &f;
    return nullptr;
}

static bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

const ComponentInfo* ComponentRegistry::find(std::string_view name) {
    for (auto& c : all())
        if (equalsIgnoreCase(c.name, name) || equalsIgnoreCase(c.scriptName, name))
            return &c;
    return nullptr;
}

Json saveField(const FieldInfo& f, const void* c) {
    switch (f.type) {
    case FieldType::Bool: return Json(f.ref<bool>(c));
    case FieldType::Int: return Json(f.ref<int>(c));
    case FieldType::Float: return Json(f.ref<float>(c));
    case FieldType::Vec2: {
        auto& v = f.ref<Vec2>(c);
        Json j = Json::array();
        j.push(v.x);
        j.push(v.y);
        return j;
    }
    case FieldType::Vec3: {
        auto& v = f.ref<Vec3>(c);
        Json j = Json::array();
        j.push(v.x);
        j.push(v.y);
        j.push(v.z);
        return j;
    }
    case FieldType::Color: {
        auto& v = f.ref<Color>(c);
        Json j = Json::array();
        j.push(v.r);
        j.push(v.g);
        j.push(v.b);
        j.push(v.a);
        return j;
    }
    case FieldType::String:
    case FieldType::Asset: return Json(f.ref<std::string>(c));
    case FieldType::Enum: {
        int32_t v = f.ref<int32_t>(c);
        if (v >= 0 && v < static_cast<int32_t>(f.options.enumNames.size()))
            return Json(f.options.enumNames[static_cast<size_t>(v)]);
        return Json(v);
    }
    case FieldType::EntityRef: {
        auto& id = f.ref<UUID>(c);
        return id ? Json(id.toString()) : Json();
    }
    }
    return {};
}

void loadField(const FieldInfo& f, void* c, const Json& v) {
    if (v.isNull() && f.type != FieldType::EntityRef)
        return;
    switch (f.type) {
    case FieldType::Bool: f.ref<bool>(c) = v.asBool(); break;
    case FieldType::Int: f.ref<int>(c) = v.asInt(); break;
    case FieldType::Float: f.ref<float>(c) = v.asFloat(); break;
    case FieldType::Vec2: f.ref<Vec2>(c) = {v[0].asFloat(), v[1].asFloat()}; break;
    case FieldType::Vec3: f.ref<Vec3>(c) = {v[0].asFloat(), v[1].asFloat(), v[2].asFloat()}; break;
    case FieldType::Color:
        f.ref<Color>(c) = {v[0].asFloat(1), v[1].asFloat(1), v[2].asFloat(1), v.size() > 3 ? v[3].asFloat(1) : 1.0f};
        break;
    case FieldType::String:
    case FieldType::Asset: f.ref<std::string>(c) = v.asString(); break;
    case FieldType::Enum: {
        if (v.isString()) {
            auto& names = f.options.enumNames;
            bool found = false;
            for (size_t i = 0; i < names.size(); ++i)
                if (equalsIgnoreCase(names[i], v.asString())) {
                    f.ref<int32_t>(c) = static_cast<int32_t>(i);
                    found = true;
                }
            if (!found && firstTime(f.name + "=" + v.asString())) {
                std::string choices;
                for (auto& n : names)
                    choices += (choices.empty() ? "" : ", ") + n;
                Log::warn("'", v.asString(), "' isn't a choice for ", f.label, ". It can be: ", choices, ".");
            }
        } else {
            f.ref<int32_t>(c) = v.asInt();
        }
        break;
    }
    case FieldType::EntityRef: f.ref<UUID>(c) = v.isString() ? UUID::fromString(v.asString()) : UUID{}; break;
    }
}

Json saveComponent(const ComponentInfo& info, const void* component) {
    Json j = Json::object();
    for (auto& f : info.fields)
        if (!f.options.runtime)
            j[f.name] = saveField(f, component);
    if (info.saveExtra)
        info.saveExtra(component, j);
    return j;
}

void loadComponent(const ComponentInfo& info, void* component, const Json& data) {
    // Missing keys keep their defaults, so older scene files keep loading after fields are added.
    for (auto& f : info.fields)
        if (!f.options.runtime && data.contains(f.name))
            loadField(f, component, data[f.name]);
    if (info.loadExtra)
        info.loadExtra(component, data);
    // Point out typos in hand-edited files instead of silently ignoring them.
    for (auto& m : data.members()) {
        if (info.findField(m.key) || std::find(info.extraKeys.begin(), info.extraKeys.end(), m.key) != info.extraKeys.end())
            continue;
        std::string suggestion = toSnakeCase(m.key);
        const FieldInfo* close = info.findField(suggestion);
        // "className" for "class_name": clearly meant, so use it (and say how it's spelled).
        if (close && !close->options.runtime && !data.contains(suggestion))
            loadField(*close, component, m.value);
        if (!firstTime(info.name + "." + m.key))
            continue;
        if (close)
            Log::warn(info.name, ": '", m.key, "' is spelled '", suggestion, "' in scene files (it was read anyway).");
        else
            Log::warn(info.name, " has no field '", m.key, "'; it was ignored.");
    }
}

} // namespace aven
