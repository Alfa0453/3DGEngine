#include "CharacterAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

void CharacterAsset::Capture(const EditorScene::Object& o) {
    name = o.name; modelAssetPath = o.modelAssetPath; materialAssetPath = o.materialAssetPath;
    modelOffsetPosition = o.modelOffsetPosition; modelOrientationEuler = o.modelOrientationEuler;
    modelOffsetScale = o.modelOffsetScale;
    colliderEnabled = o.colliderEnabled; collider = o.collider;
    playerControllerEnabled = o.playerControllerEnabled; playerController = o.playerController;
    skeletalModel = o.skeletalModel; animationClipIndex = o.animationClipIndex;
    animationClipName = o.animationClipName; animationAutoplay = o.animationAutoplay;
    animationLoop = o.animationLoop; animationSpeed = o.animationSpeed;
    locomotionEnabled = o.animationLocomotionEnabled;
    idleClipIndex = o.animationIdleClipIndex; walkClipIndex = o.animationWalkClipIndex;
    runClipIndex = o.animationRunClipIndex; idleClipName = o.animationIdleClipName;
    walkClipName = o.animationWalkClipName; runClipName = o.animationRunClipName;
    walkAt = o.animationWalkAt; runAt = o.animationRunAt;
    animationStates = o.animationStates;
    animationParameters = o.animationParameters;
    animationTransitions = o.animationTransitions;
    animationActionProfiles = o.animationActionProfiles;
    animationEvents = o.animationEvents;
    healthEnabled = o.healthEnabled; health = o.health;
    navAgentEnabled = o.navAgentEnabled; navSpeed = o.navAgentSpeed;
    navMaxForce = o.navAgentMaxForce; navReachRadius = o.navAgentReachRadius;
    navRepathInterval = o.navAgentRepathInterval; navTargetName = o.navAgentTargetName;
    navVisionRange = o.navAgentVisionRange; navVisionHalfAngle = o.navAgentVisionHalfAngle;
    behaviorTreeAsset = o.navAgentBrainAsset; navTeam = o.navAgentTeam; navAutoTarget = o.navAgentAutoTarget;
    navMovementMode = o.navMovementMode; navGravity = o.navMovementGravity;
    navMaxFallSpeed = o.navMovementMaxFallSpeed; navGroundProbe = o.navMovementGroundProbe;
    navStepHeight = o.navMovementStepHeight; navMaxSlope = o.navMovementMaxSlope;
    scriptEnabled = o.scriptEnabled; scriptClassName = o.scriptClassName; scriptPath = o.scriptPath;
}

bool CharacterAsset::Apply(EditorScene& scene) const {
    if (!scene.SelectedObject() || scene.SelectedLocked()) return false;
    scene.SetSelectedName(name);
    scene.SetSelectedModelAsset(modelAssetPath);
    scene.SetSelectedMaterialAsset(materialAssetPath);
    scene.SetSelectedModelOffset(modelOffsetPosition, modelOrientationEuler, modelOffsetScale);
    scene.SetSelectedColliderEnabled(colliderEnabled);
    if (colliderEnabled) scene.SetSelectedCollider(collider);
    scene.SetSelectedPlayerControllerEnabled(playerControllerEnabled);
    if (playerControllerEnabled) scene.SetSelectedPlayerController(playerController);
    scene.SetSelectedAnimationSettings(skeletalModel, animationClipIndex, animationClipName,
        animationAutoplay, animationLoop, animationSpeed);
    scene.SetSelectedAnimationLocomotion(locomotionEnabled, idleClipIndex, idleClipName,
        walkClipIndex, walkClipName, runClipIndex, runClipName, walkAt, runAt);
    scene.SetSelectedAnimationStateGraph(animationStates, animationTransitions, animationParameters);
    scene.SetSelectedAnimationActionProfiles(animationActionProfiles);
    scene.SetSelectedAnimationEvents(animationEvents);
    scene.SetSelectedHealthEnabled(healthEnabled);
    if (healthEnabled) scene.SetSelectedHealth(health);
    scene.SetSelectedNavAgent(navAgentEnabled, navSpeed, navMaxForce, navReachRadius,
        navRepathInterval, navTargetName, navVisionRange, navVisionHalfAngle);
    if (navAgentEnabled) {
        scene.SetSelectedNavAgentBrain(behaviorTreeAsset);
        scene.SetSelectedNavAgentTeam(navTeam, navAutoTarget);
        scene.SetSelectedNavAgentMovement(navMovementMode, navGravity, navMaxFallSpeed,
            navGroundProbe, navStepHeight, navMaxSlope);
    }
    scene.SetSelectedScript(scriptClassName, scriptPath, scriptEnabled);
    return true;
}

