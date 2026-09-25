#pragma once

#include "aven/core/json.h"
#include "aven/ecs/registry.h"
#include "aven/scene/components.h"

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aven {

// A scene is a tree of entities ("game objects"). Every entity has an
// EntityInfo (name/tag/active), a Transform and a place in the hierarchy.
class Scene {
public:
    Scene();

    Registry& registry() { return registry_; }
    const Registry& registry() const { return registry_; }

    // --- Entities
    Entity create(std::string name = "Entity", Entity parent = {});
    void destroy(Entity e);      // destroys children too
    void destroyLater(Entity e); // safe to call while the game is updating
    void flushDestroyed();
    bool valid(Entity e) const { return registry_.valid(e); }
    Entity duplicate(Entity e);
    size_t entityCount() const { return registry_.aliveCount(); }

    EntityInfo& info(Entity e) { return registry_.get<EntityInfo>(e); }
    const EntityInfo& info(Entity e) const { return registry_.get<EntityInfo>(e); }
    Transform& transform(Entity e) { return registry_.get<Transform>(e); }
    // Active itself and all its parents are active.
    bool isActive(Entity e) const;

    // --- Hierarchy
    Entity parent(Entity e) const;
    const std::vector<Entity>& children(Entity e) const;
    const std::vector<Entity>& roots() const { return roots_; }
    // Returns false (and does nothing) if it would make an entity its own ancestor.
    bool setParent(Entity child, Entity newParent, bool keepWorldTransform = true, int index = -1);
    bool isAncestor(Entity ancestor, Entity e) const;
    int siblingIndex(Entity e) const;
    // Depth-first walk in hierarchy order. Return false from fn to skip children.
    void walk(const std::function<bool(Entity, int depth)>& fn) const;

    // --- Lookup
    Entity findByName(std::string_view name) const;
    Entity findByUUID(UUID id) const;
    std::vector<Entity> findAllWithTag(std::string_view tag) const;

    // --- Transforms
    void updateTransforms();
    Mat4 worldMatrix(Entity e) const; // walks parents; always up to date
    Vec3 worldPosition(Entity e) const;
    void setWorldPosition(Entity e, Vec3 position);
    void setWorldMatrix(Entity e, const Mat4& world);

    // --- Saving
    Json save() const;
    bool load(const Json& data, std::string* error = nullptr);
    // Serializes entities and their children (used by prefabs, copy/paste, duplicate).
    Json saveEntities(const std::vector<Entity>& entities) const;
    // Creates copies of saved entities with fresh IDs. Returns the new top-level entities.
    std::vector<Entity> instantiate(const Json& data, Entity parent = {});

    void clear();

    std::string name = "Untitled";

private:
    Registry registry_;
    std::vector<Entity> roots_;
    std::unordered_map<UUID, Entity> byUUID_;
    std::vector<Entity> pendingDestroy_;

    Entity createWithId(UUID id, std::string name);
    std::vector<Entity>& siblingList(Entity parent);
    void detach(Entity e);
    Json saveEntity(Entity e) const;
    void loadEntityComponents(Entity e, const Json& components);
};

} // namespace aven
