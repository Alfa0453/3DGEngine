#include "CharacterAsset.h"

#include "AnimationGraphAsset.h"
#include "AnimationClipAsset.h"

#include <engine/assets/AssetReference.h>
#include <engine/assets/AssetRegistry.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>

void CharacterAsset::Capture(const EditorScene::Object& o) {
    name = o.name; modelAssetPath = o.modelAssetPath;
    modelAssetId = o.modelAssetId;
    materialAssetPath = o.materialAssetPath;
    materialAssetId = o.materialAssetId;
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
    animationSources.clear();
    for (const EditorScene::AnimationSource& source : o.animationSources) {
        CharacterAnimationSource captured;
        captured.file = source.file;
        captured.assetId = source.assetId;
        captured.clipName = source.clipName;
        captured.stripRootMotion = source.stripRootMotion;
        animationSources.push_back(std::move(captured));
    }
    // Rebuild named sockets and their optional visible attachments from the baked
    // scene records. Socket-only records intentionally have an empty model path.
    sockets.clear();
    attachments.clear();
    for (std::size_t i = 0; i < o.modelAttachments.size(); ++i) {
        const EditorScene::ModelAttachment& a = o.modelAttachments[i];
        const std::string socketName = a.socketName.empty()
            ? "Socket" + std::to_string(i + 1) : a.socketName;
        const auto existing = std::find_if(
            sockets.begin(), sockets.end(),
            [&socketName](const CharacterSocket& socket) {
                return socket.name == socketName;
            });
        if (existing == sockets.end()) {
            CharacterSocket s;
            s.name = socketName;
            s.boneName = a.boneName;
            s.position = a.position;
            s.eulerDegrees = a.eulerDegrees;
            s.scale = a.scale;
            sockets.push_back(std::move(s));
        }
        if (!a.modelPath.empty()) {
            CharacterAttachment captured;
            captured.modelPath = a.modelPath;
            captured.modelAssetId = a.modelAssetId;
            captured.materialAssetId = a.materialAssetId;
            captured.socketName = socketName;
            captured.materialPath = a.materialPath;
            attachments.push_back(std::move(captured));
        }
    }
    healthEnabled = o.healthEnabled; health = o.health;
    ragdollEnabled = o.ragdollEnabled; ragdoll = o.ragdoll;
    ragdoll.active = false;
    ragdoll.parts.clear();
    ragdoll.boneDrivers.clear();
    ragdoll.boneFromBody.clear();
    navAgentEnabled = o.navAgentEnabled; navSpeed = o.navAgentSpeed;
    navMaxForce = o.navAgentMaxForce; navReachRadius = o.navAgentReachRadius;
    navRepathInterval = o.navAgentRepathInterval; navTargetName = o.navAgentTargetName;
    navVisionRange = o.navAgentVisionRange; navVisionHalfAngle = o.navAgentVisionHalfAngle;
    navHearingRange = o.navAgentHearingRange;
    behaviorTreeAsset = o.navAgentBrainAsset; navTeam = o.navAgentTeam; navAutoTarget = o.navAgentAutoTarget;
    navMovementMode = o.navMovementMode; navGravity = o.navMovementGravity;
    navMaxFallSpeed = o.navMovementMaxFallSpeed; navGroundProbe = o.navMovementGroundProbe;
    navStepHeight = o.navMovementStepHeight; navMaxSlope = o.navMovementMaxSlope;
    scriptEnabled = o.scriptEnabled; scriptClassName = o.scriptClassName; scriptPath = o.scriptPath;
    scripts.clear();
    if (!o.scriptClassName.empty()) {
        scripts.push_back({o.scriptEnabled, o.scriptClassName, o.scriptPath});
    }
    for (const EditorScene::ScriptBinding& script : o.additionalScripts) {
        scripts.push_back({script.enabled, script.className, script.path});
    }
}

