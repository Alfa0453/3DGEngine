#pragma once

#include "EditorAssets.h"
#include <engine/assets/DialogueAsset.h>

#include <string>
#include <unordered_map>

class DialogueEditorPanel {
public:
    struct Result { bool assignSelected=false,saved=false;std::string message; };
    Result Draw(EditorAssets& assets,const std::string& root,bool* open);
    void QueueOpen(std::string path){m_pendingOpen=std::move(path);}
    const std::string& Path()const{return m_path;}
    bool IsDirty()const{return m_dirty;}
    bool SaveForShutdown(const std::string& root,std::string* error);
private:
    void New(const std::string& root);
    bool AssetCombo(const char* label,EditorAssets::Type type,std::string& path,engine::AssetHandle& id,EditorAssets& assets);
    void DrawFlowPreview();
    void DrawDebugger();
    engine::DialogueAssetData m_asset;
    std::string m_path,m_pendingOpen,m_status,m_previewNode;
    std::unordered_map<std::string,bool> m_previewFlags;
    int m_selectedSpeaker=0,m_selectedNode=0;
    bool m_previewActive=false,m_dirty=false;
};
