#include "engine/gameplay/Script.h"
#include "engine/gameplay/LuaScript.h"
#include "engine/graphics/CameraShake.h"
#include "engine/gameplay/CameraDirector.h"
#include "engine/gameplay/GameMode.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/animation/Animator.h"
#include "engine/audio/RuntimeAudioSystem.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/RuntimeParticleSystem.h"
#include "engine/math/Spline.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <excpt.h>   // __try/__except for isolating script hardware faults
#endif

namespace engine {
namespace {
std::string g_scriptSceneLoadRequest;
std::vector<ScriptLevelStreamRequest> g_scriptLevelStreamRequests;
std::vector<ScriptSaveGameRequest> g_scriptSaveGameRequests;
constexpr const char* kSaveDataPath = "3dg_savegame.dat";

std::unordered_map<std::string, std::string> ReadSaveValues() {
    std::unordered_map<std::string, std::string> values;
    std::ifstream input(kSaveDataPath);
    std::string key, value;
    while (input >> std::quoted(key) >> std::quoted(value))
        values[std::move(key)] = std::move(value);
    return values;
}
} // namespace

ecs::Transform* Script::Transform() {
    return TryGet<ecs::Transform>();
}

const ecs::Transform* Script::Transform() const {
    return TryGet<ecs::Transform>();
}

ecs::Entity Script::FindObject(const std::string& name) const {
    if (!m_context.registry || name.empty()) {
        return ecs::kNull;
    }

    ecs::Entity found = ecs::kNull;
    m_context.registry->view<ecs::RuntimeName>().each(
        [&found, &name](ecs::Entity entity, ecs::RuntimeName& runtimeName) {
            if (found == ecs::kNull && runtimeName.value == name) {
                found = entity;
            }
        });
    return found;
}

ecs::Transform* Script::FindTransform(const std::string& name) {
    const ecs::Entity entity = FindObject(name);
    return entity == ecs::kNull ? nullptr : TryGet<ecs::Transform>(entity);
}

bool Script::SocketTransform(const std::string& name, glm::mat4* world) const {
    const ecs::Transform* character = Transform();
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    return character && animated
        && animated->SocketWorldTransform(*character, name, world);
}

bool Script::SocketPosition(const std::string& name, glm::vec3* position) const {
    if (!position) return false;
    glm::mat4 world(1.0f);
    if (!SocketTransform(name, &world)) return false;
    *position = glm::vec3(world[3]);
    return true;
}

RaycastHit Script::TraceLine(const glm::vec3& start, const glm::vec3& end,
                             std::uint32_t layerMask) const {
    RaycastHit result;
    if (!m_context.physics || !m_context.registry) return result;
    const glm::vec3 delta = end - start;
    const float distance = glm::length(delta);
    if (distance <= 1.0e-6f) return result;
    return m_context.physics->Raycast(
        *m_context.registry, Ray{start, delta}, distance, layerMask, m_context.entity);
}

RaycastHit Script::TraceSphere(const glm::vec3& start, const glm::vec3& end, float radius,
                               std::uint32_t layerMask) const {
    RaycastHit result;
    if (!m_context.physics || !m_context.registry) return result;
    return m_context.physics->SphereCast(
        *m_context.registry, start, end, std::max(radius, 0.0f),
        m_context.entity, layerMask);
}

std::vector<ecs::Entity> Script::TraceOverlapSphere(
    const glm::vec3& center, float radius, std::uint32_t layerMask) const {
    if (!m_context.physics || !m_context.registry) return {};
    return m_context.physics->OverlapSphere(
        *m_context.registry, center, std::max(radius, 0.0f), layerMask);
}

void Script::DestroySelf() {
    Destroy(m_context.entity);
}

void Script::Destroy(ecs::Entity entity) {
    if (!m_context.registry || entity == ecs::kNull) {
        return;
    }
    if (m_context.destroyQueue) {
        m_context.destroyQueue->push_back(entity);
    } else if (m_context.registry->Valid(entity)) {
        m_context.registry->Destroy(entity);
    }
}

bool Script::IsKeyDown(int key) const {
    return m_context.input
        && m_context.input->enabled
        && m_context.input->keysDown.find(key) != m_context.input->keysDown.end();
}

bool Script::WasKeyPressed(int key) const {
    return m_context.input
        && m_context.input->enabled
        && m_context.input->keysPressed.find(key) != m_context.input->keysPressed.end();
}

bool Script::IsMouseButtonDown(int button) const {
    return m_context.input
        && m_context.input->enabled
        && m_context.input->mouseButtonsDown.find(button) != m_context.input->mouseButtonsDown.end();
}

bool Script::WasMouseButtonPressed(int button) const {
    return m_context.input
        && m_context.input->enabled
        && m_context.input->mouseButtonsPressed.find(button) != m_context.input->mouseButtonsPressed.end();
}

float Script::MouseDeltaX() const {
    return m_context.input && m_context.input->enabled ? m_context.input->mouseDeltaX : 0.0f;
}

float Script::MouseDeltaY() const {
    return m_context.input && m_context.input->enabled ? m_context.input->mouseDeltaY : 0.0f;
}

bool Script::IsTriggerTouching(ecs::Entity entity) const {
    if (!m_context.input || !m_context.input->physicsEvents || entity == ecs::kNull) {
        return false;
    }
    for (const CollisionEvent& event : *m_context.input->physicsEvents) {
        if (!event.trigger || event.phase == CollisionEvent::Phase::Exit) {
            continue;
        }
        if ((event.a == m_context.entity && event.b == entity)
            || (event.b == m_context.entity && event.a == entity)) {
            return true;
        }
    }
    return false;
}

bool Script::WasTriggerEntered(ecs::Entity entity) const {
    if (!m_context.input || !m_context.input->physicsEvents || entity == ecs::kNull) {
        return false;
    }
    for (const CollisionEvent& event : *m_context.input->physicsEvents) {
        if (event.trigger
            && event.phase == CollisionEvent::Phase::Enter
            && ((event.a == m_context.entity && event.b == entity)
                || (event.b == m_context.entity && event.a == entity))) {
            return true;
        }
    }
    return false;
}

bool Script::WasTriggerExited(ecs::Entity entity) const {
    if (!m_context.input || !m_context.input->physicsEvents || entity == ecs::kNull) {
        return false;
    }
    for (const CollisionEvent& event : *m_context.input->physicsEvents) {
        if (event.trigger
            && event.phase == CollisionEvent::Phase::Exit
            && ((event.a == m_context.entity && event.b == entity)
                || (event.b == m_context.entity && event.a == entity))) {
            return true;
        }
    }
    return false;
}

bool Script::WasAnimationEvent(const std::string& name) const {
    return WasAnimationEvent(m_context.entity, name);
}

bool Script::WasAnimationEvent(ecs::Entity entity, const std::string& name) const {
    if (!m_context.input || !m_context.input->animationEvents || entity == ecs::kNull || name.empty()) {
        return false;
    }
    for (const ScriptAnimationEvent& event : *m_context.input->animationEvents) {
        if (event.entity == entity && event.name == name) {
            return true;
        }
    }
    return false;
}

bool Script::PlayAnimationAction(int clipIndex, float fadeIn, float fadeOut, float speed) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model || clipIndex < 0) {
        return false;
    }
    if (clipIndex >= static_cast<int>(animated->model->AnimationCount())) {
        return false;
    }

    std::vector<AnimEvent> actionEvents;
    for (const AnimEvent& event : animated->events) {
        if (event.clip < 0 || event.clip == clipIndex) actionEvents.push_back(event);
    }
    animated->PlayAction(clipIndex,
        {},
        std::move(actionEvents),
        std::max(fadeIn, 0.0f),
        std::max(fadeOut, 0.0f),
        std::max(speed, 0.0f));
    return true;
}

