#pragma once

// Entity-component storage using sparse sets: each component type lives in a
// tightly packed array, so systems iterate cache-friendly data. Beginners never
// see this directly; they see "entities with components" in the editor.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

namespace aven {

struct Entity {
    static constexpr uint32_t kInvalidIndex = 0xFFFFFFFFu;
    uint32_t index = kInvalidIndex;
    uint32_t generation = 0;

    bool isNull() const { return index == kInvalidIndex; }
    explicit operator bool() const { return !isNull(); }
    bool operator==(const Entity&) const = default;

    uint64_t toHandle() const { return isNull() ? 0 : ((uint64_t(generation) << 32) | (uint64_t(index) + 1)); }
    static Entity fromHandle(uint64_t h) {
        if (h == 0)
            return {};
        return {static_cast<uint32_t>((h & 0xFFFFFFFFu) - 1), static_cast<uint32_t>(h >> 32)};
    }
};

namespace detail {
inline uint32_t nextComponentTypeId() {
    static uint32_t counter = 0;
    return counter++;
}
} // namespace detail

template <class T> uint32_t componentTypeId() {
    static const uint32_t id = detail::nextComponentTypeId();
    return id;
}

class ComponentPoolBase {
public:
    virtual ~ComponentPoolBase() = default;
    virtual bool contains(uint32_t index) const = 0;
    virtual void erase(uint32_t index) = 0;
    virtual void clear() = 0;
    virtual size_t size() const = 0;
    virtual const std::vector<uint32_t>& indices() const = 0;
};

template <class T> class ComponentPool final : public ComponentPoolBase {
public:
    static constexpr uint32_t kEmpty = 0xFFFFFFFFu;

    bool contains(uint32_t index) const override {
        return index < sparse_.size() && sparse_[index] != kEmpty;
    }

    template <class... Args> T& emplace(uint32_t index, Args&&... args) {
        if (index >= sparse_.size())
            sparse_.resize(index + 1, kEmpty);
        if (sparse_[index] != kEmpty) {
            data_[sparse_[index]] = T{std::forward<Args>(args)...};
            return data_[sparse_[index]];
        }
        sparse_[index] = static_cast<uint32_t>(dense_.size());
        dense_.push_back(index);
        data_.push_back(T{std::forward<Args>(args)...});
        return data_.back();
    }

    T* find(uint32_t index) {
        return contains(index) ? &data_[sparse_[index]] : nullptr;
    }
    const T* find(uint32_t index) const {
        return contains(index) ? &data_[sparse_[index]] : nullptr;
    }

    void erase(uint32_t index) override {
        if (!contains(index))
            return;
        uint32_t slot = sparse_[index];
        uint32_t last = static_cast<uint32_t>(dense_.size() - 1);
        if (slot != last) {
            dense_[slot] = dense_[last];
            data_[slot] = std::move(data_[last]);
            sparse_[dense_[slot]] = slot;
        }
        dense_.pop_back();
        data_.pop_back();
        sparse_[index] = kEmpty;
    }

    void clear() override {
        sparse_.clear();
        dense_.clear();
        data_.clear();
    }

    size_t size() const override { return dense_.size(); }
    const std::vector<uint32_t>& indices() const override { return dense_; }

private:
    std::vector<uint32_t> sparse_;
    std::vector<uint32_t> dense_;
    std::vector<T> data_;
};

// Note: references returned by get/emplace stay valid only until another
// component of the same type is added or removed.
class Registry {
public:
    Entity create() {
        uint32_t index;
        if (!free_.empty()) {
            index = free_.back();
            free_.pop_back();
        } else {
            index = static_cast<uint32_t>(generations_.size());
            generations_.push_back(0);
            alive_.push_back(false);
        }
        alive_[index] = true;
        ++aliveCount_;
        return {index, generations_[index]};
    }

    void destroy(Entity e) {
        if (!valid(e))
            return;
        for (auto& pool : pools_)
            if (pool)
                pool->erase(e.index);
        alive_[e.index] = false;
        ++generations_[e.index];
        free_.push_back(e.index);
        --aliveCount_;
    }

    bool valid(Entity e) const {
        return !e.isNull() && e.index < generations_.size() && alive_[e.index] &&
               generations_[e.index] == e.generation;
    }

