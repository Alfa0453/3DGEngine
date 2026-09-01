#include "engine/gameplay/Script.h"
#include "engine/gameplay/LuaScript.h"
#include "engine/graphics/CameraShake.h"
#include "engine/gameplay/CameraDirector.h"
#include "engine/gameplay/GameMode.h"
#include "engine/gameplay/GameplayComponents.h"
#include "engine/gameplay/RagdollSystem.h"
#include "engine/gameplay/AbilitySystem.h"
#include "engine/gameplay/DestructionSystem.h"
#include "engine/gameplay/InteractionSystem.h"
#include "engine/animation/IKRigSystem.h"
#include "engine/gameplay/PortalSystem.h"
#include "engine/gameplay/QuestSystem.h"
#include "engine/gameplay/DialogueSystem.h"
#include "engine/gameplay/InventorySystem.h"
#include "engine/gameplay/CombatSystem.h"
#include "engine/gameplay/SpawnSystem.h"
#include "engine/gameplay/SaveProfileSystem.h"
#include "engine/gameplay/EquipmentSystem.h"
#include "engine/gameplay/QuestSystem.h"

#include "engine/animation/AnimatedModel.h"
#include "engine/animation/Animator.h"
#include "engine/audio/RuntimeAudioSystem.h"
#include "engine/ecs/Registry.h"
#include "engine/graphics/SkinnedModel.h"
#include "engine/graphics/RuntimeParticleSystem.h"
#include "engine/math/Spline.h"
#include "engine/assets/ScatterGraphAsset.h"
#include "engine/assets/BiomeAsset.h"
#include "engine/assets/DayNightTimelineAsset.h"
#include "engine/assets/CaveAsset.h"
#include "engine/assets/PoseLibraryAsset.h"
#include "engine/assets/EquipmentAsset.h"

#include <exception>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
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
class ScriptEventDispatcher {
public:
    static void Dispatch(ecs::Registry& registry,
                         std::vector<ecs::Entity>& destroyQueue,
                         const ScriptInputState* input, RuntimeAudioSystem* audio,
                         CameraShake* cameraShake, CameraDirector* cameraDirector,
                         GameMode* gameMode, PhysicsWorld* physics);
};
class ScriptCallDispatcher {
public:
    static bool Invoke(const ScriptContext& caller, const ScriptHandle& handle,
                       const std::string& functionName, ScriptEvent arguments,
                       ScriptEvent* result);
};

namespace {
std::string g_scriptSceneLoadRequest;
std::vector<ScriptLevelStreamRequest> g_scriptLevelStreamRequests;
std::vector<ScriptSaveGameRequest> g_scriptSaveGameRequests;
struct QueuedScriptEvent {
    ecs::Registry* registry = nullptr;
    ScriptEvent event;
};
std::vector<QueuedScriptEvent> g_scriptEvents;
struct ObservedHealth {
    float hp = 0.0f;
    float maxHp = 0.0f;
    bool alive = true;
};
std::unordered_map<ecs::Registry*, std::unordered_map<ecs::Entity, ObservedHealth>>
    g_observedHealth;
constexpr const char* kSaveDataPath = "3dg_savegame.dat";

struct RuntimeScriptDebugger {
    bool enabled = false;
    bool paused = false;
    bool stepRequested = false;
    std::string stopReason;
    std::unordered_set<std::string> breakpoints;
    std::unordered_map<std::string, ScriptExecutionStat> statistics;
};

RuntimeScriptDebugger& ScriptDebugger() {
    static RuntimeScriptDebugger debugger;
    return debugger;
}

std::string ScriptDebugKey(const std::string& className, ScriptCallbackKind callback) {
    return className + '#' + std::to_string(static_cast<int>(callback));
}

std::string ScriptStatKey(ecs::Entity entity, const std::string& className,
                          ScriptCallbackKind callback) {
    return std::to_string(entity) + '#' + ScriptDebugKey(className, callback);
}

std::unordered_map<std::string, std::string> ReadSaveValues() {
    std::unordered_map<std::string, std::string> values;
    std::ifstream input(kSaveDataPath);
    std::string key, value;
    while (input >> std::quoted(key) >> std::quoted(value))
        values[std::move(key)] = std::move(value);
    return values;
}
} // namespace

bool ScriptEvent::GetBool(const std::string& key, bool fallback) const {
    const auto it = bools.find(key);
    return it == bools.end() ? fallback : it->second;
}

int ScriptEvent::GetInt(const std::string& key, int fallback) const {
    const auto it = ints.find(key);
    return it == ints.end() ? fallback : it->second;
}

float ScriptEvent::GetFloat(const std::string& key, float fallback) const {
    const auto it = floats.find(key);
    return it == floats.end() ? fallback : it->second;
}

std::string ScriptEvent::GetString(const std::string& key,
                                   const std::string& fallback) const {
    const auto it = strings.find(key);
    return it == strings.end() ? fallback : it->second;
}

glm::vec3 ScriptEvent::GetVector(const std::string& key,
                                 const glm::vec3& fallback) const {
    const auto it = vectors.find(key);
    return it == vectors.end() ? fallback : it->second;
}

ecs::Entity ScriptEvent::GetEntity(const std::string& key, ecs::Entity fallback) const {
    const auto it = entities.find(key);
    return it == entities.end() ? fallback : it->second;
}

void QueueScriptEvent(ecs::Registry& registry, ScriptEvent event) {
    if (event.name.empty()) return;
    g_scriptEvents.push_back(QueuedScriptEvent{&registry, std::move(event)});
}

void QueueScriptAnimationEvent(ecs::Registry& registry, ecs::Entity entity,
                               const std::string& eventName) {
    if (eventName.empty()) return;
    ScriptEvent event;
    event.name = "animation.event";
    event.sender = entity;
    event.target = entity;
    event.strings["name"] = eventName;
    QueueScriptEvent(registry, std::move(event));
}

void QueueScriptCollisionEvents(ecs::Registry& registry,
                                const std::vector<CollisionEvent>& events) {
    for (const CollisionEvent& collision : events) {
        const char* phase = collision.phase == CollisionEvent::Phase::Enter ? "enter"
            : collision.phase == CollisionEvent::Phase::Stay ? "stay" : "exit";
        const std::string name = std::string(collision.trigger ? "trigger." : "collision.")
            + phase;
        auto queueFor = [&](ecs::Entity target, ecs::Entity other, const glm::vec3& normal) {
            if (target == ecs::kNull) return;
            ScriptEvent event;
            event.name = name;
            event.sender = other;
            event.target = target;
            event.entities["other"] = other;
            event.bools["trigger"] = collision.trigger;
            event.vectors["point"] = collision.point;
            event.vectors["normal"] = normal;
            event.floats["impulse"] = collision.impulse;
            QueueScriptEvent(registry, std::move(event));
        };
        queueFor(collision.a, collision.b, collision.normal);
        queueFor(collision.b, collision.a, -collision.normal);
    }
}

void Script::PublishEvent(ScriptEvent event) {
    if (!m_context.registry || event.name.empty()) return;
    if (event.sender == ecs::kNull) event.sender = m_context.entity;
    QueueScriptEvent(*m_context.registry, std::move(event));
}

void Script::PublishEvent(const std::string& name, ecs::Entity target) {
    ScriptEvent event;
    event.name = name;
    event.target = target;
    PublishEvent(std::move(event));
}

bool Script::ListenForEvent(const std::string& name) {
    return !name.empty() && m_listenedEvents.insert(name).second;
}

bool Script::StopListeningForEvent(const std::string& name) {
    return m_listenedEvents.erase(name) != 0;
}

int Script::SubscribeEvent(const std::string& name,
                           std::function<void(const ScriptEvent&)> callback) {
    if (name.empty() || !callback) return 0;
    const int id = m_nextEventSubscriptionId++;
    m_eventSubscriptions.push_back(EventSubscription{id, name, std::move(callback)});
    return id;
}

bool Script::UnsubscribeEvent(int subscriptionId) {
    const auto oldSize = m_eventSubscriptions.size();
    m_eventSubscriptions.erase(
        std::remove_if(m_eventSubscriptions.begin(), m_eventSubscriptions.end(),
            [subscriptionId](const EventSubscription& subscription) {
                return subscription.id == subscriptionId;
            }),
        m_eventSubscriptions.end());
    return oldSize != m_eventSubscriptions.size();
}

void Script::UnsubscribeAllEvents() {
    m_listenedEvents.clear();
    m_eventSubscriptions.clear();
}

bool Script::WantsEvent(const std::string& name) const {
    if (m_listenedEvents.count(name) != 0 || m_listenedEvents.count("*") != 0) return true;
    return std::any_of(m_eventSubscriptions.begin(), m_eventSubscriptions.end(),
        [&name](const EventSubscription& subscription) {
            return subscription.name == name || subscription.name == "*";
        });
}

void Script::DispatchEvent(const ScriptEvent& event) {
    if (m_listenedEvents.count(event.name) != 0 || m_listenedEvents.count("*") != 0)
        OnEvent(event);

    // Handlers may unsubscribe themselves. Copy matching functions first so those
    // edits cannot invalidate the active callback iteration.
    std::vector<std::function<void(const ScriptEvent&)>> callbacks;
    for (const EventSubscription& subscription : m_eventSubscriptions) {
        if (subscription.name == event.name || subscription.name == "*")
            callbacks.push_back(subscription.callback);
    }
    for (const auto& callback : callbacks) callback(event);
}