bool Script::PlayAnimationAction(const std::string& clipName, float fadeIn, float fadeOut, float speed) {
    if (clipName.empty()) {
        return false;
    }

    const AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model) {
        return false;
    }

    const auto& animations = animated->model->Animations();
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == clipName) {
            return PlayAnimationAction(static_cast<int>(i), fadeIn, fadeOut, speed);
        }
    }
    return false;
}

bool Script::PlayMaskedAnimationAction(int clipIndex,
                                       const std::string& rootBone,
                                       float fadeIn,
                                       float fadeOut,
                                       float speed) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model || clipIndex < 0 || rootBone.empty()) {
        return false;
    }
    if (clipIndex >= static_cast<int>(animated->model->AnimationCount())) {
        return false;
    }

    const Skeleton& skeleton = animated->model->GetSkeleton();
    if (skeleton.Find(rootBone) < 0) {
        return false;
    }

    std::vector<AnimEvent> actionEvents;
    for (const AnimEvent& event : animated->events) {
        if (event.clip < 0 || event.clip == clipIndex) actionEvents.push_back(event);
    }
    animated->PlayAction(clipIndex,
        Animator::BuildMask(skeleton, rootBone),
        std::move(actionEvents),
        std::max(fadeIn, 0.0f),
        std::max(fadeOut, 0.0f),
        std::max(speed, 0.0f));
    return true;
}

bool Script::PlayMaskedAnimationAction(const std::string& clipName,
                                       const std::string& rootBone,
                                       float fadeIn,
                                       float fadeOut,
                                       float speed) {
    if (clipName.empty()) {
        return false;
    }

    const AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model) {
        return false;
    }

    const auto& animations = animated->model->Animations();
    for (std::size_t i = 0; i < animations.size(); ++i) {
        if (animations[i].name == clipName) {
            return PlayMaskedAnimationAction(static_cast<int>(i), rootBone, fadeIn, fadeOut, speed);
        }
    }
    return false;
}

bool Script::PlayAnimationProfile(const std::string& profileName) {
    if (profileName.empty()) {
        return false;
    }

    const ecs::SkinnedModelAsset* asset = TryGet<ecs::SkinnedModelAsset>();
    if (!asset) {
        return false;
    }

    for (const ecs::SkinnedModelAsset::ActionProfile& profile : asset->actionProfiles) {
        if (profile.name != profileName) {
            continue;
        }
        if (!profile.maskRootBone.empty()) {
            if (!profile.clipName.empty()) {
                if (PlayMaskedAnimationAction(profile.clipName,
                    profile.maskRootBone,
                    profile.fadeIn,
                    profile.fadeOut,
                    profile.speed)) {
                    return true;
                }
                // A renamed or missing mask bone should not make an otherwise valid
                // action silently fail. Fall back to full-body playback.
                return PlayAnimationAction(profile.clipName,
                    profile.fadeIn,
                    profile.fadeOut,
                    profile.speed);
            }
            if (PlayMaskedAnimationAction(profile.clipIndex,
                profile.maskRootBone,
                profile.fadeIn,
                profile.fadeOut,
                profile.speed)) {
                return true;
            }
            return PlayAnimationAction(profile.clipIndex,
                profile.fadeIn,
                profile.fadeOut,
                profile.speed);
        }
        if (!profile.clipName.empty()) {
            return PlayAnimationAction(profile.clipName,
                profile.fadeIn,
                profile.fadeOut,
                profile.speed);
        }
        return PlayAnimationAction(profile.clipIndex,
            profile.fadeIn,
            profile.fadeOut,
            profile.speed);
    }
    return false;
}

bool Script::PlayActionClip(const std::string& actionName) {
    return PlayAnimationProfile(actionName);
}

bool Script::SetAnimationParameter(const std::string& name, float value) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || name.empty()) {
        return false;
    }
    animated->controller.SetParameter(name, value);
    return true;
}

bool Script::SetAnimationBool(const std::string& name, bool value) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || name.empty()) {
        return false;
    }
    animated->controller.SetBoolParameter(name, value);
    return true;
}

bool Script::SetAnimationTrigger(const std::string& name) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || name.empty()) {
        return false;
    }
    animated->controller.SetTriggerParameter(name);
    return true;
}

float Script::GetAnimationParameter(const std::string& name, float fallback) const {
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    return (!animated || name.empty()) ? fallback : animated->controller.Parameter(name, fallback);
}

bool Script::GetAnimationBool(const std::string& name, bool fallback) const {
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    return (!animated || name.empty()) ? fallback : animated->controller.BoolParameter(name, fallback);
}

bool Script::IsAnimationActionPlaying() const {
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    return animated && animated->ActionPlaying();
}

