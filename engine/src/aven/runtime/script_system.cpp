#include "aven/runtime/script_system.h"

#include "aven/blocks/blocks.h"
#include "aven/core/fs.h"
#include "aven/core/log.h"
#include "aven/render/ui_layout.h"
#include "aven/runtime/game.h"
#include "aven/runtime/systems.h"
#include "aven/scene/reflection.h"
#include "aven/script/stdlib.h"

#include <algorithm>
#include <unordered_set>
#include <cmath>

namespace aven {

using script::CallArgs;
using script::intern;
using script::raise;
using script::Symbol;
using script::Value;

// ---------------------------------------------------------------- easing

bool parseEasing(const std::string& name, Easing& out) {
    static const std::pair<const char*, Easing> table[] = {
        {"linear", Easing::Linear},       {"ease_in", Easing::EaseIn},   {"ease_out", Easing::EaseOut},
        {"ease_in_out", Easing::EaseInOut}, {"bounce", Easing::Bounce},  {"elastic", Easing::Elastic},
        {"back", Easing::Back},           {"smooth", Easing::EaseInOut},
    };
    for (auto& [n, e] : table)
        if (name == n) {
            out = e;
            return true;
        }
    return false;
}

float applyEasing(Easing e, float t) {
    t = saturate(t);
    switch (e) {
    case Easing::Linear: return t;
    case Easing::EaseIn: return t * t * t;
    case Easing::EaseOut: return 1 - std::pow(1 - t, 3.0f);
    case Easing::EaseInOut: return t < 0.5f ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3.0f) / 2;
    case Easing::Bounce: {
        const float n = 7.5625f, d = 2.75f;
        if (t < 1 / d)
            return n * t * t;
        if (t < 2 / d) {
            t -= 1.5f / d;
            return n * t * t + 0.75f;
        }
        if (t < 2.5f / d) {
            t -= 2.25f / d;
            return n * t * t + 0.9375f;
        }
        t -= 2.625f / d;
        return n * t * t + 0.984375f;
    }
    case Easing::Elastic:
        if (t == 0 || t == 1)
            return t;
        return std::pow(2.0f, -10 * t) * std::sin((t * 10 - 0.75f) * (2 * kPi / 3)) + 1;
    case Easing::Back: {
        const float c1 = 1.70158f, c3 = c1 + 1;
        return 1 + c3 * std::pow(t - 1, 3.0f) + c1 * std::pow(t - 1, 2.0f);
    }
    }
    return t;
}

// ---------------------------------------------------------------- value conversions

Vec3 ScriptSystem::toVec3(const Value& v, const char* context, Vec3 fallback) {
    if (v.isVec()) {
        auto& o = v.vecObj();
        return {static_cast<float>(o.v[0]), static_cast<float>(o.v[1]),
                o.components > 2 ? static_cast<float>(o.v[2]) : fallback.z};
    }
    if (v.isList()) {
        auto& items = v.listObj().items;
        if (items.size() >= 2 && items[0].isNumber() && items[1].isNumber())
            return {static_cast<float>(items[0].number()), static_cast<float>(items[1].number()),
                    items.size() > 2 && items[2].isNumber() ? static_cast<float>(items[2].number()) : fallback.z};
    }
    raise(std::string(context) + " should be a position like vec(1, 2), but got " + v.typeDescription() + ".");
}

Color ScriptSystem::toColor(const Value& v, const char* context) {
    Value c = script::toColor(v, context);
    auto& o = c.vecObj();
    return {static_cast<float>(o.v[0]), static_cast<float>(o.v[1]), static_cast<float>(o.v[2]),
            static_cast<float>(o.v[3])};
}

Value ScriptSystem::fromVec3(Vec3 v, int components) { return Value::vec(v.x, v.y, v.z, components); }
Value ScriptSystem::fromColor(Color c) { return Value::color(c.r, c.g, c.b, c.a); }

static std::string toText(const Value& v) { return v.toString(); }

static float toNumber(const Value& v, const char* what) {
    if (v.isNumber())
        return static_cast<float>(v.number());
    if (v.isBool())
        return v.boolean() ? 1.0f : 0.0f;
    raise(std::string("'") + what + "' should be a number, but got " + v.typeDescription() + " (" + v.repr() + ").");
}

// ---------------------------------------------------------------- script objects

namespace {

struct Bounds {
    Vec3 min, max;
    bool valid = false;
};

Bounds worldBounds(Scene& scene, Entity e) {
    auto& reg = scene.registry();
    Vec3 half{0.5f, 0.5f, 0.5f};
    Vec3 center{0, 0, 0};
    bool any = false;
    if (auto* b = reg.tryGet<BoxCollider2D>(e)) {
        half = {b->size.x * 0.5f, b->size.y * 0.5f, 0.5f};
        center = {b->offset.x, b->offset.y, 0};
        any = true;
    } else if (auto* c = reg.tryGet<CircleCollider2D>(e)) {
        half = {c->radius, c->radius, 0.5f};
        center = {c->offset.x, c->offset.y, 0};
        any = true;
    } else if (auto* bc = reg.tryGet<BoxCollider>(e)) {
        half = bc->size * 0.5f;
        center = bc->offset;
        any = true;
    } else if (auto* sc = reg.tryGet<SphereCollider>(e)) {
        half = Vec3(sc->radius);
        center = sc->offset;
        any = true;
    } else if (auto* s = reg.tryGet<SpriteRenderer>(e)) {
        half = {s->size.x * 0.5f, s->size.y * 0.5f, 0.5f};
        any = true;
    } else if (reg.has<MeshRenderer>(e) || reg.has<TextRenderer>(e)) {
        any = true;
    }
    if (!any)
        return {};
    Mat4 m = scene.worldMatrix(e);
    Bounds b{Vec3(1e30f), Vec3(-1e30f), true};
    for (int i = 0; i < 8; ++i) {
        Vec3 corner{(i & 1) ? half.x : -half.x, (i & 2) ? half.y : -half.y, (i & 4) ? half.z : -half.z};
        Vec3 w = transformPoint(m, center + corner);
        b.min = min(b.min, w);
        b.max = max(b.max, w);
    }
    return b;
}

bool overlaps(const Bounds& a, const Bounds& b) {
    return a.valid && b.valid && a.min.x <= b.max.x && a.max.x >= b.min.x && a.min.y <= b.max.y &&
           a.max.y >= b.min.y && a.min.z <= b.max.z && a.max.z >= b.min.z;
}

bool is3D(Scene& scene, Entity e) {
    auto& r = scene.registry();
    return r.has<MeshRenderer>(e) || r.has<RigidBody>(e) || r.has<CharacterController>(e) ||
           r.has<BoxCollider>(e) || r.has<SphereCollider>(e) || r.has<Light>(e);
}

class ComponentObject final : public script::NativeObject {
public:
    ComponentObject(ScriptSystem& sys, Entity e, const ComponentInfo* info) : sys_(sys), entity_(e), info_(info) {}

    std::string typeName() const override { return info_->name; }
    std::string repr() const override { return info_->name; }
    bool equals(const NativeObject& o) const override {
        auto* other = dynamic_cast<const ComponentObject*>(&o);
        return other && other->entity_ == entity_ && other->info_ == info_;
    }

    void* data() {
        Scene& s = sys_.game().scene();
        if (!s.valid(entity_))
            raise("This " + info_->name + " belongs to an object that was destroyed.");
        void* c = info_->get(s.registry(), entity_);
        if (!c)
            raise("The " + info_->name + " was removed from this object.");
        return c;
    }

    bool getAttr(script::VM&, const std::string& name, Value& out) override {
        const FieldInfo* f = info_->findField(name);
        if (!f)
            return false;
        void* c = data();
        switch (f->type) {
        case FieldType::Bool: out = Value(f->ref<bool>(c)); break;
        case FieldType::Int: out = Value(f->ref<int>(c)); break;
        case FieldType::Float: out = Value(f->ref<float>(c)); break;
        case FieldType::Vec2: {
            Vec2 v = f->ref<Vec2>(c);
            out = Value::vec(v.x, v.y, 0, 2);
            break;
        }
        case FieldType::Vec3: out = ScriptSystem::fromVec3(f->ref<Vec3>(c)); break;
        case FieldType::Color: out = ScriptSystem::fromColor(f->ref<Color>(c)); break;
        case FieldType::String:
        case FieldType::Asset: out = Value(f->ref<std::string>(c)); break;
        case FieldType::Enum: {
            int32_t v = f->ref<int32_t>(c);
            out = v >= 0 && v < static_cast<int32_t>(f->options.enumNames.size())
                      ? Value(toSnakeCase(f->options.enumNames[static_cast<size_t>(v)]))
                      : Value(v);
            break;
        }
        case FieldType::EntityRef: {
            Entity t = sys_.game().scene().findByUUID(f->ref<UUID>(c));
            out = t ? sys_.entityValue(t) : Value();
            break;
        }
        }
        return true;
    }

