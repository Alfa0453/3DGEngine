#include "engine/audio/RuntimeAudioSystem.h"

#include "engine/ecs/Components.h"
#include "engine/ecs/Registry.h"
#include "engine/physics/PhysicsWorld.h"

#include <algorithm>
#include <filesystem>
#include <glm/gtc/quaternion.hpp>

namespace engine {
namespace {
bool IsCuePath(const std::string& path) {
    std::string extension = std::filesystem::path(path).extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".3dgaudio";
}
}

RuntimeAudioSystem::RuntimeAudioSystem(AudioEngine& audio) : m_audio(&audio) {}

RuntimeAudioSystem::~RuntimeAudioSystem() {
    Stop();
}

void RuntimeAudioSystem::Update(ecs::Registry& registry, float dt) {
    for (auto it = m_voices.begin(); it != m_voices.end();) {
        if (!registry.Valid(it->first) || !registry.Has<ecs::AudioSource>(it->first)) {
            m_audio->DestroySource(it->second.handle);
            it = m_voices.erase(it);
        } else {
            ++it;
        }
    }

    registry.view<ecs::AudioSource, ecs::Transform>().each(
        [this, dt](ecs::Entity entity, ecs::AudioSource& source, ecs::Transform& transform) {
            auto voice = m_voices.find(entity);
            if (voice != m_voices.end()
                && (voice->second.path != source.path || voice->second.bus != source.bus)) {
                if (voice->second.handle != AudioEngine::InvalidSource)
                    m_audio->DestroySource(voice->second.handle);
                m_voices.erase(voice);
                voice = m_voices.end();
            }

            if (voice == m_voices.end()) {
                if (source.path.empty()) return;
                if (IsCuePath(source.path)) {
                    Voice cueVoice;
                    cueVoice.path = source.path;
                    cueVoice.bus = source.bus;
                    cueVoice.position = transform.position;
                    cueVoice.cue = true;
                    cueVoice.force2D = !source.spatial;
                    voice = m_voices.emplace(entity, std::move(cueVoice)).first;
                    if (source.autoplay)
                        m_audio->PlayCue(source.path, transform.position, !source.spatial);
                    return;
                }
                const bool streamed = !source.spatial && source.loop;
                const AudioEngine::SourceHandle handle =
                    m_audio->CreateSource(source.path, source.spatial, source.loop, streamed, source.bus);
                if (handle == AudioEngine::InvalidSource) return;
                voice = m_voices.emplace(entity,
                    Voice{handle, source.path, source.bus, transform.position, false, false}).first;
                if (source.autoplay) m_audio->PlaySource(handle, true);
            }

            const AudioEngine::SourceHandle handle = voice->second.handle;
            const glm::vec3 previousPosition = voice->second.position;
            voice->second.position = transform.position;
            m_audio->SetSourceSpatial(handle, source.spatial);
            m_audio->SetSourceLooping(handle, source.loop);
            m_audio->SetSourceVolumePitch(handle, source.volume, source.pitch);
            m_audio->SetSourceAttenuation(handle, source.minDistance,
                                          source.maxDistance, source.rolloff);
            if (source.spatial) {
                m_audio->SetSourcePosition(handle, transform.position);
                if (dt > 0.000001f)
                    m_audio->SetSourceVelocity(handle,
                        (transform.position - previousPosition) / dt);
                m_audio->SetSourceDirection(handle,
                    transform.rotation * glm::vec3(0.0f, 0.0f, -1.0f));
                m_audio->SetSourceCone(handle, source.coneInnerAngle,
                    source.coneOuterAngle, source.coneOuterGain);
                m_audio->SetSourceDoppler(handle, source.dopplerFactor);
                m_audio->SetSourceOcclusion(handle, source.occlusion);
            }
            m_audio->SetSourcePriority(handle, source.priority);
        });
}

void RuntimeAudioSystem::Stop() {
    if (!m_audio) return;
    for (const auto& entry : m_voices) m_audio->DestroySource(entry.second.handle);
    m_voices.clear();
}

AudioEngine::SourceHandle RuntimeAudioSystem::SourceFor(ecs::Entity entity) const {
    const auto voice = m_voices.find(entity);
    return voice == m_voices.end() ? AudioEngine::InvalidSource : voice->second.handle;
}

bool RuntimeAudioSystem::Play(ecs::Entity entity, bool restart) {
    const auto voice = m_voices.find(entity);
    if (voice == m_voices.end()) return false;
    if (voice->second.cue)
        return m_audio->PlayCue(voice->second.path, voice->second.position,
                                voice->second.force2D);
    return m_audio->PlaySource(voice->second.handle, restart);
}

bool RuntimeAudioSystem::Pause(ecs::Entity entity) {
    return m_audio->PauseSource(SourceFor(entity));
}

bool RuntimeAudioSystem::Resume(ecs::Entity entity) {
    return m_audio->ResumeSource(SourceFor(entity));
}

bool RuntimeAudioSystem::Stop(ecs::Entity entity) {
    return m_audio->StopSource(SourceFor(entity));
}

bool RuntimeAudioSystem::Seek(ecs::Entity entity, float seconds) {
    return m_audio->SeekSource(SourceFor(entity), seconds);
}

bool RuntimeAudioSystem::IsPlaying(ecs::Entity entity) const {
    return m_audio->IsSourcePlaying(SourceFor(entity));
}

bool RuntimeAudioSystem::IsPaused(ecs::Entity entity) const {
    return m_audio->IsSourcePaused(SourceFor(entity));
}

float RuntimeAudioSystem::CursorSeconds(ecs::Entity entity) const {
    return m_audio->SourceCursorSeconds(SourceFor(entity));
}

void RuntimeAudioSystem::ApplySnapshot(AudioSnapshotPreset preset, float transitionSeconds) {
    m_audio->ApplySnapshot(preset, transitionSeconds);
}

void RuntimeAudioSystem::SetDialogueDucking(bool enabled) {
    m_audio->SetDialogueDucking(enabled);
}

bool RuntimeAudioSystem::PlayCue(const std::string& path, const glm::vec3& position,
                                 bool force2D) {
    return m_audio->PlayCue(path, position, force2D);
}

bool RuntimeAudioSystem::LoadAdaptiveMusic(const std::string& path, std::string* error) {
    return m_audio->LoadAdaptiveMusicAsset(path, error);
}

bool RuntimeAudioSystem::SetMusicState(const std::string& stateName, bool synchronizeToBeat) {
    return m_audio->SetMusicState(stateName, synchronizeToBeat);
}

ecs::Entity RuntimeAudioSystem::FindNamedEntity(ecs::Registry& registry,
                                                 const std::string& name) const {
    ecs::Entity result = ecs::kNull;
    if (name.empty()) return result;
    registry.view<ecs::RuntimeName>().each(
        [&result, &name](ecs::Entity entity, ecs::RuntimeName& runtimeName) {
            if (result == ecs::kNull && runtimeName.value == name) result = entity;
        });
    return result;
}

bool RuntimeAudioSystem::ApplyAction(ecs::Registry& registry, ecs::Entity target, int value) {
    if (target == ecs::kNull || !registry.Valid(target)) return false;
    const ecs::AudioAction action = static_cast<ecs::AudioAction>(value);
    if (action == ecs::AudioAction::None) return false;
    // Make a newly added AudioSource available before dispatching its action.
    Update(registry);
    switch (action) {
    case ecs::AudioAction::Play: return Play(target, false);
    case ecs::AudioAction::Restart: return Play(target, true);
    case ecs::AudioAction::Pause: return Pause(target);
    case ecs::AudioAction::Resume: return Resume(target);
    case ecs::AudioAction::Stop: return Stop(target);
    case ecs::AudioAction::None: break;
    }
    return false;
}

void RuntimeAudioSystem::ProcessCollisionEvents(ecs::Registry& registry,
                                                 const std::vector<CollisionEvent>& events) {
    for (const CollisionEvent& event : events) {
        if (!event.trigger || event.phase == CollisionEvent::Phase::Stay) continue;
        const ecs::Entity candidates[] = {event.a, event.b};
        for (const ecs::Entity trigger : candidates) {
            const ecs::TriggerAudioAction* binding =
                registry.TryGet<ecs::TriggerAudioAction>(trigger);
            if (!binding) continue;
            const ecs::Entity target = FindNamedEntity(registry, binding->targetName);
            const ecs::AudioAction action = event.phase == CollisionEvent::Phase::Enter
                ? binding->onEnter : binding->onExit;
            ApplyAction(registry, target, static_cast<int>(action));
        }
    }
}

bool RuntimeAudioSystem::ProcessAnimationEvent(ecs::Registry& registry,
                                                ecs::Entity emitter,
                                                const std::string& eventName) {
    // Format: Audio.Play, Audio.Restart:SourceName, Audio.Pause:SourceName,
    // Audio.Resume:SourceName, or Audio.Stop:SourceName. With no target suffix,
    // the animated entity's own AudioSource is used.
    constexpr const char* prefix = "Audio.";
    if (eventName.rfind(prefix, 0) != 0) return false;
    const std::size_t separator = eventName.find(':');
    const std::string command = eventName.substr(6, separator == std::string::npos
        ? std::string::npos : separator - 6);
    ecs::Entity target = emitter;
    if (separator != std::string::npos && separator + 1 < eventName.size()) {
        target = FindNamedEntity(registry, eventName.substr(separator + 1));
    }
    ecs::AudioAction action = ecs::AudioAction::None;
    if (command == "Play") action = ecs::AudioAction::Play;
    else if (command == "Restart") action = ecs::AudioAction::Restart;
    else if (command == "Pause") action = ecs::AudioAction::Pause;
    else if (command == "Resume") action = ecs::AudioAction::Resume;
    else if (command == "Stop") action = ecs::AudioAction::Stop;
    return ApplyAction(registry, target, static_cast<int>(action));
}

void RuntimeAudioSystem::UpdateOcclusion(ecs::Registry& registry,
                                         const PhysicsWorld& physics,
                                         const glm::vec3& listenerPosition) {
    for (const auto& entry : m_voices) {
        if (entry.second.cue || entry.second.handle == AudioEngine::InvalidSource) continue;
        const ecs::AudioSource* source = registry.TryGet<ecs::AudioSource>(entry.first);
        if (!source || !source->spatial) continue;
        const glm::vec3 delta = entry.second.position - listenerPosition;
        const float distance = glm::length(delta);
        float occlusion = source->occlusion;
        if (distance > 0.01f) {
            const RaycastHit hit = physics.Raycast(registry,
                Ray{listenerPosition, delta / distance}, distance);
            if (hit.hit && hit.entity != entry.first)
                occlusion = std::max(occlusion, 1.0f);
        }
        m_audio->SetSourceOcclusion(entry.second.handle, occlusion);
    }
}

} // namespace engine
