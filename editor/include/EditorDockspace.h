#pragma once

#include "EditorAssets.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"

#include <engine/assets/RuntimeAssetManager.h>

#include <cstddef>
#include <string>
#include <vector>

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
        std::vector<EditorScene::AnimationStateTransition> transitions;
        std::vector<ParameterInfo> parameters;
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
        EditorScene* scene = nullptr;
        EditorAssets* assets = nullptr;
        engine::RuntimeAssetManager* runtimeAssets = nullptr;
        EditorDragDrop* dragDrop = nullptr;
        const EditorProject* project = nullptr;
        EditorLog* log = nullptr;
        EditorGizmo* gizmo = nullptr;
        const char* modeName = "Edit";
        bool playMode = false;
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
        bool* physicsEventGuidesSelectedOnly = nullptr;
        bool* physicsEventGuidesTriggersOnly = nullptr;
        bool* physicsEventGuidesEnterExitOnly = nullptr;
        bool clearPhysicsEventGuidesRequested = false;
        char* scenePathBuffer = nullptr;
        std::size_t scenePathBufferSize = 0;
        float fps = 0.0f;
        float* animationPreviewTime = nullptr;
        int* animationActionClip = nullptr;
        float* animationActionFadeIn = nullptr;
        float* animationActionFadeOut = nullptr;
        float* animationActionSpeed = nullptr;
        char* animationActionMaskRoot = nullptr;
        std::size_t animationActionMaskRootSize = 0;
        bool animationActionRequested = false;
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
        bool addCubeRequested = false;
        bool addPlaneRequested = false;
        bool addSphereRequested = false;
        bool addDynamicCubeRequested = false;
        bool addStaticFloorRequested = false;
        bool addTriggerVolumeRequested = false;
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