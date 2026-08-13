#pragma once

#include "RuntimePropertySnapshot.h"

#include <engine/ecs/Entity.h>

#include <array>
#include <deque>
#include <string>
#include <unordered_map>

class RuntimePropertyInspectorPanel {
public:
    void BeginPlay(const engine::ecs::Registry& registry,
                   const std::unordered_map<engine::ecs::Entity, std::string>& names);
    void EndPlay();
    void Draw(engine::ecs::Registry* registry,
              const std::unordered_map<engine::ecs::Entity, std::string>& names,
              bool& paused, bool& stepRequested, bool* open);

private:
    struct EditEntry { std::string entity, property; };
    void EnsureSnapshot(const engine::ecs::Registry& registry, engine::ecs::Entity entity);
    void Record(const std::string& entity, const std::string& property);

    std::unordered_map<engine::ecs::Entity, RuntimeEntitySnapshot> m_start;
    std::deque<EditEntry> m_history;
    engine::ecs::Entity m_selected = engine::ecs::kNull;
    std::array<char, 128> m_entityFilter{};
    std::array<char, 128> m_propertyFilter{};
};
