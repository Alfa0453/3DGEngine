#pragma once

#include <engine/assets/RagdollAsset.h>

#include <string>

class EditorScene;
namespace engine { class RuntimeAssetManager; struct Skeleton; }

class RagdollPhysicsPanel {
public:
    void Draw(EditorScene& scene, engine::RuntimeAssetManager& assets,
              const std::string& assetRoot, bool* open,
              bool* assetSaved = nullptr, std::string* message = nullptr);
    void QueueOpen(const std::string& path) { m_queuedPath = path; }

private:
    bool AutoGenerate(const engine::Skeleton& skeleton);
    bool Save(const std::string& assetRoot, std::string* error);
    bool Load(const std::string& path, std::string* error);
    void DrawSkeletonPreview(const engine::Skeleton* skeleton);

    engine::RagdollAssetData m_asset;
    std::string m_path;
    std::string m_queuedPath;
    std::string m_status;
    int m_selectedBody = -1;
    int m_selectedConstraint = -1;
    bool m_dirty = false;
};
