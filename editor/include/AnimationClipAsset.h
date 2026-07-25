#pragma once

#include <string>
#include <vector>

// A reusable animation-clip asset (.3dgclip). It names one clip inside a source
// model/animation file and the settings used to play it, so clips can be authored once
// in the Clip Editor and dropped into characters without re-specifying everything.
struct AnimationClipAsset {
    struct Event {
        float       time = 0.0f;   // seconds from the beginning of the clip
        std::string name;
    };

    int         version = 3;
    std::string name = "Clip";
    std::string sourceFile;        // FBX / glTF containing the animation
    std::string clipName;          // which clip in the file (empty = first)
    bool        stripRootMotion = false;  // freeze the root so it plays in place
    bool        loop = true;
    float       speed = 1.0f;
    bool        action = false;          // standalone one-shot action, not a graph state
    std::string maskRootBone;            // empty = full body and movement-locking
    float       fadeIn = 0.08f;
    float       fadeOut = 0.15f;
    std::vector<Event> events;     // action/gameplay notifies authored on this clip

    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
};