ScriptHandle Script::FindScript(ecs::Entity entity,
                                const std::string& className) const {
    if (!m_context.registry || entity == ecs::kNull
        || !m_context.registry->Valid(entity)) return {};
    const NativeScriptComponent* component =
        m_context.registry->TryGet<NativeScriptComponent>(entity);
    if (!component) return {};
    auto matches = [&className](const NativeScriptSlot& slot) {
        return !slot.className.empty()
            && (className.empty() || slot.className == className);
    };
    if (matches(*component)) return {entity, component->className};
    for (const NativeScriptSlot& additional : component->additional)
        if (matches(additional)) return {entity, additional.className};
    return {};
}

ScriptHandle Script::FindScript(const std::string& objectName,
                                const std::string& className) const {
    return FindScript(FindObject(objectName), className);
}

bool Script::IsScriptValid(const ScriptHandle& handle) const {
    if (!handle) return false;
    const ScriptHandle resolved = FindScript(handle.entity, handle.className);
    return resolved && resolved.className == handle.className;
}

bool Script::IsScriptEnabled(const ScriptHandle& handle) const {
    if (!m_context.registry || !handle || !m_context.registry->Valid(handle.entity))
        return false;
    const NativeScriptComponent* component =
        m_context.registry->TryGet<NativeScriptComponent>(handle.entity);
    if (!component) return false;
    auto enabled = [&handle](const NativeScriptSlot& slot) {
        return slot.className == handle.className && slot.enabled;
    };
    if (enabled(*component)) return true;
    return std::any_of(component->additional.begin(), component->additional.end(), enabled);
}

bool Script::SetScriptEnabled(const ScriptHandle& handle, bool enabled) {
    if (!m_context.registry || !handle || !m_context.registry->Valid(handle.entity))
        return false;
    NativeScriptComponent* component =
        m_context.registry->TryGet<NativeScriptComponent>(handle.entity);
    if (!component) return false;
    auto set = [&](NativeScriptSlot& slot) {
        if (slot.className != handle.className) return false;
        slot.enabled = enabled;
        return true;
    };
    if (set(*component)) return true;
    for (NativeScriptSlot& additional : component->additional)
        if (set(additional)) return true;
    return false;
}

bool Script::SetSelfEnabled(bool enabled) {
    if (!m_context.registry || m_context.entity == ecs::kNull
        || !m_context.registry->Valid(m_context.entity)) return false;
    NativeScriptComponent* component =
        m_context.registry->TryGet<NativeScriptComponent>(m_context.entity);
    if (!component) return false;
    auto set = [&](NativeScriptSlot& slot) {
        if (slot.instance.get() != this) return false;
        slot.enabled = enabled;
        return true;
    };
    if (set(*component)) return true;
    for (NativeScriptSlot& additional : component->additional)
        if (set(additional)) return true;
    return false;
}

bool Script::BindScriptFunction(const std::string& functionName,
                                CallableFunction function) {
    if (functionName.empty() || !function) return false;
    m_scriptFunctions[functionName] = std::move(function);
    return true;
}

bool Script::UnbindScriptFunction(const std::string& functionName) {
    return m_scriptFunctions.erase(functionName) != 0;
}

bool Script::CallScript(const ScriptHandle& handle, const std::string& functionName,
                        ScriptEvent arguments, ScriptEvent* result) {
    return ScriptCallDispatcher::Invoke(
        m_context, handle, functionName, std::move(arguments), result);
}

bool Script::InvokeScriptFunction(const std::string& functionName,
                                  const ScriptEvent& arguments,
                                  ScriptEvent& result) {
    const auto bound = m_scriptFunctions.find(functionName);
    if (bound != m_scriptFunctions.end() && bound->second(arguments, result)) return true;
    return OnScriptCall(functionName, arguments, result);
}

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

bool Script::ActivateRagdoll() {
    return m_context.registry && m_context.physics
        && engine::ActivateRagdoll(*m_context.registry, *m_context.physics,
                                   m_context.entity);
}

bool Script::RecoverFromRagdoll() {
    return m_context.registry
        && engine::RequestRagdollRecovery(*m_context.registry,
                                          m_context.entity);
}

bool Script::GrantAbility(const std::string& assetPath) {
    return m_context.registry && engine::GrantAbility(
        *m_context.registry, m_context.entity, assetPath, nullptr);
}
bool Script::ActivateAbility(const std::string& name, ecs::Entity target) {
    return m_context.registry && engine::ActivateAbility(
        *m_context.registry, m_context.entity, name, target);
}
bool Script::CancelAbility() {
    return m_context.registry && engine::CancelAbility(*m_context.registry, m_context.entity);
}
bool Script::IsAbilityActive(const std::string& name) const {
    return m_context.registry && engine::IsAbilityActive(*m_context.registry, m_context.entity, name);
}
float Script::AbilityCooldown(const std::string& name) const {
    return m_context.registry ? engine::AbilityCooldownRemaining(
        *m_context.registry, m_context.entity, name) : 0.0f;
}
bool Script::SetAbilityResources(float mana, float stamina) {
    if (!m_context.registry) return false;
    AbilityResource* resource = m_context.registry->TryGet<AbilityResource>(m_context.entity);
    if (!resource) resource = &m_context.registry->Add<AbilityResource>(m_context.entity, {});
    resource->mana = std::clamp(mana, 0.0f, resource->maxMana);
    resource->stamina = std::clamp(stamina, 0.0f, resource->maxStamina);
    return true;
}
bool Script::WasAbilityEvent(const std::string& eventName) {
    if (!m_context.registry || eventName.empty()) return false;
    AbilityComponent* abilities = m_context.registry->TryGet<AbilityComponent>(m_context.entity);
    if (!abilities) return false;
    const auto it = std::find_if(abilities->events.begin(), abilities->events.end(),
        [&](const AbilityRuntimeEvent& event) { return event.name == eventName; });
    if (it == abilities->events.end()) return false;
    abilities->events.erase(it);
    return true;
}

