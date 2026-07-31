#pragma once

#include <engine/assets/AssetIdentity.h>

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

    int         version = 4;
    engine::AssetHandle assetId;
    std::string name = "Clip";
    std::string sourceFile;        // native engine-imported .3dgskmesh / .3dganim with the clip
    engine::AssetHandle sourceAssetId;
    std::string clipName;          // which clip in the source (empty = first)
    bool        stripRootMotion = false;  // freeze the root so it plays in place
    bool        loop = true;
    float       speed = 1.0f;
    bool        action = false;          // standalone one-shot action, not a graph state
    std::string maskRootBone;            // empty = full body and movement-locking
    float       fadeIn = 0.08f;
    float       fadeOut = 0.15f;
    std::vector<Event> events;     // action/gameplay notifies authored on this clip

    bool Save(const std::string& path, std::string* error = nullptr);
    bool Load(const std::string& path, std::string* error = nullptr);
};
