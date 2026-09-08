#pragma once

#include <engine/ai/NavGrid.h>
#include <engine/ai/NavMesh.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

class NavigationQueryPanel {
public:
    struct Obstacle {
        std::string name;
        glm::vec3 center{0.0f};
        glm::vec3 halfExtents{0.5f};
        bool dynamic = false;
    };

    struct Snapshot {
        bool playMode = false;
        const engine::ai::NavMesh* mesh = nullptr;
        const engine::ai::NavGrid* grid = nullptr;
        std::vector<Obstacle> obstacles;
        glm::vec3 cameraPosition{0.0f};
        bool hasSelectedPosition = false;
        glm::vec3 selectedPosition{0.0f};
    };

    struct Result {
        bool rebuildRequested = false;
        bool showNavigationSurface = false;
    };

    Result Draw(const Snapshot& snapshot, bool* open);
    const std::vector<glm::vec3>& Path() const { return m_path; }
    const std::vector<glm::vec3>& ObstacleHits() const { return m_obstacleHits; }
    const glm::vec3& Start() const { return m_start; }
    const glm::vec3& Goal() const { return m_goal; }
    bool HasQuery() const { return m_hasQuery; }
    bool QuerySucceeded() const { return m_success; }
    float AgentRadius() const { return m_agentRadius; }
    float CellSize() const { return m_cellSize; }

private:
    void RunQuery(const Snapshot& snapshot);
    void AnalyzeMesh(const engine::ai::NavMesh& mesh);

    glm::vec3 m_start{-2.0f, 0.0f, 0.0f};
    glm::vec3 m_goal{2.0f, 0.0f, 0.0f};
    float m_agentRadius = 0.4f;
    float m_agentHeight = 1.8f;
    float m_cellSize = 0.5f;
    float m_costPerMeter = 1.0f;
    int m_source = 0;
    bool m_allowDiagonal = true;
    bool m_showNavigationSurface = true;
    bool m_hasQuery = false;
    bool m_success = false;
    bool m_startOnMesh = false;
    bool m_goalOnMesh = false;
    int m_startPoly = -1;
    int m_goalPoly = -1;
    int m_regions = 0;
    int m_portals = 0;
    float m_pathLength = 0.0f;
    float m_pathCost = 0.0f;
    bool m_testLinkEnabled = false;
    bool m_testLinkBidirectional = true;
    bool m_usedTestLink = false;
    glm::vec3 m_testLinkStart{-1.0f, 0.0f, 0.0f};
    glm::vec3 m_testLinkEnd{1.0f, 0.0f, 0.0f};
    float m_testLinkCost = 1.0f;
    std::string m_status = "Set start and goal, then run a query.";
    std::vector<glm::vec3> m_path;
    std::vector<glm::vec3> m_obstacleHits;
};
