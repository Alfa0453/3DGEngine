#pragma once

#include "EditorAssets.h"
#include <engine/assets/QuestAsset.h>

#include <string>
#include <vector>

class QuestEditorPanel {
public:
    struct Result { bool grantSelected=false,saved=false;std::string message; };
    Result Draw(EditorAssets& assets,const std::string& root,bool* open);
    void QueueOpen(std::string path){m_pendingOpen=std::move(path);}
    const engine::QuestAssetData& Asset()const{return m_asset;}
    const std::string& Path()const{return m_path;}
    bool IsDirty()const{return m_dirty;}
    bool SaveForShutdown(const std::string& root,std::string* error);
private:
    void New(const std::string& root);
    bool AssetCombo(const char* label,EditorAssets::Type type,std::string& path,engine::AssetHandle& id,EditorAssets& assets);
    void DrawDebugger();
    engine::QuestAssetData m_asset;
    std::string m_path,m_pendingOpen,m_status;
    std::vector<int> m_previewProgress;
    int m_selectedObjective=0,m_selectedReward=0;
    bool m_previewActive=false,m_previewFailed=false,m_dirty=false;
};
