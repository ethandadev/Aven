#include "aven/scene/scene.h"

#include "aven/core/log.h"
#include "aven/scene/reflection.h"

#include <algorithm>
#include <cmath>

namespace aven {

namespace {
const std::vector<Entity> kNoChildren;
constexpr int kSceneFormatVersion = 1;
} // namespace

Scene::Scene() = default;

Entity Scene::createWithId(UUID id, std::string entityName) {
    Entity e = registry_.create();
    auto& info = registry_.emplace<EntityInfo>(e);
    info.name = std::move(entityName);
    info.uuid = id;
    registry_.emplace<Hierarchy>(e);
    registry_.emplace<Transform>(e);
    registry_.emplace<WorldTransform>(e);
    byUUID_[id] = e;
    return e;
}

Entity Scene::create(std::string entityName, Entity parentEntity) {
    Entity e = createWithId(UUID::generate(), std::move(entityName));
    roots_.push_back(e);
    if (parentEntity)
        setParent(e, parentEntity, false);
    return e;
}

std::vector<Entity>& Scene::siblingList(Entity parentEntity) {
    if (parentEntity && registry_.valid(parentEntity))
        return registry_.get<Hierarchy>(parentEntity).children;
    return roots_;
}

void Scene::detach(Entity e) {
    auto& h = registry_.get<Hierarchy>(e);
    auto& list = siblingList(h.parent);
    list.erase(std::remove(list.begin(), list.end(), e), list.end());
    h.parent = {};
}

void Scene::destroy(Entity e) {
    if (!registry_.valid(e))
        return;
    std::vector<Entity> kids = registry_.get<Hierarchy>(e).children;
    for (Entity c : kids)
        destroy(c);
    detach(e);
    byUUID_.erase(registry_.get<EntityInfo>(e).uuid);
    registry_.destroy(e);
}

void Scene::destroyLater(Entity e) {
    if (registry_.valid(e) && std::find(pendingDestroy_.begin(), pendingDestroy_.end(), e) == pendingDestroy_.end())
        pendingDestroy_.push_back(e);
}

void Scene::flushDestroyed() {
    auto pending = std::move(pendingDestroy_);
    pendingDestroy_.clear();
    for (Entity e : pending)
        destroy(e);
}

bool Scene::isActive(Entity e) const {
    while (registry_.valid(e)) {
        if (!registry_.get<EntityInfo>(e).active)
            return false;
        e = registry_.get<Hierarchy>(e).parent;
    }
    return true;
}

Entity Scene::parent(Entity e) const {
    return registry_.valid(e) ? registry_.get<Hierarchy>(e).parent : Entity{};
}

const std::vector<Entity>& Scene::children(Entity e) const {
    return registry_.valid(e) ? registry_.get<Hierarchy>(e).children : kNoChildren;
}

bool Scene::isAncestor(Entity ancestor, Entity e) const {
    for (Entity p = parent(e); p; p = parent(p))
        if (p == ancestor)
            return true;
    return false;
}

int Scene::siblingIndex(Entity e) const {
    Entity p = parent(e);
    const auto& list = p ? children(p) : roots_;
    auto it = std::find(list.begin(), list.end(), e);
    return it == list.end() ? -1 : static_cast<int>(it - list.begin());
}

bool Scene::setParent(Entity child, Entity newParent, bool keepWorldTransform, int index) {
    if (!registry_.valid(child) || child == newParent)
        return false;
    if (newParent && (!registry_.valid(newParent) || isAncestor(child, newParent)))
        return false;
    Mat4 world = worldMatrix(child);
    detach(child);
    registry_.get<Hierarchy>(child).parent = newParent;
    auto& list = siblingList(newParent);
    if (index < 0 || index > static_cast<int>(list.size()))
        list.push_back(child);
    else
        list.insert(list.begin() + index, child);
    if (keepWorldTransform)
        setWorldMatrix(child, world);
    return true;
}

void Scene::walk(const std::function<bool(Entity, int)>& fn) const {
    // Depth first, parents before children, with a stack of its own instead of recursion: the
    // renderer and the editor walk the whole scene several times a frame. Children are taken when
    // their parent is visited, so fn can add or remove objects as it goes. (A stack per call: fn
    // may walk the scene again.)
    std::vector<std::pair<Entity, int>> stack;
    stack.reserve(64);
    for (auto it = roots_.rbegin(); it != roots_.rend(); ++it)
        stack.emplace_back(*it, 0);
    while (!stack.empty()) {
        auto [e, depth] = stack.back();
        stack.pop_back();
        if (!registry_.valid(e) || !fn(e, depth))
            continue;
        const auto* h = registry_.tryGet<Hierarchy>(e); // (fn may have destroyed e)
        if (!h)
            continue;
        const auto& kids = h->children;
        for (auto it = kids.rbegin(); it != kids.rend(); ++it)
            stack.emplace_back(*it, depth + 1);
    }
}

Entity Scene::findByName(std::string_view entityName) const {
    Entity found;
    walk([&](Entity e, int) {
        if (!found && registry_.get<EntityInfo>(e).name == entityName)
            found = e;
        return !found;
    });
    return found;
}

bool Scene::setUUID(Entity e, UUID id) {
    if (!registry_.valid(e) || !id)
        return false;
    auto& info = registry_.get<EntityInfo>(e);
    if (info.uuid == id)
        return true;
    if (findByUUID(id))
        return false;
    byUUID_.erase(info.uuid);
    info.uuid = id;
    byUUID_[id] = e;
    return true;
}

Entity Scene::findByUUID(UUID id) const {
    auto it = byUUID_.find(id);
    return it != byUUID_.end() && registry_.valid(it->second) ? it->second : Entity{};
}

std::vector<Entity> Scene::findAllWithTag(std::string_view tag) const {
    std::vector<Entity> out;
    walk([&](Entity e, int) {
        if (registry_.get<EntityInfo>(e).tag == tag)
            out.push_back(e);
        return true;
    });
    return out;
}

namespace {

// A position, rotation or scale that isn't a number (nan, inf from a script's maths) would spread
// to physics and drawing; it's put back to an ordinary value, and the Console says where.
void keepFinite(Transform& t, const std::string& name) {
    bool fixed = false;
    auto fix = [&](Vec3& v, float fallback) {
        for (float* c : {&v.x, &v.y, &v.z})
            if (!std::isfinite(*c)) {
                *c = fallback;
                fixed = true;
            }
    };
    fix(t.position, 0.0f);
    fix(t.rotation, 0.0f);
    fix(t.scale, 1.0f);
    if (fixed)
        Log::warn("'", name, "' got a position, rotation or scale that isn't a number (like nan or inf, often from "
                  "dividing by zero), so it was reset.");
}

} // namespace

void Scene::keepTransformsFinite() {
    registry_.each<Transform, EntityInfo>([](Entity, Transform& t, EntityInfo& info) {
        if (!std::isfinite(t.position.x + t.position.y + t.position.z + t.rotation.x + t.rotation.y + t.rotation.z +
                           t.scale.x + t.scale.y + t.scale.z))
            keepFinite(t, info.name);
    });
}

void Scene::updateTransforms() {
    std::function<void(Entity, const Mat4&)> visit = [&](Entity e, const Mat4& parentWorld) {
        Transform& t = registry_.get<Transform>(e);
        if (!std::isfinite(t.position.x + t.position.y + t.position.z + t.rotation.x + t.rotation.y + t.rotation.z +
                           t.scale.x + t.scale.y + t.scale.z))
            keepFinite(t, registry_.get<EntityInfo>(e).name);
        Mat4 world = parentWorld * t.localMatrix();
        registry_.get<WorldTransform>(e).matrix = world;
        for (Entity c : registry_.get<Hierarchy>(e).children)
            visit(c, world);
    };
    Mat4 identity;
    for (Entity e : roots_)
        visit(e, identity);
}

Mat4 Scene::worldMatrix(Entity e) const {
    if (!registry_.valid(e))
        return {};
    Mat4 m = registry_.get<Transform>(e).localMatrix();
    for (Entity p = parent(e); p; p = parent(p))
        m = registry_.get<Transform>(p).localMatrix() * m;
    return m;
}

Vec3 Scene::worldPosition(Entity e) const {
    Mat4 m = worldMatrix(e);
    return {m.m[12], m.m[13], m.m[14]};
}

void Scene::setWorldPosition(Entity e, Vec3 position) {
    if (!registry_.valid(e))
        return;
    Entity p = parent(e);
    Vec3 local = p ? transformPoint(inverse(worldMatrix(p)), position) : position;
    registry_.get<Transform>(e).position = local;
}

void Scene::setWorldMatrix(Entity e, const Mat4& world) {
    Entity p = parent(e);
    Mat4 local = p ? inverse(worldMatrix(p)) * world : world;
    Vec3 t, s;
    Quat r;
    if (!decompose(local, t, r, s))
        return;
    auto& tr = registry_.get<Transform>(e);
    Vec3 oldEuler = tr.rotation;
    tr.position = t;
    tr.scale = s;
    // Keep the user's original angles if the rotation did not actually change,
    // so values like 360 or -90 are not rewritten to equivalent angles.
    if (std::abs(dot(Quat::fromEuler(oldEuler), r)) < 0.99999f)
        tr.rotation = r.toEuler();
}

// ---------------------------------------------------------------- saving

Json Scene::saveEntity(Entity e) const {
    const auto& info = registry_.get<EntityInfo>(e);
    Json j = Json::object();
    j["id"] = info.uuid.toString();
    j["name"] = info.name;
    if (!info.tag.empty())
        j["tag"] = info.tag;
    if (!info.layer.empty() && info.layer != "Default")
        j["layer"] = info.layer;
    if (!info.active)
        j["active"] = false;
    Entity p = parent(e);
    if (p)
        j["parent"] = registry_.get<EntityInfo>(p).uuid.toString();
    Json comps = Json::object();
    auto& reg = const_cast<Registry&>(registry_);
    for (auto& ci : ComponentRegistry::all())
        if (const void* c = ci.get(reg, e))
            comps[ci.name] = saveComponent(ci, c);
    j["components"] = std::move(comps);
    return j;
}

Json Scene::saveEntities(const std::vector<Entity>& entities) const {
    Json list = Json::array();
    std::vector<Entity> tops;
    for (Entity e : entities) {
        if (!registry_.valid(e))
            continue;
        bool nested = false;
        for (Entity other : entities)
            if (other != e && isAncestor(other, e))
                nested = true;
        if (!nested)
            tops.push_back(e);
    }
    std::function<void(Entity)> visit = [&](Entity e) {
        list.push(saveEntity(e));
        for (Entity c : children(e))
            visit(c);
    };
    for (Entity e : tops)
        visit(e);
    Json j = Json::object();
    j["entities"] = std::move(list);
    return j;
}

Json Scene::save() const {
    Json j = Json::object();
    j["aven"] = "scene";
    j["version"] = kSceneFormatVersion;
    j["name"] = name;
    Json saved = saveEntities(roots_);
    j["entities"] = saved["entities"];
    return j;
}

void Scene::loadEntityComponents(Entity e, const Json& comps) {
    for (auto& member : comps.members()) {
        const ComponentInfo* ci = ComponentRegistry::find(member.key);
        if (!ci) {
            Log::warn("Unknown component '", member.key, "' on '", info(e).name, "' was skipped");
            continue;
        }
        loadComponent(*ci, ci->add(registry_, e), member.value);
    }
}

void Scene::clear() {
    registry_.clear();
    roots_.clear();
    byUUID_.clear();
    pendingDestroy_.clear();
}

// Puts loaded entities into the hierarchy once they all exist, so a child listed before its parent
// (a hand-edited or merged file) still finds it. A parent that would make a loop, or that isn't
// there, leaves the entity at the top level (or under `fallback`).
void Scene::linkLoaded(const std::vector<std::pair<Entity, UUID>>& wanted, Entity fallback, std::vector<Entity>* tops) {
    for (auto& [e, parentId] : wanted) {
        Entity p = parentId ? findByUUID(parentId) : Entity{};
        bool linked = p && p != e && !isAncestor(e, p);
        if (!linked)
            p = fallback;
        registry_.get<Hierarchy>(e).parent = p;
        siblingList(p).push_back(e);
        if (!linked && tops)
            tops->push_back(e);
    }
}

bool Scene::load(const Json& data, std::string* error) {
    if (!data.isObject() || !data["entities"].isArray()) {
        if (error)
            *error = "This is not an Aven scene file.";
        return false;
    }
    if (data["version"].asInt(1) > kSceneFormatVersion)
        Log::warn("Scene was saved by a newer version of Aven; some things may be missing.");
    clear();
    name = data["name"].asString("Untitled");
    std::vector<std::pair<Entity, UUID>> wanted;
    std::vector<const Json*> components;
    for (auto& ej : data["entities"].elements()) {
        UUID id = UUID::fromString(ej["id"].asString());
        if (!id || byUUID_.count(id))
            id = UUID::generate();
        Entity e = createWithId(id, ej["name"].asString("Entity"));
        auto& info = registry_.get<EntityInfo>(e);
        info.tag = ej["tag"].asString();
        info.layer = ej["layer"].asString();
        info.active = ej["active"].asBool(true);
        wanted.push_back({e, ej.contains("parent") ? UUID::fromString(ej["parent"].asString()) : UUID{}});
        components.push_back(&ej["components"]);
    }
    linkLoaded(wanted, {}, nullptr);
    for (size_t i = 0; i < wanted.size(); ++i)
        loadEntityComponents(wanted[i].first, *components[i]);
    updateTransforms();
    return true;
}

std::vector<Entity> Scene::instantiate(const Json& data, Entity parentEntity) {
    // Every copy gets a fresh id; an entry without one (or with one used twice) gets its own.
    std::unordered_map<std::string, UUID> remap;
    std::vector<UUID> ids;
    for (auto& ej : data["entities"].elements()) {
        std::string old = ej["id"].asString("");
        UUID fresh = UUID::generate();
        if (!old.empty() && !remap.count(old))
            remap[old] = fresh;
        ids.push_back(fresh);
    }
    std::vector<std::pair<Entity, UUID>> wanted;
    std::vector<const Json*> components;
    std::vector<Entity> created, tops;
    size_t i = 0;
    for (auto& ej : data["entities"].elements()) {
        Entity e = createWithId(ids[i++], ej["name"].asString("Entity"));
        auto& info = registry_.get<EntityInfo>(e);
        info.tag = ej["tag"].asString();
        info.layer = ej["layer"].asString();
        info.active = ej["active"].asBool(true);
        std::string parentId = ej["parent"].asString("");
        auto pit = parentId.empty() ? remap.end() : remap.find(parentId);
        wanted.push_back({e, pit != remap.end() ? pit->second : UUID{}});
        components.push_back(&ej["components"]);
        created.push_back(e);
    }
    linkLoaded(wanted, parentEntity, &tops);
    for (size_t k = 0; k < created.size(); ++k)
        loadEntityComponents(created[k], *components[k]);
    // Entity references inside the copied set point at the new copies.
    auto fix = [&](UUID& id) {
        auto it = remap.find(id.toString());
        if (it != remap.end())
            id = it->second;
    };
    for (Entity e : created)
        for (auto& ci : ComponentRegistry::all())
            if (void* c = ci.get(registry_, e)) {
                for (auto& f : ci.fields)
                    if (f.type == FieldType::EntityRef)
                        fix(f.ref<UUID>(c));
                if (ci.eachRef)
                    ci.eachRef(c, fix);
            }
    updateTransforms();
    return tops;
}

Entity Scene::duplicate(Entity e) {
    if (!registry_.valid(e))
        return {};
    Entity p = parent(e);
    int index = siblingIndex(e);
    auto copies = instantiate(saveEntities({e}), p);
    if (copies.empty())
        return {};
    Entity copy = copies.front();
    setParent(copy, p, false, index + 1);
    return copy;
}

} // namespace aven