    bool setAttr(script::VM&, const std::string& name, const Value& v) override {
        const FieldInfo* f = info_->findField(name);
        if (!f)
            return false;
        void* c = data();
        std::string what = info_->name + "." + name;
        switch (f->type) {
        case FieldType::Bool: f->ref<bool>(c) = v.truthy(); break;
        case FieldType::Int: f->ref<int>(c) = static_cast<int>(toNumber(v, what.c_str())); break;
        case FieldType::Float: f->ref<float>(c) = toNumber(v, what.c_str()); break;
        case FieldType::Vec2: {
            Vec3 p = ScriptSystem::toVec3(v, what.c_str());
            f->ref<Vec2>(c) = {p.x, p.y};
            break;
        }
        case FieldType::Vec3: f->ref<Vec3>(c) = ScriptSystem::toVec3(v, what.c_str(), f->ref<Vec3>(c)); break;
        case FieldType::Color: f->ref<Color>(c) = ScriptSystem::toColor(v, what.c_str()); break;
        case FieldType::String:
        case FieldType::Asset: f->ref<std::string>(c) = toText(v); break;
        case FieldType::Enum: {
            if (v.isNumber()) {
                f->ref<int32_t>(c) = static_cast<int32_t>(v.number());
                break;
            }
            std::string wanted = toSnakeCase(toText(v));
            auto& names = f->options.enumNames;
            for (size_t i = 0; i < names.size(); ++i)
                if (toSnakeCase(names[i]) == wanted) {
                    f->ref<int32_t>(c) = static_cast<int32_t>(i);
                    goto enumDone;
                }
            {
                std::vector<std::string> options;
                for (auto& n : names)
                    options.push_back(toSnakeCase(n));
                std::string list;
                for (auto& o : options)
                    list += (list.empty() ? "" : ", ") + ("\"" + o + "\"");
                raise(what + " can't be \"" + toText(v) + "\". Choose one of: " + list + ".");
            }
        enumDone:
            break;
        }
        case FieldType::EntityRef: {
            Entity t = sys_.entityFromValue(v);
            f->ref<UUID>(c) = t ? sys_.game().scene().info(t).uuid : UUID{};
            break;
        }
        }
        // Physics settings changed from a script take effect right away.
        if (info_->category == "Physics 2D")
            sys_.game().physics2D().refresh(entity_);
        else if (info_->category == "Physics 3D")
            sys_.game().physics3D().refresh(entity_);
        return true;
    }

    std::vector<std::string> attrNames() const override {
        std::vector<std::string> out;
        for (auto& f : info_->fields)
            if (!f.options.runtime)
                out.push_back(f.name);
        return out;
    }

private:
    ScriptSystem& sys_;
    Entity entity_;
    const ComponentInfo* info_;
};

// `game` is shared by every script and survives scene changes: game.score = 0
class GameDataObject final : public script::NativeObject {
public:
    std::string typeName() const override { return "game"; }
    std::string repr() const override { return "game"; }
    bool getAttr(script::VM&, const std::string& name, Value& out) override {
        if (Value* v = data.dictObj().find(Value(name))) {
            out = *v;
            return true;
        }
        raise("game." + name + " hasn't been given a value yet. Set it first, for example in on_start(): game." + name +
              " = 0");
    }
    bool setAttr(script::VM&, const std::string& name, const Value& v) override {
        data.dictObj().set(Value(name), v);
        return true;
    }
    std::vector<std::string> attrNames() const override {
        std::vector<std::string> out;
        for (auto& [k, v] : data.dictObj().entries)
            out.push_back(k.toString());
        return out;
    }
    Value data = Value::dict();
};

using Method = std::function<Value(ScriptSystem&, Entity, CallArgs&)>;
struct MethodDef {
    const char* name;
    const char* signature;
    int minArgs, maxArgs;
    Method fn;
};
const std::vector<MethodDef>& entityMethods();

const char* kComponentAliases[][2] = {
    {"sprite", "SpriteRenderer"},   {"body", "RigidBody2D"},        {"camera", "Camera"},
    {"light", "Light"},             {"audio", "AudioSource"},       {"particles", "ParticleEmitter"},
    {"animator", "SpriteAnimator"}, {"mesh", "MeshRenderer"},       {"text_renderer", "TextRenderer"},
    {"ui", "UIElement"},            {"button", "UIButton"},         {"collider", "BoxCollider2D"},
    {"controller", "CharacterController"}, {"follow", "CameraFollow"}, {"post", "PostProcessing"},
    {"tilemap", "Tilemap"},
};

class EntityObject final : public script::NativeObject {
public:
    EntityObject(ScriptSystem& sys, Entity e) : sys_(sys), entity_(e) {}

    Entity entity() const { return entity_; }
    bool alive() const { return sys_.game().scene().valid(entity_); }

    std::string typeName() const override { return "object"; }
    std::string repr() const override {
        if (!alive())
            return "a destroyed object";
        return "'" + sys_.game().scene().info(entity_).name + "'";
    }
    bool equals(const NativeObject& o) const override {
        auto* other = dynamic_cast<const EntityObject*>(&o);
        return other && other->entity_ == entity_;
    }
    std::string hashKey() const override { return std::to_string(entity_.toHandle()); }
    bool truthy() const override { return alive(); }

    bool getAttr(script::VM& vm, const std::string& name, Value& out) override {
        if (!alive()) {
            if (name == "exists" || name == "alive") {
                out = Value(false);
                return true;
            }
            raise("This object has been destroyed, so '" + name +
                  "' isn't available. Check that it still exists first, like: if enemy:");
        }
        if (sys_.getProperty(entity_, name, out))
            return true;
        for (auto& m : entityMethods()) {
            if (name != m.name)
                continue;
            Entity e = entity_;
            ScriptSystem* sys = &sys_;
            Method fn = m.fn;
            out = script::makeNative(m.name, m.signature, m.minArgs, m.maxArgs,
                                     [sys, e, fn](CallArgs& a) { return fn(*sys, e, a); });
            return true;
        }
        if (auto inst = sys_.instanceOf(entity_))
            if (Value* v = inst->find(intern(name))) {
                out = *v;
                return true;
            }
        Scene& scene = sys_.game().scene();
        const ComponentInfo* info = ComponentRegistry::find(name);
        if (!info)
            for (auto& alias : kComponentAliases)
                if (name == alias[0])
                    info = ComponentRegistry::find(alias[1]);
        if (info && info->get(scene.registry(), entity_)) {
            out = Value::object(std::make_shared<ComponentObject>(sys_, entity_, info));
            return true;
        }
        (void)vm;
        return false;
    }

    bool setAttr(script::VM&, const std::string& name, const Value& v) override {
        if (!alive())
            raise("This object has been destroyed, so '" + name + "' can't be changed.");
        if (sys_.setProperty(entity_, name, v))
            return true;
        for (auto& m : entityMethods())
            if (name == m.name)
                raise("'" + name + "' is an action, not a value. Call it with ( ), like: self." + name + "()");
        auto inst = sys_.instanceOf(entity_);
        if (!inst) {
            // Objects without a script can still hold values set by other scripts.
            raise("'" + sys_.game().scene().info(entity_).name + "' has no property called '" + name +
                  "' and no script to store it in.");
        }
        inst->set(intern(name), v);
        return true;
    }