bool Script::ConfigureDestructible(const std::string& assetPath) {
    return m_context.registry&&engine::ConfigureDestructible(*m_context.registry,m_context.entity,assetPath);
}
bool Script::DamageDestructible(float damage,const glm::vec3& point,const glm::vec3& impulse) {
    return m_context.registry&&engine::DamageDestructible(*m_context.registry,m_context.entity,damage,point,impulse);
}
bool Script::ImpactDestructible(float impact,const glm::vec3& point,const glm::vec3& direction) {
    return m_context.registry&&engine::ImpactDestructible(*m_context.registry,m_context.entity,impact,point,direction);
}
float Script::DestructibleHealth() const {return m_context.registry?engine::DestructibleHealth(*m_context.registry,m_context.entity):0.f;}
bool Script::IsDestructibleBroken() const {return m_context.registry&&engine::IsDestructibleBroken(*m_context.registry,m_context.entity);}
bool Script::WasDestructionEvent(const std::string& name) {
    if(!m_context.registry)return false;auto* component=m_context.registry->TryGet<DestructibleComponent>(m_context.entity);if(!component)return false;
    const auto match=[&](const DestructionRuntimeEvent& event){return (name=="Broken"&&event.type==DestructionRuntimeEvent::Type::Broken)||(name=="Damaged"&&event.type==DestructionRuntimeEvent::Type::DamagedState);};
    const auto it=std::find_if(component->events.begin(),component->events.end(),match);if(it==component->events.end())return false;component->events.erase(it);return true;
}
bool Script::OpenInteraction(ecs::Entity target,const std::string& accessTag) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::OpenInteraction(*m_context.registry,target,accessTag);
}
bool Script::CloseInteraction(ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::CloseInteraction(*m_context.registry,target);
}
bool Script::ToggleInteraction(ecs::Entity target,const std::string& accessTag) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::ToggleInteraction(*m_context.registry,target,accessTag);
}
bool Script::SetInteractionLocked(bool locked,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::SetInteractionLocked(*m_context.registry,target,locked);
}
std::string Script::InteractionState(ecs::Entity target) const {
    if(!m_context.registry)return "Disabled";if(target==ecs::kNull)target=m_context.entity;
    return engine::InteractionStateName(engine::GetInteractionState(*m_context.registry,target));
}
namespace {
std::vector<std::string> ParseInteractionTags(const std::string& value) {
    std::vector<std::string> tags; std::stringstream stream(value); std::string tag;
    while (std::getline(stream, tag, ',')) {
        const auto first=tag.find_first_not_of(" \t");const auto last=tag.find_last_not_of(" \t");
        if(first!=std::string::npos)tags.push_back(tag.substr(first,last-first+1));
    }
    return tags;
}
InteractionQuery ScriptInteractionQuery(const ecs::Registry& registry,ecs::Entity interactor,
                                        const std::string& access,const std::string& tags,bool sight) {
    InteractionQuery query;query.accessTag=access;query.conditionTags=ParseInteractionTags(tags);
    query.hasLineOfSight=sight;
    if(const auto* transform=registry.TryGet<ecs::Transform>(interactor)) {
        query.interactorPosition=transform->position;
        query.interactorForward=transform->rotation*glm::vec3(0.0f,0.0f,-1.0f);
    }
    return query;
}
}
bool Script::CanInteract(ecs::Entity target,const std::string& access,const std::string& tags,bool sight) const {
    if(!m_context.registry||target==ecs::kNull)return false;
    return engine::QueryInteraction(*m_context.registry,target,
        ScriptInteractionQuery(*m_context.registry,m_context.entity,access,tags,sight)).available;
}
std::string Script::InteractionPrompt(ecs::Entity target,const std::string& access,const std::string& tags,bool sight) const {
    if(!m_context.registry||target==ecs::kNull)return "Unavailable";
    return engine::QueryInteraction(*m_context.registry,target,
        ScriptInteractionQuery(*m_context.registry,m_context.entity,access,tags,sight)).prompt;
}
bool Script::RequestInteraction(ecs::Entity target,float held,const std::string& access,
                                const std::string& tags,bool sight) {
    if(!m_context.registry||target==ecs::kNull)return false;
    return engine::RequestInteraction(*m_context.registry,target,
        ScriptInteractionQuery(*m_context.registry,m_context.entity,access,tags,sight),held);
}
bool Script::SignalInteractionEvent(ecs::Entity target,const std::string& eventName) {
    return m_context.registry&&target!=ecs::kNull&&
        engine::SignalInteractionAnimationEvent(*m_context.registry,target,eventName);
}
void Script::CancelInteractionInput(ecs::Entity target) {
    if(m_context.registry&&target!=ecs::kNull)
        engine::CancelInteractionRequest(*m_context.registry,target);
}
bool Script::WasInteractionEvent(const std::string& eventName,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    auto* component=m_context.registry->TryGet<InteractiveMotionComponent>(target);
    if(!component)return false;
    const auto it=std::find_if(component->events.begin(),component->events.end(),
        [&](const InteractionRuntimeEvent& event){return event.eventName==eventName;});
    if(it==component->events.end())return false;
    const auto index=static_cast<std::size_t>(std::distance(component->events.begin(),it));
    component->events.erase(it);
    if(component->processedEventCount>index&&component->processedEventCount>0)
        --component->processedEventCount;
    return true;
}
bool Script::ConfigureIKRig(const std::string& path,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::ConfigureIKRig(*m_context.registry,target,path);
}
bool Script::SetIKTarget(const std::string& goal,const glm::vec3& position,float weight,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::SetIKTarget(*m_context.registry,target,goal,position,weight);
}
bool Script::ClearIKTarget(const std::string& goal,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::ClearIKTarget(*m_context.registry,target,goal);
}
bool Script::SetIKWeight(const std::string& goal,float weight,ecs::Entity target) {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::SetIKGoalWeight(*m_context.registry,target,goal,weight);
}
bool Script::HasIKGoal(const std::string& goal,ecs::Entity target) const {
    if(!m_context.registry)return false;if(target==ecs::kNull)target=m_context.entity;
    return engine::HasIKGoal(*m_context.registry,target,goal);
}
bool Script::UsePortal(ecs::Entity portal, const std::string& accessTag) {
    if (!m_context.registry || portal == ecs::kNull) return false;
    if (!engine::ActivatePortal(*m_context.registry, portal, m_context.entity, accessTag)) return false;
    for (const auto& event : engine::ConsumePortalEvents(*m_context.registry, portal)) {
        if (event.type == engine::PortalEventType::LevelTransitionRequested &&
            !event.levelPath.empty()) RequestSceneLoad(event.levelPath);
    }
    return true;
}
bool Script::IsPortalReady(ecs::Entity portal) const {
    return m_context.registry && portal != ecs::kNull &&
        engine::PortalReady(*m_context.registry, portal);
}
bool Script::GrantQuest(const std::string& path){return m_context.registry&&engine::GrantQuest(*m_context.registry,m_context.entity,path);}
bool Script::StartQuest(const std::string& name){return m_context.registry&&engine::StartQuest(*m_context.registry,m_context.entity,name);}
bool Script::AdvanceQuest(const std::string& name,const std::string& objective,int amount){return m_context.registry&&engine::AdvanceQuest(*m_context.registry,m_context.entity,name,objective,amount);}
bool Script::FailQuest(const std::string& name){return m_context.registry&&engine::FailQuest(*m_context.registry,m_context.entity,name);}
bool Script::SetQuestFlag(const std::string& flag,bool value){return m_context.registry&&engine::SetQuestFlag(*m_context.registry,m_context.entity,flag,value);}
std::string Script::QuestState(const std::string& name)const{return m_context.registry?engine::QuestStateName(engine::GetQuestState(*m_context.registry,m_context.entity,name)):"Inactive";}
int Script::QuestProgress(const std::string& name,const std::string& objective)const{return m_context.registry?engine::GetQuestProgress(*m_context.registry,m_context.entity,name,objective):0;}
std::string Script::SaveQuestState()const{return m_context.registry?engine::SerializeQuestState(*m_context.registry,m_context.entity):std::string{};}
bool Script::LoadQuestState(const std::string& data){return m_context.registry&&engine::RestoreQuestState(*m_context.registry,m_context.entity,data);}
bool Script::StartDialogue(const std::string& path){return m_context.registry&&engine::StartDialogue(*m_context.registry,m_context.entity,path);}
bool Script::StartDialogue(ecs::Entity source){return m_context.registry&&engine::StartDialogue(*m_context.registry,m_context.entity,source);}
bool Script::ChooseDialogue(int choice){return m_context.registry&&engine::ChooseDialogueOption(*m_context.registry,m_context.entity,choice);}
bool Script::ContinueDialogue(){return m_context.registry&&engine::ContinueDialogue(*m_context.registry,m_context.entity);}
bool Script::CancelDialogue(){return m_context.registry&&engine::CancelDialogue(*m_context.registry,m_context.entity);}
bool Script::SetDialogueFlag(const std::string& flag,bool value){return m_context.registry&&engine::SetDialogueFlag(*m_context.registry,m_context.entity,flag,value);}
bool Script::IsDialogueActive()const{return m_context.registry&&engine::IsDialogueActive(*m_context.registry,m_context.entity);}
std::string Script::DialogueNode()const{const auto* n=m_context.registry?engine::CurrentDialogueNode(*m_context.registry,m_context.entity):nullptr;return n?n->id:std::string{};}
std::string Script::DialogueText()const{const auto* n=m_context.registry?engine::CurrentDialogueNode(*m_context.registry,m_context.entity):nullptr;return n?n->text:std::string{};}
std::string Script::DialogueSpeaker()const{const auto* n=m_context.registry?engine::CurrentDialogueNode(*m_context.registry,m_context.entity):nullptr;return n?n->speaker:std::string{};}
std::string Script::SaveDialogueState()const{return m_context.registry?engine::SerializeDialogueState(*m_context.registry,m_context.entity):std::string{};}
bool Script::LoadDialogueState(const std::string& data){return m_context.registry&&engine::RestoreDialogueState(*m_context.registry,m_context.entity,data);}
bool Script::AddItem(const std::string& path,int count){return m_context.registry&&engine::AddItem(*m_context.registry,m_context.entity,path,count);}
int Script::RemoveItem(const std::string& name,int count){return m_context.registry?engine::RemoveItem(*m_context.registry,m_context.entity,name,count):0;}
bool Script::UseItem(const std::string& name){return m_context.registry&&engine::UseItem(*m_context.registry,m_context.entity,name);}
bool Script::EquipItem(const std::string& name){return m_context.registry&&engine::EquipItem(*m_context.registry,m_context.entity,name);}
bool Script::UnequipItemSlot(int slot){return m_context.registry&&engine::UnequipSlot(*m_context.registry,m_context.entity,static_cast<engine::EquipmentSlot>(std::clamp(slot,0,8)));}
int Script::ItemCount(const std::string& name)const{return m_context.registry?engine::ItemCount(*m_context.registry,m_context.entity,name):0;}
bool Script::HasItem(const std::string& name,int count)const{return m_context.registry&&engine::HasItem(*m_context.registry,m_context.entity,name,count);}
float Script::InventoryWeight()const{return m_context.registry?engine::InventoryWeight(*m_context.registry,m_context.entity):0.0f;}
std::string Script::SaveInventory()const{return m_context.registry?engine::SerializeInventory(*m_context.registry,m_context.entity):std::string{};}
bool Script::LoadInventory(const std::string& data){return m_context.registry&&engine::RestoreInventory(*m_context.registry,m_context.entity,data);}
bool Script::ConfigureCombat(const std::string& path){return m_context.registry&&engine::ConfigureCombat(*m_context.registry,m_context.entity,path);}
void Script::SetCombatBlocking(bool blocking){if(m_context.registry)engine::SetCombatBlocking(*m_context.registry,m_context.entity,blocking);}
bool Script::StartCombat(ecs::Entity target){return m_context.registry&&engine::StartCombatCombo(*m_context.registry,m_context.entity,target);}
bool Script::AdvanceCombat(){return m_context.registry&&engine::AdvanceCombatCombo(*m_context.registry,m_context.entity);}
std::string Script::CombatHit(ecs::Entity target){return m_context.registry?engine::CombatResultName(engine::ExecuteCombatHit(*m_context.registry,m_context.entity,target)):"Miss";}
std::string Script::DealCombatDamage(ecs::Entity target,float damage,const std::string& type){return m_context.registry?engine::CombatResultName(engine::ApplyCombatDamage(*m_context.registry,m_context.entity,target,damage,type)):"Miss";}
bool Script::IsCombatStaggered()const{return m_context.registry&&engine::IsCombatStaggered(*m_context.registry,m_context.entity);}
int Script::CombatStep()const{return m_context.registry?engine::CombatComboStep(*m_context.registry,m_context.entity):-1;}
bool Script::ConfigureSpawnManager(const std::string&path){return m_context.registry&&engine::ConfigureSpawnManager(*m_context.registry,m_context.entity,path);}
bool Script::StartSpawn(float difficulty,ecs::Entity manager){if(manager==ecs::kNull)manager=m_context.entity;return m_context.registry&&engine::StartSpawnEncounter(*m_context.registry,manager,difficulty);}
void Script::StopSpawn(ecs::Entity manager){if(manager==ecs::kNull)manager=m_context.entity;if(m_context.registry)engine::StopSpawnEncounter(*m_context.registry,manager);}
void Script::ResetSpawn(ecs::Entity manager){if(manager==ecs::kNull)manager=m_context.entity;if(m_context.registry)engine::ResetSpawnEncounter(*m_context.registry,manager);}
bool Script::TriggerSpawnWave(int wave,ecs::Entity manager){if(manager==ecs::kNull)manager=m_context.entity;return m_context.registry&&engine::TriggerSpawnWave(*m_context.registry,manager,wave);}
void Script::SetSpawnDifficulty(float difficulty,ecs::Entity manager){if(manager==ecs::kNull)manager=m_context.entity;if(m_context.registry)engine::SetSpawnDifficulty(*m_context.registry,manager,difficulty);}
int Script::SpawnAlive(ecs::Entity manager)const{if(manager==ecs::kNull)manager=m_context.entity;return m_context.registry?engine::SpawnAliveCount(*m_context.registry,manager):0;}
bool Script::IsSpawnRunning(ecs::Entity manager)const{if(manager==ecs::kNull)manager=m_context.entity;return m_context.registry&&engine::SpawnEncounterRunning(*m_context.registry,manager);}

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

