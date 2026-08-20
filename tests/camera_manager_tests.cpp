#include "EditorScene.h"
#include "RuntimeSceneExporter.h"
#include <engine/scene/RuntimeSceneLoader.h>
#include <engine/graphics/CameraBlend.h>
#include <engine/graphics/CameraShake.h>
#include <engine/graphics/CameraSequence.h>
#include <engine/gameplay/CameraDirector.h>
#include <engine/ui/Hud.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
}

bool Near(float a, float b, float epsilon = 0.0001f) {
    return std::fabs(a - b) <= epsilon;
}

} // namespace

int main() {
    {
        const std::filesystem::path hudPath =
            std::filesystem::temp_directory_path()
            / "3dg_native_hud_test.hud";
        engine::HudDocument hud;
        engine::HudWidget& image =
            hud.Add(engine::HudWidgetType::Image);
        image.imageAsset = "Content/Textures/Icon.3dgtex";
        image.shaderPath = "Content/Shaders/UI.3dgshader";
        std::string error;
        Check(hud.Save(hudPath.string(), &error),
              "save engine-owned HUD asset");
        const engine::AssetHandle hudId = hud.assetId;
        engine::HudDocument loadedHud;
        Check(hudId.Valid()
              && loadedHud.Load(hudPath.string(), &error)
              && loadedHud.assetId == hudId,
              "HUD stable ID survives save and load");
        Check(hud.Save(hudPath.string(), &error)
              && hud.assetId == hudId,
              "HUD overwrite preserves its stable ID");
        std::filesystem::remove(hudPath);
    }
    {
        engine::CameraPose from;
        from.position = glm::vec3(0.0f);
        from.target = glm::vec3(0.0f, 0.0f, -1.0f);
        from.fov = 40.0f;
        engine::CameraPose to = from;
        to.position.x = 10.0f;
        to.target.x = 10.0f;
        to.fov = 80.0f;

        engine::CameraBlend blend;
        blend.Start(from, to, 2.0f, engine::CameraBlend::Easing::EaseIn);
        const engine::CameraPose quarter = blend.Update(1.0f);
        Check(Near(quarter.position.x, 2.5f), "ease-in camera position");
        Check(Near(quarter.fov, 50.0f), "camera lens blends with pose");
        Check(blend.Active(), "blend remains active before duration");
        const engine::CameraPose finished = blend.Update(1.0f);
        Check(Near(finished.position.x, 10.0f), "blend reaches exact target");
        Check(!blend.Active(), "blend completes at duration");
    }

    {
        engine::CameraShake shake;
        engine::CameraShakeSettings settings;
        settings.duration = 1.0f;
        settings.frequency = 12.0f;
        settings.translationAmplitude = glm::vec3(0.2f);
        settings.rotationAmplitudeDegrees = glm::vec2(2.0f);
        shake.Start(settings);
        shake.StartImpulse(0.5f, 0.5f, 20.0f);
        Check(shake.Active(), "camera shake starts");
        Check(shake.ActiveCount() == 2, "camera shakes layer");
        const engine::CameraShakeSample sample = shake.Update(0.1f);
        const float motion = glm::dot(sample.translation, sample.translation)
            + glm::dot(sample.rotationDegrees, sample.rotationDegrees);
        Check(motion > 0.000001f, "camera shake produces an offset");
        shake.Update(1.0f);
        Check(!shake.Active(), "camera shake expires");
    }

    {
        engine::CameraPose start;
        engine::CameraSequenceShot first;
        first.pose = start;
        first.pose.position.x = 10.0f;
        first.pose.target.x = 10.0f;
        first.travelDuration = 2.0f;
        first.holdDuration = 0.5f;
        first.eventName = "Reveal";
        engine::CameraSequenceShot second = first;
        second.pose.position.x = 20.0f;
        second.pose.target.x = 20.0f;
        second.travelDuration = 1.0f;
        second.holdDuration = 0.0f;

        engine::CameraSequencePlayer sequence;
        sequence.Start(start, {first, second}, false);
        Check(Near(sequence.Duration(), 3.5f), "camera sequence reports timeline duration");
        Check(Near(sequence.Update(1.0f).position.x, 5.0f),
              "camera sequence travels between shots");
        Check(Near(sequence.Update(1.0f).position.x, 10.0f) && sequence.Holding(),
              "camera sequence reaches and holds a shot");
        const auto events = sequence.TakeEvents();
        Check(events.size() == 1 && events[0] == "Reveal",
              "camera sequence emits shot event");
        sequence.Update(0.5f);
        Check(sequence.ShotIndex() == 1, "camera sequence advances to next shot");
        Check(Near(sequence.Update(1.0f).position.x, 20.0f),
              "camera sequence reaches final shot");
        sequence.Update(0.0f);
        Check(!sequence.Active(), "non-looping camera sequence completes");
        sequence.Start(start, {first, second}, false);
        Check(Near(sequence.Seek(1.0f).position.x, 5.0f)
              && Near(sequence.Time(), 1.0f),
              "camera sequence supports absolute timeline seeking");
        const glm::vec3 curved = engine::CameraSequencePlayer::CatmullRom(
            {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {2.0f, 1.0f, 0.0f}, {4.0f, 1.0f, 0.0f}, 0.5f);
        Check(curved.y > 0.4f && curved.x > 1.4f,
              "Catmull-Rom rail produces a curved path");
    }

    {
        engine::CameraDirector director;
        director.Play("Opening", true, true);
        const auto commands = director.TakeCommands();
        Check(commands.size() == 1
              && commands[0].type == engine::CameraSequenceCommand::Type::Play,
              "camera director queues script play command");
        director.SetPlaying("Opening", true, true);
        Check(director.InputLocked() && director.Skippable(),
              "camera director exposes cinematic controls");
        director.NotifyFinished("Opening", true);
        Check(!director.Playing() && director.Events().size() == 1
              && director.Events()[0].skipped,
              "camera director reports skipped completion");
        director.NotifyTimelineEvent("Opening", "Reveal");
        Check(director.TimelineEvents().size() == 1,
              "camera director exposes timeline events");
    }

    {
        EditorScene materialScene;
        engine::Mesh placeholder;
        materialScene.AddEmpty(placeholder);
        materialScene.AddEmpty(placeholder);
        materialScene.AddEmpty(placeholder);
        Check(materialScene.ToggleSelectedLocked(),
              "lock one object before multi-material assignment");
        materialScene.SelectIndex(0);
        materialScene.ToggleSelection(1);
        materialScene.ToggleSelection(2);
        const engine::AssetHandle materialId = engine::AssetHandle::Generate();
        const std::string materialPath =
            "Content/Materials/ForgeSelection.3dgmat";
        Check(materialScene.SetSelectedMaterialAssetToSelection(
                  materialPath, materialId) == 2,
              "multi-material assignment skips locked selected objects");
        Check(materialScene.Objects()[0].materialAssetPath == materialPath
              && materialScene.Objects()[0].materialAssetId == materialId
              && materialScene.Objects()[1].materialAssetPath == materialPath
              && materialScene.Objects()[1].materialAssetId == materialId
              && materialScene.Objects()[2].materialAssetPath.empty(),
              "multi-material assignment updates every unlocked selection member");
        Check(materialScene.Undo(
                  placeholder, placeholder, placeholder, placeholder, placeholder,
                  placeholder, placeholder, placeholder, placeholder)
              && materialScene.Objects()[0].materialAssetPath.empty()
              && materialScene.Objects()[1].materialAssetPath.empty()
              && materialScene.Objects()[2].materialAssetPath.empty(),
              "multi-material assignment is restored by one undo step");
    }

    {
        EditorScene decalScene;
        engine::Mesh placeholder;
        decalScene.AddDecal(
            placeholder, glm::vec3(2.0f, 0.0f, -1.0f),
            glm::vec3(0.0f, 1.0f, 0.0f), glm::vec2(3.0f, 1.5f),
            35.0f, 0.02f, 0.45f, "Content/Materials/Scorch.3dgmat");
        const std::filesystem::path decalPath =
            std::filesystem::temp_directory_path() / "3dg_decal_test.scene";
        std::string decalError;
        Check(decalScene.Save(decalPath.string(), &decalError),
              "save surface decal metadata");
        EditorScene loadedDecals;
        Check(loadedDecals.Load(
                  decalPath.string(), placeholder, placeholder, placeholder,
                  placeholder, placeholder, placeholder, placeholder,
                  placeholder, placeholder, &decalError),
              "load surface decal metadata");
        Check(loadedDecals.Objects().size() == 1
                  && loadedDecals.Objects()[0].decal
                  && Near(loadedDecals.Objects()[0].decalOpacity, 0.45f)
                  && Near(loadedDecals.Objects()[0].decalSurfaceOffset, 0.02f),
              "decal opacity and surface offset survive scene round trip");

        const std::filesystem::path runtimeDecalPath =
            std::filesystem::temp_directory_path() / "3dg_decal_test.3dgscene";
        Check(RuntimeSceneExporter::Export(
                  decalScene, runtimeDecalPath.string(), &decalError),
              "export decal actor to runtime scene");
        engine::RuntimeSceneLoader::Scene runtimeDecals;
        Check(engine::RuntimeSceneLoader::Load(
                  runtimeDecalPath.string(), &runtimeDecals, &decalError),
              "load runtime decal actor");
        Check(runtimeDecals.entities.size() == 1
                  && runtimeDecals.entities[0].primitive == "Plane"
                  && runtimeDecals.entities[0].materialParameterOverrides.at("Opacity")
                      == "0.450000",
              "runtime decal keeps its plane, material and opacity override");
        std::filesystem::remove(decalPath);
        std::filesystem::remove(runtimeDecalPath);
    }

    EditorScene scene;
    engine::Mesh emptyPlaceholder;
    scene.AddEmpty(emptyPlaceholder);
    Check(scene.ToggleSelectedLocked(), "lock empty object for script attachment test");
    Check(scene.SelectedLocked(), "script attachment test object is locked");
    Check(scene.SetSelectedScript(
              "PlayerAttack", "Content/Scripts/PlayerAttack.h", true),
          "locked object accepts script component attachment");
    Check(scene.SelectedObject()
              && scene.SelectedObject()->scriptEnabled
              && scene.SelectedObject()->scriptClassName == "PlayerAttack",
          "locked object stores attached script metadata");
    Check(scene.SetSelectedScriptEnabled(false),
          "locked object permits script component toggle");

    engine::ParticleSystemComponent particleSettings;
    particleSettings.assetId = engine::AssetHandle::Generate();
    particleSettings.config.texturePath =
        "Content/Textures/Fireball.3dgtex";
    particleSettings.config.textureAssetId =
        engine::AssetHandle::Generate();
    particleSettings.config.meshPath =
        "Content/Meshes/Fireball.3dgmesh";
    particleSettings.config.meshAssetId =
        engine::AssetHandle::Generate();
    particleSettings.config.shaderPath =
        "Content/Shaders/Fireball.3dgshader";
    particleSettings.config.shaderAssetId =
        engine::AssetHandle::Generate();
    particleSettings.config.shaderParameters.push_back(
        {"Intensity", 0, "2.5"});
    scene.AddParticleSystem(
        emptyPlaceholder, {}, "Content/Particles/Fireball.particle",
        particleSettings);
    engine::ParticleEffectLayer particleLayer;
    particleLayer.name = "Impact";
    particleLayer.assetPath =
        "Content/Particles/FireballImpact.particle";
    particleLayer.assetId = engine::AssetHandle::Generate();
    Check(scene.SetSelectedParticleEffectLayers({particleLayer}),
          "scene accepts a stable particle effect layer reference");

    EditorScene::CameraPreset gameplay;
    gameplay.name = "Gameplay Camera";
    gameplay.position = {2.0f, 4.0f, 8.0f};
    gameplay.target = {0.0f, 1.0f, 0.0f};
    gameplay.fov = 60.0f;
    gameplay.blendDuration = 0.75f;
    gameplay.blendEasing = 3;
    gameplay.primary = true;
    gameplay.useInPlay = true;

    const std::size_t gameplayIndex = scene.AddCameraPreset(gameplay);
    Check(gameplayIndex == 0, "first camera index");
    Check(scene.PrimaryCameraPreset() != nullptr, "primary camera exists");
    Check(scene.PrimaryCameraPreset()->name == "Gameplay Camera", "primary camera name");

    EditorScene::CameraPreset cutaway;
    cutaway.name = "Cutaway";
    const std::size_t cutawayIndex = scene.AddCameraPreset(cutaway);
    Check(scene.SetPrimaryCameraPreset(cutawayIndex), "set second camera primary");
    Check(scene.PrimaryCameraPreset()->name == "Cutaway", "primary camera changed");

    const std::size_t duplicateIndex = scene.DuplicateCameraPreset(cutawayIndex);
    Check(duplicateIndex == 2, "duplicate camera index");
    Check(!scene.CameraPresets()[duplicateIndex].primary, "duplicate is not primary");
    Check(scene.RemoveCameraPreset(duplicateIndex), "remove duplicate camera");

    EditorScene::CameraSequence intro;
    intro.name = "Opening Cinematic";
    intro.shots.push_back({"Gameplay Camera", 1.5f, 0.25f, 1});
    intro.shots.push_back({"Cutaway", 2.0f, 1.0f, 3});
    EditorScene::CinematicCue cue;
    cue.type = EditorScene::CinematicCueType::Event;
    cue.time = 1.25f;
    cue.name = "RevealBoss";
    intro.cues.push_back(cue);
    Check(scene.AddCameraSequence(intro) == 0, "add camera sequence");

    EditorScene::Environment environment = scene.GetEnvironment();
    environment.hudAsset = "Content/UI/Gameplay.hud";
    environment.hudAssetId = engine::AssetHandle::Generate();
    EditorScene::Environment::PostProcessEffect postEffect;
    postEffect.shaderPath = "Content/Shaders/ArcaneGlow.3dgshader";
    postEffect.enabled = true;
    postEffect.parameters.push_back({"Intensity", 0, "1.75"});
    environment.postProcessEffects.push_back(postEffect);
    scene.SetEnvironment(environment);

    EditorScene::GameModeSettings gameMode;
    gameMode.playerObjectName = "PlayerStart";
    gameMode.playerInputEnabled = true;
    gameMode.startPaused = true;
    gameMode.allowPause = false;
    gameMode.allowRestart = false;
    gameMode.loseOnPlayerDeath = false;
    gameMode.initialScore = 25;
    gameMode.cameraOverride = true;
    gameMode.cameraMode = 2;
    scene.SetGameModeSettings(gameMode);

    scene.AddPlane(emptyPlaceholder);
    Check(scene.SetSelectedWater(
              48.0f, 96, 1.5f,
              {0.15f, 0.50f, 0.48f}, {0.02f, 0.10f, 0.18f},
              {0.55f, 0.72f, 0.92f}, 0.72f, 5.0f, 0.8f, 320.0f),
          "create depth-aware water test body");
    Check(scene.SetSelectedWaterDepth(7.5f, 1.25f, 0.9f),
          "set depth-aware water settings");
    Check(scene.SetSelectedWaterOptics(0.035f, 0.28f, 0.65f, 1.1f),
          "set water reflection and refraction settings");
    Check(scene.SetSelectedWaterEffects(
              0.42f, 2.25f, 1800.0f, {0.02f, 0.24f, 0.31f}, 0.21f, 0.009f, 4.5f),
          "set water caustics and underwater settings");
    EditorScene::AnimationSource timedSource;
    timedSource.file = "Content/Animations/Walk.3dganim";
    timedSource.clipName = "Walk";
    timedSource.sourceClipName = "Walk Take";
    timedSource.basePlaybackSpeed = 1.5f;
    scene.SetSelectedAnimationSettings(true, 0, "Walk", true, true, 1.0f);
    Check(scene.SetSelectedAnimationSources({timedSource}),
          "attach animation source timing metadata to scene object");
    // Dedicated water actors are exported through the runtime water section.
    // Also attach the timing metadata to a regular entity so the runtime entity
    // serialization path is covered independently.
    scene.AddEmpty(emptyPlaceholder);
    Check(scene.SetSelectedAnimationSettings(
              true, 0, "Walk", true, true, 1.0f),
          "configure runtime animation timing test entity");
    Check(scene.SetSelectedAnimationSources({timedSource}),
          "attach animation timing metadata to runtime entity");

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "3dg_camera_manager_test.scene";
    std::string error;
    Check(scene.Save(path.string(), &error, false), "save camera presets");

    std::ifstream input(path);
    std::ostringstream contents;
    contents << input.rdbuf();
    const std::string saved = contents.str();
    Check(saved.find("camera \"Gameplay Camera\"") != std::string::npos,
          "camera name with spaces is quoted");
    Check(saved.find("0.75 3") != std::string::npos,
          "camera blend settings are serialized");
    Check(saved.find("camera \"Cutaway\"") != std::string::npos,
          "second camera serialized");
    Check(saved.find("camera_sequence \"Opening Cinematic\"") != std::string::npos,
          "camera sequence serialized");
    Check(saved.find("\"Gameplay Camera\" 1.5 0.25 1") != std::string::npos,
          "camera sequence shot settings serialized");
    Check(saved.find("\"RevealBoss\"") != std::string::npos,
          "cinematic timeline cue serialized");
    Check(saved.find(
              "post_effect \"Content/Shaders/ArcaneGlow.3dgshader\"")
              != std::string::npos
          && saved.find(" 1 1 \"Intensity\" 0 \"1.75\"")
              != std::string::npos
          && saved.find("\"Intensity\" 0 \"1.75\"") != std::string::npos,
          "post-process stack and parameters are serialized");
    Check(saved.find(
              "game_mode \"PlayerStart\" 1 1 0 0 0 25 1 2")
              != std::string::npos,
          "game mode startup and camera override settings are serialized");
    input.close();

    EditorScene loadedScene;
    Check(loadedScene.Load(path.string(),
              emptyPlaceholder, emptyPlaceholder, emptyPlaceholder,
              emptyPlaceholder, emptyPlaceholder, emptyPlaceholder,
              emptyPlaceholder, emptyPlaceholder, emptyPlaceholder, &error),
          "load scene with depth-aware water settings");
    const auto loadedWater = std::find_if(
        loadedScene.Objects().begin(), loadedScene.Objects().end(),
        [](const EditorScene::Object& object) { return object.isWater; });
    Check(loadedWater != loadedScene.Objects().end()
              && Near(loadedWater->waterDepthFadeDistance, 7.5f)
              && Near(loadedWater->waterShoreFoamWidth, 1.25f)
              && Near(loadedWater->waterShoreFoamStrength, 0.9f)
              && Near(loadedWater->waterRefractionStrength, 0.035f)
              && Near(loadedWater->waterReflectionRoughness, 0.28f)
              && Near(loadedWater->waterEnvironmentReflectionStrength, 0.65f)
              && Near(loadedWater->waterAbsorptionStrength, 1.1f)
              && Near(loadedWater->waterCausticsStrength, 0.42f)
              && Near(loadedWater->waterCausticsScale, 2.25f)
              && Near(loadedWater->waterMaxRenderDistance, 1800.0f)
              && Near(loadedWater->waterUnderwaterTint.g, 0.24f)
              && Near(loadedWater->waterUnderwaterFogDensity, 0.21f)
              && Near(loadedWater->waterUnderwaterDistortion, 0.009f)
              && Near(loadedWater->waterUnderwaterTransitionSpeed, 4.5f)
              && loadedWater->animationSources.size() == 1
              && Near(loadedWater->animationSources[0].basePlaybackSpeed, 1.5f),
          "water depth and optics settings survive scene round trip");
    std::filesystem::remove(path);

    const std::filesystem::path runtimePath =
        std::filesystem::temp_directory_path() / "3dg_camera_manager_test.3dgscene";
    Check(RuntimeSceneExporter::Export(scene, runtimePath.string(), &error),
          "export runtime camera data");
    engine::RuntimeSceneLoader::Scene runtimeScene;
    Check(engine::RuntimeSceneLoader::Load(
              runtimePath.string(), &runtimeScene, &error),
          "load runtime camera data");
    Check(runtimeScene.cameraPresets.size() == 2,
          "runtime export preserves saved cameras");
    Check(runtimeScene.cameraSequences.size() == 1,
          "runtime export preserves camera sequences");
    Check(runtimeScene.waters.size() == 1
              && Near(runtimeScene.waters[0].size, 48.0f)
              && Near(runtimeScene.waters[0].refractionStrength, 0.035f)
              && Near(runtimeScene.waters[0].causticsStrength, 0.42f)
              && Near(runtimeScene.waters[0].underwaterFogDensity, 0.21f),
          "runtime export preserves complete water settings");
    const auto timedRuntimeEntity = std::find_if(
        runtimeScene.entities.begin(), runtimeScene.entities.end(),
        [](const auto& entity) { return !entity.animationSources.empty(); });
    Check(timedRuntimeEntity != runtimeScene.entities.end()
          && Near(timedRuntimeEntity->animationSources[0].basePlaybackSpeed, 1.5f),
          "runtime scene preserves baked clip base playback speed");
    Check(runtimeScene.cameraSequences[0].shots.size() == 2
          && runtimeScene.cameraSequences[0].shots[0].cameraName == "Gameplay Camera",
          "runtime export preserves sequence shot references");
    Check(runtimeScene.cameraSequences[0].cues.size() == 1
          && runtimeScene.cameraSequences[0].cues[0].name == "RevealBoss",
          "runtime export preserves cinematic cues");
    Check(runtimeScene.gameMode.playerObjectName == "PlayerStart"
          && runtimeScene.gameMode.startPaused
          && !runtimeScene.gameMode.allowPause
          && runtimeScene.gameMode.initialScore == 25
          && runtimeScene.gameMode.cameraOverride
          && runtimeScene.gameMode.cameraMode == 2,
          "runtime export preserves Game Mode settings");
    Check(runtimeScene.environment.hudAssetId
              == environment.hudAssetId
          && runtimeScene.environment.hudAsset
              == environment.hudAsset,
          "runtime export preserves the stable HUD reference");
    const auto particleEntity = std::find_if(
        runtimeScene.entities.begin(), runtimeScene.entities.end(),
        [](const engine::RuntimeSceneLoader::EntityDesc& entity) {
            return entity.particleSystemEnabled;
        });
    Check(particleEntity != runtimeScene.entities.end(),
          "runtime scene preserves the particle component");
    Check(particleEntity->particleSystem.assetId
              == particleSettings.assetId,
          "runtime scene preserves the particle asset ID");
    Check(particleEntity->particleSystem.config.textureAssetId
              == particleSettings.config.textureAssetId
          && particleEntity->particleSystem.config.meshAssetId
              == particleSettings.config.meshAssetId
          && particleEntity->particleSystem.config.shaderAssetId
              == particleSettings.config.shaderAssetId,
          "runtime scene preserves particle renderer dependency IDs");
    Check(particleEntity->particleSystem.config.shaderPath
              == particleSettings.config.shaderPath
          && particleEntity->particleSystem.config.shaderParameters.size()
              == 1,
          "runtime scene preserves the particle custom shader and parameters");
    Check(particleEntity->particleEffect.layers.size() == 1
          && particleEntity->particleEffect.layers[0].assetId
              == particleLayer.assetId,
          "runtime scene preserves particle effect layer IDs");
    std::filesystem::remove(runtimePath);

    std::cout << "camera manager tests passed\n";
    return 0;
}
