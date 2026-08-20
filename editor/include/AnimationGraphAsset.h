#pragma once

#include "EditorScene.h"   // AnimationStateNode / AnimationParameter / AnimationStateTransition
#include <engine/assets/AssetIdentity.h>

#include <string>
#include <vector>
#include <glm/vec2.hpp>

// One clip used by a graph, added from a saved .3dgclip asset. The resolved source is
// denormalised so baking a character never has to open the .3dgclip again. States and
// transitions reference the clip by `clipName` (its name inside the source file).
struct AnimationGraphClip {
    std::string clipAsset;                 // source .3dgclip (for re-loading in the editor)
    engine::AssetHandle clipAssetId;
    std::string sourceFile;                // resolved native .3dgskmesh / .3dganim with the animation
    engine::AssetHandle sourceAssetId;
    std::string sourceClipName;            // take name inside the source
    std::string clipName;                  // unique graph-facing alias (states reference this)
    bool        stripRootMotion = false;
};

// A reusable animation state machine (.3dggraph): its own clips + states + parameters +
// transitions + blend spaces. The Character Editor references one of these instead of
// authoring animation itself; the graph is baked onto a character on placement.
struct AnimationGraphAsset {
    struct NodeLayout {
        engine::AssetHandle stateId;
        glm::vec2 position{0.0f};
        bool collapsed = false;
    };

    int         version = 7;
    engine::AssetHandle assetId;
    std::string name = "Graph";
    std::string previewModel;   // rig to preview the graph on (editor only, not baked)
    engine::AssetHandle previewModelAssetId;

    std::vector<AnimationGraphClip>                    clips;
    std::vector<EditorScene::AnimationStateNode>       states;
    std::vector<EditorScene::AnimationParameter>       parameters;
    std::vector<EditorScene::AnimationStateTransition> transitions;
    std::vector<EditorScene::AnimationActionProfile>   actions;
    std::vector<EditorScene::AnimationEvent>           events;

    // Editor-only state-machine metadata. The runtime builder only consumes the
    // state/parameter/transition arrays above.
    engine::AssetHandle entryStateId;
    glm::vec2 entryNodePosition{-260.0f, 20.0f};
    glm::vec2 anyStateNodePosition{-260.0f, 170.0f};
    std::vector<NodeLayout> nodeLayouts;

    bool Save(const std::string& path, std::string* error = nullptr);
    bool Load(const std::string& path, std::string* error = nullptr);
    void NormalizeGraphMetadata(bool generateLayout = true,
                                bool migrateLegacyMotionSources = false);
};