bool CharacterAsset::Save(const std::string& path, std::string* error) const {
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not write character asset: " + path; return false; }
    out << "3DG_CHARACTER " << version << '\n'
        << std::quoted(name) << '\n' << std::quoted(modelAssetPath) << '\n' << std::quoted(materialAssetPath) << '\n'
        << colliderEnabled << ' ' << static_cast<int>(collider.shape) << ' '
        << collider.halfExtents.x << ' ' << collider.halfExtents.y << ' ' << collider.halfExtents.z << ' '
        << collider.radius << ' ' << collider.halfHeight << '\n'
        << playerControllerEnabled << ' ' << playerController.firstPerson << ' '
        << playerController.walkSpeed << ' ' << playerController.runSpeed << ' ' << playerController.jumpSpeed << ' '
        << playerController.lookSensitivity << ' ' << playerController.capsuleRadius << ' ' << playerController.capsuleHeight << ' '
        << playerController.eyeHeight << ' ' << playerController.cameraDistance << ' ' << playerController.cameraTargetHeight << ' '
        << playerController.maxSlopeDegrees << ' ' << playerController.stepHeight << '\n'
        << skeletalModel << ' ' << animationClipIndex << ' ' << std::quoted(animationClipName) << ' '
        << animationAutoplay << ' ' << animationLoop << ' ' << animationSpeed << '\n'
        << locomotionEnabled << ' ' << idleClipIndex << ' ' << std::quoted(idleClipName) << ' '
        << walkClipIndex << ' ' << std::quoted(walkClipName) << ' ' << runClipIndex << ' ' << std::quoted(runClipName) << ' '
        << walkAt << ' ' << runAt << '\n'
        << healthEnabled << ' ' << health.hp << ' ' << health.maxHp << ' ' << health.alive << '\n'
        << navAgentEnabled << ' ' << navSpeed << ' ' << navMaxForce << ' ' << navReachRadius << ' ' << navRepathInterval << ' '
        << std::quoted(navTargetName) << ' ' << navVisionRange << ' ' << navVisionHalfAngle << ' '
        << std::quoted(behaviorTreeAsset) << ' ' << navTeam << ' ' << navAutoTarget << '\n'
        << static_cast<int>(navMovementMode) << ' ' << navGravity << ' ' << navMaxFallSpeed << ' '
        << navGroundProbe << ' ' << navStepHeight << ' ' << navMaxSlope << '\n'
        << scriptEnabled << ' ' << std::quoted(scriptClassName) << ' ' << std::quoted(scriptPath) << '\n'
        << "GRAPH " << animationStates.size() << ' ' << animationParameters.size() << ' '
        << animationTransitions.size() << ' ' << animationActionProfiles.size() << ' '
        << animationEvents.size() << '\n';
    for (const auto& state : animationStates) {
        out << std::quoted(state.name) << ' ' << state.clipIndex << ' ' << std::quoted(state.clipName) << ' '
            << state.loop << ' ' << state.speed << ' ' << state.blendClipIndex << ' '
            << std::quoted(state.blendClipName) << ' ' << std::quoted(state.blendParameter) << ' '
            << state.blendMin << ' ' << state.blendMax << ' ' << state.rootMotion << '\n';
    }
    for (const auto& parameter : animationParameters) {
        out << std::quoted(parameter.name) << ' ' << static_cast<int>(parameter.type) << ' '
            << parameter.defaultValue << '\n';
    }
    for (const auto& transition : animationTransitions) {
        out << std::quoted(transition.fromState) << ' ' << std::quoted(transition.toState) << ' '
            << std::quoted(transition.parameter) << ' ' << static_cast<int>(transition.compare) << ' '
            << transition.threshold << ' ' << transition.fade << ' ' << transition.exitTime << ' '
            << transition.priority << ' ' << transition.canInterrupt << '\n';
    }
    for (const auto& action : animationActionProfiles) {
        out << std::quoted(action.name) << ' ' << action.clipIndex << ' ' << std::quoted(action.clipName) << ' '
            << std::quoted(action.maskRootBone) << ' ' << action.fadeIn << ' ' << action.fadeOut << ' '
            << action.speed << '\n';
    }
    for (const auto& event : animationEvents) {
        out << event.clipIndex << ' ' << event.time << ' ' << std::quoted(event.name) << '\n';
    }
    out << "COLLISION " << collider.planeNormal.x << ' ' << collider.planeNormal.y << ' '
        << collider.planeNormal.z << ' ' << collider.planeOffset << ' ' << collider.majorRadius << ' '
        << collider.minorRadius << ' ' << collider.steps << ' ' << collider.restitution << ' '
        << collider.friction << ' ' << collider.isTrigger << ' ' << collider.layer << ' '
        << collider.mask << '\n'
        << "BLEND_SPACES " << animationStates.size() << '\n';
    for (const auto& state : animationStates) {
        out << state.blendSpace2D << ' ' << std::quoted(state.blendParameterY) << ' '
            << state.synchronizeBlendSpace << ' ' << state.blendSamples.size();
        for (const auto& sample : state.blendSamples) {
            out << ' ' << sample.clipIndex << ' ' << std::quoted(sample.clipName) << ' '
                << sample.value << ' ' << sample.valueY;
        }
        out << '\n';
    }
    out << "ANIM_SOURCES " << animationSources.size();
    for (const auto& source : animationSources) {
        out << ' ' << std::quoted(source.file.empty() ? std::string("-") : source.file)
            << ' ' << std::quoted(source.clipName.empty() ? std::string("-") : source.clipName)
            << ' ' << (source.stripRootMotion ? 1 : 0);
    }
    out << '\n';
    out << "MODEL_OFFSET "
        << modelOffsetPosition.x << ' ' << modelOffsetPosition.y << ' ' << modelOffsetPosition.z << ' '
        << modelOrientationEuler.x << ' ' << modelOrientationEuler.y << ' ' << modelOrientationEuler.z << ' '
        << modelOffsetScale.x << ' ' << modelOffsetScale.y << ' ' << modelOffsetScale.z << '\n';
    return static_cast<bool>(out);
}

