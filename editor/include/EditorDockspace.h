#pragma once

#include "EditorAssets.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"

#include <engine/assets/RuntimeAssetManager.h>
#include <engine/audio/AudioEngine.h>
#include <engine/graphics/CameraShake.h>

#include <cstddef>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine {
class Camera;
}

class EditorDockspace {
public:
    struct PhysicsEventRow {
        std::string text;
        std::string objectA;
        std::string objectB;
        int phase = 0; // 0 enter, 1 stay, 2 exit
        bool trigger = false;
        bool action = false;
    };

    struct GameplayDebugState {
        bool hasSelection = false;
        bool playEntityFound = false;
        bool hasHealth = false;
        bool healthAlive = false;
        bool healthJustDied = false;
        float health = 0.0f;
        float maxHealth = 0.0f;
        bool hasScript = false;
        bool scriptEnabled = false;
        bool scriptCreated = false;
        bool scriptMissingFactory = false;
        std::string selectedName;
        std::string scriptClassName;
        std::string scriptPath;
        int authoredFieldCount = 0;
        int runtimeFieldCount = 0;
        int selectedTriggerTouchCount = 0;
        int selectedTriggerEnterCount = 0;
        int selectedTriggerExitCount = 0;
    };

    struct AnimationPreviewState {
        struct ClipInfo {
            std::string name;
            float durationSeconds = 0.0f;
        };

        struct BoneInfo {
            std::string name;
            int parent = -1;
            int depth = 0;
        };

        struct ParameterInfo {
            std::string name;
            float value = 0.0f;
            EditorScene::AnimationParameter::Type type = EditorScene::AnimationParameter::Type::Float;
        };

        struct TransitionDebugRow {
            std::string fromState;
            std::string toState;
            std::string parameter;
            float value = 0.0f;
            float threshold = 0.0f;
            float exitTime = 0.0f;
            int priority = 0;
            bool canInterrupt = false;
            bool conditionMet = false;
            bool exitTimeReached = false;
            bool blockedByBlend = false;
            bool eligible = false;
            bool selected = false;
        };

        bool hasSelection = false;
        bool skeletalModel = false;
        bool modelLoaded = false;
        bool playMode = false;
        bool runtimeAnimated = false;
        bool locomotionEnabled = false;
        std::string selectedName;
        std::string modelPath;
        std::string loadError;
        std::vector<ClipInfo> clips;
        std::vector<BoneInfo> bones;
        std::vector<EditorScene::AnimationActionProfile> actionProfiles;
        std::vector<EditorScene::AnimationStateNode> states;
        std::vector<EditorScene::AnimationParameter> parameterDefinitions;
        std::vector<EditorScene::AnimationStateTransition> transitions;
        std::vector<std::string> graphWarnings;
        std::vector<ParameterInfo> parameters;
        std::vector<TransitionDebugRow> transitionDebugRows;
        int defaultClipIndex = 0;
        std::string defaultClipName;
        bool autoplay = true;
        bool loop = true;
        float playbackSpeed = 1.0f;
        int idleClipIndex = 0;
        int walkClipIndex = 0;
        int runClipIndex = 0;
        std::string idleClipName;
        std::string walkClipName;
        std::string runClipName;
        float walkAt = 0.15f;
        float runAt = 3.0f;
        std::vector<EditorScene::AnimationEvent> events;
        std::string currentState;
        int currentClip = -1;
        int previousClip = -1;
        float currentTime = 0.0f;
        float previousTime = 0.0f;
        float previewTime = 0.0f;
        float previewDuration = 0.0f;
        bool actionPlaying = false;
        float blend = 1.0f;
        float parameter = 0.0f;
        std::size_t stateCount = 0;
        std::size_t poseBones = 0;
    };
    