float Script::GetAnimationCurve(const std::string& name, float fallback) const {
    const AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model || name.empty()) return fallback;
    int clipIndex = animated->controller.CurrentClip();
    float time = animated->controller.CurrentSourceTime();
    if (animated->action.active) {
        clipIndex = animated->action.clip;
        time = animated->action.time;
    }
    if (clipIndex < 0
        || clipIndex >= static_cast<int>(animated->model->AnimationCount()))
        return fallback;
    return Animator::SampleCurve(
        animated->model->Animations()[static_cast<std::size_t>(clipIndex)],
        name, time, fallback);
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

float Script::SplineLength(ecs::Entity spline) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || component->points.size() < 2) return 0.0f;
    return Spline(component->points, component->closed).Length();
}

bool Script::ApplyAnimationPose(const std::string& libraryPath, const std::string& poseName,
                                float weight) {
    AnimatedModel* animated = TryGet<AnimatedModel>();
    if (!animated || !animated->model || libraryPath.empty() || poseName.empty()) return false;
    PoseLibraryAssetData library; std::string error;
    if (!LoadPoseLibraryAsset(libraryPath, &library, &error)) return false;
    const PoseLibraryPose* pose = FindPose(library, poseName); if (!pose) return false;
    std::vector<BoneLocal> local;
    ResolvePoseForSkeleton(*pose, animated->model->GetSkeleton(), local);
    animated->SetPoseOverride(std::move(local), weight); return true;
}

void Script::ClearAnimationPose() {
    if (AnimatedModel* animated = TryGet<AnimatedModel>()) animated->ClearPoseOverride();
}
bool Script::EquipCharacterItem(const std::string& path,const std::string& item){
    if (!m_context.registry || m_context.entity == ecs::kNull ||
        !m_context.registry->Valid(m_context.entity)) return false;
    const bool equipped=engine::EquipItem(*m_context.registry,m_context.entity,path,item);
    if(equipped){EquipmentAssetData set;std::string error;if(LoadEquipmentAsset(path,&set,&error))if(const EquipmentItem*entry=FindEquipmentItem(set,item);entry&&!entry->equipAudioPath.empty())PlayAudioCue(entry->equipAudioPath,true);}
    return equipped;
}
bool Script::UnequipCharacterSlot(const std::string& slot){return m_context.registry&&m_context.entity!=ecs::kNull&&m_context.registry->Valid(m_context.entity)&&engine::UnequipSlot(*m_context.registry,m_context.entity,slot);}
std::string Script::EquippedCharacterItem(const std::string& slot)const{return m_context.registry&&m_context.entity!=ecs::kNull&&m_context.registry->Valid(m_context.entity)?engine::EquippedItem(*m_context.registry,m_context.entity,slot):std::string{};}

int Script::GenerateScatterGraph(const std::string& assetPath,
                                 const glm::vec3& worldOffset,
                                 std::uint32_t seedOverride) {
    if (!m_context.registry || assetPath.empty()) return 0;
    ScatterGraphAssetData graph;
    std::string error;
    if (!LoadScatterGraphAsset(assetPath, &graph, &error)) return 0;
    const std::vector<ScatterPlacement> placements =
        EvaluateScatterGraph(graph, {}, worldOffset, seedOverride);
    int generated = 0;
    for (const ScatterPlacement& placement : placements) {
        const ecs::Entity entity = m_context.registry->Create();
        ecs::Transform transform;
        transform.position = placement.position;
        transform.rotation = placement.rotation;
        transform.scale = placement.scale;
        m_context.registry->Add<ecs::Transform>(entity, transform);
        m_context.registry->Add<ecs::ModelAsset>(entity,
            ecs::ModelAsset{placement.meshPath});
        m_context.registry->Add<ecs::RuntimeName>(entity,
            ecs::RuntimeName{"Scatter_" + std::to_string(generated + 1)});
        ++generated;
    }
    return generated;
}

int Script::GenerateBiome(const std::string& assetPath,
                          const glm::vec3& worldOffset,
                          std::uint32_t seedOverride) {
    if (!m_context.registry || assetPath.empty()) return 0;
    BiomeAssetData biome; std::string error;
    if (!LoadBiomeAsset(assetPath, &biome, &error)) return 0;
    const auto surface = [&biome](float, float) {
        BiomeSurfaceSample sample;
        sample.moisture = biome.moisture;
        sample.temperature = biome.temperature;
        sample.normalizedHeight = 0.5f;
        return sample;
    };
    const auto placements = EvaluateBiome(biome, surface, worldOffset, seedOverride);
    int generated = 0;
    for (const BiomePlacement& placement : placements) {
        const ecs::Entity entity = m_context.registry->Create();
        ecs::Transform transform;
        transform.position = placement.position;
        transform.rotation = placement.rotation;
        transform.scale = placement.scale;
        m_context.registry->Add<ecs::Transform>(entity, transform);
        m_context.registry->Add<ecs::ModelAsset>(entity, ecs::ModelAsset{placement.meshPath});
        m_context.registry->Add<ecs::RuntimeName>(entity,
            ecs::RuntimeName{"Biome_" + std::to_string(generated + 1)});
        ++generated;
    }
    return generated;
}

ecs::Entity Script::SpawnCave(const std::string& assetPath, const glm::vec3& worldOffset) {
    if (!m_context.registry || assetPath.empty()) return ecs::kNull;
    CaveAssetData cave; std::string error;
    if (!LoadCaveAsset(assetPath, &cave, &error) || cave.bakedMeshPath.empty())
        return ecs::kNull;
    const ecs::Entity entity = m_context.registry->Create();
    ecs::Transform transform; transform.position = worldOffset;
    m_context.registry->Add<ecs::Transform>(entity, transform);
    m_context.registry->Add<ecs::ModelAsset>(entity, ecs::ModelAsset{cave.bakedMeshPath});
    m_context.registry->Add<ecs::RuntimeName>(entity,
        ecs::RuntimeName{"Cave_" + cave.name + "_Runtime"});
    return entity;
}

bool Script::LoadDayNightTimeline(const std::string& assetPath, bool play) {
    auto& runtime = DayNightTimelineRuntime::Instance(); std::string error;
    if (!runtime.Load(assetPath, &error)) return false;
    if (play) runtime.Play(); else runtime.Pause(); return true;
}
void Script::PlayDayNightTimeline() { DayNightTimelineRuntime::Instance().Play(); }
void Script::PauseDayNightTimeline() { DayNightTimelineRuntime::Instance().Pause(); }
void Script::StopDayNightTimeline() { DayNightTimelineRuntime::Instance().Stop(); }
void Script::SetDayNightTime(float time) { DayNightTimelineRuntime::Instance().SetTime(time); }
float Script::DayNightTime() const { return DayNightTimelineRuntime::Instance().Time(); }
void Script::SetDayNightPlaybackRate(float rate) { DayNightTimelineRuntime::Instance().SetPlaybackRate(rate); }
bool Script::WasDayNightEvent(const std::string& name) {
    static std::vector<std::string> frameEvents;
    auto fresh = DayNightTimelineRuntime::Instance().TakeEvents();
    frameEvents.insert(frameEvents.end(), fresh.begin(), fresh.end());
    const auto it = std::find(frameEvents.begin(), frameEvents.end(), name);
    if (it == frameEvents.end()) return false; frameEvents.erase(it); return true;
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

glm::vec3 Script::SplineClosestPoint(ecs::Entity spline, const glm::vec3& world,
                                     const glm::vec3& fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || component->points.size() < 2) return fallback;
    return Spline(component->points, component->closed).ClosestPoint(world);
}

