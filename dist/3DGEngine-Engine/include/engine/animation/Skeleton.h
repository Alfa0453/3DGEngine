#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace engine {

// One joint in a skeleton. Bones are stored in topological order (a parent always
// precedes its children) so a single forward pass can compose world transforms.
struct Bone {
    std::string name;
    int         parent = -1;            // index of parent bone, -1 for the root
    glm::mat4   offset{1.0f};           // mesh space -> bone space (inverse bind)
    glm::mat4   localBind{1.0f};        // default local transform when unanimated
};

// The rig: bones plus the inverse of the model's root transform (so poses come out
// back in mesh space).
struct Skeleton {
    std::vector<Bone> bones;
    glm::mat4         globalInverse{1.0f};

    int Find(const std::string& n) const {
        for (std::size_t i = 0; i < bones.size(); ++i)
            if (bones[i].name == n) return static_cast<int>(i);
        return -1;
    }
    std::size_t Count() const { return bones.size(); }
};

// Keyframes.
struct VecKey  { float time; glm::vec3 value; };
struct QuatKey { float time; glm::quat value; };

// Animation channel for one bone (parallel-indexed to Skeleton::bones). Any of the
// three tracks may be empty; if all three are, the bone uses its localBind.
struct BoneChannel {
    std::vector<VecKey>  positions;
    std::vector<QuatKey> rotations;
    std::vector<VecKey>  scales;
    bool Empty() const { return positions.empty() && rotations.empty() && scales.empty(); }
};

// A single clip. duration is in ticks; play time (seconds) is scaled by
// ticksPerSecond and wrapped to loop.
struct Animation {
    std::string              name;
    float                    duration       = 0.0f;   // ticks
    float                    ticksPerSecond = 25.0f; 
    std::vector<BoneChannel> channels;                // indexed by bone
};

} // namespace engine