bool CharacterAsset::Apply(EditorScene& scene) const {
    if (!scene.SelectedObject() || scene.SelectedLocked()) return false;
    scene.SetSelectedName(name);
    scene.SetSelectedModelAsset(modelAssetPath, modelAssetId);
    scene.SetSelectedMaterialAsset(materialAssetPath, materialAssetId);
    scene.SetSelectedModelOffset(modelOffsetPosition, modelOrientationEuler, modelOffsetScale);
    scene.SetSelectedColliderEnabled(colliderEnabled);
    if (colliderEnabled) scene.SetSelectedCollider(collider);
    scene.SetSelectedPlayerControllerEnabled(playerControllerEnabled);
    if (playerControllerEnabled) scene.SetSelectedPlayerController(playerController);
    scene.SetSelectedAnimationSettings(skeletalModel, animationClipIndex, animationClipName,
        animationAutoplay, animationLoop, animationSpeed);
    // Animation is supplied entirely by the referenced .3dggraph (its own clips + states +
    // transitions), baked onto the object here. When no graph is set, fall back to the
    // character's legacy inline animation data so older characters still work.
    AnimationGraphAsset graph;
    std::string graphError;
    const bool haveGraph = !animationGraphPath.empty() && graph.Load(animationGraphPath, &graphError);
    if (haveGraph) {
        std::vector<EditorScene::AnimationSource> graphSources;
        graphSources.reserve(graph.clips.size());
        for (const AnimationGraphClip& c : graph.clips) {
            EditorScene::AnimationSource source;
            source.file = c.sourceFile;
            source.assetId = c.sourceAssetId;
            source.clipName = c.clipName;
            source.stripRootMotion = c.stripRootMotion;
            source.sourceClipName = c.sourceClipName;
            graphSources.push_back(std::move(source));
        }
        scene.SetSelectedAnimationSources(graphSources);
        scene.SetSelectedAnimationLocomotion(false, 0, "", 0, "", 0, "", 0.15f, 3.0f);
        scene.SetSelectedAnimationStateGraph(graph.states, graph.transitions, graph.parameters);
    } else {
        scene.SetSelectedAnimationLocomotion(locomotionEnabled, idleClipIndex, idleClipName,
            walkClipIndex, walkClipName, runClipIndex, runClipName, walkAt, runAt);
        scene.SetSelectedAnimationStateGraph(animationStates, animationTransitions, animationParameters);
        std::vector<EditorScene::AnimationSource> sceneSources;
        sceneSources.reserve(animationSources.size());
        for (const CharacterAnimationSource& source : animationSources) {
            EditorScene::AnimationSource baked;
            baked.file = source.file;
            baked.assetId = source.assetId;
            baked.clipName = source.clipName;
            baked.stripRootMotion = source.stripRootMotion;
            sceneSources.push_back(std::move(baked));
        }
        scene.SetSelectedAnimationSources(sceneSources);
    }
    // Bake each named socket (bone + offset) and its optional visible attachment into
    // the scene so editor Play and exported builds resolve the same animated point.
    std::vector<EditorScene::ModelAttachment> sceneAttachments;
    sceneAttachments.reserve(sockets.size() + attachments.size());
    // Preserve every named socket even when it has no visible model attached. These
    // socket-only records become animated gameplay points available to scripts.
    for (const CharacterSocket& s : sockets) {
        EditorScene::ModelAttachment socket;
        socket.socketName = s.name;
        socket.boneName = s.boneName;
        socket.position = s.position;
        socket.eulerDegrees = s.eulerDegrees;
        socket.scale = s.scale;
        sceneAttachments.push_back(std::move(socket));
    }
    for (const CharacterAttachment& a : attachments) {
        EditorScene::ModelAttachment m;
        m.modelPath = a.modelPath;
        m.modelAssetId = a.modelAssetId;
        m.materialAssetId = a.materialAssetId;
        m.materialPath = a.materialPath;
        for (const CharacterSocket& s : sockets) {
            if (s.name == a.socketName) {
                m.socketName = s.name;
                m.boneName = s.boneName;
                m.position = s.position;
                m.eulerDegrees = s.eulerDegrees;
                m.scale = s.scale;
                break;
            }
        }
        sceneAttachments.push_back(std::move(m));
    }
    scene.SetSelectedModelAttachments(sceneAttachments);
    std::vector<EditorScene::AnimationSource> resolvedSources;
    if (const EditorScene::Object* selected = scene.SelectedObject()) {
        resolvedSources = selected->animationSources;
    }
    std::vector<EditorScene::AnimationActionProfile> resolvedActions =
        animationActionProfiles;
    std::vector<EditorScene::AnimationEvent> resolvedEvents = animationEvents;
    for (const std::string& actionPath : actionClipAssets) {
        AnimationClipAsset actionClip;
        if (!actionClip.Load(actionPath, nullptr) || !actionClip.action
            || actionClip.sourceFile.empty()) {
            continue;
        }
        const std::string alias = actionClip.name.empty()
            ? std::filesystem::path(actionPath).stem().string()
            : actionClip.name;
        resolvedSources.erase(
            std::remove_if(resolvedSources.begin(), resolvedSources.end(),
                [&alias](const EditorScene::AnimationSource& source) {
                    return source.clipName == alias;
                }),
            resolvedSources.end());
        resolvedActions.erase(
            std::remove_if(resolvedActions.begin(), resolvedActions.end(),
                [&alias](const EditorScene::AnimationActionProfile& profile) {
                    return profile.name == alias;
                }),
            resolvedActions.end());
        resolvedEvents.erase(
            std::remove_if(resolvedEvents.begin(), resolvedEvents.end(),
                [&alias](const EditorScene::AnimationEvent& event) {
                    return event.clipName == alias;
                }),
            resolvedEvents.end());
        EditorScene::AnimationSource resolved;
        resolved.file = actionClip.sourceFile;
        resolved.assetId = actionClip.sourceAssetId;
        resolved.clipName = alias;
        resolved.stripRootMotion = actionClip.stripRootMotion;
        resolved.sourceClipName = actionClip.clipName;
        resolvedSources.push_back(std::move(resolved));
        resolvedActions.push_back(EditorScene::AnimationActionProfile{
            alias, 0, alias, actionClip.maskRootBone,
            actionClip.fadeIn, actionClip.fadeOut, actionClip.speed});
        for (const AnimationClipAsset::Event& event : actionClip.events) {
            if (event.name.empty()) continue;
            resolvedEvents.push_back(EditorScene::AnimationEvent{
                0, std::max(event.time, 0.0f), event.name, alias});
        }
    }
    scene.SetSelectedAnimationSources(resolvedSources);
    scene.SetSelectedAnimationActionProfiles(resolvedActions);
    scene.SetSelectedAnimationEvents(resolvedEvents);
    scene.SetSelectedHealthEnabled(healthEnabled);
    if (healthEnabled) scene.SetSelectedHealth(health);
    scene.SetSelectedRagdollEnabled(ragdollEnabled);
    if (ragdollEnabled) scene.SetSelectedRagdoll(ragdoll);
    scene.SetSelectedNavAgent(navAgentEnabled, navSpeed, navMaxForce, navReachRadius,
        navRepathInterval, navTargetName, navVisionRange, navVisionHalfAngle, navHearingRange);
    if (navAgentEnabled) {
        scene.SetSelectedNavAgentBrain(behaviorTreeAsset);
        scene.SetSelectedNavAgentTeam(navTeam, navAutoTarget);
        scene.SetSelectedNavAgentMovement(navMovementMode, navGravity, navMaxFallSpeed,
            navGroundProbe, navStepHeight, navMaxSlope);
    }
    const CharacterScript legacy{scriptEnabled, scriptClassName, scriptPath};
    const CharacterScript* primary = !scripts.empty() ? &scripts.front() : &legacy;
    scene.SetSelectedScript(primary->className, primary->path, primary->enabled);
    std::vector<EditorScene::ScriptBinding> additional;
    if (scripts.size() > 1) {
        additional.reserve(scripts.size() - 1);
        for (std::size_t i = 1; i < scripts.size(); ++i) {
            additional.push_back(
                {scripts[i].enabled, scripts[i].className, scripts[i].path, {}});
        }
    }
    scene.SetSelectedAdditionalScripts(additional);
    return true;
}