    std::vector<std::string> attrNames() const override {
        std::vector<std::string> out = ScriptSystem::propertyNames();
        for (auto& m : entityMethods())
            out.push_back(m.name);
        if (auto inst = sys_.instanceOf(entity_))
            for (auto& n : inst->varNames())
                out.push_back(n);
        return out;
    }

private:
    ScriptSystem& sys_;
    Entity entity_;
};

Vec3 targetPosition(ScriptSystem& sys, const Value& v, const char* context) {
    if (Entity t = sys.entityFromValue(v))
        return sys.game().scene().worldPosition(t);
    return ScriptSystem::toVec3(v, context);
}

// Position from call args: f(x, y), f(x, y, z), f(vec) or f(entity).
Vec3 positionArgs(ScriptSystem& sys, CallArgs& a, size_t first, const char* context, Vec3 fallback = {}) {
    if (!a.has(first))
        return fallback;
    if (a[first].isNumber()) {
        float x = static_cast<float>(a.number(first, "x"));
        float y = static_cast<float>(a.numberOr(first + 1, "y", fallback.y));
        float z = static_cast<float>(a.numberOr(first + 2, "z", fallback.z));
        return {x, y, z};
    }
    Vec3 p = targetPosition(sys, a[first], context);
    if (!a[first].isVec() || a[first].vecObj().components < 3)
        if (!sys.entityFromValue(a[first]))
            p.z = fallback.z;
    return p;
}

std::string requireKeyName(ScriptSystem& sys, CallArgs& a, size_t i) {
    const std::string& name = a.string(i, "key");
    Input& input = sys.game().input();
    if (!Input::isValidName(name) && !input.hasAction(name)) {
        std::vector<std::string> options = Input::allNames();
        for (auto& act : input.actions())
            options.push_back(act.name);
        raise(std::string(a.functionName) + "(): I don't know the key \"" + name + "\"." +
              script::didYouMean(name, options) + " Keys are written like \"space\", \"left\", \"a\" or \"enter\".");
    }
    return name;
}

const std::vector<MethodDef>& entityMethods() {
    static const std::vector<MethodDef> methods = {
        {"destroy", "self.destroy()", 0, 0,
         [](ScriptSystem& s, Entity e, CallArgs&) {
             s.game().destroyEntity(e);
             return Value();
         }},
        {"damage", "self.damage(amount)", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             // Works with the Health behavior; without it, the scene restarts.
             s.game().gameplay().damage(e, static_cast<int>(a.number(0, "amount")), s.game().scene().worldPosition(e), 0);
             return Value();
         }},
        {"heal", "self.heal(amount)", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             if (auto* h = s.game().scene().registry().tryGet<Health>(e))
                 h->current = std::min(h->maxHealth, h->current + static_cast<int>(a.number(0, "amount")));
             return Value();
         }},
        {"move", "self.move(dx, dy) or self.move(dx, dy, dz)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Vec3 d = a[0].isNumber() ? Vec3(static_cast<float>(a.number(0, "dx")), static_cast<float>(a.numberOr(1, "dy", 0)),
                                             static_cast<float>(a.numberOr(2, "dz", 0)))
                                      : ScriptSystem::toVec3(a[0], "move()");
             s.game().scene().transform(e).position += d;
             return Value();
         }},
        {"move_forward", "self.move_forward(distance)", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             Transform& t = scene.transform(e);
             float d = static_cast<float>(a.number(0, "distance"));
             if (is3D(scene, e))
                 t.position += rotate(Quat::fromEuler(t.rotation), {0, 0, -1}) * d;
             else
                 t.position += Vec3(std::cos(radians(t.rotation.z)), std::sin(radians(t.rotation.z)), 0) * d;
             return Value();
         }},
        {"turn", "self.turn(degrees)", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             float deg = static_cast<float>(a.number(0, "degrees"));
             if (is3D(scene, e))
                 scene.transform(e).rotation.y += deg;
             else
                 scene.transform(e).rotation.z += deg;
             return Value();
         }},
        {"look_at", "self.look_at(target) or self.look_at(x, y)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             Vec3 target = positionArgs(s, a, 0, "look_at()");
             Vec3 pos = scene.worldPosition(e);
             Transform& t = scene.transform(e);
             if (is3D(scene, e)) {
                 Vec3 d = target - pos;
                 float yaw = degrees(std::atan2(-d.x, -d.z));
                 float pitch = degrees(std::atan2(d.y, std::sqrt(d.x * d.x + d.z * d.z)));
                 t.rotation = {pitch, yaw, 0};
             } else {
                 t.rotation.z = degrees(std::atan2(target.y - pos.y, target.x - pos.x));
             }
             return Value();
         }},
        {"move_toward", "self.move_toward(target, step)", 2, 4,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             size_t stepIndex = a[0].isNumber() ? (a.size() == 4 ? 3 : 2) : 1;
             Vec3 pos = scene.worldPosition(e);
             Vec3 target = positionArgs(s, a, 0, "move_toward()", pos);
             if (a[0].isNumber() && a.size() < 4)
                 target.z = pos.z;
             float step = static_cast<float>(a.number(stepIndex, "step"));
             Vec3 d = target - pos;
             float dist = length(d);
             Vec3 next = dist <= step || dist < 1e-6f ? target : pos + d / dist * step;
             scene.setWorldPosition(e, next);
             return Value(dist <= step);
         }},
        {"distance_to", "self.distance_to(target)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Vec3 pos = s.game().scene().worldPosition(e);
             Vec3 target = positionArgs(s, a, 0, "distance_to()", pos);
             return Value(static_cast<double>(length(target - pos)));
         }},
        {"direction_to", "self.direction_to(target)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Vec3 pos = s.game().scene().worldPosition(e);
             Vec3 target = positionArgs(s, a, 0, "direction_to()", pos);
             Vec3 d = normalize(target - pos);
             return ScriptSystem::fromVec3(d, is3D(s.game().scene(), e) ? 3 : 2);
         }},
        {"is_touching", "self.is_touching(other) or self.is_touching(\"tag\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             std::vector<Entity> candidates;
             if (a[0].isString()) {
                 candidates = scene.findAllWithTag(a[0].string());
                 if (candidates.empty())
                     if (Entity n = scene.findByName(a[0].string()))
                         candidates.push_back(n);
             } else if (Entity o = s.entityFromValue(a[0])) {
                 candidates.push_back(o);
             } else {
                 raise("is_touching() needs another object or a tag name in quotes.");
             }
             std::vector<Entity> touching = s.game().physics2D().touching(e);
             for (Entity t : s.game().physics3D().touching(e))
                 touching.push_back(t);
             Bounds mine = worldBounds(scene, e);
             for (Entity c : candidates) {
                 if (c == e || !scene.isActive(c))
                     continue;
                 if (std::find(touching.begin(), touching.end(), c) != touching.end())
                     return Value(true);
                 // Works without physics too: compare the objects' boxes.
                 if (overlaps(mine, worldBounds(scene, c)))
                     return Value(true);
             }
             return Value(false);
         }},
        {"apply_force", "self.apply_force(x, y) or self.apply_force(x, y, z)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Vec3 f = positionArgs(s, a, 0, "apply_force()");
             if (s.game().physics3D().hasBody(e))
                 s.game().physics3D().applyForce(e, f);
             else
                 s.game().physics2D().applyForce(e, {f.x, f.y});
             return Value();
         }},
        {"apply_impulse", "self.apply_impulse(x, y) or self.apply_impulse(x, y, z)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Vec3 f = positionArgs(s, a, 0, "apply_impulse()");
             if (s.game().physics3D().hasBody(e))
                 s.game().physics3D().applyImpulse(e, f);
             else
                 s.game().physics2D().applyImpulse(e, {f.x, f.y});
             return Value();
         }},
        {"play_animation", "self.play_animation(first_frame, last_frame, fps=10, loop=True)", 2, 4,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto& anim = s.game().scene().registry().getOrEmplace<SpriteAnimator>(e);
             int first = static_cast<int>(a.number(0, "first_frame")), last = static_cast<int>(a.number(1, "last_frame"));
             bool loop = a.has(3) ? a[3].truthy() : true;
             if (const Value* k = a.keyword("loop"))
                 loop = k->truthy();
             float fps = static_cast<float>(a.has(2) ? a.number(2, "fps") : a.keywordNumber("fps", 10));
             if (anim.firstFrame != first || anim.lastFrame != last || !anim.playing) {
                 anim.time = 0;
                 if (auto* sr = s.game().scene().registry().tryGet<SpriteRenderer>(e))
                     sr->frame = first;
             }
             anim.firstFrame = first;
             anim.lastFrame = last;
             anim.fps = fps;
             anim.loop = loop;
             anim.playing = true;
             return Value();
         }},
        {"set_tile", "tilemap.set_tile(column, row, tile)  (tile -1 erases)", 3, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto* tm = s.game().scene().registry().tryGet<Tilemap>(e);
             if (!tm)
                 raise("set_tile(): this object has no Tilemap.");
             int tile = a[2].isNone() ? -1 : static_cast<int>(a.number(2, "tile"));
             tm->set(static_cast<int>(std::floor(a.number(0, "column"))), static_cast<int>(std::floor(a.number(1, "row"))), tile);
             return Value();
         }},
        {"get_tile", "tilemap.get_tile(column, row)  (-1 if empty)", 2, 2,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto* tm = s.game().scene().registry().tryGet<Tilemap>(e);
             if (!tm)
                 raise("get_tile(): this object has no Tilemap.");
             return Value(static_cast<double>(
                 tm->get(static_cast<int>(std::floor(a.number(0, "column"))), static_cast<int>(std::floor(a.number(1, "row"))))));
         }},
        {"set_tile_at", "tilemap.set_tile_at(x, y, tile)  (a world position; tile -1 erases)", 3, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto* tm = s.game().scene().registry().tryGet<Tilemap>(e);
             if (!tm)
                 raise("set_tile_at(): this object has no Tilemap.");
             Vec3 local = transformPoint(inverse(s.game().scene().worldMatrix(e)),
                                         {static_cast<float>(a.number(0, "x")), static_cast<float>(a.number(1, "y")), 0});
             float ts = std::max(tm->tileSize, 0.001f);
             int tile = a[2].isNone() ? -1 : static_cast<int>(a.number(2, "tile"));
             tm->set(static_cast<int>(std::floor(local.x / ts)), static_cast<int>(std::floor(local.y / ts)), tile);
             return Value();
         }},
        {"get_tile_at", "tilemap.get_tile_at(x, y)  (a world position; -1 if empty)", 2, 2,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto* tm = s.game().scene().registry().tryGet<Tilemap>(e);
             if (!tm)
                 raise("get_tile_at(): this object has no Tilemap.");
             Vec3 local = transformPoint(inverse(s.game().scene().worldMatrix(e)),
                                         {static_cast<float>(a.number(0, "x")), static_cast<float>(a.number(1, "y")), 0});
             float ts = std::max(tm->tileSize, 0.001f);
             return Value(static_cast<double>(tm->get(static_cast<int>(std::floor(local.x / ts)), static_cast<int>(std::floor(local.y / ts)))));
         }},
        {"stop_animation", "self.stop_animation()", 0, 0,
         [](ScriptSystem& s, Entity e, CallArgs&) {
             if (auto* anim = s.game().scene().registry().tryGet<SpriteAnimator>(e))
                 anim->playing = false;
             return Value();
         }},
        {"tween", "self.tween(\"property\", target, seconds, \"ease_out\")", 3, 5,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Easing easing = Easing::EaseOut;
             if (a.has(3) && !a[3].isNone()) {
                 const std::string& name = a.string(3, "easing");
                 if (!parseEasing(name, easing))
                     raise("tween(): unknown easing \"" + name +
                           "\". Try \"linear\", \"ease_in\", \"ease_out\", \"ease_in_out\", \"bounce\", \"elastic\" or \"back\".");
             }
             s.addTween(e, a.string(0, "property"), static_cast<float>(a.number(1, "target")),
                        static_cast<float>(a.number(2, "seconds")), easing, a.has(4) ? a[4] : Value());
             return Value();
         }},
        {"clone", "self.clone()", 0, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             Entity copy = scene.duplicate(e);
             if (a.size())
                 scene.setWorldPosition(copy, positionArgs(s, a, 0, "clone()", scene.worldPosition(e)));
             s.onSpawn(copy);
             if (auto inst = s.instanceOf(copy))
                 inst->set(intern("is_clone"), Value(true));
             return s.entityValue(copy);
         }},
        {"hide", "self.hide()", 0, 0,
         [](ScriptSystem& s, Entity e, CallArgs&) {
             s.setProperty(e, "visible", Value(false));
             return Value();
         }},
        {"show", "self.show()", 0, 0,
         [](ScriptSystem& s, Entity e, CallArgs&) {
             s.setProperty(e, "visible", Value(true));
             return Value();
         }},
        {"get_component", "self.get_component(\"SpriteRenderer\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             const ComponentInfo* info = ComponentRegistry::find(a.string(0, "name"));
             if (!info)
                 raise("There's no component called \"" + a[0].string() + "\".");
             if (!info->get(s.game().scene().registry(), e))
                 return Value();
             return Value::object(std::make_shared<ComponentObject>(s, e, info));
         }},
        {"add_component", "self.add_component(\"RigidBody2D\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             const ComponentInfo* info = ComponentRegistry::find(a.string(0, "name"));
             if (!info) {
                 std::vector<std::string> names;
                 for (auto& c : ComponentRegistry::all())
                     names.push_back(c.name);
                 raise("There's no component called \"" + a[0].string() + "\"." + script::didYouMean(a[0].string(), names));
             }
             info->add(s.game().scene().registry(), e);
             s.game().physics2D().refresh(e);
             s.game().physics3D().refresh(e);
             return Value::object(std::make_shared<ComponentObject>(s, e, info));
         }},
        {"has_component", "self.has_component(\"RigidBody2D\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             const ComponentInfo* info = ComponentRegistry::find(a.string(0, "name"));
             return Value(info && info->get(s.game().scene().registry(), e) != nullptr);
         }},
        {"remove_component", "self.remove_component(\"RigidBody2D\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             const ComponentInfo* info = ComponentRegistry::find(a.string(0, "name"));
             if (info && info->removable) {
                 info->remove(s.game().scene().registry(), e);
                 s.game().physics2D().refresh(e);
                 s.game().physics3D().refresh(e);
             }
             return Value();
         }},
        {"find_child", "self.find_child(\"name\")", 1, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             const std::string& name = a.string(0, "name");
             std::function<Entity(Entity)> search = [&](Entity p) -> Entity {
                 for (Entity c : scene.children(p)) {
                     if (scene.info(c).name == name)
                         return c;
                     if (Entity found = search(c))
                         return found;
                 }
                 return {};
             };
             Entity found = search(e);
             return found ? s.entityValue(found) : Value();
         }},
        {"play_sound", "self.play_sound(\"sounds/jump.wav\", volume=1)", 1, 3,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             float volume = static_cast<float>(a.has(1) ? a.number(1, "volume") : a.keywordNumber("volume", 1));
             float pitch = static_cast<float>(a.has(2) ? a.number(2, "pitch") : a.keywordNumber("pitch", 1));
             s.game().audio().playSound(a.string(0, "sound"), volume, pitch, s.game().scene().worldPosition(e));
             return Value();
         }},
        {"send", "self.send(\"function_name\", values...)", 1, -1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             auto inst = s.instanceOf(e);
             if (!inst)
                 return Value();
             std::vector<Value> args(a.args.begin() + 1, a.args.end());
             return s.vm().callFunction(inst, intern(a.string(0, "function_name")), std::move(args)).value;
         }},
        {"say", "self.say(\"Hello!\", seconds=2)", 1, 2,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             Scene& scene = s.game().scene();
             Entity bubble;
             for (Entity c : scene.children(e))
                 if (scene.info(c).name == "_speech")
                     bubble = c;
             if (!bubble) {
                 bubble = scene.create("_speech", e);
                 auto& tr = scene.registry().emplace<TextRenderer>(bubble);
                 tr.fontSize = 0.4f;
                 tr.order = 1000;
                 tr.color = Color::fromHex(0xFFFFFF);
             }
             float height = 1;
             if (auto* sr = scene.registry().tryGet<SpriteRenderer>(e))
                 height = sr->size.y;
             scene.transform(bubble).position = {0, height * 0.5f + 0.45f, 0.01f};
             scene.registry().get<TextRenderer>(bubble).text = a[0].toString();
             float seconds = static_cast<float>(a.numberOr(1, "seconds", 2));
             if (seconds > 0) {
                 Game* game = &s.game();
                 Value remover = script::makeNative("_hide_speech", "", 0, 0, [game, bubble](CallArgs&) {
                     if (game->scene().valid(bubble))
                         game->scene().destroyLater(bubble);
                     return Value();
                 });
                 s.vm().addTimer(seconds, 0, remover, {});
             }
             return Value();
         }},
        {"emit", "self.emit(count)", 0, 1,
         [](ScriptSystem& s, Entity e, CallArgs& a) {
             s.game().gameplay().burst(e, static_cast<int>(a.numberOr(0, "count", 20)));
             return Value();
         }},
    };
    return methods;
}

} // namespace

