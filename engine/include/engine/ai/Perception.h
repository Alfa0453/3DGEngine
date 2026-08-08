#pragma once

#include "engine/ecs/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace engine {
class PhysicsWorld;
namespace ecs { class Registry; }

namespace ai {

// A vision cone: how far and how wide an agent can see.
struct VisionCone {
    float range            = 20.0f;
    float halfAngleDegrees = 45.0f;     // half of the total field of view
};

// Geometry only: is 'target' within the cone's range and angle from an eye at
// 'eye' facing 'forward'? (No occlusion -- pure maths, no physics needed.)
inline bool InvisionCone(const glm::vec3& eye, const glm::vec3& forward,
                         const VisionCone& cone, const glm::vec3& target) {
    const glm::vec3 d = target - eye;
    const float dist2 = glm::dot(d, d);
    const float range = std::max(cone.range, 0.0f);
    if (dist2 > range * range) return false;
    if (dist2 < 1e-10f) return true;
    const float forwardLen2 = glm::dot(forward, forward);
    if (forwardLen2 < 1e-10f) return false;
    const float dist = std::sqrt(dist2);
    const float cosToTarget = glm::dot(forward, d)
        / (std::sqrt(forwardLen2) * dist);
    return cosToTarget >= std::cos(glm::radians(cone.halfAngleDegrees));
}

// Full check: in the cone AND with clear line of sight. Casts a ray from eye to
// target; if a collider other than the target is hit first, the view is blocked.
bool CanSee(const glm::vec3& eye, const glm::vec3& forward, const VisionCone& cone,
            const glm::vec3& target, ecs::Entity targetEntity,
            PhysicsWorld& world, ecs::Registry& registry,
            ecs::Entity observerEntity = ecs::kNull);

// ----------------------------------- hearing --------------------------------
//
// Sound is modelled as point stimuli that carry out to a radius and attenuate
// linearly to the source. Unlike vision, hearing ignores occlusion (noise travels
// around corners), so these are pure-maths helpers -- no physics needed.

// A single noise: a footstep, a gunshot, a thrown object landing.
struct SoundStimulus {
    glm::vec3 position{0.0f};
    float     radius   = 0.0f;   // how far the noise carries (0 = silent)
    float     loudness = 1.0f;   // relative intensity at the source
};

// Can an ear at 'ear' with acuity 'hearingRange' (<=0 => unlimited) hear 's'?
// Writes the perceived loudness (source loudness attenuated by distance) if given.
inline bool CanHear(const glm::vec3& ear, float hearingRange, const SoundStimulus& s,
                    float* perceivedLoudness = nullptr) {
    const float reach = (hearingRange > 0.0f) ? std::min(hearingRange, s.radius) : s.radius;
    const glm::vec3 delta = s.position - ear;
    if (s.radius <= 0.0f || reach <= 0.0f
        || glm::dot(delta, delta) > reach * reach) {
        if (perceivedLoudness) *perceivedLoudness = 0.0f;
        return false;
    }
    if (perceivedLoudness) {
        const float dist = std::sqrt(glm::dot(delta, delta));
        const float falloff = 1.0f - (dist / s.radius);   // linear to the source radius
        *perceivedLoudness = s.loudness * (falloff > 0.0f ? falloff : 0.0f);
    }
    return true;
}

// A short-lived pool of noises. The host emits stimuli (on footsteps, weapons,
// impacts), calls Update() once per frame to age them out, and each agent queries
// LoudestAudible() to find the strongest noise it can currently hear -- typically
// fed into its "investigate" behaviour as a point of interest.
class SoundField {
public:
    // Emit a noise that fades out over 'ttlSeconds' (its loudness scales with the
    // remaining lifetime, so it dies away smoothly). A tiny default keeps a one-shot
    // "ping" alive for roughly a frame or two.
    void Emit(const glm::vec3& position, float radius, float loudness = 1.0f,
              float ttlSeconds = 0.2f) {
        Entry e;
        e.stimulus = SoundStimulus{position, radius, loudness};
        e.ttl = e.timeLeft = (ttlSeconds > 0.0f) ? ttlSeconds : 0.2f;
        m_entries.push_back(e);
    }

    // Age every stimulus and drop the ones that have expired. Call once per frame.
    void Update(float dt) {
        if (m_entries.empty()) return;
        for (Entry& e : m_entries) e.timeLeft -= dt;
        m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(),
                        [](const Entry& e) { return e.timeLeft <= 0.0f; }),
                        m_entries.end());
    }

    // Find the loudest stimulus this ear can hear right now. Returns false if none
    // are audible; otherwise writes the noise position (and perceived loudness).
    bool LoudestAudible(const glm::vec3& ear, float hearingRange,
                        glm::vec3* outPosition, float* outLoudness = nullptr) const {
        if (m_entries.empty()) {
            if (outLoudness) *outLoudness = 0.0f;
            return false;
        }
        bool found = false;
        float best = -1.0f;
        glm::vec3 bestPos{0.0f};
        for (const Entry& e : m_entries) {
            const SoundStimulus& s = e.stimulus;
            const float reach = (hearingRange > 0.0f)
                ? std::min(hearingRange, s.radius) : s.radius;
            if (s.radius <= 0.0f || reach <= 0.0f) continue;
            const glm::vec3 delta = s.position - ear;
            const float distSq = glm::dot(delta, delta);
            if (distSq > reach * reach) continue;
            const float dist = std::sqrt(distSq);
            const float falloff = 1.0f - dist / s.radius;
            const float life = e.ttl > 0.0f
                ? (e.timeLeft > 0.0f ? e.timeLeft / e.ttl : 0.0f) : 0.0f;
            const float perceived = s.loudness * life
                * (falloff > 0.0f ? falloff : 0.0f);
            if (perceived > best) {
                best = perceived; bestPos = s.position; found = true;
            }
        }
        if (found) {
            if (outPosition) *outPosition = bestPos;
            if (outLoudness) *outLoudness = best;
        }
        return found;
    }

    void        Clear()        { m_entries.clear(); }
    std::size_t Count()  const { return m_entries.size(); }

private:
    struct Entry {
        SoundStimulus stimulus;
        float         ttl      = 0.0f;
        float         timeLeft = 0.0f;
        // Loudness fades with the fraction of lifetime remaining.
        SoundStimulus Current() const {
            SoundStimulus s = stimulus;
            if (ttl > 0.0f) s.loudness *= (timeLeft > 0.0f ? timeLeft / ttl : 0.0f);
            return s;
        }
    };
    std::vector<Entry> m_entries;
};

} // namespace ai
} // namespace engine