ecs::Entity Script::SpawnEmpty(const std::string& name, const glm::vec3& position) {
    if (!m_context.registry) return ecs::kNull;
    const ecs::Entity entity = m_context.registry->Create();
    m_context.registry->Add<ecs::Transform>(entity).position = position;
    m_context.registry->Add<ecs::RuntimeName>(
        entity, ecs::RuntimeName{name.empty() ? "SpawnedObject" : name});
    return entity;
}

ecs::Entity Script::SpawnFromObject(
    const std::string& prototypeName, const glm::vec3& position) {
    if (!m_context.registry) return ecs::kNull;
    const ecs::Entity prototype = FindObject(prototypeName);
    if (prototype == ecs::kNull) return ecs::kNull;
    const ecs::Entity entity = m_context.registry->Clone(prototype);
    if (ecs::Transform* transform = m_context.registry->TryGet<ecs::Transform>(entity))
        transform->position = position;
    else
        m_context.registry->Add<ecs::Transform>(entity).position = position;
    if (ecs::RuntimeName* name = m_context.registry->TryGet<ecs::RuntimeName>(entity))
        name->value = prototypeName + "_Instance";
    return entity;
}

void Script::RequestSceneLoad(const std::string& runtimeScenePath) {
    if (runtimeScenePath.empty()) return;
    // Prefer the host-owned sink (DLL-safe); fall back to the module-local global.
    if (m_context.sceneLoadRequest) *m_context.sceneLoadRequest = runtimeScenePath;
    else g_scriptSceneLoadRequest = runtimeScenePath;
}

int Script::SplinePointCount(ecs::Entity spline) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    return component ? static_cast<int>(component->points.size()) : 0;
}

bool Script::IsSplineClosed(ecs::Entity spline) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    return component && component->closed;
}

bool Script::SetSplineClosed(ecs::Entity spline, bool closed) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component) return false;
    component->closed = closed;
    ++component->revision;
    return true;
}

glm::vec3 Script::GetSplinePoint(ecs::Entity spline, int index,
                                 const glm::vec3& fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    return component && index >= 0 && index < static_cast<int>(component->points.size())
        ? component->points[static_cast<std::size_t>(index)] : fallback;
}

bool Script::SetSplinePoint(ecs::Entity spline, int index, const glm::vec3& point) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || index < 0 || index >= static_cast<int>(component->points.size())) return false;
    component->points[static_cast<std::size_t>(index)] = point;
    ++component->revision;
    return true;
}

glm::vec3 Script::GetSplinePointRotation(ecs::Entity spline, int index,
                                         const glm::vec3& fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    return component && index >= 0 && index < static_cast<int>(component->rotations.size())
        ? component->rotations[static_cast<std::size_t>(index)] : fallback;
}

bool Script::SetSplinePointRotation(ecs::Entity spline, int index,
                                    const glm::vec3& degrees) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || index < 0 || index >= static_cast<int>(component->points.size())) return false;
    component->rotations.resize(component->points.size(), glm::vec3(0.0f));
    component->rotations[static_cast<std::size_t>(index)] = degrees;
    ++component->revision;
    return true;
}

int Script::AddSplinePoint(ecs::Entity spline, const glm::vec3& point,
                           const glm::vec3& rotationDegrees) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component) return -1;
    component->points.push_back(point);
    component->rotations.resize(component->points.size() - 1, glm::vec3(0.0f));
    component->rotations.push_back(rotationDegrees);
    ++component->revision;
    return static_cast<int>(component->points.size()) - 1;
}

bool Script::InsertSplinePoint(ecs::Entity spline, int index, const glm::vec3& point,
                               const glm::vec3& rotationDegrees) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || index < 0 || index > static_cast<int>(component->points.size())) return false;
    component->rotations.resize(component->points.size(), glm::vec3(0.0f));
    component->points.insert(component->points.begin() + index, point);
    component->rotations.insert(component->rotations.begin() + index, rotationDegrees);
    ++component->revision;
    return true;
}

bool Script::RemoveSplinePoint(ecs::Entity spline, int index) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || index < 0 || index >= static_cast<int>(component->points.size())) return false;
    component->points.erase(component->points.begin() + index);
    if (index < static_cast<int>(component->rotations.size()))
        component->rotations.erase(component->rotations.begin() + index);
    component->rotations.resize(component->points.size(), glm::vec3(0.0f));
    ++component->revision;
    return true;
}

bool Script::TranslateSpline(ecs::Entity spline, const glm::vec3& delta) {
    ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component) return false;
    for (glm::vec3& point : component->points) point += delta;
    if (ecs::Transform* transform = TryGet<ecs::Transform>(spline)) transform->position += delta;
    ++component->revision;
    return true;
}

glm::vec3 Script::SplinePositionAt(ecs::Entity spline, float normalizedDistance,
                                   const glm::vec3& fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || component->points.size() < 2) return fallback;
    const Spline curve(component->points, component->closed);
    return curve.PositionAtDistance(curve.Length() * std::clamp(normalizedDistance, 0.0f, 1.0f));
}

glm::vec3 Script::SplineTangentAt(ecs::Entity spline, float normalizedDistance,
                                  const glm::vec3& fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || component->points.size() < 2) return fallback;
    const Spline curve(component->points, component->closed);
    return curve.TangentAtDistance(curve.Length() * std::clamp(normalizedDistance, 0.0f, 1.0f));
}

void Script::RequestLevelLoad(const std::string& level) {
    if (!level.empty())
        g_scriptLevelStreamRequests.push_back({level, true});
}

void Script::RequestLevelUnload(const std::string& level) {
    if (!level.empty())
        g_scriptLevelStreamRequests.push_back({level, false});
}

bool Script::SaveValue(const std::string& key, const std::string& value) {
    if (key.empty()) return false;
    auto values = ReadSaveValues();
    values[key] = value;
    std::ofstream output(kSaveDataPath, std::ios::trunc);
    if (!output) return false;
    for (const auto& pair : values)
        output << std::quoted(pair.first) << ' ' << std::quoted(pair.second) << '\n';
    return static_cast<bool>(output);
}

std::string Script::LoadValue(
    const std::string& key, const std::string& fallback) const {
    const auto values = ReadSaveValues();
    const auto found = values.find(key);
    return found == values.end() ? fallback : found->second;
}