// ---------------------------------------------------------------- properties

std::vector<std::string> ScriptSystem::propertyNames() {
    return {"name",     "tag",      "active",   "visible",  "id",        "exists",    "x",          "y",
            "z",        "position", "world_x",  "world_y",  "world_z",   "world_position", "angle", "rotation",
            "rotation_x", "rotation_y", "rotation_z", "scale", "scale_x", "scale_y", "scale_z", "size",
            "width",    "height",   "color",    "alpha",    "text",      "image",     "shape",      "frame",
            "flip_x",   "flip_y",   "order",    "velocity", "velocity_x", "velocity_y", "velocity_z", "on_ground",
            "parent",   "children", "forward",  "right",    "up",        "is_clone"};
}

std::vector<std::string> ScriptSystem::methodNames() {
    std::vector<std::string> out;
    for (auto& m : entityMethods())
        out.push_back(m.name);
    return out;
}

namespace {

Color* colorOf(Registry& r, Entity e) {
    if (auto* c = r.tryGet<SpriteRenderer>(e))
        return &c->color;
    if (auto* c = r.tryGet<MeshRenderer>(e))
        return &c->color;
    if (auto* c = r.tryGet<TextRenderer>(e))
        return &c->color;
    if (auto* c = r.tryGet<UIText>(e))
        return &c->color;
    if (auto* c = r.tryGet<UIImage>(e))
        return &c->color;
    if (auto* c = r.tryGet<UIButton>(e))
        return &c->normalColor;
    if (auto* c = r.tryGet<Light>(e))
        return &c->color;
    return nullptr;
}

std::string* textOf(Registry& r, Entity e) {
    if (auto* c = r.tryGet<TextRenderer>(e))
        return &c->text;
    if (auto* c = r.tryGet<UIText>(e))
        return &c->text;
    if (auto* c = r.tryGet<UIButton>(e))
        return &c->text;
    return nullptr;
}

Vec3 velocityOf(Game& g, Entity e) {
    if (g.physics3D().hasBody(e))
        return g.physics3D().velocity(e);
    if (auto* cc = g.scene().registry().tryGet<CharacterController>(e))
        return cc->velocity;
    Vec2 v = g.physics2D().velocity(e);
    return {v.x, v.y, 0};
}

void setVelocityOf(Game& g, Entity e, Vec3 v) {
    auto& reg = g.scene().registry();
    if (g.physics3D().hasBody(e))
        g.physics3D().setVelocity(e, v);
    else if (auto* cc = reg.tryGet<CharacterController>(e))
        cc->velocity = v;
    else if (reg.has<RigidBody2D>(e) || reg.has<BoxCollider2D>(e) || reg.has<CircleCollider2D>(e) || reg.has<RigidBody>(e))
        g.physics2D().setVelocity(e, {v.x, v.y});
    else {
        // A classic mistake: velocity needs physics. Say so once per object instead of doing nothing.
        static std::unordered_set<uint64_t> warned;
        if (warned.insert(g.scene().info(e).uuid.value).second)
            Log::warn("'", g.scene().info(e).name,
                         "' has no RigidBody2D, so changing its velocity does nothing. Add a RigidBody2D (Physics 2D) to it.");
    }
}

} // namespace