    struct Context {
        EditorPanels* panels = nullptr;
        engine::Config* config = nullptr;
        EditorScene* scene = nullptr;
        EditorAssets* assets = nullptr;
        engine::RuntimeAssetManager* runtimeAssets = nullptr;
        EditorDragDrop* dragDrop = nullptr;
        const EditorProject* project = nullptr;
        EditorLog* log = nullptr;
        EditorGizmo* gizmo = nullptr;
        engine::Camera* camera = nullptr;
        bool cameraBlendRequested = false;
        EditorScene::CameraPreset cameraBlendPreset;
        bool cameraShakeRequested = false;
        bool cameraShakeStopRequested = false;
        bool cameraShakeActive = false;
        engine::CameraShakeSettings cameraShakeSettings;
        bool cameraSequencePlayRequested = false;
        bool cameraSequenceStopRequested = false;
        bool cameraSequenceActive = false;
        bool cameraSequenceInputLocked = false;
        bool cameraSequenceSkippable = false;
        std::string cameraSequenceActiveName;
        EditorScene::CameraSequence cameraSequence;
        bool* showCameraRails = nullptr;
        float cameraSequenceTime = 0.0f;
        float cameraSequenceDuration = 0.0f;
        bool cameraSequencePaused = false;
        bool cameraSequencePauseToggleRequested = false;
        bool cameraSequenceSeekRequested = false;
        float cameraSequenceSeekTime = 0.0f;
        const char* modeName = "Edit";
        bool playMode = false;
        bool scriptCompileAndRestartRequested = false;
        bool physicsPaused = false;
        bool physicsPauseToggleRequested = false;
        bool physicsStepRequested = false;
        float physicsFixedTimestep = 0.0f;
        float physicsAccumulator = 0.0f;
        int physicsStepsLastFrame = 0;
        int physicsEventEnterCount = 0;
        int physicsEventStayCount = 0;
        int physicsEventExitCount = 0;
        int physicsActionCount = 0;
        const std::vector<PhysicsEventRow>* physicsEventRows = nullptr;
        GameplayDebugState gameplayDebug;
        AnimationPreviewState animationPreview;
        bool* showPhysicsEventGuides = nullptr;
        bool* showAiDebug = nullptr;
        bool* useNavMesh = nullptr;
        bool*  terrainSculpt = nullptr;
        int*   terrainSculptMode = nullptr;
        int*   terrainPaintLayer = nullptr;
        float* terrainBrushRadius = nullptr;
        float* terrainBrushStrength = nullptr;
        bool* showNavigationPreview = nullptr;
        bool* showGrid = nullptr;            // reference ground grid + world axes
        bool* showParticleDebug = nullptr;
        bool* particleDebugSelectedOnly = nullptr;
        bool* particleDebugShapes = nullptr;
        bool* particleDebugDirections = nullptr;
        bool* particleDebugBounds = nullptr;
        bool* particleDebugCullingState = nullptr;
        int navigationPreviewPolygons = 0;
        bool rebuildNavigationPreviewRequested = false;
        bool* physicsEventGuidesSelectedOnly = nullptr;
        bool* physicsEventGuidesTriggersOnly = nullptr;
        bool* physicsEventGuidesEnterExitOnly = nullptr;
        bool clearPhysicsEventGuidesRequested = false;
        bool vsync = false;                  // current window vsync (filled by the app)
        bool vsyncChangeRequested = false;   // set by the World Settings checkbox
        char* scenePathBuffer = nullptr;
        std::size_t scenePathBufferSize = 0;
        // Project management (New / Open Project).
        bool newProjectRequested = false;
        bool openProjectRequested = false;
        bool browseProjectLocationRequested = false;   // open native folder picker for the location
        bool browseOpenProjectRequested = false;        // open native file picker for a .3dgproject
        char* projectNameBuffer = nullptr;
        std::size_t projectNameBufferSize = 0;
        char* projectLocationBuffer = nullptr;          // display of the chosen location (filled by dialog)
        std::size_t projectLocationBufferSize = 0;
        char* openProjectBuffer = nullptr;
        std::size_t openProjectBufferSize = 0;
        float fps = 0.0f;
        int particleDrawCalls = 0;
        int particleCulledEmitters = 0;
        std::size_t particleRenderedCount = 0;
        double particleCpuMilliseconds = 0.0;
        double particleGpuMilliseconds = 0.0;
        float* animationPreviewTime = nullptr;
        int* animationActionClip = nullptr;
        float* animationActionFadeIn = nullptr;
        float* animationActionFadeOut = nullptr;
        float* animationActionSpeed = nullptr;
        char* animationActionMaskRoot = nullptr;
        std::size_t animationActionMaskRootSize = 0;
        bool animationActionRequested = false;
        bool audioAvailable = false;
        bool previewAudioRequested = false;
        bool stopAudioPreviewRequested = false;
        std::string previewAudioPath;
        float previewAudioVolume = 1.0f;
        float previewAudioPitch = 1.0f;
        bool previewAudioSpatial = false;
        bool previewAudioLoop = false;
        float previewAudioMinDistance = 1.0f;
        float previewAudioMaxDistance = 40.0f;
        float previewAudioRolloff = 1.0f;
        engine::AudioBus previewAudioBus = engine::AudioBus::SFX;
        std::array<float, static_cast<std::size_t>(engine::AudioBus::Count)> audioBusVolumes{};
        std::array<bool, static_cast<std::size_t>(engine::AudioBus::Count)> audioBusMuted{};
        std::array<engine::AudioBusEffects, static_cast<std::size_t>(engine::AudioBus::Count)> audioBusEffects{};
        engine::AudioSnapshotPreset activeAudioSnapshot = engine::AudioSnapshotPreset::Default;
        engine::AudioSnapshotPreset requestedAudioSnapshot = engine::AudioSnapshotPreset::Default;
        bool audioSnapshotRequested = false;
        float audioSnapshotTransition = 0.25f;
        bool dialogueDucking = true;
        engine::AudioEngine::DebugStats audioDebugStats{};
        engine::AudioEngine::DeviceInfo audioDeviceInfo{};
        std::size_t audioMaxVoices = 64;
        bool audioMaxVoicesChanged = false;
        bool saveAudioMixerPresetRequested = false;
        bool loadAudioMixerPresetRequested = false;
        std::array<char, 320> audioMixerPresetPath{};
        bool selectedRuntimeAudioAvailable = false;
        bool selectedRuntimeAudioPlaying = false;
        bool selectedRuntimeAudioPaused = false;
        float selectedRuntimeAudioCursor = 0.0f;
        bool runtimeAudioRestartRequested = false;
        bool runtimeAudioPauseResumeRequested = false;
        bool runtimeAudioStopRequested = false;
        std::unordered_map<std::string, float>* animationPreviewParameters = nullptr;
        bool sceneDirty = false;
        bool viewportDropRequested = false;
        bool newSceneRequested = false;
        bool saveSceneRequested = false;
        bool saveAsSceneRequested = false;
        bool loadSceneRequested = false;
        bool exportRuntimeRequested = false;
        bool validateRuntimeRequested = false;
        bool enterPlayModeRequested = false;
        bool exitPlayModeRequested = false;
        bool undoRequested = false;
        bool redoRequested = false;
        int recentSceneRequested = -1;
        std::string sceneAssetOpenRequested;
        std::string behaviorGraphAssetOpenRequested;
        std::string editorAssetOpenRequested;
        EditorAssets::Type editorAssetOpenType = EditorAssets::Type::Other;
        bool addEmptyRequested = false;
        bool addCubeRequested = false;
        bool addPlaneRequested = false;
        bool addSphereRequested = false;
        bool addCapsuleRequested = false;
        bool addConfiguredPrimitiveRequested = false;
        EditorScene::Primitive configuredPrimitive = EditorScene::Primitive::Cube;
        std::string configuredPrimitiveName;
        engine::ecs::Transform configuredPrimitiveTransform;
        bool configuredPrimitiveColliderEnabled = false;
        engine::ecs::Collider configuredPrimitiveCollider;
        bool addDynamicCubeRequested = false;
        bool addStaticFloorRequested = false;
        bool addTerrainRequested = false;
        bool addWaterRequested = false;
        int  addWaterPreset = 0;            // 0 generic, 1 lake, 2 ocean, 3 river
        bool addSplineRequested = false;
        bool addTriggerVolumeRequested = false;
        bool addNavMeshBoundsVolumeRequested = false;
        bool addPlayerStartRequested = false;
        bool addDoorRequested = false;
        bool addPickupRequested = false;
        bool addDamageZoneRequested = false;
        bool addMovingPlatformRequested = false;
        bool addTriggerMoverTestRequested = false;
        bool addDirectionalLightRequested = false;
        bool addPointLightRequested = false;
        bool addSpotLightRequested = false;
        bool addAreaLightRequested = false;
        bool duplicateSelectedRequested = false;
        bool deleteSelectedRequested = false;
        bool frameSelectedRequested = false;
    };

    bool Draw(Context& context);
    bool IsCompiledWithImGui() const;
};
