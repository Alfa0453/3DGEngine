// EditorApp — managed play camera, camera blends/shakes, camera sequences and
// cinematic cues. Split out of EditorApp.cpp to keep the shell file smaller.
// EditorApp.h is unchanged; these are ordinary EditorApp:: member definitions,
// so nothing at the call sites changes.

#include "EditorApp.h"

#include <algorithm>
#include <utility>

void EditorApp::ApplyManagedPlayCamera()
{
    const EditorScene::CameraPreset* preset = m_playCameraOverride
        ? &*m_playCameraOverride
        : m_scene.PrimaryCameraPreset();
    if (!preset || (!m_playCameraOverride && !preset->useInPlay)) return;

    engine::CameraPose pose;
    pose.position = preset->position;
    pose.target = preset->target;
    pose.fov = preset->fov;
    pose.nearPlane = preset->nearPlane;
    pose.farPlane = preset->farPlane;
    engine::CameraBlend::Apply(pose, m_camera);
}

void EditorApp::BeginCameraBlend(const EditorScene::CameraPreset& preset)
{
    engine::CameraPose target;
    target.position = preset.position;
    target.target = preset.target;
    target.fov = preset.fov;
    target.nearPlane = preset.nearPlane;
    target.farPlane = preset.farPlane;
    const auto easing = static_cast<engine::CameraBlend::Easing>(
        std::clamp(preset.blendEasing, 0, 3));
    m_cameraBlend.Start(engine::CameraBlend::FromCamera(m_camera),
                        target, preset.blendDuration, easing);
    engine::CameraBlend::Apply(m_cameraBlend.Current(), m_camera);
}

void EditorApp::UpdateCameraBlend(float dt)
{
    engine::CameraBlend::Apply(m_cameraBlend.Update(dt), m_camera);
}

void EditorApp::RestoreCameraBeforeShake()
{
    if (!m_cameraBeforeShake) return;
    engine::CameraBlend::Apply(*m_cameraBeforeShake, m_camera);
    m_cameraBeforeShake.reset();
}

void EditorApp::UpdateCameraShake(float dt)
{
    if (!m_cameraShake.Active()) return;
    m_cameraBeforeShake = engine::CameraBlend::FromCamera(m_camera);
    engine::CameraShake::Apply(m_cameraShake.Update(dt), m_camera);
}

void EditorApp::StartCameraSequence(const EditorScene::CameraSequence& sequence,
                                    bool lockInput, bool skippable)
{
    RestoreCameraBeforeShake();
    std::vector<engine::CameraSequenceShot> shots;
    shots.reserve(sequence.shots.size());
    for (const EditorScene::CameraSequenceShot& source : sequence.shots) {
        const auto preset = std::find_if(
            m_scene.CameraPresets().begin(), m_scene.CameraPresets().end(),
            [&](const EditorScene::CameraPreset& camera) {
                return camera.name == source.cameraName;
            });
        if (preset == m_scene.CameraPresets().end()) {
            m_log.Warning("Camera sequence '" + sequence.name
                + "' skipped missing camera: " + source.cameraName);
            continue;
        }
        engine::CameraSequenceShot shot;
        shot.pose.position = preset->position;
        shot.pose.target = preset->target;
        shot.pose.fov = preset->fov;
        shot.pose.nearPlane = preset->nearPlane;
        shot.pose.farPlane = preset->farPlane;
        shot.travelDuration = std::max(source.travelDuration, 0.0f);
        shot.holdDuration = std::max(source.holdDuration, 0.0f);
        shot.easing = static_cast<engine::CameraBlend::Easing>(
            std::clamp(source.easing, 0, 3));
        shot.path = static_cast<engine::CameraSequenceShot::Path>(
            std::clamp(source.pathMode, 0, 1));
        shot.eventName = source.eventName;
        shots.push_back(shot);
    }
    if (shots.empty()) {
        m_log.Warning("Camera sequence has no valid shots: " + sequence.name);
        return;
    }
    m_cameraBlend.Cancel();
    m_cameraSequence.Start(
        engine::CameraBlend::FromCamera(m_camera), std::move(shots), sequence.loop);
    m_activeCinematicCues = sequence.cues;
    std::sort(m_activeCinematicCues.begin(), m_activeCinematicCues.end(),
              [](const auto& a, const auto& b) { return a.time < b.time; });
    m_cameraSequencePaused = false;
    m_cameraDirector.SetPlaying(sequence.name, lockInput, skippable);
    m_log.Info("Camera sequence started: " + sequence.name);
}

