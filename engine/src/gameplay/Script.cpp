#include "engine/gameplay/Script.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/animation/Animator.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/SkinnedModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <utility>

namespace engine {

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

    animated->PlayAction(clipIndex,
        {},
        {},
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

    animated->PlayAction(clipIndex,
        Animator::BuildMask(skeleton, rootBone),
        {},
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
                return PlayMaskedAnimationAction(profile.clipName,
                    profile.maskRootBone,
                    profile.fadeIn,
                    profile.fadeOut,
                    profile.speed);
            }
            return PlayMaskedAnimationAction(profile.clipIndex,
                profile.maskRootBone,
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

std::string Script::GetFieldString(const std::string& name, const std::string& fallback) const {
    const NativeScriptComponent* script = TryGet<NativeScriptComponent>();
    if (!script) {
        return fallback;
    }
    for (const ScriptField& field : script->fields) {
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

void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input) {
    std::vector<ecs::Entity> destroyQueue;
    registry.view<NativeScriptComponent>().each(
        [&registry, &destroyQueue, input, dt](ecs::Entity entity, NativeScriptComponent& script) {
            if (!script.enabled || script.className.empty()) {
                return;
            }

            if (!script.instance) {
                script.instance = ScriptRegistry::Instance().Create(script.className);
                script.missingFactory = !script.instance;
                if (!script.instance) {
                    return;
                }
                script.instance->SetContext(ScriptContext{&registry, entity, &destroyQueue, input});
            }

            if (!script.created) {
                script.instance->OnCreate();
                script.created = true;
            }
            script.instance->OnUpdate(dt);
        }
    );

    for (ecs::Entity entity : destroyQueue) {
        if (registry.Valid(entity)) {
            registry.Destroy(entity);
        }
    }
}

} // namespace engine