bool ScriptSystem::getProperty(Entity e, const std::string& name, Value& out) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    const Transform& t = reg.get<Transform>(e);
    const EntityInfo& info = reg.get<EntityInfo>(e);
    bool three = is3D(scene, e);
    int comps = three ? 3 : 2;
    if (name == "name") out = Value(info.name);
    else if (name == "tag") out = Value(info.tag);
    else if (name == "active") out = Value(info.active);
    else if (name == "visible") out = Value(!reg.has<Hidden>(e));
    else if (name == "id") out = Value(info.uuid.toString());
    else if (name == "exists" || name == "alive") out = Value(true);
    else if (name == "x") out = Value(t.position.x);
    else if (name == "y") out = Value(t.position.y);
    else if (name == "z") out = Value(t.position.z);
    else if (name == "position") out = fromVec3(t.position, comps);
    else if (name == "world_x") out = Value(scene.worldPosition(e).x);
    else if (name == "world_y") out = Value(scene.worldPosition(e).y);
    else if (name == "world_z") out = Value(scene.worldPosition(e).z);
    else if (name == "world_position") out = fromVec3(scene.worldPosition(e), comps);
    else if (name == "angle") out = Value(three ? t.rotation.y : t.rotation.z);
    else if (name == "rotation") out = fromVec3(t.rotation);
    else if (name == "rotation_x") out = Value(t.rotation.x);
    else if (name == "rotation_y") out = Value(t.rotation.y);
    else if (name == "rotation_z") out = Value(t.rotation.z);
    else if (name == "scale") out = fromVec3(t.scale, comps);
    else if (name == "scale_x") out = Value(t.scale.x);
    else if (name == "scale_y") out = Value(t.scale.y);
    else if (name == "scale_z") out = Value(t.scale.z);
    else if (name == "size" || name == "width" || name == "height") {
        Vec2 size{1, 1};
        if (auto* s = reg.tryGet<SpriteRenderer>(e))
            size = s->size;
        else if (auto* u = reg.tryGet<UIElement>(e))
            size = u->size;
        else
            size = {t.scale.x, t.scale.y};
        out = name == "width" ? Value(size.x) : name == "height" ? Value(size.y) : Value::vec(size.x, size.y, 0, 2);
    } else if (name == "color") {
        Color* c = colorOf(reg, e);
        out = c ? fromColor(*c) : Value();
    } else if (name == "alpha") {
        Color* c = colorOf(reg, e);
        out = Value(c ? c->a : 1.0f);
    } else if (name == "text") {
        std::string* s = textOf(reg, e);
        out = s ? Value(*s) : Value("");
    } else if (name == "image") {
        if (auto* s = reg.tryGet<SpriteRenderer>(e))
            out = Value(s->texture);
        else if (auto* u = reg.tryGet<UIImage>(e))
            out = Value(u->texture);
        else
            out = Value("");
    } else if (name == "shape") {
        auto* s = reg.tryGet<SpriteRenderer>(e);
        const ComponentInfo* ci = ComponentRegistry::find("SpriteRenderer");
        out = s ? Value(toSnakeCase(ci->findField("shape")->options.enumNames[static_cast<size_t>(s->shape)])) : Value();
    } else if (name == "frame") {
        auto* s = reg.tryGet<SpriteRenderer>(e);
        out = Value(s ? s->frame : 0);
    } else if (name == "flip_x") {
        auto* s = reg.tryGet<SpriteRenderer>(e);
        out = Value(s && s->flipX);
    } else if (name == "flip_y") {
        auto* s = reg.tryGet<SpriteRenderer>(e);
        out = Value(s && s->flipY);
    } else if (name == "order") {
        auto* s = reg.tryGet<SpriteRenderer>(e);
        out = Value(s ? s->order : 0);
    } else if (name == "velocity") out = fromVec3(velocityOf(game_, e), comps);
    else if (name == "velocity_x") out = Value(velocityOf(game_, e).x);
    else if (name == "velocity_y") out = Value(velocityOf(game_, e).y);
    else if (name == "velocity_z") out = Value(velocityOf(game_, e).z);
    else if (name == "on_ground") {
        bool g = game_.physics2D().isOnGround(e) || game_.physics3D().isOnGround(e);
        if (auto* cc = reg.tryGet<CharacterController>(e))
            g = g || cc->grounded;
        out = Value(g);
    } else if (name == "parent") {
        Entity p = scene.parent(e);
        out = p ? entityValue(p) : Value();
    } else if (name == "children") {
        std::vector<Value> kids;
        for (Entity c : scene.children(e))
            kids.push_back(entityValue(c));
        out = Value::list(std::move(kids));
    } else if (name == "forward" || name == "right" || name == "up") {
        Quat q = Quat::fromEuler(t.rotation);
        Vec3 d = name == "forward" ? (three ? Vec3(0, 0, -1) : Vec3(1, 0, 0)) : name == "right" ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
        if (!three && name == "right")
            d = {0, -1, 0};
        out = fromVec3(rotate(q, d), comps);
    } else if (name == "is_clone") {
        auto inst = instanceOf(e);
        Value* v = inst ? inst->find(intern("is_clone")) : nullptr;
        out = v ? *v : Value(false);
    } else {
        return false;
    }
    return true;
}

