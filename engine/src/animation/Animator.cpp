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

void Animator::SampleLocal(const Skeleton& skel, const Animation& anim, float timeSeconds, std::vector<BoneLocal>& out) {
    const std::size_t n = skel.bones.size();
    out.assign(n, BoneLocal{});
    const float ticks = WrapTicks(anim, timeSeconds);
    for (std::size_t i = 0; i < n; ++i) {
        const BonePose p = SampleBoneTRS(anim, i, ticks, skel.bones[i]);
        out[i].pos = p.pos; out[i].rot = p.rot; out[i].scale = p.scale;
    }
}

glm::vec3 Animator::SampleRootTranslation(const Skeleton& skel, const Animation& anim, float timeSeconds) {
    if (skel.bones.empty()) return glm::vec3(0.0f);
    return SampleBoneTRS(anim, 0, WrapTicks(anim, timeSeconds), skel.bones[0]).pos;
}

glm::quat Animator::SampleRootRotation(const Skeleton& skel, const Animation& anim, float timeSeconds) {
    if (skel.bones.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return SampleBoneTRS(anim, 0, WrapTicks(anim, timeSeconds), skel.bones[0]).rot;
}

void Animator::BlendLocal(const std::vector<BoneLocal>& a, const std::vector<BoneLocal>& b,
                          float blend, std::vector<BoneLocal>& out) {
    const std::size_t n = std::min(a.size(), b.size());
    const float w = glm::clamp(blend, 0.0f, 1.0f);
    out.assign(n, BoneLocal{});
    for (std::size_t i = 0; i < n; ++i) {
        out[i].pos   = glm::mix(a[i].pos, b[i].pos, w);
        out[i].rot   = glm::normalize(glm::slerp(a[i].rot, b[i].rot, w));
        out[i].scale = glm::mix(a[i].scale, b[i].scale, w);
    }
}

void Animator::LayerLocal(std::vector<BoneLocal>& base, const std::vector<BoneLocal>& layer,
                          const std::vector<float>& mask, float weight) {
    const std::size_t n = std::min(base.size(), layer.size());
    const float gw = glm::clamp(weight, 0.0f, 1.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const float m = (i < mask.size()) ? mask[i] : 1.0f;     // empty/short mask -> full body
        const float w = gw * m;
        if (w <= 0.0f) continue;
        base[i].pos   = glm::mix(base[i].pos, layer[i].pos, w);
        base[i].rot   = glm::normalize(glm::slerp(base[i].rot, layer[i].rot, w));
        base[i].scale = glm::mix(base[i].scale, layer[i].scale, w);
    }
}

void Animator::Compose(const Skeleton& skel, const std::vector<BoneLocal>& local,
                       std::vector<glm::mat4>& out) {
    const std::size_t n = skel.bones.size();
    out.assign(n, glm::mat4(1.0f));
    std::vector<glm::mat4> world(n, glm::mat4(1.0f));
    for (std::size_t i = 0; i < n; ++i) {
        const BoneLocal& bl = (i < local.size()) ? local[i] : BoneLocal{};
        const glm::mat4 lm = glm::translate(glm::mat4(1.0f), bl.pos)
                           * glm::mat4_cast(bl.rot)
                           * glm::scale(glm::mat4(1.0f), bl.scale);
        const Bone& b = skel.bones[i];
        world[i] = (b.parent >= 0) ? world[static_cast<std::size_t>(b.parent)] * lm : lm;
        out[i]   = skel.globalInverse * world[i] * b.offset;
    }
}

void Animator::ComputeLayeredPose(const Skeleton& skel,
                                  const Animation& base, float baseTime,
                                  const Animation& layer, float layerTime,
                                  const std::vector<float>& mask, float weight,
                                  std::vector<glm::mat4>& out) {
    std::vector<BoneLocal> b, l;
    SampleLocal(skel, base, baseTime, b);
    SampleLocal(skel, layer, layerTime, l);
    LayerLocal(b, l, mask, weight);
    Compose(skel, b, out);
}
 
std::vector<float> Animator::BuildMask(const Skeleton& skel, const std::string& rootBone,
                                       float inside, float outside) {
    const std::size_t n = skel.bones.size();
    std::vector<float> mask(n, outside);
    const int root = skel.Find(rootBone);
    if (root < 0) return mask;              // not found -> all outside
    // Bones are topologically ordered (parent before child), so one forward pass
    // marks the root and everything descended from it.
    std::vector<char> in(n, 0);
    in[static_cast<std::size_t>(root)] = 1;
    for (std::size_t i = 0; i < n; ++i) {
        const int par = skel.bones[i].parent;
        if (par >= 0 && in[static_cast<std::size_t>(par)]) in[i] = 1;
    }
    for (std::size_t i = 0; i < n; ++i) mask[i] = in[i] ? inside : outside;
    return mask;
}

namespace {
// Shortest-arc rotation taking unit vector a onto unit vector b (no experimental GLM).
glm::quat RotationBetween(glm::vec3 a, glm::vec3 b) {
    a = glm::normalize(a);
    b = glm::normalize(b);
    const float d = glm::dot(a, b);
    if (d >= 1.0f - 1.0e-6f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (d <= -1.0f + 1.0e-6f) {                       // opposite: rotate 180 about any perp axis
        glm::vec3 axis = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), a);
        if (glm::dot(axis, axis) < 1.0e-6f) axis = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), a);
        return glm::angleAxis(3.14159265358979323846f, glm::normalize(axis));
    }
    const glm::vec3 c = glm::cross(a, b);
    glm::quat q(1.0f + d, c.x, c.y, c.z);
    return glm::normalize(q);
}
} // namespace