bool Script::SaveCheckpoint(const std::string& name, const glm::vec3& position) {
    if (name.empty()) return false;
    return SaveValue(
        "checkpoint." + name,
        std::to_string(position.x) + " " +
        std::to_string(position.y) + " " +
        std::to_string(position.z));
}

bool Script::LoadCheckpoint(const std::string& name, glm::vec3* position) const {
    if (name.empty() || !position) return false;
    const std::string value = LoadValue("checkpoint." + name);
    if (value.empty()) return false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::istringstream input(value);
    if (!(input >> x >> y >> z)) return false;
    *position = glm::vec3(x, y, z);
    return true;
}

bool Script::SaveInt(const std::string& key, int value) {
    return SaveValue(key, std::to_string(value));
}
bool Script::SaveFloat(const std::string& key, float value) {
    return SaveValue(key, std::to_string(value));
}
bool Script::SaveBool(const std::string& key, bool value) {
    return SaveValue(key, value ? "1" : "0");
}
bool Script::SaveVec3(const std::string& key, const glm::vec3& value) {
    return SaveValue(key, std::to_string(value.x) + " " + std::to_string(value.y)
                        + " " + std::to_string(value.z));
}
int Script::GetSavedInt(const std::string& key, int fallback) const {
    const std::string value = LoadValue(key);
    if (value.empty()) return fallback;
    try { return std::stoi(value); } catch (...) { return fallback; }
}
float Script::GetSavedFloat(const std::string& key, float fallback) const {
    const std::string value = LoadValue(key);
    if (value.empty()) return fallback;
    try { return std::stof(value); } catch (...) { return fallback; }
}
bool Script::GetSavedBool(const std::string& key, bool fallback) const {
    const std::string value = LoadValue(key);
    if (value.empty()) return fallback;
    return value == "1" || value == "true";
}
glm::vec3 Script::GetSavedVec3(const std::string& key, const glm::vec3& fallback) const {
    const std::string value = LoadValue(key);
    if (value.empty()) return fallback;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    std::istringstream input(value);
    if (!(input >> x >> y >> z)) return fallback;
    return glm::vec3(x, y, z);
}

void Script::SaveGameToSlot(int slot, const std::string& displayName) {
    ScriptSaveGameRequest request;
    request.slot = slot;
    request.load = false;
    request.displayName = displayName;
    g_scriptSaveGameRequests.push_back(std::move(request));
}
void Script::LoadGameFromSlot(int slot) {
    ScriptSaveGameRequest request;
    request.slot = slot;
    request.load = true;
    g_scriptSaveGameRequests.push_back(std::move(request));
}

int Script::SetTimer(float seconds, std::function<void()> callback, bool repeat) {
    if (!callback) return 0;
    Timer timer;
    timer.id = m_nextTimerId++;
    timer.remaining = std::max(seconds, 0.0f);
    timer.interval = std::max(seconds, 0.0001f);
    timer.repeat = repeat;
    timer.callback = std::move(callback);
    m_timers.push_back(std::move(timer));
    return m_timers.back().id;
}

bool Script::BindTimerFunction(const std::string& functionName,
                               std::function<void()> function) {
    if (functionName.empty() || !function) return false;
    m_timerFunctions[functionName] = std::move(function);
    return true;
}

bool Script::UnbindTimerFunction(const std::string& functionName) {
    if (functionName.empty()) return false;
    ClearTimerByFunctionName(functionName);
    return m_timerFunctions.erase(functionName) != 0;
}

bool Script::HasTimerFunction(const std::string& functionName) const {
    const auto it = m_timerFunctions.find(functionName);
    return it != m_timerFunctions.end() && static_cast<bool>(it->second);
}

bool Script::InvokeTimerFunction(const std::string& functionName) {
    const auto it = m_timerFunctions.find(functionName);
    if (it == m_timerFunctions.end() || !it->second) return false;
    it->second();
    return true;
}

int Script::SetTimerByFunctionName(const std::string& functionName,
                                   float seconds, bool repeat) {
    if (!HasTimerFunction(functionName)) return 0;
    Timer timer;
    timer.id = m_nextTimerId++;
    timer.remaining = std::max(seconds, 0.0f);
    timer.interval = std::max(seconds, 0.0001f);
    timer.repeat = repeat;
    timer.functionName = functionName;
    timer.callback = [this, functionName] {
        InvokeTimerFunction(functionName);
    };
    m_timers.push_back(std::move(timer));
    return m_timers.back().id;
}

void Script::ClearTimer(int timerId) {
    for (Timer& timer : m_timers)
        if (timer.id == timerId) timer.cancelled = true;
}

int Script::ClearTimerByFunctionName(const std::string& functionName) {
    int cleared = 0;
    for (Timer& timer : m_timers) {
        if (!timer.cancelled && timer.functionName == functionName) {
            timer.cancelled = true;
            ++cleared;
        }
    }
    return cleared;
}

bool Script::IsTimerActive(int timerId) const {
    return std::any_of(m_timers.begin(), m_timers.end(),
        [timerId](const Timer& timer) {
            return !timer.cancelled && timer.id == timerId;
        });
}

bool Script::IsTimerActive(const std::string& functionName) const {
    return std::any_of(m_timers.begin(), m_timers.end(),
        [&functionName](const Timer& timer) {
            return !timer.cancelled && timer.functionName == functionName;
        });
}

void Script::SetGlobalTimeDilation(float dilation) {
    if (m_context.gameMode)
        m_context.gameMode->SetGlobalTimeDilation(dilation);
}

float Script::GlobalTimeDilation() const {
    return m_context.gameMode
        ? m_context.gameMode->GlobalTimeDilation() : 1.0f;
}

float Script::EffectiveTimeDilation() const {
    return m_context.gameMode
        ? m_context.gameMode->EffectiveTimeDilation() : 1.0f;
}

void Script::HitStop(float unscaledSeconds, float dilation) {
    if (m_context.gameMode)
        m_context.gameMode->HitStop(unscaledSeconds, dilation);
}

bool Script::IsHitStopActive() const {
    return m_context.gameMode && m_context.gameMode->HitStopActive();
}

