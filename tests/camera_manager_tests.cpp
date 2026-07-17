#include "EditorScene.h"
#include "RuntimeSceneExporter.h"
#include <engine/scene/RuntimeSceneLoader.h>
#include <engine/graphics/CameraBlend.h>
#include <engine/graphics/CameraShake.h>
#include <engine/graphics/CameraSequence.h>
#include <engine/gameplay/CameraDirector.h>

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

    EditorScene scene;

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
    EditorScene::Environment::PostProcessEffect postEffect;
    postEffect.shaderPath = "Content/Shaders/ArcaneGlow.3dgshader";
    postEffect.enabled = true;
    postEffect.parameters.push_back({"Intensity", 0, "1.75"});
    environment.postProcessEffects.push_back(postEffect);
    scene.SetEnvironment(environment);

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
              "post_effect \"Content/Shaders/ArcaneGlow.3dgshader\" 1 1")
              != std::string::npos
          && saved.find("\"Intensity\" 0 \"1.75\"") != std::string::npos,
          "post-process stack and parameters are serialized");
    input.close();
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
    Check(runtimeScene.cameraSequences[0].shots.size() == 2
          && runtimeScene.cameraSequences[0].shots[0].cameraName == "Gameplay Camera",
          "runtime export preserves sequence shot references");
    Check(runtimeScene.cameraSequences[0].cues.size() == 1
          && runtimeScene.cameraSequences[0].cues[0].name == "RevealBoss",
          "runtime export preserves cinematic cues");
    std::filesystem::remove(runtimePath);

    std::cout << "camera manager tests passed\n";
    return 0;
}