bool ScriptSystem::setProperty(Entity e, const std::string& name, const Value& v) {
    Scene& scene = game_.scene();
    auto& reg = scene.registry();
    Transform& t = reg.get<Transform>(e);
    EntityInfo& info = reg.get<EntityInfo>(e);
    auto num = [&](const char* what) { return toNumber(v, what); };
    bool three = is3D(scene, e);
    if (name == "name") info.name = toText(v);
    else if (name == "tag") info.tag = toText(v);
    else if (name == "active") info.active = v.truthy();
    else if (name == "visible") {
        if (v.truthy())
            reg.remove<Hidden>(e);
        else
            reg.getOrEmplace<Hidden>(e);
    } else if (name == "x") t.position.x = num("x");
    else if (name == "y") t.position.y = num("y");
    else if (name == "z") t.position.z = num("z");
    else if (name == "position") t.position = toVec3(v, "position", t.position);
    else if (name == "world_x" || name == "world_y" || name == "world_z") {
        Vec3 p = scene.worldPosition(e);
        (name == "world_x" ? p.x : name == "world_y" ? p.y : p.z) = num(name.c_str());
        scene.setWorldPosition(e, p);
    } else if (name == "world_position") scene.setWorldPosition(e, toVec3(v, "world_position", scene.worldPosition(e)));
    else if (name == "angle") (three ? t.rotation.y : t.rotation.z) = num("angle");
    else if (name == "rotation") t.rotation = v.isNumber() ? Vec3(0, 0, num("rotation")) : toVec3(v, "rotation", t.rotation);
    else if (name == "rotation_x") t.rotation.x = num("rotation_x");
    else if (name == "rotation_y") t.rotation.y = num("rotation_y");
    else if (name == "rotation_z") t.rotation.z = num("rotation_z");
    else if (name == "scale") {
        if (v.isNumber())
            t.scale = Vec3(num("scale"));
        else
            t.scale = toVec3(v, "scale", t.scale);
    } else if (name == "scale_x") t.scale.x = num("scale_x");
    else if (name == "scale_y") t.scale.y = num("scale_y");
    else if (name == "scale_z") t.scale.z = num("scale_z");
    else if (name == "size" || name == "width" || name == "height") {
        Vec2* size = nullptr;
        if (auto* s = reg.tryGet<SpriteRenderer>(e))
            size = &s->size;
        else if (auto* u = reg.tryGet<UIElement>(e))
            size = &u->size;
        if (!size)
            raise("Only sprites and UI elements have a size. Use scale for other objects.");
        if (name == "width")
            size->x = num("width");
        else if (name == "height")
            size->y = num("height");
        else if (v.isNumber())
            *size = Vec2(num("size"));
        else {
            Vec3 p = toVec3(v, "size");
            *size = {p.x, p.y};
        }
    } else if (name == "color") {
        Color* c = colorOf(reg, e);
        if (!c)
            raise("'" + info.name + "' has nothing to color. Add a SpriteRenderer, MeshRenderer or text first.");
        *c = toColor(v, "color");
    } else if (name == "alpha") {
        if (Color* c = colorOf(reg, e))
            c->a = saturate(num("alpha"));
    } else if (name == "text") {
        std::string* s = textOf(reg, e);
        if (!s)
            raise("'" + info.name + "' doesn't show text. Add a TextRenderer, UIText or UIButton first.");
        *s = toText(v);
    } else if (name == "image") {
        if (auto* s = reg.tryGet<SpriteRenderer>(e))
            s->texture = toText(v);
        else if (auto* u = reg.tryGet<UIImage>(e))
            u->texture = toText(v);
        else
            reg.emplace<SpriteRenderer>(e).texture = toText(v);
    } else if (name == "shape") {
        auto& s = reg.getOrEmplace<SpriteRenderer>(e);
        const ComponentInfo* ci = ComponentRegistry::find("SpriteRenderer");
        s.texture.clear();
        std::string wanted = toSnakeCase(toText(v));
        auto& names = ci->findField("shape")->options.enumNames;
        bool found = false;
        for (size_t i = 0; i < names.size(); ++i)
            if (toSnakeCase(names[i]) == wanted) {
                s.shape = static_cast<Shape2D>(i);
                found = true;
            }
        if (!found)
            raise("Unknown shape \"" + toText(v) +
                  "\". Try \"square\", \"circle\", \"triangle\", \"rounded_square\", \"diamond\", \"star\" or \"heart\".");
    } else if (name == "frame") reg.getOrEmplace<SpriteRenderer>(e).frame = static_cast<int>(num("frame"));
    else if (name == "flip_x") reg.getOrEmplace<SpriteRenderer>(e).flipX = v.truthy();
    else if (name == "flip_y") reg.getOrEmplace<SpriteRenderer>(e).flipY = v.truthy();
    else if (name == "order") reg.getOrEmplace<SpriteRenderer>(e).order = static_cast<int>(num("order"));
    else if (name == "velocity") setVelocityOf(game_, e, toVec3(v, "velocity", velocityOf(game_, e)));
    else if (name == "velocity_x" || name == "velocity_y" || name == "velocity_z") {
        Vec3 vel = velocityOf(game_, e);
        (name == "velocity_x" ? vel.x : name == "velocity_y" ? vel.y : vel.z) = num(name.c_str());
        setVelocityOf(game_, e, vel);
    } else if (name == "parent") {
        Entity p = entityFromValue(v);
        if (!v.isNone() && !p)
            raise("parent must be another object or None.");
        if (!scene.setParent(e, p))
            raise("Can't make an object a child of itself or of its own children.");
    } else if (name == "on_ground" || name == "id" || name == "children" || name == "exists" || name == "forward" ||
               name == "right" || name == "up") {
        raise("'" + name + "' can be read but not changed.");
    } else {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- lifecycle

ScriptSystem::ScriptSystem(Game& game) : game_(game) {
    gameData_ = Value::object(std::make_shared<GameDataObject>());
    vm_.onPrint = [](const std::string& text, const std::string& file, int line) {
        Log::write(LogLevel::Info, text, file, line);
    };
    vm_.onError = [this](const script::ScriptError& e) {
        ErrorKey key{e.file, e.line, e.what()};
        int& count = errorCounts_[key];
        ++count;
        // The same error every frame would flood the console; show it once, then every 5 seconds' worth.
        if (count == 1 || count % 300 == 0) {
            std::string text = e.what();
            if (count > 1)
                text += " (repeated " + std::to_string(count) + " times)";
            if (!e.trace.empty() && e.trace.find('\n') != e.trace.rfind('\n'))
                text += "\n" + e.trace;
            Log::write(LogLevel::Error, text, e.file, e.line);
        }
    };
    registerApi();
}

ScriptSystem::~ScriptSystem() = default;

std::string ScriptSystem::loadSource(const std::string& path, bool& ok) {
    auto text = fs::readText(game_.assets().resolve(path));
    if (!text) {
        ok = false;
        Log::write(LogLevel::Error, "Can't find the script '" + path + "'.", path, 0);
        return {};
    }
    ok = true;
    if (fs::extension(path) == ".blocks") {
        std::string error;
        std::string source = blocks::compileFile(*text, &error);
        if (!error.empty()) {
            ok = false;
            Log::write(LogLevel::Error, "The blocks in '" + path + "' couldn't be read: " + error, path, 0);
        }
        return source;
    }
    return *text;
}

std::shared_ptr<script::Module> ScriptSystem::module(const std::string& path, bool reportErrors) {
    auto it = modules_.find(path);
    if (it != modules_.end() && (it->second.module || it->second.failed))
        return it->second.module;
    CachedModule c;
    c.modified = fs::modifiedTime(game_.assets().resolve(path));
    bool ok = false;
    std::string source = loadSource(path, ok);
    if (ok) {
        auto saved = vm_.onError;
        if (!reportErrors)
            vm_.onError = nullptr;
        c.module = vm_.compile(source, path);
        vm_.onError = saved;
    }
    c.failed = !c.module;
    modules_[path] = c;
    return c.module;
}

void ScriptSystem::checkForChanges() {
    for (auto& [path, cached] : modules_) {
        int64_t m = fs::modifiedTime(game_.assets().resolve(path));
        if (m == 0 || m == cached.modified)
            continue;
        cached.modified = m;
        bool ok = false;
        std::string source = loadSource(path, ok);
        if (!ok)
            continue;
        auto fresh = vm_.compile(source, path);
        if (!fresh) {
            Log::warn("'", path, "' has an error, so the old version keeps running until it's fixed.");
            continue;
        }
        cached.module = fresh;
        cached.failed = false;
        int reloaded = 0;
        for (auto& [e, inst] : instances_) {
            auto* sc = game_.scene().registry().tryGet<Script>(e);
            if (!sc || sc->path != path)
                continue;
            if (vm_.reload(inst, fresh)) {
                applyOverrides(e, *inst);
                ++reloaded;
            }
        }
        Log::info("Reloaded ", path, reloaded ? " (" + std::to_string(reloaded) + " running)" : "");
    }
}

void ScriptSystem::applyOverrides(Entity e, script::Instance& inst) {
    auto* sc = game_.scene().registry().tryGet<Script>(e);
    if (!sc)
        return;
    for (auto& m : sc->overrides.members())
        inst.set(intern(m.key), script::VM::fromJson(m.value));
}

void ScriptSystem::attach(Entity e) {
    Scene& scene = game_.scene();
    auto* sc = scene.registry().tryGet<Script>(e);
    if (!sc || sc->path.empty() || instances_.count(e))
        return;
    auto mod = module(sc->path);
    if (!mod)
        return;
    auto inst = vm_.createInstance(mod, entityValue(e), scene.info(e).name);
    if (!inst)
        return;
    applyOverrides(e, *inst);
    instances_[e] = inst;
    startOrder_.push_back(e);
    pendingStart_.push_back(e);
}

void ScriptSystem::start() {
    stop();
    started_ = true;
    Scene& scene = game_.scene();
    scene.walk([&](Entity e, int) {
        attach(e);
        return true;
    });
}

void ScriptSystem::stop() {
    for (auto& [e, inst] : instances_)
        inst->alive = false;
    instances_.clear();
    entityValues_.clear();
    startOrder_.clear();
    pendingStart_.clear();
    tweens_.clear();
    errorCounts_.clear();
    vm_.clearTasks();
    started_ = false;
}

void ScriptSystem::onSpawn(Entity root) {
    if (!started_)
        return;
    Scene& scene = game_.scene();
    std::function<void(Entity)> visit = [&](Entity e) {
        attach(e);
        for (Entity c : scene.children(e))
            visit(c);
    };
    visit(root);
}

void ScriptSystem::onDestroy(Entity e) {
    auto it = instances_.find(e);
    if (it != instances_.end()) {
        auto inst = it->second;
        vm_.callFunction(inst, intern("on_destroy"), {}, false);
        inst->alive = false;
        vm_.cancelTasks(inst.get());
        instances_.erase(it);
    }
    std::erase(startOrder_, e);
    std::erase(pendingStart_, e);
    std::erase_if(tweens_, [&](const Tween& t) { return t.entity == e; });
    entityValues_.erase(e);
}

std::shared_ptr<script::Instance> ScriptSystem::instanceOf(Entity e) const {
    auto it = instances_.find(e);
    return it == instances_.end() ? nullptr : it->second;
}

Value ScriptSystem::entityValue(Entity e) {
    if (!e)
        return Value();
    auto it = entityValues_.find(e);
    if (it != entityValues_.end())
        return it->second;
    Value v = Value::object(std::make_shared<EntityObject>(*this, e));
    entityValues_.emplace(e, v);
    return v;
}

Entity ScriptSystem::entityFromValue(const Value& v) const {
    if (!v.isObject())
        return {};
    auto* obj = dynamic_cast<EntityObject*>(&v.nativeObject());
    if (!obj || !obj->alive())
        return {};
    return obj->entity();
}

void ScriptSystem::callAll(Symbol event, const std::vector<Value>& args, bool skipIfWaiting) {
    std::vector<Entity> order = startOrder_;
    Scene& scene = game_.scene();
    for (Entity e : order) {
        auto it = instances_.find(e);
        if (it == instances_.end() || !scene.valid(e) || !scene.isActive(e))
            continue;
        auto inst = it->second;
        if (skipIfWaiting && vm_.isWaiting(inst.get(), event))
            continue;
        vm_.callFunction(inst, event, args);
    }
}

void ScriptSystem::update(float dt) {
    deltaTime_ = dt;
    reloadTimer_ += dt;
    if (reloadTimer_ > 0.5f) {
        reloadTimer_ = 0;
        checkForChanges();
    }

    // Start newly attached scripts first, in hierarchy order.
    static const Symbol onStart = intern("on_start");
    while (!pendingStart_.empty()) {
        std::vector<Entity> batch = std::move(pendingStart_);
        pendingStart_.clear();
        for (Entity e : batch)
            if (auto inst = instanceOf(e))
                vm_.callFunction(inst, onStart, {});
    }

    vm_.update(dt);

    // Key events for "when key pressed" style scripts.
    static const Symbol onKey = intern("on_key_pressed");
    Input& input = game_.input();
    for (int k = 0; k < keys::Count; ++k)
        if (input.keyPressed(k)) {
            std::string keyName = Input::nameOfKey(k);
            if (!keyName.empty())
                callAll(onKey, {Value(keyName)}, false);
        }

    static const Symbol onUpdate = intern("on_update");
    callAll(onUpdate, {Value(dt)}, true);
    updateTweens(dt);
}

void ScriptSystem::fixedUpdate(float dt) {
    static const Symbol onFixed = intern("on_fixed_update");
    callAll(onFixed, {Value(dt)}, true);
}

void ScriptSystem::onCollision(Entity a, Entity b, bool begin, bool trigger) {
    static const Symbol collide = intern("on_collide"), collideEnd = intern("on_collide_end");
    static const Symbol trig = intern("on_trigger"), trigExit = intern("on_trigger_exit");
    Symbol event = trigger ? (begin ? trig : trigExit) : (begin ? collide : collideEnd);
    Scene& scene = game_.scene();
    for (auto [self, other] : {std::pair{a, b}, std::pair{b, a}}) {
        if (!scene.valid(self) || !scene.valid(other))
            continue;
        if (auto inst = instanceOf(self))
            vm_.callFunction(inst, event, {entityValue(other)});
        // Triggers also notify regular collision handlers so beginners only need on_collide.
        if (trigger && begin)
            if (auto inst = instanceOf(self))
                if (!inst->find(trig))
                    vm_.callFunction(inst, collide, {entityValue(other)});
    }
}

void ScriptSystem::onClick(Entity e) {
    static const Symbol onClickSym = intern("on_click");
    if (auto inst = instanceOf(e))
        vm_.callFunction(inst, onClickSym, {});
}

Value ScriptSystem::gameValue(const std::string& name) const {
    auto* obj = gameData_.as<GameDataObject>();
    if (Value* v = obj->data.dictObj().find(Value(name)))
        return *v;
    return Value();
}

void ScriptSystem::setGameValue(const std::string& name, const Value& v) {
    gameData_.as<GameDataObject>()->data.dictObj().set(Value(name), v);
}

double ScriptSystem::gameNumber(const std::string& name, double fallback) const {
    Value v = gameValue(name);
    return v.isNumber() ? v.number() : fallback;
}

void ScriptSystem::addToGameNumber(const std::string& name, double amount) {
    setGameValue(name, Value(gameNumber(name, 0) + amount));
}

void ScriptSystem::broadcast(const std::string& message, const Value& data) {
    static const Symbol onMessage = intern("on_message");
    callAll(onMessage, {Value(message), data}, false);
}

void ScriptSystem::addTween(Entity e, const std::string& property, float target, float duration, Easing easing,
                            Value onDone) {
    Value current;
    if (!getProperty(e, property, current) || !current.isNumber())
        raise("tween() can animate number properties like \"x\", \"y\", \"angle\", \"scale_x\" or \"alpha\", not \"" +
              property + "\".");
    // A new tween on the same property replaces the old one.
    std::erase_if(tweens_, [&](const Tween& t) { return t.entity == e && t.property == property; });
    Tween t;
    t.entity = e;
    t.property = property;
    t.from = static_cast<float>(current.number());
    t.to = target;
    t.duration = std::max(duration, 1e-4f);
    t.easing = easing;
    t.onDone = std::move(onDone);
    tweens_.push_back(std::move(t));
}

void ScriptSystem::updateTweens(float dt) {
    Scene& scene = game_.scene();
    std::vector<Tween> finished;
    for (auto it = tweens_.begin(); it != tweens_.end();) {
        if (!scene.valid(it->entity)) {
            it = tweens_.erase(it);
            continue;
        }
        it->elapsed += dt;
        float k = applyEasing(it->easing, it->elapsed / it->duration);
        try {
            setProperty(it->entity, it->property, Value(it->from + (it->to - it->from) * k));
        } catch (script::ScriptError& err) {
            Log::error(err.what());
            it = tweens_.erase(it);
            continue;
        }
        if (it->elapsed >= it->duration) {
            finished.push_back(std::move(*it));
            it = tweens_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& t : finished)
        if (t.onDone.isCallable())
            vm_.call(t.onDone, {entityValue(t.entity)});
}

// ---------------------------------------------------------------- global API

void ScriptSystem::registerApi() {
    auto def = [this](const char* name, const char* sig, int minA, int maxA, script::NativeFn fn) {
        vm_.defineFunction(name, sig, minA, maxA, std::move(fn));
    };
    Game& g = game_;

    vm_.setGlobal("game", gameData_);

    // --- input
    def("key_down", "key_down(\"space\")", 1, 1, [this, &g](CallArgs& a) { return Value(g.input().down(requireKeyName(*this, a, 0))); });
    def("key_pressed", "key_pressed(\"space\")", 1, 1, [this, &g](CallArgs& a) { return Value(g.input().pressed(requireKeyName(*this, a, 0))); });
    def("key_released", "key_released(\"space\")", 1, 1, [this, &g](CallArgs& a) { return Value(g.input().released(requireKeyName(*this, a, 0))); });
    auto button = [](CallArgs& a) {
        std::string b = a.has(0) ? a.string(0, "button") : "left";
        if (b == "left") return MouseButton::Left;
        if (b == "right") return MouseButton::Right;
        if (b == "middle") return MouseButton::Middle;
        raise(std::string(a.functionName) + "(): the button should be \"left\", \"right\" or \"middle\".");
    };
    def("mouse_down", "mouse_down(\"left\")", 0, 1, [&g, button](CallArgs& a) { return Value(g.input().mouseDown(button(a))); });
    def("mouse_pressed", "mouse_pressed(\"left\")", 0, 1, [&g, button](CallArgs& a) { return Value(g.input().mousePressed(button(a))); });
    def("mouse_released", "mouse_released(\"left\")", 0, 1, [&g, button](CallArgs& a) { return Value(g.input().mouseReleased(button(a))); });
    auto mouseWorld = [&g]() {
        Vec2 size = g.screenSize();
        CameraView cam = g.camera(size.x / std::max(size.y, 1.0f));
        return cam.screenToWorld(g.input().mousePosition(), size);
    };
    def("mouse_x", "mouse_x()", 0, 0, [mouseWorld](CallArgs&) { return Value(mouseWorld().x); });
    def("mouse_y", "mouse_y()", 0, 0, [mouseWorld](CallArgs&) { return Value(mouseWorld().y); });
    def("mouse_position", "mouse_position()", 0, 0, [mouseWorld](CallArgs&) {
        Vec3 p = mouseWorld();
        return Value::vec(p.x, p.y, p.z, 2);
    });
    def("mouse_screen_position", "mouse_screen_position()", 0, 0, [&g](CallArgs&) {
        Vec2 p = g.input().mousePosition();
        return Value::vec(p.x, p.y, 0, 2);
    });
    def("mouse_delta", "mouse_delta()", 0, 0, [&g](CallArgs&) {
        Vec2 d = g.input().mouseDelta();
        return Value::vec(d.x, d.y, 0, 2);
    });
    def("mouse_scroll", "mouse_scroll()", 0, 0, [&g](CallArgs&) { return Value(g.input().scroll().y); });
    def("axis", "axis(\"horizontal\") or axis(\"vertical\")", 1, 1, [&g](CallArgs& a) {
        const std::string& name = a.string(0, "name");
        if (name != "horizontal" && name != "vertical" && name != "x" && name != "y" && name != "look_x" && name != "look_y")
            raise("axis() understands \"horizontal\" and \"vertical\".");
        return Value(g.input().axis(name));
    });

    // --- objects
    def("find", "find(\"Player\")", 1, 1, [this, &g](CallArgs& a) {
        Entity e = g.scene().findByName(a.string(0, "name"));
        return e ? entityValue(e) : Value();
    });
    def("find_all", "find_all(\"enemy\")", 1, 1, [this, &g](CallArgs& a) {
        std::vector<Value> out;
        for (Entity e : g.scene().findAllWithTag(a.string(0, "tag")))
            if (g.scene().isActive(e))
                out.push_back(entityValue(e));
        return Value::list(std::move(out));
    });
    def("count", "count(\"enemy\")", 1, 1, [&g](CallArgs& a) {
        int n = 0;
        for (Entity e : g.scene().findAllWithTag(a.string(0, "tag")))
            if (g.scene().isActive(e))
                ++n;
        return Value(n);
    });
    def("spawn", "spawn(\"prefabs/coin.prefab\", x, y)", 1, 4, [this, &g](CallArgs& a) {
        Vec3 pos = positionArgs(*this, a, 1, "spawn()");
        Entity e = g.spawnPrefab(a.string(0, "prefab"), pos);
        return e ? entityValue(e) : Value();
    });
    def("create_sprite", "create_sprite(\"circle\", x, y, size=1, color=\"white\")", 1, 5, [this, &g](CallArgs& a) {
        Scene& scene = g.scene();
        const std::string& what = a.string(0, "shape_or_image");
        Entity e = scene.create(what);
        auto& sr = scene.registry().emplace<SpriteRenderer>(e);
        setProperty(e, what.find('.') != std::string::npos ? "image" : "shape", Value(what));
        if (a.has(1))
            scene.transform(e).position = positionArgs(*this, a, 1, "create_sprite()");
        float size = static_cast<float>(a.has(3) ? a.number(3, "size") : a.keywordNumber("size", 1));
        sr.size = Vec2(size);
        if (const Value* c = a.has(4) ? &a[4] : a.keyword("color"))
            sr.color = toColor(*c, "create_sprite() color");
        return entityValue(e);
    });
    def("create_text", "create_text(\"Hello\", x, y, size=0.5)", 1, 4, [this, &g](CallArgs& a) {
        Scene& scene = g.scene();
        Entity e = scene.create("Text");
        auto& tr = scene.registry().emplace<TextRenderer>(e);
        tr.text = a[0].toString();
        if (a.has(1))
            scene.transform(e).position = positionArgs(*this, a, 1, "create_text()");
        tr.fontSize = static_cast<float>(a.has(3) ? a.number(3, "size") : a.keywordNumber("size", 0.5));
        return entityValue(e);
    });
    def("destroy", "destroy(obj)", 1, 1, [this, &g](CallArgs& a) {
        if (Entity e = entityFromValue(a[0]))
            g.destroyEntity(e);
        else if (!a[0].isNone() && !a[0].isObject())
            raise("destroy() needs an object, like destroy(enemy).");
        return Value();
    });
    def("camera", "camera()", 0, 0, [this, &g](CallArgs&) {
        Entity e = SceneRenderer::findCamera(g.scene());
        return e ? entityValue(e) : Value();
    });
    def("distance", "distance(a, b)", 2, 2, [this](CallArgs& a) {
        return Value(static_cast<double>(length(targetPosition(*this, a[0], "distance()") - targetPosition(*this, a[1], "distance()"))));
    });
    def("direction", "direction(from, to)", 2, 2, [this](CallArgs& a) {
        Vec3 d = normalize(targetPosition(*this, a[1], "direction()") - targetPosition(*this, a[0], "direction()"));
        return fromVec3(d);
    });
    def("tween", "tween(obj, \"property\", target, seconds, \"ease_out\")", 4, 5, [this](CallArgs& a) {
        Entity e = entityFromValue(a[0]);
        if (!e)
            raise("tween(): the first value should be an object.");
        Easing easing = Easing::EaseOut;
        if (a.has(4) && !parseEasing(a.string(4, "easing"), easing))
            raise("tween(): unknown easing \"" + a[4].string() + "\".");
        addTween(e, a.string(1, "property"), static_cast<float>(a.number(2, "target")), static_cast<float>(a.number(3, "seconds")),
                 easing, Value());
        return Value();
    });
    def("broadcast", "broadcast(\"message\", data=None)", 1, 2, [this](CallArgs& a) {
        broadcast(a[0].toString(), a.has(1) ? a[1] : Value());
        return Value();
    });

    // --- scenes and game flow
    def("load_scene", "load_scene(\"scenes/level2.scene\")", 1, 1, [&g](CallArgs& a) {
        std::string path = a.string(0, "scene");
        if (path.find('.') == std::string::npos)
            path = "scenes/" + path + ".scene";
        if (!fs::exists(g.assets().resolve(path)))
            raise("load_scene(): there's no scene file \"" + path + "\" in the project.");
        g.requestSceneChange(path);
        return Value();
    });
    def("get_game", "get_game(\"score\", 0)", 2, 2, [this](CallArgs& a) {
        // Like game.score, but gives the default instead of an error when it hasn't been set yet.
        Value v = gameValue(a.string(0, "name"));
        return v.isNone() ? a[1] : v;
    });
    def("restart_scene", "restart_scene()", 0, 0, [&g](CallArgs&) {
        g.requestSceneChange(g.scenePath());
        return Value();
    });
    def("quit", "quit()", 0, 0, [&g](CallArgs&) {
        g.requestQuit();
        return Value();
    });
    def("pause_game", "pause_game()", 0, 0, [&g](CallArgs&) {
        g.setPaused(true);
        return Value();
    });
    def("resume_game", "resume_game()", 0, 0, [&g](CallArgs&) {
        g.setPaused(false);
        return Value();
    });
    def("is_paused", "is_paused()", 0, 0, [&g](CallArgs&) { return Value(g.paused()); });
    def("time", "time()", 0, 0, [&g](CallArgs&) { return Value(g.time()); });
    def("delta_time", "delta_time()", 0, 0, [this](CallArgs&) { return Value(deltaTime_); });
    def("screen_width", "screen_width()", 0, 0, [&g](CallArgs&) { return Value(g.screenSize().x); });
    def("screen_height", "screen_height()", 0, 0, [&g](CallArgs&) { return Value(g.screenSize().y); });
    def("set_fullscreen", "set_fullscreen(True)", 0, 1, [&g](CallArgs& a) {
        if (g.setFullscreen)
            g.setFullscreen(a.has(0) ? a[0].truthy() : true);
        return Value();
    });
    def("is_fullscreen", "is_fullscreen()", 0, 0, [&g](CallArgs&) { return Value(g.isFullscreen && g.isFullscreen()); });
    def("lock_mouse", "lock_mouse(True)", 0, 1, [&g](CallArgs& a) {
        if (g.setCursorLocked)
            g.setCursorLocked(a.has(0) ? a[0].truthy() : true);
        return Value();
    });
    def("camera_shake", "camera_shake(amount=0.3, seconds=0.3)", 0, 2, [&g](CallArgs& a) {
        g.gameplay().shake(static_cast<float>(a.numberOr(0, "amount", 0.3)), static_cast<float>(a.numberOr(1, "seconds", 0.3)));
        return Value();
    });
    def("set_time_scale", "set_time_scale(0.5)", 1, 1, [&g](CallArgs& a) {
        g.timeScale = std::max(0.0f, static_cast<float>(a.number(0, "scale")));
        return Value();
    });

    // --- physics
    def("set_gravity", "set_gravity(x, y) or set_gravity(x, y, z)", 2, 3, [&g](CallArgs& a) {
        if (a.size() == 3)
            g.physics3D().setGravity({static_cast<float>(a.number(0, "x")), static_cast<float>(a.number(1, "y")), static_cast<float>(a.number(2, "z"))});
        else
            g.physics2D().setGravity({static_cast<float>(a.number(0, "x")), static_cast<float>(a.number(1, "y"))});
        return Value();
    });
    def("raycast", "raycast(from, to)", 2, 2, [this, &g](CallArgs& a) {
        Vec3 from = toVec3(a[0], "raycast() start"), to = toVec3(a[1], "raycast() end");
        bool twoD = a[0].isVec() && a[0].vecObj().components == 2;
        RayHit hit;
        bool found = twoD ? g.physics2D().raycast({from.x, from.y}, {to.x, to.y}, hit)
                          : g.physics3D().raycast(from, normalize(to - from), length(to - from), hit);
        if (!found)
            return Value();
        Value d = Value::dict();
        d.dictObj().set(Value("object"), entityValue(hit.entity));
        d.dictObj().set(Value("point"), fromVec3(hit.point, twoD ? 2 : 3));
        d.dictObj().set(Value("normal"), fromVec3(hit.normal, twoD ? 2 : 3));
        d.dictObj().set(Value("distance"), Value(hit.distance));
        return d;
    });

    // --- audio
    def("play_sound", "play_sound(\"sounds/jump.wav\", volume=1, pitch=1)", 1, 3, [&g](CallArgs& a) {
        float volume = static_cast<float>(a.has(1) ? a.number(1, "volume") : a.keywordNumber("volume", 1));
        float pitch = static_cast<float>(a.has(2) ? a.number(2, "pitch") : a.keywordNumber("pitch", 1));
        g.audio().playSound(a.string(0, "sound"), volume, pitch);
        return Value();
    });
    def("play_music", "play_music(\"music/theme.ogg\", volume=1, loop=True)", 1, 3, [&g](CallArgs& a) {
        float volume = static_cast<float>(a.has(1) ? a.number(1, "volume") : a.keywordNumber("volume", 1));
        bool loop = a.has(2) ? a[2].truthy() : (a.keyword("loop") ? a.keyword("loop")->truthy() : true);
        g.audio().playMusic(a.string(0, "music"), volume, loop);
        return Value();
    });
    def("stop_music", "stop_music()", 0, 0, [&g](CallArgs&) {
        g.audio().stopMusic();
        return Value();
    });
    def("set_volume", "set_volume(0.5)", 1, 1, [&g](CallArgs& a) {
        g.audio().setMasterVolume(static_cast<float>(a.number(0, "volume")));
        return Value();
    });

    // --- saving
    auto savePath = [&g]() { return fs::userDataDir(g.settings().name) / "save.json"; };
    auto readSave = [savePath]() {
        auto text = fs::readText(savePath());
        Json j = text ? Json::parse(*text) : Json::object();
        return j.isObject() ? j : Json::object();
    };
    def("save_data", "save_data(\"high_score\", 100)", 2, 2, [savePath, readSave](CallArgs& a) {
        Json j = readSave();
        j[a[0].toString()] = script::VM::toJson(a[1]);
        if (!fs::writeText(savePath(), j.dump(2)))
            raise("save_data(): couldn't write the save file.");
        return Value();
    });
    def("load_data", "load_data(\"high_score\", 0)", 1, 2, [readSave](CallArgs& a) {
        Json j = readSave();
        std::string key = a[0].toString();
        if (!j.contains(key))
            return a.has(1) ? a[1] : Value();
        return script::VM::fromJson(j[key]);
    });
    def("has_data", "has_data(\"high_score\")", 1, 1, [readSave](CallArgs& a) { return Value(readSave().contains(a[0].toString())); });
    def("delete_data", "delete_data(\"high_score\")", 1, 1, [savePath, readSave](CallArgs& a) {
        Json j = readSave();
        j.erase(a[0].toString());
        fs::writeText(savePath(), j.dump(2));
        return Value();
    });
}

} // namespace aven
