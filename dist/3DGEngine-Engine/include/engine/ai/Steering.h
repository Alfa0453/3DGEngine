#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace engine {
namespace ai {

// A steered agent: position + velocity on the world plane, with speed and
// steering-force limits. Behaviours return a steering acceleration; Integrate
// applies it. (Works in 3D; ground agents usually keep y flat.)
struct Agent {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float     maxSpeed = 5.0f;
    float     maxForce = 20.0f;     // max steering acceleration
    float     wanderAngle = 0.0f;   // internal state for Wander
};

struct Obstacle { glm::vec3 center{0.0f}; float radius = 1.0f; };

// --- behaviours: each returns a steering acceleration (clamped to maxForce) ---
glm::vec3 Seek  (const Agent& a, const glm::vec3& target);
glm::vec3 Flee  (const Agent& a, const glm::vec3& target);
glm::vec3 Arrive(const Agent& a, const glm::vec3& target, float slowRadius, float stopRadius = 0.05f);
glm::vec3 Pursue(const Agent& a, const glm::vec3& targetPos, const glm::vec3& targetVel);
glm::vec3 Evade (const Agent& a, const glm::vec3& targetPos, const glm::vec3& targetVel);
glm::vec3 Wander(Agent& a, float jitter, float circleDist = 2.0f, float circleRadius = 1.0f);
glm::vec3 AvoidObstacles(const Agent& a, const std::vector<Obstacle>& obstacles,
                         float lookAhead, float agentRadius = 0.5f);
// Follow a waypoint list; advances 'index' as waypoints are reached, Arrives at the end.
glm::vec3 FollowPath(const Agent& a, const std::vector<glm::vec3>& path, std::size_t& index,
                     float waypointRadius = 0.6f, float slowRadius = 1.5f);
        
// Advance the agent by a steering acceleration.
void Integrate(Agent& a, const glm::vec3& steering, float dt);

} // namespace ai
} // namespace engine