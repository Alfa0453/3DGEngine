#pragma once

#include "EditorAssets.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"

#include <cstddef>

class EditorDockspace {
public:
    struct PhysicsEventRow {
        std::string text;
        std::string objectA;
        std::string objectB;
        int phase = 0; // 0 enter, 1 stay, 2 exit
        bool trigger = false;
    };
    
    struct Context {
        EditorPanels* panels = nullptr;
        EditorScene* scene = nullptr;
        EditorAssets* assets = nullptr;
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
        const std::vector<PhysicsEventRow>* physicsEventRows = nullptr;
        bool* showPhysicsEventGuides = nullptr;
        bool* physicsEventGuidesSelectedOnly = nullptr;
        bool* physicsEventGuidesTriggersOnly = nullptr;
        bool* physicsEventGuidesEnterExitOnly = nullptr;
        bool clearPhysicsEventGuidesRequested = false;
        char* scenePathBuffer = nullptr;
        std::size_t scenePathBufferSize = 0;
        float fps = 0.0f;
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