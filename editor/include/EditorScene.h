#pragma once

#include <engine/ecs/Components.h>
#include <engine/ecs/Entity.h>
#include <engine/ecs/Registry.h>
#include <engine/gameplay/GameplayComponents.h>
#include <engine/gameplay/Script.h>
#include <engine/physics/PhysicsComponents.h>

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace engine {
class Mesh;
}

class EditorScene {
public:
    enum class Primitive { Plane, Cube, Sphere };

    enum class TriggerActionMode {
        None = 0,
        Enable = 1,
        Disable = 2,
        Toggle = 3,
    };

    struct PlayerControllerSettings {
        bool firstPerson = false;
        float walkSpeed = 4.0f;
        float runSpeed = 7.0f;
        float jumpSpeed = 5.0f;
        float lookSensitivity = 0.1f;
        float capsuleRadius = 0.4f;
        float capsuleHeight = 1.8f;
        float eyeHeight = 0.6f;
        float cameraDistance = 5.0f;
        float cameraTargetHeight = 1.0f;
        float maxSlopeDegrees = 50.0f;
        float stepHeight = 0.35f;
    };

    using ScriptField = engine::ScriptField;

    struct AnimationEvent {
        int clipIndex = 0;
        float time = 0.0f;
        std::string name;
    };

    struct AnimationActionProfile {
        std::string name = "Action";
        int clipIndex = 0;
        std::string clipName;
        std::string maskRootBone;
        float fadeIn = 0.08f;
        float fadeOut = 0.15f;
        float speed = 1.0f;
    };

    struct AnimationStateNode {
        std::string name = "State";
        int clipIndex = 0;
        std::string clipName;
        bool loop = true;
        float speed = 1.0f;
    };

    struct AnimationStateTransition {
        enum class Compare {
            GreaterOrEqual = 0,
            Less = 1,
            Equal = 2,
            NotEqual = 3
        };

        std::string fromState;
        std::string toState;
        std::string parameter = "Speed";
        Compare compare = Compare::GreaterOrEqual;
        float threshold = 0.0f;
        float fade = 0.2f;
    };

    struct Object {
        engine::ecs::Entity entity = engine::ecs::kNull;
        std::string name;
        Primitive primitive = Primitive::Cube;
        bool light = false;
        engine::ecs::Light lightData;
        bool visible = true;
        bool locked = false;
        std::string modelAssetPath;
        std::string materialAssetPath;
        bool skeletalModel = false;
        int animationClipIndex = 0;
        std::string animationClipName;
        bool animationAutoplay = true;
        bool animationLoop = true;
        float animationSpeed = 1.0f;
        bool animationLocomotionEnabled = false;
        int animationIdleClipIndex = 0;
        int animationWalkClipIndex = 0;
        int animationRunClipIndex = 0;
        std::string animationIdleClipName;
        std::string animationWalkClipName;
        std::string animationRunClipName;
        float animationWalkAt = 0.15f;
        float animationRunAt = 3.0f;
        std::vector<AnimationEvent> animationEvents;
        std::vector<AnimationActionProfile> animationActionProfiles;
        std::vector<AnimationStateNode> animationStates;
        std::vector<AnimationStateTransition> animationTransitions;
        bool linearVelocityEnabled = false;
        bool angularVelocityEnabled = false;
        glm::vec3 linearVelocity{0.0f};
        glm::vec3 angularVelocityAxis{0.0f, 1.0f, 0.0f};
        float angularVelocityRadians = 0.0f;
        bool rigidBodyEnabled = false;
        bool colliderEnabled = false;
        engine::ecs::RigidBody rigidBody;
        engine::ecs::Collider collider;
        bool rotatorEnabled = false;
        engine::ecs::Rotator rotator;
        bool moverEnabled = false;
        engine::ecs::Mover mover;
        std::string triggerTargetName;
        TriggerActionMode triggerEnterMoverAction = TriggerActionMode::None;
        TriggerActionMode triggerEnterRotatorAction = TriggerActionMode::None;
        TriggerActionMode triggerExitMoverAction = TriggerActionMode::None;
        TriggerActionMode triggerExitRotatorAction = TriggerActionMode::None;
        bool playerControllerEnabled = false;
        PlayerControllerSettings playerController;
        bool healthEnabled = false;
        engine::Health health;
        bool scriptEnabled = false;
        std::string scriptClassName;
        std::string scriptPath;
        std::vector<ScriptField> scriptFields;
    };