bool CharacterAsset::Save(const std::string& path, std::string* error) {
    if (!assetId.Valid()) assetId = engine::AssetHandle::Generate();
    version = 20;
    const std::string contentRoot = engine::FindContentRootForAsset(path);
    engine::AssetRegistry registry;
    bool haveRegistry = false;
    if (!contentRoot.empty()) {
        std::string ignored;
        haveRegistry = registry.Load(
            engine::AssetRegistry::DefaultRegistryPath(contentRoot), &ignored);
        const engine::AssetHandle currentModel =
            engine::MakeAssetReference(
                &registry, contentRoot, modelAssetPath,
                skeletalModel ? engine::AssetType::SkeletalMesh
                              : engine::AssetType::StaticMesh).id;
        if (currentModel.Valid()) modelAssetId = currentModel;
        const engine::AssetHandle currentMaterial =
            engine::MakeAssetReference(
                &registry, contentRoot, materialAssetPath,
                engine::AssetType::Material).id;
        if (currentMaterial.Valid()) materialAssetId = currentMaterial;
        const engine::AssetHandle currentGraph =
            engine::MakeAssetReference(
                &registry, contentRoot, animationGraphPath,
                engine::AssetType::AnimationGraph).id;
        if (currentGraph.Valid()) animationGraphAssetId = currentGraph;
        for (CharacterAnimationSource& source : animationSources)
            if (const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &registry, contentRoot, source.file).id)
                source.assetId = current;
        for (CharacterAttachment& attachment : attachments) {
            if (const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &registry, contentRoot, attachment.modelPath,
                        engine::AssetType::StaticMesh).id)
                attachment.modelAssetId = current;
            if (const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &registry, contentRoot, attachment.materialPath,
                        engine::AssetType::Material).id)
                attachment.materialAssetId = current;
        }
        actionClipAssetIds.resize(actionClipAssets.size());
        for (std::size_t i = 0; i < actionClipAssets.size(); ++i)
            if (const engine::AssetHandle current =
                    engine::MakeAssetReference(
                        &registry, contentRoot, actionClipAssets[i],
                        engine::AssetType::AnimationClip).id)
                actionClipAssetIds[i] = current;
    }
    std::error_code ec;
    const std::filesystem::path p(path);
    if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path(), ec);
    std::ofstream out(path, std::ios::trunc);
    if (!out) { if (error) *error = "Could not write character asset: " + path; return false; }
    const CharacterScript legacy{scriptEnabled, scriptClassName, scriptPath};
    const CharacterScript* primary = !scripts.empty() ? &scripts.front() : &legacy;
    out << "3DG_CHARACTER 23 " << assetId.ToString() << '\n'
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
        << navHearingRange << ' '
        << std::quoted(behaviorTreeAsset) << ' ' << navTeam << ' ' << navAutoTarget << '\n'
        << static_cast<int>(navMovementMode) << ' ' << navGravity << ' ' << navMaxFallSpeed << ' '
        << navGroundProbe << ' ' << navStepHeight << ' ' << navMaxSlope << '\n'
        << primary->enabled << ' ' << std::quoted(primary->className)
        << ' ' << std::quoted(primary->path) << '\n'
        << "SCRIPTS " << scripts.size();
    for (const CharacterScript& script : scripts) {
        out << ' ' << script.enabled << ' ' << std::quoted(script.className)
            << ' ' << std::quoted(script.path);
    }
    out << '\n'
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
            << transition.priority << ' ' << transition.canInterrupt << ' '
            << transition.requireAllConditions << ' ' << transition.additionalConditions.size();
        for (const auto& condition : transition.additionalConditions) {
            out << ' ' << std::quoted(condition.parameter) << ' '
                << static_cast<int>(condition.compare) << ' ' << condition.threshold;
        }
        out << '\n';
    }
    for (const auto& action : animationActionProfiles) {
        out << std::quoted(action.name) << ' ' << action.clipIndex << ' ' << std::quoted(action.clipName) << ' '
            << std::quoted(action.maskRootBone) << ' ' << action.fadeIn << ' ' << action.fadeOut << ' '
            << action.speed << '\n';
    }
    for (const auto& event : animationEvents) {
        out << event.clipIndex << ' ' << event.time << ' ' << std::quoted(event.name) << ' '
            << std::quoted(event.clipName.empty() ? std::string("-") : event.clipName) << '\n';
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
    out << "PLAYER_FACING " << playerController.facingMode << ' ' << playerController.turnSpeed << '\n';
    out << "CAMERA_SETTINGS " << playerController.cameraMode << ' '
        << playerController.isometricYaw << ' ' << playerController.isometricPitch << ' '
        << playerController.isometricDistance << ' '
        << playerController.cameraCollision << ' '
        << playerController.cameraProbeRadius << ' '
        << playerController.cameraCollisionPadding << ' '
        << playerController.cameraReturnSpeed << ' '
        << playerController.shoulderCamera << ' '
        << playerController.shoulderOffset << ' '
        << playerController.shoulderSwitchSpeed << ' '
        << playerController.rightShoulder << ' '
        << playerController.lockOnEnabled << ' '
        << playerController.lockOnRange << ' '
        << playerController.lockOnViewAngle << ' '
        << playerController.lockOnTargetHeight << ' '
        << playerController.lockOnTrackingSpeed << ' '
        << playerController.platformerYaw << '\n';
    out << "SOCKETS " << sockets.size() << '\n';
    for (const CharacterSocket& s : sockets) {
        out << std::quoted(s.name.empty() ? std::string("-") : s.name) << ' '
            << std::quoted(s.boneName.empty() ? std::string("-") : s.boneName) << ' '
            << s.position.x << ' ' << s.position.y << ' ' << s.position.z << ' '
            << s.eulerDegrees.x << ' ' << s.eulerDegrees.y << ' ' << s.eulerDegrees.z << ' '
            << s.scale.x << ' ' << s.scale.y << ' ' << s.scale.z << '\n';
    }
    out << "ATTACHMENTS " << attachments.size() << '\n';
    for (const CharacterAttachment& a : attachments) {
        out << std::quoted(a.modelPath.empty() ? std::string("-") : a.modelPath) << ' '
            << std::quoted(a.socketName.empty() ? std::string("-") : a.socketName) << ' '
            << std::quoted(a.materialPath.empty() ? std::string("-") : a.materialPath) << '\n';
    }
    out << "ANIM_GRAPH " << std::quoted(animationGraphPath.empty() ? std::string("-") : animationGraphPath) << '\n';
    out << "ACTION_CLIPS " << actionClipAssets.size();
    for (const std::string& actionPath : actionClipAssets) {
        out << ' ' << std::quoted(actionPath.empty() ? std::string("-") : actionPath);
    }
    out << '\n'
        << "RAGDOLL " << ragdollEnabled << ' '
        << ragdoll.enabled << ' ' << ragdoll.activateOnDeath << ' '
        << ragdoll.totalMass << ' ' << ragdoll.bodyRadiusScale << ' '
        << ragdoll.linearDamping << ' ' << ragdoll.angularDamping << ' '
        << ragdoll.deathImpulse << ' ' << ragdoll.maxBodies << '\n'
        << "ASSET_REFS "
        << (modelAssetId.Valid() ? modelAssetId.ToString() : std::string("-"))
        << ' ' << (animationGraphAssetId.Valid()
            ? animationGraphAssetId.ToString() : std::string("-"))
        << ' ' << (materialAssetId.Valid()
            ? materialAssetId.ToString() : std::string("-"))
        << ' ' << animationSources.size();
    for (const CharacterAnimationSource& source : animationSources)
        out << ' ' << (source.assetId.Valid()
            ? source.assetId.ToString() : std::string("-"));
    out << ' ' << attachments.size();
    for (const CharacterAttachment& attachment : attachments) {
        out << ' ' << (attachment.modelAssetId.Valid()
            ? attachment.modelAssetId.ToString() : std::string("-"))
            << ' ' << (attachment.materialAssetId.Valid()
                ? attachment.materialAssetId.ToString() : std::string("-"));
    }
    out << ' ' << actionClipAssets.size();
    for (std::size_t i = 0; i < actionClipAssets.size(); ++i) {
        const engine::AssetHandle id = i < actionClipAssetIds.size()
            ? actionClipAssetIds[i] : engine::AssetHandle{};
        out << ' ' << (id.Valid() ? id.ToString() : std::string("-"));
    }
    std::vector<engine::AssetHandle> dependencies;
    const auto addDependency = [&](engine::AssetHandle id) {
        if (id.Valid()
            && std::find(dependencies.begin(), dependencies.end(), id)
                   == dependencies.end())
            dependencies.push_back(id);
    };
    addDependency(modelAssetId);
    addDependency(materialAssetId);
    addDependency(animationGraphAssetId);
    if (haveRegistry)
        addDependency(engine::MakeAssetReference(
            &registry, contentRoot, materialAssetPath,
            engine::AssetType::Material).id);
    for (const CharacterAnimationSource& source : animationSources)
        addDependency(source.assetId);
    for (const CharacterAttachment& attachment : attachments) {
        addDependency(attachment.modelAssetId);
        addDependency(attachment.materialAssetId);
        if (haveRegistry)
            addDependency(engine::MakeAssetReference(
                &registry, contentRoot, attachment.materialPath,
                engine::AssetType::Material).id);
    }
    for (engine::AssetHandle id : actionClipAssetIds) addDependency(id);
    out << '\n' << "ASSET_DEPS " << dependencies.size();
    for (engine::AssetHandle id : dependencies) out << ' ' << id.ToString();
    out << '\n';
    return static_cast<bool>(out);
}

