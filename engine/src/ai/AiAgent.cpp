#include "engine/ai/AiAgent.h"

#include "engine/ai/AStar.h"
#include "engine/ai/NavMesh.h"

#include <cmath>

namespace engine {
namespace ai {

void AiAgent::Step(float dt, const glm::vec3& targetPos, bool seesTarget, const Planner& plan) {
    m_sawTarget = seesTarget;

    // --- transitions ---
    switch (m_state) {
        case State::Patrol:
            if (seesTarget) {
                m_state = State::Chase; m_lastKnown = targetPos;
                m_path = plan(agent.position, m_lastKnown);
                m_pathIndex = 0; m_repathTimer = 0.0f;
            }
            break;
        case State::Chase:
            if (seesTarget) { m_lastKnown = targetPos; }
            else {
                m_state = State::Search;
                m_path = plan(agent.position, m_lastKnown);
                m_pathIndex = 0;
            }
            break;
        case State::Search:
            if (seesTarget) {
                m_state = State::Chase; m_lastKnown = targetPos;
                m_path = plan(agent.position, m_lastKnown);
                m_pathIndex = 0; m_repathTimer = 0.0f;
            } else if (glm::length(m_lastKnown - agent.position) < reachRadius) {
                m_state = State::Patrol;
            }
            break;
    }

    // --- action ---
    glm::vec3 steer(0.0f);
    switch (m_state) {
        case State::Patrol:
            if (!patrol.empty()) {
                steer = FollowPath(agent, patrol, m_pathIndex, reachRadius, 1.5f);
                if (m_patrolIndex + 1 >= patrol.size() &&
                    glm::length(patrol.back() - agent.position) < reachRadius) m_patrolIndex = 0;  // loop
            }
            break;
        case State::Chase:
            m_repathTimer += dt;
            if (m_repathTimer > repathInterval) {
                m_path = plan(agent.position, m_lastKnown);
                m_pathIndex = 0; m_repathTimer = 0.0f;
            }
            steer = (seesTarget && glm::length(targetPos - agent.position) < chargeRadius)
                    ? Seek(agent, targetPos)
                    : FollowPath(agent, m_path, m_pathIndex, reachRadius);
            break;
        case State::Search:
            steer = FollowPath(agent, m_path, m_pathIndex, reachRadius, 1.0f);
            break;
    }

    Integrate(agent, steer, dt);
    if (glm::length(agent.velocity) > 1e-3f) m_facing = glm::normalize(agent.velocity);
}

void AiAgent::Update(float dt, const glm::vec3& targetPos, bool seesTarget, const NavGrid& grid) {
    Step(dt, targetPos, seesTarget, [&grid](const glm::vec3& start, const glm::vec3& goal) {
        return AStar::FindPathWorld(grid, start, goal);
    });
}

void AiAgent::Update(float dt, const glm::vec3& targetPos, bool seesTarget, const NavMesh& mesh) {
    Step(dt, targetPos, seesTarget, [&mesh](const glm::vec3& start, const glm::vec3& goal) {
        std::vector<glm::vec3> path;
        mesh.FindPath(start, goal, path);   // empty on failure -> agent holds position
        return path;
    });
}

} // namespace ai
} // namespace engine