void Script::TickTimers(float dt) {
    m_timerCallbacks.clear();
    if (m_timerCallbacks.capacity() < m_timers.size())
        m_timerCallbacks.reserve(m_timers.size());
    for (Timer& timer : m_timers) {
        if (timer.cancelled) continue;
        timer.remaining -= std::max(dt, 0.0f);
        if (timer.remaining > 0.0f) continue;
        m_timerCallbacks.push_back(timer.callback);
        if (timer.repeat) {
            do timer.remaining += timer.interval;
            while (timer.remaining <= 0.0f);
        } else {
            timer.cancelled = true;
        }
    }
    m_timers.erase(
        std::remove_if(m_timers.begin(), m_timers.end(),
                       [](const Timer& timer) { return timer.cancelled; }),
        m_timers.end());
    for (auto& callback : m_timerCallbacks) callback();

    // Index-based over the initial count: an action may start a new sequence (push_back),
    // which could reallocate and invalidate a range-for iterator. The sequence objects
    // themselves are heap-stable via unique_ptr, so ticking through indices is safe.
    const std::size_t sequenceCount = m_sequences.size();
    for (std::size_t i = 0; i < sequenceCount; ++i) {
        if (m_sequences[i]) m_sequences[i]->Tick(dt);
    }
    m_sequences.erase(
        std::remove_if(m_sequences.begin(), m_sequences.end(),
            [](const std::unique_ptr<ScriptSequence>& s) { return !s || s->Done(); }),
        m_sequences.end());
}

ScriptSequence& Script::Sequence() {
    m_sequences.push_back(std::make_unique<ScriptSequence>());
    return *m_sequences.back();
}

bool Script::IsAnimationMovementLocked() const {
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    return animated && animated->BlocksMovement();
}

bool Script::PlayAudio(bool restart) { return PlayAudio(Self(), restart); }
bool Script::PlayAudio(ecs::Entity entity, bool restart) {
    if (!m_context.audio || !m_context.registry || entity == ecs::kNull) return false;
    // Ensure sources added by this or another script are observed immediately.
    m_context.audio->Update(*m_context.registry);
    return m_context.audio->Play(entity, restart);
}
bool Script::PauseAudio() { return PauseAudio(Self()); }
bool Script::PauseAudio(ecs::Entity entity) {
    return m_context.audio && m_context.audio->Pause(entity);
}
bool Script::ResumeAudio() { return ResumeAudio(Self()); }
bool Script::ResumeAudio(ecs::Entity entity) {
    return m_context.audio && m_context.audio->Resume(entity);
}
bool Script::StopAudio() { return StopAudio(Self()); }
bool Script::StopAudio(ecs::Entity entity) {
    return m_context.audio && m_context.audio->Stop(entity);
}
bool Script::SeekAudio(float seconds) { return SeekAudio(Self(), seconds); }
bool Script::SeekAudio(ecs::Entity entity, float seconds) {
    return m_context.audio && m_context.audio->Seek(entity, std::max(seconds, 0.0f));
}
bool Script::IsAudioPlaying() const { return IsAudioPlaying(Self()); }
bool Script::IsAudioPlaying(ecs::Entity entity) const {
    if (!m_context.audio) return false;
    return m_context.audio->IsPlaying(entity);
}
bool Script::IsAudioPaused() const { return IsAudioPaused(Self()); }
bool Script::IsAudioPaused(ecs::Entity entity) const {
    return m_context.audio && m_context.audio->IsPaused(entity);
}
float Script::AudioCursorSeconds() const { return AudioCursorSeconds(Self()); }
float Script::AudioCursorSeconds(ecs::Entity entity) const {
    return m_context.audio ? m_context.audio->CursorSeconds(entity) : 0.0f;
}
bool Script::SetAudioVolume(float volume) { return SetAudioVolume(Self(), volume); }
bool Script::SetAudioVolume(ecs::Entity entity, float volume) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->volume = std::max(volume, 0.0f);
    return true;
}
bool Script::SetAudioPitch(float pitch) { return SetAudioPitch(Self(), pitch); }
bool Script::SetAudioPitch(ecs::Entity entity, float pitch) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->pitch = std::max(pitch, 0.01f);
    return true;
}
bool Script::SetAudioLooping(bool looping) { return SetAudioLooping(Self(), looping); }
bool Script::SetAudioLooping(ecs::Entity entity, bool looping) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->loop = looping;
    return true;
}
bool Script::SetAudioSpatial(bool spatial) { return SetAudioSpatial(Self(), spatial); }
bool Script::SetAudioSpatial(ecs::Entity entity, bool spatial) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->spatial = spatial;
    return true;
}
bool Script::SetAudioBus(AudioBus bus) { return SetAudioBus(Self(), bus); }
bool Script::SetAudioBus(ecs::Entity entity, AudioBus bus) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source || bus == AudioBus::Count) return false;
    source->bus = bus;
    return true;
}
bool Script::ApplyAudioSnapshot(AudioSnapshotPreset preset, float transitionSeconds) {
    if (!m_context.audio) return false;
    m_context.audio->ApplySnapshot(preset, std::max(transitionSeconds, 0.0f));
    return true;
}
bool Script::SetDialogueDucking(bool enabled) {
    if (!m_context.audio) return false;
    m_context.audio->SetDialogueDucking(enabled);
    return true;
}

bool Script::PlayAudioCue(const std::string& path, bool spatial) {
    if (!m_context.audio) return false;
    const ecs::Transform* transform = Transform();
    return m_context.audio->PlayCue(path,
        transform ? transform->position : glm::vec3(0.0f), !spatial);
}

bool Script::LoadAdaptiveMusic(const std::string& path) {
    return m_context.audio && m_context.audio->LoadAdaptiveMusic(path);
}

