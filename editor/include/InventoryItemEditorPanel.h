#pragma once

#include "EditorAssets.h"
#include <engine/assets/ItemAsset.h>

#include <string>
#include <vector>

class InventoryItemEditorPanel {
public:
    struct Result {bool addSelected=false,saved=false;int count=1,maximumSlots=24;float maximumWeight=100.0f;std::string message;};
    Result Draw(EditorAssets& assets,const std::string& root,bool* open);
    void QueueOpen(std::string path){m_pendingOpen=std::move(path);}
    const engine::ItemAssetData& Asset()const{return m_asset;}
    const std::string& Path()const{return m_path;}
    bool IsDirty()const{return m_dirty;}
    bool SaveForShutdown(const std::string& root,std::string* error);
private:
    void New(const std::string& root);
    bool AssetCombo(const char* label,EditorAssets::Type type,std::string& path,engine::AssetHandle& id,EditorAssets& assets,bool allowNone=true);
    engine::ItemAssetData m_asset;
    std::string m_path,m_pendingOpen,m_status;
    int m_addCount=1,m_inventorySlots=24;
    float m_inventoryWeight=100.0f;
    std::vector<int> m_previewStacks;
    bool m_dirty=false;
};
