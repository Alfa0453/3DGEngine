#pragma once

#include "engine/animation/Skeleton.h"

#include <glm/glm.hpp>
#include <vector>

namespace engine {

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
};

} // namespace engine