float Script::SplineClosestDistance(ecs::Entity spline, const glm::vec3& world,
                                    float fallback) const {
    const ecs::SplineComponent* component = TryGet<ecs::SplineComponent>(spline);
    if (!component || component->points.size() < 2) return fallback;
    float distance = fallback;
    Spline(component->points, component->closed).ClosestPoint(world, &distance);
    return distance;
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
bool Script::ConfigureSaveProfile(const std::string& assetPath) {
    return engine::ConfigureSaveProfile(assetPath);
}
bool Script::RespawnFromCheckpoint() {
    return m_context.registry
        && engine::RespawnFromLastCheckpoint(*m_context.registry, m_context.entity);
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
    if (m_nextSequenceId <= 0) m_nextSequenceId = 1;
    m_sequences.push_back(std::make_unique<ScriptSequence>(m_nextSequenceId++));
    return *m_sequences.back();
}

ScriptSequence* Script::FindSequence(int handle) {
    if (handle <= 0) return nullptr;
    const auto found = std::find_if(m_sequences.begin(), m_sequences.end(),
        [handle](const std::unique_ptr<ScriptSequence>& sequence) {
            return sequence && sequence->Handle() == handle;
        });
    return found == m_sequences.end() ? nullptr : found->get();
}

const ScriptSequence* Script::FindSequence(int handle) const {
    if (handle <= 0) return nullptr;
    const auto found = std::find_if(m_sequences.begin(), m_sequences.end(),
        [handle](const std::unique_ptr<ScriptSequence>& sequence) {
            return sequence && sequence->Handle() == handle;
        });
    return found == m_sequences.end() ? nullptr : found->get();
}

bool Script::CancelSequence(int handle) {
    ScriptSequence* sequence = FindSequence(handle);
    if (!sequence || sequence->Done()) return false;
    sequence->Cancel();
    return true;
}

bool Script::PauseSequence(int handle) {
    ScriptSequence* sequence = FindSequence(handle);
    if (!sequence || sequence->Done() || sequence->Paused()) return false;
    sequence->Pause();
    return true;
}

bool Script::ResumeSequence(int handle) {
    ScriptSequence* sequence = FindSequence(handle);
    if (!sequence || sequence->Done() || !sequence->Paused()) return false;
    sequence->Resume();
    return true;
}

bool Script::IsSequenceActive(int handle) const {
    const ScriptSequence* sequence = FindSequence(handle);
    return sequence && !sequence->Done();
}

bool Script::IsSequencePaused(int handle) const {
    const ScriptSequence* sequence = FindSequence(handle);
    return sequence && sequence->Paused();
}

void Script::CancelAllSequences() {
    for (const std::unique_ptr<ScriptSequence>& sequence : m_sequences)
        if (sequence) sequence->Cancel();
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
        m_registrationErrors.push_back("Script registration has an empty name or factory");
        return;
    }
    if (m_factories.find(className) != m_factories.end()) {
        if (m_strictValidation) {
            m_registrationErrors.push_back("Duplicate script registration: " + className);
            return;
        }
    }
    m_factories[className] = std::move(factory);
}

bool ScriptRegistry::Valid(std::string* error) const {
    if (!m_registrationErrors.empty()) {
        if (error) *error = m_registrationErrors.front();
        return false;
    }
    for (const auto& entry : m_factories) {
        try {
            if (!entry.second || !entry.second()) {
                if (error) *error = "Script factory could not create: " + entry.first;
                return false;
            }
        } catch (const std::exception& exception) {
            if (error) *error = "Script factory '" + entry.first + "' failed: " + exception.what();
            return false;
        } catch (...) {
            if (error) *error = "Script factory '" + entry.first + "' failed validation";
            return false;
        }
    }
    return true;
}

bool ScriptRegistry::Has(const std::string& className) const {
    return m_factories.find(className) != m_factories.end();
}

std::unique_ptr<Script> ScriptRegistry::Create(const std::string& className) const {
    const auto it = m_factories.find(className);
    return it == m_factories.end() ? nullptr : it->second();
}

std::vector<std::string> ScriptRegistry::Names() const {
    std::vector<std::string> names;
    names.reserve(m_factories.size());
    for (const auto& entry : m_factories) names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}

void ScriptRegistry::Remove(const std::string& className) {
    m_factories.erase(className);
}

ScriptRegistry ScriptRegistry::Extract(const std::vector<std::string>& classNames) {
    ScriptRegistry result;
    for (const std::string& name : classNames) {
        const auto found = m_factories.find(name);
        if (found == m_factories.end()) continue;
        result.m_factories.emplace(name, std::move(found->second));
        m_factories.erase(found);
    }
    return result;
}

void ScriptRegistry::MergeFrom(ScriptRegistry&& other) {
    for (auto& entry : other.m_factories) {
        m_factories[entry.first] = std::move(entry.second);
    }
    other.m_factories.clear();
    other.m_registrationErrors.clear();
}

const char* ScriptCallbackKindName(ScriptCallbackKind callback) {
    switch (callback) {
    case ScriptCallbackKind::OnCreate: return "OnCreate";
    case ScriptCallbackKind::OnEnable: return "OnEnable";
    case ScriptCallbackKind::OnDisable: return "OnDisable";
    case ScriptCallbackKind::OnFixedUpdate: return "OnFixedUpdate";
    case ScriptCallbackKind::OnEvent: return "OnEvent";
    case ScriptCallbackKind::OnScriptCall: return "OnScriptCall";
    default: return "OnUpdate";
    }
}

void SetScriptDebuggingEnabled(bool enabled) {
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    debugger.enabled = enabled;
    if (!enabled) {
        debugger.paused = false;
        debugger.stepRequested = false;
        debugger.stopReason.clear();
    }
}

void SetScriptExecutionPaused(bool paused) {
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    debugger.paused = paused;
    debugger.stepRequested = false;
    debugger.stopReason = paused ? "Paused by user" : std::string();
}

void RequestScriptExecutionStep() {
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    debugger.enabled = true;
    debugger.paused = true;
    debugger.stepRequested = true;
    debugger.stopReason = "Waiting to step one callback";
}

void SetScriptCallbackBreakpoint(const std::string& className,
                                 ScriptCallbackKind callback, bool enabled) {
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    const std::string key = ScriptDebugKey(className, callback);
    if (enabled && !className.empty()) debugger.breakpoints.insert(key);
    else debugger.breakpoints.erase(key);
}

bool HasScriptCallbackBreakpoint(const std::string& className,
                                 ScriptCallbackKind callback) {
    return ScriptDebugger().breakpoints.count(ScriptDebugKey(className, callback)) != 0;
}

void ClearScriptCallbackBreakpoints() { ScriptDebugger().breakpoints.clear(); }
void ClearScriptExecutionStatistics() { ScriptDebugger().statistics.clear(); }

ScriptDebugState GetScriptDebugState() {
    const RuntimeScriptDebugger& debugger = ScriptDebugger();
    ScriptDebugState state;
    state.enabled = debugger.enabled;
    state.paused = debugger.paused;
    state.stopReason = debugger.stopReason;
    state.statistics.reserve(debugger.statistics.size());
    for (const auto& entry : debugger.statistics) state.statistics.push_back(entry.second);
    std::sort(state.statistics.begin(), state.statistics.end(),
        [](const ScriptExecutionStat& a, const ScriptExecutionStat& b) {
            if (a.maximumMilliseconds != b.maximumMilliseconds)
                return a.maximumMilliseconds > b.maximumMilliseconds;
            if (a.className != b.className) return a.className < b.className;
            return static_cast<int>(a.callback) < static_cast<int>(b.callback);
        });
    return state;
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

template <class Fn>
bool RunDebugged(ecs::Entity entity, NativeScriptSlot& script,
                 ScriptCallbackKind callback, Fn&& fn) {
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    if (!debugger.enabled) {
        RunGuarded(script, std::forward<Fn>(fn));
        return true;
    }

    const bool stepping = debugger.paused && debugger.stepRequested;
    if (debugger.paused && !stepping) return false;
    if (stepping) debugger.stepRequested = false;

    const auto start = std::chrono::high_resolution_clock::now();
    RunGuarded(script, std::forward<Fn>(fn));
    const double elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - start).count();
    ScriptExecutionStat& stat = debugger.statistics[
        ScriptStatKey(entity, script.className, callback)];
    stat.entity = entity;
    stat.className = script.className;
    stat.callback = callback;
    stat.lastMilliseconds = elapsed;
    stat.maximumMilliseconds = std::max(stat.maximumMilliseconds, elapsed);
    ++stat.callCount;
    stat.averageMilliseconds += (elapsed - stat.averageMilliseconds)
        / static_cast<double>(stat.callCount);

    const bool hitBreakpoint = debugger.breakpoints.count(
        ScriptDebugKey(script.className, callback)) != 0;
    if (stepping || hitBreakpoint) {
        debugger.paused = true;
        debugger.stopReason = (stepping ? "Stepped " : "Breakpoint: ")
            + script.className + "::" + ScriptCallbackKindName(callback)
            + " on entity " + std::to_string(entity);
    }
    return true;
}

void ObserveHealthEvents(ecs::Registry& registry) {
    auto& observed = g_observedHealth[&registry];
    std::unordered_set<ecs::Entity> present;
    registry.view<Health>().each([&](ecs::Entity entity, Health& health) {
        present.insert(entity);
        const bool alive = health.alive && health.hp > 0.0f;
        const ObservedHealth current{health.hp, health.maxHp, alive};
        const auto previous = observed.find(entity);
        if (previous != observed.end()) {
            auto emit = [&](const char* name) {
                ScriptEvent event;
                event.name = name;
                event.target = entity;
                event.floats["health"] = health.hp;
                event.floats["maxHealth"] = health.maxHp;
                event.floats["delta"] = health.hp - previous->second.hp;
                QueueScriptEvent(registry, std::move(event));
            };
            if (health.hp < previous->second.hp) emit("health.damage");
            else if (health.hp > previous->second.hp) emit("health.healed");
            if (previous->second.alive && !alive) emit("health.death");
            else if (!previous->second.alive && alive) emit("health.revived");
        }
        observed[entity] = current;
    });
    for (auto it = observed.begin(); it != observed.end();) {
        if (present.count(it->first) == 0) it = observed.erase(it);
        else ++it;
    }
}

struct OrderedScriptSlot {
    ecs::Entity entity = ecs::kNull;
    NativeScriptSlot* slot = nullptr;
    int attachmentIndex = 0;
};