    struct ObjectSnapshot {
        Object object;
        engine::ecs::Transform transform;
        glm::vec3 color{1.0f};
    };

    struct PhysicsJoint {
        enum class Type {
            Distance,
            Spring
        };

        Type type = Type::Distance;
        bool enabled = true;
        std::string objectA;
        std::string objectB;
        bool worldAnchor = false;
        glm::vec3 anchor{0.0f};
        float restLength = 1.0f;
        bool rope = false;
        float stiffness = 100.0f;
        float damping = 1.0f;
    };

    struct Environment {
        float timeOfDay = 0.46f;
        float skyLightIntensity = 1.0f;
        bool driveSunLight = true;
        float sunIntensity = 1.0f;
        bool showLightGuides = true;
        bool selectedLightGuideOnly = true;
        bool ibl = true;
        bool ssao = false;
        float ssaoRadius = 0.5f;
        float ssaoBias = 0.025f;
        bool ssr = false;
        float ssrIntensity = 0.5f;
        bool directionalShadows = true;
        bool pointShadows = true;
        bool spotShadows = true;
        float shadowSoftness = 2.5f;
        bool  fog = true;
        glm::vec3 fogColor{0.58f, 0.68f, 0.80f};
        float fogDensity = 0.008f;
        float fogHeight = -0.35f;
        float fogHeightFalloff = 0.10f;
        glm::vec3 physicsGravity{0.0f, -9.81f, 0.0f};
        int physicsSolverIterations = 4;
        bool physicsBroadPhase = true;
        float physicsCellSize = 2.0f;
        float physicsRestitutionThreshold = 0.5f;
        bool physicsAllowSleeping = true;
        float physicsSleepLinearVelocity = 0.06f;
        float physicsSleepAngularVelocity = 0.15f;
        float physicsTimeToSleep = 0.5f;
        bool showPhysicsGuides = true;
        bool selectedPhysicsGuideOnly = false;
    };

    struct Snapshot {
        std::vector<ObjectSnapshot> objects;
        std::vector<PhysicsJoint> joints;
        Environment environment;
        int selectedIndex = -1;
        int nextCubeNumber = 1;
    };

