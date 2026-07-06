#include "engine/animation/Animator.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>

namespace engine {
namespace {

// Find the segment [i, i+1] containing 'time' and return the interpolation factor.
template <class Key>
std::size_t FindSegment(const std::vector<Key>& keys, float time, float& outT) {
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        if (time < keys[i + 1].time) {
            const float span = keys[i + 1].time - keys[i].time;
            outT = (span > 1e-8f) ? (time - keys[i].time) / span : 0.0f;
            if (outT < 0.0f) outT = 0.0f;
            if (outT > 1.0f) outT = 1.0f;
            return i;
        }
    }
    outT = 0.0f;
    return keys.empty() ? 0 : keys.size() - 1;
}

glm::vec3 SampleVecAt(const std::vector<VecKey>& keys, float t, const glm::vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;
    float f;
    const std::size_t i = FindSegment(keys, t, f);
    if (i + 1 >= keys.size()) return keys.back().value;
    return glm::mix(keys[i].value, keys[i + 1].value, f);
}

glm::quat SampleQuatAt(const std::vector<QuatKey>& keys, float t) {
    if (keys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (keys.size() == 1) return glm::normalize(keys[0].value);
    float f;
    const std::size_t i = FindSegment(keys, t, f);
    if (i + 1 >= keys.size()) return glm::normalize(keys.back().value);
    return glm::normalize(glm::slerp(keys[i].value, keys[i + 1].value, f));
}

glm::mat4 LocalAt(const BoneChannel& ch, float t, const glm::mat4& bind) {
    if (ch.Empty()) return bind;
    const glm::vec3 pos   = SampleVecAt(ch.positions, t, glm::vec3(0.0f));
    const glm::quat rot   = SampleQuatAt(ch.rotations, t);
    const glm::vec3 scale = SampleVecAt(ch.scales, t, glm::vec3(1.0f));
    return glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(rot)
         * glm::scale(glm::mat4(1.0f), scale);
}

float WrapTicks(const Animation& anim, float timeSeconds) {
    if (anim.duration <= 0.0f) return 0.0f;
    const float tps = (anim.ticksPerSecond > 0.0f) ? anim.ticksPerSecond : 25.0f;
    float t = std::fmod(timeSeconds * tps, anim.duration);
    if (t < 0.0f) t += anim.duration;
    return t;
}

struct BonePose { glm::vec3 pos; glm::quat rot; glm::vec3 scale; };

BonePose SampleBoneTRS(const Animation& anim, std::size_t bone, float ticks, const Bone& b) {
    if (bone < anim.channels.size() && !anim.channels[bone].Empty()) {
        const BoneChannel& ch = anim.channels[bone];
        return { SampleVecAt(ch.positions, ticks, glm::vec3(0.0f)),
                 SampleQuatAt(ch.rotations, ticks),
                 SampleVecAt(ch.scales, ticks, glm::vec3(1.0f)) };
    }
    // No channel: fall back to the bind pose (translation + rotation from localBind).
    return { glm::vec3(b.localBind[3]),
             glm::normalize(glm::quat_cast(glm::mat3(b.localBind))),
             glm::vec3(1.0f) };
}

} // namespace

void Animator::ComputePose(const Skeleton& skel, const Animation& anim, float timeSeconds, std::vector<glm::mat4>& out) {
    const std::size_t n = skel.bones.size();
    out.assign(n, glm::mat4(1.0f));
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));

    float ticks = 0.0f;
    if (anim.duration > 0.0f) {
        const float tps = (anim.ticksPerSecond > 0.0f) ? anim.ticksPerSecond : 25.0f;
        ticks = std::fmod(timeSeconds * tps, anim.duration);
        if (ticks < 0.0f) ticks += anim.duration;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const Bone& b = skel.bones[i];
        glm::mat4 local = b.localBind;
        if (i < anim.channels.size()) local = LocalAt(anim.channels[i], ticks, b.localBind);
        world[i] = (b.parent >= 0) ? world[b.parent] * local : local;
        out[i]   = skel.globalInverse * world[i] * b.offset;
    }
}

void Animator::ComputeBindPose(const Skeleton& skel, std::vector<glm::mat4>& out) {
    const std::size_t n = skel.bones.size();
    out.assign(n, glm::mat4(1.0f));
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));
    for (std::size_t i = 0; i < n; ++i) {
        const Bone& b = skel.bones[i];
        world[i] = (b.parent >= 0) ? world[b.parent] * b.localBind : b.localBind;
        out[i]   = skel.globalInverse * world[i] * b.offset;
    }
}

void Animator::ComputeBlendedPose(const Skeleton& skel,
                                  const Animation& a, float timeA,
                                  const Animation& b, float timeB,
                                  float blend, std::vector<glm::mat4>& out) {
    const std::size_t n = skel.bones.size();
    out.assign(n, glm::mat4(1.0f));
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));
    const float tA = WrapTicks(a, timeA), tB = WrapTicks(b, timeB);
    const float w  = glm::clamp(blend, 0.0f, 1.0f);

    for (std::size_t i = 0; i < n; ++i) {
        const Bone& bone = skel.bones[i];
        const BonePose pa = SampleBoneTRS(a, i, tA, bone);
        const BonePose pb = SampleBoneTRS(b, i, tB, bone);
        const glm::vec3 pos = glm::mix(pa.pos, pb.pos, w);
        const glm::quat rot = glm::normalize(glm::slerp(pa.rot, pb.rot, w));
        const glm::vec3 scl = glm::mix(pa.scale, pb.scale, w);
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), pos)
                              * glm::mat4_cast(rot)
                              * glm::scale(glm::mat4(1.0f), scl);
        world[i] = (bone.parent >= 0) ? world[bone.parent] * local : local;
        out[i]   = skel.globalInverse * world[i] * bone.offset;
    }
}

} // namespace engine