void EditorApp::UpdateCameraSequence(float dt)
{
    if (m_cameraSequencePaused) {
        engine::CameraBlend::Apply(m_cameraSequence.Current(), m_camera);
        return;
    }
    const bool wasActive = m_cameraSequence.Active();
    const std::string sequenceName = m_cameraDirector.ActiveName();
    const float previousTime = m_cameraSequence.Time();
    engine::CameraBlend::Apply(m_cameraSequence.Update(dt), m_camera);
    const float currentTime = m_cameraSequence.Time();
    ExecuteCinematicCues(previousTime, currentTime, currentTime < previousTime);
    for (const std::string& eventName : m_cameraSequence.TakeEvents()) {
        m_cameraDirector.NotifyTimelineEvent(sequenceName, eventName);
        m_log.Info("Camera sequence event: " + eventName);
    }
    if (wasActive && !m_cameraSequence.Active()) {
        m_cameraDirector.NotifyFinished(sequenceName, false);
        m_activeCinematicCues.clear();
        m_log.Info("Camera sequence finished: " + sequenceName);
    }
}

void EditorApp::SkipActiveCameraSequence()
{
    if (!m_cameraSequence.Active() || !m_cameraDirector.Skippable()) return;
    const std::string sequenceName = m_cameraDirector.ActiveName();
    engine::CameraBlend::Apply(m_cameraSequence.SkipToEnd(), m_camera);
    m_cameraDirector.NotifyFinished(sequenceName, true);
    m_activeCinematicCues.clear();
    m_cameraSequencePaused = false;
    m_log.Info("Camera sequence skipped: " + sequenceName);
}

void EditorApp::ProcessCameraDirectorCommands()
{
    for (const engine::CameraSequenceCommand& command : m_cameraDirector.TakeCommands()) {
        if (command.type == engine::CameraSequenceCommand::Type::Stop) {
            m_cameraSequence.Stop();
            m_cameraDirector.SetStopped();
            m_activeCinematicCues.clear();
            m_cameraSequencePaused = false;
            m_log.Info("Camera sequence stopped");
            continue;
        }
        if (command.type == engine::CameraSequenceCommand::Type::Skip) {
            SkipActiveCameraSequence();
            continue;
        }
        const auto sequence = std::find_if(
            m_scene.CameraSequences().begin(), m_scene.CameraSequences().end(),
            [&](const EditorScene::CameraSequence& candidate) {
                return candidate.name == command.name;
            });
        if (sequence == m_scene.CameraSequences().end()) {
            m_log.Warning("Camera sequence was not found: " + command.name);
            continue;
        }
        StartCameraSequence(*sequence, command.lockInput, command.skippable);
    }
}

void EditorApp::ExecuteCinematicCues(float previousTime, float currentTime, bool wrapped)
{
    for (const EditorScene::CinematicCue& cue : m_activeCinematicCues) {
        const bool crossed = wrapped
            ? (cue.time > previousTime || cue.time <= currentTime)
            : ((cue.time > previousTime && cue.time <= currentTime)
               || (previousTime == 0.0f && cue.time == 0.0f && currentTime > 0.0f));
        if (crossed) ExecuteCinematicCue(cue);
    }
}

void EditorApp::ExecuteCinematicCue(const EditorScene::CinematicCue& cue)
{
    switch (cue.type) {
    case EditorScene::CinematicCueType::Event:
        m_cameraDirector.NotifyTimelineEvent(
            m_cameraDirector.ActiveName(), cue.name);
        if (!cue.name.empty()) m_log.Info("Cinematic event: " + cue.name);
        break;
    case EditorScene::CinematicCueType::Audio:
        if (!cue.assetPath.empty()) {
            m_audio.Play(cue.assetPath, 1.0f, std::max(cue.volume, 0.0f),
                         engine::AudioBus::SFX);
            m_log.Info("Cinematic audio: " + cue.assetPath);
        }
        break;
    case EditorScene::CinematicCueType::Animation: {
        if (!m_playRegistry) {
            m_log.Info("Animation cues execute in Play mode.");
            break;
        }
        engine::ecs::Entity target = engine::ecs::kNull;
        for (const auto& pair : m_playEntityNames) {
            if (pair.second == cue.targetObject) {
                target = pair.first;
                break;
            }
        }
        engine::AnimatedModel* animated = target == engine::ecs::kNull
            ? nullptr : m_playRegistry->TryGet<engine::AnimatedModel>(target);
        if (!animated || !animated->model) {
            m_log.Warning("Cinematic animation target is unavailable: " + cue.targetObject);
            break;
        }
        int clip = -1;
        const auto& animations = animated->model->Animations();
        for (std::size_t i = 0; i < animations.size(); ++i) {
            if (animations[i].name == cue.animationClip) {
                clip = static_cast<int>(i);
                break;
            }
        }
        if (clip < 0) {
            m_log.Warning("Cinematic animation clip was not found: " + cue.animationClip);
            break;
        }
        animated->PlayAction(clip);
        m_log.Info("Cinematic animation: " + cue.targetObject + " / " + cue.animationClip);
        break;
    }
    }
}
