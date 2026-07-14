#pragma once

#include "engine/animation/Skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace engine {

// One bone's LOCAL transform (before the hierarchy is composed). Sampling a clip
// yields one of these per bone; layers/blends operate on them, then Compose() folds
// the hierarchy into skinning matrices.
struct BoneLocal {
    glm::vec3 pos{0.0f};
    glm::quat rot{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// Turns a skeleton + a clip into the array of skinning matrices the vertex shader
// multiplies each vertex by. Stateless: give it a time, get a pose.
class Animator {
public:
    // Sample 'anim' on 'skel' at timeSeconds (looping) into outMatrices (one per
    // bone): outMatrices[i] = globalInverse * worldTransform[i] * offset[i].
    static void ComputePose(const Skeleton& skel, const Animation& anim,
                            float timeSeconds, std::vector<glm::mat4>& outMatrices);

    // The rest pose (no animation): each bone uses its localBind.
    static void ComputeBindPose(const Skeleton& skel, std::vector<glm::mat4>& outMatrices);
    
    // Cross-fade blend of two clips into one pose. blend in [0,1]: 0 = clip A,
    // 1 = clip B. Each bone's local translate/rotate/scale is sampled from both
    // clips and interpolated (lerp position/scale, slerp rotation) before the
    // hierarchy is composed -- used to transition smoothly between animations.
    static void ComputeBlendedPose(const Skeleton& skel,
                                   const Animation& a, float timeA,
                                   const Animation& b, float timeB,
                                   float blend, std::vector<glm::mat4>& outMatrices);
    
    // --- Layered / partial-body pipeline --------------------------------
    // Sample a clip into per-bone LOCAL transforms (looping via the clip's ticks).
    static void SampleLocal(const Skeleton& skel, const Animation& anim, 
                            float timeSeconds, std::vector<BoneLocal>& out);
    static glm::vec3 SampleRootTranslation(const Skeleton& skel, const Animation& anim,
                                           float timeSeconds);

    // Uniform blend of two local poses (per bone: lerp pos/scale, slerp rot).
    static void BlendLocal(const std::vector<BoneLocal>& a, const std::vector<BoneLocal>& b,
                           float blend, std::vector<BoneLocal>& out);

    
    // Layer `layer` over `base` IN PLACE, per bone weighted by weight*mask[i] (mask
    // shorter than the skeleton, or empty, counts as 1 -> full body). An override
    // layer: masked bones move toward the layer pose, the rest keep the base.
    static void LayerLocal(std::vector<BoneLocal>& base, const std::vector<BoneLocal>& layer,
                           const std::vector<float>& mask, float weight);

    // Fold a local pose through the bone hierarchy into skinning matrices.
    static void Compose(const Skeleton& skel, const std::vector<BoneLocal>& local,
                        std::vector<glm::mat4>& out);

    // Convenience: base clip with a masked action layered on top, composed.
    static void ComputeLayeredPose(const Skeleton& skel, const Animation& base, float baseTime,
                                  const Animation& layer, float layerTime, const std::vector<float>& mask,
                                  float weight, std::vector<glm::mat4>& out);
    
    // A per-bone mask that is `inside` for `rootBone` and all its descendants and
    // `outside` elsewhere -- e.g. BuildMask(skel, "spine_01") for an upper body.
    static std::vector<float> BuildMask(const Skeleton& skel, const std::string& rootBone,
                                        float inside = 1.0f, float outside = 0.0f);                   
};

} // namespace engine