bool CharacterAsset::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    std::string magic; int loadedVersion = 0;
    if (!(in >> magic >> loadedVersion) || magic != "3DG_CHARACTER" || loadedVersion < 1) {
        if (error) *error = "Invalid character asset: " + path; return false;
    }
    int shape = 0, movementMode = 0;
    in >> std::quoted(name) >> std::quoted(modelAssetPath) >> std::quoted(materialAssetPath)
       >> colliderEnabled >> shape >> collider.halfExtents.x >> collider.halfExtents.y >> collider.halfExtents.z >> collider.radius >> collider.halfHeight
       >> playerControllerEnabled >> playerController.firstPerson >> playerController.walkSpeed >> playerController.runSpeed >> playerController.jumpSpeed
       >> playerController.lookSensitivity >> playerController.capsuleRadius >> playerController.capsuleHeight >> playerController.eyeHeight
       >> playerController.cameraDistance >> playerController.cameraTargetHeight >> playerController.maxSlopeDegrees >> playerController.stepHeight
       >> skeletalModel >> animationClipIndex >> std::quoted(animationClipName) >> animationAutoplay >> animationLoop >> animationSpeed
       >> locomotionEnabled >> idleClipIndex >> std::quoted(idleClipName) >> walkClipIndex >> std::quoted(walkClipName)
       >> runClipIndex >> std::quoted(runClipName) >> walkAt >> runAt
       >> healthEnabled >> health.hp >> health.maxHp >> health.alive
       >> navAgentEnabled >> navSpeed >> navMaxForce >> navReachRadius >> navRepathInterval >> std::quoted(navTargetName)
       >> navVisionRange >> navVisionHalfAngle >> std::quoted(behaviorTreeAsset) >> navTeam >> navAutoTarget
       >> movementMode >> navGravity >> navMaxFallSpeed >> navGroundProbe >> navStepHeight >> navMaxSlope
       >> scriptEnabled >> std::quoted(scriptClassName) >> std::quoted(scriptPath);
    if (!in) { if (error) *error = "Character asset is incomplete: " + path; return false; }
    collider.shape = static_cast<engine::ecs::ColliderShape>(shape);
    navMovementMode = static_cast<engine::ai::AiMovementMode>(movementMode);
    version = 7; // Upgrade legacy assets when they are next saved.
    // Default the render-only model offset; overwritten below for v7+ files.
    modelOffsetPosition = glm::vec3(0.0f);
    modelOrientationEuler = glm::vec3(0.0f);
    modelOffsetScale = glm::vec3(1.0f);
    animationStates.clear(); animationParameters.clear(); animationTransitions.clear();
    animationActionProfiles.clear(); animationEvents.clear();
    if (loadedVersion >= 2) {
        std::string graphTag;
        std::size_t stateCount=0, parameterCount=0, transitionCount=0, actionCount=0, eventCount=0;
        if (!(in >> graphTag >> stateCount >> parameterCount >> transitionCount >> actionCount >> eventCount)
            || graphTag != "GRAPH") {
            if (error) *error = "Character animation graph is incomplete: " + path; return false;
        }
        for (std::size_t i=0; i<stateCount; ++i) {
            EditorScene::AnimationStateNode state;
            in >> std::quoted(state.name) >> state.clipIndex >> std::quoted(state.clipName)
               >> state.loop >> state.speed >> state.blendClipIndex >> std::quoted(state.blendClipName)
               >> std::quoted(state.blendParameter) >> state.blendMin >> state.blendMax >> state.rootMotion;
            animationStates.push_back(std::move(state));
        }
        for (std::size_t i=0; i<parameterCount; ++i) {
            EditorScene::AnimationParameter parameter; int type=0;
            in >> std::quoted(parameter.name) >> type >> parameter.defaultValue;
            parameter.type = static_cast<EditorScene::AnimationParameter::Type>(type);
            animationParameters.push_back(std::move(parameter));
        }
        for (std::size_t i=0; i<transitionCount; ++i) {
            EditorScene::AnimationStateTransition transition; int compare=0;
            in >> std::quoted(transition.fromState) >> std::quoted(transition.toState)
               >> std::quoted(transition.parameter) >> compare >> transition.threshold >> transition.fade
               >> transition.exitTime >> transition.priority >> transition.canInterrupt;
            transition.compare = static_cast<EditorScene::AnimationStateTransition::Compare>(compare);
            animationTransitions.push_back(std::move(transition));
        }
        for (std::size_t i=0; i<actionCount; ++i) {
            EditorScene::AnimationActionProfile action;
            in >> std::quoted(action.name) >> action.clipIndex >> std::quoted(action.clipName)
               >> std::quoted(action.maskRootBone) >> action.fadeIn >> action.fadeOut >> action.speed;
            animationActionProfiles.push_back(std::move(action));
        }
        for (std::size_t i=0; i<eventCount; ++i) {
            EditorScene::AnimationEvent event;
            in >> event.clipIndex >> event.time >> std::quoted(event.name);
            animationEvents.push_back(std::move(event));
        }
        if (!in) { if (error) *error = "Character animation graph data is invalid: " + path; return false; }
    }
    if (loadedVersion >= 3) {
        std::string collisionTag;
        if (!(in >> collisionTag >> collider.planeNormal.x >> collider.planeNormal.y
            >> collider.planeNormal.z >> collider.planeOffset >> collider.majorRadius
            >> collider.minorRadius >> collider.steps >> collider.restitution >> collider.friction
            >> collider.isTrigger >> collider.layer >> collider.mask)
            || collisionTag != "COLLISION") {
            if (error) *error = "Character collision data is invalid: " + path; return false;
        }
    }
    if (loadedVersion >= 4) {
        std::string blendTag;
        std::size_t stateCount = 0;
        if (!(in >> blendTag >> stateCount) || blendTag != "BLEND_SPACES"
            || stateCount != animationStates.size()) {
            if (error) *error = "Character Blend Space data is invalid: " + path; return false;
        }
        for (auto& state : animationStates) {
            std::size_t sampleCount = 0;
            if (loadedVersion >= 5) {
                in >> state.blendSpace2D >> std::quoted(state.blendParameterY)
                   >> state.synchronizeBlendSpace;
            }
            in >> sampleCount;
            for (std::size_t i = 0; i < sampleCount; ++i) {
                EditorScene::AnimationStateNode::BlendSample sample;
                in >> sample.clipIndex >> std::quoted(sample.clipName) >> sample.value;
                if (loadedVersion >= 5) in >> sample.valueY;
                state.blendSamples.push_back(std::move(sample));
            }
        }
        if (!in) { if (error) *error = "Character Blend Space samples are invalid: " + path; return false; }
    }
    animationSources.clear();
    if (loadedVersion >= 6) {
        std::string tag;
        std::size_t sourceCount = 0;
        if ((in >> tag >> sourceCount) && tag == "ANIM_SOURCES") {
            for (std::size_t i = 0; i < sourceCount; ++i) {
                CharacterAnimationSource source;
                int strip = 0;
                in >> std::quoted(source.file) >> std::quoted(source.clipName) >> strip;
                if (source.file == "-") source.file.clear();
                if (source.clipName == "-") source.clipName.clear();
                source.stripRootMotion = strip != 0;
                animationSources.push_back(std::move(source));
            }
        }
    }
    if (loadedVersion >= 7) {
        std::string offsetTag;
        if ((in >> offsetTag) && offsetTag == "MODEL_OFFSET") {
            in >> modelOffsetPosition.x >> modelOffsetPosition.y >> modelOffsetPosition.z
               >> modelOrientationEuler.x >> modelOrientationEuler.y >> modelOrientationEuler.z
               >> modelOffsetScale.x >> modelOffsetScale.y >> modelOffsetScale.z;
        }
    }
    return true;
}
