#include "engine/ai/Steering.h"

#include <cmath>

namespace engine {
namespace ai {
namespace {
constexpr float kVelocityResponseTime = 0.15f;

glm::vec3 ClampLen(const glm::vec3& v, float maxLen) {
    const float l2 = glm::dot(v, v);
    if (l2 > maxLen * maxLen && l2 > 1e-12f) return v * (maxLen / std::sqrt(l2));
    return v;
}

glm::vec3 MatchVelocity(const Agent& a, const glm::vec3& desired) {
    // Convert the velocity error into acceleration. Previously the raw velocity
    // difference was integrated as acceleration, which made Max Force mostly
    // ineffective and allowed agents to continue along an obsolete heading for
    // close to a second after their target changed direction.
    return ClampLen((desired - a.velocity) / kVelocityResponseTime, a.maxForce);
}
} // namespace

glm::vec3 Seek(const Agent& a, const glm::vec3& target) {
    const glm::vec3 d = target - a.position;
    const float len = glm::length(d);
    if (len < 1e-6f) return glm::vec3(0.0f);
    const glm::vec3 desired = (d / len) * a.maxSpeed;
    return MatchVelocity(a, desired);
}

glm::vec3 Flee(const Agent& a, const glm::vec3& target) {
    const glm::vec3 d = a.position - target;
    const float len = glm::length(d);
    if (len < 1e-6f) return glm::vec3(0.0f);
    const glm::vec3 desired = (d / len) * a.maxSpeed;
    return MatchVelocity(a, desired);
}

glm::vec3 Arrive(const Agent& a, const glm::vec3& target, float slowRadius, float stopRadius) {
    const glm::vec3 d = target - a.position;
    const float dist = glm::length(d);
    if (dist < stopRadius) return MatchVelocity(a, glm::vec3(0.0f));   // brake to a stop
    const float speed = (dist < slowRadius) ? a.maxSpeed * (dist / slowRadius) : a.maxSpeed;
    const glm::vec3 desired = (d / dist) * speed;
    return MatchVelocity(a, desired);
}

glm::vec3 Pursue(const Agent& a, const glm::vec3& targetPos, const glm::vec3& targetVel) {
    const float dist = glm::length(targetPos - a.position);
    const float t = (a.maxSpeed > 0.0f) ? dist / a.maxSpeed : 0.0f;   // lead time
    return Seek(a, targetPos + targetVel * t);
}

glm::vec3 Evade(const Agent& a, const glm::vec3& targetPos, const glm::vec3& targetVel) {
    const float dist = glm::length(targetPos - a.position);
    const float t = (a.maxSpeed > 0.0f) ? dist / a.maxSpeed : 0.0f;
    return Flee(a, targetPos + targetVel * t);
}

glm::vec3 Wander(Agent& a, float jitter, float circleDist, float circleRadius) {
    a.wanderAngle += jitter;
    const glm::vec3 fwd = (glm::length(a.velocity) > 1e-4f) ? glm::normalize(a.velocity)
                                                            : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 center = a.position + fwd * circleDist;
    const glm::vec3 offset(std::cos(a.wanderAngle) * circleRadius, 0.0f, std::sin(a.wanderAngle) * circleRadius);
    return Seek(a, center + offset);
}

glm::vec3 AvoidObstacles(const Agent& a, const std::vector<Obstacle>& obstacles,
                         float lookAhead, float agentRadius) {
    const float speed = glm::length(a.velocity);
    if (speed < 1e-4f) return glm::vec3(0.0f);
    const glm::vec3 dir = a.velocity / speed;

    const Obstacle* threat = nullptr;
    float nearestAlong = lookAhead;
    for (const Obstacle& o : obstacles) {
        const glm::vec3 toO = o.center - a.position;
        const float along = glm::dot(toO, dir);
        if (along < 0.0f || along > lookAhead) continue;             // behind or beyond reach
        const glm::vec3 onRay = a.position + dir * along;            // closest ray point
        const float lateral = glm::length(o.center - onRay);
        if (lateral < o.radius + agentRadius && along < nearestAlong) { nearestAlong = along; threat = &o; }
    }
    if (!threat) return glm::vec3(0.0f);

    glm::vec3 away = (a.position + dir * nearestAlong) - threat->center;   // push out sideways
    if (glm::length(away) < 1e-4f) away = glm::vec3(-dir.z, 0.0f, dir.x);  // dead-ahead: pick a side
    return ClampLen(glm::normalize(away) * a.maxForce, a.maxForce);
}

glm::vec3 FollowPath(const Agent& a, const std::vector<glm::vec3>& path, std::size_t& index,
                     float waypointRadius, float slowRadius) {
    if (path.empty()) return glm::vec3(0.0f);
    if (index >= path.size()) index = path.size() - 1;
    if (index + 1 < path.size()) {
        if (glm::length(path[index] - a.position) < waypointRadius) ++index;
        return Seek(a, path[index]);
    }
    return Arrive(a, path.back(), slowRadius);   // final waypoint
}

void Integrate(Agent& a, const glm::vec3& steering, float dt) {
    a.velocity += ClampLen(steering, a.maxForce) * dt;
    a.velocity = ClampLen(a.velocity, a.maxSpeed);
    a.position += a.velocity * dt;
}

} // namespace ai
} // namespace engine