    void BuildDefault(const engine::Mesh& Cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool Save(const std::string& path, std::string* error, bool markClean = true);
    bool Load(const std::string& path, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere, std::string* error);

    engine::ecs::Registry& Registry() { return m_registry; }
    const std::vector<Object>& Objects() const { return m_objects; }
    const std::vector<PhysicsJoint>& PhysicsJoints() const { return m_joints; }
    bool IsDirty() const { return m_dirty; }
    void MarkClean() { m_dirty = false; }
    void MarkDirty() { m_dirty = true; }

    int SelectedIndex() const { return m_selectedIndex; }
    const Object* SelectedObject() const;
    engine::ecs::Transform* SelectedTransform();
    const engine::ecs::Transform* TryGetTransform(engine::ecs::Entity entity) const;
    const engine::ecs::MeshRenderer* TryGetMeshRenderer(engine::ecs::Entity entity) const;
    const engine::ecs::Light* TryGetLight(engine::ecs::Entity entity) const;
    const Environment& GetEnvironment() const { return m_environment; }
    bool IsVisible(engine::ecs::Entity entity) const;
    bool SelectedLocked() const;

    void SelectNext();
    void SelectPrevious();
    void SelectIndex(int index);
    void Deselect();
    void MoveSelected(const glm::vec3& delta);
    void RotateSelected(const glm::vec3& axis, float degrees);
    void RotateSelectedYaw(float degrees);
    void ScaleSelectedAxis(const glm::vec3& axis, float factor);
    void ScaleSelected(float factor);
    bool SetSelectedTransform(const engine::ecs::Transform& transform);
    void ResetSelectedTransform();
    void BeginTransformEdit();
    void EndTransformEdit();
    bool Undo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool Redo(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    Snapshot CreateSnapshot();
    void RestoreFromSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    void AddCube(const engine::Mesh& cube);
    void AddPlane(const engine::Mesh& plane);
    void AddSphere(const engine::Mesh& sphere);
    void AddDirectionalLight(const engine::Mesh& placeholderMesh);
    void AddPointLight(const engine::Mesh& placeholderMesh);
    void AddSpotLight(const engine::Mesh& placeholderMesh);
    void AddAreaLight(const engine::Mesh& placeholderMesh);
    bool AddModel(const std::string& path, const engine::Mesh& placeholderMesh, const engine::ecs::Transform& transform);
    bool CycleSelectedColor();
    bool SetSelectedName(const std::string& name);
    bool SetSelectedColor(const glm::vec3& color);
    bool SetSelectedPrimitive(Primitive primitive, const engine::Mesh& mesh);
    bool SetSelectedModelAsset(const std::string& path);
    bool SetSelectedMaterialAsset(const std::string& path);
    bool SetSelectedAnimationSettings(bool skeletalModel,
                                      int clipIndex,
                                      const std::string& clipName,
                                      bool autoplay,
                                      bool loop,
                                      float speed);
    bool SetSelectedAnimationLocomotion(bool enabled,
                                    int idleClipIndex,
                                    const std::string& idleClipName,
                                    int walkClipIndex,
                                    const std::string& walkClipName,
                                    int runClipIndex,
                                    const std::string& runClipName,
                                    float walkAt,
                                    float runAt);
    bool SetSelectedAnimationEvents(const std::vector<AnimationEvent>& events);
    bool SetSelectedAnimationActionProfiles(const std::vector<AnimationActionProfile>& profiles);
    bool SetSelectedAnimationStateGraph(const std::vector<AnimationStateNode>& states,
                                        const std::vector<AnimationStateTransition>& transitions);
    bool SetSelectedLight(const engine::ecs::Light& light);
    void SetEnvironment(const Environment& environment);
    bool SetSelectedLinearVelocityEnabled(bool enabled);
    bool SetSelectedAngularVelocityEnabled(bool enabled);
    bool SetSelectedLinearVelocity(const glm::vec3& velocity);
    bool SetSelectedAngularVelocity(const glm::vec3& axis, float radiansPerSecond);
    bool SetSelectedRigidBodyEnabled(bool enabled);
    bool SetSelectedRigidBody(const engine::ecs::RigidBody& rigidBody);
    bool SetSelectedColliderEnabled(bool enabled);
    bool SetSelectedCollider(const engine::ecs::Collider& collider);
    bool SetSelectedRotatorEnabled(bool enabled);
    bool SetSelectedRotator(const engine::ecs::Rotator& rotator);
    bool SetSelectedMoverEnabled(bool enabled);
    bool SetSelectedMover(const engine::ecs::Mover& mover);
    bool SetSelectedTriggerAction(const std::string& targetName,
                              TriggerActionMode enterMoverAction,
                              TriggerActionMode enterRotatorAction,
                              TriggerActionMode exitMoverAction,
                              TriggerActionMode exitRotatorAction);
    bool SetSelectedPlayerControllerEnabled(bool enabled);
    bool SetSelectedPlayerController(const PlayerControllerSettings& settings);
    bool SetSelectedHealthEnabled(bool enabled);
    bool SetSelectedHealth(const engine::Health& health);
    bool SetSelectedScript(const std::string& className, const std::string& path, bool enabled);
    bool SetSelectedScriptEnabled(bool enabled);
    bool SetSelectedScriptFields(const std::vector<ScriptField>& fields);
    bool AddSelectedScriptField();
    bool SetSelectedScriptField(std::size_t index, const ScriptField& field);
    bool RemoveSelectedScriptField(std::size_t index);
    bool AddPhysicsJoint(const PhysicsJoint& joint);
    bool SetPhysicsJoint(std::size_t index, const PhysicsJoint& joint);
    bool RemovePhysicsJoint(std::size_t index);
    bool ToggleSelectVisible();
    bool ToggleSelectedLocked();
    bool DuplicateSelected(const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    bool DeleteSelected();

private:
    engine::ecs::Entity CreateObject(const std::string& name, Primitive primitive,
                                     const engine::Mesh& mesh, const engine::ecs::Transform& transform,
                                     const glm::vec3& color);
    Snapshot CaptureSnapshot();
    void RestoreSnapshot(const Snapshot& snapshot, const engine::Mesh& cube, const engine::Mesh& plane, const engine::Mesh& sphere);
    void PushUndoSnapshot();
    void ClearHistory();
    void Clear();

    engine::ecs::Registry m_registry;
    std::vector<Object> m_objects;
    std::vector<PhysicsJoint> m_joints;
    std::vector<Snapshot> m_undoStack;
    std::vector<Snapshot> m_redoStack;
    Environment m_environment;
    int m_selectedIndex = -1;
    int m_nextCubeNumber = 1;
    bool m_dirty = false;
    bool m_transformEditOpen = false;
};