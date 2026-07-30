#pragma once

#include "EditorScene.h"   // AnimationStateNode / AnimationParameter / AnimationStateTransition
#include <engine/assets/AssetIdentity.h>

#include <string>
#include <vector>

// One clip used by a graph, added from a saved .3dgclip asset. The resolved source is
// denormalised so baking a character never has to open the .3dgclip again. States and
// transitions reference the clip by `clipName` (its name inside the source file).
struct AnimationGraphClip {
    std::string clipAsset;                 // source .3dgclip (for re-loading in the editor)
    engine::AssetHandle clipAssetId;
    std::string sourceFile;                // resolved FBX / glTF containing the animation
    engine::AssetHandle sourceAssetId;
    std::string sourceClipName;            // take name inside the source file
    std::string clipName;                  // unique graph-facing alias (states reference this)
    bool        stripRootMotion = false;
};

// A reusable animation state machine (.3dggraph): its own clips + states + parameters +
// transitions + blend spaces. The Character Editor references one of these instead of
// authoring animation itself; the graph is baked onto a character on placement.
struct AnimationGraphAsset {
    int         version = 5;
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

    bool Save(const std::string& path, std::string* error = nullptr);
    bool Load(const std::string& path, std::string* error = nullptr);
};