std::vector<OrderedScriptSlot> BuildScriptExecutionOrder(ecs::Registry& registry) {
    std::vector<OrderedScriptSlot> nodes;
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& component) {
            nodes.push_back({entity, &component, 0});
            for (std::size_t i = 0; i < component.additional.size(); ++i)
                nodes.push_back({entity, &component.additional[i], static_cast<int>(i) + 1});
        });
    std::stable_sort(nodes.begin(), nodes.end(),
        [](const OrderedScriptSlot& a, const OrderedScriptSlot& b) {
            if (a.slot->executionOrder != b.slot->executionOrder)
                return a.slot->executionOrder < b.slot->executionOrder;
            if (a.entity != b.entity) return a.entity < b.entity;
            return a.attachmentIndex < b.attachmentIndex;
        });

    std::unordered_map<std::string, std::vector<std::size_t>> providers;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        NativeScriptSlot& slot = *nodes[i].slot;
        slot.dependencyError.clear();
        if (slot.enabled && !slot.className.empty()) providers[slot.className].push_back(i);
    }

    std::vector<std::vector<std::size_t>> outgoing(nodes.size());
    std::vector<int> indegree(nodes.size(), 0);
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        NativeScriptSlot& slot = *nodes[i].slot;
        if (!slot.enabled || slot.className.empty()) continue;
        for (const std::string& dependency : slot.dependencies) {
            if (dependency.empty()) continue;
            const auto found = providers.find(dependency);
            if (found == providers.end()) {
                if (!slot.dependencyError.empty()) slot.dependencyError += ", ";
                slot.dependencyError += "missing " + dependency;
                continue;
            }
            for (const std::size_t provider : found->second) {
                if (provider == i) continue;
                outgoing[provider].push_back(i);
                ++indegree[i];
            }
        }
    }

    std::vector<OrderedScriptSlot> ordered;
    ordered.reserve(nodes.size());
    std::vector<bool> emitted(nodes.size(), false);
    while (ordered.size() < nodes.size()) {
        std::size_t ready = nodes.size();
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (!emitted[i] && indegree[i] == 0) { ready = i; break; }
        }
        if (ready == nodes.size()) break;
        emitted[ready] = true;
        ordered.push_back(nodes[ready]);
        for (const std::size_t dependent : outgoing[ready]) --indegree[dependent];
    }
    if (ordered.size() != nodes.size()) {
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            if (emitted[i]) continue;
            if (!nodes[i].slot->dependencyError.empty())
                nodes[i].slot->dependencyError += ", ";
            nodes[i].slot->dependencyError += "circular dependency";
            ordered.push_back(nodes[i]); // deterministic fallback for diagnostics
        }
    }
    for (OrderedScriptSlot& node : nodes) {
        NativeScriptSlot& slot = *node.slot;
        if (slot.dependencyError != slot.reportedDependencyError) {
            if (!slot.dependencyError.empty()) {
                ReportScriptError("'" + slot.className + "' dependency error: "
                                  + slot.dependencyError);
            }
            slot.reportedDependencyError = slot.dependencyError;
        }
    }
    return ordered;
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
        if (!RunDebugged(entity, script, ScriptCallbackKind::OnCreate,
                [&] { script.instance->OnCreate(); })) {
            return nullptr;
        }
        script.created = true;
        if (script.enabled && script.restoreReloadState) {
            RunGuarded(script, [&] { script.instance->OnAfterHotReload(script.reloadState); });
            script.reloadState.clear();
            script.restoreReloadState = false;
        }
    }
    if (script.enabled && script.created && script.restorePersistentState) {
        RunGuarded(script, [&] {
            script.instance->OnLoadState(
                script.persistentStateVersion, script.persistentState);
        });
        script.persistentState.clear();
        script.restorePersistentState = false;
    }
    return script.enabled ? script.instance.get() : nullptr;
}

bool ScriptDependenciesReady(const NativeScriptSlot& slot,
                             const std::vector<OrderedScriptSlot>& ordered) {
    for (const std::string& dependency : slot.dependencies) {
        if (dependency.empty() || dependency == slot.className) continue;
        const bool ready = std::any_of(ordered.begin(), ordered.end(),
            [&dependency](const OrderedScriptSlot& candidate) {
                return candidate.slot->enabled && candidate.slot->active
                    && candidate.slot->className == dependency
                    && candidate.slot->created && candidate.slot->instance;
            });
        if (!ready) return false;
    }
    return true;
}

void SetRuntimeScriptContext(ecs::Registry& registry, ecs::Entity entity,
                             NativeScriptSlot& slot,
                             std::vector<ecs::Entity>* destroyQueue,
                             const ScriptInputState* input,
                             RuntimeAudioSystem* audio, CameraShake* cameraShake,
                             CameraDirector* cameraDirector, GameMode* gameMode,
                             PhysicsWorld* physics) {
    if (!slot.instance) return;
    slot.instance->SetContext(
        ScriptContext{&registry, entity, destroyQueue, input, audio, cameraShake,
                      cameraDirector, &slot.fields, gameMode, nullptr, physics});
}

void DeactivateScript(ecs::Registry& registry, ecs::Entity entity,
                      NativeScriptSlot& slot,
                      std::vector<ecs::Entity>* destroyQueue = nullptr,
                      const ScriptInputState* input = nullptr,
                      RuntimeAudioSystem* audio = nullptr,
                      CameraShake* cameraShake = nullptr,
                      CameraDirector* cameraDirector = nullptr,
                      GameMode* gameMode = nullptr,
                      PhysicsWorld* physics = nullptr) {
    if (!slot.active || !slot.instance || !slot.created) {
        slot.active = false;
        return;
    }
    SetRuntimeScriptContext(registry, entity, slot, destroyQueue, input, audio,
                            cameraShake, cameraDirector, gameMode, physics);
    RunDebugged(entity, slot, ScriptCallbackKind::OnDisable,
                [&] { slot.instance->OnDisable(); });
    slot.active = false;
}

// Teardown cannot be paused by the callback debugger: once destruction has been
// committed, OnDisable must run before the instance and its module-owned vtable
// disappear.
void ForceDeactivateScript(ecs::Registry& registry, ecs::Entity entity,
                           NativeScriptSlot& slot) {
    if (!slot.active || !slot.instance || !slot.created) {
        slot.active = false;
        return;
    }
    SetRuntimeScriptContext(registry, entity, slot, nullptr, nullptr, nullptr,
                            nullptr, nullptr, nullptr, nullptr);
    RunGuarded(slot, [&] { slot.instance->OnDisable(); });
    slot.active = false;
}

// Destroy queued entities, giving each scripted entity an OnDestroy() first.
void FlushDestroyQueue(ecs::Registry& registry, const std::vector<ecs::Entity>& destroyQueue) {
    for (ecs::Entity entity : destroyQueue) {
        if (!registry.Valid(entity)) {
            continue;
        }
        if (NativeScriptComponent* script = registry.TryGet<NativeScriptComponent>(entity)) {
            ForceDeactivateScript(registry, entity, *script);
            if (script->instance && script->created) {
                RunGuarded(*script, [&] { script->instance->OnDestroy(); });
            }
            for (NativeScriptSlot& additional : script->additional) {
                ForceDeactivateScript(registry, entity, additional);
                if (additional.instance && additional.created) {
                    RunGuarded(additional, [&] { additional.instance->OnDestroy(); });
                }
            }
        }
        registry.Destroy(entity);
    }
}

} // namespace

void ScriptEventDispatcher::Dispatch(
    ecs::Registry& registry, std::vector<ecs::Entity>& destroyQueue,
    const ScriptInputState* input, RuntimeAudioSystem* audio,
    CameraShake* cameraShake, CameraDirector* cameraDirector,
    GameMode* gameMode, PhysicsWorld* physics) {
    std::vector<ScriptEvent> pending;
    for (auto it = g_scriptEvents.begin(); it != g_scriptEvents.end();) {
        if (it->registry == &registry) {
            pending.push_back(std::move(it->event));
            it = g_scriptEvents.erase(it);
        } else {
            ++it;
        }
    }

    for (const ScriptEvent& event : pending) {
        for (const OrderedScriptSlot& node : BuildScriptExecutionOrder(registry)) {
            NativeScriptSlot& slot = *node.slot;
            if (event.target != ecs::kNull && event.target != node.entity) continue;
            if (!slot.enabled || !slot.active || !slot.created || !slot.instance
                || !slot.dependencyError.empty()
                || !slot.instance->WantsEvent(event.name)) continue;
            slot.instance->SetContext(
                ScriptContext{&registry, node.entity, &destroyQueue, input, audio,
                              cameraShake, cameraDirector, &slot.fields, gameMode,
                              nullptr, physics});
            RunDebugged(node.entity, slot, ScriptCallbackKind::OnEvent,
                [&] { slot.instance->DispatchEvent(event); });
        }
    }
}

