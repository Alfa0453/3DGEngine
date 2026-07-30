#pragma once

#include "EditorScene.h"
#include <engine/assets/AssetIdentity.h>

#include <string>

// A reusable object template ("prefab"). It captures a scene object's component
// configuration so you can author it once in the Prefab Editor and stamp out linked
// instances anywhere. Each instance keeps its own world Transform — that lives in the
// ECS Transform component, NOT in EditorScene::Object — so ApplyTo copies component
// configuration only and never moves an instance.
//
// This mirrors the CharacterAsset pattern (Capture / Apply / Save / Load): the runtime
// and packaged pipeline stay unchanged because a prefab bakes its values onto ordinary
// scene objects, exactly like a character does.
struct PrefabAsset {
    int version = 1;
    engine::AssetHandle assetId;
    std::string name = "Prefab";

    // The captured component template. Identity fields (entity/name) and the per-instance
    // prefab link are ignored on apply; everything else is the reusable configuration.
    EditorScene::Object object;

    // Read a scene object's component configuration into this prefab.
    void Capture(const EditorScene::Object& source) {
        object = source;
        object.entity = engine::ecs::kNull;        // identity is per-instance
        object.characterAssetPath.clear();          // a prefab is not a character link
        object.characterAssetId = engine::AssetHandle{};
    }

    // Stamp this prefab's component configuration onto an existing scene object, keeping
    // that object's identity (entity + name). The world Transform is untouched. Callers
    // are responsible for (re)setting the instance's prefab link after applying.
    void ApplyTo(EditorScene::Object& target) const {
        const engine::ecs::Entity keepEntity = target.entity;
        const std::string keepName = target.name;
        target = object;
        target.entity = keepEntity;
        target.name = keepName;
    }

    // Stamp this prefab's captured components onto the scene's currently selected object
    // through the scene setters (so the ECS side stays in sync). The world transform is
    // left untouched. Returns false if nothing is selected or it is locked.
    bool Apply(EditorScene& scene) const;

    // Persisted as `3DG_PREFAB <version>` + the curated component block.
    bool Save(const std::string& path, std::string* error = nullptr) const;
    bool Load(const std::string& path, std::string* error = nullptr);
};
