#pragma once

#include "engine/ecs/Entity.h"

#include <cstdint>
#include <memory>
#include <tuple>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace ecs {

// --- Component storage: a sparse set ---------------------------------------
//
// A sparse set keeps two arrays:
//   * `dense`  — a *packed* list of the entities that have this component (and a
//                parallel `comps` array of the component data). Packed = fast,
//                cache-friendly iteration with no gaps.
//   * `sparse` — indexed by an entity's INDEX, giving the position of that entity
//                inside `dense` (or kInvalid). This makes has/get O(1).
//
// Removal is swap-and-pop: move the last element into the hole and fix one
// pointer, so it stays O(1) and `dense` stays packed (order is not preserved).

class IPool {
public:
    virtual ~IPool() = default;
    virtual void Remove(Entity e) = 0;
    virtual bool Has(Entity e) const = 0;
};

template <class T>
class Pool : public IPool {
public:
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;

    std::vector<std::uint32_t> sparse;  // entity index -> position in dense
    std::vector<Entity>        dense;   // packed entities that own a T
    std::vector<T>             comps;   // component data, parallel to dense

    bool Has(Entity e) const override {
        const std::uint32_t i = EntityIndex(e);
        return i < sparse.size() && sparse[i] != kInvalid && dense[sparse[i]] == e;
    }

    T& Add(Entity e, T value) {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size()) sparse.resize(i + 1, kInvalid);
        if (sparse[i] != kInvalid) {                 // already present: overwrite
            comps[sparse[i]] = std::move(value);
            return comps[sparse[i]];
        }
        sparse[i] = static_cast<std::uint32_t>(dense.size());
        dense.push_back(e);
        comps.push_back(std::move(value));
        return comps.back();
    }

    T& Get(Entity e) { return comps[sparse[EntityIndex(e)]]; }

    void Remove(Entity e) override {
        const std::uint32_t i = EntityIndex(e);
        if (i >= sparse.size() || sparse[i] == kInvalid) return;
        const std::uint32_t hole = sparse[i];
        const std::uint32_t last = static_cast<std::uint32_t>(dense.size() - 1);
        dense[hole] = dense[last];                      // move last element into the hole
        comps[hole] = std::move(comps[last]);
        sparse[EntityIndex(dense[hole])] = hole;        // repoint the moved entity
        dense.pop_back();
        comps.pop_back();
        sparse[i] = kInvalid;
    }
};

template <class... Cs> class View;  // forward declaration

// --- The Registry: owns entities and component pools -----------------------

class Registry {
public:
    // Create a fresh entity, reusing a recycled slot if one is free.
    Entity Create() {
        std::uint32_t index;
        if (!m_free.empty()) {
            index = m_free.back();
            m_free.pop_back();
        } else {
            index = static_cast<std::uint32_t>(m_generations.size());
            m_generations.push_back(0);
        }
        return MakeEntity(index, m_generations[index]);
    }

    // True if `e` still refers to a live entity (generation matches).
    bool Valid(Entity e) const {
        const std::uint32_t i = EntityIndex(e);
        return i < m_generations.size() && m_generations[i] == EntityGeneration(e);
    }

    // Destroy `e`: drop all its components, bump its generation (invalidating any
    // outstanding handles), and free its slot for reuse.
    void Destroy(Entity e) {
        if (!Valid(e)) return;
        for (auto& kv : m_pools) kv.second->Remove(e);
        ++m_generations[EntityIndex(e)];
        m_free.push_back(EntityIndex(e));
    }

    template <class T> T& Add(Entity e, T value = T{}) {
        return Assure<T>().Add(e, std::move(value));
    }
    template <class T, class... Args> T& Emplace(Entity e, Args&&... args) {
        return Assure<T>().Add(e, T{std::forward<Args>(args)...});
    }
    template <class T> bool Has(Entity e) { auto* p = TryPool<T>(); return p && p->Has(e); }
    template <class T> T&   Get(Entity e) { return Assure<T>().Get(e); }
    template <class T> T*   TryGet(Entity e) {
        auto* p = TryPool<T>();
        return (p && p->Has(e)) ? &p->Get(e) : nullptr;
    }
    template <class T> void Remove(Entity e) { if (auto* p = TryPool<T>()) p->Remove(e); }

    // Iterate entities that have every component in Cs (see View below).
    template <class... Cs> View<Cs...> view() { return View<Cs...>(*this); }

    std::size_t AliveCount() const { return m_generations.size() - m_free.size(); }

    template <class T> Pool<T>* TryPool() {
        auto it = m_pools.find(std::type_index(typeid(T)));
        return it == m_pools.end() ? nullptr : static_cast<Pool<T>*>(it->second.get());
    }
private:
    template <class T> Pool<T>& Assure() {
        const std::type_index ti(typeid(T));
        auto it = m_pools.find(ti);
        if (it == m_pools.end())
            it = m_pools.emplace(ti, std::make_unique<Pool<T>>()).first;
        return *static_cast<Pool<T>*>(it->second.get());
    }

    std::vector<std::uint8_t>  m_generations;  // generation per index
    std::vector<std::uint32_t> m_free;         // recycled indices
    std::unordered_map<std::type_index, std::unique_ptr<IPool>> m_pools;
};

// --- View: iterate entities that have all of Cs ----------------------------
//
//   reg.view<Position, Velocity>().each([](Entity e, Position& p, Velocity& v) {
//       p.value += v.value * dt;
//   });
//
// It iterates the FIRST listed component's pool and checks the rest, so listing
// the rarest component first is the cheap optimisation. Iterating in reverse
// makes it safe to Remove the current entity during the loop.
template <class... Cs>
class View {
public:
    explicit View(Registry& reg) : m_reg(&reg) {}

    template <class F> void each(F&& func) {
        using First = std::tuple_element_t<0, std::tuple<Cs...>>;
        Pool<First>* lead = m_reg->TryPool<First>();
        if (!lead) return;
        for (std::size_t k = lead->dense.size(); k-- > 0; ) {
            const Entity e = lead->dense[k];
            if ((m_reg->Has<Cs>(e) && ...))
                func(e, m_reg->Get<Cs>(e)...);
        }
    }

private:
    Registry* m_reg;
};

} // namespace ecs
} // namespace engine