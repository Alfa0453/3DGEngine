#pragma once
#include "EditorAssets.h"
#include "EditorScene.h"
#include <engine/assets/CaveAsset.h>
#include <string>

class CaveTunnelPanel {
public:
    struct Result { bool build=false, remove=false, saved=false; std::string message; };
    void QueueOpen(std::string path){m_pendingOpen=std::move(path);}
    Result Draw(const EditorScene& scene,EditorAssets& assets,const std::string&root,bool*open);
    const engine::CaveAssetData& Cave()const{return m_cave;}
    const std::string& Path()const{return m_path;}
    bool IsDirty()const{return m_dirty;}
    bool SaveForShutdown(const std::string&root,std::string*error);
private:
    bool AssetCombo(const char*,std::string&,engine::AssetHandle&,EditorAssets&);
    void New(const std::string&root);void Preview()const;
    engine::CaveAssetData m_cave;std::string m_path,m_pendingOpen,m_splineName,m_status;bool m_dirty=true;
};
