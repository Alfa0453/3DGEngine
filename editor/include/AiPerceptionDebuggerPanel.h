#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

class AiPerceptionDebuggerPanel {
public:
    struct AgentRow {
        std::uint32_t entity = 0;
        std::string name;
        int team = 0;
        std::string state;
        std::uint32_t targetEntity = 0;
        std::string targetName;
        int targetTeam = 0;
        glm::vec3 position{0.0f};
        glm::vec3 facing{0.0f, 0.0f, -1.0f};
        glm::vec3 targetPosition{0.0f};
        glm::vec3 heardPosition{0.0f};
        glm::vec3 lastKnownPosition{0.0f};
        float visionRange = 0.0f;
        float visionHalfAngleDeg = 0.0f;
        float hearingRange = 0.0f;
        float targetDistance = 0.0f;
        float targetAngleDeg = 0.0f;
        float heardLoudness = 0.0f;
        bool targetValid = false;
        bool inRange = false;
        bool inFov = false;
        bool lineOfSight = false;
        bool seesTarget = false;
        bool heardNoise = false;
        bool heardFromSquad = false;
        bool hasLastKnown = false;
        bool behaviorGraph = false;
    };

    struct Snapshot {
        bool playMode = false;
        std::size_t activeSounds = 0;
        std::vector<AgentRow> agents;
    };

    void Draw(const Snapshot& snapshot, bool* open);

    bool ShowSceneGuides() const { return m_showSceneGuides; }
    bool ShowVision() const { return m_showVision; }
    bool ShowHearing() const { return m_showHearing; }
    bool ShowLastKnown() const { return m_showLastKnown; }
    bool SelectedOnly() const { return m_selectedOnly; }
    std::uint32_t SelectedEntity() const { return m_selectedEntity; }

private:
    struct Previous {
        std::uint32_t target = 0;
        bool sees = false;
        bool heard = false;
        bool initialized = false;
    };
    struct HistoryEvent {
        double time = 0.0;
        std::uint32_t entity = 0;
        std::string agent;
        std::string message;
    };

    void Observe(const Snapshot& snapshot);
    bool Visible(const AgentRow& row) const;

    std::unordered_map<std::uint32_t, Previous> m_previous;
    std::vector<HistoryEvent> m_history;
    std::uint32_t m_selectedEntity = std::numeric_limits<std::uint32_t>::max();
    char m_filter[96]{};
    int m_teamFilter = -1;
    bool m_onlySensing = false;
    bool m_freeze = false;
    bool m_showSceneGuides = true;
    bool m_showVision = true;
    bool m_showHearing = true;
    bool m_showLastKnown = true;
    bool m_selectedOnly = false;
};
