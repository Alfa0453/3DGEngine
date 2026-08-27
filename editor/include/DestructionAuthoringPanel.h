#pragma once

#include "EditorAssets.h"
#include "EditorScene.h"
#include <engine/assets/DestructionAsset.h>

#include <string>

class DestructionAuthoringPanel {
public:
    struct Result {
        bool captureSelected = false;
        bool buildPreview = false;
        bool removePreview = false;
        bool saved = false;
        std::string message;
    };
    Result Draw(const EditorScene& scene,EditorAssets& assets,
                const std::string& assetRoot,bool* open);
    void QueueOpen(std::string path){m_pendingOpen=std::move(path);}
    const engine::DestructionAssetData& Asset()const{return m_asset;}
    const std::string& Path()const{return m_path;}
    bool IsDirty()const{return m_dirty;}
    bool SaveForShutdown(const std::string& root,std::string* error);
    void Capture(const EditorScene::Object& object,const engine::ecs::Transform& transform);
private:
    void New(const std::string& root);
    bool AssetCombo(const char* label,EditorAssets::Type type,std::string& path,
                    engine::AssetHandle& id,EditorAssets& assets,const char* empty);
    void DrawPreview()const;
    engine::DestructionAssetData m_asset;
    std::string m_path,m_pendingOpen,m_status;
    bool m_dirty=true;
};