Animator::TwoBoneIKResult Animator::SolveTwoBoneIK(const glm::vec3& hip, const glm::vec3& knee,
                                                   const glm::vec3& foot, const glm::vec3& target,
                                                   const glm::vec3& poleHint) {
    TwoBoneIKResult result;
    const float upperLen = glm::length(knee - hip);
    const float lowerLen = glm::length(foot - knee);
    const glm::vec3 curEnd = foot - hip;
    const float curEndLen = glm::length(curEnd);
    if (upperLen < 1.0e-5f || lowerLen < 1.0e-5f || curEndLen < 1.0e-5f) return result;

    const glm::vec3 toTarget = target - hip;
    const float rawDist = glm::length(toTarget);
    if (rawDist < 1.0e-5f) return result;
    const glm::vec3 targetDir = toTarget / rawDist;

    // Clamp the reach so the limb never stretches or hyperextends.
    const float minReach = std::abs(upperLen - lowerLen) + 1.0e-3f;
    const float maxReach = upperLen + lowerLen - 1.0e-3f;
    const float dist = glm::clamp(rawDist, minReach, maxReach);

    // Knee bend plane: prefer the pole hint; fall back to the current limb plane.
    glm::vec3 axis = glm::cross(targetDir, glm::normalize(poleHint - hip));
    if (glm::dot(axis, axis) < 1.0e-6f)
        axis = glm::cross(glm::normalize(curEnd), glm::normalize(knee - hip));
    if (glm::dot(axis, axis) < 1.0e-6f) return result;   // fully degenerate
    axis = glm::normalize(axis);

    // 1) Straighten the limb onto the target direction.
    const glm::quat align = RotationBetween(curEnd, targetDir);
    // 2) Bend the upper bone off the target line by the law-of-cosines hip angle.
    const float hipCos = glm::clamp(
        (upperLen * upperLen + dist * dist - lowerLen * lowerLen) / (2.0f * upperLen * dist),
        -1.0f, 1.0f);
    const float hipAngle = std::acos(hipCos);
    result.upper = glm::normalize(glm::angleAxis(hipAngle, axis) * align);

    // 3) Point the lower bone from the solved knee at the (clamped) target.
    const glm::vec3 kneeSolved = hip + result.upper * (knee - hip);
    const glm::vec3 lowerNow = result.upper * (foot - knee);
    const glm::vec3 lowerWant = (hip + targetDir * dist) - kneeSolved;
    result.lower = RotationBetween(lowerNow, lowerWant);
    return result;
}

} // namespace engine
