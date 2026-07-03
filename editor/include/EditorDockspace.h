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
    struct Context {
        EditorPanels* panels = nullptr;
        EditorScene* scene = nullptr;
        EditorAssets* assets = nullptr;
        EditorDragDrop* dragDrop = nullptr;
        const EditorProject* project = nullptr;
        EditorLog* log = nullptr;
        EditorGizmo* gizmo = nullptr;
        const char* modeName = "Edit";
        char* scenePathBuffer = nullptr;
        std::size_t scenePathBufferSize = 0;
        float fps = 0.0f;
        bool sceneDirty = false;
        bool viewportDropRequested = false;
        bool saveSceneRequested = false;
        bool saveAsSceneRequested = false;
        bool loadSceneRequested = false;
        bool exportRuntimeRequested = false;
        bool validateRuntimeRequested = false;
        int recentSceneRequested = -1;
        bool addDirectionalLightRequested = false;
        bool addPointLightRequested = false;
        bool addSpotLightRequested = false;
        bool addAreaLightRequested = false;
    };

    bool Draw(Context& context);
    bool IsCompiledWithImGui() const;
};