    // Entity handle for an index known to be alive (used when walking pools).
    Entity entityAt(uint32_t index) const { return {index, generations_[index]}; }

    template <class T, class... Args> T& emplace(Entity e, Args&&... args) {
        assert(valid(e));
        return pool<T>().emplace(e.index, std::forward<Args>(args)...);
    }

    template <class T> T& getOrEmplace(Entity e) {
        if (T* c = tryGet<T>(e))
            return *c;
        return emplace<T>(e);
    }

    template <class T> T* tryGet(Entity e) {
        if (!valid(e))
            return nullptr;
        auto* p = findPool<T>();
        return p ? p->find(e.index) : nullptr;
    }
    template <class T> const T* tryGet(Entity e) const {
        if (!valid(e))
            return nullptr;
        auto* p = findPool<T>();
        return p ? p->find(e.index) : nullptr;
    }

    template <class T> T& get(Entity e) {
        T* c = tryGet<T>(e);
        assert(c && "entity does not have this component");
        return *c;
    }
    template <class T> const T& get(Entity e) const {
        const T* c = tryGet<T>(e);
        assert(c && "entity does not have this component");
        return *c;
    }

    template <class T> bool has(Entity e) const {
        if (!valid(e))
            return false;
        auto* p = findPool<T>();
        return p && p->contains(e.index);
    }

    template <class T> void remove(Entity e) {
        if (!valid(e))
            return;
        if (auto* p = findPool<T>())
            p->erase(e.index);
    }

    template <class T> size_t count() const {
        auto* p = findPool<T>();
        return p ? p->size() : 0;
    }

    // Calls fn(Entity, First&, Rest&...) for every entity having all listed components.
    // Safe against entities or components being added/removed during iteration.
    template <class First, class... Rest, class Fn> void each(Fn&& fn) {
        auto* first = findPool<First>();
        if (!first || first->size() == 0)
            return;
        std::vector<uint32_t> snapshot = first->indices();
        for (uint32_t index : snapshot) {
            if (!alive_[index])
                continue;
            Entity e{index, generations_[index]};
            First* a = first->find(index);
            if (!a)
                continue;
            if constexpr (sizeof...(Rest) == 0) {
                fn(e, *a);
            } else {
                if (!(has<Rest>(e) && ...))
                    continue;
                fn(e, *a, *tryGet<Rest>(e)...);
            }
        }
    }

    // Entities having component T, in storage order.
    template <class T> std::vector<Entity> entitiesWith() const {
        std::vector<Entity> out;
        if (auto* p = findPool<T>()) {
            out.reserve(p->size());
            for (uint32_t index : p->indices())
                out.push_back({index, generations_[index]});
        }
        return out;
    }

    size_t aliveCount() const { return aliveCount_; }

    template <class Fn> void forEachEntity(Fn&& fn) const {
        for (uint32_t i = 0; i < alive_.size(); ++i)
            if (alive_[i])
                fn(Entity{i, generations_[i]});
    }

    void clear() {
        for (auto& pool : pools_)
            if (pool)
                pool->clear();
        for (uint32_t i = 0; i < alive_.size(); ++i) {
            if (alive_[i]) {
                alive_[i] = false;
                ++generations_[i];
                free_.push_back(i);
            }
        }
        aliveCount_ = 0;
    }

private:
    std::vector<uint32_t> generations_;
    std::vector<bool> alive_;
    std::vector<uint32_t> free_;
    std::vector<std::unique_ptr<ComponentPoolBase>> pools_;
    size_t aliveCount_ = 0;

    template <class T> ComponentPool<T>& pool() {
        uint32_t id = componentTypeId<T>();
        if (id >= pools_.size())
            pools_.resize(id + 1);
        if (!pools_[id])
            pools_[id] = std::make_unique<ComponentPool<T>>();
        return static_cast<ComponentPool<T>&>(*pools_[id]);
    }

    template <class T> ComponentPool<T>* findPool() const {
        uint32_t id = componentTypeId<T>();
        if (id >= pools_.size() || !pools_[id])
            return nullptr;
        return static_cast<ComponentPool<T>*>(pools_[id].get());
    }
};

} // namespace aven

template <> struct std::hash<aven::Entity> {
    size_t operator()(const aven::Entity& e) const noexcept { return std::hash<uint64_t>{}(e.toHandle()); }
};
