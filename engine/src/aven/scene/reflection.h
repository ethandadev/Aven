#pragma once

// Component reflection: each component's fields are described once, and that
// description drives saving/loading, the editor inspector, EasyScript/blocks
// property access and the C API. Adding a field in one place exposes it everywhere.

#include "aven/core/json.h"
#include "aven/core/uuid.h"
#include "aven/ecs/registry.h"
#include "aven/math/math.h"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace aven {

enum class FieldType { Bool, Int, Float, Vec2, Vec3, Color, String, Enum, Asset, EntityRef };

enum class AssetKind { None, Image, Model, Audio, Script, Font, Prefab, Scene };

struct FieldOptions {
    const char* label = nullptr;
    const char* tooltip = nullptr;
    std::vector<std::string> enumNames;
    AssetKind asset = AssetKind::None;
    float min = 0, max = 0; // slider range when max > min
    float step = 0.05f;
    bool advanced = false; // hidden in beginner mode
    bool runtime = false;  // not saved, not shown (runtime state)
    bool multiline = false;
};

struct FieldInfo {
    std::string name;  // snake_case: used in files, scripts and the C API
    std::string label; // shown in the inspector
    FieldType type = FieldType::Float;
    std::function<void*(void*)> access;
    FieldOptions options;

    template <class T> T& ref(void* component) const { return *static_cast<T*>(access(component)); }
    template <class T> const T& ref(const void* component) const {
        return *static_cast<const T*>(access(const_cast<void*>(component)));
    }
};

struct ComponentInfo {
    std::string name;       // "RigidBody2D"
    std::string scriptName; // "rigid_body_2d"
    std::string category;   // "Rendering", "Physics 2D", ...
    std::string description;
    bool advanced = false;
    bool removable = true;
    bool is3D = false;
    bool is2D = false;
    std::vector<FieldInfo> fields;

    std::function<void*(Registry&, Entity)> get;
    std::function<void*(Registry&, Entity)> add;
    std::function<void(Registry&, Entity)> remove;
    std::function<void(const void*, Json&)> saveExtra; // optional custom data
    std::function<void(void*, const Json&)> loadExtra;

    const FieldInfo* findField(std::string_view fieldName) const;
};

class ComponentRegistry {
public:
    static const std::vector<ComponentInfo>& all();
    // Accepts the C++ name ("RigidBody2D") or the script name ("rigid_body_2d"), any case.
    static const ComponentInfo* find(std::string_view name);
};

std::string toSnakeCase(std::string_view camel);
std::string toLabel(std::string_view snake);

Json saveField(const FieldInfo& field, const void* component);
void loadField(const FieldInfo& field, void* component, const Json& value);
Json saveComponent(const ComponentInfo& info, const void* component);
void loadComponent(const ComponentInfo& info, void* component, const Json& data);

// Builder used when registering components.
template <class T> class ComponentBuilder {
public:
    explicit ComponentBuilder(ComponentInfo& info) : info_(info) {
        info_.get = [](Registry& r, Entity e) -> void* { return r.tryGet<T>(e); };
        info_.add = [](Registry& r, Entity e) -> void* { return &r.getOrEmplace<T>(e); };
        info_.remove = [](Registry& r, Entity e) { r.remove<T>(e); };
    }

    template <class M> ComponentBuilder& field(const char* member, M T::*ptr, FieldOptions options = {}) {
        FieldInfo f;
        f.name = toSnakeCase(member);
        f.label = options.label ? options.label : toLabel(f.name);
        f.type = deduce<M>(options);
        f.access = [ptr](void* c) -> void* { return &(static_cast<T*>(c)->*ptr); };
        f.options = std::move(options);
        info_.fields.push_back(std::move(f));
        return *this;
    }

private:
    ComponentInfo& info_;

    template <class M> static FieldType deduce(const FieldOptions& o);
};

template <class T> template <class M> FieldType ComponentBuilder<T>::deduce(const FieldOptions& o) {
    if constexpr (std::is_same_v<M, bool>)
        return FieldType::Bool;
    else if constexpr (std::is_same_v<M, int>)
        return FieldType::Int;
    else if constexpr (std::is_same_v<M, float>)
        return FieldType::Float;
    else if constexpr (std::is_same_v<M, Vec2>)
        return FieldType::Vec2;
    else if constexpr (std::is_same_v<M, Vec3>)
        return FieldType::Vec3;
    else if constexpr (std::is_same_v<M, Color>)
        return FieldType::Color;
    else if constexpr (std::is_same_v<M, std::string>)
        return o.asset != AssetKind::None ? FieldType::Asset : FieldType::String;
    else if constexpr (std::is_same_v<M, UUID>)
        return FieldType::EntityRef;
    else if constexpr (std::is_enum_v<M>) {
        static_assert(sizeof(M) == sizeof(int32_t), "reflected enums must be int32_t-sized");
        return FieldType::Enum;
    } else
        static_assert(sizeof(M) == 0, "unsupported reflected field type");
}

} // namespace aven