bool ScriptCallDispatcher::Invoke(
    const ScriptContext& caller, const ScriptHandle& handle,
    const std::string& functionName, ScriptEvent arguments, ScriptEvent* result) {
    if (!caller.registry || !handle || functionName.empty()
        || !caller.registry->Valid(handle.entity)) return false;
    NativeScriptComponent* component =
        caller.registry->TryGet<NativeScriptComponent>(handle.entity);
    if (!component) return false;
    NativeScriptSlot* target = nullptr;
    auto choose = [&](NativeScriptSlot& slot) {
        if (!target && slot.enabled && slot.className == handle.className) target = &slot;
    };
    choose(*component);
    for (NativeScriptSlot& additional : component->additional) choose(additional);
        if (!target || !target->active || !target->created || !target->instance
        || !target->dependencyError.empty()) return false;

    thread_local std::vector<std::string> activeCalls;
    const std::string callKey = std::to_string(handle.entity) + '#'
        + handle.className + '#' + functionName;
    if (activeCalls.size() >= 32
        || std::find(activeCalls.begin(), activeCalls.end(), callKey) != activeCalls.end()) {
        ReportScriptError("Blocked recursive script call: " + handle.className
                          + "::" + functionName);
        return false;
    }

    if (arguments.name.empty()) arguments.name = functionName;
    if (arguments.sender == ecs::kNull) arguments.sender = caller.entity;
    arguments.target = handle.entity;
    ScriptEvent callResult;
    callResult.name = functionName + ".result";
    callResult.sender = handle.entity;
    callResult.target = caller.entity;
    bool handled = false;
    activeCalls.push_back(callKey);
    target->instance->SetContext(
        ScriptContext{caller.registry, handle.entity, caller.destroyQueue, caller.input,
                      caller.audio, caller.cameraShake, caller.cameraDirector,
                      &target->fields, caller.gameMode, caller.sceneLoadRequest,
                      caller.physics});
    const bool ran = RunDebugged(handle.entity, *target, ScriptCallbackKind::OnScriptCall,
        [&] { handled = target->instance->InvokeScriptFunction(
            functionName, arguments, callResult); });
    activeCalls.pop_back();
    if (ran && handled && result) *result = std::move(callResult);
    return ran && handled;
}

void UpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input,
                   RuntimeAudioSystem* audio, CameraShake* cameraShake,
                   CameraDirector* cameraDirector, GameMode* gameMode,
                   PhysicsWorld* physics) {
    thread_local std::vector<ecs::Entity> destroyQueueStorage;
    auto& destroyQueue = destroyQueueStorage;
    destroyQueue.clear();
    ObserveHealthEvents(registry);
    const std::vector<OrderedScriptSlot> ordered = BuildScriptExecutionOrder(registry);
    for (const OrderedScriptSlot& node : ordered) {
        NativeScriptSlot& slot = *node.slot;
        const bool shouldBeActive = slot.enabled && slot.dependencyError.empty()
            && ScriptDependenciesReady(slot, ordered);
        if (!shouldBeActive) {
            DeactivateScript(registry, node.entity, slot, &destroyQueue, input, audio,
                             cameraShake, cameraDirector, gameMode, physics);
            continue;
        }
        if (Script* instance = PrepareScript(
                registry, node.entity, slot, destroyQueue, input, audio,
                cameraShake, cameraDirector, gameMode, physics)) {
            if (!slot.active) {
                if (!RunDebugged(node.entity, slot, ScriptCallbackKind::OnEnable,
                        [&] { instance->OnEnable(); })) continue;
                slot.active = true;
            }
            // OnCreate/OnEnable may safely request deactivation. The matching
            // OnDisable is delivered at the next callback boundary.
            if (!slot.enabled || !slot.active) continue;
            RunDebugged(node.entity, slot, ScriptCallbackKind::OnUpdate, [&] {
                instance->TickTimers(dt);
                instance->OnUpdate(dt);
            });
        }
    }
    ScriptEventDispatcher::Dispatch(registry, destroyQueue, input, audio, cameraShake,
                                    cameraDirector, gameMode, physics);
    FlushDestroyQueue(registry, destroyQueue);
}

bool RecreateScriptsAfterHotReload(ecs::Registry& registry,
                                   RuntimeAudioSystem* audio,
                                   CameraShake* cameraShake,
                                   CameraDirector* cameraDirector,
                                   GameMode* gameMode,
                                   PhysicsWorld* physics,
                                   std::string* error) {
    thread_local std::vector<ecs::Entity> destroyQueueStorage;
    auto& destroyQueue = destroyQueueStorage;
    destroyQueue.clear();
    RuntimeScriptDebugger& debugger = ScriptDebugger();
    const bool debuggingWasEnabled = debugger.enabled;
    const bool executionWasPaused = debugger.paused;
    debugger.enabled = false;
    debugger.paused = false;
    bool success = true;
    const std::vector<OrderedScriptSlot> ordered = BuildScriptExecutionOrder(registry);
    for (const OrderedScriptSlot& node : ordered) {
        NativeScriptSlot& slot = *node.slot;
        if (!slot.enabled || slot.className.empty() || IsLuaScriptPath(slot.sourcePath)) continue;
        Script* instance = PrepareScript(registry, node.entity, slot, destroyQueue, nullptr,
                                         audio, cameraShake, cameraDirector, gameMode, physics);
        if (!instance || !slot.enabled || !slot.created) {
            success = false;
            if (error && error->empty()) {
                *error = "Could not recreate attached script '" + slot.className + "'.";
            }
            continue;
        }
        if (!slot.active) {
            RunGuarded(slot, [&] { instance->OnEnable(); });
            slot.active = slot.enabled;
            if (!slot.active) {
                success = false;
                if (error && error->empty()) {
                    *error = "Script '" + slot.className + "' failed during OnEnable.";
                }
            }
        }
    }
    debugger.enabled = debuggingWasEnabled;
    debugger.paused = executionWasPaused;
    return success;
}

void FixedUpdateScripts(ecs::Registry& registry, float dt, const ScriptInputState* input,
                        RuntimeAudioSystem* audio, CameraShake* cameraShake,
                        CameraDirector* cameraDirector, GameMode* gameMode,
                        PhysicsWorld* physics) {
    thread_local std::vector<ecs::Entity> destroyQueueStorage;
    auto& destroyQueue = destroyQueueStorage;
    destroyQueue.clear();
    const std::vector<OrderedScriptSlot> ordered = BuildScriptExecutionOrder(registry);
    for (const OrderedScriptSlot& node : ordered) {
        NativeScriptSlot& slot = *node.slot;
        const bool shouldBeActive = slot.enabled && slot.dependencyError.empty()
            && ScriptDependenciesReady(slot, ordered);
        if (!shouldBeActive) {
            DeactivateScript(registry, node.entity, slot, &destroyQueue, input, audio,
                             cameraShake, cameraDirector, gameMode, physics);
            continue;
        }
        // Creation + OnCreate/OnEnable happen in UpdateScripts; fixed update only
        // runs fully active scripts.
        if (!slot.active || !slot.instance || !slot.created) continue;
        slot.instance->SetContext(
            ScriptContext{&registry, node.entity, &destroyQueue, input, audio,
                          cameraShake, cameraDirector, &slot.fields, gameMode,
                          nullptr, physics});
        RunDebugged(node.entity, slot, ScriptCallbackKind::OnFixedUpdate,
            [&] { slot.instance->OnFixedUpdate(dt); });
    }
    FlushDestroyQueue(registry, destroyQueue);
}

void ShutdownScripts(ecs::Registry& registry) {
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& script) {
            auto shutdown = [&](NativeScriptSlot& slot) {
                ForceDeactivateScript(registry, entity, slot);
                if (slot.instance && slot.created) {
                    // No destroy queue / input during teardown.
                    slot.instance->SetContext(
                        ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, &slot.fields});
                    RunGuarded(slot, [&] { slot.instance->OnDestroy(); });
                }
                slot.instance.reset();
                slot.created = false;
                slot.active = false;
            };
            shutdown(script);
            for (NativeScriptSlot& additional : script.additional) shutdown(additional);
        });
    g_scriptEvents.erase(
        std::remove_if(g_scriptEvents.begin(), g_scriptEvents.end(),
            [&registry](const QueuedScriptEvent& queued) {
                return queued.registry == &registry;
            }),
        g_scriptEvents.end());
    g_observedHealth.erase(&registry);
}

void PrepareScriptsForHotReload(ecs::Registry& registry) {
    registry.view<NativeScriptComponent>().each(
        [&](ecs::Entity entity, NativeScriptComponent& script) {
            auto capture = [&](NativeScriptSlot& slot) {
                slot.reloadState.clear();
                slot.restoreReloadState = false;
                if (slot.instance && slot.created) {
                    slot.instance->SetContext(
                        ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, &slot.fields});
                    RunGuarded(slot, [&] {
                        slot.instance->OnBeforeHotReload(slot.reloadState);
                    });
                    // Deliver the callback even when the old instance stored no keys; a
                    // replacement may use the hook simply to rebind external resources.
                    slot.restoreReloadState = true;
                }
            };
            capture(script);
            for (NativeScriptSlot& additional : script.additional) capture(additional);
        });
    ShutdownScripts(registry);
    registry.view<NativeScriptComponent>().each(
        [](ecs::Entity, NativeScriptComponent& script) {
            script.missingFactory = false;
            for (NativeScriptSlot& additional : script.additional) {
                additional.missingFactory = false;
            }
        });
}

void ShutdownScripts(ecs::Registry& registry, const std::vector<ecs::Entity>& entities) {
    for (ecs::Entity entity : entities) {
        if (!registry.Valid(entity)) continue;
        NativeScriptComponent* script = registry.TryGet<NativeScriptComponent>(entity);
        if (!script) continue;
        auto shutdown = [&](NativeScriptSlot& slot) {
            ForceDeactivateScript(registry, entity, slot);
            if (slot.instance && slot.created) {
                slot.instance->SetContext(
                    ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, &slot.fields});
                RunGuarded(slot, [&] { slot.instance->OnDestroy(); });
            }
            slot.instance.reset();
            slot.created = false;
            slot.active = false;
        };
        shutdown(*script);
        for (NativeScriptSlot& additional : script->additional) shutdown(additional);
    }
}

namespace {

std::string EncodeScriptStateSegment(const std::string& value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size() * 2);
    for (const unsigned char byte : value) {
        encoded.push_back(kHex[byte >> 4]);
        encoded.push_back(kHex[byte & 0x0F]);
    }
    return encoded;
}

int HexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    return -1;
}

std::string DecodeScriptStateSegment(const std::string& value) {
    if ((value.size() & 1u) != 0u) return {};
    std::string decoded;
    decoded.reserve(value.size() / 2);
    for (std::size_t i = 0; i < value.size(); i += 2) {
        const int high = HexDigit(value[i]);
        const int low = HexDigit(value[i + 1]);
        if (high < 0 || low < 0) return {};
        decoded.push_back(static_cast<char>((high << 4) | low));
    }
    return decoded;
}

std::string ScriptStateBase(const std::string& entityName,
                            const NativeScriptSlot& slot, std::size_t slotIndex) {
    return "__3dg_script_state|" + EncodeScriptStateSegment(entityName) + '|'
        + EncodeScriptStateSegment(slot.className) + '|'
        + std::to_string(slotIndex) + '|';
}

} // namespace

void CaptureScriptPersistentStatesImpl(
    ecs::Registry& registry,
    std::unordered_map<std::string, std::string>& values,
    const std::unordered_set<ecs::Entity>* filter) {
    registry.view<ecs::RuntimeName, NativeScriptComponent>().each(
        [&](ecs::Entity entity, ecs::RuntimeName& runtimeName,
            NativeScriptComponent& component) {
            if (filter && filter->count(entity) == 0) return;
            if (runtimeName.value.empty()) return;
            auto capture = [&](NativeScriptSlot& slot, std::size_t slotIndex) {
                if (slot.className.empty()) return;
                const std::string base =
                    ScriptStateBase(runtimeName.value, slot, slotIndex);
                for (auto it = values.begin(); it != values.end();) {
                    if (it->first.rfind(base, 0) == 0) it = values.erase(it);
                    else ++it;
                }

                Script::StateMap state;
                int version = std::max(slot.persistentStateVersion, 1);
                bool available = false;
                if (slot.instance && slot.created) {
                    slot.instance->SetContext(
                        ScriptContext{&registry, entity, nullptr, nullptr, nullptr,
                                      nullptr, nullptr, &slot.fields});
                    RunGuarded(slot, [&] {
                        version = std::max(slot.instance->PersistentStateVersion(), 0);
                        if (version > 0) {
                            slot.instance->OnSaveState(state);
                            available = true;
                        }
                    });
                } else if (slot.restorePersistentState) {
                    state = slot.persistentState;
                    available = true;
                }
                if (!available) return;

                values[base + 'v'] = std::to_string(version);
                for (const auto& field : state)
                    values[base + "f|" + EncodeScriptStateSegment(field.first)] = field.second;
            };
            capture(component, 0);
            for (std::size_t i = 0; i < component.additional.size(); ++i)
                capture(component.additional[i], i + 1);
        });
}

void RestoreScriptPersistentStatesImpl(
    ecs::Registry& registry,
    const std::unordered_map<std::string, std::string>& values,
    const std::unordered_set<ecs::Entity>* filter) {
    registry.view<ecs::RuntimeName, NativeScriptComponent>().each(
        [&](ecs::Entity entity, ecs::RuntimeName& runtimeName,
            NativeScriptComponent& component) {
            if (filter && filter->count(entity) == 0) return;
            if (runtimeName.value.empty()) return;
            auto restore = [&](NativeScriptSlot& slot, std::size_t slotIndex) {
                if (slot.className.empty()) return;
                const std::string base =
                    ScriptStateBase(runtimeName.value, slot, slotIndex);
                const auto version = values.find(base + 'v');
                if (version == values.end()) return; // old save or non-persistent script
                try {
                    slot.persistentStateVersion = std::max(std::stoi(version->second), 1);
                } catch (...) {
                    slot.persistentStateVersion = 1;
                }
                slot.persistentState.clear();
                const std::string fieldPrefix = base + "f|";
                for (const auto& value : values) {
                    if (value.first.rfind(fieldPrefix, 0) != 0) continue;
                    const std::string name = DecodeScriptStateSegment(
                        value.first.substr(fieldPrefix.size()));
                    if (!name.empty()) slot.persistentState[name] = value.second;
                }
                slot.restorePersistentState = true;
            };
            restore(component, 0);
            for (std::size_t i = 0; i < component.additional.size(); ++i)
                restore(component.additional[i], i + 1);
        });
}

void CaptureScriptPersistentStates(
    ecs::Registry& registry,
    std::unordered_map<std::string, std::string>& values) {
    CaptureScriptPersistentStatesImpl(registry, values, nullptr);
}

void CaptureScriptPersistentStates(
    ecs::Registry& registry, const std::vector<ecs::Entity>& entities,
    std::unordered_map<std::string, std::string>& values) {
    const std::unordered_set<ecs::Entity> filter(entities.begin(), entities.end());
    CaptureScriptPersistentStatesImpl(registry, values, &filter);
}

void RestoreScriptPersistentStates(
    ecs::Registry& registry,
    const std::unordered_map<std::string, std::string>& values) {
    RestoreScriptPersistentStatesImpl(registry, values, nullptr);
}

void RestoreScriptPersistentStates(
    ecs::Registry& registry, const std::vector<ecs::Entity>& entities,
    const std::unordered_map<std::string, std::string>& values) {
    const std::unordered_set<ecs::Entity> filter(entities.begin(), entities.end());
    RestoreScriptPersistentStatesImpl(registry, values, &filter);
}

std::vector<ScriptTestResult> RunScriptTests(
    const std::vector<std::string>& luaSourcePaths) {
    struct Source {
        std::string suite;
        std::function<std::unique_ptr<Script>()> create;
    };
    std::vector<Source> sources;
    for (const std::string& className : ScriptRegistry::Instance().Names()) {
        sources.push_back(Source{className, [className] {
            return ScriptRegistry::Instance().Create(className);
        }});
    }
    for (const std::string& path : luaSourcePaths) {
        if (!IsLuaScriptPath(path)) continue;
        const std::filesystem::path sourcePath(path);
        sources.push_back(Source{
            sourcePath.stem().string().empty() ? path : sourcePath.stem().string(),
            [path] { return std::make_unique<LuaScript>(path); }});
    }

    std::vector<ScriptTestResult> results;
    for (const Source& source : sources) {
        std::vector<std::string> testNames;
        try {
            std::unique_ptr<Script> discovery = source.create();
            if (!discovery) continue;
            ScriptTestSuite suite;
            discovery->DefineTests(suite);
            for (const ScriptTestSuite::TestCase& test : suite.Tests())
                testNames.push_back(test.name);
        } catch (const std::exception& error) {
            results.push_back(ScriptTestResult{
                source.suite, "Discovery", false, 0, 0.0,
                {std::string("Test discovery failed: ") + error.what()}});
            continue;
        } catch (...) {
            results.push_back(ScriptTestResult{
                source.suite, "Discovery", false, 0, 0.0,
                {"Test discovery failed with an unknown exception"}});
            continue;
        }

        for (const std::string& testName : testNames) {
            ScriptTestResult result;
            result.suite = source.suite;
            result.name = testName;
            const auto start = std::chrono::high_resolution_clock::now();
            std::unique_ptr<ecs::Registry> isolatedRegistry;
            try {
                std::unique_ptr<Script> script = source.create();
                if (!script) throw std::runtime_error("Could not create test script");
                isolatedRegistry = std::make_unique<ecs::Registry>();
                const ecs::Entity entity = isolatedRegistry->Create();
                isolatedRegistry->Add<ecs::RuntimeName>(
                    entity, ecs::RuntimeName{"ScriptTestEntity"});
                isolatedRegistry->Add<ecs::Transform>(entity);
                std::vector<ecs::Entity> destroyQueue;
                script->SetContext(
                    ScriptContext{isolatedRegistry.get(), entity, &destroyQueue});

                ScriptTestSuite suite;
                script->DefineTests(suite);
                const auto test = std::find_if(
                    suite.Tests().begin(), suite.Tests().end(),
                    [&testName](const ScriptTestSuite::TestCase& candidate) {
                        return candidate.name == testName;
                    });
                if (test == suite.Tests().end())
                    throw std::runtime_error("Test disappeared during isolated discovery");
                ScriptTestContext context;
                test->function(context);
                result.assertions = context.AssertionCount();
                result.failures = context.Failures();
                result.passed = context.Passed();
            } catch (const std::exception& error) {
                result.passed = false;
                result.failures.push_back(error.what());
            } catch (...) {
                result.passed = false;
                result.failures.push_back("Unknown exception");
            }
            if (isolatedRegistry) {
                ecs::Registry* registry = isolatedRegistry.get();
                g_scriptEvents.erase(
                    std::remove_if(g_scriptEvents.begin(), g_scriptEvents.end(),
                        [registry](const QueuedScriptEvent& event) {
                            return event.registry == registry;
                        }),
                    g_scriptEvents.end());
                g_observedHealth.erase(registry);
            }
            result.milliseconds = std::chrono::duration<double, std::milli>(
                std::chrono::high_resolution_clock::now() - start).count();
            results.push_back(std::move(result));
        }
    }
    std::sort(results.begin(), results.end(),
        [](const ScriptTestResult& a, const ScriptTestResult& b) {
            return a.suite != b.suite ? a.suite < b.suite : a.name < b.name;
        });
    return results;
}

std::string ConsumeScriptSceneLoadRequest() {
    std::string request = std::move(g_scriptSceneLoadRequest);
    g_scriptSceneLoadRequest.clear();
    return request;
}

void QueueScriptSceneLoadRequest(const std::string& runtimeScenePath) {
    if (!runtimeScenePath.empty()) g_scriptSceneLoadRequest = runtimeScenePath;
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