bool Script::SetMusicState(const std::string& stateName, bool synchronizeToBeat) {
    return m_context.audio
        && m_context.audio->SetMusicState(stateName, synchronizeToBeat);
}
bool Script::SetAudioAttenuation(float minDistance, float maxDistance, float rolloff) {
    return SetAudioAttenuation(Self(), minDistance, maxDistance, rolloff);
}
bool Script::SetAudioAttenuation(ecs::Entity entity, float minDistance,
                                 float maxDistance, float rolloff) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->minDistance = std::max(minDistance, 0.01f);
    source->maxDistance = std::max(maxDistance, source->minDistance);
    source->rolloff = std::max(rolloff, 0.0f);
    return true;
}
bool Script::SetAudioDoppler(float factor) { return SetAudioDoppler(Self(), factor); }
bool Script::SetAudioDoppler(ecs::Entity entity, float factor) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->dopplerFactor = std::max(factor, 0.0f);
    return true;
}
bool Script::SetAudioCone(float innerDegrees, float outerDegrees, float outerGain) {
    return SetAudioCone(Self(), innerDegrees, outerDegrees, outerGain);
}
bool Script::SetAudioCone(ecs::Entity entity, float innerDegrees,
                          float outerDegrees, float outerGain) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->coneInnerAngle = std::clamp(innerDegrees, 0.0f, 360.0f);
    source->coneOuterAngle = std::clamp(outerDegrees, source->coneInnerAngle, 360.0f);
    source->coneOuterGain = std::clamp(outerGain, 0.0f, 1.0f);
    return true;
}
bool Script::SetAudioOcclusion(float amount) { return SetAudioOcclusion(Self(), amount); }
bool Script::SetAudioOcclusion(ecs::Entity entity, float amount) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->occlusion = std::clamp(amount, 0.0f, 1.0f);
    return true;
}
bool Script::SetAudioPriority(int priority) { return SetAudioPriority(Self(), priority); }
bool Script::SetAudioPriority(ecs::Entity entity, int priority) {
    ecs::AudioSource* source = TryGet<ecs::AudioSource>(entity);
    if (!source) return false;
    source->priority = std::clamp(priority, 0, 100);
    return true;
}

bool Script::PlayParticles(bool restart) { return PlayParticles(Self(), restart); }
bool Script::PlayParticles(ecs::Entity entity, bool restart) {
    return m_context.registry && PlayParticleSystem(*m_context.registry, entity, restart);
}
bool Script::StopParticles(bool clear) { return StopParticles(Self(), clear); }
bool Script::StopParticles(ecs::Entity entity, bool clear) {
    return m_context.registry && StopParticleSystem(*m_context.registry, entity, clear);
}
bool Script::RestartParticles() { return RestartParticles(Self()); }
bool Script::RestartParticles(ecs::Entity entity) {
    return m_context.registry && PlayParticleSystem(*m_context.registry, entity, true);
}
bool Script::BurstParticles(int count) { return BurstParticles(Self(), count); }
bool Script::BurstParticles(ecs::Entity entity, int count) {
    return m_context.registry && BurstParticleSystem(*m_context.registry, entity, count);
}
bool Script::ClearParticles() { return ClearParticles(Self()); }
bool Script::ClearParticles(ecs::Entity entity) {
    return m_context.registry && ClearParticleSystem(*m_context.registry, entity);
}
bool Script::SetParticlesEnabled(bool enabled) { return SetParticlesEnabled(Self(), enabled); }
bool Script::SetParticlesEnabled(ecs::Entity entity, bool enabled) {
    return m_context.registry && SetParticleSystemEnabled(*m_context.registry, entity, enabled);
}
bool Script::SetParticleRate(float particlesPerSecond) {
    return SetParticleRate(Self(), particlesPerSecond);
}
bool Script::SetParticleRate(ecs::Entity entity, float particlesPerSecond) {
    return m_context.registry && SetParticleEmissionRate(*m_context.registry, entity, particlesPerSecond);
}
bool Script::SetParticleSpeed(float simulationSpeed) {
    return SetParticleSpeed(Self(), simulationSpeed);
}
bool Script::SetParticleSpeed(ecs::Entity entity, float simulationSpeed) {
    return m_context.registry && SetParticleSimulationSpeed(*m_context.registry, entity, simulationSpeed);
}
bool Script::AreParticlesPlaying() const { return AreParticlesPlaying(Self()); }
bool Script::AreParticlesPlaying(ecs::Entity entity) const {
    return m_context.registry && IsParticleSystemPlaying(*m_context.registry, entity);
}
int Script::ParticleCount() const { return ParticleCount(Self()); }
int Script::ParticleCount(ecs::Entity entity) const {
    return m_context.registry
        ? static_cast<int>(ParticleSystemAliveCount(*m_context.registry, entity)) : 0;
}

bool Script::ShakeCamera(float intensity, float duration, float frequency) {
    if (!m_context.cameraShake) return false;
    m_context.cameraShake->StartImpulse(intensity, duration, frequency);
    return true;
}

bool Script::ShakeCameraAdvanced(float translationAmplitude, float rotationDegrees,
                                 float duration, float frequency, float fovAmplitude) {
    if (!m_context.cameraShake) return false;
    CameraShakeSettings settings;
    settings.duration = duration;
    settings.frequency = frequency;
    const float translation = std::max(translationAmplitude, 0.0f);
    settings.translationAmplitude = glm::vec3(
        translation * 0.75f, translation, translation * 0.5f);
    const float rotation = std::max(rotationDegrees, 0.0f);
    settings.rotationAmplitudeDegrees = glm::vec2(rotation);
    settings.fovAmplitude = std::max(fovAmplitude, 0.0f);
    m_context.cameraShake->Start(settings);
    return true;
}

bool Script::PlayCameraSequence(const std::string& name, bool lockInput, bool skippable) {
    if (!m_context.cameraDirector || name.empty()) return false;
    m_context.cameraDirector->Play(name, lockInput, skippable);
    return true;
}

bool Script::StopCameraSequence() {
    if (!m_context.cameraDirector) return false;
    m_context.cameraDirector->Stop();
    return true;
}

bool Script::SkipCameraSequence() {
    if (!m_context.cameraDirector) return false;
    m_context.cameraDirector->Skip();
    return true;
}

bool Script::IsCameraSequencePlaying(const std::string& name) const {
    if (!m_context.cameraDirector) return false;
    return name.empty()
        ? m_context.cameraDirector->Playing()
        : m_context.cameraDirector->Playing(name);
}

bool Script::WasCameraSequenceFinished(const std::string& name) const {
    if (!m_context.cameraDirector || name.empty()) return false;
    for (const CameraSequenceEvent& event : m_context.cameraDirector->Events()) {
        if (event.name == name) return true;
    }
    return false;
}

