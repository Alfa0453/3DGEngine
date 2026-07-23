#pragma once

#include "engine/ai/NavGrid.h"
#include "engine/ai/Steering.h"
#include "engine/ai/Perception.h"

#include <glm/glm.hpp>

#include <functional>
#include <vector>

namespace engine {
namespace ai {

class NavMesh;

// A ready-made AI controller: it packages steering, pathfinding and a simple
// patrol / chase / search brain so a game doesn't have to wire the pieces by
// hand. Perception is injected (pass 'seesTarget' from ai::CanSee or your own
// check) so the controller stays independent of the physics/ECS and is fully
// testable. Call Update once per fixed step, then copy Position() to the entity.
class AiAgent {
public:
    enum class State { Patrol, Chase, Search };

    Agent                  agent;                  // steering body (position/velocity/limits)
    VisionCone             vision;                 // for the caller's perception, if it uses it
    std::vector<glm::vec3> patrol;                 // patrol loop (world points)
    float                  repathInterval = 0.3f;  // seconds between chase re-plans
    float                  reachRadius    = 1.0f;  // "arrived at a waypoint" distance
    float                  chargeRadius   = 4.0f;  // within this + visible -> charge straight in

    void SetPosition(const glm::vec3& p) { agent.position = p; }
    State GetState() const { return m_state; }
    bool SeesTarget() const { return m_sawTarget; }
    const glm::vec3& Position() const { return agent.position; }
    const glm::vec3& Facing() const { return m_facing; }
    const std::vector<glm::vec3>& Path() const { return m_path; }

    // Drive one fixed step. targetPos is the pursued point; seesTarget is whether
    // the agent can see it right now (from ai::CanSee or a custom check).
    void Update(float dt, const glm::vec3& targetPos, bool seesTarget, const NavGrid& grid);

    // Same brain, but chase/search paths are planned on a NavMesh (funnel-smoothed,
    // corner-cutting-free) instead of a grid. Patrol is identical (steering-only).
    void Update(float dt, const glm::vec3& targetPos, bool seesTarget, const NavMesh& mesh);

private:
    // The transition/action brain shared by both overloads. 'plan' returns a
    // world-space waypoint list from start to goal on whatever nav source is used.
    using Planner = std::function<std::vector<glm::vec3>(const glm::vec3&, const glm::vec3&)>;
    void Step(float dt, const glm::vec3& targetPos, bool seesTarget, const Planner& plan);

    State       m_state = State::Patrol;
    std::size_t m_patrolIndex = 0;
    std::vector<glm::vec3> m_path;
    std::size_t m_pathIndex = 0;
    glm::vec3   m_pathGoal{0.0f};
    bool        m_pathGoalValid = false;
    glm::vec3   m_lastKnown{0.0f};
    float       m_repathTimer = 0.0f;
    glm::vec3   m_facing{1.0f, 0.0f, 0.0f};
    bool        m_sawTarget = false;
};

} // namespace ai
} // namespace engine
