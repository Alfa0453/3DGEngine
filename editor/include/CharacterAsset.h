#pragma once

#include "EditorScene.h"

#include <string>
#include <vector>

// One animation file merged onto the character's model by bone name (the separate
// FBX / Mixamo workflow). clipName becomes the clip's name; strip root motion for
// in-place walk/run so the character doesn't slide.
struct CharacterAnimationSource {
    std::string file;
    std::string clipName;
    bool        stripRootMotion = false;
};

struct CharacterAsset {
    int version = 7;
    std::string name = "Character";
    std::string modelAssetPath;
    std::string materialAssetPath;
    std::vector<CharacterAnimationSource> animationSources;  // extra clips from separate files

    // Render-only model offset transform (applied to the mesh, NOT the collider): the
    // mesh is placed at position, rotated by these Euler degrees, and scaled, all about
    // the model's centre. Carried onto the scene object when the character is added so
    // every instance spawns oriented the same way. Defaults = untouched mesh.
    glm::vec3 modelOffsetPosition{0.0f};
    glm::vec3 modelOrientationEuler{0.0f};
    glm::vec3 modelOffsetScale{1.0f};

    bool colliderEnabled = true;
    engine::ecs::Collider collider = [] {
        engine::ecs::Collider value;
        value.shape = engine::ecs::ColliderShape::Capsule;
        value.radius = 0.4f;
        value.halfHeight = 0.5f;
        value.layer = engine::ecs::CollisionLayer::Player;
        value.mask = engine::ecs::CollisionLayer::CharacterBlockers;
        return value;
    }();
    bool playerControllerEnabled = true;
    EditorScene::PlayerControllerSettings playerController;

    bool skeletalModel = true;
    int animationClipIndex = 0;
    std::string animationClipName;
    bool animationAutoplay = true;
    bool animationLoop = true;
    float animationSpeed = 1.0f;
    bool locomotionEnabled = true;
    int idleClipIndex = 0;
    int walkClipIndex = 0;
    int runClipIndex = 0;
    std::string idleClipName;
    std::string walkClipName;
    std::string runClipName;
    float walkAt = 0.15f;
    float runAt = 3.0f;
    std::vector<EditorScene::AnimationStateNode> animationStates;
    std::vector<EditorScene::AnimationParameter> animationParameters;
    std::vector<EditorScene::AnimationStateTransition> animationTransitions;
    std::vector<EditorScene::AnimationActionProfile> animationActionProfiles;
    std::vector<EditorScene::AnimationEvent> animationEvents;

    bool healthEnabled = true;
    engine::Health health;
    bool navAgentEnabled = false;
    float navSpeed = 3.0f;
    float navMaxForce = 20.0f;
    float navReachRadius = 0.6f;
    float navRepathInterval = 0.3f;
    std::string navTargetName;
    float navVisionRange = 12.0f;
    float navVisionHalfAngle = 45.0f;
    std::string behaviorTreeAsset;
    int navTeam = 0;
    bool navAutoTarget = false;
    engine::ai::AiMovementMode navMovementMode = engine::ai::AiMovementMode::Grounded;
    float navGravity = -9.81f;
    float navMaxFallSpeed = 35.0f;
    float navGroundProbe = 0.25f;
    float navStepHeight = 0.35f;
    float navMaxSlope = 50.0f;

    bool scriptEnabled = false;
    std::string scriptClassName;
    std::string scriptPath;

    void Capture(const EditorScene::Object& object);
    bool Apply(EditorScene& scene) const;
    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
};
