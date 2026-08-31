#pragma once

// A minimal open-addressing hash map keyed by std::uint64_t, built for the physics hot path
// (Pass-5 Phase 18). The whole point is that clear() does NOT free memory: std::unordered_map
// frees every node on clear(), so a per-step "clear then refill with N entries" pattern allocates
// N nodes every step. This map keeps its slab across clear() (just bumps a generation stamp), so a
// warmed-up map performs ZERO heap allocations per step. Keys are 64-bit (physics pair keys /
// entity ids); values are small POD. Not a general-purpose container -- no rehash-shrinking, no
// thread safety -- just the exact behaviour the solver's warm-start / event / age maps need.

#include <cstdint>
#include <vector>

namespace engine {

template <class T>
class FlatU64Map {
public:
    struct Slot { std::uint64_t key; T value; std::uint32_t gen; };

    FlatU64Map() { rehash(64); }

    void clear() {
        // O(1) logical clear: bump the generation so every existing slot reads as empty. The slab
        // and its capacity survive, so subsequent inserts reuse the same memory (no allocation).
        if (++m_gen == 0) {                        // generation wrapped -> hard reset the stamps
            for (Slot& s : m_slots) s.gen = 0;
            m_gen = 1;
        }
        m_size = 0;
    }

    // Insert-or-assign. Returns a reference to the stored value.
    T& operator[](std::uint64_t key) {
        if (m_size * 10 >= m_cap * 7) rehash(m_cap * 2);   // keep load factor < 0.7
        std::size_t i = index(key);
        while (occupied(i)) {
            if (m_slots[i].key == key) return m_slots[i].value;
            i = (i + 1) & m_mask;
        }
        m_slots[i].key = key; m_slots[i].value = T{}; m_slots[i].gen = m_gen;
        ++m_size;
        return m_slots[i].value;
    }

    // Pointer to the value if present, else nullptr (the find(...) != end() idiom).
    const T* find(std::uint64_t key) const {
        std::size_t i = index(key);
        while (occupied(i)) {
            if (m_slots[i].key == key) return &m_slots[i].value;
            i = (i + 1) & m_mask;
        }
        return nullptr;
    }
    T* find(std::uint64_t key) {
        std::size_t i = index(key);
        while (occupied(i)) {
            if (m_slots[i].key == key) return &m_slots[i].value;
            i = (i + 1) & m_mask;
        }
        return nullptr;
    }

    // Remove a key if present. Uses Knuth 6.4R backward-shift deletion so linear-probe chains stay
    // contiguous (no tombstones to accumulate or sweep). No memory is freed -- the slab is reused.
    void erase(std::uint64_t key) {
        std::size_t i = index(key);
        while (occupied(i)) {
            if (m_slots[i].key == key) { eraseAt(i); return; }
            i = (i + 1) & m_mask;
        }
    }

    void swap(FlatU64Map& o) noexcept {
        m_slots.swap(o.m_slots);
        std::swap(m_cap, o.m_cap); std::swap(m_mask, o.m_mask);
        std::swap(m_size, o.m_size); std::swap(m_gen, o.m_gen);
    }

    bool contains(std::uint64_t key) const { return find(key) != nullptr; }
    std::size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }
    void reserve(std::size_t n) { std::size_t need = 1; while (need * 7 < n * 10) need <<= 1; if (need > m_cap) rehash(need); }

    // Visit every live (key,value). Order is slab order -- callers that need determinism sort the
    // results themselves (the physics event path does). Fn is invoked as fn(uint64_t, T&).
    template <class Fn> void for_each(Fn&& fn) {
        for (Slot& s : m_slots) if (s.gen == m_gen) fn(s.key, s.value);
    }
    template <class Fn> void for_each(Fn&& fn) const {
        for (const Slot& s : m_slots) if (s.gen == m_gen) fn(s.key, s.value);
    }

private:
    std::size_t index(std::uint64_t k) const {
        k ^= k >> 33; k *= 0xff51afd7ed558ccdull; k ^= k >> 33;   // fmix64
        return static_cast<std::size_t>(k) & m_mask;
    }
    bool occupied(std::size_t i) const { return m_slots[i].gen == m_gen; }

    // Backward-shift deletion (Knuth 6.4R) for a linear-probe table: after emptying slot i, pull
    // any following element back into i when its ideal (home) slot lies outside the open gap, so
    // the cluster stays free of holes and find() never stops early.
    void eraseAt(std::size_t i) {
        std::size_t j = i;
        while (true) {
            m_slots[i].gen = 0;                     // 0 is always "empty" (m_gen >= 1)
            std::size_t k;
            do {
                j = (j + 1) & m_mask;
                if (!occupied(j)) { --m_size; return; }
                k = index(m_slots[j].key);          // home slot of the element at j
            } while ((i <= j) ? (k > i && k <= j) : (k > i || k <= j));   // k inside (i, j] -> keep
            m_slots[i] = m_slots[j];                // move j back into the gap at i
            i = j;
        }
    }

    void rehash(std::size_t newCap) {
        std::vector<Slot> old = std::move(m_slots);
        const std::uint32_t oldGen = m_gen;
        m_slots.assign(newCap, Slot{0, T{}, 0});
        m_cap = newCap; m_mask = newCap - 1; m_gen = 1; m_size = 0;
        for (Slot& s : old)
            if (s.gen == oldGen) (*this)[s.key] = s.value;
    }

    std::vector<Slot> m_slots;
    std::size_t   m_cap  = 0;
    std::size_t   m_mask = 0;
    std::size_t   m_size = 0;
    std::uint32_t m_gen  = 1;   // slots with gen != m_gen are logically empty
};

} // namespace engine