bool CharacterAsset::Load(const std::string& path, std::string* error) {
    std::ifstream in(path);
    std::string magic; int loadedVersion = 0;
    if (!(in >> magic >> loadedVersion) || magic != "3DG_CHARACTER" || loadedVersion < 1) {
        if (error) *error = "Invalid character asset: " + path; return false;
    }
    assetId = {};
    if (loadedVersion >= 19) {
        std::string id;
        in >> id;
        if (!engine::AssetHandle::Parse(id, &assetId)) {
            if (error) *error = "Character asset has an invalid stable ID: " + path;
            return false;
        }
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
       >> navVisionRange >> navVisionHalfAngle;
    if (loadedVersion >= 22) in >> navHearingRange;   // character-authored hearing range
    in >> std::quoted(behaviorTreeAsset) >> navTeam >> navAutoTarget
       >> movementMode >> navGravity >> navMaxFallSpeed >> navGroundProbe >> navStepHeight >> navMaxSlope
       >> scriptEnabled >> std::quoted(scriptClassName) >> std::quoted(scriptPath);
    if (!in) { if (error) *error = "Character asset is incomplete: " + path; return false; }
    collider.shape = static_cast<engine::ecs::ColliderShape>(shape);
    navMovementMode = static_cast<engine::ai::AiMovementMode>(movementMode);
    version = 21; // Upgrade legacy assets when they are next saved.
    modelAssetId = {};
    materialAssetId = {};
    animationGraphAssetId = {};
    actionClipAssetIds.clear();
    scripts.clear();
    if (loadedVersion >= 18) {
        std::string scriptsTag;
        std::size_t scriptCount = 0;
        in >> scriptsTag >> scriptCount;
        if (!in || scriptsTag != "SCRIPTS") {
            if (error) *error = "Character asset has an invalid scripts section: " + path;
            return false;
        }
        scripts.reserve(scriptCount);
        for (std::size_t i = 0; i < scriptCount; ++i) {
            CharacterScript script;
            in >> script.enabled >> std::quoted(script.className) >> std::quoted(script.path);
            scripts.push_back(std::move(script));
        }
        if (scripts.empty() && !scriptClassName.empty()) {
            scripts.push_back({scriptEnabled, scriptClassName, scriptPath});
        }
    } else if (!scriptClassName.empty()) {
        scripts.push_back({scriptEnabled, scriptClassName, scriptPath});
    }
    animationGraphPath.clear();
    actionClipAssets.clear();
    sockets.clear();
    attachments.clear();
    // Default the render-only model offset; overwritten below for v7+ files.
    modelOffsetPosition = glm::vec3(0.0f);
    modelOrientationEuler = glm::vec3(0.0f);
    modelOffsetScale = glm::vec3(1.0f);
    playerController.facingMode = 0;   // default: face camera; overwritten for v8+
    playerController.turnSpeed = 12.0f;
    playerController.cameraMode = playerController.firstPerson ? 1 : 0;
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
            if (loadedVersion >= 13) {
                std::size_t conditionCount = 0;
                in >> transition.requireAllConditions >> conditionCount;
                for (std::size_t c = 0; c < conditionCount; ++c) {
                    EditorScene::AnimationStateTransition::Condition condition;
                    int conditionCompare = 0;
                    in >> std::quoted(condition.parameter)
                       >> conditionCompare >> condition.threshold;
                    condition.compare =
                        static_cast<EditorScene::AnimationStateTransition::Compare>(
                            std::clamp(conditionCompare, 0, 3));
                    transition.additionalConditions.push_back(std::move(condition));
                }
            }
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
            if (loadedVersion >= 17) in >> std::quoted(event.clipName);
            if (event.clipName == "-") event.clipName.clear();
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
    if (loadedVersion >= 8) {
        std::string facingTag;
        if ((in >> facingTag) && facingTag == "PLAYER_FACING") {
            in >> playerController.facingMode >> playerController.turnSpeed;
        }
    }
    if (loadedVersion >= 14) {
        std::string cameraTag;
        if ((in >> cameraTag)
            && (cameraTag == "CAMERA_MODE" || cameraTag == "CAMERA_SETTINGS")) {
            in >> playerController.cameraMode
               >> playerController.isometricYaw
               >> playerController.isometricPitch
               >> playerController.isometricDistance;
            if (cameraTag == "CAMERA_SETTINGS") {
                in >> playerController.cameraCollision
                   >> playerController.cameraProbeRadius
                   >> playerController.cameraCollisionPadding
                   >> playerController.cameraReturnSpeed
                   >> playerController.shoulderCamera
                   >> playerController.shoulderOffset
                   >> playerController.shoulderSwitchSpeed
                   >> playerController.rightShoulder
                   >> playerController.lockOnEnabled
                   >> playerController.lockOnRange
                   >> playerController.lockOnViewAngle
                   >> playerController.lockOnTargetHeight
                   >> playerController.lockOnTrackingSpeed;
                if (loadedVersion >= 23) in >> playerController.platformerYaw;
            }
            playerController.cameraMode = std::clamp(playerController.cameraMode, 0, 3);
            playerController.firstPerson = playerController.cameraMode == 1;
            playerController.isometricPitch =
                std::clamp(playerController.isometricPitch, -89.0f, 89.0f);
            playerController.isometricDistance =
                std::max(playerController.isometricDistance, 0.0f);
            playerController.cameraProbeRadius =
                std::max(playerController.cameraProbeRadius, 0.0f);
            playerController.cameraCollisionPadding =
                std::max(playerController.cameraCollisionPadding, 0.0f);
            playerController.cameraReturnSpeed =
                std::max(playerController.cameraReturnSpeed, 0.0f);
            playerController.shoulderOffset =
                std::max(playerController.shoulderOffset, 0.0f);
            playerController.shoulderSwitchSpeed =
                std::max(playerController.shoulderSwitchSpeed, 0.0f);
            playerController.lockOnRange =
                std::max(playerController.lockOnRange, 0.0f);
            playerController.lockOnViewAngle =
                std::clamp(playerController.lockOnViewAngle, 0.0f, 180.0f);
            playerController.lockOnTrackingSpeed =
                std::max(playerController.lockOnTrackingSpeed, 0.0f);
        }
    }
    if (loadedVersion == 9) {
        // Legacy: attachments stored bone + offset directly. Convert each into a socket
        // plus an attachment that references it, matching the new socket model.
        std::string tag;
        std::size_t count = 0;
        if ((in >> tag >> count) && tag == "ATTACHMENTS") {
            for (std::size_t i = 0; i < count; ++i) {
                CharacterSocket s;
                CharacterAttachment a;
                in >> std::quoted(a.modelPath) >> std::quoted(s.boneName)
                   >> s.position.x >> s.position.y >> s.position.z
                   >> s.eulerDegrees.x >> s.eulerDegrees.y >> s.eulerDegrees.z
                   >> s.scale.x >> s.scale.y >> s.scale.z;
                if (a.modelPath == "-") a.modelPath.clear();
                if (s.boneName == "-") s.boneName.clear();
                s.name = "Socket" + std::to_string(i + 1);
                a.socketName = s.name;
                sockets.push_back(std::move(s));
                attachments.push_back(std::move(a));
            }
        }
    } else if (loadedVersion >= 10) {
        std::string tag;
        std::size_t count = 0;
        if ((in >> tag >> count) && tag == "SOCKETS") {
            for (std::size_t i = 0; i < count; ++i) {
                CharacterSocket s;
                in >> std::quoted(s.name) >> std::quoted(s.boneName)
                   >> s.position.x >> s.position.y >> s.position.z
                   >> s.eulerDegrees.x >> s.eulerDegrees.y >> s.eulerDegrees.z
                   >> s.scale.x >> s.scale.y >> s.scale.z;
                if (s.name == "-") s.name.clear();
                if (s.boneName == "-") s.boneName.clear();
                sockets.push_back(std::move(s));
            }
        }
        if ((in >> tag >> count) && tag == "ATTACHMENTS") {
            for (std::size_t i = 0; i < count; ++i) {
                CharacterAttachment a;
                in >> std::quoted(a.modelPath) >> std::quoted(a.socketName);
                if (loadedVersion >= 11) {
                    in >> std::quoted(a.materialPath);
                    if (a.materialPath == "-") a.materialPath.clear();
                }
                if (a.modelPath == "-") a.modelPath.clear();
                if (a.socketName == "-") a.socketName.clear();
                attachments.push_back(std::move(a));
            }
        }
    }
    if (loadedVersion >= 12) {
        std::string tag;
        if ((in >> tag) && tag == "ANIM_GRAPH") {
            in >> std::quoted(animationGraphPath);
            if (animationGraphPath == "-") animationGraphPath.clear();
        }
    }
    if (loadedVersion >= 16) {
        std::string tag;
        std::size_t count = 0;
        if ((in >> tag >> count) && tag == "ACTION_CLIPS") {
            for (std::size_t i = 0; i < count; ++i) {
                std::string actionPath;
                in >> std::quoted(actionPath);
                if (actionPath != "-") actionClipAssets.push_back(std::move(actionPath));
            }
        }
    }
    ragdollEnabled = true;
    ragdoll = engine::Ragdoll{};
    if (loadedVersion >= 21) {
        std::string tag;
        in >> tag >> ragdollEnabled
           >> ragdoll.enabled >> ragdoll.activateOnDeath
           >> ragdoll.totalMass >> ragdoll.bodyRadiusScale
           >> ragdoll.linearDamping >> ragdoll.angularDamping
           >> ragdoll.deathImpulse >> ragdoll.maxBodies;
        if (!in || tag != "RAGDOLL") {
            if (error) *error = "Character ragdoll data is invalid: " + path;
            return false;
        }
    }
    if (loadedVersion >= 19) {
        std::string tag;
        std::string id;
        std::size_t count = 0;
        in >> tag >> id;
        if (!in || tag != "ASSET_REFS"
            || (id != "-" && !engine::AssetHandle::Parse(id, &modelAssetId))) {
            if (error) *error = "Character asset references are invalid: " + path;
            return false;
        }
        in >> id;
        if (!in || (id != "-"
            && !engine::AssetHandle::Parse(id, &animationGraphAssetId))) {
            if (error) *error = "Character graph identity is invalid: " + path;
            return false;
        }
        if (loadedVersion >= 20) {
            in >> id;
            if (!in || (id != "-"
                && !engine::AssetHandle::Parse(id, &materialAssetId))) {
                if (error) *error =
                    "Character material identity is invalid: " + path;
                return false;
            }
        }
        in >> count;
        if (!in || count != animationSources.size()) {
            if (error) *error = "Character animation reference count is invalid: " + path;
            return false;
        }
        for (CharacterAnimationSource& source : animationSources) {
            in >> id;
            if (!in || (id != "-"
                && !engine::AssetHandle::Parse(id, &source.assetId))) {
                if (error) *error = "Character animation identity is invalid: " + path;
                return false;
            }
        }
        in >> count;
        if (!in || count != attachments.size()) {
            if (error) *error = "Character attachment reference count is invalid: " + path;
            return false;
        }
        for (CharacterAttachment& attachment : attachments) {
            in >> id;
            if (!in || (id != "-"
                && !engine::AssetHandle::Parse(id, &attachment.modelAssetId))) {
                if (error) *error = "Character attachment identity is invalid: " + path;
                return false;
            }
            if (loadedVersion >= 20) {
                in >> id;
                if (!in || (id != "-"
                    && !engine::AssetHandle::Parse(
                        id, &attachment.materialAssetId))) {
                    if (error) *error =
                        "Character attachment material identity is invalid: "
                        + path;
                    return false;
                }
            }
        }
        in >> count;
        if (!in || count != actionClipAssets.size()) {
            if (error) *error = "Character action reference count is invalid: " + path;
            return false;
        }
        actionClipAssetIds.resize(count);
        for (engine::AssetHandle& actionId : actionClipAssetIds) {
            in >> id;
            if (!in || (id != "-"
                && !engine::AssetHandle::Parse(id, &actionId))) {
                if (error) *error = "Character action identity is invalid: " + path;
                return false;
            }
        }
    }
    if (loadedVersion >= 19) {
        const std::string contentRoot = engine::FindContentRootForAsset(path);
        engine::AssetRegistry registry;
        std::string ignored;
        if (!contentRoot.empty()
            && registry.Load(
                engine::AssetRegistry::DefaultRegistryPath(contentRoot),
                &ignored)) {
            if (modelAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot, {modelAssetId, modelAssetPath},
                    skeletalModel ? engine::AssetType::SkeletalMesh
                                  : engine::AssetType::StaticMesh);
                if (!resolved.empty()) modelAssetPath = resolved;
            }
            if (materialAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot,
                    {materialAssetId, materialAssetPath},
                    engine::AssetType::Material);
                if (!resolved.empty()) materialAssetPath = resolved;
            }
            if (animationGraphAssetId.Valid()) {
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot,
                    {animationGraphAssetId, animationGraphPath},
                    engine::AssetType::AnimationGraph);
                if (!resolved.empty()) animationGraphPath = resolved;
            }
            for (CharacterAnimationSource& source : animationSources) {
                if (!source.assetId.Valid()) continue;
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot, {source.assetId, source.file});
                if (!resolved.empty()) source.file = resolved;
            }
            for (CharacterAttachment& attachment : attachments) {
                if (attachment.modelAssetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &registry, contentRoot,
                        {attachment.modelAssetId, attachment.modelPath},
                        engine::AssetType::StaticMesh);
                    if (!resolved.empty()) attachment.modelPath = resolved;
                }
                if (attachment.materialAssetId.Valid()) {
                    const std::string resolved = engine::ResolveAssetReference(
                        &registry, contentRoot,
                        {attachment.materialAssetId, attachment.materialPath},
                        engine::AssetType::Material);
                    if (!resolved.empty()) attachment.materialPath = resolved;
                }
            }
            for (std::size_t i = 0;
                 i < actionClipAssets.size()
                     && i < actionClipAssetIds.size(); ++i) {
                if (!actionClipAssetIds[i].Valid()) continue;
                const std::string resolved = engine::ResolveAssetReference(
                    &registry, contentRoot,
                    {actionClipAssetIds[i], actionClipAssets[i]},
                    engine::AssetType::AnimationClip);
                if (!resolved.empty()) actionClipAssets[i] = resolved;
            }
        }
    }
    return true;
}
