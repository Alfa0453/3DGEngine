#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace engine {
namespace ai {

// A named, typed key-value store shared by every node and script in one agent's
// behaviour tree. Tasks/decorators/services read and write it to coordinate (e.g. a
// task writes "hasAmmo", a decorator gates on it). Values are grouped by type; a
// getter returns the supplied default when the key is missing. Keys are plain
// identifiers (no spaces). Held on AgentContext as c.blackboard.
class Blackboard {
public:
    // --- setters ---
    void SetBool  (const std::string& k, bool v)             { m_bools[k]    = v; }
    void SetInt   (const std::string& k, int v)              { m_ints[k]     = v; }
    void SetFloat (const std::string& k, float v)            { m_floats[k]   = v; }
    void SetVec3  (const std::string& k, const glm::vec3& v) { m_vecs[k]     = v; }
    void SetString(const std::string& k, const std::string& v){ m_strings[k] = v; }
    void SetEntity(const std::string& k, ecs::Entity v)      { m_entities[k] = v; }

    // --- getters (return 'def' when the key is absent) ---
    bool        GetBool  (const std::string& k, bool def = false) const            { return Find(m_bools, k, def); }
    int         GetInt   (const std::string& k, int def = 0) const                 { return Find(m_ints, k, def); }
    float       GetFloat (const std::string& k, float def = 0.0f) const            { return Find(m_floats, k, def); }
    glm::vec3   GetVec3  (const std::string& k, const glm::vec3& def = glm::vec3(0.0f)) const { return Find(m_vecs, k, def); }
    std::string GetString(const std::string& k, const std::string& def = {}) const { return Find(m_strings, k, def); }
    ecs::Entity GetEntity(const std::string& k, ecs::Entity def = ecs::kNull) const { return Find(m_entities, k, def); }

    bool HasBool  (const std::string& k) const { return m_bools.count(k)   != 0; }
    bool HasInt   (const std::string& k) const { return m_ints.count(k)    != 0; }
    bool HasFloat (const std::string& k) const { return m_floats.count(k)  != 0; }
    bool HasVec3  (const std::string& k) const { return m_vecs.count(k)    != 0; }
    bool HasString(const std::string& k) const { return m_strings.count(k) != 0; }
    bool HasEntity(const std::string& k) const { return m_entities.count(k)!= 0; }

    void Clear() {
        m_bools.clear(); m_ints.clear(); m_floats.clear();
        m_vecs.clear(); m_strings.clear(); m_entities.clear();
    }

    // Every key with its current value stringified -- for a debug watch panel.
    std::vector<std::pair<std::string, std::string>> Snapshot() const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& kv : m_bools)   out.emplace_back(kv.first, kv.second ? "true" : "false");
        for (const auto& kv : m_ints)    out.emplace_back(kv.first, std::to_string(kv.second));
        for (const auto& kv : m_floats)  out.emplace_back(kv.first, std::to_string(kv.second));
        for (const auto& kv : m_vecs) {
            out.emplace_back(kv.first, std::to_string(kv.second.x) + ", " +
                                       std::to_string(kv.second.y) + ", " +
                                       std::to_string(kv.second.z));
        }
        for (const auto& kv : m_strings) out.emplace_back(kv.first, kv.second);
        for (const auto& kv : m_entities) out.emplace_back(kv.first, std::to_string(kv.second));
        return out;
    }

private:
    template <class Map, class T>
    static T Find(const Map& m, const std::string& k, const T& def) {
        const auto it = m.find(k);
        return it != m.end() ? it->second : def;
    }

    std::unordered_map<std::string, bool>        m_bools;
    std::unordered_map<std::string, int>         m_ints;
    std::unordered_map<std::string, float>       m_floats;
    std::unordered_map<std::string, glm::vec3>   m_vecs;
    std::unordered_map<std::string, std::string> m_strings;
    std::unordered_map<std::string, ecs::Entity> m_entities;
};

} // namespace ai
} // namespace engine
