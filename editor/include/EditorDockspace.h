#pragma once

#include "EditorAssets.h"
#include "EditorDragDrop.h"
#include "EditorGizmo.h"
#include "EditorLog.h"
#include "EditorPanels.h"
#include "EditorProject.h"
#include "EditorScene.h"

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
        float fps = 0.0f;
        bool sceneDirty = false;
        bool viewportDropRequested = false;
    };

    bool Draw(Context& context);
    bool IsCompiledWithImGui() const;
};