bool Script::WasCameraSequenceSkipped(const std::string& name) const {
    if (!m_context.cameraDirector || name.empty()) return false;
    for (const CameraSequenceEvent& event : m_context.cameraDirector->Events()) {
        if (event.name == name && event.skipped) return true;
    }
    return false;
}

bool Script::WasCameraSequenceEvent(
    const std::string& sequenceName, const std::string& eventName) const {
    if (!m_context.cameraDirector || sequenceName.empty() || eventName.empty()) return false;
    for (const CameraTimelineEvent& event : m_context.cameraDirector->TimelineEvents()) {
        if (event.sequenceName == sequenceName && event.eventName == eventName) return true;
    }
    return false;
}

std::string Script::GetFieldString(const std::string& name, const std::string& fallback) const {
    if (!m_context.fields) {
        return fallback;
    }
    for (const ScriptField& field : *m_context.fields) {
        if (field.name == name) {
            return field.value;
        }
    }
    return fallback;
}

float Script::GetFieldFloat(const std::string& name, float fallback) const {
    const std::string value = GetFieldString(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const float parsed = std::strtof(value.c_str(), &end);
    return end != value.c_str() ? parsed : fallback;
}

int Script::GetFieldInt(const std::string& name, int fallback) const {
    const std::string value = GetFieldString(name);
    if (value.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(value.c_str(), &end, 10);
    return end != value.c_str() ? static_cast<int>(parsed) : fallback;
}

bool Script::GetFieldBool(const std::string& name, bool fallback) const {
    const std::string value = GetFieldString(name);
    if (value == "1" || value == "true" || value == "True") {
        return true;
    }
    if (value == "0" || value == "false" || value == "False") {
        return false;
    }
    return fallback;
}

glm::vec3 Script::GetFieldVec3(const std::string& name, const glm::vec3& fallback) const {
    const std::string value = GetFieldString(name);
    if (value.empty()) return fallback;
    glm::vec3 result = fallback;
    const char* p = value.c_str();
    char* end = nullptr;
    result.x = std::strtof(p, &end); if (end == p) return fallback; p = end;
    result.y = std::strtof(p, &end); if (end == p) return fallback; p = end;
    result.z = std::strtof(p, &end); if (end == p) return fallback;
    return result;
}

glm::vec3 Script::GetFieldColor(const std::string& name, const glm::vec3& fallback) const {
    return GetFieldVec3(name, fallback);   // a colour is stored just like a vec3 (r g b)
}

ecs::Entity Script::GetFieldEntity(const std::string& name) const {
    const std::string value = GetFieldString(name);
    return value.empty() ? ecs::kNull : FindObject(value);
}

std::string Script::GetFieldAsset(const std::string& name, const std::string& fallback) const {
    const std::string value = GetFieldString(name);
    return value.empty() ? fallback : value;
}

ScriptRegistry& ScriptRegistry::Instance() {
    static ScriptRegistry registry;
    return registry;
}

void ScriptRegistry::Register(const std::string& className, Factory factory) {
    if (className.empty() || !factory) {
        return;
    }
    m_factories[className] = std::move(factory);
}

bool ScriptRegistry::Has(const std::string& className) const {
    return m_factories.find(className) != m_factories.end();
}

std::unique_ptr<Script> ScriptRegistry::Create(const std::string& className) const {
    const auto it = m_factories.find(className);
    return it == m_factories.end() ? nullptr : it->second();
}

namespace {

std::function<void(const std::string&)>& ScriptErrorSink() {
    static std::function<void(const std::string&)> sink;
    return sink;
}

void ReportScriptError(const std::string& message) {
    std::fprintf(stderr, "[Script] %s\n", message.c_str());
    if (const auto& sink = ScriptErrorSink()) sink(message);
}

#if defined(_WIN32)
// Runs invoke(userData) under Structured Exception Handling: a hardware fault (access
// violation from a null/out-of-bounds access, divide-by-zero, etc.) is caught here, while
// a C++ exception is declined (CONTINUE_SEARCH) so the caller's C++ handler still receives
// it with its message. Holds NO C++ objects that need unwinding (MSVC C2712).
constexpr unsigned long kCppExceptionCode = 0xE06D7363u;
bool RunUnderSeh(void (*invoke)(void*), void* userData, unsigned long* faultCode) {
    __try {
        invoke(userData);
        return true;
    } __except (GetExceptionCode() == kCppExceptionCode
                    ? EXCEPTION_CONTINUE_SEARCH : EXCEPTION_EXECUTE_HANDLER) {
        *faultCode = GetExceptionCode();
        return false;
    }
}
#endif

// Run a script callback, isolating failures so one misbehaving script cannot take down
// the update loop. Thrown C++ exceptions AND (on Windows) hardware faults disable the
// script and are logged once.
template <class Fn>
void RunGuarded(NativeScriptSlot& script, Fn&& fn) {
#if defined(_WIN32)
    using FnType = std::remove_reference_t<Fn>;
    FnType* fnPtr = &fn;
    unsigned long faultCode = 0;
    bool ok = true;
    try {
        ok = RunUnderSeh([](void* p) { (*static_cast<FnType*>(p))(); }, fnPtr, &faultCode);
    } catch (const std::exception& e) {
        script.enabled = false;
        ReportScriptError("'" + script.className + "' disabled after exception: " + e.what());
        return;
    } catch (...) {
        script.enabled = false;
        ReportScriptError("'" + script.className + "' disabled after unknown exception");
        return;
    }
    if (!ok) {
        script.enabled = false;
        char code[16];
        std::snprintf(code, sizeof(code), "0x%08lX", faultCode);
        ReportScriptError("'" + script.className + "' disabled after a hardware fault ("
                          + code + ") — likely a null or out-of-bounds access");
    }
#else
    try {
        fn();
    } catch (const std::exception& e) {
        script.enabled = false;
        ReportScriptError("'" + script.className + "' disabled after exception: " + e.what());
    } catch (...) {
        script.enabled = false;
        ReportScriptError("'" + script.className + "' disabled after unknown exception");
    }
#endif
}

// Ensure the script has a live instance and has run OnCreate, and REFRESH its
// context every call — destroyQueue/input are per-call, so the pointers captured
// at creation time would otherwise dangle. Returns the instance, or nullptr if the
// script is disabled or its factory is missing.
Script* PrepareScript(ecs::Registry& registry, ecs::Entity entity, NativeScriptSlot& script,
                      std::vector<ecs::Entity>& destroyQueue, const ScriptInputState* input,
                      RuntimeAudioSystem* audio, CameraShake* cameraShake,
                      CameraDirector* cameraDirector, GameMode* gameMode,
                      PhysicsWorld* physics) {
    if (!script.enabled || script.className.empty()) {
        return nullptr;
    }
    if (!script.instance) {
        if (script.missingFactory) {
            return nullptr;   // already known-missing: don't retry every frame
        }
        if (IsLuaScriptPath(script.sourcePath)) {
            script.instance = std::make_unique<LuaScript>(script.sourcePath);
        } else {
            script.instance = ScriptRegistry::Instance().Create(script.className);
        }
        if (!script.instance) {
            script.missingFactory = true;
            std::fprintf(stderr,
                         "[Script] no registered factory for class '%s' "
                         "(rebuild the game and register the script?)\n",
                         script.className.c_str());
            return nullptr;
        }
    }
    script.instance->SetContext(
        ScriptContext{&registry, entity, &destroyQueue, input, audio, cameraShake,
                      cameraDirector, &script.fields, gameMode, nullptr, physics});
    if (!script.created) {
        RunGuarded(script, [&] { script.instance->OnCreate(); });
        script.created = true;
    }
    return script.enabled ? script.instance.get() : nullptr;
}

// Destroy queued entities, giving each scripted entity an OnDestroy() first.
void FlushDestroyQueue(ecs::Registry& registry, const std::vector<ecs::Entity>& destroyQueue) {
    for (ecs::Entity entity : destroyQueue) {
        if (!registry.Valid(entity)) {
            continue;
        }
        if (NativeScriptComponent* script = registry.TryGet<NativeScriptComponent>(entity)) {
            if (script->instance && script->created) {
                RunGuarded(*script, [&] { script->instance->OnDestroy(); });
            }
            for (NativeScriptSlot& additional : script->additional) {
                if (additional.instance && additional.created) {
                    RunGuarded(additional, [&] { additional.instance->OnDestroy(); });
                }
            }
        }
        registry.Destroy(entity);
    }
}

} // namespace

void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input,
                   RuntimeAudioSystem* audio, CameraShake* cameraShake,
                   CameraDirector* cameraDirector, GameMode* gameMode,
                   PhysicsWorld* physics) {
    thread_local std::vector<ecs::Entity> destroyQueueStorage;
    auto& destroyQueue = destroyQueueStorage;
    destroyQueue.clear();
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& script) {
            auto update = [&](NativeScriptSlot& slot) {
                if (Script* instance = PrepareScript(
                        registry, entity, slot, destroyQueue, input, audio,
                        cameraShake, cameraDirector, gameMode, physics)) {
                    RunGuarded(slot, [&] {
                        instance->TickTimers(dt);
                        instance->OnUpdate(dt);
                    });
                }
            };
            update(script);
            for (NativeScriptSlot& additional : script.additional) update(additional);
        });
    FlushDestroyQueue(registry, destroyQueue);
}

void FixedUpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input,
                        RuntimeAudioSystem* audio, CameraShake* cameraShake,
                        CameraDirector* cameraDirector, GameMode* gameMode,
                        PhysicsWorld* physics) {
    thread_local std::vector<ecs::Entity> destroyQueueStorage;
    auto& destroyQueue = destroyQueueStorage;
    destroyQueue.clear();
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& script) {
            auto fixedUpdate = [&](NativeScriptSlot& slot) {
                // Creation + OnCreate happen in UpdateScripts; only run live scripts.
                if (!slot.enabled || !slot.instance || !slot.created) return;
                slot.instance->SetContext(
                    ScriptContext{&registry, entity, &destroyQueue, input, audio,
                                  cameraShake, cameraDirector, &slot.fields, gameMode,
                                  nullptr, physics});
                RunGuarded(slot, [&] { slot.instance->OnFixedUpdate(dt); });
            };
            fixedUpdate(script);
            for (NativeScriptSlot& additional : script.additional) fixedUpdate(additional);
        });
    FlushDestroyQueue(registry, destroyQueue);
}

void ShutdownScripts(ecs::Registry& registry) {
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& script) {
            auto shutdown = [&](NativeScriptSlot& slot) {
                if (slot.instance && slot.created) {
                    // No destroy queue / input during teardown.
                    slot.instance->SetContext(
                        ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, &slot.fields});
                    RunGuarded(slot, [&] { slot.instance->OnDestroy(); });
                }
                slot.instance.reset();
                slot.created = false;
            };
            shutdown(script);
            for (NativeScriptSlot& additional : script.additional) shutdown(additional);
        });
}

void ShutdownScripts(ecs::Registry& registry, const std::vector<ecs::Entity>& entities) {
    for (ecs::Entity entity : entities) {
        if (!registry.Valid(entity)) continue;
        NativeScriptComponent* script = registry.TryGet<NativeScriptComponent>(entity);
        if (!script) continue;
        auto shutdown = [&](NativeScriptSlot& slot) {
            if (slot.instance && slot.created) {
                slot.instance->SetContext(
                    ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, &slot.fields});
                RunGuarded(slot, [&] { slot.instance->OnDestroy(); });
            }
            slot.instance.reset();
            slot.created = false;
        };
        shutdown(*script);
        for (NativeScriptSlot& additional : script->additional) shutdown(additional);
    }
}

std::string ConsumeScriptSceneLoadRequest() {
    std::string request = std::move(g_scriptSceneLoadRequest);
    g_scriptSceneLoadRequest.clear();
    return request;
}

std::vector<ScriptLevelStreamRequest> ConsumeScriptLevelStreamRequests() {
    std::vector<ScriptLevelStreamRequest> requests =
        std::move(g_scriptLevelStreamRequests);
    g_scriptLevelStreamRequests.clear();
    return requests;
}

std::vector<ScriptSaveGameRequest> ConsumeScriptSaveGameRequests() {
    std::vector<ScriptSaveGameRequest> requests = std::move(g_scriptSaveGameRequests);
    g_scriptSaveGameRequests.clear();
    return requests;
}

void SetScriptErrorHandler(std::function<void(const std::string&)> handler) {
    ScriptErrorSink() = std::move(handler);
}

} // namespace engine
